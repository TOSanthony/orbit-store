import http.client
import importlib.util
import json
from pathlib import Path
import tempfile
import threading
import unittest

spec = importlib.util.spec_from_file_location("probe_server", Path(__file__).with_name("server.py"))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class SessionProbeTests(unittest.TestCase):
    def test_dummy_cookie_lifecycle_and_private_report(self):
        with tempfile.TemporaryDirectory() as directory:
            report = Path(directory) / "report.json"
            server = module.make_server("127.0.0.1", 0, "127.0.0.1", "testsession123", report)
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()

            def get(path, headers=None):
                c = http.client.HTTPConnection("127.0.0.1", server.server_port)
                c.request("GET", path, headers=headers or {})
                r = c.getresponse()
                body = r.read()
                result = r.status, r.getheaders(), body
                c.close()
                return result

            try:
                self.assertEqual(get("/wrong/seed/native")[0], 404)
                status, headers, body = get("/testsession123/seed/native")
                self.assertEqual(status, 200)
                cookie_headers = [v for k, v in headers if k == "Set-Cookie"]
                self.assertEqual(len(cookie_headers), 2)
                self.assertTrue(all("HttpOnly" in v for v in cookie_headers))
                self.assertEqual(sum("Max-Age" in v for v in cookie_headers), 1)
                cookie = "; ".join(v.split(";", 1)[0] for v in cookie_headers)
                self.assertEqual(get("/testsession123/check/native", {"Cookie": cookie + "; unrelated=SECRET"})[0], 200)
                self.assertEqual(get("/testsession123/report/native?local=1&session=0")[0], 200)
                self.assertEqual(get("/testsession123/event/native?phase=open-check&rc=0")[0], 200)
                self.assertEqual(get("/testsession123/event/native?phase=arbitrary&rc=0")[0], 400)
                self.assertNotIn("SECRET", report.read_text())
                events = json.loads(report.read_text())["events"]
                self.assertTrue(events[1]["persistentCookie"])
                self.assertTrue(events[1]["sessionCookie"])
                self.assertFalse(events[2]["sessionStorage"])
                _, headers, _ = get("/testsession123/clear/native")
                self.assertEqual(sum(k == "Set-Cookie" and "Max-Age=0" in v for k, v in headers), 2)
            finally:
                server.shutdown()
                server.server_close()
                thread.join()

    def test_other_peer_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            report = Path(directory) / "report.json"
            server = module.make_server("127.0.0.1", 0, "192.0.2.1", "testsession123", report)
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            try:
                c = http.client.HTTPConnection("127.0.0.1", server.server_port)
                c.request("GET", "/testsession123/seed/native")
                self.assertEqual(c.getresponse().status, 403)
                c.close()
                self.assertFalse(report.exists())
            finally:
                server.shutdown()
                server.server_close()
                thread.join()


if __name__ == "__main__":
    unittest.main()
