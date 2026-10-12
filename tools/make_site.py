"""Assemble the web flasher site (GitHub Pages) from site/, catalog/ and a finished build.

    python tools/make_site.py [pio_build_dir] [out_dir]

Defaults: .pio/build -> _site. Needs every firmware build; the ESP32-P4 ones are looked for in
.pio/build-p4 next to it (see tools/serve.sh).
Serve the result over https or localhost, because Web Serial only works in a secure context:
    python -m http.server -d _site
"""
import json
import pathlib
import shutil
import sys

root = pathlib.Path(__file__).resolve().parent.parent
builds = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else root / ".pio/build"
out = pathlib.Path(sys.argv[2]) if len(sys.argv) > 2 else root / "_site"
version = (root / "VERSION").read_text().strip()

# PlatformIO env -> (ESP Web Tools chip family, file name stem)
CHIPS = {
    "esp32": ("ESP32", "presencecam-esp32"),
    "esp32s3": ("ESP32-S3", "presencecam-esp32s3"),
    "esp32p4": ("ESP32-P4", "presencecam-esp32p4"),
    "esp32p4r3": ("ESP32-P4", "presencecam-esp32p4r3"),
}
# ESP Web Tools picks a build by chip family only, so the two ESP32-P4 revisions get a manifest
# each; the configurator switches between them.
MANIFESTS = {
    "manifest.json": ["esp32", "esp32s3", "esp32p4"],
    "manifest-p4r3.json": ["esp32", "esp32s3", "esp32p4r3"],
}

if out.exists():
    shutil.rmtree(out)
shutil.copytree(root / "site", out, ignore=shutil.ignore_patterns("test"))
(out / ".nojekyll").write_text("")

# One catalog file for the configurator
catalog = {}
for kind in ("boards", "sensors", "displays", "layouts"):
    catalog[kind] = [json.loads(p.read_text()) for p in sorted((root / "catalog" / kind).glob("*.json"))]
(out / "catalog.json").write_text(json.dumps(catalog, ensure_ascii=False))

fw = out / "firmware"
fw.mkdir()
for env, (family, stem) in CHIPS.items():
    build = builds / env
    if not build.exists():
        build = builds.with_name("build-p4") / env
    shutil.copy(build / "presencecam-merged.bin", fw / f"{stem}.bin")
    shutil.copy(build / "firmware.bin", fw / f"{stem}-ota.bin")

for name, envs in MANIFESTS.items():
    manifest = {
        "name": "PresenceCam",
        "version": version,
        "new_install_prompt_erase": True,
        "new_install_improv_wait_time": 20,
        "builds": [
            {"chipFamily": CHIPS[env][0], "parts": [{"path": f"firmware/{CHIPS[env][1]}.bin", "offset": 0}]}
            for env in envs
        ],
    }
    (out / name).write_text(json.dumps(manifest, indent=2) + "\n")
html = out / "index.html"
html.write_text(html.read_text().replace("{{VERSION}}", version))
print("site ready in", out, "version", version)
