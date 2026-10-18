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
class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        body = SERVED.get(self.path.lstrip("/"))
        self.send_response(200 if body else 404); self.end_headers()
        if body: self.wfile.write(body.encode())
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

def run(root, firmware):
    env = dict(os.environ, ORBIT_TEST_GAMEUPDATES_BASE="http://127.0.0.1:%d" % server.server_port)
    out = subprocess.run([driver, root, firmware], env=env, capture_output=True, text=True, timeout=60)
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
sys.exit(1 if failures else 0)
