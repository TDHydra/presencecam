# PresenceCam v1 (ESP32-S3 camera), notes

These are the notes that came with the original flasher bundle
(`PresenceCam-flasher-1.1.0-20261011-0252-acce0e96.zip`). The firmware source was not in the
bundle, only compiled binaries, so v1 can't be rebuilt from this repo.

What v1 was:

- **Board:** Waveshare ESP32-S3-LCD-1.47B (16 MB flash, OPI PSRAM, 1.47" LCD, microSD slot, battery charger).
- **Sensors:** HC-SR501 PIR wakes the board from deep sleep, a VL53L0X measures distance, and an Arducam
  Mega 3MP (SPI) takes the photo. A P-MOSFET (AO3401) switches power to the camera and ToF.
- **Power:** 18650 cell with BMS; motion wake pauses when the battery is low.
- **Storage:** `IMG_000001.jpg`… on SD, plus a signed, chained `log.txt` (mirrored to internal
  flash) so a missing or swapped photo shows up.
- **Viewer:** hold BOOT 3 s (or the VIEW button) to open a Wi-Fi access point `PresenceCam-XXXX`
  with a photo grid that verifies every photo against the log.
- **Case:** two-part PETG snap-fit enclosure (front shell + wall plate), see `CASE.md`.

Files:

| File | |
|---|---|
| `FLASHER-README.md` | The v1 flasher's instructions (`pcam.py` flash / monitor / doctor…) |
| `PARTS-LIST.md` | Shopping list summary |
| `CASE.md` | Enclosure design, print settings, assembly |
| `manifest.json`, `partitions.csv` | v1 build manifest and flash layout |

v2 (the rest of this repo) keeps the signed log and the self-hosted web page, and swaps the
camera build for an ESP32 NodeMCU, a radar and an OLED.
