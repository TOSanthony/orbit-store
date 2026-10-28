"""Prepare the public catalogue feed. Does not commit, push, build an ELF, or fetch URLs."""
import argparse
import json
from pathlib import Path
from catalog import ROOT, compile_catalog, read, now
from catalog_crypto import FEED, FEED_VERSION, unseal, source_envelopes, sealed_bytes, encode
from addons import compile_addons

LEGACY_MAX_RELEASES = 4096
LEGACY_MAX_BYTES = 8 * 1024 * 1024


def fits_legacy_capacity(envelope, encoded_size=None):
    return (len(envelope['releases']) <= LEGACY_MAX_RELEASES and
            (len(encode(envelope)) if encoded_size is None else encoded_size) <= LEGACY_MAX_BYTES)


def compatible_rows(rows, version=3):
    """Keep archives and new browser providers out of feeds older applications consume."""
    rows = [row for row in rows if not row.get('addonId')]
    if version >= 7:
        return rows
    if version >= 6:
        return [row for row in rows if row['sourceId'] in ('archive', 'vikingfile', 'akirabox', 'datanodes')]
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
    addon_path = ROOT / 'catalog/addons.json'
    addons = json.loads(addon_path.read_text()) if addon_path.exists() else []
    addon_rows = compile_addons(games, source_releases, addons)
    # Released v7/v8 readers require the filename suffix to match compression.
    # Local readers can now handle identified RAR files named .7z, but putting
    # one in those feeds would make older clients reject the entire update.
    if any(row.get('compression') == 'rar' and row['filename'].lower().endswith('.7z')
           for row in [*rows, *addon_rows]):
        raise ValueError('RAR files named .7z need a new feed generation or an explicit '
                         'compatibility export before publication to released clients')
    target = public_repo / 'catalogue-v8.enc'
    game_target = public_repo / 'catalogue-v7.enc'
    archive_target = public_repo / 'catalogue-v6.enc'
    ps4_target = public_repo / 'catalogue-v5.enc'
    ps5_target = public_repo / 'catalogue-v4.enc'
    compatible = public_repo / 'catalogue-v3.enc'
    compatibility_targets = {3: compatible, 4: ps5_target, 5: ps4_target, 6: archive_target, 7: game_target}
    proposed = {version: compatible_rows(rows, version) for version in compatibility_targets}
    older_rows = compatible_rows(rows)
    if not older_rows:
        raise ValueError('The compatibility feed needs at least one supported option')
    legacy = [public_repo / name for name in ('catalogue.json', 'catalogue-v2.json')]
    revision_path = ROOT / 'catalog/revision.json'
    revision = json.loads(revision_path.read_text())['revision']
    if type(revision) is not int or not 1 <= revision < 2147483647:
        raise ValueError('Invalid local catalogue revision')
    existing = {}
    for current in [*legacy, *compatibility_targets.values(), target]:
        if current.exists():
            plain = unseal(current.read_bytes(), FEED) if current.suffix == '.enc' else current.read_bytes()
            previous = json.loads(plain)
            if not isinstance(previous, dict) or previous.get('schemaVersion') != 1 or type(previous.get('revision')) is not int or not 1 <= previous['revision'] < 2147483647 or not isinstance(previous.get('releases'), list):
                raise ValueError(f'Existing public feed {current.name} is invalid; inspect it before publishing')
            existing[current] = (previous, len(plain))
            revision = max(revision, previous['revision'])

    # Keep the last working feed byte-for-byte for a generation that can no
    # longer fit the catalogue. Do not truncate options or block newer clients.
    # A missing, damaged or incompatible previous feed still blocks publication.
    preserved = set()
    for version, path in compatibility_targets.items():
        if version >= 7:
            continue # v7 has the same 8,192-option / 16 MiB capacity as v8.
        envelope = dict(schemaVersion=1, feedVersion=version, revision=2147483646,
                        releases=proposed[version])
        if fits_legacy_capacity(envelope):
            continue
        previous, size = existing.get(path, ({}, 0))
        old_rows = previous.get('releases', [])
        if (previous.get('feedVersion') != version or not old_rows or
                not fits_legacy_capacity(previous, size) or
                any(not isinstance(row, dict) or 'sourceId' not in row or 'format' not in row
                    for row in old_rows) or compatible_rows(old_rows, version) != old_rows):
            raise ValueError(f'Catalogue v{version} exceeds older app capacity and has no valid previous feed to preserve')
        preserved.add(path)

    changed = False
    for current, (previous, _) in existing.items():
        if current not in preserved:
            releases = ([{k: v for k, v in row.items() if k != 'browserUrl'}
                         for row in older_rows if row['sourceId'] == 'archive']
                        if current.name == 'catalogue.json' else
                        rows if current in (target, game_target) else
                        compatible_rows(rows, 6) if current == archive_target else
                        compatible_rows(rows, 5) if current == ps4_target else
                        compatible_rows(rows, 4) if current == ps5_target else older_rows)
            changed |= releases != previous['releases']
            if current == target:
                changed |= previous.get('addons', []) != addon_rows
            if current.suffix == '.enc':
                expected_version = FEED_VERSION if current == target else 7 if current == game_target else 6 if current == archive_target else 5 if current == ps4_target else 4 if current == ps5_target else 3
                changed |= previous.get('feedVersion') != expected_version
    # The first add-on publication must not share a revision with a base-only
    # v8 bundle already used locally. Existing base-only generations stay safe.
    if addon_rows and target not in existing:
        changed = True
    if changed:
        revision += 1
    if revision >= 2147483647:
        raise ValueError('Catalogue revision limit reached')
    writes = source_envelopes(ROOT / 'catalog', games, source_releases, rows, revision, addons, addon_rows)
    # The public feed and bundled fallback are byte-for-byte the same envelope.
    encoded = next(data for path, data in writes if path.name == 'catalog.enc')
    writes.append((target, encoded))
    for version, path in compatibility_targets.items():
        if path not in preserved:
            writes.append((path, sealed_bytes(
                {'schemaVersion': 1, 'feedVersion': version, 'revision': revision,
                 'releases': proposed[version]}, FEED, path)))
    # All validation finishes before writing. Atomic replacements leave reviewable files.
    for path, data in [*writes,
                       (ROOT / 'catalog/games.json', (json.dumps(games, ensure_ascii=False, indent=2) + '\n').encode()),
                       (ROOT / 'catalog/catalog.json', (json.dumps(rows, ensure_ascii=False, indent=2) + '\n').encode()),
                       (ROOT / 'catalog/addons-catalog.json', (json.dumps(addon_rows, ensure_ascii=False, indent=2) + '\n').encode()),
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
    print('Prepared catalogue-v8.enc with separate add-ons; v7 and older receive only compatible base games. Nothing was pushed.')
