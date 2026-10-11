# PlatformIO post-script: after the app is built, merge bootloader + partition table +
# boot_app0 + app into one image that is written at offset 0x0. The web flasher and
# `esptool.py write_flash 0x0 presencecam-merged.bin` both use this single file.
import os

Import("env")  # noqa: F821  (provided by PlatformIO)


def merge(source, target, env):
    build = env.subst("$BUILD_DIR")
    out = os.path.join(build, "presencecam-merged.bin")
    esptool = os.path.join(env.PioPlatform().get_package_dir("tool-esptoolpy"), "esptool.py")
    parts = []
    for offset, image in env.get("FLASH_EXTRA_IMAGES", []):
        parts += [offset, env.subst(image)]
    parts += [env.subst("$ESP32_APP_OFFSET"), os.path.join(build, env.subst("${PROGNAME}.bin"))]
    cmd = [
        env.subst("$PYTHONEXE"), esptool, "--chip", "esp32", "merge_bin", "-o", out,
        "--flash_mode", "dio", "--flash_freq", "40m", "--flash_size", "4MB",
    ] + parts
    print("Merging firmware image -> " + out)
    env.Execute(" ".join('"%s"' % c for c in cmd))


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge)
