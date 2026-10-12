// "Tiles" layout: a dashboard grid, every reading in its own boxed tile so as much as
// possible fits on one page. Presence (if any sensor gives it) is the first tile and is
// filled solid while someone is there. Tiles that don't fit go to the next page
// (short-press BOOT).
#include "layout.h"
#include "presence.h"
#include "sensors/sensor.h"
#include "settings.h"

namespace {

struct Tile {
  String label, value, unit;
  float frac;  // bar fill, < 0 = no bar
  bool highlight, valid;
};

const int MAX_TILES = 16;
const int GAP = 2;
Tile tiles[MAX_TILES];
int tileCount = 0;

String fmt(const Reading& r) {
  if (!r.valid) return "--";
  if (r.boolean) return r.value ? "yes" : "no";
  return String(r.value, (unsigned int)r.decimals);
}

void collect() {
  tileCount = 0;
  if (sensorsGivePresence()) {
    bool p = presence().present;
    tiles[tileCount++] = {"Presence", p ? "PRESENT" : "clear", "", -1, p, true};
  }
  sensorsForEachShown(true, [](Sensor&, const Reading& r, void*) {
    if (tileCount >= MAX_TILES) return;
    float frac = r.max > 0 && r.valid ? r.value / r.max : (r.max > 0 ? 0 : -1);
    tiles[tileCount++] = {r.label, fmt(r), r.valid && !r.boolean ? r.unit : "", frac, false, r.valid};
  }, nullptr);
}

bool tall(Gfx& g) { return g.height() >= 120; }

int cols(Gfx& g) {
  int w = g.width();
  if (w < 160) return 2;
  return w < 220 ? 2 : 3;
}

int tileHeight(Gfx& g) { return tall(g) ? 70 : 26; }

int tileWidth(Gfx& g) {
  int c = cols(g);
  return (g.width() - (c - 1) * GAP) / c;
}

int rowsPerPage(Gfx& g) {
  // the header height is fixed per screen size, see uiHeader()
  int top = tall(g) ? 30 : 13;
  return max(1, (g.height() - top + GAP) / (tileHeight(g) + GAP));
}

int perPage(Gfx& g) { return cols(g) * rowsPerPage(g); }

int pages(Gfx& g) {
  collect();
  if (!tileCount) return 1;
  return (tileCount + perPage(g) - 1) / perPage(g);
}

void drawTile(Gfx& g, const Tile& t, int x, int y, int w, int h) {
  bool big = tall(g);
  Color frame = g.color() ? C_MUTED : C_FG;
  Color labelC = C_MUTED, valueC = t.valid ? C_FG : C_MUTED, unitC = C_MUTED;
  if (t.highlight) {
    g.fillRect(x, y, w, h, g.color() ? C_WARN : C_FG);
    labelC = valueC = unitC = C_BG;
  } else {
    g.rect(x, y, w, h, frame);
  }

  int pad = big ? 4 : 2;
  g.font(F_SMALL);
  g.text(x + pad, y + pad - 1, t.label.c_str(), labelC);

  // value: F_LARGE on tall screens if it fits beside its unit, else F_MEDIUM
  FontSize unitFont = big ? F_SMALL : F_TINY;
  g.font(unitFont);
  int unitW = t.unit.length() ? g.textWidth(t.unit.c_str()) + 3 : 0;
  FontSize valueFont = F_MEDIUM;
  if (big) {
    g.font(F_LARGE);
    if (g.textWidth(t.value.c_str()) + unitW <= w - 2 * pad) valueFont = F_LARGE;
  }
  g.font(valueFont);
  int vy = big ? y + 18 : y + 11;
  int vh = g.lineHeight();
  g.text(x + pad, vy, t.value.c_str(), valueC);
  if (t.unit.length()) {
    int ux = x + pad + g.textWidth(t.value.c_str()) + 3;
    g.font(unitFont);
    int uy = vy + vh - g.lineHeight();  // sit the unit on the value's baseline
    if (uy < vy) uy = vy;
    g.text(ux, uy, t.unit.c_str(), unitC);
  }

  if (t.frac >= 0) {
    int bh = big ? 5 : 3;
    g.bar(x + pad, y + h - bh - (big ? 3 : 1), w - 2 * pad, bh, t.frac, C_ACCENT);
  }
}

void draw(Gfx& g, int page) {
  collect();
  int top = uiHeader(g, settings.name.c_str());
  if (!tileCount) {
    g.font(F_MEDIUM);
    g.textCenter(top + 6, "No sensors yet");
    g.font(F_SMALL);
    g.textCenter(top + 6 + g.lineHeight() + 6, "Pick them in the", C_MUTED);
    g.textCenter(top + 6 + 2 * g.lineHeight() + 8, "web flasher", C_MUTED);
    return;
  }
  int c = cols(g), tw = tileWidth(g), th = tileHeight(g);
  int per = perPage(g);
  int first = page * per;
  for (int i = first; i < tileCount && i < first + per; i++) {
    int n = i - first;
    int x = (n % c) * (tw + GAP);
    int y = top + (n / c) * (th + GAP);
    drawTile(g, tiles[i], x, y, tw, th);
  }
}

}  // namespace

const Layout TILES_LAYOUT = {"tiles", pages, draw};
