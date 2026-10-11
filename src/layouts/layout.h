// Screen layouts. A layout draws the main page(s); the network and device pages are added
// after them by display.cpp. To add a layout: write layouts/<id>.cpp with a Layout, list it
// in layoutFind() (display.cpp), accept the id in hwconfig.cpp validate() and add
// catalog/layouts/<id>.json.
#pragma once
#include "display/gfx.h"

struct Layout {
  const char* id;
  int (*pages)(Gfx& g);              // how many pages this layout needs right now
  void (*draw)(Gfx& g, int page);
};

extern const Layout BASIC_LAYOUT;

// Shared drawing helpers (display.cpp)
int uiHeader(Gfx& g, const char* title);  // draws the top bar, returns the y where content starts
String uiDuration(uint32_t seconds);
