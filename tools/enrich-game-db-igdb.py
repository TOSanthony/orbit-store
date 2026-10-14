#!/usr/bin/env python3
"""Fill games-db.json from IGDB (real data) and translate it into French with an LLM. Resumable.

  1. IGDB (Twitch credentials) finds each game by its title on PS4 / PS5 and gives the summary, the PlayStation
     release date, genres / themes and, with --images, a cover and a background.
  2. An LLM (Gemini or OpenAI) turns the English summary into a short French description and writes a tagline.
     It only works from the IGDB summary: a game IGDB does not know stays empty, nothing is invented.

Existing values are never overwritten. Keys come from the environment, never from the file:

    export IGDB_CLIENT_ID=... IGDB_CLIENT_SECRET=...
    export GEMINI_API_KEYS="key1,key2"            # or: --llm openai with OPENAI_API_KEY
    python3 enrich-game-db-igdb.py games-db.json --max-games 500

Facts are kept in <db>.igdb-cache.json, so a run that stops (quota, Ctrl+C) loses nothing.
"""
import argparse
import difflib
import json
import os
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime, timezone
from pathlib import Path

FIELDS = ["titleId", "region", "title", "genre", "tagline", "description", "cover", "hero", "releaseDate"]
EXTRAS = re.compile(r"\b(demo|beta|avatar|theme|soundtrack|ost|season pass|dlc|add-?on|pack|wallpaper|trial|dynamic|bonus)\b", re.I)
GENRE_MAP = {
    "Fighting": "Fighting", "Shooter": "Shooter", "Music": "Music/Rhythm", "Platform": "Platformer", "Puzzle": "Puzzle",
    "Racing": "Driving/Racing", "Real Time Strategy (RTS)": "Strategy", "Role-playing (RPG)": "Role Playing Games",
    "Simulator": "Simulation", "Sport": "Sport", "Strategy": "Strategy", "Turn-based strategy (TBS)": "Strategy",
    "Tactical": "Strategy", "Hack and slash/Beat 'em up": "Action", "Adventure": "Adventure", "Arcade": "Arcade",
    "Visual Novel": "Adventure", "Point-and-click": "Adventure", "Quiz/Trivia": "Casual", "Card & Board Game": "Casual",
    "Pinball": "Arcade", "MOBA": "Strategy",
}
THEME_MAP = {"Action": "Action", "Horror": "Horror"}
IGDB_FIELDS = ("name,platforms,summary,storyline,first_release_date,genres.name,themes.name,release_dates.date,release_dates.platform.name,"
               "cover.image_id,artworks.image_id,screenshots.image_id")


def http_json(url, body=None, headers=None, timeout=40):
    if not (url.startswith("https://") or url.startswith("http://127.0.0.1")):
        raise ValueError("not https")
    data = body.encode("utf-8") if isinstance(body, str) else body
    request = urllib.request.Request(url, data, headers or {})
    with urllib.request.urlopen(request, timeout=timeout) as reply:
        return json.loads(reply.read(8 << 20).decode("utf-8"))


def save(path: Path, db: list):
    temporary = path.with_name(path.name + ".part")
    temporary.write_text("[\n" + ",\n".join(json.dumps({f: e.get(f, "") for f in FIELDS}, ensure_ascii=False) for e in db) + "\n]\n", "utf-8")
    temporary.replace(path)


def norm(name: str) -> str:
    name = re.sub(r"[™®©]", "", name or "").lower().replace("&", " and ")
    return re.sub(r"\s+", " ", re.sub(r"[^a-z0-9]+", " ", name)).strip()


def pick(title, games):
    """The IGDB game that really is this title: exact (normalised) name, else a very close one."""
    wanted = norm(title)
    best, score = None, 0.0
    for game in games:
        ratio = 1.0 if norm(game.get("name", "")) == wanted else difflib.SequenceMatcher(None, norm(game.get("name", "")), wanted).ratio()
        if ratio >= 0.9 and set(game.get("platforms") or []) & {48, 167}:
            ratio += 0.05                              # same name on PlayStation beats the PC / Switch entry
        if ratio > score:
            best, score = game, ratio
    return best if best and score >= 0.9 else None


def facts_of(game):
    stamps = [r["date"] for r in game.get("release_dates", []) if r.get("date") and "PlayStation" in (r.get("platform") or {}).get("name", "")]
    stamp = min(stamps) if stamps else game.get("first_release_date")
    genres = [THEME_MAP[t["name"]] for t in game.get("themes", []) if t.get("name") in THEME_MAP and t["name"] == "Action"]
    genres += [GENRE_MAP[g["name"]] for g in game.get("genres", []) if g.get("name") in GENRE_MAP]
    genres += [THEME_MAP[t["name"]] for t in game.get("themes", []) if t.get("name") == "Horror"]
    seen = []
    for g in genres:
        if g not in seen:
            seen.append(g)
    art = (game.get("artworks") or game.get("screenshots") or [{}])[0].get("image_id", "")
    return {
        "name": game.get("name", ""),
        "summary": re.sub(r"\s+", " ", game.get("summary") or game.get("storyline") or "").strip()[:1200],
        "date": datetime.fromtimestamp(stamp, tz=timezone.utc).strftime("%Y-%m-%d") if stamp else "",
        "genre": " / ".join(seen[:3]),
        "cover": (game.get("cover") or {}).get("image_id", ""),
        "art": art,
    }


class Igdb:
    def __init__(self):
        self.id, self.secret, self.token = os.environ.get("IGDB_CLIENT_ID", ""), os.environ.get("IGDB_CLIENT_SECRET", ""), ""
        self.base = os.environ.get("IGDB_BASE", "https://api.igdb.com/v4").rstrip("/")
        self.auth = os.environ.get("IGDB_TOKEN_URL", "https://id.twitch.tv/oauth2/token")

    def login(self):
        query = urllib.parse.urlencode({"client_id": self.id, "client_secret": self.secret, "grant_type": "client_credentials"})
        self.token = http_json(self.auth + "?" + query, b"")["access_token"]

    def search(self, batch):
        """{titleId: [games]} for up to 10 entries, in one multiquery call."""
        def escape(text):
            return text.replace("\\", "\\\\").replace('"', '\\"')
        body = "\n".join(
            f'query games "{e["titleId"]}" {{ search "{escape(e["title"])}"; fields {IGDB_FIELDS}; limit 10; }};'
            for e in batch)
        for attempt in range(2):
            if not self.token:
                self.login()
            try:
                answer = http_json(self.base + "/multiquery", body,
                                   {"Client-ID": self.id, "Authorization": f"Bearer {self.token}", "Content-Type": "text/plain"})
                self.last = json.dumps(answer, ensure_ascii=False)[:500]
                return {item["name"]: item.get("result", []) for item in answer if isinstance(item, dict)}
            except urllib.error.HTTPError as problem:
                if problem.code == 401 and attempt == 0:
                    self.token = ""
                    continue
                raise


def llm_json(kind, prompt, state):
    """One JSON answer from Gemini or OpenAI; None with state['error'] set when it fails."""
    try:
        if kind == "openai":
            base = os.environ.get("OPENAI_BASE", "https://api.openai.com/v1").rstrip("/")
            answer = http_json(base + "/responses", json.dumps({"model": state["model"], "input": prompt, "max_output_tokens": 4000}),
                               {"Authorization": "Bearer " + os.environ.get("OPENAI_API_KEY", ""), "Content-Type": "application/json"}, 90)
            raw = "".join(b.get("text", "") for item in answer.get("output", []) if item.get("type") == "message"
                          for b in item.get("content", []) if b.get("type") == "output_text")
        else:
            base = os.environ.get("GEMINI_BASE", "https://generativelanguage.googleapis.com/v1beta").rstrip("/")
            for key in [k for k in state["keys"] if k not in state["dead"]]:
                try:
                    answer = http_json(f"{base}/models/{state['model']}:generateContent",
                                       json.dumps({"contents": [{"parts": [{"text": prompt}]}], "generationConfig": {"responseMimeType": "application/json"}}),
                                       {"Content-Type": "application/json", "x-goog-api-key": key}, 90)
                    raw = answer["candidates"][0]["content"]["parts"][0]["text"]
                    break
                except urllib.error.HTTPError as problem:
                    state["error"] = f"HTTP {problem.code}: " + re.sub(r"\s+", " ", problem.read(600).decode("utf-8", "replace"))[:160]
                    if problem.code in (400, 401, 403, 429):
                        state["dead"].add(key)
            else:
                return None
        data = json.loads(re.sub(r"^```(?:json)?\s*|\s*```$", "", raw.strip(), flags=re.I))
        data = data.get("games") or data.get("results") or [data] if isinstance(data, dict) else data
        return [x for x in data if isinstance(x, dict)]
    except urllib.error.HTTPError as problem:
        state["error"] = f"HTTP {problem.code}: " + re.sub(r"\s+", " ", problem.read(600).decode("utf-8", "replace"))[:160]
    except (urllib.error.URLError, OSError, ValueError, KeyError, IndexError, TypeError) as problem:
        state["error"] = type(problem).__name__
    return None


def french_prompt(items):
    lines = "\n".join(json.dumps({"id": i["id"], "title": i["title"], "summary": i["summary"]}, ensure_ascii=False) for i in items)
    return (
        "Tu es rédacteur spécialisé dans les jeux vidéo. Pour chaque jeu ci-dessous (titre et résumé anglais fiable), écris :\n"
        '- "description" : 1 à 2 phrases en français, 220 caractères maximum, fidèles au résumé (ce que l\'on fait dans le jeu), sans formule publicitaire ;\n'
        '- "tagline" : un slogan français original de 4 à 10 mots, qui se termine par un point, sans recopier le début de la description.\n'
        "N'invente aucun élément qui n'est pas dans le résumé.\n\n" + lines +
        '\n\nRéponds STRICTEMENT par un tableau JSON : [{"id": "...", "description": "...", "tagline": "..."}]')


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("db", type=Path)
    parser.add_argument("-o", "--output", type=Path, help="default: overwrite the input file")
    parser.add_argument("--max-games", type=int, default=200, help="most games looked up on IGDB per run (default 200)")
    parser.add_argument("--llm", choices=("gemini", "openai", "none"), default="gemini", help="who writes the French text (none: only IGDB facts)")
    parser.add_argument("--model", help="default: GEMINI_MODEL / gemini-3.6-flash, or OPENAI_MODEL for openai")
    parser.add_argument("--images", action="store_true", help="also fill an empty cover / hero with IGDB images")
    parser.add_argument("--platform", choices=("ps4", "ps5"))
    parser.add_argument("--extras", action="store_true", help="also do demos, avatars, themes, soundtracks...")
    parser.add_argument("--delay", type=float, default=0.4, help="seconds between IGDB calls (limit: 4 a second)")
    args = parser.parse_args()

    raw = json.loads(args.db.read_text("utf-8"))
    db = raw.get("releases", []) if isinstance(raw, dict) else raw
    out = args.output or args.db
    cache_path = out.with_name(out.name + ".igdb-cache.json")
    try:
        cache = json.loads(cache_path.read_text("utf-8")) if cache_path.is_file() else {}
    except (OSError, ValueError):
        cache = {}
    cache = cache if isinstance(cache, dict) else {}
    if cache.get("v") != 2:
        cache = {"v": 2}                               # older cache: its "not found" answers came from a stricter search
    cache.setdefault("facts", {})
    cache.setdefault("none", {})
    cache.setdefault("textfail", {})
    month = 30 * 86400

    def eligible(e):
        if args.platform and e["titleId"].startswith("CUSA") != (args.platform == "ps4"):
            return False
        return bool(e.get("title")) and (args.extras or not EXTRAS.search(e["title"]))

    def incomplete(e):
        return not (e.get("description") and e.get("tagline") and e.get("genre") and e.get("releaseDate")) or \
            (args.images and not (e.get("cover") and e.get("hero")))

    todo = [e for e in db if eligible(e) and incomplete(e)]
    todo.sort(key=lambda e: (not e.get("cover"), e["titleId"]))
    by_id = {e["titleId"]: e for e in db}
    notes = []

    # 1. IGDB
    igdb, looked, warned = Igdb(), 0, False
    if not (igdb.id and igdb.secret):
        print("IGDB_CLIENT_ID / IGDB_CLIENT_SECRET missing", file=sys.stderr)
        return 2
    pending = [e for e in todo if e["titleId"] not in cache["facts"] and time.time() - float(cache["none"].get(e["titleId"], 0)) > month]
    try:
        while pending and looked < args.max_games:
            batch, pending = pending[:min(10, args.max_games - looked)], pending[min(10, args.max_games - looked):]
            looked += len(batch)
            found = igdb.search(batch)
            if not any(found.get(e["titleId"]) for e in batch) and not warned:
                warned = True
                print("  warning: IGDB returned no game for a whole batch; raw answer starts with: " + getattr(igdb, "last", "?"), file=sys.stderr)
            for e in batch:
                game = pick(e["title"], found.get(e["titleId"], []))
                if game:
                    cache["facts"][e["titleId"]] = facts_of(game)
                else:
                    cache["none"][e["titleId"]] = time.time()
            if looked % 50 < 10:
                cache_path.write_text(json.dumps(cache), "utf-8")
                print(f"  IGDB: {looked} game(s) looked up", file=sys.stderr)
            time.sleep(args.delay)
    except (urllib.error.URLError, OSError, ValueError, KeyError) as problem:
        notes.append(f"IGDB stopped: {type(problem).__name__} {getattr(problem, 'code', '')}")
    cache_path.write_text(json.dumps(cache), "utf-8")

    # 2. facts into the database (genre, date, images), then French text
    filled = 0
    for e in todo:
        f = cache["facts"].get(e["titleId"])
        if not f:
            continue
        if not e.get("genre") and f["genre"]:
            e["genre"], filled = f["genre"], filled + 1
        if not e.get("releaseDate") and f["date"]:
            e["releaseDate"] = f["date"]
        if args.images:
            if not e.get("cover") and f["cover"]:
                e["cover"] = f"https://images.igdb.com/igdb/image/upload/t_cover_big/{f['cover']}.jpg"
            if not e.get("hero") and f["art"]:
                e["hero"] = f"https://images.igdb.com/igdb/image/upload/t_1080p/{f['art']}.jpg"
    texted = 0
    if args.llm != "none":
        keys = [k.strip() for k in re.split(r"[,\s]+", os.environ.get("GEMINI_API_KEYS", "")) if k.strip()]
        state = {"keys": keys, "dead": set(), "error": "", "model": args.model or (os.environ.get("OPENAI_MODEL", "gpt-6-luna") if args.llm == "openai" else os.environ.get("GEMINI_MODEL", "gemini-3.6-flash"))}
        if args.llm == "gemini" and not keys:
            notes.append("no key in GEMINI_API_KEYS: French text skipped")
        elif args.llm == "openai" and not os.environ.get("OPENAI_API_KEY"):
            notes.append("OPENAI_API_KEY missing: French text skipped")
        else:
            need = [e for e in todo if cache["facts"].get(e["titleId"], {}).get("summary") and not (e.get("description") and e.get("tagline"))
                    and time.time() - float(cache["textfail"].get(e["titleId"], 0)) > month]
            fails = 0
            while need and fails < 3 and not (args.llm == "gemini" and len(state["dead"]) >= len(keys)):
                batch, need = need[:10], need[10:]
                answers = llm_json(args.llm, french_prompt([{"id": e["titleId"], "title": e["title"], "summary": cache["facts"][e["titleId"]]["summary"]} for e in batch]), state)
                if not answers:
                    fails += 1
                    continue
                fails = 0
                got = {str(a.get("id", "")).upper(): a for a in answers}
                for e in batch:
                    a = got.get(e["titleId"])
                    description = re.sub(r"\s+", " ", str((a or {}).get("description", ""))).strip()[:240]
                    tagline = re.sub(r"\s+", " ", str((a or {}).get("tagline", ""))).strip().strip("\"'«» ")[:90]
                    if len(description) < 30 or not tagline:
                        cache["textfail"][e["titleId"]] = time.time()
                        continue
                    e.setdefault("description", "")
                    if not e["description"]:
                        e["description"] = description
                    if not e.get("tagline"):
                        e["tagline"] = tagline
                    texted += 1
                if texted % 50 < 10:
                    save(out, db)
                    cache_path.write_text(json.dumps(cache), "utf-8")
                time.sleep(2.0)
            if state["error"] and (fails or state["dead"]):
                notes.append("LLM stopped: " + state["error"])
    save(out, db)
    cache_path.write_text(json.dumps(cache), "utf-8")
    left = sum(1 for e in db if eligible(e) and incomplete(e) and e["titleId"] not in cache["none"])
    print(f"{looked} game(s) looked up on IGDB ({len(cache['facts'])} known, {len(cache['none'])} not found), {texted} French text(s) written; about {left} incomplete game(s) left")
    for note in notes:
        print("  note:", note)
    return 0


if __name__ == "__main__":
    sys.exit(main())
