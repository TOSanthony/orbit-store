#!/usr/bin/env python3
"""Builds test databases shaped like the console's app.db (table tbl_contentinfo) for tests/appdb_test.c."""
import json, os, random, sqlite3, sys

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
SCHEMA = ("CREATE TABLE tbl_contentinfo (titleId TEXT PRIMARY KEY, titleName TEXT, contentId TEXT, "
          "icon0Info TEXT, AppInfoJson TEXT, extra INTEGER)")

def info(i, padding=0):
    blob = {"CONTENT_VERSION": "06.%03d.000" % (i % 1000),
            "VERSION_FILE_URI": "https:\\/\\/sgst.prod.dl.playstation.net\\/sgst\\/prod\\/00\\/np\\/PPSA%05d_00\\/%08d-aaaa-bbbb-cccc-0123456789ab-version.xml   " % (i, i),
            "OTHER": "x" * padding}
    # the console escapes slashes: write the JSON text by hand so that "\/" stays as it is
    return ('{"CONTENT_VERSION":"%s","VERSION_FILE_URI":"%s","OTHER":"%s"}'
            % (blob["CONTENT_VERSION"], blob["VERSION_FILE_URI"], blob["OTHER"]))

def make(name, page_size, rows, padding=0, encoding="UTF-8", wal=False):
    path = os.path.join(out, name)
    if os.path.exists(path): os.remove(path)
    con = sqlite3.connect(path)
    con.execute("PRAGMA page_size=%d" % page_size)
    con.execute("PRAGMA encoding='%s'" % encoding)
    con.execute(SCHEMA)
    con.execute("CREATE INDEX idx_name ON tbl_contentinfo(titleName)")
    random.seed(7)
    ids = list(range(1, rows + 1)); random.shuffle(ids)
    for i in ids:
        pad = padding if i % 3 == 0 else 0
        con.execute("INSERT INTO tbl_contentinfo VALUES (?,?,?,?,?,?)",
                    ("PPSA%05d" % i, "Game %d é" % i, "UP0000-PPSA%05d_00-X" % i, "{}", info(i, pad), i))
    # a row that is not a game, and a PS4 row with APP_VER
    con.execute("INSERT INTO tbl_contentinfo VALUES ('junk','x','x','{}','{}',0)")
    con.execute("INSERT INTO tbl_contentinfo VALUES ('CUSA16194','PS4 game','c','{}','{\"APP_VER\":\"01.05\"}',0)")
    con.commit(); con.close()

make("small.db", 4096, 5)
make("many.db", 4096, 3000)                 # interior pages
make("overflow.db", 4096, 40, padding=9000)  # AppInfoJson spills onto overflow pages
make("tiny_pages.db", 512, 300, padding=1500)
make("big_pages.db", 65536, 400, padding=70000)
make("utf16.db", 4096, 3, encoding="UTF-16le")
open(os.path.join(out, "garbage.db"), "wb").write(os.urandom(20000))
open(os.path.join(out, "empty.db"), "wb").write(b"")
if os.path.exists(os.path.join(out, "other.db")): os.remove(os.path.join(out, "other.db"))
con = sqlite3.connect(os.path.join(out, "other.db")); con.execute("CREATE TABLE t(a)"); con.commit(); con.close()
print("ok")
