// The flasher page's configurator: board → display → sensors → layout, then the wiring
// diagram, pin table, install button and "send setup to board".
import { allocate, defaultSelection, findById } from "./allocator.mjs";
import { sendConfig, serialSupported } from "./serial.mjs";
import { renderWiring } from "./wiring.mjs";

const $ = (id) => document.getElementById(id);
const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);
const STORE = "presencecam.selection.v1";

let catalog;
let sel;
let result;

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

  $("layouts").innerHTML = catalog.layouts
    .map((l) => choice("layout", l.id, l.id === sel.layout, l.name, l.description))
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
    $("wiring").innerHTML = renderWiring(board, result.parts, { title: board.name });
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
  for (const r of document.getElementsByName("p4rev")) r.checked = r.value === p4Rev();
  document.querySelector("esp-web-install-button").setAttribute("manifest", p4 && p4Rev() === "3" ? "manifest-p4r3.json" : "manifest.json");
  $("wifinote").textContent = p4
    ? "On a new board, let it erase the device when asked. This board has no Wi-Fi: plug in a network cable and it gets an address by DHCP."
    : "On a new board, let it erase the device when asked. When it finishes it asks for your Wi-Fi name and password and sends them over the cable. Then close the installer window.";
  $("send").disabled = !!result.errors.length || !serialSupported();
}

// Which ESP32-P4 chip revision to install for: "1" (v1.x) or "3"
function p4Rev() {
  try {
    return localStorage.getItem(STORE + ".p4rev") || "1";
  } catch {
    return "1";
  }
}

function onChange(e) {
  const t = e.target;
  if (t.name === "p4rev") {
    try {
      localStorage.setItem(STORE + ".p4rev", t.value);
    } catch {}
    renderResult();
    return;
  }
  if (t.name === "board") {
    const keep = sel.sensors;
    sel = defaultSelection(catalog, t.value);
    sel.sensors = keep;
  } else if (t.name === "display") sel.display = t.value;
  else if (t.name === "layout") sel.layout = t.value;
  else if (t.name === "sensor") {
    if (t.checked) sel.sensors.push({ id: t.value });
    else sel.sensors = sel.sensors.filter((s) => s.id !== t.value);
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
  const blob = new Blob([$("wiring").innerHTML], { type: "image/svg+xml" });
  const a = document.createElement("a");
  a.href = URL.createObjectURL(blob);
  a.download = `presencecam-wiring-${sel.board}.svg`;
  a.click();
  setTimeout(() => URL.revokeObjectURL(a.href), 1000);
}

export async function start() {
  catalog = await (await fetch("catalog.json")).json();
  sel = restore();
  renderChoices();
  renderResult();
  $("configurator").addEventListener("change", onChange);
  $("p4rev").addEventListener("change", onChange);
  $("send").addEventListener("click", onSend);
  $("dlsvg").addEventListener("click", downloadSvg);
  $("copycfg").addEventListener("click", () => navigator.clipboard?.writeText($("config").textContent));
  if (!serialSupported()) $("sendstate").textContent = "Needs Chrome or Edge on a computer.";
  // When the installer dialog closes, point at the next step
  let installerOpen = false;
  new MutationObserver(() => {
    const open = !!document.querySelector("ewt-install-dialog");
    if (installerOpen && !open) $("step-send").classList.add("next");
    installerOpen = open;
  }).observe(document.body, { childList: true });
}
