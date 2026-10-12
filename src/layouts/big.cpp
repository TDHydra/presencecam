// "Big" layout: one reading per page, drawn as large as the screen allows. Presence comes
// first (if any sensor gives it), then one page per sensor reading (short-press BOOT).
// Works on a 128x64 OLED (13 px header, F_LARGE values) and on the 172x320 / 240x280
// colour LCDs (30 px header, F_HUGE values when they fit).
#include "layout.h"
#include "presence.h"
#include "sensors/sensor.h"
#include "settings.h"

namespace {

struct Item {
  bool presence;  // true = the presence page, the Reading fields are unused
  String label, value, unit;
  float frac;  // bar fill, < 0 = no bar
  bool valid;
};

const int MAX_ITEMS = 17;  // presence + up to 16 readings
Item items[MAX_ITEMS];
int count = 0;

String fmt(const Reading& r) {
  if (!r.valid) return "--";
  if (r.boolean) return r.value ? "yes" : "no";
  return String(r.value, (unsigned int)r.decimals);
}

void collect() {
  count = 0;
  if (sensorsGivePresence()) items[count++] = {true, "", "", "", -1, true};
  sensorsForEachShown(true, [](Sensor&, const Reading& r, void*) {
    if (count >= MAX_ITEMS) return;
    float frac = r.max > 0 && r.valid ? r.value / r.max : (r.max > 0 ? 0 : -1);
    items[count++] = {false, r.label, fmt(r), r.valid && !r.boolean ? r.unit : "", frac, r.valid};
  }, nullptr);
}

bool tall(Gfx& g) { return g.height() >= 120; }

int pages(Gfx& g) {
  collect();
  return max(1, count);
}

// Picks the biggest font (from `first` downwards) whose rendering of s fits in maxW.
// Leaves that font selected and returns it.
FontSize fitFont(Gfx& g, const char* s, int maxW, FontSize first, FontSize smallest) {
  for (int f = first; f > smallest; f--) {
    g.font((FontSize)f);
    if (g.textWidth(s) <= maxW) return (FontSize)f;
  }
  g.font(smallest);
  return smallest;
}

void drawPresence(Gfx& g, int top) {
  const Presence& p = presence();
  int w = g.width(), h = g.height();
  const char* word = p.present ? "PRESENT" : "clear";

  g.font(F_SMALL);
  int smallLh = g.lineHeight();
  int infoLines = tall(g) ? 2 : 1;
  int infoH = infoLines * (smallLh + 2);

  FontSize wf = fitFont(g, word, w - 8, F_HUGE, F_LARGE);
  g.font(wf);
  int wordLh = g.lineHeight();

  // centre the word + info block vertically in the space under the header
  int blockH = wordLh + 6 + infoH;
  int y = top + max(0, (h - top - blockH) / 2);
  int wx = (w - g.textWidth(word)) / 2;

  if (p.present) {
    if (g.color()) {
      g.text(wx, y, word, C_WARN);
    } else {
      g.fillRect(0, y - 2, w, wordLh + 4, C_FG);
      g.text(wx, y, word, C_BG);
    }
  } else {
    g.text(wx, y, word, C_FG);
  }

  uint32_t since = (millis() - p.sinceMs) / 1000;
  String dur = "for " + uiDuration(since);
  String visits = String(p.visitsToday) + (p.visitsToday == 1 ? " visit today" : " visits today");
  int iy = y + wordLh + 6;
  g.font(F_SMALL);
  if (infoLines == 2) {
    g.textCenter(iy, dur.c_str(), C_MUTED);
    g.textCenter(iy + smallLh + 2, visits.c_str(), C_MUTED);
  } else {
    String one = dur + "  " + String(p.visitsToday) + " visits";
    g.textCenter(iy, one.c_str(), C_MUTED);
  }
}

void drawReading(Gfx& g, const Item& it, int top) {
  int w = g.width(), h = g.height();
  int pad = 4;

  g.font(F_SMALL);
  int smallLh = g.lineHeight();
  g.text(pad, top + 1, it.label.c_str(), C_MUTED);
  int contentTop = top + 1 + smallLh + 2;

  // thick bar across the bottom when the reading has a max
  int barH = tall(g) ? 12 : 5;
  int barY = h - pad - barH;
  int contentBottom = it.frac >= 0 ? barY - 4 : h - 2;
  if (it.frac >= 0) g.bar(pad, barY, w - 2 * pad, barH, it.frac, C_ACCENT);

  // value in the biggest font that fits with the unit beside it
  int unitW = 0;
  if (it.unit.length()) {
    g.font(F_SMALL);
    unitW = g.textWidth(it.unit.c_str()) + pad;
  }
  int maxW = w - 2 * pad - unitW;
  FontSize vf = fitFont(g, it.value.c_str(), maxW, tall(g) ? F_HUGE : F_LARGE, F_MEDIUM);
  g.font(vf);
  int vLh = g.lineHeight();
  int vW = g.textWidth(it.value.c_str());
  int vy = contentTop + max(0, (contentBottom - contentTop - vLh) / 2);
  int vx = (w - vW - unitW) / 2;
  if (vx < pad) vx = pad;
  Color c = it.valid ? C_FG : C_MUTED;
  g.text(vx, vy, it.value.c_str(), c);

  if (it.unit.length()) {
    g.font(F_SMALL);
    // sit the unit on the value's baseline
    g.text(vx + vW + pad, vy + vLh - smallLh, it.unit.c_str(), C_MUTED);
  }
}

void draw(Gfx& g, int page) {
  collect();
  int top = uiHeader(g, settings.name.c_str());
  if (!count) {
    g.font(F_MEDIUM);
    g.textCenter(top + 6, "No sensors yet");
    g.font(F_SMALL);
    g.textCenter(top + 6 + g.lineHeight() + 6, "Pick them in the", C_MUTED);
    g.textCenter(top + 6 + 2 * g.lineHeight() + 8, "web flasher", C_MUTED);
    return;
  }
  if (page < 0 || page >= count) page = 0;
  const Item& it = items[page];
  if (it.presence) drawPresence(g, top);
  else drawReading(g, it, top);
}

}  // namespace

const Layout BIG_LAYOUT = {"big", pages, draw};
