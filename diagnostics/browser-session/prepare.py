"""Prepare an isolated native test app; never edits the regular app or console."""
import argparse
import ipaddress
import json
import shutil
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--host", required=True)
p.add_argument("--run", required=True)
p.add_argument("--port", type=int, default=34578)
a = p.parse_args()
ipaddress.IPv4Address(a.host)
if not a.run.isalnum() or not 8 <= len(a.run) <= 64 or not 1024 <= a.port <= 65535:
    p.error("Invalid test ID or port")
root = Path(__file__).resolve().parents[2]
out = root / ".state/viking-console-session/app"
if out.exists():
    p.error(f"Build workspace already exists: {out}")
shutil.copytree(root / "app", out, ignore=shutil.ignore_patterns(".deps", "build", "dist", "node_modules"))
(out / ".deps").symlink_to("../../../app/.deps", target_is_directory=True)
(out.parent / "i18n").symlink_to("../../i18n", target_is_directory=True)
shutil.copyfile(Path(__file__).with_name("native_main.cpp"), out / "src/main.cpp")
origin = f"http://{a.host}:{a.port}"
(out / "src/session_probe_config.hpp").write_text(
    f'#define PROBE_HOST "{a.host}"\n#define PROBE_PORT {a.port}\n'
    f'#define PROBE_RUN "{a.run}"\n#define PROBE_ORIGIN "{origin}"\n')
dialog = out / "src/platform/ps5/provider_dialog.cpp"
text = dialog.read_text()
needle = 'url.rfind("https://", 0) != 0'
assert text.count(needle) == 1
text = text.replace(needle, f'url.rfind("{origin}/{a.run}/", 0) != 0')
text = text.replace('/download0/orbit-store', '/download0/orbit-browser-session')
dialog.write_text(text)
param = out / "sce_sys/param.json"
data = json.loads(param.read_text())
data.update(titleId="PPSA99179", conceptId="99179", contentId="UP9000-PPSA99179_00-ORBITBROWSERTEST", contentVersion="00.001.000")
data["localizedParameters"]["en-US"]["titleName"] = "Orbit Browser Session Test"
param.write_text(json.dumps(data, indent=2) + "\n")
print(out)
