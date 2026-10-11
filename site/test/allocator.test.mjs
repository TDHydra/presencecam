// Allocator tests: node --test site/test
import assert from "node:assert/strict";
import { readFileSync, readdirSync } from "node:fs";
import { dirname, join } from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";

import { allocate, configPins } from "../allocator.mjs";

const root = join(dirname(fileURLToPath(import.meta.url)), "..", "..");
const load = (dir) =>
  readdirSync(join(root, "catalog", dir))
    .filter((f) => f.endsWith(".json"))
    .sort()
    .map((f) => JSON.parse(readFileSync(join(root, "catalog", dir, f), "utf8")));
const catalog = { boards: load("boards"), sensors: load("sensors"), displays: load("displays"), layouts: load("layouts") };

// The firmware's built-in defaults, read straight out of src/hwconfig.cpp
function firmwareDefaults() {
  const src = readFileSync(join(root, "src", "hwconfig.cpp"), "utf8");
  const block = src.slice(src.indexOf("#if CONFIG_IDF_TARGET_ESP32S3"), src.indexOf("#endif"));
  const [s3, esp32] = block.split("#else").map((part) => JSON.parse([...part.matchAll(/R"\((.*?)\)"/g)].map((m) => m[1]).join("")));
  return { s3, esp32 };
}

const sensorSets = [
  [],
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
        assert.deepEqual(r.errors, [], label);
        const pins = configPins(r.config);
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
            else
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
  assert.deepEqual(r.config, firmwareDefaults().s3);
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
