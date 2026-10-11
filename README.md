# PresenceCam

Show sensor readings on a small screen and on the web, detect presence, and keep a
**tamper-evident log** of who came and went, on an ESP32 board.

You pick your **board**, **display**, **sensors** and **screen layout** in the web flasher. It
works out which pin each wire goes to, draws the wiring diagram, installs the firmware and
sends your setup to the board. Nothing to compile, no pins to edit.

**Web flasher: <https://tdhydra.github.io/presencecam/>** (Chrome or Edge on a computer)

![Example wiring the flasher draws: ESP32 DevKit with a 0.96" OLED, LD2410 radar and VL53L0X](docs/wiring-example.svg)

## Supported hardware

| | Supported now | Notes |
|---|---|---|
| **Boards** | ESP32 DevKit / NodeMCU ESP-32S (30 pin) | DOIT DevKit V1 layout |
| | ESP32 DevKitC / NodeMCU-32S (38 pin) | Espressif DevKitC V4 layout |
| | Waveshare ESP32-S3-LCD-1.47B | Built-in 1.47" LCD and RGB LED. Header order is drawn as a schematic, so wire by the printed GPIO numbers. |
| **Displays** | 0.96" SSD1306 OLED 128×64 (I²C) | Also works: 1.3" SH1106 (`"type":"sh1106"`) |
| | Built-in ST7789 LCD 172×320 | On the Waveshare board |
| **Sensors** | HLK-LD2410B/C 24 GHz radar | UART (distance, moving/still energy) and/or OUT pin (presence) |
| | VL53L0X laser distance (ToF) | I²C, shares the wires with the OLED. Can also count as presence below a set distance. |
| **Layouts** | Basic | One row per reading with bars, presence on top, extra pages for more rows |

## How it works

```
catalog/*.json ──► web flasher (site/) ──► hardware config JSON ──► firmware (src/)
boards, sensors,   pick parts, allocate       {"board":…, "i2c":…,     reads it at boot,
displays, layouts  pins, draw wiring          "sensors":[…], …}        starts drivers
```

1. **Catalog** ([`catalog/`](catalog/)): one JSON file per board, sensor, display and layout. It
   holds header pin positions, which pins are off-limits, default I²C/UART pins, and how each part
   is wired.
2. **Configurator** ([`site/`](site/)): [`allocator.mjs`](site/allocator.mjs) gives every wire a
   GPIO. I²C parts share one pair, UART parts get a free hardware UART, and plain signals get the
   first safe pin. Flash, PSRAM, USB and on-board pins are never used, and problems are explained.
   [`wiring.mjs`](site/wiring.mjs) draws the diagram.
3. **Install**: ESP Web Tools picks the ESP32 or ESP32-S3 build from the chip it finds, then sets
   up Wi-Fi over USB (Improv).
4. **Send setup**: the page sends `config set {…}` over Web Serial. The firmware checks it (pins
   exist, aren't taken twice, aren't flash/USB), saves it and restarts. You can change parts later
   the same way, without reinstalling.

The firmware ([`src/`](src/)) has no pin numbers built in, only a default per chip. On an ESP32
that's the original wiring: OLED on 21/22, LD2410 on 16/17/27. On an ESP32-S3 it's the Waveshare
LCD.

## Buttons

| Press | Does |
|---|---|
| BOOT, short | Next screen page |
| BOOT, double | Dim / brighten the screen |
| BOOT, hold 3 s | Show the web address and a QR code |
| BOOT, hold 10 s | Forget Wi-Fi, restart into setup |
| RST, once | Restart |
| RST, 2× quickly | Open the `PresenceCam-XXXX` setup network for 10 minutes |
| RST, 3× quickly | Safe mode for one boot: built-in default setup, no sensors |

While BOOT is held, the screen shows what letting go now will do and a bar until the next action.
Holding BOOT during reset or power-up still enters download mode, for flashing. RST cuts the chip
off completely, so the firmware counts quick restarts instead of reading the button. The BOOT
actions can be changed in the hardware config:
`"buttons":{"short":"next","double":"dim","hold3":"info","hold10":"forget_wifi"}`. The possible
actions are `none next dim info forget_wifi setup_ap reboot`.

## Power

The Wi-Fi radio pulls 300–500 mA spikes when it starts. On a weak USB supply, many NodeMCU boards
brown out and reset. The firmware handles this itself
([power.h](src/power.h)). After one brownout reset, Wi-Fi restarts at 8.5 dBm. After a second,
Wi-Fi stays off for that start with a message on screen, while the screen, sensors and console keep
working. If low power turns out stable, auto mode keeps using it. The `boot` log line is only
written after 10 s of uptime, so a reset loop doesn't wear the flash. The real fixes are a better
cable or charger, or a 470–1000 µF capacitor across 3V3 and GND. Wi-Fi power can also be set by
hand (web page, or `txpower auto|19.5|15|11|8.5` in the console).

## Using it

- **Screen**: the layout's pages, then *Network* (IP, signal) and *Device* (version, board, uptime).
- **Web page** (`http://<ip>/` or `http://presencecam-xxxx.local/`): live readings, the event log
  with a *Verified* badge, the hardware in use (pins), settings (name, hold time, LD2410 max
  distance, screen flip and sleep), Wi-Fi, firmware update over the air.
- **Status LED**: lit while someone is present (amber on the Waveshare RGB LED).

### Serial console (115200 baud)

```
status            firmware, sensors, presence, Wi-Fi, display, log
sensors [s]       live readings for s seconds
config            show the hardware config and its pins
config set {...}  replace it (what the web flasher sends), then restart
config reset      back to this chip's default
log [n] · verify  the event log and its chain check
join SSID PASS · forget · setup · name · hold · maxcm · flip · reboot · factory YES
```

### API

| Endpoint | |
|---|---|
| `GET /api/status` | Live JSON: `present`, `readings[]` (`label value unit valid max`), sensor status, Wi-Fi, settings |
| `GET /api/hardware` | The hardware config and the GPIO for each connection |
| `GET /api/log?n=50` · `/api/log.txt` | Log lines / the whole log |
| `GET /api/verify` | Chain check result |
| `POST /api/settings` | `name hold maxcm screensleep screenflip` |
| `POST /api/wifi` | `ssid pass` |
| `POST /update` | Firmware `.bin` (`presencecam-<chip>-ota.bin`) |

## The event log

Each event (boot, present, clear, wifi, settings, hardware, update) is one line in flash:

```
n|unix_time|uptime_s|event|detail|prev|mac
```

`mac` is an HMAC-SHA256 of the rest of the line, keyed with a random secret the board makes on its
first start. `prev` is the previous line's `mac`, and the line counter is stored separately. So
changed, removed or reordered lines are caught, and so is an emptied log. It protects against edits
through the web page and to downloaded copies, not against someone who reads the key out of the
chip's flash. Erasing the board starts a new key at line 1.

## Adding hardware

- **A board** with an ESP32 or ESP32-S3: add `catalog/boards/<id>.json` (see
  [catalog/README.md](catalog/README.md)). No firmware change.
- **A sensor**: write a driver class in `src/sensors/` implementing `Sensor`
  ([sensor.h](src/sensors/sensor.h)). Add it to `makeSensor()`, accept its type in
  `validate()` ([hwconfig.cpp](src/hwconfig.cpp)), and add `catalog/sensors/<id>.json`.
- **A display**: a `Gfx` backend in `src/display/` ([gfx.h](src/display/gfx.h)), plus its type
  in `displayBegin()` and `validate()`, plus `catalog/displays/<id>.json`.
- **A layout**: a `Layout` in `src/layouts/` ([layout.h](src/layouts/layout.h)), listed in
  `layoutFind()` and `validate()`, plus `catalog/layouts/<id>.json`.

Then run the tests: `node --test site/test/*.test.mjs`. Among other things, they check that every
board × display × sensor combination gets conflict-free pins, and that the firmware's built-in
defaults match what the configurator produces.

## Building

```sh
pio run                      # both chips; images in .pio/build/<env>/presencecam-merged.bin
pio run -e esp32 -t upload   # flash an ESP32 board directly
node --test site/test/*.test.mjs
tools/serve.sh               # build, test and serve the flasher on port 8000
```

**Testing locally:** `tools/serve.sh` builds both chips, runs the tests and serves the flasher at
`https://<this machine>:8765` with a self-signed certificate (USB in the browser needs https). The
browser warns once: *Advanced → Proceed*.

**Releasing:** GitHub Actions ([build.yml](.github/workflows/build.yml)) runs only for releases. Bump
[`VERSION`](VERSION), commit, then `git tag vX.Y.Z && git push origin main vX.Y.Z`. It runs the tests,
builds both chips, publishes the flasher to GitHub Pages and attaches the images to the release.

## Project layout

```
catalog/          boards, sensors, displays, layouts (JSON)
site/             web flasher: index.html, allocator.mjs, wiring.mjs, serial.mjs, configurator.mjs, test/
src/
  main.cpp        startup and main loop
  hwconfig.*      the hardware config: defaults, validation, storage
  sensors/        Sensor interface + LD2410, VL53L0X
  display/        Gfx interface + OLED (U8g2) and ST7789 (LovyanGFX) backends; display.cpp runs pages
  layouts/        Basic layout
  buttons.*       BOOT gestures and RST multi-tap
  presence.*      hold time, present/clear events, status LED
  eventlog.*      signed, chained log
  net.*           Wi-Fi, setup network, mDNS, Improv
  web.*           web page and API (web_page.h is the dashboard)
  console.*       serial commands
tools/            version, merged image, site assembly
legacy-s3/        notes from the original ESP32-S3 camera version
```

## History

PresenceCam started as a battery-powered motion **camera** on the Waveshare ESP32-S3-LCD-1.47B:
PIR wake, an Arducam Mega 3MP, a VL53L0X, photos to SD card, and the same signed log so photos
couldn't be quietly deleted. Its notes are in [`legacy-s3/`](legacy-s3/).
