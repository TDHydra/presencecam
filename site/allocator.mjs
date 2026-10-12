// Picks a GPIO for every wire, from the catalog and the user's choices, and builds the
// hardware config the firmware reads (see src/hwconfig.h). Pure functions, no DOM, so
// it runs in the browser and under `node --test`.
//
// selection = {
//   board: "esp32-devkit-30",
//   display: "ssd1306",                 // a display id from board.displays
//   layout: "basic",
//   sensors: [{id: "ld2410", groups: {uart: true, out: true}, options: {}}, {id: "vl53l0x", options: {presenceMm: 0}}],
//   network: "eth" | "wifi",           // only for boards with both (see hasWifi); Ethernet by default
//   camera: "arducam_mega" | "none",   // a camera id from catalog.cameras offered for this board
//   (each sensor may also have show: {"Distance": "web" | "screen" | "both" | "off", ...})
//   web: {title, theme: "auto"|"dark"|"light", accent: "#rrggbb", style: "cards"|"list"|"big",
//         hide: ["log", "rules", "photos", "hardware", "health", "debug"], admin: "open"|"collapsed"},
// }
//
// Returns { config, parts, errors, warnings }:
//   parts: [{ id, name, pins: [{ label, role, to: {gpio, label} | {power, label} }] }] for the diagram and table.

export function findById(list, id) {
  return list.find((x) => x.id === id);
}

// Every header pin by GPIO number and every power pin by kind, with the board label.
function headerIndex(board) {
  const byGpio = new Map();
  const power = { "3v3": [], "5v": [], gnd: [] };
  for (const side of ["left", "right"]) {
    (board.headers[side] || []).forEach((p, i) => {
      const at = { side, index: i, label: p.label };
      if (p.gpio !== undefined) byGpio.set(p.gpio, at);
      if (p.power) power[p.power].push(at);
    });
  }
  return { byGpio, power };
}

// Whether a board can use Wi-Fi: every chip has it built in except the ESP32-P4, which needs a
// co-processor (ESP32-C6 over ESP-Hosted).
export function hasWifi(board) {
  return board.chip !== "ESP32-P4" || !!board.wifiHosted;
}

// Cameras offered for a board: built-in ones list their boards, wired ones their chips
export function camerasFor(catalog, board) {
  return (catalog.cameras || []).filter((c) => (c.bus === "builtin" ? (c.boards || []).includes(board.id) : (c.chips || []).includes(board.chip)));
}

export function defaultSelection(catalog, boardId) {
  const board = findById(catalog.boards, boardId) || catalog.boards[0];
  return {
    board: board.id,
    display: board.displays[0],
    layout: catalog.layouts[0].id,
    sensors: [],
  };
}

export function allocate(catalog, sel) {
  const errors = [];
  const warnings = [];
  const board = findById(catalog.boards, sel.board);
  if (!board) return { config: null, parts: [], errors: [`Unknown board "${sel.board}"`], warnings };
  const idx = headerIndex(board);

  // GPIO -> what uses it. Board-reserved pins start taken.
  const used = new Map();
  for (const [g, why] of Object.entries(board.reserved || {})) used.set(Number(g), why);
  const forbidden = new Map(Object.entries(board.forbidden || {}).map(([g, why]) => [Number(g), why]));

  const claim = (gpio, what) => {
    if (forbidden.has(gpio)) {
      errors.push(`${what}: GPIO${gpio} can't be used (${forbidden.get(gpio)})`);
      return false;
    }
    if (used.has(gpio)) {
      errors.push(`${what}: GPIO${gpio} is already used by ${used.get(gpio)}`);
      return false;
    }
    if (!idx.byGpio.has(gpio)) warnings.push(`${what}: GPIO${gpio} isn't on this board's headers`);
    used.set(gpio, what);
    return true;
  };

  const display = findById(catalog.displays, sel.display) || findById(catalog.displays, "none");
  if (!board.displays.includes(display.id)) errors.push(`${display.name} isn't offered for ${board.name}`);
  const sensors = (sel.sensors || [])
    .map((s) => ({ sel: s, def: findById(catalog.sensors, s.id) }))
    .filter((s) => {
      if (!s.def) errors.push(`Unknown sensor "${s.sel.id}"`);
      return s.def;
    });

  // I2C: one shared pair for every I2C part
  const needI2c = display.bus === "i2c" || sensors.some((s) => s.def.bus === "i2c");
  let i2c = null;
  if (needI2c) {
    i2c = { ...board.i2c };
    claim(i2c.sda, "I²C SDA");
    claim(i2c.scl, "I²C SCL");
  }

  const config = {
    v: 1,
    board: board.id,
    chip: board.chip,
    led: board.led ?? -1,
    ...(board.ledType && board.ledType !== "gpio" ? { ledType: board.ledType } : {}),
    button: board.button ?? 0,
    ...(board.bootOutputs?.length ? { outputs: board.bootOutputs.map(({ gpio, level }) => ({ gpio, level })) } : {}),
    ...(board.ethernet && (sel.network !== "wifi" || !hasWifi(board)) ? { eth: { ...board.ethernet } } : {}),
    ...(board.wifiHosted ? { wifiHosted: { ...board.wifiHosted } } : {}),
    i2c: i2c || { sda: -1, scl: -1 },
    display: { type: "none" },
    layout: findById(catalog.layouts, sel.layout) ? sel.layout : catalog.layouts[0].id,
    sensors: [],
  };

  const parts = [];
  const powerTo = (kind) => {
    const p = idx.power[kind][0];
    if (!p) {
      errors.push(`${board.name} has no ${kind.toUpperCase()} pin on its headers`);
      return { power: kind, label: kind.toUpperCase() };
    }
    return { power: kind, label: p.label };
  };
  const gpioTo = (gpio) => ({ gpio, label: idx.byGpio.get(gpio)?.label ?? `GPIO${gpio}` });

  // Wires every module pin by its role. `pinsFor` gives the GPIO for signal roles.
  const wireModule = (def, name, pinsFor, enabled = () => true) => {
    const pins = [];
    for (const p of def.pins) {
      if (p.group && !enabled(p.group)) continue;
      let to;
      if (p.role === "vcc") to = powerTo(def.supply || "3v3");
      else if (p.role === "gnd") to = powerTo("gnd");
      else if (p.role === "sda") to = gpioTo(i2c.sda);
      else if (p.role === "scl") to = gpioTo(i2c.scl);
      else to = gpioTo(pinsFor[p.key]);
      pins.push({ label: p.label, role: p.role, to });
    }
    parts.push({ id: def.id, name, pins });
  };

  // Display
  if (display.bus === "builtin") {
    if (!board.builtinDisplay) errors.push(`${board.name} has no built-in screen`);
    else config.display = { ...board.builtinDisplay.config };
  } else if (display.bus === "i2c") {
    config.display = { ...display.config };
    wireModule(display, display.short || display.name, {});
  }

  // Sensors, in the order chosen
  let uartsLeft = [...(board.uarts || [])];
  const nextDigital = (what) => {
    for (const g of board.digitalPrefer || []) {
      if (!used.has(g) && !forbidden.has(g)) {
        used.set(g, what);
        return g;
      }
    }
    errors.push(`${what}: no free GPIO left for it on ${board.name}`);
    return -1;
  };

  const seenType = {};
  sensors.forEach(({ sel: s, def }) => {
    // number them only when the same sensor is fitted more than once
    seenType[def.id] = (seenType[def.id] || 0) + 1;
    const dupes = sensors.filter((x) => x.def.id === def.id).length > 1;
    const what = `${def.short || def.name}${dupes ? " #" + seenType[def.id] : ""}`;
    const groups = {};
    for (const g of def.groups || []) groups[g.id] = s.groups?.[g.id] ?? g.default;
    const cfg = { type: def.id };
    for (const o of def.options || []) cfg[o.key] = Number(s.options?.[o.key] ?? o.default);
    const pinsFor = {};

    if (def.bus === "gpio") {
      for (const p of def.pins)
        if (p.role === "digital_in" || p.role === "digital_out") {
          const g = nextDigital(`${what} ${p.label}`);
          cfg[p.key] = g;
          pinsFor[p.key] = g;
        }
    } else if (def.bus === "uart") {
      if (groups.uart !== false) {
        const u = uartsLeft.find((u) => !used.has(u.rx) && !used.has(u.tx));
        if (!u) {
          errors.push(`${what}: ${board.name} has no free hardware UART left`);
        } else {
          uartsLeft = uartsLeft.filter((x) => x !== u);
          claim(u.rx, `${what} TX`);
          claim(u.tx, `${what} RX`);
          Object.assign(cfg, { uart: u.num, rx: u.rx, tx: u.tx });
          Object.assign(pinsFor, { rx: u.rx, tx: u.tx });
        }
      } else {
        Object.assign(cfg, { rx: -1, tx: -1 });
      }
      if (groups.out !== false) {
        const g = nextDigital(`${what} OUT`);
        cfg.out = g;
        pinsFor.out = g;
      } else {
        cfg.out = -1;
      }
      if (groups.uart === false && groups.out === false) errors.push(`${what}: wire TX/RX, OUT, or both`);
    }
    // Where each reading shows; "both" is the firmware's default, so only the others are stored
    const show = Object.fromEntries(Object.entries(s.show || {}).filter(([k, w]) => w !== "both" && (def.readings || []).some((r) => r.key === k)));
    if (Object.keys(show).length) cfg.show = show;
    config.sensors.push(cfg);
    wireModule(def, what, pinsFor, (g) => groups[g] !== false);
  });

  // Two I2C parts at one address can't share the bus
  const i2cAt = new Map();
  const i2cParts = [
    ...(display.bus === "i2c" ? [{ name: display.short || display.name, addr: display.config?.addr }] : []),
    ...sensors.filter((s) => s.def.bus === "i2c").map((s) => ({ name: s.def.short || s.def.name, addr: parseInt(s.def.address) })),
  ];
  for (const { name, addr } of i2cParts) {
    if (!addr) continue;
    if (i2cAt.has(addr)) errors.push(`${i2cAt.get(addr)} and ${name} both use I²C address 0x${addr.toString(16)}; fit only one of them`);
    else i2cAt.set(addr, name);
  }

  // Camera
  const camera = sel.camera && sel.camera !== "none" ? findById(catalog.cameras || [], sel.camera) : null;
  if (camera) {
    if (!camerasFor(catalog, board).some((c) => c.id === camera.id)) errors.push(`${camera.name} isn't offered for ${board.name}`);
    config.camera = { ...camera.config };
    if (camera.bus === "spi") {
      // The board's SPI pins if it names them, otherwise any free pins
      const spi = board.spi || {};
      const pinsFor = {};
      for (const p of camera.pins) {
        if (!p.key) continue;
        const want = spi[p.key];
        const g = want !== undefined && !used.has(want) && !forbidden.has(want) ? (used.set(want, `${camera.short || camera.name} ${p.label}`), want) : nextDigital(`${camera.short || camera.name} ${p.label}`);
        config.camera[p.key] = g;
        pinsFor[p.key] = g;
      }
      wireModule(camera, camera.short || camera.name, pinsFor);
    } else if (camera.bus === "i2c") {
      if (!i2c) {
        i2c = { ...board.i2c };
        claim(i2c.sda, "I²C SDA");
        claim(i2c.scl, "I²C SCL");
        config.i2c = i2c;
      }
      config.camera.sda = i2c.sda;
      config.camera.scl = i2c.scl;
    }
  }

  // The device's web page: only what differs from the firmware's defaults
  const web = {};
  const w = sel.web || {};
  if (w.title?.trim()) web.title = w.title.trim().slice(0, 40);
  if (w.theme && w.theme !== "auto") web.theme = w.theme;
  if (/^#[0-9a-f]{6}$/i.test(w.accent || "")) web.accent = w.accent.toLowerCase();
  if (w.style && w.style !== "cards") web.style = w.style;
  if (w.hide?.length) web.hide = [...w.hide];
  if (w.admin === "collapsed") web.admin = "collapsed";
  if (Object.keys(web).length) config.web = web;

  // Power checks
  const fiveVolt = parts.filter((p) => p.pins.some((x) => x.to.power === "5v"));
  if (fiveVolt.length && !board.positionsVerified)
    warnings.push(`${fiveVolt.map((p) => p.name).join(", ")} need${fiveVolt.length === 1 ? "s" : ""} 5 V: check your board has a 5V pin before wiring it.`);
  if (!board.positionsVerified)
    warnings.push("Pin positions for this board are drawn as a schematic: wire by the GPIO numbers printed on the board.");

  return { config, parts, errors, warnings };
}

// Every GPIO the config uses, for tests and the pin table.
export function configPins(config) {
  const out = [];
  const add = (what, g) => g >= 0 && out.push({ what, gpio: g });
  add("status LED", config.led);
  add("button", config.button);
  for (const o of config.outputs || []) add("boot output", o.gpio);
  if (config.wifiHosted) for (const k of ["clk", "cmd", "d0", "d1", "d2", "d3", "rst"]) add("Wi-Fi co-processor " + k, config.wifiHosted[k] ?? -1);
  if (config.camera) for (const k of ["cs", "sck", "mosi", "miso"]) if (config.camera[k] !== undefined) add("camera " + k, config.camera[k]);
  if (config.eth) for (const k of ["sclk", "mosi", "miso", "cs", "int", "rst"]) add("ethernet " + k, config.eth[k] ?? -1);
  add("I2C SDA", config.i2c.sda);
  add("I2C SCL", config.i2c.scl);
  if (config.display.type === "st7789") for (const k of ["mosi", "sclk", "cs", "dc", "rst", "bl"]) add("display " + k, config.display[k]);
  config.sensors.forEach((s, i) => {
    for (const k of ["rx", "tx", "out", "trig", "echo"]) if (s[k] !== undefined) add(`${s.type} #${i + 1} ${k}`, s[k]);
  });
  return out;
}
