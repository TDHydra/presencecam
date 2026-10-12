#!/bin/bash
# Local test loop: build every firmware, run the tests, assemble the web flasher and serve it
# on this machine over https. Nothing goes to GitHub. Open https://<this machine>:8765 in
# Chrome/Edge. USB in the browser needs https; the certificate is self-signed, so the browser
# warns once: Advanced -> Proceed.
#
#   tools/serve.sh            build + serve on port 8765
#   PORT=8080 tools/serve.sh  another port
#   NOBUILD=1 tools/serve.sh  serve the last build
#
# The ESP32-P4 builds use another PlatformIO platform whose packages clash with the ESP32 and
# ESP32-S3 ones, so they get their own core directory (P4_CORE_DIR, default <core>-p4) and
# build directory (.pio/build-p4; sharing one makes PlatformIO wipe the other chips' builds).
set -e
cd "$(dirname "$0")/.."
if [ -z "$NOBUILD" ]; then
  pio run
  PLATFORMIO_CORE_DIR="${P4_CORE_DIR:-${PLATFORMIO_CORE_DIR:-$HOME/.platformio}-p4}" PLATFORMIO_BUILD_DIR=.pio/build-p4 \
    pio run -e esp32p4 -e esp32p4r3
  node --test site/test/*.test.mjs
fi
python3 tools/make_site.py .pio/build _site
echo "Serving the flasher on https://$(hostname -I 2>/dev/null | awk '{print $1}'):${PORT:-8765}/"
exec python3 tools/serve_https.py _site "${PORT:-8765}" "${CERT_DIR:-.dev-cert}"
