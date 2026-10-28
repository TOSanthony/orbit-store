#!/usr/bin/env bash
# Orbit Store TV app - Regenerate src/orbit/icons.{hpp,cpp} from icons.json.
#
# icons.json holds the interface designs' line icons as SVG markup on a
# 24 px grid. A headless Chrome samples every outline (flatten.html), and
# icons.py writes the polylines as C++ tables. Usage: tools/icons/generate.sh
# (needs Google Chrome or Chromium and Python 3).

set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
chrome=${CHROME:-$(command -v chromium || command -v google-chrome ||
    echo "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome")}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
printf 'const ICONS=%s;' "$(cat "$here/icons.json")" > "$work/icons.js"
cp "$here/flatten.html" "$work/"
"$chrome" --headless=new --disable-gpu --allow-file-access-from-files --virtual-time-budget=5000 \
    --dump-dom "file://$work/flatten.html" 2>/dev/null > "$work/dom.html"
python3 "$here/icons.py" "$work/dom.html" "$here/../../src/orbit"
