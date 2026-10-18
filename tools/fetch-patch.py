#!/usr/bin/env python3
"""Download a PS5 game patch from Sony's servers into the NAS game folder.

    python3 fetch-patch.py /volume1/web/orbit-3f9a7c1e PPSA01552 06.005.000 \\
        "https://sgst.prod.dl.playstation.net/sgst/prod/00/PPSA01552_00/app/info/76/f_.../UP4001-PPSA01552_00-XXXX.crc"

The link is the "CRC" link of the patch on prosperopatches.com. Replacing ".crc" by ".json" gives the
patch manifest: the list of the pieces of the .pkg (url, offset, size, SHA-256). The pieces are downloaded
one after the other, each is checked against its SHA-256, and together they make
    <folder>/packages/ps5/updates/<TITLEID>/<TITLEID>-v<version>.pkg
which the next scan of make-nas-index.py lists under "Updates".

* Only Sony's download hosts are contacted (the manifest cannot send the script anywhere else).
* A stopped or interrupted download resumes where it left off (state kept next to the file).
* --console-fw / --required-fw: refuses a patch that needs a newer firmware than your console has
  (--force ignores that). Only the first two numbers are compared: console "10.20" accepts "10.20.00.06".
"""
import argparse
import fcntl
import hashlib
import http.client
import json
import os
import re
import shutil
import signal
import ssl
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

SONY_HOSTS = {
    "sgst.prod.dl.playstation.net",
    "gst.prod.dl.playstation.net",
    "gs2.ww.prod.dl.playstation.net",
    "gs-sec.ww.np.dl.playstation.net",
}
TITLE_ID = re.compile(r"^PPSA\d{5}$")
VERSION = re.compile(r"^\d{2}\.\d{3}\.\d{3}$")
URL_PATH = re.compile(r"^/[A-Za-z0-9/._\-]+$")
HEX = re.compile(r"^(?:[0-9a-fA-F]{64}|[0-9a-fA-F]{40})$")
LINK_SUFFIXES = (".crc", ".json", "_sc.pkg", "-DP.pkg")
MAX_TOTAL = 300 * 1024 ** 3          # no PS5 patch is anywhere near this
MAX_PIECES = 512
MAX_MANIFEST = 8 << 20
BLOCK = 1 << 20
SPARE = 256 << 20                    # free space to leave on the volume


class Fail(Exception):
    """A problem to report as is (exit code 1)."""


def check_url(url: str) -> str:
    """The URL when it points to one of Sony's download hosts, else Fail."""
    try:
        parts = urllib.parse.urlsplit(url)
    except ValueError:
        raise Fail("invalid address")
    if parts.scheme not in ("http", "https") or parts.hostname not in SONY_HOSTS:
        raise Fail("address not on Sony's download servers: " + (parts.hostname or "?"))
    if parts.username or parts.password or parts.port not in (None, 80, 443) or parts.query or parts.fragment:
        raise Fail("unexpected parts in the address")
    if not URL_PATH.match(parts.path) or ".." in parts.path:
        raise Fail("unexpected characters in the address")
    return url


def manifest_url(link: str) -> str:
    """The patch's manifest (.json) from its CRC link (or the .json, _sc.pkg or -DP.pkg link)."""
    link = check_url(link.strip())
    for suffix in LINK_SUFFIXES:
        if link.endswith(suffix):
            return check_url(link[: -len(suffix)] + ".json")
    raise Fail("the link must end in .crc (or .json, _sc.pkg, -DP.pkg)")


class SonyOnly(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        check_url(newurl)
        return super().redirect_request(req, fp, code, msg, headers, newurl)


# Sony's download servers use a private root, "SCEI DNAS Root 05" (public certificate, valid until 2037),
# that no system or browser trusts by default. It is added to the normal list, for these servers only.
SCEI_ROOT_PEM = """
-----BEGIN CERTIFICATE-----
MIID0jCCArqgAwIBAgIBADANBgkqhkiG9w0BAQUFADBUMQswCQYDVQQGEwJKUDEp
MCcGA1UEChMgU29ueSBDb21wdXRlciBFbnRlcnRhaW5tZW50IEluYy4xGjAYBgNV
BAMTEVNDRUkgRE5BUyBSb290IDA1MB4XDTA0MDcxMjA5MDExOVoXDTM3MTIwNjA5
MDExOVowVDELMAkGA1UEBhMCSlAxKTAnBgNVBAoTIFNvbnkgQ29tcHV0ZXIgRW50
ZXJ0YWlubWVudCBJbmMuMRowGAYDVQQDExFTQ0VJIEROQVMgUm9vdCAwNTCCASIw
DQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBANmPeza8PwCqlI7esOGIkoSESnIN
g72ZD3Ut63jy7SdothPIvGBqVZWYkIpqJYJd1I4Nh//IpXQCQL0PnJLrh9BBeowq
Muf5NNq3Us80Ihiu9CvNEAEO18g3OFV1TYdSwQ5zUsk33OUeI7h4aBPDVcZXYeHt
dbPLqe4K8igian5prrAD5S6h28t8aAm+qMWRo+bW25B/841XwDGBP7/IxZv8Yoio
rCo80CVYe6lGoU08eeqQiaHI5zAF281DWZSoVfLjJUEWmEnxqr8aOhszRGePi+Ei
7UQjHDuZX9rLhDI1zAND+BA259tn/iwOqVXe20OccJllHJcG4Ecmd98f5qMCAwEA
AaOBrjCBqzAdBgNVHQ4EFgQUxlahM1tPzoN3YgVEhm0gV7Wv2twwfAYDVR0jBHUw
c4AUxlahM1tPzoN3YgVEhm0gV7Wv2tyhWKRWMFQxCzAJBgNVBAYTAkpQMSkwJwYD
VQQKEyBTb255IENvbXB1dGVyIEVudGVydGFpbm1lbnQgSW5jLjEaMBgGA1UEAxMR
U0NFSSBETkFTIFJvb3QgMDWCAQAwDAYDVR0TBAUwAwEB/zANBgkqhkiG9w0BAQUF
AAOCAQEACZPihjwXA27wJ03tEKcHAeFLi8aBw2ysH4GwuH1dWb3UpuznWOB0iQT1
wQocnEFYCJx5XFEnj4aLWpSHLEq/sSO+my+aPoTEsy20ajF+YLYZm0bZxH50CJYh
rkET4C2aC0XvhGp9k1JQ1o0W6+cFT5LTlXapsq8Btt31t+XDPX7RqGV4WGekt3hM
T7xRc7JWXdAQijIrbYi8mtbM07KEGnPU6IT8C47+0mSurpwLOoWL1tPgo6ePpLNi
c4quUMgh9RXVjeTyXOMmyYdeUm2gt7qErvQONli+6Epmhm0A2khpIMHSpQjTE8gV
rZp42a6+zg1iYy2vFBOmiQ17GRUl0A==
-----END CERTIFICATE-----
"""
SCEI_ROOT_SHA256 = "51d5a7833f67f1d1c6212f75997e4b83dada0f7910ba4b5a3fa4e9b7b9bc827c"


CA_FILES = ("/etc/ssl/certs/ca-certificates.crt", "/etc/pki/tls/certs/ca-bundle.crt", "/etc/ssl/cert.pem",
            "/usr/local/etc/openssl/cert.pem", "/usr/syno/etc/ssl/certs/ca-certificates.crt")


def tls_context() -> ssl.SSLContext:
    """Normal certificate checking. Some NAS Pythons ship without a CA list: then use the system one."""
    context = ssl.create_default_context()
    if not os.environ.get("SSL_CERT_FILE") and context.cert_store_stats().get("x509_ca", 0) == 0:
        for path in CA_FILES:
            if os.path.isfile(path):
                context.load_verify_locations(cafile=path)
                break
    context.load_verify_locations(cadata=SCEI_ROOT_PEM)
    context.verify_flags &= ~getattr(ssl, "VERIFY_X509_STRICT", 0)     # this 2004 root predates the strict rules
    return context


def why(problem: BaseException) -> str:
    """A readable reason (DNS, certificate, timeout...) instead of just the exception class."""
    reason = getattr(problem, "reason", problem)
    return f"{type(reason).__name__}: {str(reason)[:160]}"


OPENER = urllib.request.build_opener(SonyOnly, urllib.request.HTTPSHandler(context=tls_context()))


def request(url: str, start: int = 0, end: int = 0):
    headers = {"User-Agent": "Mozilla/5.0 (compatible; orbit-nas-patch/1.0)"}
    if end:
        headers["Range"] = f"bytes={start}-{end - 1}"
    return OPENER.open(urllib.request.Request(check_url(url), headers=headers), timeout=30)


def load_manifest(url: str) -> dict:
    try:
        with request(url) as reply:
            raw = reply.read(MAX_MANIFEST + 1)
    except urllib.error.HTTPError as problem:
        raise Fail(f"Sony answered HTTP {problem.code} for the manifest")
    except (urllib.error.URLError, OSError) as problem:
        raise Fail("cannot reach Sony: " + why(problem))
    if len(raw) > MAX_MANIFEST:
        raise Fail("manifest too large")
    try:
        data = json.loads(raw.decode("utf-8"))
        total = data["originalFileSize"]
        pieces = sorted(data["pieces"], key=lambda p: p["fileOffset"])
    except (ValueError, KeyError, TypeError):
        raise Fail("unreadable manifest")
    if not isinstance(total, int) or not 0 < total <= MAX_TOTAL or not 0 < len(pieces) <= MAX_PIECES:
        raise Fail("manifest out of bounds")
    position, out = 0, []
    for piece in pieces:
        try:
            offset, size, digest, link = piece["fileOffset"], piece["fileSize"], str(piece["hashValue"]), str(piece["url"])
        except (KeyError, TypeError):
            raise Fail("unreadable piece in the manifest")
        if not (isinstance(offset, int) and isinstance(size, int)) or offset != position or size <= 0 or not HEX.match(digest):
            raise Fail("pieces of the manifest do not follow each other")
        out.append({"url": check_url(link), "offset": offset, "size": size, "hash": digest.lower()})
        position += size
    if position != total:
        raise Fail("pieces do not add up to the package size")
    return {"total": total, "pieces": out}


def fw_key(value: str):
    """First two numbers of a firmware ("10.20.00.06" -> (10, 20)), None when unreadable."""
    found = re.match(r"^\s*(\d{1,2})\.(\d{1,2})", value or "")
    return (int(found.group(1)), int(found.group(2))) if found else None


def digest_of(fd: int, offset: int, size: int, algorithm: str, cancelled) -> str:
    h = hashlib.sha1() if algorithm == "sha1" else hashlib.sha256()
    done = 0
    while done < size:
        block = os.pread(fd, min(8 * BLOCK, size - done), offset + done)
        if not block:
            break
        h.update(block)
        done += len(block)
        cancelled()
    return h.hexdigest()


class Job:
    def __init__(self, out_dir: str, name: str, manifest: dict, murl: str, cancel_file: str):
        self.dir, self.name, self.m, self.murl = out_dir, name, manifest, murl
        self.part = os.path.join(out_dir, "." + name + ".part")
        self.state_path = os.path.join(out_dir, "." + name + ".state.json")
        self.cancel_file = cancel_file
        self.fd = -1
        self.written = [0] * len(manifest["pieces"])     # bytes of each piece already on disk
        self.done = [False] * len(manifest["pieces"])    # piece fully there and checked
        self.last_print = self.last_save = 0.0
        self.speed_mark = (time.time(), 0)
        self.speed = 0.0

    # ---- state -------------------------------------------------------------------
    def load_state(self) -> bool:
        try:
            with open(self.state_path, "r", encoding="utf-8") as handle:
                saved = json.load(handle)
            ok = (saved.get("manifest") == self.murl and saved.get("total") == self.m["total"]
                  and len(saved.get("pieces", [])) == len(self.written) and os.path.getsize(self.part) == self.m["total"])
        except (OSError, ValueError, AttributeError):
            return False
        if not ok:
            return False
        for i, item in enumerate(saved["pieces"]):
            size = self.m["pieces"][i]["size"]
            self.written[i] = max(0, min(int(item.get("written", 0)), size))
            self.done[i] = bool(item.get("done")) and self.written[i] == size
        return True

    def save_state(self) -> None:
        if self.fd < 0:
            return
        data = {"manifest": self.murl, "total": self.m["total"],
                "pieces": [{"written": w, "done": d} for w, d in zip(self.written, self.done)]}
        temporary = self.state_path + ".tmp"
        try:
            os.fsync(self.fd)
            with open(temporary, "w", encoding="utf-8") as handle:
                json.dump(data, handle)
            os.replace(temporary, self.state_path)
        except OSError:
            pass
        self.last_save = time.time()

    # ---- helpers -----------------------------------------------------------------
    def cancelled(self) -> None:
        if self.cancel_file and os.path.exists(self.cancel_file):
            raise Fail("cancelled")

    def have(self) -> int:
        return sum(self.m["pieces"][i]["size"] if self.done[i] else self.written[i] for i in range(len(self.written)))

    def progress(self, force: bool = False) -> None:
        now = time.time()
        if not force and now - self.last_print < 1.0:
            return
        mark_time, mark_bytes = self.speed_mark
        have = self.have()
        if now - mark_time >= 3.0:
            self.speed = max(0.0, (have - mark_bytes) / (now - mark_time))
            self.speed_mark = (now, have)
        current = next((i for i, d in enumerate(self.done) if not d), len(self.done) - 1) + 1
        print(f"progress: {have} {self.m['total']} {current} {len(self.done)} {int(self.speed)}", flush=True)
        self.last_print = now

    # ---- one piece ----------------------------------------------------------------
    def fetch_piece(self, i: int) -> None:
        piece = self.m["pieces"][i]
        algorithm = "sha1" if len(piece["hash"]) == 40 else "sha256"
        network_failures = hash_failures = 0
        while not self.done[i]:
            self.cancelled()
            start = self.written[i]
            try:
                if start < piece["size"]:
                    self.stream(i, start)
                digest = digest_of(self.fd, piece["offset"], piece["size"], algorithm, self.cancelled)
                if digest == piece["hash"]:
                    self.done[i] = True
                    self.save_state()
                    self.progress(True)
                    return
                hash_failures += 1
                print(f"piece {i + 1}: checksum mismatch, downloading it again", flush=True)
                if hash_failures >= 2:
                    raise Fail(f"piece {i + 1} keeps failing its SHA check")
                self.written[i] = 0
                self.save_state()
            except (urllib.error.URLError, http.client.HTTPException, OSError, ValueError) as problem:
                if isinstance(problem, urllib.error.HTTPError) and problem.code in (400, 401, 403, 404, 410):
                    raise Fail(f"Sony answered HTTP {problem.code} for piece {i + 1} (the link may have expired)")
                if isinstance(problem, OSError) and getattr(problem, "errno", None) == 28:
                    raise Fail("disk full")
                network_failures += 1
                self.save_state()
                if network_failures > 8:
                    raise Fail(f"piece {i + 1}: too many network errors ({why(problem)})")
                wait = min(2 ** network_failures, 30)
                print(f"piece {i + 1}: {why(problem)}, retrying in {wait} s", flush=True)
                for _ in range(wait):
                    self.cancelled()
                    time.sleep(1)

    def stream(self, i: int, start: int) -> None:
        piece = self.m["pieces"][i]
        end = piece["size"]
        with request(piece["url"], start, end) as reply:
            if start > 0 and reply.status != 206:
                self.written[i] = 0                      # the server ignores Range: begin the piece again
                raise ValueError("range ignored")
            position = start
            while position < end:
                block = reply.read(min(BLOCK, end - position))
                if not block:
                    break
                os.pwrite(self.fd, block, piece["offset"] + position)
                position += len(block)
                self.written[i] = position
                self.cancelled()
                self.progress()
                if time.time() - self.last_save > 15:
                    self.save_state()
        if position != end:
            raise ValueError("connection closed early")

    # ---- whole package -----------------------------------------------------------
    def run(self, final: str) -> None:
        resumed = os.path.exists(self.part) and self.load_state()
        if not resumed:
            self.written = [0] * len(self.written)
            self.done = [False] * len(self.done)
        # The file is reserved at its full size at the start, so only what is not reserved yet needs room.
        reserved = os.stat(self.part).st_blocks * 512 if os.path.exists(self.part) else 0
        extra = max(0, self.m["total"] - reserved)
        free = shutil.disk_usage(self.dir).free
        if free < extra + SPARE:
            raise Fail(f"not enough free space ({free // 2**30} GiB free, {extra // 2**30} GiB needed)")
        self.fd = os.open(self.part, os.O_RDWR | os.O_CREAT, 0o644)
        try:
            if os.fstat(self.fd).st_size != self.m["total"]:
                try:
                    os.posix_fallocate(self.fd, 0, self.m["total"])
                except (AttributeError, OSError):
                    os.ftruncate(self.fd, self.m["total"])
            print(("resuming" if resumed else "starting") + f": {len(self.done)} piece(s), {self.m['total']} bytes", flush=True)
            for i in range(len(self.done)):
                if self.done[i]:
                    continue
                self.fetch_piece(i)
            os.fsync(self.fd)
        finally:
            self.save_state()
            os.close(self.fd)
            self.fd = -1
        os.replace(self.part, final)
        try:
            os.unlink(self.state_path)
        except OSError:
            pass


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("folder", help="game folder (the one holding index.json)")
    parser.add_argument("title_id", help="PPSAxxxxx")
    parser.add_argument("version", help="patch version, e.g. 06.005.000")
    parser.add_argument("link", help="the patch's CRC link from prosperopatches.com (or its .json link)")
    parser.add_argument("--required-fw", default="", help="firmware the patch needs, e.g. 10.20.00.06")
    parser.add_argument("--console-fw", default="", help="firmware of your console, e.g. 10.20")
    parser.add_argument("--force", action="store_true", help="download even if the firmware is too old")
    parser.add_argument("--cancel-file", default="", help="stop (keeping what was downloaded) when this file exists")
    parser.add_argument("--subdir", default="packages/ps5/updates", help="where patches go, relative to the folder")
    args = parser.parse_args()

    signal.signal(signal.SIGTERM, lambda *_: sys.exit(143))
    job = None
    try:
        if not TITLE_ID.match(args.title_id) or not VERSION.match(args.version):
            raise Fail("title ID (PPSAxxxxx) or version (00.000.000) not valid")
        if not re.match(r"^[A-Za-z0-9_.\-]+(/[A-Za-z0-9_.\-]+)*$", args.subdir) or ".." in args.subdir.split("/"):
            raise Fail("--subdir not valid")
        root = os.path.abspath(args.folder)
        if not os.path.isdir(root):
            raise Fail(f"{root} is not a folder")
        need, have = fw_key(args.required_fw), fw_key(args.console_fw)
        if need and have and need > have and not args.force:
            raise Fail(f"this patch needs firmware {args.required_fw} and the console has {args.console_fw}")
        murl = manifest_url(args.link)
        if args.title_id not in murl:
            raise Fail("the link does not belong to " + args.title_id)
        manifest = load_manifest(murl)
        out_dir = os.path.join(root, *args.subdir.split("/"), args.title_id)
        os.makedirs(out_dir, exist_ok=True)
        name = f"{args.title_id}-v{args.version}.pkg"
        final = os.path.join(out_dir, name)
        if os.path.isfile(final) and os.path.getsize(final) == manifest["total"]:
            print(f"already there: {final}", flush=True)
            return 0
        lock = open(os.path.join(out_dir, "." + name + ".lock"), "w")
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError:
            raise Fail("this patch is already being downloaded")
        job = Job(out_dir, name, manifest, murl, args.cancel_file)
        job.run(final)
        job.progress(True)
        try:
            os.unlink(lock.name)
        except OSError:
            pass
        print(f"done: {final}", flush=True)
        return 0
    except Fail as problem:
        if job is not None:
            job.progress(True)
        if str(problem) == "cancelled":
            print("cancelled: what was downloaded is kept, start again to resume", flush=True)
            return 130
        print(f"error: {problem}", file=sys.stderr, flush=True)
        return 1


if __name__ == "__main__":
    sys.exit(main())
