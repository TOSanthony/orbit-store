#!/usr/bin/env python3
"""Pinned public-domain SQLite source; shared by host tests and the PS5 build."""
import hashlib
import pathlib
import urllib.request
import zipfile

root = pathlib.Path(__file__).resolve().parent.parent
archive = root / '.deps/sqlite-amalgamation-3540100.zip'
archive.parent.mkdir(exist_ok=True)
if not archive.exists():
    with urllib.request.urlopen('https://www.sqlite.org/2026/sqlite-amalgamation-3540100.zip', timeout=60) as response:
        archive.write_bytes(response.read(8 * 1024 * 1024))
if hashlib.sha256(archive.read_bytes()).hexdigest() != '0a9c8e34352dad46006aa82836d5383bff9dc6bc9ce5a60553c05aa30e6ad1f9':
    raise SystemExit('SQLite archive checksum mismatch')
target = root / '.deps/sqlite'
target.mkdir(exist_ok=True)
with zipfile.ZipFile(archive) as source:
    for name in ['sqlite3.c', 'sqlite3.h']:
        data = source.read('sqlite-amalgamation-3540100/' + name)
        if name == 'sqlite3.c' and hashlib.sha3_256(data).hexdigest() != 'e2b869d3003b33c942b7dd7063604fbc53553e1b66609570df39b9b7b558ee81':
            raise SystemExit('SQLite source checksum mismatch')
        (target / name).write_bytes(data)
