"""Compile add-ons separately from base games for the v8 catalogue.

The source entity owns display metadata and base-game association. Each download
keeps its own title/content identity, regional requirements and source evidence.
No compatibility claim is inferred from a matching title ID.
"""
import copy
import re

TYPES = ('expansion', 'content-pack', 'activation', 'bundle')
ID = re.compile(r'[a-z0-9][a-z0-9-]{0,62}')
CONTENT_ID = re.compile(r'[A-Z]{2}\d{4}-(?:CUSA|PPSA)\d{5}_\d{2}-[A-Z0-9_]{1,16}')


def text(value, cap, field, optional=False):
    if optional and value is None:
        return None
    if (not isinstance(value, str) or not value.strip() or len(value.encode()) >= cap or
            any(ord(c) < 32 or ord(c) == 127 for c in value)):
        raise ValueError('Invalid add-on ' + field)
    return value


def requirements(value, title_ids):
    if not isinstance(value, dict) or set(value) - {'edition', 'titleIds', 'minimumVersion', 'notes'}:
        raise ValueError('Invalid add-on requirements')
    ids = value.get('titleIds')
    if (not isinstance(ids, list) or not ids or len(ids) > 16 or
            any(not isinstance(tid, str) for tid in ids) or len(set(ids)) != len(ids) or
            set(ids) != set(title_ids)):
        raise ValueError('Add-on requirements must identify the selected base-game title IDs')
    out = {'titleIds': ids}
    for key, cap in [('edition', 256), ('minimumVersion', 32), ('notes', 1024)]:
        out[key] = text(value.get(key), cap, key, optional=True)
    return out


def compile_addons(games, base_releases, addons):
    from catalog import compile_catalog, title_platform
    if not isinstance(addons, list) or len(addons) > 8192:
        raise ValueError('Expected an add-on entity list')
    by_id = {g['id']: g for g in games}
    base_tids = {}
    for r in base_releases:
        base_tids.setdefault(r['gameId'], set()).add(r['titleId'])
    seen_ids = set(by_id)
    seen_releases = {r['id'] for r in base_releases}
    seen_urls = {r['url'] for r in base_releases}
    content_owners = {}
    rows = []
    for addon in addons:
        if not isinstance(addon, dict):
            raise ValueError('Expected an add-on entity')
        aid = addon.get('id')
        if not isinstance(aid, str) or not ID.fullmatch(aid) or aid in seen_ids:
            raise ValueError('Invalid or duplicate add-on identity')
        seen_ids.add(aid)
        base = by_id.get(addon.get('baseGameId'))
        if not base:
            raise ValueError('Add-on must refer to an existing base game')
        kind = addon.get('type')
        if kind not in TYPES:
            raise ValueError('Unsupported add-on type')
        name = text(addon.get('title'), 128, 'title')
        description = text(addon.get('description'), 4001, 'description')
        contents = addon.get('contents')
        if not isinstance(contents, list) or not 1 <= len(contents) <= 128:
            raise ValueError('Add-on contents must be listed')
        for item in contents:
            text(item, 256, 'contents')
        options = addon.get('releases')
        if not isinstance(options, list) or not options or any(not isinstance(r, dict) for r in options):
            raise ValueError('Every add-on needs a download option')
        tids = {r.get('titleId') for r in options}
        required = requirements(addon.get('requirements'), tids)
        platform = addon.get('platform')
        if platform not in ('ps4', 'ps5') or platform != base.get('platform', 'ps5'):
            raise ValueError('Add-on and base-game platforms differ')
        if not tids & base_tids.get(base['id'], set()):
            raise ValueError('At least one add-on edition must match the associated base game')
        for tid in tids:
            if title_platform(tid) != platform:
                raise ValueError('Add-on platform/title ID mismatch')
        # A base-game banner's title-ID evidence belongs to that base game, not
        # the DLC. Reuse URLs explicitly without copying a false DLC provenance.
        display = {k: copy.deepcopy(base.get(k)) for k in
                   ('genre', 'cover', 'hero', 'coverFallback', 'heroFallback', 'artworkLayout', 'publisher')}
        for key in ('cover', 'hero', 'coverFallback', 'heroFallback', 'artworkLayout', 'publisher'):
            if addon.get(key) is not None:
                display[key] = addon[key]
        display.update(id=aid, title=name, platform=platform, description=description,
                       tagline=None, releaseDate=addon.get('releaseDate'), addedAt=addon.get('addedAt'))
        for option in options:
            if option.get('id') in seen_releases or option.get('url') in seen_urls:
                raise ValueError('Duplicate add-on/base-game download identity or URL')
            seen_releases.add(option.get('id')); seen_urls.add(option.get('url'))
            proof = option.get('importEvidence', {})
            if not proof.get('identityEvidence') or not proof.get('identitySourceUrl'):
                raise ValueError('Add-on needs source evidence for its game/content association')
            cid = option.get('contentId')
            if cid is not None:
                if (not isinstance(cid, str) or not CONTENT_ID.fullmatch(cid) or
                        cid[7:16] != option['titleId']):
                    raise ValueError('Invalid add-on content identity')
                owner = content_owners.setdefault(cid, aid)
                if owner != aid:
                    raise ValueError('One content ID cannot identify different add-ons')
            elif not option.get('compression') and not (kind == 'activation' and proof.get('activationEvidence')):
                raise ValueError('A raw DLC package needs an identified content ID')
            if kind == 'activation' and not proof.get('activationEvidence'):
                raise ValueError('Activation packages require explicit content-type evidence')
            if option.get('format') not in ('PKG', 'FPKG', 'Unknown'):
                raise ValueError('DLC requires packages or a single archive of packages')
            if option.get('format') == 'FPKG' and not proof.get('formatEvidence'):
                raise ValueError('Do not infer a DLC FPKG subtype from the filename')
            selected = copy.deepcopy(option)
            selected.update(gameId=aid, platform=platform)
            own = copy.deepcopy(required)
            own['titleIds'] = [selected['titleId']]
            if 'requirements' in option:
                own = requirements(option['requirements'], [selected['titleId']])
            selected.update(addonId=aid, baseGameId=base['id'], baseGameTitle=base['title'],
                            addonType=kind, contents=contents, requirements=own)
            # Shared filename/range/provider validation remains identical to
            # base games. Only accurately identified PS5 DLC may use plain PKG.
            rows.extend(compile_catalog([display], [selected], _addon=True))
    if len(rows) + len(base_releases) > 8192:
        raise ValueError('Base-game and add-on options exceed the console capacity')
    return rows
