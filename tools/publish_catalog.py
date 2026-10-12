"""Prepare the public catalogue feed. Does not commit, push, build an ELF, or fetch URLs."""
import argparse
import json
from pathlib import Path
from catalog import ROOT, compile_catalog, read, now
from catalog_crypto import FEED, FEED_VERSION, unseal, source_envelopes, sealed_bytes


def compatible_rows(rows, version=3):
    """Keep archives and new browser providers out of feeds older applications consume."""
    return [row for row in rows if 'compression' not in row and row['sourceId'] in ('archive', 'vikingfile') and
            (version >= 5 or row.get('platform', 'ps5') == 'ps5') and
            (version >= 4 or row['format'] in ('FFPFSC', 'exFAT'))]


def prepare(public_repo):
    public_repo = Path(public_repo).resolve()
    if not (public_repo / '.git').exists():
        raise ValueError('Expected an existing checkout of the public project repository')
    games = read('games.json')
    for game in games:
        if not game.get('addedAt'):
            game['addedAt'] = now()
    source_releases = read('releases.json')
    rows = compile_catalog(games, source_releases)
    target = public_repo / 'catalogue-v6.enc'
    ps4_target = public_repo / 'catalogue-v5.enc'
    ps5_target = public_repo / 'catalogue-v4.enc'
    compatible = public_repo / 'catalogue-v3.enc'
    older_rows = compatible_rows(rows)
    if not older_rows:
        raise ValueError('The compatibility feed needs at least one supported option')
    legacy = [public_repo / name for name in ('catalogue.json', 'catalogue-v2.json')]
    revision_path = ROOT / 'catalog/revision.json'
    revision = json.loads(revision_path.read_text())['revision']
    if type(revision) is not int or not 1 <= revision < 2147483647:
        raise ValueError('Invalid local catalogue revision')
    changed = False
    for current in [*legacy, compatible, ps5_target, ps4_target, target]:
        if current.exists():
            previous = json.loads(unseal(current.read_bytes(), FEED) if current.suffix == '.enc' else current.read_bytes())
            if previous.get('schemaVersion') != 1 or type(previous.get('revision')) is not int or not 1 <= previous['revision'] < 2147483647 or not isinstance(previous.get('releases'), list):
                raise ValueError(f'Existing public feed {current.name} is invalid; inspect it before publishing')
            revision = max(revision, previous['revision'])
            releases = ([{k: v for k, v in row.items() if k != 'browserUrl'}
                         for row in older_rows if row['sourceId'] == 'archive']
                        if current.name == 'catalogue.json' else
                        rows if current == target else
                        compatible_rows(rows, 5) if current == ps4_target else
                        compatible_rows(rows, 4) if current == ps5_target else older_rows)
            changed |= releases != previous['releases']
            if current.suffix == '.enc':
                expected_version = FEED_VERSION if current == target else 5 if current == ps4_target else 4 if current == ps5_target else 3
                changed |= previous.get('feedVersion') != expected_version
    if changed:
        revision += 1
    if revision >= 2147483647:
        raise ValueError('Catalogue revision limit reached')
    writes = source_envelopes(ROOT / 'catalog', games, source_releases, rows, revision)
    # The public feed and bundled fallback are byte-for-byte the same envelope.
    encoded = next(data for path, data in writes if path.name == 'catalog.enc')
    writes.append((target, encoded))
    writes.append((ps4_target, sealed_bytes(
        {'schemaVersion': 1, 'feedVersion': 5, 'revision': revision, 'releases': compatible_rows(rows, 5)}, FEED, ps4_target)))
    writes.append((ps5_target, sealed_bytes(
        {'schemaVersion': 1, 'feedVersion': 4, 'revision': revision, 'releases': compatible_rows(rows, 4)}, FEED, ps5_target)))
    writes.append((compatible, sealed_bytes(
        {'schemaVersion': 1, 'feedVersion': 3, 'revision': revision, 'releases': older_rows}, FEED, compatible)))
    # All validation finishes before writing. Atomic replacements leave reviewable files.
    for path, data in [*writes,
                       (ROOT / 'catalog/games.json', (json.dumps(games, ensure_ascii=False, indent=2) + '\n').encode()),
                       (ROOT / 'catalog/catalog.json', (json.dumps(rows, ensure_ascii=False, indent=2) + '\n').encode()),
                       (revision_path, (json.dumps({'revision': revision}) + '\n').encode())]:
        temporary = path.with_suffix('.publish-tmp')
        temporary.write_bytes(data)
        temporary.replace(path)
    # Older clients retain their bundled/cached catalogue. Keeping these files
    # public would also keep a plaintext copy of the encrypted feed available.
    for path in legacy:
        path.unlink(missing_ok=True)
    return revision, len({row['gameId'] for row in rows}), len(rows)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--public-repo', required=True, type=Path)
    args = parser.parse_args()
    revision, games, options = prepare(args.public_repo)
    print(f'Prepared catalogue revision {revision}: {games} games, {options} validated download options.')
    print('Prepared catalogue-v6.enc and v5/v4/v3 compatibility feeds locally. Nothing was pushed.')
