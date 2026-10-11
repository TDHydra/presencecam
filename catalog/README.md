# Hardware catalog

The web flasher's configurator reads these files (combined into `catalog.json` by
`tools/make_site.py`) to offer choices, place wires and draw the wiring diagram. The firmware
never reads them: it gets the finished hardware config (see `src/hwconfig.h`).

## boards/*.json

| Field | |
|---|---|
| `id`, `name`, `short`, `chip` | `chip` is the ESP Web Tools chip family (`ESP32`, `ESP32-S3`) |
| `headers.left` / `headers.right` | Header pins from the USB end's opposite side down: `{"label": "D27", "gpio": 27}` or `{"label": "GND", "power": "gnd"}` (`3v3`, `5v`, `gnd`) |
| `positionsVerified` | `false` draws the headers as a schematic and tells the user to go by the printed GPIO labels |
| `forbidden` | GPIO → reason; never assigned (flash, PSRAM, USB) |
| `reserved` | GPIO → what uses it on the board (LED, button, built-in display, SD); never assigned |
| `i2c` | Default SDA/SCL, shared by every I²C part |
| `uarts` | Free hardware UARTs in order of preference: `{"num": 2, "rx": 16, "tx": 17}` |
| `digitalPrefer` | GPIOs for plain digital inputs, best first |
| `led`, `ledType`, `button` | Status LED (`gpio` or `ws2812`) and the BOOT button |
| `displays` | Display ids offered on this board, first is the default |
| `builtinDisplay` | Display config for a screen soldered on the board |

## sensors/*.json

`bus` (`i2c` or `uart`), `supply` (`3v3`/`5v`), `pins` (the module's pins in the order printed on
it, each with a `role`: `vcc`, `gnd`, `sda`, `scl`, `uart_rx` = the ESP32 receives here, `uart_tx`,
`digital_in`), `presence`, `readings`, `options` (extra fields copied into the sensor's config).

## displays/*.json

`config` is copied into the hardware config's `display`; `bus: "builtin"` uses the board's
`builtinDisplay` instead.

## Adding a part

A sensor also needs a driver in `src/sensors/` (see `sensor.h`); a display type or layout needs
firmware support. A board needs only its JSON, as long as its chip has a build.
