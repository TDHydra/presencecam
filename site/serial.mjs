// Sends the hardware config to a running board over Web Serial: "config set {...}" on the
// firmware's console, then waits for "config ok <hash>" or "config error <why>".

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

export const serialSupported = () => "serial" in navigator;

async function pickPort() {
  // A port this page already used (e.g. by the installer) can be reopened without asking
  const known = await navigator.serial.getPorts();
  if (known.length === 1) return known[0];
  return navigator.serial.requestPort();
}

// Line reader that never drops data: a read that outlives one timeout is picked up by the
// next call instead of being thrown away.
function lines(reader) {
  const dec = new TextDecoder();
  let buf = "";
  let pending = null;
  // Reads until match(line) returns something or ms pass.
  return async function readUntil(match, ms, onLine) {
    const deadline = Date.now() + ms;
    while (Date.now() < deadline) {
      pending ??= reader.read();
      const r = await Promise.race([pending, sleep(Math.max(1, deadline - Date.now())).then(() => null)]);
      if (r === null) break;
      pending = null;
      if (r.done) break;
      buf += dec.decode(r.value, { stream: true });
      let nl;
      while ((nl = buf.indexOf("\n")) >= 0) {
        const line = buf.slice(0, nl).replace(/\r$/, "");
        buf = buf.slice(nl + 1);
        onLine?.(line);
        const m = match(line);
        if (m) return m;
      }
    }
    return null;
  };
}

export async function sendConfig(config, log = () => {}) {
  const port = await pickPort();
  await port.open({ baudRate: 115200 });
  const writer = port.writable.getWriter();
  const reader = port.readable.getReader();
  const readUntil = lines(reader);
  try {
    // Release DTR/RTS so a board with auto-reset wiring runs normally (it may restart once)
    try {
      await port.setSignals({ dataTerminalReady: false, requestToSend: false });
    } catch {}
    const enc = new TextEncoder();
    const line = "config set " + JSON.stringify(config) + "\n";
    const match = (l) => {
      const m = l.match(/^config (ok|error)\s*(.*)$/);
      return m && { ok: m[1] === "ok", detail: m[2] };
    };
    for (let attempt = 1; attempt <= 3; attempt++) {
      log(attempt === 1 ? "Waiting for the board…" : "No answer yet, trying again…");
      // Let a board that just restarted finish booting; show what it prints meanwhile
      await readUntil(() => null, attempt === 1 ? 1500 : 2500, (l) => l.trim() && log("board: " + l));
      await writer.write(enc.encode("\n"));
      for (let i = 0; i < line.length; i += 64) {
        await writer.write(enc.encode(line.slice(i, i + 64)));
        await sleep(8);
      }
      const r = await readUntil(match, 4000, (l) => l.startsWith("config") || l.startsWith("hw:") ? log("board: " + l) : null);
      if (r) return r;
    }
    return { ok: false, detail: "the board didn't answer. Is the PresenceCam firmware installed and the port right?" };
  } finally {
    await reader.cancel().catch(() => {});
    reader.releaseLock();
    writer.releaseLock();
    await port.close().catch(() => {});
  }
}
