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
For a game in a .zip, icon0.png and the background (pic0.png if present, else pic1.png) found inside the archive (normally under
sce_sys/) are copied there automatically; use --no-extract to skip this and
--refresh-covers to replace images extracted earlier. Images you placed yourself are kept.

For a .zip holding a PS5 game, sce_sys/param.json gives the title, title ID and version
(--no-param to skip). With --ai, Gemini fills what is still missing for every game: description
(French), publisher, genre, tagline and release date; it only renames games whose title came from
the file name. Results are kept in .ai-cache.json and the key comes from the GEMINI_API_KEYS
environment variable (comma separated). The title ID and the title are sent to Google. Never put
keys in index.json or on the command line. addedAt is the file's modification date.

Optional titles.json next to index.json (or --titles) adjusts rows, keyed by the file's
path relative to the folder:

    {"Games/Some Game PPSA01234.exfat": {"title": "Some Game", "version": "1.02",
                                         "titleId": "PPSA01234", "cover": "covers/some.jpg"}}
"""
import argparse
import hashlib
import html as htmllib
import json
import os
import math
import re
import struct
import sys
import time
import zlib
import urllib.error
import urllib.request
import zipfile
from datetime import datetime, timezone
from pathlib import Path
from urllib.parse import quote

TITLE_ID = re.compile(r"(?<![A-Za-z0-9])(PPSA|CUSA)(\d{5})(?!\d)", re.IGNORECASE)
IMAGE_FORMATS = {".exfat": "exFAT", ".ffpfsc": "FFPFSC", ".pkg": "FPKG"}
ARCHIVES = {".zip": "zip", ".7z": "7z", ".rar": "rar"}
SKIP_DIRS = {"@eaDir", "#recycle", "@Recycle", "@tmp"}


PACK_FOLDERS = {"dlc": "DLC", "updates": "UPDATES", "update": "UPDATES", "mises a jour": "UPDATES",
                "apps": "APPS", "applications": "APPS"}


def pack_kind(relative: Path) -> str:
    """"DLC", "UPDATES" or "APPS" when a folder of the path is called dlc / updates / apps (any case), else "".

    packages/ps5/dlc/PPSA01342/*.pkg  ->  DLC;  packages/ps5/updates/PPSA01342/*.pkg  ->  UPDATES;
    packages/ps5/apps/zip/MyApp.zip  ->  APPS."""
    for part in relative.parts[:-1]:
        kind = PACK_FOLDERS.get(part.strip().lower())
        if kind:
            return kind
    return ""


VERSION = re.compile(r"(?<![A-Za-z0-9])v(\d+(?:\.\d+)+)(?![A-Za-z0-9])", re.IGNORECASE)


def title_from(name: str) -> str:
    stem = Path(name).stem
    stem = TITLE_ID.sub(" ", stem)
    stem = VERSION.sub(" ", stem)
    stem = re.sub(r"[\[\(][^\]\)]*[\]\)]", " ", stem)
    stem = re.sub(r"_+", " ", stem)
    return re.sub(r"\s+", " ", stem).strip(" -.") or Path(name).stem


IMAGE_TYPES = (".jpg", ".png", ".jpeg", ".webp")   # the small optimised copy wins over the PNG


def find_artwork(root: Path, folder: str):
    """{lowercase file name: relative path} for the images under <root>/<folder>."""
    found = {}
    base = root / folder
    if base.is_dir():
        for item in base.iterdir():
            if item.is_file() and item.suffix.lower() in IMAGE_TYPES:
                found[item.name.lower()] = f"{folder}/{item.name}"
    return found


def walk_files(root: Path):
    """Every file under root, following symbolic links to folders (so games kept on another
    volume can be linked into the web folder). A folder is visited once, which stops loops."""
    seen = set()
    for folder, subfolders, names in os.walk(root, followlinks=True):
        real = os.path.realpath(folder)
        if real in seen:
            subfolders[:] = []
            continue
        seen.add(real)
        subfolders[:] = sorted(d for d in subfolders if d not in SKIP_DIRS and not d.startswith("."))
        for name in sorted(names):
            yield Path(folder) / name


MAX_ART_BYTES = 20 * 1024 * 1024
MADE_MARK = b"orbit-store:optimized"   # written into the JPEGs this script makes, to tell them from yours


def custom_art(root: Path, images: dict, title_id: str, kind: str) -> bool:
    """True when covers/ holds your own <TITLEID>_<kind>.jpg / .jpeg / .webp (not one made by this
    script): it is then used as it is, and never extracted over or rewritten."""
    for suffix in (".jpg", ".jpeg", ".webp"):
        name = f"{title_id}_{kind}{suffix}".lower()
        rel = images.get(name)
        if not rel:
            continue
        if suffix == ".webp":
            return True
        try:
            with (root / rel).open("rb") as handle:
                if MADE_MARK not in handle.read(4096):
                    return True
        except OSError:
            continue
    return False


def mark_jpeg(data: bytes) -> bytes:
    """Insert a JPEG comment saying this script made the file (after the JFIF header when present)."""
    if data[:2] != b"\xff\xd8":
        return data
    at = 2
    if data[2:4] == b"\xff\xe0":
        at = 4 + int.from_bytes(data[4:6], "big")
    comment = b"\xff\xfe" + (len(MADE_MARK) + 2).to_bytes(2, "big") + MADE_MARK
    return data[:at] + comment + data[at:]
ART_NAMES = {"icon0": ("icon0.png",), "pic1": ("pic0.png", "pic1.png")}   # background: pic0 first, else pic1


def extract_art(zip_path: Path, title_id: str, root: Path, folder: str, images: dict, refresh: bool):
    """Copy icon0.png / pic1.png out of a game .zip into <root>/<folder>/<TITLEID>_<kind>.png.

    Looks for sce_sys/icon0.png (or the shallowest icon0.png) inside the archive. Existing
    files are kept unless --refresh-covers is given, and an image of yours (.jpg / .webp in
    covers/) is never replaced. Returns the kinds that were written."""
    written = []
    try:
        with zipfile.ZipFile(zip_path) as archive:
            for kind, wanted in ART_NAMES.items():
                target_name = f"{title_id}_{kind}.png"
                if custom_art(root, images, title_id, kind):
                    continue                             # your own jpg / webp: nothing to extract
                if not refresh and target_name.lower() in images:
                    continue
                candidates = []
                for name in wanted:                      # first name that exists in the archive wins
                    candidates = [
                        i for i in archive.infolist()
                        if not i.is_dir() and i.filename.lower().rsplit("/", 1)[-1] == name
                        and 0 < i.file_size <= MAX_ART_BYTES
                    ]
                    if candidates:
                        break
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


try:
    from PIL import Image          # optional: only used to shrink the extracted pictures
except ImportError:                # pragma: no cover
    Image = None
SIZE_LIMITS = {"icon0": (512, 512), "pic1": (1920, 1080)}


def optimize_art(root: Path, images: dict, title_id: str, kind: str, refresh: bool):
    """Write <TITLEID>_<kind>.jpg, a small copy of the extracted PNG (about 10x lighter), so the
    console downloads 300 KB instead of several MB. The PNG stays; returns True when written."""
    if Image is None or custom_art(root, images, title_id, kind):
        return False
    source_rel = images.get(f"{title_id}_{kind}.png".lower())
    if not source_rel:
        return False
    source = root / source_rel
    target = source.with_suffix(".jpg")
    try:
        if not refresh and target.exists() and target.stat().st_mtime >= source.stat().st_mtime:
            images[target.name.lower()] = f"{source_rel.rsplit('/', 1)[0]}/{target.name}" if "/" in source_rel else target.name
            return False
        with Image.open(source) as picture:
            picture.load()
            if picture.width * picture.height > 64_000_000:
                return False
            if picture.mode in ("RGBA", "LA", "P"):
                picture = picture.convert("RGBA")
                flat = Image.new("RGB", picture.size, (20, 22, 30))
                flat.paste(picture, mask=picture.getchannel("A"))
                picture = flat
            else:
                picture = picture.convert("RGB")
            picture.thumbnail(SIZE_LIMITS[kind], Image.LANCZOS)
            temporary = target.with_name(target.name + ".part")
            picture.save(temporary, format="JPEG", quality=86, optimize=True)
            temporary.write_bytes(mark_jpeg(temporary.read_bytes()))
            temporary.replace(target)
        images[target.name.lower()] = f"{source_rel.rsplit('/', 1)[0]}/{target.name}" if "/" in source_rel else target.name
        return True
    except (OSError, ValueError, Image.DecompressionBombError):
        return False


def artwork_for(images: dict, title_id: str, kind: str):
    for suffix in (".jpg", ".jpeg", ".webp", ".png"):          # small copies and your own files before the big PNG
        hit = images.get(f"{title_id}_{kind}{suffix}".lower())
        if hit:
            return quote(hit, safe="/")
    return None


# ---- prosperopatches.com: title, publisher, region and artwork for a title ID -------------------
PROSPERO_REGIONS = {"EU": "EUR", "EUR": "EUR", "UK": "EUR", "US": "USA", "USA": "USA", "JP": "JPN", "JPN": "JPN",
                    "ASIA": "ASIA", "HK": "ASIA", "KR": "ASIA", "TW": "ASIA", "CN": "ASIA"}
PROSPERO_AGENT = "Mozilla/5.0 (compatible; orbit-nas-index/1.0)"


def http_get(url: str, limit: int, accept: str = "*/*"):
    """(bytes, '') or (None, reason). Only https (or a local test server), size-limited."""
    if not (url.startswith("https://") or url.startswith("http://127.0.0.1")):
        return None, "not https"
    request = urllib.request.Request(url, headers={"User-Agent": PROSPERO_AGENT, "Accept": accept})
    try:
        with urllib.request.urlopen(request, timeout=25) as reply:
            data = reply.read(limit + 1)
    except urllib.error.HTTPError as problem:
        return None, f"HTTP {problem.code}"
    except (urllib.error.URLError, OSError, ValueError) as problem:
        return None, type(problem).__name__
    return (data, "") if len(data) <= limit else (None, "too large")


def _text(fragment: str) -> str:
    return " ".join(htmllib.unescape(re.sub(r"<[^>]+>", " ", fragment)).split())


def prospero_parse(page: str, title_id: str, cdn: str):
    """Read a title page. Returns {name, publisher, region, icon, hero} or None when the page is not this title."""
    page = htmllib.unescape(page)
    fields = {}
    for chunk in re.split(r"bd-links-group", page)[1:]:
        found = re.search(r"<strong[^>]*>(.*?)</strong>(.*?)(?:</li>|$)", chunk, re.S)
        if found:
            heading = _text(found.group(1)).lower()
            fields.setdefault("publisherid" if heading.startswith("publisher id") else heading.split(" ")[0], _text(found.group(2)))
    if fields.get("title", "").upper() != title_id.upper():
        return None
    head = re.search(r"<h1[^>]*bd-title[^>]*>(.*?)</h1>", page, re.S)
    art = {}
    for found in re.finditer(re.escape(cdn) + r"/titles/[A-Za-z0-9_]+/(icon0|pic0|pic1)\.(?:webp|png|jpe?g)", page):
        art.setdefault(found.group(1), found.group(0))
    region = PROSPERO_REGIONS.get(fields.get("region", "").upper(), "")
    return {
        "name": clean_text(_text(head.group(1)), 60) if head else "",
        "publisher": clean_text(fields.get("publisher", ""), 60),
        "region": region,
        "icon": art.get("icon0", ""),
        "hero": art.get("pic0") or art.get("pic1", ""),
    }


def image_ext(data: bytes):
    if data[:4] == b"RIFF" and data[8:12] == b"WEBP":
        return ".webp"
    if data.startswith(b"\x89PNG\r\n\x1a\n"):
        return ".png"
    if data.startswith(b"\xff\xd8\xff"):
        return ".jpg"
    return None


def sha256_of(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(8 * 1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def make_png(width: int, height: int, top, bottom) -> bytes:
    """A dark gradient with Orbit's ring mark, as an RGB PNG (no imaging library needed)."""
    radius = min(width, height) * 0.16
    box = int(radius * 2.0)
    cx, cy = width / 2, height / 2
    stroke = max(2.0, radius * 0.11)
    turn = math.radians(34)
    cos_t, sin_t = math.cos(turn), math.sin(turn)
    raw = bytearray()
    for y in range(height):
        t = y / max(1, height - 1)
        base = [round(top[i] + (bottom[i] - top[i]) * t) for i in range(3)]
        row = bytearray(base * width)
        if abs(y - cy) <= box:
            for x in range(max(0, int(cx - box)), min(width, int(cx + box))):
                dx, dy = x - cx, y - cy
                ring = abs(math.hypot(dx, dy) - radius) < stroke / 2
                ex, ey = dx * cos_t - dy * sin_t, dx * sin_t + dy * cos_t       # ellipse turned by -34°
                orbit = abs(math.sqrt((ex / (radius * 1.67)) ** 2 + (ey / (radius * 0.56)) ** 2) - 1) * radius * 0.56 < stroke / 2
                if ring or orbit:
                    i = x * 3
                    for c in range(3):
                        row[i + c] = round(row[i + c] * 0.62 + 255 * 0.38)
        raw += b"\x00" + row

    def chunk(kind: bytes, data: bytes) -> bytes:
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))


def ensure_default_art(root: Path):
    """Create assets/coverFallback.png and assets/heroFallback.png unless they already exist."""
    made = []
    for name, size in (("coverFallback.png", (512, 512)), ("heroFallback.png", (1920, 1080))):
        target = root / "assets" / name
        if not target.exists():
            target.parent.mkdir(parents=True, exist_ok=True)
            partial = target.with_name(target.name + ".part")
            partial.write_bytes(make_png(size[0], size[1], (22, 52, 110), (8, 19, 41)))
            partial.replace(target)
            made.append(name)
    return made


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


AI_FIELDS = ("region", "description", "publisher", "genre", "tagline", "releaseDate")
REGIONS = ("USA", "EUR", "JPN", "ASIA")
GENRES = "Action, Adventure, Role Playing Games, Horror, Driving/Racing, Shooter, Strategy, Fighting, Sport, Simulation, Puzzle, Platformer, Family, Music/Rhythm, Arcade, Casual, Unique"


def ai_prompt(title_id: str, hint: str, lang: str) -> str:
    return (
        "You are an expert on PlayStation games and apps (PS4: CUSAxxxxx, PS5: PPSAxxxxx).\n"
        f'- Exact Title ID: "{title_id}"\n'
        f'- Known title or file name: "{hint}"\n\n'
        "Use the Title ID to identify the real game. If you are not sure, return empty strings instead of guessing.\n"
        "Return STRICTLY one valid JSON object:\n"
        '{"name": "Official clean title (max 60 characters)", '
        f'"description": "Summary in {lang} (max 180 characters)", '
        '"author": "Developer or publisher (max 60 characters)", '
        f'"genre": "1 to 3 genres from: {GENRES}; separate with \\" / \\"", '
        '"tagline": "Short English tagline of 2 to 4 words, like \\"Protect your brother.\\"", '
        '"region": "Region of this Title ID: USA, EUR, JPN or ASIA", '
        '"releaseDate": "Original release date YYYY-MM-DD, or empty if unknown"}'
    )


def ai_lookup(title_id: str, hint: str, keys, model: str, errors: list, lang: str = "English", dead=None):
    """Ask Gemini for the game's name, description and publisher; None when it cannot say."""
    base = os.environ.get("GEMINI_BASE", GEMINI_BASE).rstrip("/")
    if not (base.startswith("https://") or base.startswith("http://127.0.0.1")):
        errors.append("GEMINI_BASE must be https")
        return None
    url = f"{base}/models/{model}:generateContent"
    body = json.dumps({
        "contents": [{"parts": [{"text": ai_prompt(title_id, hint, lang)}]}],
        "generationConfig": {"responseMimeType": "application/json"},
    }).encode("utf-8")
    dead = dead if dead is not None else set()
    for key in keys:
        if key in dead:
            continue                              # quota used up or refused earlier in this run
        request = urllib.request.Request(url, body, {"Content-Type": "application/json", "x-goog-api-key": key})
        try:
            with urllib.request.urlopen(request, timeout=25) as reply:
                payload = json.loads(reply.read(1 << 20).decode("utf-8"))
        except urllib.error.HTTPError as problem:
            errors.append(f"HTTP {problem.code}")
            if problem.code in (400, 401, 403, 429):
                dead.add(key)                     # try the next key, and do not ask this one again
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
            date = clean_text(data.get("releaseDate"), 10)
            result = {
                "v": 3,
                "name": clean_text(data.get("name"), 60),
                "description": clean_text(data.get("description"), 190),
                "author": clean_text(data.get("author"), 60),
                "genre": clean_text(data.get("genre"), 60),
                "tagline": clean_text(data.get("tagline"), 60),
                "region": clean_text(data.get("region"), 4).upper() if clean_text(data.get("region"), 4).upper() in REGIONS else "",
                "releaseDate": date if re.fullmatch(r"\d{4}-\d{2}-\d{2}", date) else "",
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
    parser.add_argument("--no-optimize", action="store_true", help="do not write small .jpg copies of the covers (needs Pillow)")
    parser.add_argument("--no-extract", action="store_true", help="do not take icon0.png / pic1.png out of .zip files")
    parser.add_argument("--refresh-covers", action="store_true", help="overwrite covers already extracted from a .zip")
    parser.add_argument("--no-param", action="store_true", help="do not read sce_sys/param.json (title, title ID, version) from PS5 .zip files")
    parser.add_argument("--ai", action="store_true", help="ask Gemini to fill missing description, publisher, genre, tagline and release date (needs GEMINI_API_KEYS)")
    parser.add_argument("--ai-max", type=int, default=40, help="most Gemini requests per run (default 40)")
    parser.add_argument("--prospero", action="store_true", help="fetch title, publisher, region and artwork from prosperopatches.com (PS5) and orbispatches.com (PS4) (cached in .prospero-cache.json)")
    parser.add_argument("--prospero-max", type=int, default=80, help="most pages fetched from prosperopatches.com per run (default 80)")
    parser.add_argument("--ai-lang", default=os.environ.get("GEMINI_LANG", "English"), help="language of the generated description (default English, like the signed catalogue)")
    parser.add_argument("--no-assets", action="store_true", help="do not create assets/coverFallback.png and assets/heroFallback.png")
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
    now_iso = datetime.now(timezone.utc).isoformat()
    made_assets = []
    if not args.no_assets:
        try:
            made_assets = ensure_default_art(root)
        except OSError as problem:
            print(f"  note: cannot create assets/ ({problem})", file=sys.stderr)
    fallback = {kind: f"assets/{kind}Fallback.png" for kind in ("cover", "hero")
                if (root / "assets" / f"{kind}Fallback.png").is_file()}
    rows, skipped, extracted, notes = [], [], [], []
    optimized = 0
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
    dead_keys = set()
    def pr_hosts(title_id: str):
        """(page base, image host): PS5 titles (PPSA) live on prosperopatches.com, PS4 titles (CUSA) on orbispatches.com."""
        if title_id.startswith("CUSA"):
            return (os.environ.get("ORBIS_BASE", "https://orbispatches.com").rstrip("/"),
                    os.environ.get("ORBIS_CDN", "https://cdn.orbispatches.com").rstrip("/"))
        return (os.environ.get("PROSPERO_BASE", "https://prosperopatches.com").rstrip("/"),
                os.environ.get("PROSPERO_CDN", "https://cdn.prosperopatches.com").rstrip("/"))

    pr_path = root / ".prospero-cache.json"
    try:
        pr_cache = json.loads(pr_path.read_text("utf-8")) if pr_path.is_file() else {}
    except (OSError, ValueError):
        pr_cache = {}
    pr_cache = pr_cache if isinstance(pr_cache, dict) else {}
    pr_dirty, pr_calls, pr_done, pr_images, pr_errors, pr_fail = False, 0, 0, 0, [], 0

    def prospero_lookup(title_id: str):
        """Cached page data for a title, fetching it (politely) when needed."""
        nonlocal pr_dirty, pr_calls, pr_fail
        entry = pr_cache.get(title_id) if isinstance(pr_cache.get(title_id), dict) else None
        if entry is not None:
            if entry.get("unknown"):
                if time.time() - float(entry.get("at", 0)) <= 7 * 86400:
                    return None
            elif entry.get("v") == 1:
                return entry
        if pr_calls >= args.prospero_max or pr_fail >= 3 or not re.fullmatch(r"(?:PPSA|CUSA)\d{5}", title_id):
            return None
        if pr_calls:
            time.sleep(1.0)                            # one page per second: be kind to the site
        pr_calls += 1
        base, cdn = pr_hosts(title_id)
        data, why = http_get(f"{base}/{title_id}", 2 << 20, "text/html")
        if data is None:
            if why == "HTTP 404":
                pr_cache[title_id] = {"unknown": True, "at": time.time()}
                pr_dirty = True
            else:
                pr_errors.append(why)
                pr_fail += 1
            return None
        pr_fail = 0
        parsed = prospero_parse(data.decode("utf-8", "replace"), title_id, cdn)
        if not parsed or not (parsed["name"] or parsed["publisher"] or parsed["icon"]):
            pr_cache[title_id] = {"unknown": True, "at": time.time()}
            pr_dirty = True
            return None
        parsed.update({"v": 1, "at": time.time(), "files": []})
        pr_cache[title_id] = parsed
        pr_dirty = True
        return parsed

    def prospero_art(title_id: str, kind: str, url: str) -> int:
        """Download <TITLEID>_<kind> from the site unless you already have your own file; returns 1 when written."""
        nonlocal pr_dirty, pr_fail
        entry = pr_cache.get(title_id, {})
        if not url or not url.startswith(pr_hosts(title_id)[1] + "/"):
            return 0
        if custom_art(root, images, title_id, kind) and not (args.refresh_covers and kind in entry.get("files", [])):
            return 0                                   # your own jpg / webp wins, and is never overwritten
        data, why = http_get(url, 12 << 20, "image/*")
        if data is None:
            pr_errors.append("image " + why)
            return 0
        ext = image_ext(data)
        if ext is None:
            pr_errors.append("image not recognised")
            return 0
        target = root / args.covers / f"{title_id}_{kind}{ext}"
        target.parent.mkdir(parents=True, exist_ok=True)
        temporary = target.with_name(target.name + ".part")
        try:
            temporary.write_bytes(data)
            temporary.replace(target)
        except OSError as problem:
            pr_errors.append(type(problem).__name__)
            return 0
        images[target.name.lower()] = f"{args.covers}/{target.name}"
        if kind not in entry.setdefault("files", []):
            entry["files"].append(kind)
            pr_dirty = True
        time.sleep(0.3)
        return 1
    from_param = 0
    pack_rows = 0
    for path in sorted(walk_files(root)):
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
        platform = "ps4" if title_id.startswith("CUSA") else "ps5"
        folder_hint = next((part.lower() for part in relative.parts[:-1] if part.lower() in ("ps4", "ps5")), "")
        if folder_hint and folder_hint != platform:
            notes.append(f"{key}: in a {folder_hint.upper()} folder but {title_id} is a {platform.upper()} title; {platform.upper()} used")
        size = path.stat().st_size
        if size < 1:
            skipped.append((key, "empty file"))
            continue
        pack = pack_kind(relative)
        row = {
            "id": f"{title_id.lower()}-nas",
            "gameId": f"{title_id}-{pack}" if pack else title_id,
            "provider": "nas",
            "sourceId": "nas",
            "platform": platform,
            "title": extra.get("title") or (param or {}).get("title") or (title_id if pack in ("DLC", "UPDATES") else title_from(path.name)),
            "titleId": title_id,
            "filename": path.name,
            "path": quote(key, safe="/"),
            "sizeBytes": size,
        }
        pr_fields, pr_title = set(), False
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
        row["sha256"] = sha256_of(path) if args.sha256 else ""
        found = VERSION.search(path.name)
        if found:
            row["version"] = found.group(1)
        if param:
            from_param += 1
            if param["version"]:
                row["version"] = param["version"]
        if args.prospero:
            got = prospero_lookup(title_id)
            if got:
                pr_done += 1
                pr_fields = {f for f in ("publisher", "region") if got.get(f) and not extra.get(f)}
                for f in pr_fields:
                    row[f] = got[f]
                if got.get("name") and not extra.get("title") and not (param or {}).get("title") and not pack:
                    row["title"], pr_title = got["name"], True
                for kind, source in (("icon0", "icon"), ("pic1", "hero")):
                    pr_images += prospero_art(title_id, kind, got.get(source, ""))
        if suffix == ".zip" and not args.no_extract:
            for kind in extract_art(path, title_id, root, args.covers, images, args.refresh_covers):
                extracted.append(f"{title_id}_{kind}.png")
        for field, kind in (("cover", "icon0"), ("hero", "pic1")):
            if not args.no_optimize and optimize_art(root, images, title_id, kind, args.refresh_covers):
                optimized += 1
            hit = artwork_for(images, title_id, kind)
            # The game's own image, else the default one from assets/.
            if hit or fallback.get(field):
                row[field] = hit or fallback[field]
            if fallback.get(field):
                row[field + "Fallback"] = fallback[field]
        if pack:
            # DLC / updates / apps: their own menu (apps also show in Discover); the file name tells packs apart.
            row["id"] = f"{title_id.lower()}-{ {'DLC': 'dlc', 'UPDATES': 'upd', 'APPS': 'app'}[pack] }-{hashlib.sha1(key.encode('utf-8')).hexdigest()[:6]}"
            row["tagline"] = pack
            pack_rows += 1
        wanted = [f for f in AI_FIELDS if not extra.get(f) and f not in pr_fields and not (pack and f == "tagline")]
        if args.ai and keys and wanted:
            info = cache.get(title_id) if isinstance(cache.get(title_id), dict) else None
            if info and info.get("unknown"):
                # Not recognised last time: ask again only after a week.
                info = None if time.time() - float(info.get("at", 0)) > 7 * 86400 else {}
            elif info is not None and info.get("v") != 3:
                info = None                       # saved by an older version, fewer fields
            if info is None and ai_calls < args.ai_max and len(dead_keys) < len(keys) and len(ai_errors) < 3 * len(keys):
                ai_calls += 1
                before = len(ai_errors)
                found_info = ai_lookup(title_id, row["title"], keys, args.ai_model, ai_errors, args.ai_lang, dead_keys)
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
                for field, source in (("region", "region"), ("description", "description"), ("publisher", "author"),
                                      ("genre", "genre"), ("tagline", "tagline"), ("releaseDate", "releaseDate")):
                    if field in wanted and info.get(source):
                        row[field] = info[source]
                # The AI only renames games whose title came from the file name.
                if info.get("name") and not extra.get("title") and not (param or {}).get("title") and not pr_title:
                    row["title"] = info["name"]
        row.setdefault("addedAt", datetime.fromtimestamp(path.stat().st_mtime, timezone.utc).replace(microsecond=0).isoformat())
        for field in ("version", "description", "publisher", "genre", "tagline", "region", "releaseDate", "addedAt",
                      "cover", "hero", "coverFallback", "heroFallback", "gameId"):
            if extra.get(field) and not (pack and field == "tagline"):
                row[field] = extra[field]
        row["artworkLayout"] = "wide"
        row["sourceCheckedAt"] = now_iso
        row["consoleVerified"] = False
        row["browserAvailable"] = False
        row["directAvailable"] = True
        rows.append(row)

    # DLC / updates take the name (and missing details) of the game itself when it is in the same index,
    # and every file of one pack shows the same title.
    base = {r["titleId"]: r for r in rows if r.get("tagline") not in ("DLC", "UPDATES")}
    first_title = {}
    for row in rows:
        if row.get("tagline") in ("DLC", "UPDATES"):
            parent = base.get(row["titleId"])
            if parent:
                row["title"] = parent["title"]
                for field in ("genre", "region", "publisher", "releaseDate", "description"):
                    if parent.get(field) and not row.get(field):
                        row[field] = parent[field]
            first_title.setdefault(row["gameId"], row["title"])
            row["title"] = first_title[row["gameId"]]

    # One id per file: "<titleid>-nas", and "-<format>" (then "-2"...) when a game has several files.
    used = set()
    for row in rows:
        candidate = row["id"]
        if candidate in used:
            candidate = f"{row['id']}-{re.sub('[^a-z0-9]', '', row['format'].lower())}"
            n = 2
            while candidate in used:
                candidate, n = f"{row['id']}-{re.sub('[^a-z0-9]', '', row['format'].lower())}-{n}", n + 1
        row["id"] = candidate
        used.add(candidate)

    ORDER = ("id", "gameId", "titleId", "sizeBytes", "provider", "sourceId", "format", "version", "filename", "sha256",
             "region", "title", "genre", "tagline", "description", "cover", "hero", "coverFallback", "heroFallback",
             "publisher", "releaseDate", "artworkLayout", "addedAt", "platform", "sourceCheckedAt",
             "consoleVerified", "browserAvailable", "directAvailable")
    rows = [{**{k: row[k] for k in ORDER if k in row}, **{k: v for k, v in row.items() if k not in ORDER}} for row in rows]

    # Written aside and renamed, so Orbit never reads a half-written index.
    partial = output.with_name(output.name + ".part")
    partial.write_text(json.dumps({"schemaVersion": 1, "releases": rows}, indent=2, ensure_ascii=False) + "\n", "utf-8")
    partial.replace(output)
    print(f"{len(rows)} file(s) listed in {output}" + (f" ({pack_rows} DLC / update / app file(s))" if pack_rows else ""))
    if cache_dirty:
        try:
            temporary = cache_path.with_name(cache_path.name + ".part")
            temporary.write_text(json.dumps(cache, ensure_ascii=False, indent=1), "utf-8")
            temporary.replace(cache_path)
        except OSError:
            notes.append("could not save .ai-cache.json (folder not writable?)")
    if pr_dirty:
        try:
            temporary = pr_path.with_name(pr_path.name + ".part")
            temporary.write_text(json.dumps(pr_cache, ensure_ascii=False, indent=1), "utf-8")
            temporary.replace(pr_path)
        except OSError:
            notes.append("could not save .prospero-cache.json (folder not writable?)")
    if made_assets:
        print(f"  default images created in assets/: {', '.join(made_assets)}")
    if from_param:
        print(f"  {from_param} game(s) described by their param.json")
    if args.prospero:
        print(f"  prosperopatches.com / orbispatches.com: {pr_done} game(s) completed, {pr_images} image(s) downloaded, {pr_calls} page(s) fetched this run"
              + (f" (last problem: {pr_errors[-1]})" if pr_errors else ""))
        if pr_fail >= 3:
            notes.append("prosperopatches.com / orbispatches.com refused 3 requests in a row; stopped for this run (blocked by the site's protection? try again later)")
    if args.ai and keys:
        print(f"  AI: {ai_done} game(s) completed, {ai_calls} request(s) this run" + (f" (last problem: {ai_errors[-1]})" if ai_errors else "")
              + (f"; {len(dead_keys)}/{len(keys)} key(s) refused or out of quota" if dead_keys else ""))
    for note in notes:
        print(f"  note: {note}")
    if optimized:
        print(f"  {optimized} small .jpg copy(ies) of the covers written (faster on the console)")
    elif Image is None and not args.no_optimize and extracted:
        print("  note: install Pillow (pip install pillow) to shrink covers: the console loads them ~10x faster")
    if extracted:
        print(f"  {len(extracted)} image(s) extracted from .zip files into {args.covers}/")
    for key, reason in skipped:
        print(f"  skipped {key}: {reason}")
    if len(rows) > 512:
        print("Orbit reads at most 512 entries from one index.", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
