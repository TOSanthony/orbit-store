#!/usr/bin/env python3
"""Checks the game-update worker end to end with a fake app.db, param.json files and a fake Sony server."""
import http.server, json, os, sqlite3, subprocess, sys, tempfile, threading

driver = sys.argv[1]
TEMPLATE = ('<title_patch nptitleid="%(id)s_00"><app_tag content_id="UP4001-%(id)s_00-X">'
            '<package content_ver="%(ver)s" manifest_url="https://sgst.prod.dl.playstation.net/sgst/prod/00/%(id)s_00/app/info/1/f_a/UP4001-%(id)s_00-X.json" '
            'delta_url="http://gst.prod.dl.playstation.net/gst/x-DP.pkg" system_ver="%(fw)d"/></app_tag></title_patch>')
SERVED = {  # basename -> version.xml
    "PPSA00001-version.xml": TEMPLATE % dict(id="PPSA00001", ver="06.005.000", fw=0x10200006),
    "PPSA00002-version.xml": TEMPLATE % dict(id="PPSA00002", ver="06.005.000", fw=0x10200006),
    "PPSA00003-version.xml": TEMPLATE % dict(id="PPSA00003", ver="02.000.000", fw=0x11000000),
    "PPSA00005-version.xml": TEMPLATE % dict(id="PPSA00005", ver="01.000.000", fw=0x10200006),
    "PPSA00006-version.xml": "not xml at all",
}
import hashlib, re
BIN = {}        # path -> bytes (patch pieces and manifests)
LOG = []        # every request path, in order
BEHAVE = {}     # path -> "bad-once" | "bad-always" | "drop-once" | "gone"
class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        name = self.path.lstrip("/")
        LOG.append(name)
        body = SERVED.get(name)
        data = body.encode() if body is not None else BIN.get(name)
        mode = BEHAVE.get(name)
        if data is None or mode == "gone":
            self.send_response(404); self.end_headers(); return
        start, end, code = 0, len(data), 200
        m = re.match(r"bytes=(\d+)-(\d+)", self.headers.get("Range", ""))
        if m:
            start, end, code = int(m.group(1)), int(m.group(2)) + 1, 206
        chunk = data[start:end]
        if mode in ("bad-once", "bad-always"):
            chunk = bytes((b ^ 0xFF) for b in chunk)
            if mode == "bad-once": BEHAVE.pop(name)
        self.send_response(code)
        self.send_header("Content-Length", str(len(chunk)))
        if code == 206: self.send_header("Content-Range", "bytes %d-%d/%d" % (start, end - 1, len(data)))
        self.end_headers()
        if mode == "drop-once":
            BEHAVE.pop(name)
            self.wfile.write(chunk[: len(chunk) // 2]); self.wfile.flush()
            self.connection.close(); return
        self.wfile.write(chunk)
    def log_message(self, *a): pass
server = http.server.HTTPServer(("127.0.0.1", 0), Handler)
threading.Thread(target=server.serve_forever, daemon=True).start()

def uri(i):
    return "https:\\/\\/sgst.prod.dl.playstation.net\\/sgst\\/prod\\/00\\/np\\/PPSA%05d_00\\/PPSA%05d-version.xml" % (i, i)

def build(root, rows, params):
    os.makedirs(root + "/system_data/priv/mms")
    con = sqlite3.connect(root + "/system_data/priv/mms/app.db")
    con.execute("CREATE TABLE tbl_contentinfo (titleId TEXT PRIMARY KEY, titleName TEXT, AppInfoJson TEXT)")
    for i, ver in rows:
        con.execute("INSERT INTO tbl_contentinfo VALUES (?,?,?)",
                    ("PPSA%05d" % i, "Game %d" % i,
                     '{"CONTENT_VERSION":"%s","VERSION_FILE_URI":"%s"}' % (ver, uri(i))))
    con.execute("INSERT INTO tbl_contentinfo VALUES ('CUSA16194','PS4 game','{\"APP_VER\":\"01.05\"}')")
    con.commit(); con.close()
    for i, ver in params.items():
        d = root + "/user/appmeta/PPSA%05d" % i
        os.makedirs(d)
        json.dump({"contentVersion": ver}, open(d + "/param.json", "w"))

def run(root, firmware, download=None, storage=None):
    env = dict(os.environ, ORBIT_TEST_GAMEUPDATES_BASE="http://127.0.0.1:%d" % server.server_port)
    if storage: env["ORBIT_TEST_STORAGE"] = storage
    args = [driver, root, firmware] + ([download] if download else [])
    out = subprocess.run(args, env=env, capture_output=True, text=True, timeout=120)
    assert out.returncode == 0, (out.returncode, out.stderr)
    return json.loads(out.stdout)

failures = 0
def check(name, ok):
    global failures
    print(("ok   " if ok else "FAIL ") + name)
    failures += not ok

with tempfile.TemporaryDirectory() as root:
    build(root, [(1, "05.000.000"), (2, "05.000.000"), (3, "01.000.000"), (4, "01.000.000"),
                 (5, "01.000.000"), (6, "01.000.000")], {2: "06.005.000"})
    s = run(root, "10200000")
    games = {g["titleId"]: g for g in s["games"]}
    check("one game with an update is counted", s["count"] == 1)
    check("PPSA00001 offers 06.005.000 over 05.000.000",
          games.get("PPSA00001", {}).get("version") == "06.005.000" and games["PPSA00001"]["installed"] == "05.000.000"
          and games["PPSA00001"]["state"] == "available")
    check("param.json wins over app.db (PPSA00002 is up to date)", "PPSA00002" not in games)
    check("a patch that needs firmware 11.00 is listed but not counted",
          games.get("PPSA00003", {}).get("state") == "needs-firmware" and games["PPSA00003"]["firmware"] == "11.00")
    check("a game Sony does not know (404) is skipped and counted as failed", "PPSA00004" not in games and s["failed"] >= 2)
    check("an installed version equal to Sony's is up to date", "PPSA00005" not in games)
    check("the PS4 row is ignored", all(not k.startswith("CUSA") for k in games))
    check("the check recorded its time and no error", s["checked"] > 0 and s["error"] == "")
    s = run(root, "11000000")
    check("on firmware 11.00 both patches are installable", s["count"] == 2)
with tempfile.TemporaryDirectory() as root:
    s = run(root, "10200000")
    check("a missing app.db gives a readable error", s["error"].startswith("Could not read the installed games"))
    check("and no update is shown", s["count"] == 0)

# ---- downloading a patch ----
def make_patch(id_, sizes):
    pieces, payload, offset = [], b"", 0
    for i, size in enumerate(sizes):
        data = bytes((i * 31 + j) % 251 for j in range(size))
        name = "%s-piece%d.pkg" % (id_, i)
        BIN[name] = data
        pieces.append({"url": "https://gs2.ww.prod.dl.playstation.net/x/" + name, "fileOffset": offset,
                       "fileSize": size, "hashValue": hashlib.sha256(data).hexdigest()})
        payload += data; offset += size
    BIN["UP4001-%s_00-X.json" % id_] = json.dumps({"originalFileSize": offset, "pieces": pieces}).encode()
    return payload

with tempfile.TemporaryDirectory() as root, tempfile.TemporaryDirectory() as drive:
    build(root, [(1, "05.000.000"), (2, "05.000.000")], {})
    payload = make_patch("PPSA00001", [300000, 123457, 5000])
    final = drive + "/homebrew/PPSA00001-v06.005.000.pkg"
    LOG.clear()
    s = run(root, "10200000", "PPSA00001", drive)
    check("a patch downloads, verifies and is saved as .pkg (installer absent: 'downloaded')",
          s.get("phase") == "downloaded" and os.path.exists(final) and open(final, "rb").read() == payload)
    check("progress reached the full size", s.get("done") == len(payload) == s.get("total") and s.get("pieces") == 3)
    check("no partial or state file is left behind", not [n for n in os.listdir(drive + "/homebrew") if n.startswith(".")])
    # corrupted once: retried, then fine
    os.remove(final)
    BEHAVE["PPSA00001-piece1.pkg"] = "bad-once"
    s = run(root, "10200000", "PPSA00001", drive)
    check("a piece that fails its checksum once is downloaded again", s.get("phase") == "downloaded" and open(final, "rb").read() == payload)
    # corrupted always: fails with a clear reason and leaves no .pkg
    os.remove(final)
    BEHAVE["PPSA00001-piece1.pkg"] = "bad-always"
    s = run(root, "10200000", "PPSA00001", drive)
    check("a piece that keeps failing its checksum fails the download", s.get("phase") == "failed" and "checksum" in s.get("error", "") and not os.path.exists(final))
    BEHAVE.clear()
    # connection dropped mid-piece: resumes with a Range request
    for n in os.listdir(drive + "/homebrew"): os.remove(drive + "/homebrew/" + n)
    BEHAVE["PPSA00001-piece0.pkg"] = "drop-once"
    LOG.clear()
    s = run(root, "10200000", "PPSA00001", drive)
    check("a dropped connection is retried and the file is intact", s.get("phase") == "downloaded" and open(final, "rb").read() == payload)
    # resume across runs: piece 2 gone on the first run, pieces 0 and 1 must not be fetched again
    os.remove(final)
    BEHAVE["PPSA00001-piece2.pkg"] = "gone"
    run(root, "10200000", "PPSA00001", drive)
    check("a failed run leaves a resumable part file", os.path.exists(drive + "/homebrew/.PPSA00001-v06.005.000.pkg.part"))
    BEHAVE.clear(); LOG.clear()
    s = run(root, "10200000", "PPSA00001", drive)
    check("the next run finishes it", s.get("phase") == "downloaded" and open(final, "rb").read() == payload)
    check("and does not download the finished pieces again", "PPSA00001-piece0.pkg" not in LOG and "PPSA00001-piece1.pkg" not in LOG)
    # a game with no offer is refused
    s = run(root, "10200000", "PPSA09999", drive)
    check("a game without an update is refused", s.get("startRefused") == 404)
    # the console without a drive
    s = run(root, "10200000", "PPSA00001", None)
    check("no drive with room fails cleanly", s.get("phase") == "failed" and "space" in s.get("error", ""))
sys.exit(1 if failures else 0)
