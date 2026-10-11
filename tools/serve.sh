#!/bin/bash
# Local test loop: build both firmwares, run the tests, assemble the web flasher and serve it
# on this machine over https. Nothing goes to GitHub. Open https://<this machine>:8765 in
# Chrome/Edge. USB in the browser needs https; the certificate is self-signed, so the browser
# warns once: Advanced -> Proceed.
#
#   tools/serve.sh            build + serve on port 8765
#   PORT=8080 tools/serve.sh  another port
#   NOBUILD=1 tools/serve.sh  serve the last build
set -e
cd "$(dirname "$0")/.."
if [ -z "$NOBUILD" ]; then
  pio run
  node --test site/test/*.test.mjs
fi
python3 tools/make_site.py .pio/build _site
echo "Serving the flasher on https://$(hostname -I 2>/dev/null | awk '{print $1}'):${PORT:-8765}/"
exec python3 tools/serve_https.py _site "${PORT:-8765}" "${CERT_DIR:-.dev-cert}"
