import test from "node:test";
import assert from "node:assert/strict";
import { existsSync, readFileSync } from "node:fs";
import { BLOCK_SIZE, buildImage, crc32, parseImage, readConfig, verifyImage } from "../generate.mjs";

// A small app image the way esptool lays it out: header, segments, padding, checksum, SHA-256.
async function fakeImage({ hash = true } = {}) {
  const block = new Uint8Array(BLOCK_SIZE);
  block.set(new TextEncoder().encode("PCAMCFG1"));
  const segs = [new Uint8Array(100).fill(7), Uint8Array.from([...new Uint8Array(37).fill(1), ...block, ...new Uint8Array(11).fill(2)])];
  const parts = [];
  const header = new Uint8Array(24);
  header[0] = 0xe9;
  header[1] = segs.length;
  header[23] = hash ? 1 : 0;
  parts.push(header);
  let sum = 0xef;
  for (const s of segs) {
    const h = new Uint8Array(8);
    new DataView(h.buffer).setUint32(4, s.length, true);
    parts.push(h, s);
    for (const x of s) sum ^= x;
  }
  let len = parts.reduce((n, p) => n + p.length, 0);
  const pad = new Uint8Array(16 - (len % 16));
  pad[pad.length - 1] = sum;
  parts.push(pad);
  len += pad.length;
  const out = new Uint8Array(len + (hash ? 32 : 0));
  let at = 0;
  for (const p of parts) out.set(p, (at += p.length) - p.length);
  if (hash) out.set(new Uint8Array(await crypto.subtle.digest("SHA-256", out.subarray(0, len))), len);
  return out;
}

const config = { v: 1, board: "esp32-devkit-30", chip: "ESP32", sensors: [{ type: "ld2410", show: { Still: "off" } }], web: { theme: "dark" } };

test("CRC32 matches the usual (zlib) value", () => {
  assert.equal(crc32(new TextEncoder().encode("123456789")), 0xcbf43926);
});

test("a fake image verifies before and after the setup is built in", async () => {
  for (const hash of [true, false]) {
    const img = await fakeImage({ hash });
    assert.equal(await verifyImage(img, { merged: false }), "");
    assert.equal(readConfig(img, { merged: false }), null);
    const out = await buildImage(img, config, { merged: false });
    assert.equal(out.length, img.length);
    assert.equal(await verifyImage(out, { merged: false }), "", `hash=${hash}`);
    assert.deepEqual(readConfig(out, { merged: false }), config);
    assert.equal(readConfig(img, { merged: false }), null, "the input is left alone");
    // and again over an image that already has a setup in it
    const again = await buildImage(out, { ...config, web: {} }, { merged: false });
    assert.equal(await verifyImage(again, { merged: false }), "");
    assert.deepEqual(readConfig(again, { merged: false }).web, {});
  }
});

test("a setup too big for the block is refused", async () => {
  const img = await fakeImage();
  await assert.rejects(buildImage(img, { pad: "x".repeat(BLOCK_SIZE) }, { merged: false }), /more than/);
});

test("an image without the block is refused", async () => {
  const img = await fakeImage();
  img.set(new TextEncoder().encode("XXXX"), parseImage(img).segments[1].start + 37);
  await assert.rejects(buildImage(img, config, { merged: false }), /no config block/);
});

// With a local build around, the real images too (tools/build.sh makes them)
for (const [file, merged] of [["../../.pio/build/esp32/presencecam-merged.bin", true], ["../../.pio/build/esp32/firmware.bin", false]]) {
  const path = new URL(file, import.meta.url);
  const img = existsSync(path) ? new Uint8Array(readFileSync(path)) : null;
  test(`real image ${file.split("/").pop()}`, { skip: !img || !new TextDecoder().decode(img).includes("PCAMCFG1") ? "no build with a config block" : false }, async () => {
    const out = await buildImage(img, config, { merged });
    assert.equal(await verifyImage(out, { merged }), "");
    assert.deepEqual(readConfig(out, { merged }), config);
  });
}
