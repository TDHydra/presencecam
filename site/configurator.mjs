// The flasher page's configurator: board → display → sensors → layout → what shows where →
// the web page, then the wiring diagram, pin table, and the install button with firmware made
// for this setup (generate.mjs), plus update downloads and "send setup to board".
import { allocate, camerasFor, defaultSelection, findById, hasWifi, useSd } from "./allocator.mjs";
import { buildImage } from "./generate.mjs";
import { sendConfig, serialSupported } from "./serial.mjs";
import { renderWiring } from "./wiring.mjs";

const $ = (id) => document.getElementById(id);
const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);
const STORE = "presencecam.selection.v1";

let catalog;
let sel;
let result;
let justAdded = null;  // part id the user just switched on: highlighted in the wiring diagram

function save() {
  try {
    localStorage.setItem(STORE, JSON.stringify(sel));
  } catch {}
}

function restore() {
  try {
    const s = JSON.parse(localStorage.getItem(STORE) || "null");
    if (s && findById(catalog.boards, s.board)) return s;
  } catch {}
  const s = defaultSelection(catalog, catalog.boards[0].id);
  s.sensors = [{ id: "ld2410" }];
  return s;
}

function choice(name, value, checked, title, detail, type = "radio") {
  return `<label class="choice"><input type="${type}" name="${name}" value="${esc(value)}"${checked ? " checked" : ""}>
    <span><b>${esc(title)}</b>${detail ? `<small>${esc(detail)}</small>` : ""}</span></label>`;
}

function renderChoices() {
  const board = findById(catalog.boards, sel.board);
  if (!board.displays.includes(sel.display)) sel.display = board.displays[0];

  $("boards").innerHTML = catalog.boards
    .map((b) => choice("board", b.id, b.id === sel.board, b.name, `${b.chip}. ${b.notes}`))
    .join("");

  const both = !!board.ethernet && hasWifi(board);
  $("netpick").hidden = !both;
  const net = sel.network === "wifi" ? "wifi" : "eth";
  $("networks").innerHTML = both
    ? choice("network", "eth", net === "eth", "Ethernet", "Address by DHCP over the network cable (and PoE power, if the board has it).") +
      choice("network", "wifi", net === "wifi", "Wi-Fi", "Joins your Wi-Fi; the network jack isn't used.")
    : "";

  $("sdpick").hidden = !board.sd;
  const sd = useSd(board, sel);
  $("storages").innerHTML = board.sd
    ? choice("storage", "sd", sd, "microSD card", `The event log, photos and debug log go on the card (falls back to flash without one). ${board.sd.note || ""}`) +
      choice("storage", "flash", !sd, "Flash only", "Keeps everything in the board's flash (small: the newest few photos and log lines).")
    : "";

  $("displays").innerHTML = board.displays
    .map((id) => {
      const d = findById(catalog.displays, id);
      const name = id === "builtin" ? board.builtinDisplay.name : d.name;
      return choice("display", id, id === sel.display, name, d.description);
    })
    .join("");

  $("sensors").innerHTML = catalog.sensors
    .map((s) => {
      const on = sel.sensors.find((x) => x.id === s.id);
      let extra = "";
      if (on) {
        for (const g of s.groups || [])
          extra += `<label class="sub"><input type="checkbox" data-sensor="${s.id}" data-group="${g.id}"${(on.groups?.[g.id] ?? g.default) ? " checked" : ""}> ${esc(g.label)}</label>`;
        for (const o of s.options || [])
          extra += `<label class="sub">${esc(o.label)} <input type="number" data-sensor="${s.id}" data-option="${o.key}" min="${o.min}" max="${o.max}" value="${esc(on.options?.[o.key] ?? o.default)}"></label>`;
      }
      return `<div class="sensor">${choice("sensor", s.id, !!on, s.name, s.description, "checkbox")}${extra}</div>`;
    })
    .join("");

  const cams = camerasFor(catalog, board);
  $("camsec").hidden = !cams.length;
  if (sel.camera && sel.camera !== "none" && !cams.some((c) => c.id === sel.camera)) sel.camera = "none";
  $("cameras").innerHTML = cams.length
    ? choice("camera", "none", !sel.camera || sel.camera === "none", "No camera", "") +
      cams.map((c) => choice("camera", c.id, c.id === sel.camera, c.name, c.description)).join("")
    : "";

  $("layouts").innerHTML = catalog.layouts
    .map((l) => choice("layout", l.id, l.id === sel.layout, l.name, l.description))
    .join("");

  renderShow();
  renderWeb();
}

const PLACES = [
  ["both", "Screen and web page"],
  ["screen", "Screen only"],
  ["web", "Web page only"],
  ["off", "Neither"],
];

function renderShow() {
  const noScreen = sel.display === "none";
  const rows = [];
  for (const s of sel.sensors) {
    const def = findById(catalog.sensors, s.id);
    for (const r of def?.readings || []) {
      const at = s.show?.[r.key] || "both";
      const opts = PLACES.map(([v, t]) => `<option value="${v}"${v === at ? " selected" : ""}${noScreen && v === "screen" ? " disabled" : ""}>${t}</option>`).join("");
      rows.push(`<tr><td>${esc(def.short || def.name)}: ${esc(r.label)}</td><td><select data-show="${esc(s.id)}" data-key="${esc(r.key)}">${opts}</select></td></tr>`);
    }
  }
  $("showtable").innerHTML = rows.join("") || `<tr><td colspan="2" class="mut">Pick a sensor first.</td></tr>`;
}

const SECTIONS = [
  ["log", "Event log"],
  ["rules", "Rules"],
  ["photos", "Photos (with a camera)"],
  ["health", "Health"],
  ["hardware", "Hardware and pins"],
  ["debug", "Debug log"],
];

function renderWeb() {
  const w = sel.web || {};
  if (document.activeElement !== $("webtitle")) $("webtitle").value = w.title || "";
  $("webaccent").value = w.accent || "#2563eb";
  $("webtheme").innerHTML = [["auto", "Follow the viewer's device", "Light or dark, whichever the phone or computer uses"], ["light", "Light", ""], ["dark", "Dark", ""]]
    .map(([v, t, d]) => choice("webtheme", v, (w.theme || "auto") === v, t, d))
    .join("");
  $("webstyle").innerHTML = [["cards", "Cards", "A grid of small tiles with bars"], ["list", "List", "One reading per line, compact"], ["big", "Big numbers", "Large values, readable across a room"]]
    .map(([v, t, d]) => choice("webstyle", v, (w.style || "cards") === v, t, d))
    .join("");
  const hide = new Set(w.hide || []);
  $("websections").innerHTML = SECTIONS.map(([k, t]) => `<label class="chk"><input type="checkbox" data-section="${k}"${hide.has(k) ? "" : " checked"}> ${esc(t)}</label>`).join("") +
    `<p class="mut">Unticked ones sit behind a "Settings and more" button at the bottom.</p>`;
  $("webadmin").innerHTML = [["open", "Shown", ""], ["collapsed", "Behind \"Settings and more\"", "A cleaner page for a wall display; one tap shows them"]]
    .map(([v, t, d]) => choice("webadmin", v, (w.admin || "open") === v, t, d))
    .join("");
}

function renderResult() {
  const board = findById(catalog.boards, sel.board);
  result = allocate(catalog, sel);
  save();

  $("problems").innerHTML =
    result.errors.map((e) => `<div class="note bad">${esc(e)}</div>`).join("") +
    result.warnings.map((w) => `<div class="note">${esc(w)}</div>`).join("");

  if (result.parts.length) {
    const ids = result.parts.map((p) => p.id);
    if (justAdded && !ids.includes(justAdded)) justAdded = null;
    $("wiring").innerHTML = renderWiring(board, result.parts, { title: board.name, highlight: justAdded });
    $("wiringnote").textContent = justAdded
      ? `Highlighted: the ${result.parts.find((p) => p.id === justAdded).name} you just added. Click the diagram to show everything equally.`
      : "";
    $("wiring-wrap").hidden = false;
  } else {
    $("wiring").innerHTML = "";
    $("wiring-wrap").hidden = true;
  }

  const rows = [];
  for (const part of result.parts)
    for (const pin of part.pins)
      rows.push(`<tr><td>${esc(part.name)}</td><td class="mono">${esc(pin.label)}</td><td class="mono">${esc(pin.to.label)}${pin.to.gpio !== undefined ? ` <span class="mut">(GPIO${pin.to.gpio})</span>` : ""}</td></tr>`);
  $("pintable").innerHTML = rows.length
    ? rows.join("")
    : `<tr><td colspan="3" class="mut">Nothing to wire: ${result.config?.display.type === "none" ? "no display and " : ""}no sensors chosen.</td></tr>`;

  $("config").textContent = result.config ? JSON.stringify(result.config) : "";
  $("chipnote").textContent = `This setup needs the ${board.chip} firmware. The installer picks it automatically from the chip it finds.`;
  const p4 = board.chip === "ESP32-P4";
  $("p4rev").hidden = !p4;
  for (const r of document.getElementsByName("p4rev")) r.checked = r.value === pref("p4rev", "1");
  // Arduino core 2 builds exist for these chips only, and have no Ethernet
  const core2ok = CORE2_CHIPS.includes(board.chip) && !result.config?.eth;
  const core = core2ok ? pref("core", "3") : "3";
  $("corepick").hidden = !CORE2_CHIPS.includes(board.chip);
  for (const r of document.getElementsByName("core")) {
    r.checked = r.value === core;
    r.disabled = r.value === "2" && !core2ok;
  }
  $("core2note").textContent = core2ok ? "Wi-Fi only" : "Not with Ethernet: pick Wi-Fi as the network to use it";
  const stem = "presencecam-" + CHIP_STEMS[board.chip] + (p4 && pref("p4rev", "1") === "3" ? "r3" : "") + (core === "2" ? "-core2" : "");
  generate(board, stem);
  $("wifinote").textContent = result.config?.eth
    ? "On a new board, let it erase the device when asked. This board uses Ethernet: plug in a network cable and it gets an address by DHCP (or set a fixed one on its web page)."
    : "On a new board, let it erase the device when asked. When it finishes it asks for your Wi-Fi name and password and sends them over the cable. Then close the installer window.";
  $("send").disabled = !!result.errors.length || !serialSupported();
}

const CORE2_CHIPS = ["ESP32", "ESP32-S3", "ESP32-C3"];
const CHIP_STEMS = { ESP32: "esp32", "ESP32-S3": "esp32s3", "ESP32-C3": "esp32c3", "ESP32-C6": "esp32c6", "ESP32-P4": "esp32p4" };

// --- firmware made for this setup ------------------------------------------------------
// The prebuilt image for the chip is fetched once, then the setup is written into it (see
// generate.mjs). The installer gets a manifest (as a blob: URL) with just that image, so it
// also refuses a board with a different chip.
const fetched = new Map(); // path -> Promise<Uint8Array>
const blobs = { manifest: null, full: null, ota: null };
let genSeq = 0;
let siteVersion = "";

function fetchBin(path) {
  if (!fetched.has(path))
    fetched.set(path, fetch(path).then((r) => {
      if (!r.ok) throw new Error(`${path}: ${r.status}`);
      return r.arrayBuffer();
    }).then((b) => new Uint8Array(b)));
  const p = fetched.get(path);
  p.catch(() => fetched.delete(path)); // try again next time
  return p;
}

function setBlob(kind, data, type) {
  if (blobs[kind]) URL.revokeObjectURL(blobs[kind]);
  blobs[kind] = data ? URL.createObjectURL(new Blob([data], { type })) : null;
  return blobs[kind];
}

let genTimer = 0;
function generate(board, stem) {
  clearTimeout(genTimer);
  const seq = ++genSeq;
  const button = document.querySelector("esp-web-install-button");
  const busy = (on) => {
    for (const id of ["dlota", "dlfull"]) $(id).disabled = on || !!result.errors.length;
  };
  busy(true);
  if (result.errors.length || !result.config) {
    $("genstate").textContent = "Fix the problems above first.";
    return;
  }
  $("genstate").textContent = "Making the firmware…";
  genTimer = setTimeout(async () => {
    try {
      const config = result.config;
      const [full, ota] = await Promise.all([fetchBin(`firmware/${stem}.bin`), fetchBin(`firmware/${stem}-ota.bin`)]);
      const [fullImg, otaImg] = await Promise.all([buildImage(full, config, { merged: true }), buildImage(ota, config, { merged: false })]);
      if (seq !== genSeq) return; // the setup changed meanwhile
      const part = setBlob("full", fullImg, "application/octet-stream");
      setBlob("ota", otaImg, "application/octet-stream");
      const manifest = {
        name: "PresenceCam",
        version: siteVersion,
        new_install_prompt_erase: true,
        new_install_improv_wait_time: config.eth ? 0 : 20,
        builds: [{ chipFamily: board.chip, parts: [{ path: part, offset: 0 }] }],
      };
      button.setAttribute("manifest", setBlob("manifest", JSON.stringify(manifest), "application/json"));
      $("genstate").textContent = `Ready: ${stem}, ${Math.round(fullImg.length / 1024)} KB, with your setup built in.`;
      busy(false);
    } catch (err) {
      if (seq !== genSeq) return;
      // An image without the config block (older release): plain firmware, then send the setup
      button.setAttribute("manifest", "manifest.json");
      $("genstate").textContent = `Couldn't build it in (${err.message}). This installs the plain firmware; send the setup afterwards (below).`;
    }
  }, 250);
}

function download(kind, name) {
  if (!blobs[kind]) return;
  const a = document.createElement("a");
  a.href = blobs[kind];
  a.download = name;
  a.click();
}

// Install preferences kept across boards: "p4rev" (ESP32-P4 chip revision "1" = v1.x or "3")
// and "core" (Arduino core "3" or "2")
function pref(key, fallback) {
  try {
    return localStorage.getItem(STORE + "." + key) || fallback;
  } catch {
    return fallback;
  }
}

function onChange(e) {
  const t = e.target;
  if (t.dataset.show) {
    const s = sel.sensors.find((x) => x.id === t.dataset.show);
    s.show = { ...s.show, [t.dataset.key]: t.value };
    renderResult();
    return;
  }
  if (t.dataset.web || t.dataset.section || ["webtheme", "webstyle", "webadmin"].includes(t.name)) {
    const w = (sel.web = { ...sel.web });
    if (t.dataset.web) w[t.dataset.web] = t.value;
    else if (t.dataset.section) {
      const hide = new Set(w.hide || []);
      t.checked ? hide.delete(t.dataset.section) : hide.add(t.dataset.section);
      w.hide = SECTIONS.map(([k]) => k).filter((k) => hide.has(k));
    } else w[t.name.slice(3)] = t.value;
    renderResult();
    return;
  }
  if (t.name === "p4rev" || t.name === "core") {
    try {
      localStorage.setItem(STORE + "." + t.name, t.value);
    } catch {}
    renderResult();
    return;
  }
  if (t.name === "board") {
    const { sensors, web } = sel;  // the parts and the web page carry over; storage goes back to the board's default
    sel = defaultSelection(catalog, t.value);
    sel.sensors = sensors;
    if (web) sel.web = web;
  } else if (t.name === "network") sel.network = t.value;
  else if (t.name === "storage") sel.storage = t.value;
  else if (t.name === "display") {
    sel.display = t.value;
    justAdded = t.value !== "none" && t.value !== "builtin" ? t.value : null;
  }
  else if (t.name === "layout") sel.layout = t.value;
  else if (t.name === "camera") {
    sel.camera = t.value;
    justAdded = t.value !== "none" ? t.value : null;
  }
  else if (t.name === "sensor") {
    if (t.checked) {
      sel.sensors.push({ id: t.value });
      justAdded = t.value;
    } else {
      sel.sensors = sel.sensors.filter((s) => s.id !== t.value);
      justAdded = null;
    }
  } else if (t.dataset.group) {
    const s = sel.sensors.find((x) => x.id === t.dataset.sensor);
    s.groups = { ...s.groups, [t.dataset.group]: t.checked };
  } else if (t.dataset.option) {
    const s = sel.sensors.find((x) => x.id === t.dataset.sensor);
    s.options = { ...s.options, [t.dataset.option]: t.value };
    renderResult();  // keep focus in the number field
    return;
  } else return;
  renderChoices();
  renderResult();
}

async function onSend() {
  const log = $("sendlog");
  log.textContent = "";
  const say = (s) => {
    log.textContent += s + "\n";
    log.scrollTop = log.scrollHeight;
  };
  $("send").disabled = true;
  try {
    say("Choose the board's port if asked.");
    const r = await sendConfig(result.config, say);
    say(r.ok ? `Done (config ${r.detail}). The board restarts with the new setup.` : `Not saved: ${r.detail}`);
    $("sendstate").textContent = r.ok ? "Sent ✓" : "Failed";
  } catch (err) {
    say(err.name === "NotFoundError" ? "No port chosen." : `Couldn't use the port: ${err.message}. Close other programs (or the installer's console) that have it open, then try again.`);
  } finally {
    $("send").disabled = !!result.errors.length;
  }
}

function downloadSvg() {
  // the download shows everything equally, never the highlight
  const board = findById(catalog.boards, sel.board);
  const blob = new Blob([renderWiring(board, result.parts, { title: board.name })], { type: "image/svg+xml" });
  const a = document.createElement("a");
  a.href = URL.createObjectURL(blob);
  a.download = `presencecam-wiring-${sel.board}.svg`;
  a.click();
  setTimeout(() => URL.revokeObjectURL(a.href), 1000);
}

export async function start() {
  catalog = await (await fetch("catalog.json")).json();
  siteVersion = (await (await fetch("manifest.json")).json()).version;
  sel = restore();
  renderChoices();
  renderResult();
  $("configurator").addEventListener("change", onChange);
  $("p4rev").addEventListener("change", onChange);
  $("corepick").addEventListener("change", onChange);
  $("send").addEventListener("click", onSend);
  for (const id of ["showsec", "websec"]) $(id).addEventListener("change", onChange);
  $("webaccentreset").addEventListener("click", () => {
    sel.web = { ...sel.web };
    delete sel.web.accent;
    renderChoices();
    renderResult();
  });
  $("dlota").addEventListener("click", () => download("ota", `presencecam-${sel.board}-ota.bin`));
  $("dlfull").addEventListener("click", () => download("full", `presencecam-${sel.board}.bin`));
  $("dlsvg").addEventListener("click", downloadSvg);
  $("wiring").addEventListener("click", () => {
    if (!justAdded) return;
    justAdded = null;
    renderResult();
  });
  $("copycfg").addEventListener("click", () => navigator.clipboard?.writeText($("config").textContent));
  if (!serialSupported()) $("sendstate").textContent = "Needs Chrome or Edge on a computer.";
}
