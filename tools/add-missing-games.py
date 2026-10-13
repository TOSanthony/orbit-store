#!/usr/bin/env python3
"""Add the PS4 / PS5 games missing from games-db.json (title ID, region, title, cover).

Sources (public GitHub data, downloaded once, no PlayStation account needed):
  - andshrew/supreme-enigma   docs/PS5-BC-Status.json   ~32,000 PS4 title IDs with name and region
  - Ephellon/game-store-catalog  ps4/!.json, ps5/!.json  PlayStation Store listings: name, cover, product ID

Existing entries are never overwritten: a missing region or cover is filled in, nothing else changes.
New entries only carry titleId, region, title and (when the Store lists it) cover; genre, tagline,
description, hero and releaseDate stay empty until make-nas-index.py (--prospero / --ai) or you fill them.

    python3 add-missing-games.py games-db.json -o games-db.json
    python3 add-missing-games.py games-db.json --cache ./sources     # keep the downloads
"""
import argparse
import json
import re
import sys
import urllib.request
from pathlib import Path

SOURCES = {
    "bc.json": "https://raw.githubusercontent.com/andshrew/supreme-enigma/main/docs/PS5-BC-Status.json",
    "ps4.json": "https://raw.githubusercontent.com/Ephellon/game-store-catalog/main/ps4/!.json",
    "ps5.json": "https://raw.githubusercontent.com/Ephellon/game-store-catalog/main/ps5/!.json",
}
FIELDS = ["titleId", "region", "title", "genre", "tagline", "description", "cover", "hero", "releaseDate"]
REGION = {"U": "USA", "E": "EUR", "J": "JPN", "H": "ASIA", "K": "ASIA", "I": "USA", "A": "ASIA"}
ID = re.compile(r"(PPSA|CUSA)(\d{5})")


def fetch(name: str, cache: Path):
    path = cache / name
    if not path.is_file():
        print(f"downloading {SOURCES[name]}", file=sys.stderr)
        request = urllib.request.Request(SOURCES[name], headers={"User-Agent": "orbit-store"})
        with urllib.request.urlopen(request, timeout=120) as reply:
            path.write_bytes(reply.read())
    return json.loads(path.read_text("utf-8"))


def clean(name: str) -> str:
    return re.sub(r"\s+", " ", re.sub(r"[™®]", "", name or "")).strip()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("db", type=Path, help="games-db.json to complete")
    parser.add_argument("-o", "--output", type=Path, help="default: overwrite the input file")
    parser.add_argument("--cache", type=Path, default=Path(".game-sources"), help="folder for the downloaded sources")
    args = parser.parse_args()

    raw = json.loads(args.db.read_text("utf-8"))
    db = raw.get("releases", []) if isinstance(raw, dict) else raw
    args.cache.mkdir(parents=True, exist_ok=True)

    found = {}                                           # titleId -> {"title", "region", "cover"}

    def note(title_id, name, region, cover=""):
        name = clean(name)
        if not name:
            return
        item = found.setdefault(title_id, {"title": name, "region": region, "cover": cover})
        # Several editions share one title ID in the Store: keep the shortest name (the base game).
        if len(name) < len(item["title"]):
            item["title"] = name
        item["region"] = item["region"] or region
        item["cover"] = item["cover"] or cover

    for entry in fetch("bc.json", args.cache):           # PS4 only: every region has its own CUSA
        if entry.get("category") == "gd" and entry.get("npTitleIdshort", "").startswith("CUSA"):
            note(entry["npTitleIdshort"], entry.get("name", ""), REGION.get((entry.get("region") or " ")[0], ""))
    for name in ("ps4.json", "ps5.json"):
        for _, item in fetch(name, args.cache):
            uuid = item.get("uuid", "")
            match = ID.search(uuid)
            if match and item.get("type", "game") == "game":
                note(match.group(0), item.get("name", ""), REGION.get(uuid[:1], ""), item.get("image", ""))

    index = {e["titleId"]: e for e in db}
    added = filled = 0
    for title_id, item in found.items():
        have = index.get(title_id)
        if have is None:
            entry = {field: "" for field in FIELDS}
            entry.update(titleId=title_id, region=item["region"], title=item["title"], cover=item["cover"])
            db.append(entry)
            added += 1
        else:
            for field in ("region", "cover"):
                if not have.get(field) and item[field]:
                    have[field] = item[field]
                    filled += 1
    db.sort(key=lambda e: e["titleId"])
    out = args.output or args.db
    out.write_text("[\n" + ",\n".join(json.dumps({f: e.get(f, "") for f in FIELDS}, ensure_ascii=False) for e in db) + "\n]\n", "utf-8")
    ps4 = sum(1 for e in db if e["titleId"].startswith("CUSA"))
    print(f"{added} game(s) added, {filled} missing region/cover filled; {len(db)} entries ({ps4} PS4, {len(db) - ps4} PS5) -> {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
