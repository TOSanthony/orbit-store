#!/usr/bin/env python3
"""Writes the two feeds Orbit's in-app updater reads: payloads.json (the ELF) and tv-app.json (the TV app).

    make-update-feeds.py --repo owner/repo --version 1.2.7 --tv-version 1.5.7 \
        --elf build/orbit_store.elf --ffpkg app/dist/PPSA99177.ffpkg --out feeds/

The release is tagged v<version> and holds both binaries; the feeds point at those assets and carry
their SHA-256 (and, for the TV app, its size). Publish the feeds only after the assets are live.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", required=True, help="owner/repo that holds the release")
    parser.add_argument("--version", required=True, help="ELF version, major.minor.patch")
    parser.add_argument("--tv-version", required=True, help="TV app version, major.minor.patch")
    parser.add_argument("--elf", required=True, type=Path)
    parser.add_argument("--ffpkg", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    for label, value in (("version", args.version), ("tv-version", args.tv_version)):
        if not re.fullmatch(r"\d{1,6}\.\d{1,6}\.\d{1,6}", value):
            raise SystemExit(f"--{label} must look like 1.2.3, got {value!r}")
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", args.repo):
        raise SystemExit("--repo must be owner/repo")
    base = f"https://github.com/{args.repo}/releases/download/v{args.version}"
    args.out.mkdir(parents=True, exist_ok=True)
    payloads = {"name": "Orbit Store", "payloads": [{
        "name": "Orbit Store", "filename": "orbit_store.elf", "url": f"{base}/orbit_store.elf",
        "description": "A modern PS5 download manager with a native TV app, resumable downloads and phone controls.",
        "version": f"v{args.version}", "category": "Utilities", "checksum": sha256(args.elf), "receiveProtocol": 1,
    }]}
    tv = {"name": "Orbit Store for TV", "filename": "PPSA99177.ffpkg", "version": args.tv_version,
          "url": f"{base}/PPSA99177.ffpkg", "checksum": sha256(args.ffpkg), "size": args.ffpkg.stat().st_size}
    (args.out / "payloads.json").write_text(json.dumps(payloads, indent=2) + "\n", "utf-8")
    (args.out / "tv-app.json").write_text(json.dumps(tv, indent=2) + "\n", "utf-8")
    print(f"feeds for v{args.version} (TV app {args.tv_version}) written to {args.out}")


if __name__ == "__main__":
    main()
