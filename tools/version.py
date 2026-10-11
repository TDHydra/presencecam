# PlatformIO pre-script: defines FW_VERSION from the PCAM_VERSION environment variable,
# or from the VERSION file when that isn't set.
import os

Import("env")  # noqa: F821  (provided by PlatformIO)

version = os.environ.get("PCAM_VERSION")
if not version:
    try:
        with open(os.path.join(env["PROJECT_DIR"], "VERSION")) as f:
            version = f.read().strip()
    except OSError:
        version = "dev"
env.Append(CPPDEFINES=[("FW_VERSION", env.StringifyMacro(version))])
print("PresenceCam firmware version " + version)
