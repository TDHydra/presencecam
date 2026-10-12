// "Basic" layout: one row per sensor reading, with presence on top. Rows that don't fit
// go to the next page (short-press BOOT). Small screens get one-line rows, tall ones get a
// label line, a large value and a bar.
#include "layout.h"
#include "presence.h"
#include "sensors/sensor.h"
#include "settings.h"

namespace {

struct Row {
  String label, value, unit;
  float frac;  // bar fill, < 0 = no bar
  bool highlight, valid;
};

const int MAX_ROWS = 16;
Row rows[MAX_ROWS];
int rowCount = 0;

String fmt(const Reading& r) {
  if (!r.valid) return "--";
  if (r.boolean) return r.value ? "yes" : "no";
  return String(r.value, (unsigned int)r.decimals);
}

void collect() {
  rowCount = 0;
  if (sensorsGivePresence()) {
    bool p = presence().present;
    rows[rowCount++] = {"Presence", p ? "PRESENT" : "clear", "", -1, p, true};
  }
  sensorsForEachShown(true, [](Sensor&, const Reading& r, void*) {
    if (rowCount >= MAX_ROWS) return;
    float frac = r.max > 0 && r.valid ? r.value / r.max : (r.max > 0 ? 0 : -1);
    rows[rowCount++] = {r.label, fmt(r), r.valid && !r.boolean ? r.unit : "", frac, false, r.valid};
  }, nullptr);
}

bool tall(Gfx& g) { return g.height() >= 120; }

int rowHeight(Gfx& g) { return tall(g) ? 58 : 17; }

int perPage(Gfx& g) {
  // the header height is fixed per screen size, see uiHeader()
  int top = tall(g) ? 30 : 13;
  return max(1, (g.height() - top) / rowHeight(g));
}

int pages(Gfx& g) {
  collect();
  if (!rowCount) return 1;
  return (rowCount + perPage(g) - 1) / perPage(g);
}

void drawRowSmall(Gfx& g, const Row& r, int y) {
  // label left, value right, thin bar underneath
  int w = g.width();
  if (r.highlight) g.fillRect(0, y, w, 14, C_FG);
  Color fg = r.highlight ? C_BG : C_FG;
  g.font(F_SMALL);
  g.text(1, y + 2, r.label.c_str(), fg);
  String v = r.value + (r.unit.length() ? " " + r.unit : "");
  g.font(F_MEDIUM);
  g.textRight(w - 1, y + 1, v.c_str(), fg);
  if (r.frac >= 0) g.bar(0, y + 14, w, 3, r.frac, C_FG);
}

void drawRowTall(Gfx& g, const Row& r, int y) {
  int w = g.width();
  g.font(F_SMALL);
  g.text(4, y, r.label.c_str(), C_MUTED);
  g.font(F_LARGE);
  Color c = r.highlight ? C_WARN : (r.valid ? C_FG : C_MUTED);
  g.text(4, y + 16, r.value.c_str(), c);
  if (r.unit.length()) {
    int vx = 4 + g.textWidth(r.value.c_str()) + 4;
    g.font(F_SMALL);
    g.text(vx, y + 30, r.unit.c_str(), C_MUTED);
  }
  if (r.frac >= 0) g.bar(4, y + 48, w - 8, 6, r.frac, C_ACCENT);
}

void draw(Gfx& g, int page) {
  collect();
  int top = uiHeader(g, settings.name.c_str());
  if (!rowCount) {
    g.font(F_MEDIUM);
    g.textCenter(top + 6, "No sensors yet");
    g.font(F_SMALL);
    g.textCenter(top + 6 + g.lineHeight() + 6, "Pick them in the", C_MUTED);
    g.textCenter(top + 6 + 2 * g.lineHeight() + 8, "web flasher", C_MUTED);
    return;
  }
  int per = perPage(g);
  int first = page * per;
  for (int i = first; i < rowCount && i < first + per; i++) {
    int y = top + (i - first) * rowHeight(g);
    if (tall(g)) drawRowTall(g, rows[i], y);
    else drawRowSmall(g, rows[i], y);
  }
}

}  // namespace

const Layout BASIC_LAYOUT = {"basic", pages, draw};
