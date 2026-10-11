"""Serve a directory over HTTPS with a self-signed certificate (made on first run).

Web Serial (USB in the browser) only works on https or localhost, so the local test flasher
is served over https. The browser warns once about the certificate: Advanced -> Proceed.

    python3 tools/serve_https.py DIR PORT CERT_DIR
"""
import functools
import http.server
import socket
import ssl
import subprocess
import sys
from pathlib import Path

directory, port, cert_dir = sys.argv[1], int(sys.argv[2]), Path(sys.argv[3])
cert, key = cert_dir / "cert.pem", cert_dir / "key.pem"

if not cert.exists():
    cert_dir.mkdir(parents=True, exist_ok=True)
    host = socket.gethostname()
    ips = subprocess.run(["hostname", "-I"], capture_output=True, text=True).stdout.split()
    san = ",".join([f"DNS:{host}", "DNS:localhost", "IP:127.0.0.1"] + [f"IP:{ip}" for ip in ips if ":" not in ip])
    subprocess.run([
        "openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "3650",
        "-keyout", str(key), "-out", str(cert), "-subj", "/CN=PresenceCam dev flasher",
        "-addext", f"subjectAltName={san}",
    ], check=True, capture_output=True)
    print("made a self-signed certificate for", san)

ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
ctx.load_cert_chain(cert, key)
handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=directory)
server = http.server.ThreadingHTTPServer(("0.0.0.0", port), handler)
server.socket = ctx.wrap_socket(server.socket, server_side=True)
server.serve_forever()
