#!/usr/bin/env python3
"""Checks the game-update worker end to end with a fake app.db, param.json files and a fake Sony server."""
import time, http.server, json, os, sqlite3, subprocess, sys, tempfile, threading

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
    "PPSA00007-version.xml": TEMPLATE % dict(id="PPSA00007", ver="02.000.000", fw=0x10200006),
    "PPSA00008-version.xml": TEMPLATE % dict(id="PPSA00008", ver="02.000.000", fw=0x10200006),
}
import hashlib, struct, re
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
        if mode in ("bad-once", "drop-once", "hang-once"):
            mode = BEHAVE.pop(name, None)   # the slices arrive together: only the first request gets the fault
        if data is None or mode == "gone":
            self.send_response(404); self.end_headers(); return
        start, end, code = 0, len(data), 200
        m = re.match(r"bytes=(\d+)-(\d+)", self.headers.get("Range", ""))
        if m:
            start, end, code = int(m.group(1)), int(m.group(2)) + 1, 206
        chunk = data[start:end]
        if mode in ("bad-once", "bad-always"):
            chunk = bytes((b ^ 0xFF) for b in chunk)
        self.send_response(code)
        self.send_header("Content-Length", str(len(chunk)))
        if code == 206: self.send_header("Content-Range", "bytes %d-%d/%d" % (start, end - 1, len(data)))
        self.end_headers()
        if mode == "hang-once":
            self.wfile.write(chunk[: len(chunk) // 2]); self.wfile.flush()
            time.sleep(4)
            try: self.wfile.write(chunk[len(chunk) // 2:])
            except OSError: pass
            return
        if mode == "drop-once":
            self.wfile.write(chunk[: len(chunk) // 2]); self.wfile.flush()
            self.connection.close(); return
        self.wfile.write(chunk)
    def log_message(self, *a): pass
server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
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

def run(root, firmware, download=None, storage=None, patches=None, nas=None, pause=None, queue=None, second=None, usbs=None):
    env = dict(os.environ, ORBIT_TEST_GAMEUPDATES_BASE="http://127.0.0.1:%d" % server.server_port)
    if pause: env["ORBIT_TEST_PAUSE"] = pause
    if queue: env["ORBIT_TEST_QUEUE"] = queue
    if second: env["ORBIT_TEST_QUEUE_SECOND"] = second
    if storage: env["ORBIT_TEST_STORAGE"] = storage
    if usbs: env["ORBIT_TEST_USBS"] = usbs
    if patches: env["ORBIT_TEST_GAME_PATCHES"] = patches
    if nas: env["ORBIT_TEST_NAS_UPDATE"] = nas
    args = [driver, root, firmware] + ([download] if download else [])
    out = subprocess.run(args, env=env, capture_output=True, text=True, timeout=120)
    assert out.returncode == 0, (out.returncode, out.stderr)
    decoder, text, at, last = json.JSONDecoder(), out.stdout, 0, None
    while at < len(text):                       # the driver may print several objects; the last one counts
        while at < len(text) and text[at].isspace(): at += 1
        if at >= len(text): break
        last, at = decoder.raw_decode(text, at)
    return last

failures = 0
def check(name, ok):
    global failures
    print(("ok   " if ok else "FAIL ") + name)
    failures += not ok

with tempfile.TemporaryDirectory() as root:
    build(root, [(1, "05.000.000"), (2, "05.000.000"), (3, "01.000.000"), (4, "01.000.000"),
                 (5, "01.000.000"), (6, "01.000.000")], {2: "06.005.000"})
    BIN["UP4001-PPSA00001_00-X.json"] = json.dumps({"originalFileSize": 4096, "pieces": [
        {"url": "https://gs2.ww.prod.dl.playstation.net/x/a.pkg", "fileOffset": 0, "fileSize": 4096,
         "hashValue": "0" * 64}]}).encode()
    s = run(root, "10200000")
    games = {g["titleId"]: g for g in s["games"]}
    check("one game with an update is counted", s["count"] == 1)
    check("PPSA00001 offers 06.005.000 over 05.000.000",
          games.get("PPSA00001", {}).get("version") == "06.005.000" and games["PPSA00001"]["installed"] == "05.000.000"
          and games["PPSA00001"]["state"] == "available")
    check("the patch size comes from Sony's list", games["PPSA00001"].get("size") == 4096 and "size" not in games.get("PPSA00003", {}))
    check("param.json wins over app.db (PPSA00002 is up to date)", "PPSA00002" not in games)
    check("a patch that needs firmware 11.00 is listed but not counted",
          games.get("PPSA00003", {}).get("state") == "needs-firmware" and games["PPSA00003"]["firmware"] == "11.00")
    check("a game Sony does not know (404) is skipped and counted as failed", "PPSA00004" not in games and s["failed"] >= 2)
    check("an installed version equal to Sony's is up to date", "PPSA00005" not in games)
    check("the PS4 row is ignored", all(not k.startswith("CUSA") for k in games))
    check("the check recorded its time and no error", s["checked"] > 0 and s["error"] == "")
    # ---- versions saved on the NAS page ----
    for g, v in (("PPSA00003", "02.000.000"), ("PPSA00005", "02.000.000")):
        BIN["UP4001-%s_00-N.json" % g] = json.dumps({"originalFileSize": 2048, "pieces": [
            {"url": "https://gs2.ww.prod.dl.playstation.net/x/n.pkg", "fileOffset": 0, "fileSize": 2048,
             "hashValue": "0" * 64}]}).encode()
    saved = root + "/game-patches.json"
    json.dump({
        "PPSA00003": {"version": "02.000.000", "link": "https://sgst.prod.dl.playstation.net/sgst/prod/00/PPSA00003_00/app/info/9/f_ab/UP4001-PPSA00003_00-N.crc"},
        "PPSA00005": {"version": "02.000.000", "link": "https://sgst.prod.dl.playstation.net/sgst/prod/00/PPSA00005_00/app/info/9/f_ab/UP4001-PPSA00005_00-N.crc"},
        "PPSA00001": {"version": "05.500.000", "link": "https://sgst.prod.dl.playstation.net/sgst/prod/00/PPSA00001_00/app/info/9/f_ab/UP4001-PPSA00001_00-N.crc"},
        "PPSA00006": {"version": "02.000.000", "link": "https://evil.example.com/x/UP4001-PPSA00006_00-N.crc"},
    }, open(saved, "w"))
    s = run(root, "10200000", patches=saved)
    games = {g["titleId"]: g for g in s["games"]}
    check("a NAS-saved version replaces a patch the firmware blocks",
          games.get("PPSA00003", {}).get("state") == "available" and games["PPSA00003"]["version"] == "02.000.000"
          and games["PPSA00003"].get("size") == 2048)
    check("a NAS-saved version is offered for a game Sony reports up to date",
          games.get("PPSA00005", {}).get("version") == "02.000.000" and games["PPSA00005"]["installed"] == "01.000.000")
    check("a NAS-saved version older than Sony's offer is ignored", games["PPSA00001"]["version"] == "06.005.000")
    check("a NAS-saved link outside Sony's servers is ignored", "PPSA00006" not in games)
    check("the count includes the NAS-saved updates", s["count"] == 3)
    s = run(root, "11000000")
    check("on firmware 11.00 both patches are installable", s["count"] == 2)
    # ---- the same version on the NAS and from Sony ----
    s = run(root, "10200000", nas="PPSA00001:06.005.000:777:" + "a" * 64)
    check("the same version on the NAS (with a SHA-256) is taken from the NAS instead of Sony",
          {g["titleId"]: g for g in s["games"]}["PPSA00001"].get("size") == 777)
    s = run(root, "10200000", nas="PPSA00001:06.005.000:777")
    check("the same version on the NAS without a SHA-256 stays on Sony",
          {g["titleId"]: g for g in s["games"]}["PPSA00001"].get("size") == 4096)
with tempfile.TemporaryDirectory() as root:
    s = run(root, "10200000")
    check("a missing app.db gives a readable error", s["error"].startswith("Could not read the installed games"))
    check("and no update is shown", s["count"] == 0)

# ---- an update file listed on the person's NAS ----
with tempfile.TemporaryDirectory() as root, tempfile.TemporaryDirectory() as drive:
    build(root, [(5, "01.000.000"), (1, "05.000.000")], {})
    nas_file = bytes((j * 7) % 253 for j in range(70000))
    BIN["nas-update.pkg"] = nas_file
    spec = "PPSA00005:03.000.000:%d:%s" % (len(nas_file), hashlib.sha256(nas_file).hexdigest())
    s = run(root, "10200000", nas=spec)
    games = {g["titleId"]: g for g in s["games"]}
    check("an update .pkg on the NAS is listed as an update (newer than installed)",
          games.get("PPSA00005", {}).get("version") == "03.000.000" and games["PPSA00005"]["state"] == "available"
          and games["PPSA00005"].get("size") == len(nas_file))
    check("a NAS update that is not newer than the installed version is not listed",
          "PPSA00005" not in {g["titleId"]: g for g in run(root, "10200000", nas="PPSA00005:01.000.000:10:" + "0" * 64)["games"]})
    s = run(root, "10200000", "PPSA00005", drive, nas=spec)
    final = drive + "/homebrew/PPSA00005-v03.000.000.pkg"
    check("the NAS file downloads, is checked and saved like a Sony patch",
          s.get("phase") == "downloaded" and os.path.exists(final) and open(final, "rb").read() == nas_file)
    os.remove(final)
    bad = "PPSA00005:03.000.000:%d:%s" % (len(nas_file), "f" * 64)
    s = run(root, "10200000", "PPSA00005", drive, nas=bad)
    check("a NAS file with the wrong SHA-256 fails and leaves no .pkg", s.get("phase") == "failed" and not os.path.exists(final))

# ---- a PS4 game updated from an update .pkg on the NAS ----
with tempfile.TemporaryDirectory() as root, tempfile.TemporaryDirectory() as drive:
    build(root, [(5, "01.000.000")], {})
    nas_file = bytes((j * 5) % 251 for j in range(50000))
    BIN["nas-update.pkg"] = nas_file
    spec = "CUSA16194:01.50:%d:%s" % (len(nas_file), hashlib.sha256(nas_file).hexdigest())
    s = run(root, "10200000", nas=spec)
    games = {g["titleId"]: g for g in s["games"]}
    check("a PS4 game gets the newer update .pkg found on the NAS",
          games.get("CUSA16194", {}).get("version") == "01.50" and games["CUSA16194"]["installed"] == "01.05"
          and games["CUSA16194"]["state"] == "available" and games["CUSA16194"].get("size") == len(nas_file))
    check("a PS4 NAS update that is not newer is not listed",
          "CUSA16194" not in {g["titleId"]: g for g in run(root, "10200000", nas="CUSA16194:01.05:10:" + "0" * 64)["games"]})
    check("a PS4 game with no NAS update is still left alone", "CUSA16194" not in {g["titleId"]: g for g in run(root, "10200000")["games"]})
    s = run(root, "10200000", "CUSA16194", drive, nas=spec)
    final = drive + "/homebrew/CUSA16194-v01.50.pkg"
    check("the PS4 update downloads from the NAS and is checked",
          s.get("phase") == "downloaded" and os.path.exists(final) and open(final, "rb").read() == nas_file)

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
    # a finished update leaves the downloads list when the person removes it
    s = run(root, "10200000", "PPSA00001", drive, pause="dismiss")
    check("removing a finished update clears it from the list", not s.get("phase"))
    check("and keeps the file", os.path.exists(final))
    # a game with no offer is refused
    s = run(root, "10200000", "PPSA09999", drive)
    check("a game without an update is refused", s.get("startRefused") == 404)
    # the console without a drive
    s = run(root, "10200000", "PPSA00001", None)
    check("no USB drive asks to connect one", s.get("phase") == "failed" and "USB drive" in s.get("error", ""))
    # a full usb0 falls through to usb1; two full drives ask for space
    with tempfile.TemporaryDirectory() as u0, tempfile.TemporaryDirectory() as u1:
        s = run(root, "10200000", "PPSA00001", None, usbs="%s:1000,%s:%d" % (u0, u1, 1 << 40))
        check("a full usb0 falls through to usb1", s.get("phase") == "downloaded" and os.path.exists(u1 + "/homebrew/PPSA00001-v06.005.000.pkg") and not os.path.exists(u0 + "/homebrew"))
        os.remove(u1 + "/homebrew/PPSA00001-v06.005.000.pkg")
        s = run(root, "10200000", "PPSA00001", None, usbs="%s:1000,%s:2000" % (u0, u1))
        check("two full USB drives ask to free some space", s.get("phase") == "failed" and "Free some space" in s.get("error", ""))
        s = run(root, "10200000", "PPSA00001", None, usbs="%s:%d,%s:%d" % (u0, 1 << 40, u1, 1 << 40))
        check("usb0 is used first when it has room", s.get("phase") == "downloaded" and os.path.exists(u0 + "/homebrew/PPSA00001-v06.005.000.pkg") and not os.path.exists(u1 + "/homebrew/PPSA00001-v06.005.000.pkg"))

# ---- pieces larger than one slice are fetched over several connections ----
with tempfile.TemporaryDirectory() as root, tempfile.TemporaryDirectory() as drive:
    build(root, [(1, "05.000.000")], {})
    payload = make_patch("PPSA00001", [10_000_000, 9_000_001, 3000])
    final = drive + "/homebrew/PPSA00001-v06.005.000.pkg"
    LOG.clear()
    s = run(root, "10200000", "PPSA00001", drive)
    check("big pieces are split over several requests and the file is intact",
          s.get("phase") == "downloaded" and open(final, "rb").read() == payload
          and LOG.count("PPSA00001-piece0.pkg") >= 3 and LOG.count("PPSA00001-piece1.pkg") >= 3)
    os.remove(final)
    BEHAVE["PPSA00001-piece0.pkg"] = "drop-once"
    s = run(root, "10200000", "PPSA00001", drive)
    check("a connection dropped in one slice is retried and the file is intact",
          s.get("phase") == "downloaded" and open(final, "rb").read() == payload)
    os.remove(final)
    BEHAVE["PPSA00001-piece1.pkg"] = "bad-once"
    s = run(root, "10200000", "PPSA00001", drive)
    check("a big piece that fails its checksum is downloaded again while the next one goes on",
          s.get("phase") == "downloaded" and open(final, "rb").read() == payload)
    BEHAVE.clear()

# ---- pause, resume and cancel ----
with tempfile.TemporaryDirectory() as root, tempfile.TemporaryDirectory() as drive:
    build(root, [(1, "05.000.000")], {})
    payload = make_patch("PPSA00001", [30000, 40000])
    final = drive + "/homebrew/PPSA00001-v06.005.000.pkg"
    part = drive + "/homebrew/.PPSA00001-v06.005.000.pkg.part"
    BEHAVE["PPSA00001-piece1.pkg"] = "hang-once"
    s = run(root, "10200000", "PPSA00001", drive, pause="pause")
    check("pausing stops the download and shows the paused phase", s.get("phase") == "paused" and s.get("active") is False)
    check("a paused download keeps its part file", os.path.exists(part))
    BEHAVE.clear()
    BEHAVE["PPSA00001-piece1.pkg"] = "hang-once"; LOG.clear()
    s = run(root, "10200000", "PPSA00001", drive, pause="resume")
    check("resuming finishes the update with the right file", s.get("phase") == "downloaded" and os.path.exists(final) and open(final, "rb").read() == payload)
    os.remove(final)
    BEHAVE["PPSA00001-piece1.pkg"] = "hang-once"
    s = run(root, "10200000", "PPSA00001", drive, pause="cancel")
    check("cancelling a paused update ends cancelled", s.get("phase") == "cancelled")
    check("and deletes what was downloaded", not [n for n in os.listdir(drive + "/homebrew") if n.startswith(".") or n.endswith(".pkg")])
    BEHAVE["PPSA00001-piece1.pkg"] = "hang-once"
    s = run(root, "10200000", "PPSA00001", drive, pause="cancel-active")
    check("cancelling a running update deletes its partial download", s.get("phase") == "cancelled" and not [n for n in os.listdir(drive + "/homebrew") if n.startswith(".") or n.endswith(".pkg")])

# ---- DLC listed on the NAS: found by the label in the package, hidden once the console has it ----
PNG = bytes.fromhex("89504e470d0a1a0a") + bytes(range(200))
def sfo(title):
    key, val = b"TITLE\0", title.encode() + b"\0"
    head = b"\0PSF" + struct.pack("<IIII", 0x101, 20 + 16, 20 + 16 + len(key), 1)
    entry = struct.pack("<HHIII", 0, 0x0204, len(val), len(val), 0)
    return head + entry + key + val

def fake_pkg(kind, content_id, size, title="", icon=None):
    body = bytearray(size)
    base = 0 if kind == "ps4" else 0x800
    if kind == "ps4":
        body[0:4] = bytes.fromhex("7f434e54")
    elif kind == "ps5":
        body[0:4] = bytes.fromhex("7f464948")
        body[0x58:0x60] = base.to_bytes(8, "little")
        body[base:base + 4] = bytes.fromhex("7f434e54")
    if kind in ("ps4", "ps5"):
        body[base + 0x40:base + 0x64] = content_id.encode()
        parts, at = [], 0x2000
        if title:
            data = sfo(title) if kind == "ps4" else json.dumps({"localizedParameters": {"defaultLanguage": "en-US", "en-US": {"titleName": title}}}).encode()
            parts.append((0x1000 if kind == "ps4" else 0x2000, at, data)); at += len(data) + 16
        if icon:
            parts.append((0x1200, at, icon)); at += len(icon) + 16
        table = 0x400
        body[base + 0x10:base + 0x14] = struct.pack(">I", len(parts))
        body[base + 0x18:base + 0x1c] = struct.pack(">I", table)
        for i, (ident, off, data) in enumerate(parts):
            body[base + table + i * 32:base + table + i * 32 + 32] = struct.pack(">IIIIII", ident, 0, 0, 0, off, len(data)) + bytes(8)
            body[base + off:base + off + len(data)] = data
    for j in range(base + 0x2000 + 0x1000, size): body[j] = (j * 7) % 251
    return bytes(body)

with tempfile.TemporaryDirectory() as root, tempfile.TemporaryDirectory() as drive:
    build(root, [(1, "06.005.000")], {})
    ps5 = fake_pkg("ps5", "EP0001-PPSA00001_00-LIMITEDPACK00000", 12000, "Claws of Awaji", PNG)
    ps4 = fake_pkg("ps4", "EP0002-CUSA16194_00-PS4DLCPACK000001", 12000, "PS4 Season Pass", PNG)
    named = fake_pkg("none", "", 5000)
    BIN["dlc-ps5.pkg"], BIN["dlc-ps4.pkg"] = ps5, ps4
    BIN["EP0003-PPSA00001_00-NAMEDONLY0000001.pkg"] = named
    BIN["unreadable.pkg"] = named
    spec = ";".join("%s|%s|%d|%s" % (g, f, len(b), hashlib.sha256(b).hexdigest()) for g, f, b in [
        ("PPSA00001", "dlc-ps5.pkg", ps5), ("CUSA16194", "dlc-ps4.pkg", ps4),
        ("PPSA00001", "EP0003-PPSA00001_00-NAMEDONLY0000001.pkg", named), ("PPSA00001", "unreadable.pkg", named)])
    def run_dlc(**kw):
        old = os.environ.get("ORBIT_TEST_NAS_DLC"); os.environ["ORBIT_TEST_NAS_DLC"] = spec
        try: return run(root, "10200000", **kw)
        finally:
            if old is None: os.environ.pop("ORBIT_TEST_NAS_DLC", None)
            else: os.environ["ORBIT_TEST_NAS_DLC"] = old
    s = run_dlc()
    keys = {d["titleId"]: d for d in s.get("dlc", [])}
    check("a PS5 DLC is found by the label in its package header", "PPSA00001/LIMITEDPACK00000" in keys)
    check("a PS4 DLC is found by the label in its package header", "CUSA16194/PS4DLCPACK000001" in keys)
    check("the DLC is named from its package and its icon is kept",
          keys["PPSA00001/LIMITEDPACK00000"].get("name") == "Claws of Awaji" and keys["PPSA00001/LIMITEDPACK00000"].get("hasIcon") is True
          and keys["CUSA16194/PS4DLCPACK000001"].get("name") == "PS4 Season Pass" and keys["CUSA16194/PS4DLCPACK000001"].get("hasIcon") is True)
    check("a DLC whose header says nothing is found by its file name", "PPSA00001/NAMEDONLY0000001" in keys)
    check("a DLC that cannot be named is reported, not listed", "unreadable.pkg" in s.get("dlcSkipped", "") and len(keys) == 3)
    check("the DLC belong to their game and are not updates",
          keys["PPSA00001/LIMITEDPACK00000"].get("baseId") == "PPSA00001" and s.get("dlcCount") == 3
          and all("/" not in g["titleId"] for g in s["games"]))
    os.makedirs(root + "/user/addcont/PPSA00001/LIMITEDPACK00000")
    s = run_dlc()
    keys = {d["titleId"] for d in s.get("dlc", [])}
    check("a DLC the console already has is no longer listed",
          "PPSA00001/LIMITEDPACK00000" not in keys and "CUSA16194/PS4DLCPACK000001" in keys and s.get("dlcCount") == 2)
    final = drive + "/homebrew/PPSA00001-dlc-NAMEDONLY0000001.pkg"
    s = run_dlc(download="PPSA00001/NAMEDONLY0000001", storage=drive)
    check("a DLC downloads, is checked and saved under its own name",
          s.get("phase") == "downloaded" and os.path.exists(final) and open(final, "rb").read() == named)
    s = run_dlc(download="PPSA00001/NOSUCHLABEL000000", storage=drive)
    check("a DLC that is not offered is refused", s.get("startRefused") == 404)
    for n in os.listdir(drive + "/homebrew"): os.remove(drive + "/homebrew/" + n)
    s = run_dlc(download="PPSA00001/NAMEDONLY0000001", storage=drive, queue="run", second="CUSA16194/PS4DLCPACK000001")
    check("a queued DLC starts by itself after the first one",
          s.get("job", {}).get("titleId") == "CUSA16194/PS4DLCPACK000001"
          and os.path.exists(drive + "/homebrew/CUSA16194-dlc-PS4DLCPACK000001.pkg"))

# ---- a DLC downloads while an update is stalled: they have a slot each ----
with tempfile.TemporaryDirectory() as root, tempfile.TemporaryDirectory() as drive:
    build(root, [(1, "05.000.000")], {})
    make_patch("PPSA00001", [30000, 40000])
    pkg = fake_pkg("ps4", "EP0002-CUSA16194_00-PS4DLCPACK000001", 12000, "PS4 Season Pass", PNG)
    BIN["dlc-ps4.pkg"] = pkg
    os.environ["ORBIT_TEST_NAS_DLC"] = "CUSA16194|dlc-ps4.pkg|%d|%s" % (len(pkg), hashlib.sha256(pkg).hexdigest())
    BEHAVE["PPSA00001-piece1.pkg"] = "hang-once"
    s = run(root, "10200000", "PPSA00001", drive, queue="parallel", second="CUSA16194/PS4DLCPACK000001")
    os.environ.pop("ORBIT_TEST_NAS_DLC", None)
    BEHAVE.clear()
    check("a DLC added during an update starts at once, not in the queue", s.get("update") == 202 and s.get("dlc") == 202)
    check("the DLC finishes while the update is still downloading",
          s.get("dlcJob", {}).get("phase") == "downloaded" and s.get("job", {}).get("active") is True
          and os.path.exists(drive + "/homebrew/CUSA16194-dlc-PS4DLCPACK000001.pkg"))

# ---- pause and resume with a piece split into slices ----
with tempfile.TemporaryDirectory() as root, tempfile.TemporaryDirectory() as drive:
    build(root, [(1, "05.000.000")], {})
    payload = make_patch("PPSA00001", [12_000_000, 20000])
    final = drive + "/homebrew/PPSA00001-v06.005.000.pkg"
    BEHAVE["PPSA00001-piece0.pkg"] = "hang-once"
    s = run(root, "10200000", "PPSA00001", drive, pause="pause")
    check("pausing a sliced piece keeps it paused", s.get("phase") == "paused")
    BEHAVE.clear()
    s = run(root, "10200000", "PPSA00001", drive, pause="resume")
    check("resuming a sliced piece gives the exact file", s.get("phase") == "downloaded" and open(final, "rb").read() == payload)

# ---- games app.db cannot describe: no update address (disc install) or not written yet ----
with tempfile.TemporaryDirectory() as root:
    build(root, [(1, "05.000.000")], {})
    con = sqlite3.connect(root + "/system_data/priv/mms/app.db")
    con.execute("INSERT INTO tbl_contentinfo VALUES ('PPSA00007','Disc game','{\"CONTENT_VERSION\":\"01.000.000\"}')")
    con.commit(); con.close()
    def param(i, extra):
        d = root + "/user/appmeta/PPSA%05d" % i
        os.makedirs(d, exist_ok=True)
        json.dump(dict({"contentVersion": "01.000.000", "versionFileUri":
                        "https://sgst.prod.dl.playstation.net/sgst/prod/00/np/PPSA%05d_00/PPSA%05d-version.xml" % (i, i) + " " * 40}, **extra),
                  open(d + "/param.json", "w"))
    param(7, {})
    param(8, {"titleId": "PPSA00008", "localizedParameters": {"defaultLanguage": "en-US", "en-US": {"titleName": "Fresh install"}}})
    s = run(root, "10200000")
    games = {g["titleId"]: g for g in s["games"]}
    check("a game with no update address in app.db uses param.json's", games.get("PPSA00007", {}).get("version") == "02.000.000")
    check("a game missing from app.db is found in appmeta", games.get("PPSA00008", {}).get("version") == "02.000.000"
          and games["PPSA00008"]["name"] == "Fresh install")
    check("nothing is reported as lacking an update address", s.get("noUpdateAddress") == "")
    os.remove(root + "/user/appmeta/PPSA00007/param.json")
    s = run(root, "10200000")
    check("a game with no update address anywhere is named in the status", "PPSA00007" in s.get("noUpdateAddress", ""))

# ---- the queue ----
with tempfile.TemporaryDirectory() as root, tempfile.TemporaryDirectory() as drive:
    build(root, [(1, "05.000.000"), (2, "05.000.000")], {})
    make_patch("PPSA00001", [20000])
    make_patch("PPSA00002", [20000])
    f1 = drive + "/homebrew/PPSA00001-v06.005.000.pkg"
    f2 = drive + "/homebrew/PPSA00002-v06.005.000.pkg"
    BEHAVE["PPSA00001-piece0.pkg"] = "hang-once"  # keeps the first one busy while the second is queued
    s = run(root, "10200000", "PPSA00001", drive, queue="run", second="PPSA00002")
    check("the first update starts and the second is accepted into the queue", s["first"] == 202 and s["second"] == 202)
    check("adding the same update again changes nothing", s["again"] == 200)
    check("the queued update starts by itself when the first ends", os.path.exists(f1) and os.path.exists(f2))
    check("and the queue is empty afterwards", s["queue"] == [] and s["job"]["titleId"] == "PPSA00002" and s["job"]["phase"] == "downloaded")
    for n in os.listdir(drive + "/homebrew"): os.remove(drive + "/homebrew/" + n)
    BEHAVE["PPSA00001-piece0.pkg"] = "hang-once"; LOG.clear()
    s = run(root, "10200000", "PPSA00001", drive, queue="dequeue", second="PPSA00002")
    check("a queued update that is removed never downloads", os.path.exists(f1) and not os.path.exists(f2) and not [l for l in LOG if "PPSA00002-piece" in l])
    for n in os.listdir(drive + "/homebrew"): os.remove(drive + "/homebrew/" + n)
    BEHAVE["PPSA00001-piece0.pkg"] = "hang-once"; LOG.clear()
    s = run(root, "10200000", "PPSA00001", drive, queue="clear", second="PPSA00002")
    check("clearing the queue leaves the running update alone", os.path.exists(f1) and not os.path.exists(f2) and s["queue"] == [])
    for n in os.listdir(drive + "/homebrew"): os.remove(drive + "/homebrew/" + n)
    json.dump({"queue": ["PPSA09999", "PPSA00002"]}, open(root + "/game-update-queue.json", "w"))
    orbit_drive = drive
    s = run(root, "10200000", storage=drive, queue="load")
    check("a saved queue resumes after a restart and skips an update no longer offered", os.path.exists(f2))
sys.exit(1 if failures else 0)
