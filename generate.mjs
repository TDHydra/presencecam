// Makes a firmware image for one setup: takes the prebuilt image for the chip and writes the
// hardware config (the JSON from allocate()) into the config block the firmware reserves for it
// (pcamEmbeddedConfig in src/hwconfig.cpp), then fixes the image's checksum and SHA-256 so the
// bootloader accepts it. Pure functions plus WebCrypto, so it runs in the browser and under node.
//
// Config block: "PCAMCFG1", u32 length, u32 CRC32 of the JSON (both little endian), the JSON.
// Length 0 = empty (what the build ships with).

export const BLOCK_SIZE = 8192;
const MAGIC = [0x50, 0x43, 0x41, 0x4d, 0x43, 0x46, 0x47, 0x31]; // "PCAMCFG1"
const IMAGE_MAGIC = 0xe9;

function u32(b, at) {
  return (b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | (b[at + 3] << 24)) >>> 0;
}
function putU32(b, at, v) {
  for (let i = 0; i < 4; i++) b[at + i] = (v >>> (8 * i)) & 0xff;
}

// CRC32 as esp_rom_crc32_le(0, ...) computes it (the usual zlib CRC32)
export function crc32(bytes) {
  let c = 0xffffffff;
  for (const x of bytes) {
    c ^= x;
    for (let k = 0; k < 8; k++) c = (c >>> 1) ^ (0xedb88320 & -(c & 1));
  }
  return (c ^ 0xffffffff) >>> 0;
}

// The first app partition's offset, from the partition table at 0x8000 of a merged image.
export function appOffset(bytes) {
  for (let at = 0x8000; at < 0x8c00; at += 32) {
    if (bytes[at] !== 0xaa || bytes[at + 1] !== 0x50) break;
    if (bytes[at + 2] === 0x00) return u32(bytes, at + 4); // type app (factory or ota_0, the first one listed)
  }
  throw new Error("no app partition in the partition table");
}

// Walks an ESP app image: { segments: [{start, end}], checksumAt, hashAt (or -1), end }
export function parseImage(bytes, base = 0) {
  if (bytes[base] !== IMAGE_MAGIC) throw new Error(`no app image at 0x${base.toString(16)}`);
  const count = bytes[base + 1];
  const hashAppended = bytes[base + 23] === 1;
  let at = base + 24;
  const segments = [];
  for (let i = 0; i < count; i++) {
    const len = u32(bytes, at + 4);
    segments.push({ start: at + 8, end: at + 8 + len });
    at += 8 + len;
    if (at > bytes.length) throw new Error("image is truncated");
  }
  // padding up to the checksum byte, which is the last byte of a 16-byte block
  const checksumAt = base + (((at - base) | 15));
  const hashAt = hashAppended ? checksumAt + 1 : -1;
  return { base, segments, checksumAt, hashAt, end: hashAppended ? hashAt + 32 : checksumAt + 1 };
}

function findBlock(bytes, from, to) {
  outer: for (let i = from; i <= to - BLOCK_SIZE; i++) {
    for (let k = 0; k < MAGIC.length; k++) if (bytes[i + k] !== MAGIC[k]) continue outer;
    return i;
  }
  return -1;
}

async function sha256(bytes) {
  return new Uint8Array(await globalThis.crypto.subtle.digest("SHA-256", bytes));
}

// Returns a new Uint8Array: `image` with `config` (an object) built in. `merged` = true for the
// full flash image (bootloader + partitions + app, flashed at 0), false for the app alone (the
// OTA .bin).
export async function buildImage(image, config, { merged }) {
  const out = new Uint8Array(image);
  const img = parseImage(out, merged ? appOffset(out) : 0);
  const block = findBlock(out, img.base, img.end);
  if (block < 0) throw new Error("this firmware has no config block (it is older than the web generator)");
  if (!img.segments.some((s) => block >= s.start && block + BLOCK_SIZE <= s.end)) throw new Error("config block straddles a segment");

  const json = new TextEncoder().encode(JSON.stringify(config));
  if (json.length > BLOCK_SIZE - 16) throw new Error(`setup is ${json.length} bytes, more than the ${BLOCK_SIZE - 16} the firmware has room for`);
  const next = new Uint8Array(BLOCK_SIZE);
  next.set(MAGIC, 0);
  putU32(next, 8, json.length);
  putU32(next, 12, crc32(json));
  next.set(json, 16);

  // The checksum is 0xEF XOR every segment byte, so only the changed bytes need folding in
  let sum = out[img.checksumAt];
  for (let i = 0; i < BLOCK_SIZE; i++) {
    sum ^= out[block + i] ^ next[i];
    out[block + i] = next[i];
  }
  out[img.checksumAt] = sum;
  if (img.hashAt >= 0) out.set(await sha256(out.subarray(img.base, img.hashAt)), img.hashAt);
  return out;
}

// Reads the config back out of an image (for tests and for checking a downloaded .bin); null if empty.
export function readConfig(image, { merged }) {
  const img = parseImage(image, merged ? appOffset(image) : 0);
  const block = findBlock(image, img.base, img.end);
  if (block < 0) return null;
  const len = u32(image, block + 8);
  if (!len || len > BLOCK_SIZE - 16) return null;
  const json = image.subarray(block + 16, block + 16 + len);
  if (crc32(json) !== u32(image, block + 12)) throw new Error("config block CRC mismatch");
  return JSON.parse(new TextDecoder().decode(json));
}

// Verifies an image the way the bootloader does: checksum and (if present) SHA-256.
export async function verifyImage(image, { merged }) {
  const img = parseImage(image, merged ? appOffset(image) : 0);
  let sum = 0xef;
  for (const s of img.segments) for (let i = s.start; i < s.end; i++) sum ^= image[i];
  if (sum !== image[img.checksumAt]) return "checksum mismatch";
  if (img.hashAt >= 0) {
    const h = await sha256(image.subarray(img.base, img.hashAt));
    if (h.some((x, i) => x !== image[img.hashAt + i])) return "SHA-256 mismatch";
  }
  return "";
}
