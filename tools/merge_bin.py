# PlatformIO post-script: after the app is built, merge bootloader + partition table +
# boot_app0 + app into one image that is written at offset 0x0. The web flasher and
# `esptool.py write_flash 0x0 presencecam-merged.bin` both use this single file.
import os

Import("env")  # noqa: F821  (provided by PlatformIO)


def merge(source, target, env):
    board = env.BoardConfig()
    build = env.subst("$BUILD_DIR")
    out = os.path.join(build, "presencecam-merged.bin")
    esptool = os.path.join(env.PioPlatform().get_package_dir("tool-esptoolpy"), "esptool.py")
    parts = []
    for offset, image in env.get("FLASH_EXTRA_IMAGES", []):
        parts += [offset, env.subst(image)]
    parts += [env.subst("$ESP32_APP_OFFSET"), os.path.join(build, env.subst("${PROGNAME}.bin"))]
    freq = str(board.get("build.f_flash", "40000000L")).rstrip("L")
    cmd = [
        env.subst("$PYTHONEXE"), esptool, "--chip", board.get("build.mcu", "esp32"), "merge_bin", "-o", out,
        "--flash_mode", "dio",
        "--flash_freq", "%dm" % (int(freq) // 1000000),
        "--flash_size", board.get("upload.flash_size", "4MB"),
    ] + parts
    print("Merging firmware image -> " + out)
    env.Execute(" ".join('"%s"' % c for c in cmd))


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge)
