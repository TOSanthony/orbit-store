"""Import reviewed, uncompressed PS4 candidates without changing PS5 editions.

Consumes the separate research JSON offline. Incomplete candidates remain in that
file with a reported reason; no network requests, public-feed writes or publishing.
"""
import argparse
import copy
import hashlib
import json
import re
from datetime import date
from pathlib import Path
from catalog import ROOT, check_region, compile_catalog, content_id, https, now, read, region_of, save, title_platform, STORE_PRODUCT


def full_metadata(record):
    """Keep research candidates out of the app until their game page is complete."""
    if record.get('fullMetadataReady') is not True:
        raise ValueError('Full PS4 metadata is still pending')
    tid = record['titleId']
    evidence = record.get('metadataEvidence', {})
    if (evidence.get('platform') != 'ps4' or evidence.get('titleId') != tid or
            not evidence.get('checkedAt') or not https(evidence.get('sourceUrl', ''))):
        raise ValueError('Missing PS4 metadata provenance')
    for field, evidence_key in (('releaseDate', 'releaseDateEvidence'),
                                ('publisher', 'publisherEvidence'), ('genre', 'genreEvidence')):
        proof = evidence.get(evidence_key, {})
        if (not isinstance(record.get(field), str) or not record[field].strip() or
                proof.get('value') != record[field] or not proof.get('checkedAt') or
                not https(proof.get('sourceUrl', ''))):
            raise ValueError('Missing verified ' + field)
    if evidence['releaseDateEvidence'].get('platform') != 'ps4':
        raise ValueError('Release date must describe the PS4 edition')
    released = date.fromisoformat(record['releaseDate'])
    if released < date(2013, 11, 15):
        raise ValueError('Release date predates PS4')
    if (not isinstance(record.get('tagline'), str) or not 8 <= len(record['tagline'].strip()) <= 240 or
            not isinstance(record.get('description'), str) or not 32 <= len(record['description'].strip()) <= 4000 or
            not evidence.get('descriptionBasis')):
        raise ValueError('Missing original one-liner or description with supporting sources')
    banner = record.get('bannerEvidence', {})
    width, height = banner.get('width'), banner.get('height')
    if (record.get('artworkLayout') != 'wide' or record.get('hero') == record.get('cover') or
            banner.get('url') != record.get('hero') or not https(record.get('hero', '')) or
            banner.get('titleId') != tid or banner.get('role') != 'banner' or
            not banner.get('platformProvenance') or not banner.get('checkedAt') or
            not https(banner.get('sourceUrl', '')) or banner.get('httpStatus') not in (200, 206) or
            banner.get('contentType') not in ('image/jpeg', 'image/png', 'image/webp') or
            type(width) is not int or type(height) is not int or
            width < 960 or height < 400 or not 1.5 <= width / height <= 3.5):
        raise ValueError('Missing checked, title-matched wide banner')
    return evidence, banner


def stage(research, games, releases, snapshot_hash):
    games, releases = copy.deepcopy(games), copy.deepcopy(releases)
    by_id = {g['id']: g for g in games}
    by_tid = {r['titleId']: by_id[r['gameId']] for r in releases}
    urls, ids = {r['url'] for r in releases}, {r['id'] for r in releases}
    added, pending = [], []
    for candidate in research.get('candidates', []):
        if candidate.get('integrationHold'):
            pending.append({'titleId': candidate.get('titleId'),
                            'reason': 'Research review hold: ' + str(candidate['integrationHold'])})
            continue
        record = candidate.get('importRecord')
        if not record:
            pending.append({'titleId': candidate.get('titleId'), 'reason': 'Import evidence incomplete'})
            continue
        if record.get('url') in urls:
            continue
        try:
            tid, rid = record['titleId'], record['id']
            if title_platform(tid) != 'ps4' or record.get('platform') != 'ps4' or rid in ids:
                raise ValueError('Expected a unique PS4 package identity')
            if record.get('format') not in ('FPKG', 'PKG'):
                raise ValueError('Only single raw packages are included')
            evidence, art = record.get('importEvidence', {}), record.get('artworkEvidence', {})
            if not evidence.get('identityEvidence') or not https(evidence.get('identitySourceUrl', '')):
                raise ValueError('Missing title identity evidence')
            if record['format'] == 'FPKG' and not evidence.get('formatEvidence'):
                raise ValueError('Missing FPKG source evidence')
            if (art.get('titleId') != tid or art.get('url') != record.get('cover') or
                    art.get('httpStatus') != 200 or not art.get('checkedAt') or
                    art.get('contentType') not in ('image/jpeg', 'image/png', 'image/webp') or
                    not https(art.get('sourceUrl', ''))):
                raise ValueError('Missing checked title-matched, native-compatible cover')
            metadata, banner = full_metadata(record)
            verification = record.get('verification', {})
            if record['sourceId'] == 'archive':
                count = verification.get('probeBytes', 0)
                if (type(count) is not int or not 1 <= count <= 32 or
                        verification.get('bodyReadBytes') != count or
                        verification.get('contentRange') != f"bytes 0-{count-1}/{record['sizeBytes']}" or
                        any(s in verification.get('contentType', '').lower() for s in ('text/', 'json', 'xml'))):
                    raise ValueError('Missing bounded file-range evidence')
            game = by_tid.get(tid) or by_id.get(record['gameId'])
            fresh = game is None
            if fresh:
                if not re.fullmatch(r'[a-z0-9][a-z0-9-]{0,62}', record['gameId']):
                    raise ValueError('Invalid PS4 game ID')
                game = {k: record.get(k) for k in ('title', 'genre', 'tagline', 'description', 'publisher',
                        'releaseDate', 'cover', 'hero', 'artworkLayout', 'metadataUrl')}
                # Keep the original genre evidence, but use the same separator
                # as existing catalogue rows so browser/native translations work.
                game['genre'] = ' / '.join(part.strip() for part in game['genre'].split(','))
                game.update(id=record['gameId'], platform='ps4', addedAt=now(), metadata={
                    'provider': art.get('provider'), 'url': metadata['sourceUrl'], 'titleId': tid,
                    'sourceTitle': record['title'], 'fetchedAt': metadata['checkedAt'],
                    'ps4Evidence': metadata, 'artworkChecks': [art, banner]})
                for field in ('coverFallback', 'heroFallback'):
                    fallback = record.get(field)
                    proof = record.get(field + 'Evidence', {})
                    if fallback and https(fallback) and proof.get('url') == fallback and proof.get('titleId') == tid:
                        if proof.get('httpStatus') in (200, 206) and proof.get('checkedAt'):
                            game[field] = fallback
            if game.get('platform') != 'ps4':
                raise ValueError('Never merge a PS4 option into a PS5 edition')
            release = {k: record[k] for k in ('id', 'titleId', 'sourceId', 'provider', 'format',
                       'filename', 'sizeBytes', 'url', 'verification')}
            release.update(gameId=game['id'], platform='ps4', version=record.get('version'), sha256='',
                importEvidence={**evidence, 'researchSha256': snapshot_hash, 'sourceRegion': record.get('region')})
            for key in ('browserUrl', 'notes'):
                if record.get(key): release[key] = record[key]
            # Region claims in filenames stay in provenance until a matching
            # official content ID supports the region shown in the filter.
            page = record.get('metadataUrl', '')
            match = re.fullmatch(STORE_PRODUCT + '(' + content_id(tid) + ')/?', page)
            if match:
                region = region_of(match[2])
                if region:
                    release.update(region=region[0], regionEvidence=[dict(provider='PlayStation Store',
                        contentId=match[1], sourceUrl=page)])
                    check_region(release)
            # Validate this record before adding it to either collection.
            compile_catalog([game], [release])
            if fresh:
                games.append(game); by_id[game['id']] = game
            by_tid[tid] = game
            releases.append(release); urls.add(release['url']); ids.add(rid)
            added.append(rid)
        except (KeyError, TypeError, ValueError) as error:
            pending.append({'titleId': record.get('titleId'), 'reason': str(error)})
    compile_catalog(games, releases)
    return games, releases, added, pending


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('research', type=Path)
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    raw = args.research.read_bytes()
    games, releases, added, pending = stage(json.loads(raw), read('games.json'), read('releases.json'),
                                          hashlib.sha256(raw).hexdigest())
    if args.apply:
        save('games.json', games)
        save('releases.json', releases)
    print(json.dumps({'applied': args.apply, 'addedOptions': len(added),
        'ps4Games': sum(g.get('platform') == 'ps4' for g in games), 'pending': pending}, indent=2))


if __name__ == '__main__':
    main()
