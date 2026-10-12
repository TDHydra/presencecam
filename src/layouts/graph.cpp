// "Graph" layout: one page per numeric reading with its current value and a graph of the
// last few minutes, plus a first page with the presence state and a timeline strip.
// Samples are taken every 2 s into a small ring buffer per reading (120 samples = 4 min).
#include "layout.h"
#include "presence.h"
#include "sensors/sensor.h"
#include "settings.h"

namespace {

const int MAX_SLOTS = 8;
const int SAMPLES = 120;
const uint32_t SAMPLE_MS = 2000;
const int LABEL_LEN = 24;

struct Slot {
  char label[LABEL_LEN];
  float samples[SAMPLES];  // NAN = no data at that time
  int head;                // next write position
  int count;               // valid entries (<= SAMPLES)
  bool used;
};

Slot slots[MAX_SLOTS];
int slotCount = 0;

uint8_t presenceHist[SAMPLES];  // 0 = clear, 1 = present
int presenceHead = 0, presenceCount = 0;

uint32_t lastSampleMs = 0;
bool sampledOnce = false;

// --- ring buffer helpers ---

int slotFor(const char* label) {
  for (int i = 0; i < slotCount; i++)
    if (strncmp(slots[i].label, label, LABEL_LEN - 1) == 0) return i;
  if (slotCount >= MAX_SLOTS) return -1;
  Slot& s = slots[slotCount];
  strncpy(s.label, label, LABEL_LEN - 1);
  s.label[LABEL_LEN - 1] = 0;
  for (int i = 0; i < SAMPLES; i++) s.samples[i] = NAN;
  s.head = 0;
  s.count = 0;
  s.used = true;
  return slotCount++;
}

void push(Slot& s, float v) {
  s.samples[s.head] = v;
  s.head = (s.head + 1) % SAMPLES;
  if (s.count < SAMPLES) s.count++;
}

// age 0 = newest sample
float sampleAt(const Slot& s, int age) {
  int idx = (s.head - 1 - age + 2 * SAMPLES) % SAMPLES;
  return s.samples[idx];
}

uint8_t presenceAt(int age) {
  int idx = (presenceHead - 1 - age + 2 * SAMPLES) % SAMPLES;
  return presenceHist[idx];
}

void sample() {
  uint32_t now = millis();
  if (sampledOnce && now - lastSampleMs < SAMPLE_MS) return;
  sampledOnce = true;
  lastSampleMs = now;
  sensorsForEachShown(true, [](Sensor&, const Reading& r, void*) {
    if (r.boolean || !r.label) return;
    int i = slotFor(r.label);
    if (i < 0) return;
    push(slots[i], r.valid ? r.value : NAN);
  }, nullptr);
  if (sensorsGivePresence()) {
    presenceHist[presenceHead] = presence().present ? 1 : 0;
    presenceHead = (presenceHead + 1) % SAMPLES;
    if (presenceCount < SAMPLES) presenceCount++;
  }
}

// --- current readings, one page each ---

struct Page {
  String label, value, unit;
  float max;
  uint8_t decimals;
  bool valid;
  int slot;  // -1 = not tracked (ran out of slots)
};

Page pagesList[MAX_SLOTS];
int pageCount = 0;
bool hasPresence = false;

void collect() {
  sample();
  pageCount = 0;
  hasPresence = sensorsGivePresence();
  sensorsForEachShown(true, [](Sensor&, const Reading& r, void*) {
    if (r.boolean || !r.label) return;
    if (pageCount >= MAX_SLOTS) return;
    Page& p = pagesList[pageCount++];
    p.label = r.label;
    p.value = r.valid ? String(r.value, (unsigned int)r.decimals) : "--";
    p.unit = r.valid && r.unit ? r.unit : "";
    p.max = r.max;
    p.decimals = r.decimals;
    p.valid = r.valid;
    p.slot = -1;
    for (int i = 0; i < slotCount; i++)
      if (strncmp(slots[i].label, r.label, LABEL_LEN - 1) == 0) { p.slot = i; break; }
  }, nullptr);
}

bool tall(Gfx& g) { return g.height() >= 120; }

int pages(Gfx& g) {
  collect();
  int n = pageCount + (hasPresence ? 1 : 0);
  return n < 1 ? 1 : n;
}

// --- drawing ---

void dottedHline(Gfx& g, int x, int y, int w, Color c) {
  for (int i = 0; i < w; i += 3) g.fillRect(x + i, y, 1, 1, c);
}

// Draws the frame and the bars of one reading's history in the box (gx, gy, gw, gh).
// Returns false if nothing could be drawn (no samples yet). lo/hi receive the scale.
bool drawGraph(Gfx& g, const Page& p, int gx, int gy, int gw, int gh, float& lo, float& hi) {
  Color frame = C_MUTED;
  Color bar = g.color() ? C_ACCENT : C_FG;
  g.rect(gx, gy, gw, gh, frame);
  int ix = gx + 1, iy = gy + 1, iw = gw - 2, ih = gh - 2;
  if (iw < 4 || ih < 3) return false;
  dottedHline(g, ix, iy + ih / 2, iw, frame);
  if (p.slot < 0) return false;
  const Slot& s = slots[p.slot];
  if (!s.count) return false;

  // scale
  if (p.max > 0) {
    lo = 0;
    hi = p.max;
  } else {
    bool any = false;
    for (int a = 0; a < s.count; a++) {
      float v = sampleAt(s, a);
      if (isnan(v)) continue;
      if (!any) { lo = hi = v; any = true; }
      else { if (v < lo) lo = v; if (v > hi) hi = v; }
    }
    if (!any) return false;
    float margin = (hi - lo) * 0.1f;
    if (margin <= 0) margin = fabsf(hi) * 0.1f;
    if (margin <= 0) margin = 1;
    lo -= margin;
    hi += margin;
  }
  float span = hi - lo;
  if (span <= 0) span = 1;

  // newest sample at the right edge, one column (or more on wide screens) per sample
  int right = ix + iw;
  int bottom = iy + ih;  // one past the last inner row
  for (int a = 0; a < s.count; a++) {
    float v = sampleAt(s, a);
    if (isnan(v)) continue;  // gap
    int x1 = right - (int)((float)a * iw / SAMPLES);
    int x0 = right - (int)((float)(a + 1) * iw / SAMPLES);
    if (x0 < ix) x0 = ix;
    int w = x1 - x0;
    if (w < 1) { w = 1; x0 = x1 - 1; }
    if (x0 < ix) break;
    int h = (int)((v - lo) / span * ih + 0.5f);
    if (h < 0) h = 0;
    if (h > ih) h = ih;
    if (h > 0) g.fillRect(x0, bottom - h, w, h, bar);
  }
  return true;
}

void drawReading(Gfx& g, const Page& p, int top) {
  int w = g.width(), h = g.height();
  bool t = tall(g);
  int pad = t ? 4 : 1;

  g.font(F_SMALL);
  g.text(pad, top, p.label.c_str(), C_MUTED);
  int y = top + g.lineHeight() + (t ? 2 : 0);

  g.font(t ? F_LARGE : F_MEDIUM);
  Color vc = p.valid ? C_FG : C_MUTED;
  g.text(pad, y, p.value.c_str(), vc);
  int vh = g.lineHeight();
  if (p.unit.length()) {
    int ux = pad + g.textWidth(p.value.c_str()) + 4;
    g.font(F_SMALL);
    g.text(ux, y + vh - g.lineHeight(), p.unit.c_str(), C_MUTED);
  }
  y += vh + (t ? 4 : 1);

  // room for "min / max" under the graph on tall screens
  int footer = 0;
  if (t) {
    g.font(F_TINY);
    footer = g.lineHeight() + 2;
  }
  int gy = y, gh = h - gy - footer - pad;
  if (gh < 6) {
    footer = 0;
    gh = h - gy - pad;
  }
  if (gh < 4) return;
  float lo = 0, hi = 0;
  bool ok = drawGraph(g, p, pad, gy, w - 2 * pad, gh, lo, hi);
  if (ok && footer) {
    g.font(F_TINY);
    String s = "min " + String(lo, (unsigned int)p.decimals) + " / max " +
               String(hi, (unsigned int)p.decimals);
    g.text(pad, gy + gh + 2, s.c_str(), C_MUTED);
  }
}

void drawPresence(Gfx& g, int top) {
  int w = g.width(), h = g.height();
  bool t = tall(g);
  int pad = t ? 4 : 1;
  bool present = presence().present;
  const char* s = present ? "PRESENT" : "clear";

  g.font(F_SMALL);
  g.text(pad, top, "Presence", C_MUTED);
  int y = top + g.lineHeight() + (t ? 2 : 0);

  g.font(F_LARGE);
  int th = g.lineHeight();
  if (y + th + 10 > h) {  // very small screen: fall back to a smaller font
    g.font(F_MEDIUM);
    th = g.lineHeight();
  }
  if (present && !g.color()) {
    int tw = g.textWidth(s);
    g.fillRect(pad, y, tw + 6, th + 2, C_FG);
    g.text(pad + 3, y + 1, s, C_BG);
  } else {
    g.text(pad, y, s, present ? C_WARN : C_FG);
  }
  y += th + (t ? 8 : 3);

  // timeline strip: last 120 samples, filled = present
  const int STRIP_H = 8;
  int sy = h - pad - STRIP_H;
  if (sy < y) sy = y;
  if (sy + STRIP_H > h) return;
  int gx = pad, gw = w - 2 * pad;
  Color fill = g.color() ? C_WARN : C_FG;
  g.rect(gx, sy, gw, STRIP_H, C_MUTED);
  int ix = gx + 1, iw = gw - 2;
  int right = ix + iw;
  for (int a = 0; a < presenceCount; a++) {
    if (!presenceAt(a)) continue;
    int x1 = right - (int)((float)a * iw / SAMPLES);
    int x0 = right - (int)((float)(a + 1) * iw / SAMPLES);
    if (x0 < ix) x0 = ix;
    int bw = x1 - x0;
    if (bw < 1) { bw = 1; x0 = x1 - 1; }
    if (x0 < ix) break;
    g.fillRect(x0, sy + 1, bw, STRIP_H - 2, fill);
  }
  if (t) {
    g.font(F_TINY);
    g.text(pad, sy - g.lineHeight() - 2, "last 4 min", C_MUTED);
  }
}

void draw(Gfx& g, int page) {
  collect();
  int top = uiHeader(g, settings.name.c_str());
  if (!pageCount && !hasPresence) {
    g.font(F_MEDIUM);
    g.textCenter(top + 6, "No sensors yet");
    g.font(F_SMALL);
    g.textCenter(top + 6 + g.lineHeight() + 6, "Pick them in the", C_MUTED);
    g.textCenter(top + 6 + 2 * g.lineHeight() + 8, "web flasher", C_MUTED);
    return;
  }
  if (hasPresence) {
    if (page == 0) {
      drawPresence(g, top);
      return;
    }
    page--;
  }
  if (page < 0 || page >= pageCount) return;
  drawReading(g, pagesList[page], top);
}

}  // namespace

const Layout GRAPH_LAYOUT = {"graph", pages, draw};
