"""Console-only dummy storage probe. No provider accounts, downloads or credentials.

The native tester opens seed, check and clear pages in separate dialog sessions.
The server records booleans only. An HTTP probe establishes basic lifetime, not
Secure-cookie behaviour or Vikingfile authentication compatibility.
"""
import argparse
import json
import secrets
import threading
from http.cookies import SimpleCookie
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlsplit


def make_server(host, port, peer, run, report_path):
    state = {"run": run, "peer": peer, "events": []}
    lock = threading.Lock()
    cookies = {"session": "orbit_probe_session", "persistent": "orbit_probe_persistent"}

    def record(event):
        with lock:
            if len(state["events"]) >= 200:
                return
            state["events"].append(event)
            report_path.write_text(json.dumps(state, indent=2) + "\n")

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_GET(self):
            if self.client_address[0] != peer:
                self.send_error(403)
                return
            url = urlsplit(self.path)
            parts = url.path.split("/")
            if len(parts) != 4 or parts[1] != run or parts[2] not in ("seed", "check", "clear", "report", "event") or parts[3] not in ("native", "shell"):
                self.send_error(404)
                return
            action, surface = parts[2:]
            if action == "event":
                q = parse_qs(url.query)
                phase, rc = q.get("phase", [""])[0], q.get("rc", [""])[0]
                if phase not in ("initialize", "open-seed", "open-check", "open-clear", "close", "finished") or not rc.lstrip("-").isdigit() or len(rc) > 12:
                    self.send_error(400)
                    return
                record({"surface": surface, "phase": phase, "rc": int(rc)})
                body = b"ok"
            elif action == "report":
                q = parse_qs(url.query)
                record({"surface": surface, "phase": "page-check", "localStorage": q.get("local", ["0"])[0] == "1",
                        "sessionStorage": q.get("session", ["0"])[0] == "1"})
                body = b"ok"
            else:
                received = SimpleCookie()
                try:
                    received.load(self.headers.get("Cookie", "")[:8192])
                except Exception:
                    pass
                record({"surface": surface, "phase": action,
                        **{key + "Cookie": bool(received.get(name) and received[name].value == run) for key, name in cookies.items()}})
                prefix = f"/{run}"
                jsrun = json.dumps(run)
                if action == "seed":
                    script = f"try{{localStorage.setItem('orbit-probe',{jsrun});sessionStorage.setItem('orbit-probe',{jsrun});}}catch(e){{}}"
                    text = "Dummy data saved. This window will close and reopen automatically in the native tester."
                elif action == "check":
                    script = f"var l=0,s=0;try{{l=localStorage.getItem('orbit-probe')==={jsrun};s=sessionStorage.getItem('orbit-probe')==={jsrun};}}catch(e){{}}new Image().src='{prefix}/report/{surface}?local='+Number(l)+'&session='+Number(s);"
                    text = "Checking storage after reopening. No account information is read."
                else:
                    script = "try{localStorage.removeItem('orbit-probe');sessionStorage.removeItem('orbit-probe');}catch(e){}"
                    text = "The dummy test data has been cleared. You can close this window."
                body = ("<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width'>"
                        "<title>Orbit browser session test</title><style>body{background:#12151d;color:#eee;font:28px sans-serif;padding:8vw;line-height:1.5}</style>"
                        f"<h1>Orbit browser session test</h1><p>{text}</p><script>{script}</script>").encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8" if action in ("seed", "check", "clear") else "text/plain")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(body)))
            if action in ("seed", "clear"):
                for key, name in cookies.items():
                    value = run if action == "seed" else ""
                    age = "; Max-Age=1800" if key == "persistent" else ""
                    if action == "clear":
                        age = "; Max-Age=0"
                    self.send_header("Set-Cookie", f"{name}={value}; Path=/{run}/; HttpOnly; SameSite=Lax{age}")
            self.end_headers()
            self.wfile.write(body)

    return ThreadingHTTPServer((host, port), Handler)


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--host", required=True)
    p.add_argument("--peer", required=True)
    p.add_argument("--port", type=int, default=34578)
    p.add_argument("--run", default=secrets.token_hex(12))
    p.add_argument("--report", type=Path, required=True)
    a = p.parse_args()
    if not a.run.isalnum() or not 8 <= len(a.run) <= 64:
        p.error("Use an 8–64 character alphanumeric test ID")
    a.report.parent.mkdir(parents=True, exist_ok=True)
    server = make_server(a.host, a.port, a.peer, a.run, a.report)
    print(f"Probe ready: http://{a.host}:{server.server_port}/{a.run}/seed/shell", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
