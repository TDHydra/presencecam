#include "display.h"

#include <lgfx/utility/lgfx_qrcode.h>

#include "config.h"
#include "eventlog.h"
#include "gfx.h"
#include "hwconfig.h"
#include "layouts/layout.h"
#include "net.h"
#include "power.h"
#include "presence.h"
#include "sensors/sensor.h"
#include "settings.h"

static Gfx* gfx = nullptr;
static String state = "none";  // what describe() says when there's no display
static const Layout* layout = &BASIC_LAYOUT;
static int page = 0;
static uint32_t lastDraw = 0;
static bool dimmed = false, asleep = false;
static uint32_t wokeAt = 0;

static String msg1, msg2;
static uint32_t msgUntil = 0, infoUntil = 0;
static uint32_t holdMs = 0, holdNextAt = 0;
static const char *holdNow = nullptr, *holdNext = nullptr;

static const Layout* layoutFind(const String& id) {
  if (id == "basic") return &BASIC_LAYOUT;
  return &BASIC_LAYOUT;
}

bool displayFound() { return gfx != nullptr; }
String displayDescribe() { return gfx ? gfx->describe() : state; }

void displayBegin() {
  JsonVariantConst d = hw["display"];
  String type = d["type"] | "none";
  layout = layoutFind(hw["layout"] | "basic");
  Gfx* g = nullptr;
  if (type == "ssd1306" || type == "sh1106") g = makeOledGfx();
  else if (type == "st7789") g = makeLcdGfx();
  if (!g) {
    state = "none";
    return;
  }
  if (!g->begin(d)) {
    delete g;
    state = type + " not found";
    Serial.println("display: " + state + " (check wiring and the i2c pins)");
    return;
  }
  gfx = g;
  gfx->setFlip(settings.screenFlip);
  Serial.println("display: " + gfx->describe() + ", layout " + layout->id);
}

static int totalPages() { return gfx ? layout->pages(*gfx) + 2 : 1; }  // + network + device

void displayNextPage() {
  page = (page + 1) % totalPages();
  lastDraw = 0;
}

void displayToggleDim() {
  dimmed = !dimmed;
  if (gfx) gfx->setDim(dimmed);
}

void displayWake() {
  wokeAt = millis();
  lastDraw = 0;
}

void displayApplyFlip() {
  if (gfx) gfx->setFlip(settings.screenFlip);
  lastDraw = 0;
}

void displayMessage(const String& a, const String& b, uint32_t ms) {
  msg1 = a;
  msg2 = b;
  msgUntil = millis() + ms;
  displayWake();
}

void displayShowInfo(uint32_t ms) {
  infoUntil = millis() + ms;
  displayWake();
}

void displayHoldHint(uint32_t heldMs, const char* now, const char* next, uint32_t nextAt) {
  holdMs = heldMs;
  holdNow = now;
  holdNext = next;
  holdNextAt = nextAt;
  displayWake();
}

void displayHoldHintClear() {
  holdMs = 0;
  lastDraw = 0;
}

// --- shared helpers -------------------------------------------------------------------

String uiDuration(uint32_t s) {
  char b[16];
  if (s < 60) snprintf(b, sizeof b, "%lus", (unsigned long)s);
  else if (s < 3600) snprintf(b, sizeof b, "%lum %02lus", (unsigned long)s / 60, (unsigned long)s % 60);
  else snprintf(b, sizeof b, "%luh %02lum", (unsigned long)s / 3600, (unsigned long)(s / 60) % 60);
  return b;
}

int uiHeader(Gfx& g, const char* title) {
  bool tall = g.height() >= 120;
  g.font(F_SMALL);
  const char* w = netConnected() ? (NET_WIRED ? "LAN" : "WiFi") : (netApActive() ? "AP" : "--");
  int top = tall ? 30 : 13;
  if (tall) g.fillRect(0, 0, g.width(), 24, C_ACCENT);
  g.text(tall ? 4 : 0, tall ? 4 : 0, title);
  g.textRight(g.width() - (tall ? 4 : 0), tall ? 4 : 0, w);
  if (!tall) g.hline(0, 11, g.width(), C_FG);
  // page dots, so you can tell there is more behind the BOOT button
  int n = totalPages();
  if (n > 1 && tall) {
    for (int i = 0; i < n; i++) g.fillRect(g.width() / 2 - n * 4 + i * 8, 26, 4, 2, i == page ? C_FG : C_MUTED);
  }
  return top;
}

// Draws lines from the top; returns the next y.
static int line(Gfx& g, int y, FontSize f, const String& s, Color c = C_FG) {
  g.font(f);
  g.text(g.height() >= 120 ? 4 : 0, y, s.c_str(), c);
  return y + g.lineHeight() + (g.height() >= 120 ? 4 : 1);
}

static void pageNetwork(Gfx& g) {
  int y = uiHeader(g, "Network");
  if (netConnected()) {
    y = line(g, y, F_SMALL, netLink(), C_MUTED);
    y = line(g, y, F_MEDIUM, netIp());
    y = line(g, y, F_SMALL, netHostname() + ".local");
    if (!NET_WIRED) line(g, y, F_SMALL, "signal " + String(netRssi()) + " dBm", C_MUTED);
  } else if (NET_WIRED) {
    y = line(g, y, F_SMALL, "Ethernet", C_MUTED);
    line(g, y, F_SMALL, "plug in a network cable");
  } else if (netApActive()) {
    y = line(g, y, F_SMALL, "Setup: join Wi-Fi", C_MUTED);
    y = line(g, y, F_MEDIUM, netApName());
    y = line(g, y, F_SMALL, "then open", C_MUTED);
    line(g, y, F_SMALL, "http://192.168.4.1");
  } else if (!powerWifiAllowed()) {
    y = line(g, y, F_SMALL, "Wi-Fi off:", C_WARN);
    y = line(g, y, F_SMALL, "USB power too weak", C_WARN);
    line(g, y, F_SMALL, "better cable / 1000uF cap", C_MUTED);
  } else {
    y = line(g, y, F_SMALL, "Connecting to", C_MUTED);
    line(g, y, F_MEDIUM, settings.wifiSsid);
  }
}

static void pageDevice(Gfx& g) {
  int y = uiHeader(g, "Device");
  y = line(g, y, F_SMALL, "fw " FW_VERSION " " FW_CHIP);
  y = line(g, y, F_SMALL, String(hwBoardName()), C_MUTED);
  y = line(g, y, F_SMALL, String(sensorsCount()) + " sensor(s), log " + String(logNextLine() - 1));
  y = line(g, y, F_SMALL, "up " + uiDuration(millis() / 1000));
  if (hwSafeMode) line(g, y, F_SMALL, "SAFE MODE", C_WARN);
}

// --- full-screen overlays -------------------------------------------------------------

// Draws a QR code with its top-left at x,y, at most maxPx square. Returns its size in px.
static int drawQr(Gfx& g, const String& text, int x, int y, int maxPx) {
  static uint8_t buf[256];  // enough for version 6
  QRCode qr;
  for (uint8_t v = 2; v <= 6; v++) {
    if (lgfx_qrcode_getBufferSize(v) > sizeof buf) break;
    if (lgfx_qrcode_initText(&qr, buf, v, ECC_LOW, text.c_str()) != 0) continue;
    int scale = max(1, maxPx / (qr.size + 2));
    int px = (qr.size + 2) * scale;
    g.fillRect(x, y, px, px, C_FG);  // quiet zone is light
    for (int my = 0; my < qr.size; my++)
      for (int mx = 0; mx < qr.size; mx++)
        if (lgfx_qrcode_getModule(&qr, mx, my)) g.fillRect(x + (mx + 1) * scale, y + (my + 1) * scale, scale, scale, C_BG);
    return px;
  }
  return 0;
}

static void drawInfo(Gfx& g) {
  String qr, l1, l2, l3;
  if (netConnected()) {
    qr = netUrl();
    l1 = "Open";
    l2 = netIp();
    l3 = netHostname() + ".local";
  } else if (netApActive()) {
    qr = "WIFI:S:" + netApName() + ";T:nopass;;";
    l1 = "Join Wi-Fi";
    l2 = "PresenceCam-";
    l3 = deviceId();
  } else {
    l1 = NET_WIRED ? "No network yet" : "No Wi-Fi yet";
  }
  if (g.height() >= 120) {
    int px = qr.length() ? drawQr(g, qr, 10, 8, g.width() - 20) : 0;
    int y = px + 18;
    g.font(F_SMALL);
    g.textCenter(y, l1.c_str(), C_MUTED);
    g.font(F_MEDIUM);
    y += g.lineHeight() + 4;
    g.textCenter(y, (l2 + (netApActive() ? l3 : "")).c_str());
    if (!netApActive()) {
      g.font(F_SMALL);
      g.textCenter(y + 24, l3.c_str(), C_MUTED);
    }
  } else {
    // QR on the left, full height; text in the tiny font beside it
    int px = qr.length() ? drawQr(g, qr, 0, 0, 64) : 0;
    int x = px + 3;
    g.font(F_TINY);
    g.text(x, 6, l1.c_str());
    g.font(F_TINY);
    g.text(x, 24, l2.c_str());
    g.text(x, 36, l3.c_str());
  }
}

static void drawHold(Gfx& g) {
  bool tall = g.height() >= 120;
  int y = tall ? 40 : 4;
  g.font(F_SMALL);
  g.textCenter(y, "Release now:", C_MUTED);
  g.font(tall ? F_LARGE : F_MEDIUM);
  y += 14;
  g.textCenter(y, holdNow ? holdNow : "-", C_ACCENT);
  y += g.lineHeight() + (tall ? 20 : 4);
  if (holdNext) {
    g.font(F_SMALL);
    String s = String("Keep holding: ") + holdNext;
    g.textCenter(y, s.c_str(), C_MUTED);
    y += g.lineHeight() + 4;
    float frac = holdNextAt ? (float)holdMs / holdNextAt : 1;
    g.bar(tall ? 10 : 4, y, g.width() - (tall ? 20 : 8), tall ? 10 : 6, frac, C_WARN);
  }
}

static void drawMessage(Gfx& g) {
  bool tall = g.height() >= 120;
  g.font(tall ? F_LARGE : F_MEDIUM);
  int y = g.height() / 2 - g.lineHeight();
  g.textCenter(y, msg1.c_str());
  g.font(F_SMALL);
  g.textCenter(y + (tall ? 40 : 20), msg2.c_str(), C_MUTED);
}

void displayLoop() {
  if (!gfx) return;
  uint32_t now = millis();
  if (now - lastDraw < 200) return;
  lastDraw = now;

  // Optional screen sleep while nobody is around (protects OLEDs from burn-in)
  bool busy = (int32_t)(msgUntil - now) > 0 || (int32_t)(infoUntil - now) > 0 || holdMs;
  bool wantSleep = settings.screenSleepS && !presence().present && !busy &&
                   now - max(presence().sinceMs, wokeAt) > settings.screenSleepS * 1000UL;
  if (wantSleep != asleep) {
    asleep = wantSleep;
    gfx->setPower(!asleep);
  }
  if (asleep) return;

  int pages = totalPages();
  if (page >= pages) page = 0;
  gfx->clear();
  if (holdMs) drawHold(*gfx);
  else if ((int32_t)(msgUntil - now) > 0) drawMessage(*gfx);
  else if ((int32_t)(infoUntil - now) > 0) drawInfo(*gfx);
  else if (page < pages - 2) layout->draw(*gfx, page);
  else if (page == pages - 2) pageNetwork(*gfx);
  else pageDevice(*gfx);
  gfx->flush();
}
