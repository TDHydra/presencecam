// A small drawing API that every display backend implements, so layouts work the same on
// a 128x64 monochrome OLED and a 172x320 colour LCD. Text positions are the top-left corner.
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

enum Color : uint8_t { C_BG, C_FG, C_MUTED, C_ACCENT, C_GOOD, C_WARN };
enum FontSize : uint8_t { F_TINY, F_SMALL, F_MEDIUM, F_LARGE, F_HUGE };

class Gfx {
 public:
  virtual ~Gfx() {}
  virtual bool begin(JsonVariantConst displayCfg) = 0;  // false if the display isn't there
  virtual int width() = 0;
  virtual int height() = 0;
  virtual bool color() = 0;  // false on monochrome OLEDs
  virtual void clear() = 0;
  virtual void flush() = 0;  // push the frame to the screen
  virtual void font(FontSize f) = 0;
  virtual int textWidth(const char* s) = 0;
  virtual int lineHeight() = 0;  // height of the current font
  virtual void text(int x, int y, const char* s, Color c = C_FG) = 0;
  virtual void fillRect(int x, int y, int w, int h, Color c) = 0;
  virtual void rect(int x, int y, int w, int h, Color c) = 0;
  virtual void setDim(bool dim) = 0;
  virtual void setPower(bool on) = 0;
  virtual void setFlip(bool flip) = 0;  // rotate 180 degrees
  virtual String describe() = 0;        // "SSD1306 128x64 at 0x3C"

  // Helpers built on the primitives
  void textCenter(int y, const char* s, Color c = C_FG) { text((width() - textWidth(s)) / 2, y, s, c); }
  void textRight(int xRight, int y, const char* s, Color c = C_FG) { text(xRight - textWidth(s), y, s, c); }
  void hline(int x, int y, int w, Color c) { fillRect(x, y, w, 1, c); }
  void bar(int x, int y, int w, int h, float frac, Color c = C_ACCENT) {
    rect(x, y, w, h, color() ? C_MUTED : C_FG);
    int fill = (int)((w - 2) * constrain(frac, 0.0f, 1.0f));
    if (fill > 0) fillRect(x + 1, y + 1, fill, h - 2, c);
  }
};

Gfx* makeOledGfx();  // SSD1306 / SH1106 on I2C (U8g2)
Gfx* makeLcdGfx();   // ST7789 on SPI (LovyanGFX)
