#!/usr/bin/env python3
"""Build the index.json that Orbit's "My NAS" source reads.

Run it on the machine that holds your game files (a Synology with the Python 3 package,
or any computer with the share mounted). It only reads file names, sizes and, for .zip
files, the list of entries inside; it never uploads anything.

    python3 tools/make-nas-index.py /volume1/web/orbit-3f9a7c1e

Put the folder under your web server (Synology Web Station) so that
https://your.domain/orbit-3f9a7c1e/index.json works, then enter that address in
Orbit's Sources screen. Entries use paths relative to index.json, so the folder can move.

Artwork is picked up by name from the "covers" sub-folder (see --covers):
    covers/PPSA01234_icon0.png   the game's icon (cover)
    covers/PPSA01234_pic1.png    the game's background (hero)
(.png, .jpg, .jpeg and .webp; the title ID is matched without regard to case).
For a game in a .zip, icon0.png and pic1.png found inside the archive (normally under
sce_sys/) are copied there automatically; use --no-extract to skip this and
--refresh-covers to replace images extracted earlier. Images you placed yourself are kept.

For a .zip holding a PS5 game, sce_sys/param.json gives the title, title ID and version
(--no-param to skip). For games without it (.exfat, .ffpfsc, other zips), --ai asks Gemini for
the name, a French description and the publisher; results are kept in .ai-cache.json and the key
comes from the GEMINI_API_KEYS environment variable (comma separated). The title ID and the
file-derived title are sent to Google. Never put keys in index.json or on the command line.

Optional titles.json next to index.json (or --titles) adjusts rows, keyed by the file's
path relative to the folder:

    {"Games/Some Game PPSA01234.exfat": {"title": "Some Game", "version": "1.02",
                                         "titleId": "PPSA01234", "cover": "covers/some.jpg"}}
"""
import argparse
import hashlib
import json
import os
import re
import sys
import time
import urllib.error
import urllib.request
import zipfile
from pathlib import Path
from urllib.parse import quote

TITLE_ID = re.compile(r"(?<![A-Za-z0-9])(PPSA|CUSA)(\d{5})(?!\d)", re.IGNORECASE)
IMAGE_FORMATS = {".exfat": "exFAT", ".ffpfsc": "FFPFSC", ".pkg": "FPKG"}
ARCHIVES = {".zip": "zip", ".7z": "7z", ".rar": "rar"}
SKIP_DIRS = {"@eaDir", "#recycle", "@Recycle", "@tmp"}


VERSION = re.compile(r"(?<![A-Za-z0-9])v(\d+(?:\.\d+)+)(?![A-Za-z0-9])", re.IGNORECASE)


def title_from(name: str) -> str:
    stem = Path(name).stem
    stem = TITLE_ID.sub(" ", stem)
    stem = VERSION.sub(" ", stem)
    stem = re.sub(r"[\[\(][^\]\)]*[\]\)]", " ", stem)
    stem = re.sub(r"_+", " ", stem)
    return re.sub(r"\s+", " ", stem).strip(" -.") or Path(name).stem


IMAGE_TYPES = (".png", ".jpg", ".jpeg", ".webp")


def find_artwork(root: Path, folder: str):
    """{lowercase file name: relative path} for the images under <root>/<folder>."""
    found = {}
    base = root / folder
    if base.is_dir():
        for item in base.iterdir():
            if item.is_file() and item.suffix.lower() in IMAGE_TYPES:
                found[item.name.lower()] = f"{folder}/{item.name}"
    return found


MAX_ART_BYTES = 20 * 1024 * 1024
ART_NAMES = {"icon0": "icon0.png", "pic1": "pic1.png"}


def extract_art(zip_path: Path, title_id: str, root: Path, folder: str, images: dict, refresh: bool):
    """Copy icon0.png / pic1.png out of a game .zip into <root>/<folder>/<TITLEID>_<kind>.png.

    Looks for sce_sys/icon0.png (or the shallowest icon0.png) inside the archive. Existing
    files are kept unless --refresh-covers is given. Returns the kinds that were written."""
    written = []
    try:
        with zipfile.ZipFile(zip_path) as archive:
            for kind, wanted in ART_NAMES.items():
                target_name = f"{title_id}_{kind}.png"
                if not refresh and target_name.lower() in images:
                    continue
                candidates = [
                    i for i in archive.infolist()
                    if not i.is_dir() and i.filename.lower().rsplit("/", 1)[-1] == wanted
                    and 0 < i.file_size <= MAX_ART_BYTES
                ]
                if not candidates:
                    continue
                candidates.sort(key=lambda i: ("sce_sys/" not in i.filename.lower(), i.filename.count("/")))
                with archive.open(candidates[0]) as source:
                    data = source.read(MAX_ART_BYTES + 1)
                if len(data) > MAX_ART_BYTES or not data.startswith(b"\x89PNG\r\n\x1a\n"):
                    continue
                target = root / folder / target_name
                target.parent.mkdir(parents=True, exist_ok=True)
                temporary = target.with_name(target.name + ".part")
                temporary.write_bytes(data)
                temporary.replace(target)
                images[target_name.lower()] = f"{folder}/{target_name}"
                written.append(kind)
    except (zipfile.BadZipFile, OSError, RuntimeError, NotImplementedError):
        pass
    return written


def artwork_for(images: dict, title_id: str, kind: str):
    for suffix in IMAGE_TYPES:
        hit = images.get(f"{title_id}_{kind}{suffix}".lower())
        if hit:
            return quote(hit, safe="/")
    return None


def sha256_of(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(8 * 1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def clean_text(value, limit: int) -> str:
    text = re.sub(r"[\x00-\x1f\x7f]+", " ", str(value or ""))
    return re.sub(r"\s+", " ", text).strip()[:limit]


def read_param(zip_path: Path):
    """Title, title ID and version from sce_sys/param.json inside a PS5 .zip, or None."""
    try:
        with zipfile.ZipFile(zip_path) as archive:
            found = [
                i for i in archive.infolist()
                if not i.is_dir() and i.filename.lower().rsplit("/", 1)[-1] == "param.json" and 0 < i.file_size <= 1 << 20
            ]
            if not found:
                return None
            found.sort(key=lambda i: ("sce_sys/" not in i.filename.lower(), i.filename.count("/")))
            data = json.loads(archive.read(found[0]).decode("utf-8-sig"))
    except (zipfile.BadZipFile, OSError, ValueError, RuntimeError, NotImplementedError):
        return None
    if not isinstance(data, dict):
        return None
    local = data.get("localizedParameters")
    local = local if isinstance(local, dict) else {}
    names = []
    for language in (local.get("defaultLanguage"), "en-US", "en-GB"):
        entry = local.get(language) if isinstance(language, str) else None
        if isinstance(entry, dict):
            names.append(entry.get("titleName"))
    names += [v.get("titleName") for v in local.values() if isinstance(v, dict)]
    title = next((clean_text(n, 120) for n in names if isinstance(n, str) and n.strip()), "")
    title_id = clean_text(data.get("titleId"), 9).upper()
    if not re.fullmatch(r"(PPSA|CUSA)\d{5}", title_id):
        title_id = ""
    version = clean_text(data.get("contentVersion"), 24)
    if not re.fullmatch(r"\d+(\.\d+){0,3}", version):
        version = ""
    if not (title or title_id or version):
        return None
    return {"title": title, "titleId": title_id, "version": version}


GEMINI_BASE = "https://generativelanguage.googleapis.com/v1beta"


def ai_prompt(title_id: str, hint: str) -> str:
    return (
        "Tu es un expert des jeux vidéo PlayStation (PS4 : CUSAxxxxx, PS5 : PPSAxxxxx).\n"
        f'- Title ID exact : "{title_id}"\n'
        f'- Titre brut ou nom de fichier : "{hint}"\n\n'
        "Fais attention au Title ID pour identifier le vrai jeu. Si tu n'es pas certain de le connaître, "
        "renvoie des chaînes vides plutôt que d'inventer.\n"
        "Renvoie STRICTEMENT un objet JSON valide :\n"
        '{"name": "Nom officiel propre du jeu (max 60 caractères)", '
        '"description": "Résumé en français (max 180 caractères)", '
        '"author": "Studio ou éditeur (max 60 caractères)"}'
    )


def ai_lookup(title_id: str, hint: str, keys, model: str, errors: list):
    """Ask Gemini for the game's name, description and publisher; None when it cannot say."""
    base = os.environ.get("GEMINI_BASE", GEMINI_BASE).rstrip("/")
    if not (base.startswith("https://") or base.startswith("http://127.0.0.1")):
        errors.append("GEMINI_BASE must be https")
        return None
    url = f"{base}/models/{model}:generateContent"
    body = json.dumps({
        "contents": [{"parts": [{"text": ai_prompt(title_id, hint)}]}],
        "generationConfig": {"responseMimeType": "application/json"},
    }).encode("utf-8")
    for key in keys:
        request = urllib.request.Request(url, body, {"Content-Type": "application/json", "x-goog-api-key": key})
        try:
            with urllib.request.urlopen(request, timeout=25) as reply:
                payload = json.loads(reply.read(1 << 20).decode("utf-8"))
        except urllib.error.HTTPError as problem:
            errors.append(f"HTTP {problem.code}")
            continue
        except (urllib.error.URLError, OSError, ValueError) as problem:
            errors.append(type(problem).__name__)
            continue
        try:
            text = payload["candidates"][0]["content"]["parts"][0]["text"]
            data = json.loads(re.sub(r"^```(?:json)?\s*|\s*```$", "", text.strip(), flags=re.IGNORECASE))
        except (KeyError, IndexError, TypeError, ValueError):
            errors.append("unreadable answer")
            continue
        if isinstance(data, dict):
            result = {
                "name": clean_text(data.get("name"), 60),
                "description": clean_text(data.get("description"), 190),
                "author": clean_text(data.get("author"), 60),
            }
            if result["description"] or result["author"]:
                return result
            errors.append("game not recognised")
            return None
    return None


def inspect_zip(path: Path):
    """(format, extracted bytes) from the entries of a .zip, or None to leave it unverified."""
    try:
        with zipfile.ZipFile(path) as archive:
            files = [i for i in archive.infolist() if not i.is_dir()]
    except (zipfile.BadZipFile, OSError):
        return None
    total = sum(i.file_size for i in files)
    if not files or total < 1:
        return None
    names = [i.filename.lower() for i in files]
    if len(files) == 1 and names[0].endswith(".exfat"):
        return "exFAT", total
    if len(files) == 1 and names[0].endswith(".ffpfsc"):
        return "FFPFSC", total
    return "Game folder", total


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("folder", type=Path, help="folder served by your web server")
    parser.add_argument("--output", type=Path, help="default: <folder>/index.json")
    parser.add_argument("--titles", type=Path, help="default: <folder>/titles.json if present")
    parser.add_argument("--covers", default="covers", help="sub-folder with <TITLEID>_icon0.png (cover) and <TITLEID>_pic1.png (background); default: covers")
    parser.add_argument("--no-extract", action="store_true", help="do not take icon0.png / pic1.png out of .zip files")
    parser.add_argument("--refresh-covers", action="store_true", help="overwrite covers already extracted from a .zip")
    parser.add_argument("--no-param", action="store_true", help="do not read sce_sys/param.json (title, title ID, version) from PS5 .zip files")
    parser.add_argument("--ai", action="store_true", help="ask Gemini for name, description and publisher of games without param.json (needs GEMINI_API_KEYS)")
    parser.add_argument("--ai-max", type=int, default=40, help="most Gemini requests per run (default 40)")
    parser.add_argument("--ai-model", default=os.environ.get("GEMINI_MODEL", "gemini-3.6-flash"))
    parser.add_argument("--sha256", action="store_true", help="also record each file's SHA-256 (slow on large files)")
    args = parser.parse_args()

    root = args.folder.resolve()
    if not root.is_dir():
        print(f"{root} is not a folder", file=sys.stderr)
        return 2
    output = args.output or root / "index.json"
    titles_path = args.titles or root / "titles.json"
    overrides = json.loads(titles_path.read_text("utf-8")) if titles_path.is_file() else {}

    images = find_artwork(root, args.covers)
    rows, skipped, extracted, notes = [], [], [], []
    keys = [k.strip() for k in re.split(r"[,\s]+", os.environ.get("GEMINI_API_KEYS", "")) if k.strip()]
    if args.ai and not keys:
        notes.append("--ai: no key in GEMINI_API_KEYS, AI step skipped")
    cache_path = root / ".ai-cache.json"
    try:
        cache = json.loads(cache_path.read_text("utf-8")) if cache_path.is_file() else {}
    except (OSError, ValueError):
        cache = {}
    cache = cache if isinstance(cache, dict) else {}
    cache_dirty, ai_calls, ai_done, ai_errors = False, 0, 0, []
    from_param = 0
    for path in sorted(root.rglob("*")):
        relative = path.relative_to(root)
        if not path.is_file() or path == output or path == titles_path:
            continue
        if any(part in SKIP_DIRS or part.startswith(".") for part in relative.parts):
            continue
        suffix = path.suffix.lower()
        if suffix not in IMAGE_FORMATS and suffix not in ARCHIVES:
            continue
        key = relative.as_posix()
        extra = overrides.get(key, {})
        param = read_param(path) if suffix == ".zip" and not args.no_param else None
        match = TITLE_ID.search(key)
        path_id = (match.group(1).upper() + match.group(2)) if match else ""
        title_id = (extra.get("titleId") or path_id or (param or {}).get("titleId") or "").upper()
        if param and param["titleId"] and path_id and param["titleId"] != path_id and not extra.get("titleId"):
            notes.append(f"{key}: param.json says {param['titleId']} but the path says {path_id}; param.json ignored")
            param = None
        if not re.fullmatch(r"(PPSA|CUSA)\d{5}", title_id):
            skipped.append((key, "no title ID (PPSAxxxxx or CUSAxxxxx) in its path; add it in titles.json"))
            continue
        size = path.stat().st_size
        if size < 1:
            skipped.append((key, "empty file"))
            continue
        row = {
            "id": "nas-" + hashlib.sha1(key.encode("utf-8")).hexdigest()[:12],
            "title": extra.get("title") or (param or {}).get("title") or title_from(path.name),
            "titleId": title_id,
            "filename": path.name,
            "path": quote(key, safe="/"),
            "sizeBytes": size,
        }
        if suffix in ARCHIVES:
            row["compression"] = ARCHIVES[suffix]
            inner = inspect_zip(path) if suffix == ".zip" else None
            if extra.get("format"):
                row["format"] = extra["format"]
            if inner:
                row["format"], row["extractedBytes"] = extra.get("format") or inner[0], inner[1]
                if row["format"] == "Game folder" and not title_id.startswith("PPSA"):
                    skipped.append((key, "a PS4 archive must hold a single .pkg; leave it out or rename"))
                    continue
            else:
                row["format"], row["archiveContents"] = "Unknown", "unverified"
        else:
            row["format"] = extra.get("format") or IMAGE_FORMATS[suffix]
        if args.sha256:
            row["sha256"] = sha256_of(path)
        found = VERSION.search(path.name)
        if found:
            row["version"] = found.group(1)
        if param:
            from_param += 1
            if param["version"]:
                row["version"] = param["version"]
        if suffix == ".zip" and not args.no_extract:
            for kind in extract_art(path, title_id, root, args.covers, images, args.refresh_covers):
                extracted.append(f"{title_id}_{kind}.png")
        for field, kind in (("cover", "icon0"), ("hero", "pic1")):
            hit = artwork_for(images, title_id, kind)
            if hit:
                row[field] = hit
        if args.ai and keys and not param and not extra.get("description"):
            info = cache.get(title_id) if isinstance(cache.get(title_id), dict) else None
            if info and info.get("unknown"):
                # Not recognised last time: ask again only after a week.
                info = None if time.time() - float(info.get("at", 0)) > 7 * 86400 else {}
            elif info is None and ai_calls < args.ai_max and len(ai_errors) < 3:
                ai_calls += 1
                before = len(ai_errors)
                found_info = ai_lookup(title_id, title_from(path.name), keys, args.ai_model, ai_errors)
                if found_info:
                    ai_errors.clear()
                    info = cache[title_id] = found_info
                    cache_dirty = True
                elif ai_errors[before:] == ["game not recognised"]:
                    ai_errors.pop()
                    cache[title_id] = {"unknown": True, "at": time.time()}
                    cache_dirty = True
            if info:
                ai_done += 1
                if info.get("description"):
                    row["description"] = info["description"]
                if info.get("author"):
                    row["publisher"] = info["author"]
                if info.get("name") and not extra.get("title"):
                    row["title"] = info["name"]
        for field in ("version", "description", "publisher", "genre", "releaseDate", "cover", "hero", "gameId"):
            if extra.get(field):
                row[field] = extra[field]
        rows.append(row)

    # Written aside and renamed, so Orbit never reads a half-written index.
    partial = output.with_name(output.name + ".part")
    partial.write_text(json.dumps({"schemaVersion": 1, "releases": rows}, indent=2, ensure_ascii=False) + "\n", "utf-8")
    partial.replace(output)
    print(f"{len(rows)} file(s) listed in {output}")
    if cache_dirty:
        try:
            temporary = cache_path.with_name(cache_path.name + ".part")
            temporary.write_text(json.dumps(cache, ensure_ascii=False, indent=1), "utf-8")
            temporary.replace(cache_path)
        except OSError:
            notes.append("could not save .ai-cache.json (folder not writable?)")
    if from_param:
        print(f"  {from_param} game(s) described by their param.json")
    if args.ai and keys:
        print(f"  AI: {ai_done} game(s) completed, {ai_calls} request(s) this run" + (f" (last problem: {ai_errors[-1]})" if ai_errors else ""))
    for note in notes:
        print(f"  note: {note}")
    if extracted:
        print(f"  {len(extracted)} image(s) extracted from .zip files into {args.covers}/")
    for key, reason in skipped:
        print(f"  skipped {key}: {reason}")
    if len(rows) > 512:
        print("Orbit reads at most 512 entries from one index.", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
