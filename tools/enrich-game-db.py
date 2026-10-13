#!/usr/bin/env python3
"""Fill the empty fields of games-db.json: genre, tagline, description, release date (Gemini, in French)
and title, cover, hero (prosperopatches.com / orbispatches.com). Resumable: run it again and again.

Nothing is invented on purpose: when Gemini is not sure about a game it answers nothing, the fields stay
empty and the game is asked again only after 30 days. Existing values are never overwritten.

    export GEMINI_API_KEYS="key1,key2"
    python3 enrich-game-db.py games-db.json --max-requests 200            # texts, 15 games per request
    python3 enrich-game-db.py games-db.json --prospero --prospero-max 2000 # images (1 page per second)

Order: games with a Store cover first (real releases), demos / avatars / themes / soundtracks last
(left out unless you pass --extras). Progress and "unknown" answers live in <db>.enrich-cache.json.
"""
import argparse
import importlib.util
import json
import os
import re
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parent
FIELDS = ["titleId", "region", "title", "genre", "tagline", "description", "cover", "hero", "releaseDate"]
EXTRAS = re.compile(r"\b(demo|beta|avatar|theme|soundtrack|ost|season pass|dlc|add-?on|pack|wallpaper|trial|dynamic|bonus)\b", re.I)
RETRY_AFTER = 30 * 86400


GENRES = "Action, Adventure, Role Playing Games, Horror, Driving/Racing, Shooter, Strategy, Fighting, Sport, Simulation, Puzzle, Platformer, Family, Music/Rhythm, Arcade, Casual, Unique"


def load_index_module():
    """make-nas-index.py (page reader for --prospero), looked for beside this script, one folder up, or here."""
    for folder in (HERE, HERE.parent, HERE / "tools", Path.cwd()):
        path = folder / "make-nas-index.py"
        if path.is_file():
            spec = importlib.util.spec_from_file_location("make_nas_index", path)
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            return module
    return None


def save(path: Path, db: list):
    temporary = path.with_name(path.name + ".part")
    temporary.write_text("[\n" + ",\n".join(json.dumps({f: e.get(f, "") for f in FIELDS}, ensure_ascii=False) for e in db) + "\n]\n", "utf-8")
    temporary.replace(path)


def prompt(batch, genres: str, lang: str) -> str:
    lines = "\n".join(f'- {e["titleId"]}: {e["title"]}' for e in batch)
    return (
        "You are an expert on PlayStation 4 and PlayStation 5 games.\n"
        "For each game below (Title ID: store name) give its details. Only answer for games you really know; "
        "for a game you do not recognise with confidence, leave its fields empty. Never invent anything.\n\n"
        f"{lines}\n\n"
        "Return STRICTLY a JSON array with one object per game, in this form:\n"
        '{"id": "the Title ID", '
        f'"genre": "1 to 3 genres from: {genres}; separate with \\" / \\"", '
        f'"tagline": "Short punchy tagline in {lang}, 4 to 10 words, ending with a period", '
        f'"description": "Summary in {lang}, 1 to 2 sentences, max 220 characters, what you do in the game, no marketing hype", '
        '"releaseDate": "Original release date YYYY-MM-DD, or empty if unknown"}'
    )


def ask(batch, keys, dead, model, genres, lang, errors):
    base = os.environ.get("GEMINI_BASE", "https://generativelanguage.googleapis.com/v1beta").rstrip("/")
    if not (base.startswith("https://") or base.startswith("http://127.0.0.1")):
        errors.append("GEMINI_BASE must be https")
        return None
    body = json.dumps({"contents": [{"parts": [{"text": prompt(batch, genres, lang)}]}],
                       "generationConfig": {"responseMimeType": "application/json"}}).encode("utf-8")
    for key in keys:
        if key in dead:
            continue
        request = urllib.request.Request(f"{base}/models/{model}:generateContent", body,
                                         {"Content-Type": "application/json", "x-goog-api-key": key})
        payload = None
        for attempt in range(3):
            try:
                with urllib.request.urlopen(request, timeout=90) as reply:
                    payload = json.loads(reply.read(4 << 20).decode("utf-8"))
                break
            except urllib.error.HTTPError as problem:
                try:
                    detail = problem.read(20000).decode("utf-8", "replace")
                except OSError:
                    detail = ""
                try:
                    message = json.loads(detail)["error"]["message"]
                except (ValueError, KeyError, TypeError):
                    message = detail
                message = re.sub(r"\s+", " ", message)[:200]
                errors.append(f"HTTP {problem.code}: {message}" if message else f"HTTP {problem.code}")
                wait = re.search(r"retry(?:Delay)?\D{0,20}?([\d.]+)\s*s", detail, re.I)
                per_day = re.search(r"per ?day|daily", detail, re.I)
                if problem.code == 429 and not per_day and attempt < 2 and (not wait or float(wait.group(1)) <= 90):
                    time.sleep(min(90.0, float(wait.group(1)) + 1 if wait else 30.0))    # per-minute limit: wait, same key
                    continue
                if problem.code in (400, 401, 403, 429):
                    dead.add(key)
                break
            except (urllib.error.URLError, OSError, ValueError) as problem:
                errors.append(type(problem).__name__)
                break
        if payload is None:
            continue
        try:
            text = payload["candidates"][0]["content"]["parts"][0]["text"]
            data = json.loads(re.sub(r"^```(?:json)?\s*|\s*```$", "", text.strip(), flags=re.I))
        except (KeyError, IndexError, TypeError, ValueError):
            errors.append("unreadable answer")
            continue
        if isinstance(data, dict):
            data = data.get("games") or data.get("results") or [data]
        if isinstance(data, list):
            return [x for x in data if isinstance(x, dict)]
    return None


def text(value, limit: int) -> str:
    return re.sub(r"\s+", " ", str(value or "")).strip()[:limit].rstrip()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("db", type=Path)
    parser.add_argument("-o", "--output", type=Path, help="default: overwrite the input file")
    parser.add_argument("--lang", default="French")
    parser.add_argument("--batch", type=int, default=15, help="games per Gemini request (default 15)")
    parser.add_argument("--max-requests", type=int, default=100, help="most Gemini requests per run (default 100)")
    parser.add_argument("--delay", type=float, default=4.0, help="seconds between two requests (default 4, free keys allow about 10 a minute)")
    parser.add_argument("--model", default=os.environ.get("GEMINI_MODEL", "gemini-3.6-flash"))
    parser.add_argument("--platform", choices=("ps4", "ps5"), help="only CUSA or only PPSA titles")
    parser.add_argument("--extras", action="store_true", help="also do demos, avatars, themes, soundtracks...")
    parser.add_argument("--prospero", action="store_true", help="fill empty cover / hero from prosperopatches.com / orbispatches.com instead of asking Gemini")
    parser.add_argument("--overwrite", action="store_true", help="with --prospero: the site's title, cover and hero replace the ones already in the database")
    parser.add_argument("--prospero-max", type=int, default=400, help="most pages per run (default 400)")
    args = parser.parse_args()

    raw = json.loads(args.db.read_text("utf-8"))
    db = raw.get("releases", []) if isinstance(raw, dict) else raw
    out = args.output or args.db
    cache_path = out.with_name(out.name + ".enrich-cache.json")
    try:
        cache = json.loads(cache_path.read_text("utf-8")) if cache_path.is_file() else {}
    except (OSError, ValueError):
        cache = {}
    cache = cache if isinstance(cache, dict) else {}

    def wanted(e):
        if args.platform and e["titleId"].startswith("CUSA") != (args.platform == "ps4"):
            return False
        return args.extras or not EXTRAS.search(e.get("title", ""))

    def recently_tried(key, entry_id):
        return time.time() - float(cache.get(key, {}).get(entry_id, 0)) < RETRY_AFTER

    cache.setdefault("text", {})
    cache.setdefault("art", {})
    changed = False

    if args.prospero:
        index = load_index_module()
        if index is None:
            print("--prospero needs make-nas-index.py next to this script", file=sys.stderr)
            return 2
        def needs_site(e):
            return wanted(e) and (args.overwrite or not e.get("hero") or not e.get("cover") or not e.get("title")) and not recently_tried("art", e["titleId"])
        todo = [e for e in db if needs_site(e)]
        todo.sort(key=lambda e: (bool(e.get("hero")), e["titleId"]))
        done = calls = failures = 0
        for e in todo:
            if calls >= args.prospero_max or failures >= 3:
                break
            title_id = e["titleId"]
            if calls:
                time.sleep(1.0)
            calls += 1
            if title_id.startswith("CUSA"):
                base, cdn = (os.environ.get("ORBIS_BASE", "https://orbispatches.com").rstrip("/"),
                             os.environ.get("ORBIS_CDN", "https://cdn.orbispatches.com").rstrip("/"))
            else:
                base, cdn = (os.environ.get("PROSPERO_BASE", "https://prosperopatches.com").rstrip("/"),
                             os.environ.get("PROSPERO_CDN", "https://cdn.prosperopatches.com").rstrip("/"))
            data, why = index.http_get_page(f"{base}/{title_id}")
            if data is None:
                if why == "HTTP 404":
                    cache["art"][title_id] = time.time()
                else:
                    failures += 1
                    print(f"  {title_id}: {why}", file=sys.stderr)
                continue
            page = data.decode("utf-8", "replace")
            if "bd-links-group" not in page:
                failures += 1
                print(f"  {title_id}: page not readable (blocked by the site?)", file=sys.stderr)
                continue
            failures = 0
            got = index.prospero_parse(page, title_id, cdn)
            cache["art"][title_id] = time.time()
            if got:
                for field, source in (("cover", "icon"), ("hero", "hero")):
                    if got.get(source) and (args.overwrite or not e.get(field)):
                        e[field], changed = got[source], True
                if got.get("name") and (args.overwrite or not e.get("title")):
                    e["title"], changed = got["name"], True
                if not e.get("region") and got.get("region"):
                    e["region"], changed = got["region"], True
                done += 1
            if calls % 25 == 0:
                save(out, db)
                cache_path.write_text(json.dumps(cache), "utf-8")
        save(out, db)
        cache_path.write_text(json.dumps(cache), "utf-8")
        left = sum(1 for e in db if needs_site(e))
        print(f"{done} game(s) completed from the site, {calls} page(s) fetched this run, about {left} still to do")
        return 0

    keys = [k.strip() for k in re.split(r"[,\s]+", os.environ.get("GEMINI_API_KEYS", "")) if k.strip()]
    if not keys:
        print("no key in GEMINI_API_KEYS", file=sys.stderr)
        return 2
    genres = getattr(load_index_module(), "GENRES", GENRES)
    todo = [e for e in db if wanted(e) and not e.get("description") and not recently_tried("text", e["titleId"])]
    todo.sort(key=lambda e: (not e.get("cover"), e["titleId"]))
    total = len(todo)
    dead, errors, requests, filled = set(), [], 0, 0
    while todo and requests < args.max_requests and len(dead) < len(keys) and len(errors) < 3 * len(keys):
        batch, todo = todo[:args.batch], todo[args.batch:]
        if requests:
            time.sleep(args.delay)
        requests += 1
        answers = ask(batch, keys, dead, args.model, genres, args.lang, errors)
        if answers is None:
            continue                                   # not cached: tried again next run
        errors.clear()
        by_id = {text(a.get("id"), 12).upper(): a for a in answers}
        for e in batch:
            a = by_id.get(e["titleId"])
            description, tagline = text(a and a.get("description"), 240), text(a and a.get("tagline"), 90)
            if not a or len(description) < 30 or not tagline:
                cache["text"][e["titleId"]] = time.time()      # not recognised: ask again in 30 days
                continue
            date = text(a.get("releaseDate"), 10)
            e["description"], e["tagline"] = description, tagline
            if not e.get("genre"):
                e["genre"] = text(a.get("genre"), 60)
            if not e.get("releaseDate") and re.fullmatch(r"\d{4}-\d{2}-\d{2}", date):
                e["releaseDate"] = date
            filled += 1
        if requests % 5 == 0:
            save(out, db)
            cache_path.write_text(json.dumps(cache), "utf-8")
        print(f"  request {requests}: {filled} game(s) filled so far", file=sys.stderr)
    save(out, db)
    cache_path.write_text(json.dumps(cache), "utf-8")
    print(f"{filled} game(s) completed in {requests} request(s); about {len(todo)} still to do" +
          (f" (last problem: {errors[-1]})" if errors else "") + (" - all keys used up, run again later" if len(dead) >= len(keys) else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
