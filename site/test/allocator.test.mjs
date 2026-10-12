// Allocator tests: node --test site/test
import assert from "node:assert/strict";
import { readFileSync, readdirSync } from "node:fs";
import { dirname, join } from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";

import { allocate, configPins, defaultSelection } from "../allocator.mjs";

const root = join(dirname(fileURLToPath(import.meta.url)), "..", "..");
const load = (dir) =>
  readdirSync(join(root, "catalog", dir))
    .filter((f) => f.endsWith(".json"))
    .sort()
    .map((f) => JSON.parse(readFileSync(join(root, "catalog", dir, f), "utf8")));
const catalog = { boards: load("boards"), sensors: load("sensors"), displays: load("displays"), layouts: load("layouts"), cameras: load("cameras") };

// The firmware's built-in defaults, read straight out of src/hwconfig.cpp
function firmwareDefaults() {
  const src = readFileSync(join(root, "src", "hwconfig.cpp"), "utf8");
  // #if CONFIG_IDF_TARGET_ESP32P4 ... #elif CONFIG_IDF_TARGET_ESP32S3 ... #else (plain ESP32) ... #endif
  const start = src.indexOf("#if CONFIG_IDF_TARGET_");
  const block = src.slice(start, src.indexOf("#endif", start));
  const out = {};
  for (const m of block.matchAll(/#(?:if|elif) CONFIG_IDF_TARGET_(\w+)([\s\S]*?)(?=#elif|#else|$)|#else([\s\S]*)/g)) {
    const chip = m[1] ? m[1].toLowerCase() : "esp32";
    out[chip] = JSON.parse([...(m[2] ?? m[3]).matchAll(/R"\((.*?)\)"/g)].map((x) => x[1]).join(""));
  }
  return out;
}

const sensorSets = [
  [],
  [{ id: "hcsr501" }],
  [{ id: "vl53l1x" }],
  [{ id: "hcsr04" }],
  [{ id: "ld2410" }, { id: "hcsr501" }],
  [{ id: "ld2410" }],
  [{ id: "ld2410", groups: { uart: false } }],
  [{ id: "ld2410", groups: { out: false } }],
  [{ id: "vl53l0x" }],
  [{ id: "ld2410" }, { id: "vl53l0x", options: { presenceMm: 500 } }],
];

test("every board, display and sensor set allocates without conflicts", () => {
  for (const board of catalog.boards) {
    for (const display of board.displays) {
      for (const sensors of sensorSets) {
        const label = `${board.id} / ${display} / ${sensors.map((s) => s.id).join("+") || "no sensors"}`;
        const r = allocate(catalog, { board: board.id, display, layout: "basic", sensors });
        // Small boards may run out of pins for the bigger sets; that must be reported, not mis-wired
        const outOfPins = (e) => /no free (GPIO|hardware UART) left/.test(e);
        assert.deepEqual(r.errors.filter((e) => !outOfPins(e)), [], label);
        if (r.errors.length && display === "none" && sensors.length < 2) assert.fail(`${label}: ${r.errors.join("; ")}`);
        const pins = configPins(r.config).filter((p) => p.gpio >= 0);
        const seen = new Map();
        for (const p of pins) {
          // I2C pins are shared by design; nothing else may repeat
          assert.ok(!seen.has(p.gpio), `${label}: GPIO${p.gpio} used by ${seen.get(p.gpio)} and ${p.what}`);
          seen.set(p.gpio, p.what);
          assert.ok(!(p.gpio in (board.forbidden || {})), `${label}: ${p.what} on forbidden GPIO${p.gpio}`);
        }
        // every signal wire lands on a header pin, every power wire on a power pin
        for (const part of r.parts)
          for (const pin of part.pins) {
            if (pin.to.power) assert.ok(pin.to.label, `${label}: ${part.name} ${pin.label} has no power pin`);
            else if (pin.to.gpio >= 0)
              assert.ok(
                Object.values(board.headers).flat().some((h) => h.gpio === pin.to.gpio),
                `${label}: ${part.name} ${pin.label} -> GPIO${pin.to.gpio} isn't on a header`,
              );
          }
      }
    }
  }
});

test("NodeMCU 30-pin + OLED + LD2410 matches the firmware's ESP32 default", () => {
  const r = allocate(catalog, { board: "esp32-devkit-30", display: "ssd1306", layout: "basic", sensors: [{ id: "ld2410" }] });
  assert.deepEqual(r.config, firmwareDefaults().esp32);
});

test("Waveshare 1.47B with its LCD and no sensors matches the firmware's ESP32-S3 default", () => {
  const r = allocate(catalog, { board: "waveshare-s3-lcd-147b", display: "builtin", layout: "basic", sensors: [] });
  assert.deepEqual(r.config, firmwareDefaults().esp32s3);
});

test("Waveshare ESP32-P4-ETH with nothing attached matches the firmware's ESP32-P4 default", () => {
  const r = allocate(catalog, { board: "waveshare-p4-eth", display: "none", layout: "basic", sensors: [] });
  assert.deepEqual(r.config, firmwareDefaults().esp32p4);
});

test("XIAO ESP32-C3 and C6 with nothing attached match the firmware's defaults", () => {
  for (const [board, chip] of [["xiao-esp32c3", "esp32c3"], ["xiao-esp32c6", "esp32c6"]]) {
    const r = allocate(catalog, { board, display: "none", layout: "basic", sensors: [] });
    assert.deepEqual(r.config, firmwareDefaults()[chip], board);
  }
});

test("two parts at one I2C address are refused", () => {
  const r = allocate(catalog, { board: "esp32-devkit-30", display: "ssd1306", layout: "basic", sensors: [{ id: "vl53l0x" }, { id: "vl53l1x" }] });
  assert.ok(r.errors.some((e) => e.includes("0x29")), r.errors.join("; "));
});

test("the HC-SR501 gets a plain input pin", () => {
  const r = allocate(catalog, { board: "esp32-devkit-30", display: "none", layout: "basic", sensors: [{ id: "hcsr501" }] });
  assert.deepEqual(r.errors, []);
  assert.equal(r.config.sensors[0].type, "hcsr501");
  assert.ok(r.config.sensors[0].out >= 0);
});

test("boards with Ethernet and Wi-Fi use Ethernet unless Wi-Fi is picked", () => {
  const sel = { board: "waveshare-s3-eth", display: "none", layout: "basic", sensors: [] };
  assert.equal(allocate(catalog, sel).config.eth.type, "w5500");
  assert.equal(allocate(catalog, { ...sel, network: "wifi" }).config.eth, undefined);
  // the ESP32-P4-ETH has no Wi-Fi, so it keeps Ethernet whatever is picked
  assert.equal(allocate(catalog, { ...sel, board: "waveshare-p4-eth", network: "wifi" }).config.eth.type, "ip101");
});

test("cameras: SPI one gets pins and wires, built-in ones only where they belong", () => {
  const spi = allocate(catalog, { board: "esp32-devkit-30", display: "ssd1306", layout: "basic", sensors: [{ id: "ld2410" }], camera: "arducam_mega" });
  assert.deepEqual(spi.errors, []);
  for (const k of ["cs", "sck", "mosi", "miso"]) assert.ok(spi.config.camera[k] >= 0, k);
  assert.ok(spi.parts.some((p) => p.id === "arducam_mega"));
  const cam = allocate(catalog, { board: "ai-thinker-esp32-cam", display: "none", layout: "basic", sensors: [], camera: "esp32cam" });
  assert.deepEqual(cam.errors, []);
  assert.equal(cam.config.camera.type, "esp32cam");
  const wrong = allocate(catalog, { board: "esp32-devkit-30", display: "none", layout: "basic", sensors: [], camera: "esp32cam" });
  assert.ok(wrong.errors.length);
});

test("the LD2410 needs 5 V and the OLED 3.3 V", () => {
  const r = allocate(catalog, { board: "esp32-devkit-30", display: "ssd1306", layout: "basic", sensors: [{ id: "ld2410" }] });
  const vcc = (id) => r.parts.find((p) => p.id === id).pins.find((p) => p.role === "vcc").to;
  assert.equal(vcc("ld2410").label, "VIN");
  assert.equal(vcc("ssd1306").label, "3V3");
});

test("running out of UARTs is reported, not silently mis-wired", () => {
  const r = allocate(catalog, {
    board: "esp32-devkit-30", display: "none", layout: "basic",
    sensors: [{ id: "ld2410" }, { id: "ld2410" }],
  });
  assert.ok(r.errors.some((e) => e.includes("no free hardware UART")), r.errors.join("; "));
});

test("an unknown sensor is an error", () => {
  const r = allocate(catalog, { board: "esp32-devkit-30", display: "none", layout: "basic", sensors: [{ id: "bogus" }] });
  assert.ok(r.errors.length);
});

test("show and web choices go into the config, defaults left out", () => {
  const sel = defaultSelection(catalog, "esp32-devkit-30");
  sel.sensors = [{ id: "ld2410", show: { Distance: "web", Still: "off", Radar: "both", Bogus: "web" } }];
  sel.web = { title: "  Hall  ", theme: "auto", accent: "#AABBCC", style: "big", hide: ["log"], admin: "collapsed" };
  const { config, errors } = allocate(catalog, sel);
  assert.deepEqual(errors, []);
  assert.deepEqual(config.sensors[0].show, { Distance: "web", Still: "off" });
  assert.deepEqual(config.web, { title: "Hall", accent: "#aabbcc", style: "big", hide: ["log"], admin: "collapsed" });
  sel.web = { theme: "auto", style: "cards" };
  assert.equal(allocate(catalog, sel).config.web, undefined);
});

test("microSD: on by default on the ESP32-P4 boards, a choice on the ESP32-CAM", () => {
  for (const board of ["waveshare-p4-eth", "waveshare-p4-module-devkit"]) {
    const sel = defaultSelection(catalog, board);
    const r = allocate(catalog, sel);
    assert.deepEqual(r.errors, [], board);
    assert.equal(r.config.sd.type, "sdmmc");
    assert.equal(r.config.sd.power, 45);
    assert.equal(allocate(catalog, { ...sel, storage: "flash" }).config.sd, undefined);
  }
  const cam = defaultSelection(catalog, "ai-thinker-esp32-cam");
  assert.equal(allocate(catalog, cam).config.sd, undefined);
  const withSd = allocate(catalog, { ...cam, display: "none", storage: "sd" });
  assert.deepEqual(withSd.errors, []);
  assert.deepEqual({ ...withSd.config.sd }, { type: "sdmmc", clk: 14, cmd: 15, d0: 2, width: 1 });
  // the card takes the I2C pins, so an I2C part can't go with it
  const clash = allocate(catalog, { ...cam, display: "none", storage: "sd", sensors: [{ id: "vl53l0x" }] });
  assert.match(clash.errors.join(" "), /GPIO1[45] is already used by SD card/);
  assert.match(clash.errors.join(" "), /Flash only/);
});

test("microSD on SPI: S3-ETH on its own pins, C6-LCD sharing the LCD's", () => {
  for (const board of ["waveshare-s3-eth", "waveshare-c6-lcd-147"]) {
    const r = allocate(catalog, defaultSelection(catalog, board));
    assert.deepEqual(r.errors, [], board);
    assert.deepEqual({ ...r.config.sd }, { type: "spi", cs: 4, miso: 5, mosi: 6, sck: 7 });
  }
  const c6 = allocate(catalog, defaultSelection(catalog, "waveshare-c6-lcd-147")).config;
  assert.equal(c6.display.sclk, c6.sd.sck);
  assert.equal(c6.display.mosi, c6.sd.mosi);
});
