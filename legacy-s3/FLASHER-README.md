# PresenceCam flasher

Installs the PresenceCam firmware on the Waveshare ESP32-S3-LCD-1.47B and helps debug it.
Works on Windows, macOS and Linux. Needs only Python 3.8+; everything else installs itself
into a private `.venv` folder on first run.

## Flash

1. Plug the board into the computer with a USB-C **data** cable (not a charge-only one).
2. Windows: double-click `pcam.bat`. macOS: double-click `pcam.command` (first time:
   right-click, Open). Linux: `./pcam.sh`.
3. Choose **1** (flash). Or from a terminal: `python pcam.py flash`.

The flasher finds the board by its USB id, checks it is an ESP32-S3 with 16 MB flash, writes
the bootloader, partition table and app, verifies them, resets the board and waits for the
firmware's banner so you know the new build is really running. It does **not** touch the
device's secret signing key, the photo/line counters or the internal log mirror (the NVS and
LittleFS partitions), so an update never breaks the chain of an existing log.

If the board does not answer: hold **BOOT**, tap **RESET** (or re-plug USB while holding
BOOT), release BOOT, and run the flash again. A board on battery with no USB power sensed
deep-sleeps and its USB port disappears until it is woken this way.

## Debug tools

| Command | What it does |
|---|---|
| `python pcam.py monitor` | Serial monitor with timestamps. Reconnects after resets, explains reset reasons and known error lines, decodes crashes into function names, saves a copy in `logs/`. Type a command + Enter to send it to the firmware. |
| `python pcam.py console status` | Sends one debug command and prints the reply. Firmware commands: `help status pins [s] cam tof sd log [n] ilog [n] mem time lcd rail on/off shot view sleep reboot`. |
| `python pcam.py doctor [--probe]` | Checks Python, esptool, drivers, port permissions, programs hogging the port, and optionally talks to the chip. |
| `python pcam.py ports` | Lists serial ports and marks the camera board. |
| `python pcam.py info` | Chip model/revision, MAC, flash size, PSRAM, secure boot / encryption state, compared with what the firmware needs. |
| `python pcam.py verify` | Compares the flash with the bundle byte for byte. |
| `python pcam.py partitions` | Reads and decodes the partition table from the device. |
| `python pcam.py backup [--data]` | Saves the whole flash, or just the key + counters + internal log, to `backups/`. `restore FILE` writes it back. |
| `python pcam.py decode [FILE]` | Turns a pasted `Backtrace: 0x...` into function names (file:line too if the Arduino IDE's esp32 toolchain is installed). |
| `python pcam.py erase [--nvs]` | Factory reset. Asks twice: it destroys the signing key and counters. |
| `python pcam.py reset` / `bootloader` | Restart the board, or hold it in download mode. |

`python pcam.py flash --full` erases everything first (new key, counting restarts at 1).

## What is in `firmware/`

`bootloader.bin` (0x0), `partitions.bin` (0x8000), `boot_app0.bin` (0xE000), `app.bin` (0x10000),
`manifest.json` (offsets, checksums, expected chip/flash, build id), `symbols.json` (function
table for crash decoding), `app.elf` (for file:line decoding), `partitions.csv`.
Rebuilt by `firmware/build.sh` in the project repo.
