// Draws the wiring diagram for an allocation as an SVG string: the board in the middle with
// its header pins, the parts on either side, one coloured wire per connection. No DOM needed.

const PITCH = 24;       // header pin spacing
const BOARD_W = 170;
const PART_W = 150;
const LANE = 9;         // gap between parallel wires
const COLORS = {
  "3v3": "#d92d20", "5v": "#f79009", gnd: "#344054",
  sda: "#2563eb", scl: "#2563eb", uart_rx: "#7a5af8", uart_tx: "#7a5af8", digital_in: "#12a150", digital_out: "#12a150",
  spi_sck: "#0e7490", spi_mosi: "#0e7490", spi_miso: "#0e7490", spi_cs: "#0e7490",
};
const DASHED = new Set(["scl", "uart_tx", "spi_miso", "digital_out"]);
const LEGEND = [
  ["3v3", "3.3 V"], ["5v", "5 V"], ["gnd", "GND"], ["sda", "I²C (SDA solid, SCL dashed)"],
  ["uart_rx", "UART, crossed (TX→RX, RX→TX)"], ["digital_in", "signal (dashed: from the board)"], ["spi_sck", "SPI"],
];

const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);

function wireColor(pin) {
  return COLORS[pin.to.power || pin.role] || "#12a150";
}

// `highlight` is a part id: its wires are drawn at full strength and everything else fades,
// so the part just added stands out.
export function renderWiring(board, parts, { title = "Wiring", highlight = null } = {}) {
  const faded = (partId) => highlight && partId !== highlight;
  const left = board.headers.left || [];
  const right = board.headers.right || [];
  const rows = Math.max(left.length, right.length);

  // Which header pin each wire lands on. Power wires use the nearest pin of that kind.
  const headerPins = [
    ...left.map((p, i) => ({ ...p, side: "left", index: i })),
    ...right.map((p, i) => ({ ...p, side: "right", index: i })),
  ];
  const target = (pin, preferSide) => {
    if (pin.to.power) {
      const all = headerPins.filter((h) => h.power === pin.to.power);
      return all.find((h) => h.side === preferSide) || all[0];
    }
    return headerPins.find((h) => h.gpio === pin.to.gpio);
  };

  // Put each part on the side most of its pins land on (power pins count for the side
  // that has that kind of pin).
  const placed = parts.map((part) => {
    let l = 0, r = 0;
    for (const pin of part.pins) {
      if (pin.to.power) {
        const sides = new Set(headerPins.filter((h) => h.power === pin.to.power).map((h) => h.side));
        if (sides.size === 1) sides.has("left") ? l++ : r++;
        continue;
      }
      const t = target(pin);
      if (t?.side === "left") l++;
      else if (t) r++;
    }
    return { part, side: l > r ? "left" : "right" };
  });

  // Wires that cross to the far header go over the top of the board
  const wires = [];
  for (const p of placed)
    for (const [i, pin] of p.part.pins.entries()) wires.push({ p, i, pin, t: target(pin, p.side) });
  const crossing = wires.filter((w) => w.t && w.t.side !== w.p.side);
  const laneCount = { left: 0, right: 0 };
  for (const w of wires) if (w.t) laneCount[w.p.side]++;
  for (const w of crossing) laneCount[w.t.side]++;
  const laneW = (s) => 30 + laneCount[s] * LANE;

  const top = 56 + crossing.length * LANE;
  const boardX = 20 + PART_W + laneW("left");
  const boardY = top;
  const headerY0 = boardY + 70;
  const boardH = 70 + rows * PITCH + 40;
  const width = boardX + BOARD_W + laneW("right") + PART_W + 20;

  // Part boxes, stacked per side
  const partH = (part) => 30 + part.pins.length * PITCH;
  const stackY = { left: boardY, right: boardY };
  for (const p of placed) {
    p.y = stackY[p.side];
    p.x = p.side === "left" ? 20 : width - 20 - PART_W;
    stackY[p.side] += partH(p.part) + 24;
  }
  const height = Math.max(boardY + boardH, stackY.left, stackY.right) + 30 + 26 * 2;

  const pinY = (h) => headerY0 + h.index * PITCH;
  const pinX = (h) => (h.side === "left" ? boardX : boardX + BOARD_W);
  const usedHeader = new Set(wires.filter((w) => w.t).map((w) => w.t.side + w.t.index));

  const out = [];
  out.push(`<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${width} ${height}" width="${width}" height="${height}" font-family="system-ui, Segoe UI, Roboto, Helvetica, Arial, sans-serif" font-size="12">`);
  out.push(`<title>${esc(title)}</title>`);
  out.push(`<rect width="${width}" height="${height}" rx="12" fill="#ffffff"/>`);
  out.push(`<text x="20" y="30" font-size="16" font-weight="700" fill="#1d2330">${esc(title)}</text>`);

  // Board
  out.push(`<rect x="${boardX}" y="${boardY}" width="${BOARD_W}" height="${boardH}" rx="10" fill="#1d2939"/>`);
  out.push(`<text x="${boardX + BOARD_W / 2}" y="${boardY + 26}" text-anchor="middle" fill="#fff" font-weight="600">${esc(board.short || board.name)}</text>`);
  out.push(`<text x="${boardX + BOARD_W / 2}" y="${boardY + 44}" text-anchor="middle" fill="#d0d5dd" font-size="11">${board.positionsVerified ? "top view, USB at the bottom" : "schematic: go by GPIO labels"}</text>`);
  out.push(`<rect x="${boardX + BOARD_W / 2 - 20}" y="${boardY + boardH - 12}" width="40" height="18" rx="3" fill="#d0d5dd"/>`);
  for (const h of headerPins) {
    const used = usedHeader.has(h.side + h.index);
    const x = pinX(h), y = pinY(h);
    const tx = h.side === "left" ? x + 10 : x - 10;
    out.push(`<text x="${tx}" y="${y + 4}" text-anchor="${h.side === "left" ? "start" : "end"}" fill="${used ? "#ffffff" : "#8b93a3"}" font-family="ui-monospace, Menlo, Consolas, monospace" font-size="11"${used ? ' font-weight="700"' : ""}>${esc(h.label)}</text>`);
    out.push(`<circle cx="${x}" cy="${y}" r="${used ? 4 : 2.5}" fill="${used ? "#fcd34d" : "#667085"}"/>`);
  }

  // Wires first, so the boxes and dots sit on top. Each side's same-side wires get lanes in
  // the order that crosses least (greedy insertion), the over-the-top wires go outside them.
  const srcY = (w) => w.p.y + 30 + w.i * PITCH;
  const crosses = (inner, outer) => {
    const within = (v, a, b) => v > Math.min(a, b) && v < Math.max(a, b);
    const ti = pinY(inner.t), to = pinY(outer.t);
    return within(srcY(inner), srcY(outer), to) || within(to, srcY(inner), ti);
  };
  const cost = (order) => {
    let n = 0;
    for (let i = 0; i < order.length; i++) for (let j = i + 1; j < order.length; j++) n += crosses(order[i], order[j]);
    return n;
  };
  const laneOf = new Map();
  for (const side of ["left", "right"]) {
    let order = [];
    for (const w of wires.filter((w) => w.t && w.p.side === side && w.t.side === side)) {
      let best = null;
      for (let k = 0; k <= order.length; k++) {
        const cand = [...order.slice(0, k), w, ...order.slice(k)];
        const c = cost(cand);
        if (!best || c < best.c) best = { c, cand };
      }
      order = best.cand;
    }
    order.forEach((w, k) => laneOf.set(w, k));
  }
  const lane = {
    left: wires.filter((w) => w.t && w.p.side === "left" && w.t.side === "left").length,
    right: wires.filter((w) => w.t && w.p.side === "right" && w.t.side === "right").length,
  };
  const laneX = (side, k) => (side === "left" ? boardX - 18 - k * LANE : boardX + BOARD_W + 18 + k * LANE);
  let over = 0;
  for (const w of wires) {
    if (!w.t) continue;
    const side = w.p.side;
    const sx = side === "left" ? w.p.x + PART_W : w.p.x;
    const sy = srcY(w);
    const tx = pinX(w.t), ty = pinY(w.t);
    let d;
    if (w.t.side === side) {
      d = `M${sx} ${sy} H${laneX(side, laneOf.get(w))} V${ty} H${tx}`;
    } else {
      const overY = boardY - 14 - over++ * LANE;
      d = `M${sx} ${sy} H${laneX(side, lane[side]++)} V${overY} H${laneX(w.t.side, lane[w.t.side]++)} V${ty} H${tx}`;
    }
    const dash = DASHED.has(w.pin.role) ? ' stroke-dasharray="7 4"' : "";
    const fade = faded(w.p.part.id) ? ' opacity="0.22"' : (highlight ? ' stroke-width="4"' : "");
    out.push(`<path d="${d}" fill="none" stroke="${wireColor(w.pin)}" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"${dash}${fade}/>`);
  }

  // Parts
  for (const p of placed) {
    const { part } = p;
    const hot = highlight && part.id === highlight;
    if (faded(part.id)) out.push(`<g opacity="0.35">`);
    out.push(`<rect x="${p.x}" y="${p.y}" width="${PART_W}" height="${partH(part)}" rx="8" fill="#14532d"${hot ? ' stroke="#fcd34d" stroke-width="3"' : ""}/>`);
    if (hot) out.push(`<text x="${p.x + PART_W / 2}" y="${p.y - 6}" text-anchor="middle" fill="#b45309" font-size="11" font-weight="700">just added: wire this</text>`);
    out.push(`<text x="${p.x + PART_W / 2}" y="${p.y + 18}" text-anchor="middle" fill="#fff" font-weight="600" font-size="11">${esc(part.name.length > 22 ? part.name.slice(0, 21) + "…" : part.name)}</text>`);
    part.pins.forEach((pin, i) => {
      const y = p.y + 30 + i * PITCH;
      const x = p.side === "left" ? p.x + PART_W : p.x;
      const tx = p.side === "left" ? x - 10 : x + 10;
      out.push(`<text x="${tx}" y="${y + 4}" text-anchor="${p.side === "left" ? "end" : "start"}" fill="#fff" font-family="ui-monospace, Menlo, Consolas, monospace" font-size="11">${esc(pin.label)}</text>`);
      out.push(`<circle cx="${x}" cy="${y}" r="4" fill="#fcd34d"/>`);
    });
    if (faded(part.id)) out.push(`</g>`);
  }

  // Legend
  let lx = 20;
  let ly = height - 52;
  for (const [role, text] of LEGEND) {
    const w = 40 + text.length * 6;
    if (lx + w > width - 10) {
      lx = 20;
      ly += 22;
    }
    out.push(`<line x1="${lx}" y1="${ly}" x2="${lx + 22}" y2="${ly}" stroke="${COLORS[role]}" stroke-width="3" stroke-linecap="round"/>`);
    out.push(`<text x="${lx + 28}" y="${ly + 4}" fill="#1d2330" font-size="11">${esc(text)}</text>`);
    lx += w;
  }
  out.push("</svg>");
  return out.join("\n");
}
