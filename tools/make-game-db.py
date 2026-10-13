#!/usr/bin/env python3
"""Turn one or more Orbit catalogs (JSON lists of releases, or {"releases": [...]}) into a small game database:
one complete entry per title ID, without any download information.

  python3 make-game-db.py database.json [more.json ...] -o games-db.json

Kept: titleId, region, title, genre, tagline, description, cover, hero, releaseDate.
Rows sharing a title ID are merged: for each field the first non-empty value wins, and a longer
description beats a shorter one. DLC / UPDATES / APPS rows keep their own tagline only if no game row exists.
"""
import argparse
import json
import sys

FIELDS = ["titleId", "region", "title", "genre", "tagline", "description", "cover", "hero", "releaseDate"]
PACKS = {"DLC", "UPDATES", "APPS"}


def rows_of(data):
    if isinstance(data, dict):
        data = data.get("releases") or data.get("games") or []
    return [r for r in data if isinstance(r, dict) and r.get("titleId")]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("inputs", nargs="+")
    parser.add_argument("-o", "--output", default="games-db.json")
    args = parser.parse_args()
    merged = {}
    for name in args.inputs:
        with open(name, encoding="utf-8") as handle:
            rows = rows_of(json.load(handle))
        for row in rows:
            entry = merged.setdefault(row["titleId"].upper(), {})
            for field in FIELDS:
                value = row.get(field)
                if value in (None, "", []):
                    continue
                if field == "tagline" and value in PACKS and entry.get("tagline") not in (None, *PACKS):
                    continue
                if field == "tagline" and entry.get("tagline") in PACKS and value not in PACKS:
                    entry[field] = value
                elif field == "description":
                    if len(str(value)) > len(str(entry.get(field, ""))):
                        entry[field] = value
                else:
                    entry.setdefault(field, value)
    games = [{f: merged[t].get(f, "") for f in FIELDS} | {"titleId": t} for t in sorted(merged)]
    with open(args.output, "w", encoding="utf-8") as handle:
        json.dump(games, handle, ensure_ascii=False, indent=1)
    print(f"{len(games)} game(s) written to {args.output}", file=sys.stderr)


if __name__ == "__main__":
    main()
