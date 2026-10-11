"""Assemble the web flasher site (GitHub Pages) from site/ and a finished build.

    python tools/make_site.py [build_dir] [out_dir]

Defaults: .pio/build/nodemcu-32s -> _site. Serve the result over https (or localhost)
because Web Serial only works in a secure context:  python -m http.server -d _site
"""
import json
import pathlib
import shutil
import sys

root = pathlib.Path(__file__).resolve().parent.parent
build = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else root / ".pio/build/nodemcu-32s"
out = pathlib.Path(sys.argv[2]) if len(sys.argv) > 2 else root / "_site"
version = (root / "VERSION").read_text().strip()

if out.exists():
    shutil.rmtree(out)
shutil.copytree(root / "site", out)
shutil.copy(root / "docs/wiring.svg", out / "wiring.svg")
fw = out / "firmware"
fw.mkdir()
shutil.copy(build / "presencecam-merged.bin", fw / "presencecam-merged.bin")
shutil.copy(build / "firmware.bin", fw / "presencecam-ota.bin")

manifest = {
    "name": "PresenceCam",
    "version": version,
    "new_install_prompt_erase": True,
    "new_install_improv_wait_time": 20,
    "builds": [{
        "chipFamily": "ESP32",
        "parts": [{"path": "firmware/presencecam-merged.bin", "offset": 0}],
    }],
}
(out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
html = out / "index.html"
html.write_text(html.read_text().replace("{{VERSION}}", version))
print("site ready in", out, "version", version)
