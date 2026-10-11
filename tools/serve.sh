#!/bin/bash
# Local test loop: build both firmwares, run the tests, assemble the web flasher and serve it
# on this machine. Nothing goes to GitHub. Open http://<this machine>:8000 in Chrome/Edge.
#
# Web Serial only works on https or localhost. To use it from another computer, open
# chrome://flags/#unsafely-treat-insecure-origin-as-secure on that computer, add
# http://<this machine>:8000, and relaunch the browser (one-time).
#
#   tools/serve.sh            build + serve on port 8000
#   PORT=8080 tools/serve.sh  another port
#   NOBUILD=1 tools/serve.sh  serve the last build
set -e
cd "$(dirname "$0")/.."
if [ -z "$NOBUILD" ]; then
  pio run
  node --test site/test/*.test.mjs
fi
python3 tools/make_site.py .pio/build _site
echo "Serving the flasher on http://$(hostname -I 2>/dev/null | awk '{print $1}'):${PORT:-8000}/"
exec python3 -m http.server --bind 0.0.0.0 "${PORT:-8000}" -d _site
