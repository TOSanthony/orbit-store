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
(--no-param to skip). A .pkg (PS4 or PS5) is read the same way: its title, content ID, DLC label, version and icon0.png go into the index and covers/ With --ai, Gemini (or your own AI with --ai-provider ollama / openai) fills what is still missing for every game: description
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
import http.client
import json
import os
import math
import re
import shutil
import signal
import struct
import subprocess
import sys
import time
import zlib
import unicodedata
import urllib.error
import urllib.parse
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


def curl_get(url: str, limit: int, accept: str = "*/*"):
    """Same as http_get but through the system's curl: sites that turn Python's requests away often let curl through."""
    exe = shutil.which("curl")
    if not exe:
        return None, "no curl"
    local = url.startswith("http://127.0.0.1")
    if not (url.startswith("https://") or local):
        return None, "not https"
    command = [exe, "-sS", "-L", "--max-redirs", "3", "--max-time", "30", "--max-filesize", str(limit),
               "-H", f"Accept: {accept}", "-w", "\n%{http_code}"] + ([] if local else ["--proto", "=https", "--proto-redir", "=https"]) + [url]
    try:
        done = subprocess.run(command, capture_output=True, timeout=45)
    except (OSError, subprocess.SubprocessError):
        return None, "curl failed"
    cut = done.stdout.rfind(b"\n")
    code = done.stdout[cut + 1:].strip()
    if done.returncode != 0 or cut < 0:
        return None, "curl failed"
    if code != b"200":
        return None, "HTTP " + code.decode("ascii", "replace")
    return done.stdout[:cut], ""


def http_get(url: str, limit: int, accept: str = "*/*"):
    """(bytes, '') or (None, reason). Only https (or a local test server), size-limited; curl is the second chance."""
    if not (url.startswith("https://") or url.startswith("http://127.0.0.1")):
        return None, "not https"
    request = urllib.request.Request(url, headers={"User-Agent": PROSPERO_AGENT, "Accept": accept})
    try:
        with urllib.request.urlopen(request, timeout=25) as reply:
            data = reply.read(limit + 1)
            expected = reply.headers.get("Content-Length", "")
        if expected.isdigit() and len(data) <= limit and len(data) != int(expected):
            # The connection dropped mid-way: Python hands back what it got without complaint.
            other, _ = curl_get(url, limit, accept)
            return (other, "") if other is not None else (None, "incomplete download")
    except urllib.error.HTTPError as problem:
        reason = f"HTTP {problem.code}"
        if problem.code in (403, 429, 503):
            data, why = curl_get(url, limit, accept)
            if data is not None:
                return data, ""
        return None, reason
    except (urllib.error.URLError, http.client.HTTPException, OSError, ValueError) as problem:
        return None, type(problem).__name__
    return (data, "") if len(data) <= limit else (None, "too large")


def http_get_page(url: str):
    """A title page; when Python gets a page without the data (anti-robot screen), try curl."""
    data, why = http_get(url, 2 << 20, "text/html")
    if data is not None and b"bd-links-group" not in data:
        other, _ = curl_get(url, 2 << 20, "text/html")
        if other is not None and b"bd-links-group" in other:
            return other, ""
    return data, why


def _text(fragment: str) -> str:
    return " ".join(htmllib.unescape(re.sub(r"<[^>]+>", " ", fragment)).split())


def logo_title(raw: str, title_id: str = "") -> str:
    """Some pages show the game's logo instead of a text title: its name is the image's alt text.
    Only the logo stored with the title's artwork counts, never the site's own header logo."""
    for tag in re.finditer(r"<img\b[^>]*>", raw, re.I):
        text = tag.group(0)
        source, alt = re.search(r'src="([^"]*)"', text), re.search(r'alt="([^"]*)"', text)
        if source and alt and re.search(r"/titles/[^/\"]*" + re.escape(title_id) + r"[^/\"]*/logo\.\w+", source.group(1)) and alt.group(1).strip():
            return " ".join(htmllib.unescape(alt.group(1)).split())
    return ""


def prospero_parse(page: str, title_id: str, cdn: str):
    """Read a title page. Returns {name, publisher, region, icon, hero} or None when the page is not this title."""
    raw = page
    page = htmllib.unescape(page)
    fields = {}
    for chunk in re.split(r"bd-links-group", page)[1:]:
        found = re.search(r"<strong[^>]*>(.*?)</strong>(.*?)(?:</li>|$)", chunk, re.S)
        if found:
            heading = _text(found.group(1)).lower()
            fields.setdefault("publisherid" if heading.startswith("publisher id") else heading.split(" ")[0], _text(found.group(2)))
    tag_text = _text((re.search(r"<title[^>]*>(.*?)</title>", page, re.S | re.I) or [None, ""])[1])
    if title_id.upper() not in fields.get("title", "").upper().split() and not tag_text.upper().startswith(title_id.upper()):
        return None
    head = re.search(r"<h1[^>]*bd-title[^>]*>(.*?)</h1>", page, re.S)
    art = {}
    for found in re.finditer(re.escape(cdn) + r"/titles/[A-Za-z0-9_]+/(icon0|pic0|pic1)\.(?:webp|png|jpe?g)", page):
        art.setdefault(found.group(1), found.group(0))
    # Values can carry the text of a link beside them ("Switch US", "Sony ... [View]"): keep the real word.
    region = next((PROSPERO_REGIONS[w] for w in re.findall(r"[A-Za-z]+", fields.get("region", "").upper()) if w in PROSPERO_REGIONS), "")
    publisher = re.sub(r"\s*\[\s*(view|switch)\s*\]\s*$", "", fields.get("publisher", ""), flags=re.I).strip()   # only a bracketed link label, never part of a name
    icon = art.get("icon0", "")
    hero, hero_alt = art.get("pic0") or art.get("pic1", ""), ""
    if not hero and icon:
        # The background is often added by the page's script, so it is not in the HTML we receive.
        # It sits next to the icon, in the same folder: try it (a 404 just means there is none).
        folder = icon.rsplit("/", 1)[0]
        hero, hero_alt = folder + "/pic0.webp", folder + "/pic1.webp"
    if not (head and _text(head.group(1))) and not logo_title(raw, title_id):
        # No text title and no logo text: the page's <title> reads "CUSA12345: Game name | ORBISPatches.com".
        tag = re.search(r"<title[^>]*>(.*?)</title>", page, re.S | re.I)
        named = re.match(r"\s*" + re.escape(title_id) + r"\s*[:\-\u2013]\s*(.+?)\s*\|", _text(tag.group(1)) + " |") if tag else None
        page_title = named.group(1) if named else ""
    else:
        page_title = ""
    return {
        "name": clean_text(_text(head.group(1)).strip() if head else "", 200) or clean_text(logo_title(raw, title_id), 200) or clean_text(page_title, 200),
        "publisher": clean_text(publisher, 120),
        "region": region,
        "icon": icon,
        "hero": hero,
        "heroAlt": hero_alt,
    }


def image_ext(data: bytes):
    if data[:4] == b"RIFF" and data[8:12] == b"WEBP":
        return ".webp"
    if data.startswith(b"\x89PNG\r\n\x1a\n"):
        return ".png"
    if data.startswith(b"\xff\xd8\xff"):
        return ".jpg"
    return None


def image_complete(data: bytes, ext: str) -> bool:
    """False for a picture that was cut short (no imaging library needed): the end of the file must be there."""
    if ext == ".webp":
        return len(data) >= 12 and len(data) >= int.from_bytes(data[4:8], "little") + 8
    if ext == ".png":
        return data.rstrip(b"\x00").endswith(b"IEND\xaeB`\x82")
    if ext == ".jpg":
        return b"\xff\xd9" in data[-64:]
    return False


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


DESC_TAIL = re.compile(r"\s*\n\s*(?:Region|Firmware|Credits|Updated)\s*:.*$", re.S | re.I)


def load_descriptions(urls, cache_file: Path, notes: list):
    """{titleId: description} from catalogue files (JSON with a "packages" list, e.g. https://pegasus-catalog.fly.dev/catalogs/pfs.json).
    Only the title ID and the description are kept, without the trailing "Region: / Firmware: / Credits: / Updated:" lines.
    The result is cached for a day in .descriptions-cache.json, so a refresh does not download the files again."""
    urls = [u for u in urls if u]
    if not urls:
        return {}
    cached = {}
    try:
        cached = json.loads(cache_file.read_text("utf-8"))
    except (OSError, ValueError):
        pass
    cached = cached if isinstance(cached, dict) else {}
    if cached.get("urls") == urls and isinstance(cached.get("d"), dict) and time.time() - float(cached.get("at", 0)) < 86400:
        return cached["d"]
    found = {}
    for url in urls:
        data, why = http_get(url, 96 << 20, "application/json")
        if data is None:
            notes.append(f"descriptions: {url} not downloaded ({why})")
            continue
        try:
            doc = json.loads(data.decode("utf-8-sig"))
        except ValueError:
            notes.append(f"descriptions: {url} is not JSON")
            continue
        items = doc.get("packages") or doc.get("releases") or doc.get("games") or [] if isinstance(doc, dict) else doc
        for item in items if isinstance(items, list) else []:
            if not isinstance(item, dict):
                continue
            title_id = str(item.get("titleId") or "").upper()
            text = item.get("description")
            if not re.fullmatch(r"(?:PPSA|CUSA)\d{5}", title_id) or not isinstance(text, str):
                continue
            text = clean_text(htmllib.unescape(DESC_TAIL.sub("", text)), 100000)
            if len(text) > 1000:
                text = text[:1000].rsplit(" ", 1)[0].rstrip(",;:-") + "…"
            if len(text) > len(found.get(title_id, "")):
                found[title_id] = text
    if found:
        try:
            temporary = cache_file.with_name(cache_file.name + ".part")
            temporary.write_text(json.dumps({"at": time.time(), "urls": urls, "d": found}, ensure_ascii=False, indent=1), "utf-8")
            temporary.replace(cache_file)
        except OSError:
            notes.append("could not save .descriptions-cache.json (folder not writable?)")
        return found
    return cached.get("d", {}) if isinstance(cached.get("d"), dict) else {}      # download failed: the last copy is better than nothing


def igdb_token(client_id: str, secret: str, errors: list):
    """An access token from the Twitch id and secret (IGDB belongs to Twitch); None when refused."""
    base = os.environ.get("IGDB_AUTH", "https://id.twitch.tv/oauth2/token")
    if not (base.startswith("https://") or base.startswith("http://127.0.0.1")):
        return None
    body = urllib.parse.urlencode({"client_id": client_id, "client_secret": secret, "grant_type": "client_credentials"}).encode()
    try:
        with urllib.request.urlopen(urllib.request.Request(base, body, {"User-Agent": PROSPERO_AGENT}), timeout=20) as reply:
            return json.loads(reply.read(1 << 16).decode("utf-8")).get("access_token")
    except (urllib.error.URLError, OSError, ValueError, AttributeError) as problem:
        errors.append(f"IGDB token: {type(problem).__name__}")
        return None


def _igdb_key(name: str) -> str:
    return re.sub(r"[^a-z0-9]+", "", unicodedata.normalize("NFKD", str(name or "")).encode("ascii", "ignore").decode().lower())


def igdb_query(title_id: str, name: str, client_id: str, token: str, errors: list):
    """{name, summary, releaseDate} from IGDB (summary, first_release_date) for a game; {} when unknown, None on error."""
    base = os.environ.get("IGDB_BASE", "https://api.igdb.com/v4").rstrip("/")
    if not (base.startswith("https://") or base.startswith("http://127.0.0.1")):
        errors.append("IGDB_BASE must be https")
        return None
    headers = {"Client-ID": client_id, "Authorization": f"Bearer {token}", "Accept": "application/json", "User-Agent": PROSPERO_AGENT}

    def post(endpoint: str, query: str):
        try:
            with urllib.request.urlopen(urllib.request.Request(f"{base}/{endpoint}", query.encode("utf-8"), headers), timeout=20) as reply:
                return json.loads(reply.read(4 << 20).decode("utf-8")), True
        except urllib.error.HTTPError as problem:
            errors.append(f"IGDB HTTP {problem.code}")
        except (urllib.error.URLError, OSError, ValueError) as problem:
            errors.append(f"IGDB {type(problem).__name__}")
        return None, False

    fields = "fields name, summary, first_release_date;"
    data = None
    if re.fullmatch(r"(?:PPSA|CUSA)\d{5}", title_id):
        # 1. By the game's code, when IGDB knows it (a store identifier containing the Title ID).
        linked, ok = post("external_games", f'fields game; where uid ~ *"{title_id}"* & game != null; limit 1;')
        if not ok:
            return None
        if isinstance(linked, list) and linked and isinstance(linked[0], dict) and isinstance(linked[0].get("game"), int):
            time.sleep(0.3)
            data, ok = post("games", f"{fields} where id = {linked[0]['game']};")
            if not ok:
                return None
            if not data:
                data = None
    if data is None:
        # 2. By the game's name.
        time.sleep(0.3)
        search = str(name).replace("\\", " ").replace('"', " ")
        data, ok = post("games", f'{fields} search "{search}"; limit 5;')
        if not ok:
            return None
    if not isinstance(data, list) or not data:
        return {}
    wanted = _igdb_key(name)
    game = next((g for g in data if isinstance(g, dict) and _igdb_key(g.get("name")) == wanted), None) or next((g for g in data if isinstance(g, dict)), {})
    stamp = game.get("first_release_date")
    date = time.strftime("%Y-%m-%d", time.gmtime(stamp)) if isinstance(stamp, (int, float)) else ""
    return {"name": clean_text(game.get("name"), 80), "summary": clean_text(game.get("summary"), 2000), "releaseDate": date}


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


def read_ps4_version(path: Path) -> str:
    """APP_VER (else VERSION) from the param.sfo inside a PS4 .pkg, read from its first bytes only. "" if unreadable."""
    try:
        with path.open("rb") as handle:
            head = handle.read(0x20)
            if len(head) < 0x20 or head[:4] != b"\x7fCNT":
                return ""
            count, table = struct.unpack(">I", head[0x10:0x14])[0], struct.unpack(">I", head[0x18:0x1C])[0]
            if not 0 < count <= 4096 or table > 1 << 24:
                return ""
            handle.seek(table)
            entries = handle.read(count * 32)
            for i in range(len(entries) // 32):
                ident, _, _, _, offset, size = struct.unpack(">6I", entries[i * 32:i * 32 + 24])
                if ident != 0x1000:
                    continue
                if not 16 <= size <= 1 << 20:
                    return ""
                handle.seek(offset)
                sfo = handle.read(size)
                if len(sfo) < size or sfo[:4] != b"\x00PSF":
                    return ""
                keys_at, data_at, n = struct.unpack("<3I", sfo[8:20])
                found = {}
                for k in range(min(n, 512)):
                    key_off, fmt, length, _, data_off = struct.unpack("<HHIII", sfo[20 + 16 * k:36 + 16 * k])
                    end = sfo.find(b"\x00", keys_at + key_off)
                    name = sfo[keys_at + key_off:end].decode("ascii", "replace")
                    if name in ("APP_VER", "VERSION") and fmt in (0x0004, 0x0204):
                        found[name] = sfo[data_at + data_off:data_at + data_off + length].split(b"\x00")[0].decode("ascii", "replace")
                for name in ("APP_VER", "VERSION"):
                    if re.fullmatch(r"\d{1,3}\.\d{1,3}", found.get(name, "")):
                        return found[name]
                return ""
    except (OSError, struct.error, ValueError):
        pass
    return ""


def parse_sfo(sfo: bytes) -> dict:
    """The text and number values of a param.sfo, by key name."""
    found = {}
    if len(sfo) < 20 or sfo[:4] != b"\x00PSF":
        return found
    keys_at, data_at, n = struct.unpack("<3I", sfo[8:20])
    for k in range(min(n, 512)):
        if len(sfo) < 36 + 16 * k:
            break
        key_off, fmt, length, _, data_off = struct.unpack("<HHIII", sfo[20 + 16 * k:36 + 16 * k])
        end = sfo.find(b"\x00", keys_at + key_off)
        name = sfo[keys_at + key_off:end].decode("ascii", "replace")
        if fmt in (0x0004, 0x0204):
            found[name] = sfo[data_at + data_off:data_at + data_off + length].split(b"\x00")[0].decode("utf-8", "replace")
    return found


PKG_ICON_MAX = 4 * 1024 * 1024


def read_pkg(path: Path):
    """What a PS4 or PS5 .pkg says about itself, read from its first blocks without extracting it:
    {"titleId", "contentId", "label", "title", "version", "icon"}, or None when it cannot be read.

    PS4 packages start with the "\\x7fCNT" header; PS5 ones with "\\x7fFIH", which says where the same header is
    embedded. Its entry table points at param.json (PS5) or param.sfo (PS4) and icon0.png. An encrypted entry is
    skipped. The label is the end of the content ID (EP0001-PPSA20397_00-LIMITEDPACK00000 -> LIMITEDPACK00000),
    which is also the name of the folder the console makes for an installed DLC."""
    try:
        with path.open("rb") as handle:
            head = handle.read(0x1000)
            base = 0
            if head[:4] == b"\x7fFIH" and len(head) >= 0xA8:
                base = struct.unpack("<Q", head[0x58:0x60])[0]
                handle.seek(base)
                head = handle.read(0x1000)
            if head[:4] != b"\x7fCNT" or len(head) < 0x100:
                return None
            info = {"titleId": "", "contentId": "", "label": "", "title": "", "version": "", "icon": None}
            content_id = head[0x40:0x64].split(b"\x00")[0].decode("ascii", "replace")
            if re.fullmatch(r"[A-Z]{2}\d{4}-[A-Z]{4}\d{5}_\d{2}-[A-Za-z0-9_]{1,16}", content_id):
                info["contentId"] = content_id
                info["label"] = content_id.rsplit("-", 1)[1]
                info["titleId"] = content_id.split("-")[1][:9]
            count, table = struct.unpack(">I", head[0x10:0x14])[0], struct.unpack(">I", head[0x18:0x1C])[0]
            if not 0 < count <= 256 or table <= 0:
                return info if info["contentId"] else None
            handle.seek(base + table)
            raw = handle.read(count * 32)
            for i in range(len(raw) // 32):
                ident, _, flags, _, offset, size = struct.unpack(">6I", raw[i * 32:i * 32 + 24])
                if flags & 0x80000000 or not size:
                    continue                                          # encrypted
                if ident in (0x1000, 0x2000) and size <= 1 << 16 and not info["title"]:
                    handle.seek(base + offset)
                    blob = handle.read(size)
                    if ident == 0x2000 and blob.lstrip()[:1] == b"{":
                        try:
                            data = json.loads(blob.decode("utf-8-sig", "replace").strip("\x00 \r\n\t"))
                        except ValueError:
                            continue
                        local = data.get("localizedParameters") if isinstance(data, dict) else None
                        local = local if isinstance(local, dict) else {}
                        names = []
                        for language in (local.get("defaultLanguage"), "en-US", "en-GB"):
                            entry = local.get(language) if isinstance(language, str) else None
                            if isinstance(entry, dict):
                                names.append(entry.get("titleName"))
                        names += [v.get("titleName") for v in local.values() if isinstance(v, dict)]
                        info["title"] = next((clean_text(n, 120) for n in names if isinstance(n, str) and n.strip()), "")
                        info["version"] = clean_text(data.get("contentVersion"), 24)
                        if not info["titleId"]:
                            info["titleId"] = clean_text(data.get("titleId"), 9).upper()
                    elif ident == 0x1000:
                        fields = parse_sfo(blob)
                        info["title"] = clean_text(fields.get("TITLE", ""), 120)
                        info["version"] = clean_text(fields.get("APP_VER") or fields.get("VERSION") or "", 24)
                        if not info["titleId"]:
                            info["titleId"] = clean_text(fields.get("TITLE_ID", ""), 9).upper()
                elif ident == 0x1200 and 8 < size <= PKG_ICON_MAX and info["icon"] is None:
                    handle.seek(base + offset)
                    blob = handle.read(size)
                    if len(blob) == size and blob.startswith(b"\x89PNG\r\n\x1a\n"):
                        info["icon"] = blob
            return info
    except (OSError, struct.error, ValueError, IndexError):
        return None


def store_icon(data: bytes, art_id: str, root: Path, folder: str, images: dict, refresh: bool) -> bool:
    """Write a package's icon as <root>/<folder>/<art_id>_icon0.png. Existing files are kept unless --refresh-covers
    is given, and an image of yours (.jpg / .webp in covers/) is never replaced. True when written."""
    if custom_art(root, images, art_id, "icon0"):
        return False
    name = f"{art_id}_icon0.png"
    if not refresh and name.lower() in images:
        return False
    target = root / folder / name
    try:
        target.parent.mkdir(parents=True, exist_ok=True)
        temporary = target.with_name(target.name + ".part")
        temporary.write_bytes(data)
        temporary.replace(target)
    except OSError:
        return False
    images[name.lower()] = f"{folder}/{name}"
    return True


GEMINI_BASE = "https://generativelanguage.googleapis.com/v1beta"


AI_FIELDS = ("description", "genre", "tagline", "releaseDate")   # never the title, publisher or region: those come from the sites
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


def ai_call(prompt: str, keys, model: str, errors: list, dead=None, provider: str = "gemini", base: str = "", json_mode: bool = True):
    """Send one prompt to the AI, return its text answer or None.
    provider "gemini": Google Gemini. "ollama": your Ollama server (native /api/generate). "openai": any server with an
    OpenAI-style /chat/completions API (LM Studio, llama.cpp, vLLM, OpenAI, OpenRouter...) at `base`."""
    ollama = provider == "ollama"
    custom = provider in ("openai", "ollama")
    if custom:
        base = (base or os.environ.get("AI_BASE", "")).strip().rstrip("/")
        if not re.match(r"https?://[^\s/]+", base):
            errors.append("AI_BASE must be an http(s) address (e.g. http://192.168.1.20:11434/v1)")
            return None
        if ollama:
            base = re.sub(r"/api(?:/(?:generate|chat))?$", "", base)          # the host is enough; a full /api/generate address works too
            url = f"{base}/api/generate"
            payload_out = {"model": model, "prompt": prompt, "stream": False, "options": {"temperature": 0.2}}
            if json_mode:
                payload_out["format"] = "json"
        else:
            url = f"{base}/chat/completions"
            payload_out = {"model": model, "messages": [{"role": "user", "content": prompt}], "temperature": 0.2}
    else:
        base = os.environ.get("GEMINI_BASE", GEMINI_BASE).rstrip("/")
        if not (base.startswith("https://") or base.startswith("http://127.0.0.1")):
            errors.append("GEMINI_BASE must be https")
            return None
        url = f"{base}/models/{model}:generateContent"
        payload_out = {"contents": [{"parts": [{"text": prompt}]}]}
        if json_mode:
            payload_out["generationConfig"] = {"responseMimeType": "application/json"}
    body = json.dumps(payload_out).encode("utf-8")
    dead = dead if dead is not None else set()
    for key in keys:
        if key in dead:
            continue                              # quota used up or refused earlier in this run
        headers = {"Content-Type": "application/json"}
        if custom:
            if key:
                headers["Authorization"] = f"Bearer {key}"    # a local server often needs no key
        else:
            headers["x-goog-api-key"] = key
        request = urllib.request.Request(url, body, headers)
        try:
            with urllib.request.urlopen(request, timeout=120 if custom else 25) as reply:
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
            if custom:
                text = payload["response"] if ollama else payload["choices"][0]["message"]["content"]
                text = re.sub(r"<think>.*?</think>", "", text, flags=re.S)       # reasoning models
            else:
                text = payload["candidates"][0]["content"]["parts"][0]["text"]
            if isinstance(text, str):
                return text
        except (KeyError, IndexError, TypeError, ValueError):
            pass
        errors.append("unreadable answer")
    return None


def ai_lookup(title_id: str, hint: str, keys, model: str, errors: list, lang: str = "English", dead=None, provider: str = "gemini", base: str = ""):
    """Ask the AI for the game's name, description and publisher; None when it cannot say."""
    before = len(errors)
    text = ai_call(ai_prompt(title_id, hint, lang), keys, model, errors, dead, provider, base)
    if text is None:
        return None
    try:
        found = re.search(r"\{.*\}", text, re.S) if provider != "gemini" else None
        data = json.loads(re.sub(r"^```(?:json)?\s*|\s*```$", "", (found.group(0) if found else text).strip(), flags=re.IGNORECASE))
    except ValueError:
        errors.append("unreadable answer")
        return None
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


def ai_translate(text: str, lang: str, keys, model: str, errors: list, dead=None, provider: str = "gemini", base: str = ""):
    """The description translated into `lang` by the AI; None when the answer looks wrong (empty, cut short, much too long)."""
    prompt = (f"Translate this PlayStation game description into {lang}. Keep the meaning, the names (game, characters, places) and the bullet points; "
              "add nothing and explain nothing. Answer with the translation only.\n\n" + text)
    answer = ai_call(prompt, keys, model, errors, dead, provider, base, json_mode=False)
    if answer is None:
        return None
    answer = clean_text(re.sub(r"^```\w*\s*|\s*```$", "", answer.strip()), 2000).strip(" \"'")
    if len(answer) < 0.4 * len(text) or len(answer) > 2.5 * len(text) + 40:
        errors.append("translation rejected (wrong length)")
        return None
    return answer


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
    parser.add_argument("--no-param", action="store_true", help="do not read sce_sys/param.json from PS5 .zip files, nor the title, content ID and icon inside .pkg files")
    parser.add_argument("--ai", action="store_true", help="ask Gemini to fill missing description, publisher, genre, tagline and release date (needs GEMINI_API_KEYS)")
    parser.add_argument("--ai-max", type=int, default=40, help="most Gemini requests per run (default 40)")
    parser.add_argument("--prospero", action="store_true", help="fetch title, publisher, region and artwork from prosperopatches.com (PS5) and orbispatches.com (PS4) (cached in .prospero-cache.json)")
    parser.add_argument("--saved-art", action="store_true", help="only fetch title and artwork from prosperopatches.com for the updates saved by the admin page (.nas-admin-patches.json), write .saved-info.json and stop")
    parser.add_argument("--saved-id", default="", help="with --saved-art: only this title ID")
    parser.add_argument("--prospero-max", type=int, default=400, help="most pages fetched from prosperopatches.com / orbispatches.com per run (default 400)")
    parser.add_argument("--db", type=Path, help="game database (games-db.json, see make-game-db.py): fills genre, tagline, description, release date and region by title ID; default: <folder>/games-db.json if present")
    parser.add_argument("--desc-url", action="append", default=[], help="catalogue JSON with titleId + description (e.g. https://pegasus-catalog.fly.dev/catalogs/pfs.json); used before the AI, for games and apps; can be repeated, or list them in DESC_URLS separated by commas")
    parser.add_argument("--igdb-max", type=int, default=400, help="most IGDB requests per run (default 400); IGDB is used when IGDB_CLIENT_ID and IGDB_ACCESS_TOKEN (or IGDB_CLIENT_SECRET) are set")
    parser.add_argument("--ai-lang", default=os.environ.get("GEMINI_LANG", "English"), help="language of the generated description (default English, like the signed catalogue)")
    parser.add_argument("--no-assets", action="store_true", help="do not create assets/coverFallback.png and assets/heroFallback.png")
    parser.add_argument("--ai-model", default=os.environ.get("AI_MODEL") or os.environ.get("GEMINI_MODEL", "gemini-3.6-flash"))
    parser.add_argument("--ai-provider", choices=("gemini", "openai", "ollama"), default=os.environ.get("AI_PROVIDER", "gemini"), help="gemini (default), ollama (your Ollama server, native /api/generate) or openai: your own AI, any server with an OpenAI-style /chat/completions API (Ollama, LM Studio, OpenAI, OpenRouter...)")
    parser.add_argument("--ai-base", default=os.environ.get("AI_BASE", ""), help="with --ai-provider openai: the API address, e.g. http://192.168.1.20:11434/v1 (key, if any, in AI_API_KEYS)")
    parser.add_argument("--sha256", action="store_true", help="also record each file's SHA-256 (slow on large files)")
    args = parser.parse_args()

    try:
        sys.stdout.reconfigure(line_buffering=True)    # the admin page reads the log while the scan runs
    except (AttributeError, ValueError):
        pass
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(143))   # `timeout` / stop: let the caches be saved first

    root = args.folder.resolve()
    if not root.is_dir():
        print(f"{root} is not a folder", file=sys.stderr)
        return 2
    output = args.output or root / "index.json"
    titles_path = args.titles or root / "titles.json"
    overrides = {}
    if titles_path.is_file():
        try:
            overrides = json.loads(titles_path.read_text("utf-8-sig"))
        except (OSError, ValueError) as problem:
            print(f"  note: cannot read {titles_path} ({problem}), titles.json ignored", file=sys.stderr)
        if not isinstance(overrides, dict):
            overrides = {}
        overrides = {k: v for k, v in overrides.items() if isinstance(v, dict)}

    db_path = args.db or (root / "games-db.json")
    game_db, db_hits = {}, 0
    if db_path.is_file():
        try:
            raw = json.loads(db_path.read_text("utf-8"))
            raw = raw.get("releases", []) if isinstance(raw, dict) else raw
            game_db = {str(e.get("titleId", "")).upper(): e for e in raw if isinstance(e, dict) and e.get("titleId")}
        except (OSError, ValueError) as problem:
            print(f"  note: cannot read {db_path} ({problem}), game database ignored", file=sys.stderr)
    elif args.db:
        print(f"  note: {db_path} not found, game database ignored", file=sys.stderr)

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
    own_ai = args.ai_provider in ("openai", "ollama")
    keys = [k.strip() for k in re.split(r"[,\s]+", os.environ.get("AI_API_KEYS" if own_ai else "GEMINI_API_KEYS", "")) if k.strip()]
    if own_ai and not keys:
        keys = [""]                                   # your own server: the key is optional
    if args.ai and own_ai and not args.ai_base:
        keys = []
        notes.append("--ai: no address in AI_BASE / --ai-base, AI step skipped")
    elif args.ai and not keys:
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

    desc_urls = args.desc_url or [u.strip() for u in os.environ.get("DESC_URLS", "").split(",") if u.strip()]
    catalog_desc = load_descriptions(desc_urls, root / ".descriptions-cache.json", notes)
    catalog_hits = 0
    # IGDB (optional): release date (first_release_date) and, when the catalogue has none, the description (summary). Cached in .igdb-cache.json.
    igdb_id = os.environ.get("IGDB_CLIENT_ID", "").strip()
    igdb_secret = os.environ.get("IGDB_CLIENT_SECRET", "").strip()
    igdb_tok = os.environ.get("IGDB_ACCESS_TOKEN", "").strip()
    igdb_on = bool(igdb_id and (igdb_tok or igdb_secret))
    igdb_path = root / ".igdb-cache.json"
    try:
        igdb_cache = json.loads(igdb_path.read_text("utf-8")) if igdb_path.is_file() else {}
    except (OSError, ValueError):
        igdb_cache = {}
    igdb_cache = igdb_cache if isinstance(igdb_cache, dict) else {}
    igdb_dirty, igdb_calls, igdb_hits, igdb_errors, igdb_dead = False, 0, 0, [], False

    def save_igdb():
        nonlocal igdb_dirty
        if not igdb_dirty:
            return
        try:
            temporary = igdb_path.with_name(igdb_path.name + ".part")
            temporary.write_text(json.dumps(igdb_cache, ensure_ascii=False, indent=1), "utf-8")
            temporary.replace(igdb_path)
            igdb_dirty = False
        except OSError:
            notes.append("could not save .igdb-cache.json (folder not writable?)")

    def igdb_lookup(title_id: str, name: str):
        """Cached IGDB data for a game, asked now when needed (up to --igdb-max per run); unknown games are asked again after 30 days."""
        nonlocal igdb_dirty, igdb_calls, igdb_tok, igdb_dead
        hit = igdb_cache.get(title_id)
        if isinstance(hit, dict) and hit.get("n") == name and hit.get("v") == 5 and (not hit.get("unknown") or time.time() - float(hit.get("at", 0)) < 30 * 86400):
            return None if hit.get("unknown") else hit
        if igdb_dead or igdb_calls >= args.igdb_max or len(igdb_errors) >= 3:
            return None
        if not igdb_tok:
            igdb_tok = igdb_token(igdb_id, igdb_secret, igdb_errors) or ""
            if not igdb_tok:
                igdb_dead = True
                return None
        if igdb_calls:
            time.sleep(0.3)                            # IGDB allows 4 requests per second
        igdb_calls += 1
        got = igdb_query(title_id, name, igdb_id, igdb_tok, igdb_errors)
        if got is None:
            if igdb_errors and igdb_errors[-1] == "IGDB HTTP 401":
                igdb_dead = True                        # token refused or expired: stop for this run
            return None
        igdb_errors.clear()
        if not got or not (got.get("summary") or got.get("releaseDate")):
            igdb_cache[title_id] = {"unknown": True, "at": time.time(), "n": name, "v": 5}
            igdb_dirty = True
            return None
        got.update({"at": time.time(), "n": name, "v": 5})
        igdb_cache[title_id] = got
        igdb_dirty = True
        if igdb_calls % 20 == 0:
            save_igdb()
        return got

    tr_path = root / ".translations-cache.json"
    try:
        tr_cache = json.loads(tr_path.read_text("utf-8")) if tr_path.is_file() else {}
    except (OSError, ValueError):
        tr_cache = {}
    tr_cache = tr_cache if isinstance(tr_cache, dict) else {}
    tr_dirty, tr_calls, tr_done, tr_errors = False, 0, 0, []
    translating = bool(args.ai and keys and args.ai_lang.strip().lower() not in ("", "english", "en", "anglais"))

    def save_translations():
        nonlocal tr_dirty
        if not tr_dirty:
            return
        try:
            temporary = tr_path.with_name(tr_path.name + ".part")
            temporary.write_text(json.dumps(tr_cache, ensure_ascii=False, indent=1), "utf-8")
            temporary.replace(tr_path)
            tr_dirty = False
        except OSError:
            if "could not save .translations-cache.json (folder not writable?)" not in notes:
                notes.append("could not save .translations-cache.json (folder not writable?)")

    def translated(title_id: str, text: str) -> str:
        """The catalogue's description in the AI language: from the cache, else asked now (up to --ai-max per run), else the original."""
        nonlocal tr_dirty, tr_calls, tr_done
        mark = hashlib.sha1(text.encode("utf-8")).hexdigest()[:12]
        hit = tr_cache.get(title_id)
        if isinstance(hit, dict) and hit.get("src") == mark and hit.get("lang") == args.ai_lang and hit.get("text"):
            return hit["text"]
        if tr_calls >= args.ai_max or len(dead_keys) >= len(keys) or len(tr_errors) >= 3 * len(keys):
            return text
        tr_calls += 1
        result = ai_translate(text, args.ai_lang, keys, args.ai_model, tr_errors, dead_keys, args.ai_provider, args.ai_base)
        if not result:
            return text
        tr_errors.clear()
        tr_done += 1
        tr_cache[title_id] = {"src": mark, "lang": args.ai_lang, "text": result}
        tr_dirty = True
        if tr_done % 10 == 0:
            save_translations()
        return result
    pr_path = root / ".prospero-cache.json"
    try:
        pr_cache = json.loads(pr_path.read_text("utf-8")) if pr_path.is_file() else {}
    except (OSError, ValueError):
        pr_cache = {}
    pr_cache = pr_cache if isinstance(pr_cache, dict) else {}
    # What was read inside each .pkg, by path, so an unchanged file (same size and date) is not opened again.
    pkg_path = root / ".pkg-cache.json"
    try:
        pkg_cache = json.loads(pkg_path.read_text("utf-8")) if pkg_path.is_file() else {}
    except (OSError, ValueError):
        pkg_cache = {}
    pkg_cache = pkg_cache if isinstance(pkg_cache, dict) else {}
    pkg_dirty = False
    pr_dirty, pr_calls, pr_done, pr_images, pr_errors, pr_fail = False, 0, 0, 0, [], 0
    pr_left = set()

    def prospero_lookup(title_id: str):
        """Cached page data for a title, fetching it (politely) when needed."""
        nonlocal pr_dirty, pr_calls, pr_fail
        entry = pr_cache.get(title_id) if isinstance(pr_cache.get(title_id), dict) else None
        if entry is not None:
            if entry.get("unknown"):
                if time.time() - float(entry.get("at", 0)) <= (86400 if entry.get("soft") else 7 * 86400):
                    return None
            elif entry.get("v") == 5:       # older entries missed titles shown as a logo, or backgrounds added by script
                return entry
        if not re.fullmatch(r"(?:PPSA|CUSA)\d{5}", title_id):
            return None
        if pr_calls >= args.prospero_max:
            pr_left.add(title_id)
            return None
        if pr_fail >= 3:
            return None
        if pr_calls:
            time.sleep(1.0)                            # one page per second: be kind to the site
        pr_calls += 1
        base, cdn = pr_hosts(title_id)
        data, why = http_get_page(f"{base}/{title_id}")
        if data is None:
            if why == "HTTP 404":
                pr_cache[title_id] = {"unknown": True, "at": time.time()}
                pr_dirty = True
            else:
                pr_errors.append(why)
                pr_fail += 1
            return None
        pr_fail = 0
        text = data.decode("utf-8", "replace")
        parsed = prospero_parse(text, title_id, cdn)
        if not parsed or not (parsed["name"] or parsed["publisher"] or parsed["icon"]):
            if "bd-links-group" not in text:
                pr_errors.append("page not readable (blocked by the site?)")   # not cached: retried next run
                pr_fail += 1
            else:
                pr_cache[title_id] = {"unknown": True, "soft": True, "at": time.time()}
                pr_dirty = True
            return None
        parsed.update({"v": 5, "at": time.time(), "files": []})
        pr_cache[title_id] = parsed
        pr_dirty = True
        return parsed

    def prospero_art(title_id: str, kind: str, url: str, alt: str = "") -> int:
        """Download <TITLEID>_<kind> from the site unless you already have your own file; returns 1 when written."""
        nonlocal pr_dirty, pr_fail
        entry = pr_cache.get(title_id, {})
        if not url or not url.startswith(pr_hosts(title_id)[1] + "/"):
            return 0
        ours = kind in entry.get("files", [])           # downloaded from the site by an earlier run
        if custom_art(root, images, title_id, kind):
            if not ours:
                return 0                               # your own jpg / webp wins, and is never overwritten
            if not args.refresh_covers:
                for ext in (".webp", ".png", ".jpg"):  # ours: kept unless it is cut short or missing
                    have = images.get(f"{title_id}_{kind}{ext}".lower())
                    if have:
                        try:
                            if image_complete((root / have).read_bytes(), ext):
                                return 0
                        except OSError:
                            pass
        data, why, ext = None, "HTTP 404", None
        for candidate in (url, alt):
            if not candidate or not candidate.startswith(pr_hosts(title_id)[1] + "/"):
                continue
            for attempt in range(2):                   # a second try for a dropped connection or a cut-short picture
                data, why = http_get(candidate, 12 << 20, "image/*")
                ext = image_ext(data) if data is not None else None
                if data is not None and ext and not image_complete(data, ext):
                    other, _ = curl_get(candidate, 12 << 20, "image/*")
                    data = other if other is not None else data
                    ext = image_ext(data)
                    if not image_complete(data, ext or ""):
                        data, why, ext = None, "incomplete image", None
                if data is not None or why == "HTTP 404":
                    break
                time.sleep(1.0)
            if data is not None or why not in ("HTTP 404",):
                break                                  # got it, or a real problem: stop here
        if data is None:
            if why != "HTTP 404":                      # a missing background is normal, not an error
                pr_errors.append(f"image {kind} of {title_id}: {why}")
            return 0
        if ext is None:
            pr_errors.append(f"image {kind} of {title_id}: not recognised")
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
    def save_caches():
        """Write .ai-cache.json / .prospero-cache.json when they changed (also on a stop or a crash)."""
        nonlocal cache_dirty, pr_dirty, pkg_dirty
        for name, data, target, dirty in ((".ai-cache.json", cache, cache_path, cache_dirty),
                                          (".prospero-cache.json", pr_cache, pr_path, pr_dirty),
                                          (".pkg-cache.json", pkg_cache, pkg_path, pkg_dirty)):
            if not dirty:
                continue
            try:
                temporary = target.with_name(target.name + ".part")
                temporary.write_text(json.dumps(data, ensure_ascii=False, indent=1), "utf-8")
                temporary.replace(target)
            except OSError:
                if f"could not save {name} (folder not writable?)" not in notes:
                    notes.append(f"could not save {name} (folder not writable?)")
                continue
            if target is cache_path:
                cache_dirty = False
            else:
                pr_dirty = False

    _ON_EXIT.append(save_caches)
    _ON_EXIT.append(save_translations)
    _ON_EXIT.append(save_igdb)

    def saved_art():
        """Title and artwork of the updates saved from the admin page (« + »), written to .saved-info.json."""
        nonlocal pr_images
        try:
            saved = json.loads((root / ".nas-admin-patches.json").read_text("utf-8"))
        except (OSError, ValueError):
            saved = {}
        info_path = root / ".saved-info.json"
        try:
            info = json.loads(info_path.read_text("utf-8"))
        except (OSError, ValueError):
            info = {}
        info = info if isinstance(info, dict) else {}
        ids = [i for i in (saved if isinstance(saved, dict) else {}) if re.fullmatch(r"PPSA\d{5}", i)]
        if args.saved_id:
            ids = [i for i in ids if i == args.saved_id]
        for title_id in ids:
            got = prospero_lookup(title_id)
            if not got:
                continue
            row_info = {"title": got.get("name", ""), "publisher": got.get("publisher", ""), "region": got.get("region", "")}
            for kind, source, key in (("icon0", "icon", "cover"), ("pic1", "hero", "hero")):
                pr_images += prospero_art(title_id, kind, got.get(source, ""), got.get("heroAlt", "") if kind == "pic1" else "")
                for ext in (".webp", ".png", ".jpg", ".jpeg"):
                    have = images.get(f"{title_id}_{kind}{ext}".lower())
                    if have:
                        row_info[key] = have
                        break
            info[title_id] = row_info
        for title_id in [i for i in info if i not in (saved if isinstance(saved, dict) else {})]:
            del info[title_id]
        try:
            temporary = info_path.with_name(info_path.name + ".part")
            temporary.write_text(json.dumps(info, ensure_ascii=False, indent=1), "utf-8")
            temporary.replace(info_path)
        except OSError:
            print("  note: could not save .saved-info.json (folder not writable?)", file=sys.stderr)

    if args.saved_art:
        saved_art()
        save_caches()
        print(f"  saved updates: {pr_images} image(s) downloaded, {pr_calls} page(s) fetched")
        return 0
    last_save = time.time()
    from_param = 0
    pack_rows = 0
    candidates = [p for p in sorted(walk_files(root)) if p.suffix.lower() in IMAGE_FORMATS or p.suffix.lower() in ARCHIVES]
    print(f"scan: {len(candidates)} candidate file(s) in {root}")
    seen_count = 0
    for path in sorted(walk_files(root)):
        if path.suffix.lower() in IMAGE_FORMATS or path.suffix.lower() in ARCHIVES:
            seen_count += 1
            if seen_count == 1 or seen_count % 10 == 0:
                print(f"  scan: {seen_count}/{len(candidates)} {path.name}")
            if (cache_dirty or pr_dirty) and time.time() - last_save > 60:
                save_caches()                          # a long run keeps what it already learned
                last_save = time.time()
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
        pkg = None
        if suffix == ".pkg" and not args.no_param:
            try:
                stat = path.stat()
            except OSError:
                stat = None
            hit = pkg_cache.get(key)
            if (stat and isinstance(hit, dict) and isinstance(hit.get("info"), dict) and not args.refresh_covers
                    and hit.get("size") == stat.st_size and hit.get("mtime") == stat.st_mtime_ns):
                pkg = {**hit["info"], "icon": None, "cached": True}
            else:
                pkg = read_pkg(path)
                if pkg and stat:
                    pkg_cache[key] = {"size": stat.st_size, "mtime": stat.st_mtime_ns, "hadIcon": pkg["icon"] is not None,
                                      "info": {k: v for k, v in pkg.items() if k != "icon"}}
                    pkg_dirty = True
        if pkg and not param and (pkg["title"] or pkg["titleId"]):
            # A package says the same things as a .zip's param.json: title, title ID and (PS5) version.
            version = pkg["version"] if re.fullmatch(r"\d+(\.\d+){1,3}", pkg["version"]) and (pkg["titleId"] or "").startswith("PPSA") else ""
            param = {"title": pkg["title"], "titleId": pkg["titleId"] if re.fullmatch(r"(PPSA|CUSA)\d{5}", pkg["titleId"]) else "", "version": version}
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
        try:
            size = path.stat().st_size
        except OSError as problem:
            skipped.append((key, f"cannot read it ({problem.strerror or type(problem).__name__})"))
            continue
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
        art_id = title_id                      # the picture's name: a DLC has its own icon, named by its label
        own_title = False                      # a DLC is named by its package, not by its game's page
        if pkg and pkg["contentId"] and pkg["titleId"] in ("", title_id):
            row["contentId"] = pkg["contentId"]
            if pack == "DLC":
                row["label"] = pkg["label"]
                if pkg["title"]:
                    row["packTitle"] = pkg["title"]      # the DLC's own name; "title" is shared by the whole pack
                art_id = f"{title_id}_{pkg['label']}"
                own_title = bool(pkg["title"]) and not extra.get("title")
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
        row["sha256"] = ""
        if args.sha256:
            try:
                row["sha256"] = sha256_of(path)
            except OSError as problem:
                skipped.append((key, f"cannot read it to compute its SHA-256 ({problem.strerror or type(problem).__name__})"))
                continue
        found = VERSION.search(path.name)
        if found:
            row["version"] = found.group(1)
        if suffix == ".pkg" and platform == "ps4":
            header_version = read_ps4_version(path)       # the file's own version beats the one in its name
            if header_version:
                row["version"] = header_version
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
                if got.get("name") and not extra.get("title") and not own_title:      # apps, DLC and updates too: the site's name, as written
                    row["title"], pr_title = got["name"], True
                for kind, source in (("icon0", "icon"), ("pic1", "hero")):
                    pr_images += prospero_art(title_id, kind, got.get(source, ""), got.get("heroAlt", "") if kind == "pic1" else "")
        db_fields = set()
        to_translate = ""                      # a description from IGDB or the catalogue, in English: translated once everything else is known
        entry = game_db.get(title_id)
        if entry:
            # Game database: after titles.json / param.json / the site, before the AI. Remote cover/hero URLs are not used.
            for field in ("genre", "tagline", "description", "releaseDate", "region"):
                if entry.get(field) and not extra.get(field) and field not in pr_fields and not (pack and field == "tagline"):
                    row[field] = entry[field]
                    db_fields.add(field)
            if entry.get("title") and not extra.get("title") and not (param or {}).get("title") and not pr_title and not own_title:
                row["title"], pr_title = entry["title"], True
            db_hits += 1
        if igdb_on and pack not in ("DLC", "UPDATES"):
            wanted_igdb = [f for f in ("description", "releaseDate") if not extra.get(f) and f not in db_fields and f not in pr_fields]
            got = igdb_lookup(title_id, row["title"])  # found by the game's code, else by its name if wanted_igdb else None
            if got:
                igdb_hits += 1
                if "description" in wanted_igdb and got.get("summary"):
                    text = got["summary"]                                  # IGDB's summary only
                    row["description"] = to_translate = text            # translated at the end
                    db_fields.add("description")
                for field in ("releaseDate",):
                    if field in wanted_igdb and got.get(field):
                        row[field] = got[field]
                        db_fields.add(field)
        if catalog_desc.get(title_id) and pack not in ("DLC", "UPDATES") and not extra.get("description") and "description" not in db_fields:
            row["description"] = to_translate = catalog_desc[title_id]     # the catalogue's own text, when IGDB had none; translated at the end
            db_fields.add("description")
            catalog_hits += 1
        if suffix == ".zip" and not args.no_extract:
            for kind in extract_art(path, title_id, root, args.covers, images, args.refresh_covers):
                extracted.append(f"{title_id}_{kind}.png")
        if pkg and pkg.get("cached") and pkg_cache.get(key, {}).get("hadIcon") and pack != "UPDATES" and not args.no_extract:
            if not (custom_art(root, images, art_id, "icon0") or f"{art_id}_icon0.png".lower() in images):
                fresh = read_pkg(path)         # the cover file was removed: read the package again for its icon
                if fresh:
                    pkg["icon"] = fresh["icon"]
        if pkg and pkg["icon"] and pack != "UPDATES" and not args.no_extract and (pkg["titleId"] in ("", title_id)):
            if store_icon(pkg["icon"], art_id, root, args.covers, images, args.refresh_covers):
                extracted.append(f"{art_id}_icon0.png")
        for field, kind in (("cover", "icon0"), ("hero", "pic1")):
            picture_id = art_id if kind == "icon0" else title_id
            if not args.no_optimize and optimize_art(root, images, picture_id, kind, args.refresh_covers):
                optimized += 1
            hit = artwork_for(images, picture_id, kind) or (artwork_for(images, title_id, kind) if picture_id != title_id else None)
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
        wanted = [f for f in AI_FIELDS if not extra.get(f) and f not in pr_fields and f not in db_fields and not (pack and f == "tagline")]
        if args.ai and keys and wanted and pack not in ("DLC", "UPDATES"):     # the AI is for games and apps only
            info = cache.get(title_id) if isinstance(cache.get(title_id), dict) else None
            if info and info.get("unknown"):
                # Not recognised last time: ask again only after a week.
                info = None if time.time() - float(info.get("at", 0)) > 7 * 86400 else {}
            elif info is not None and info.get("v") != 3:
                info = None                       # saved by an older version, fewer fields
            if info is None and ai_calls < args.ai_max and len(dead_keys) < len(keys) and len(ai_errors) < 3 * len(keys):
                ai_calls += 1
                before = len(ai_errors)
                found_info = ai_lookup(title_id, row["title"], keys, args.ai_model, ai_errors, args.ai_lang, dead_keys, args.ai_provider, args.ai_base)
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
                for field in AI_FIELDS:
                    if field in wanted and info.get(field):
                        row[field] = info[field]
        if translating and to_translate and row.get("description") == to_translate:
            row["description"] = translated(title_id, to_translate)       # last step: your AI translates it (cached)
        try:
            modified = path.stat().st_mtime
        except OSError:
            modified = time.time()
        row.setdefault("addedAt", datetime.fromtimestamp(modified, timezone.utc).replace(microsecond=0).isoformat())
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
    save_caches()
    if made_assets:
        print(f"  default images created in assets/: {', '.join(made_assets)}")
    if from_param:
        print(f"  {from_param} game(s) described by their param.json")
    if game_db:
        print(f"  game database: {db_hits} game(s) found among {len(game_db)} entries")
    if igdb_on:
        save_igdb()
        print(f"  IGDB: {igdb_hits} game(s) completed, {igdb_calls} request(s) this run" + (f" (problem: {igdb_errors[-1]})" if igdb_errors else ""))
        if igdb_calls >= args.igdb_max:
            notes.append(f"IGDB limit of {args.igdb_max} requests per run reached; run the refresh again to continue")
    if desc_urls:
        print(f"  descriptions: {len(catalog_desc)} in the catalogue(s), {catalog_hits} game(s) filled from them")
        if translating:
            save_translations()
            print(f"  translations ({args.ai_lang}): {tr_done} done this run, {tr_calls} request(s)" + (f" (last problem: {tr_errors[-1]})" if tr_errors else "")
                  + ("; the limit of --ai-max per run was reached, run the refresh again to continue" if tr_calls >= args.ai_max else ""))
    if args.prospero:
        saved_art()
        print(f"  prosperopatches.com / orbispatches.com: {pr_done} game(s) completed, {pr_images} image(s) downloaded, {pr_calls} page(s) fetched this run"
              + (f" (problems: {'; '.join(list(dict.fromkeys(pr_errors))[-3:])})" if pr_errors else ""))
        if pr_left:
            notes.append(f"{len(pr_left)} game(s) not looked up yet (limit of {args.prospero_max} pages per run); run the refresh again to continue")
        if pr_fail >= 3:
            notes.append("prosperopatches.com / orbispatches.com refused 3 requests in a row; stopped for this run (blocked by the site's protection? try again later)")
    if args.ai and keys:
        print(f"  AI ({'own AI' if own_ai else 'Gemini'}): {ai_done} game(s) completed, {ai_calls} request(s) this run" + (f" (last problem: {ai_errors[-1]})" if ai_errors else "")
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


_ON_EXIT = []      # functions run when the script ends, however it ends (caches are saved)

if __name__ == "__main__":
    try:
        code = main()
    finally:
        for finish in _ON_EXIT:
            finish()
    sys.exit(code)
