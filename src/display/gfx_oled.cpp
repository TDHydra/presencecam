// 128x64 I2C OLEDs (SSD1306 0.96", SH1106 1.3") through U8g2, drawn into a full frame buffer.
#include <U8g2lib.h>
#include <Wire.h>

#include "gfx.h"

namespace {

class OledGfx : public Gfx {
  U8G2* u = nullptr;
  uint8_t addr = 0;
  String kind;

 public:
  bool begin(JsonVariantConst cfg) override {
    kind = cfg["type"] | "ssd1306";
    // Use the configured address if it answers, else try both usual ones.
    int want = cfg["addr"] | 0x3C;
    for (int a : {want, 0x3C, 0x3D}) {
      Wire.beginTransmission(a);
      if (Wire.endTransmission() == 0) { addr = a; break; }
    }
    if (!addr) return false;
    if (kind == "sh1106") u = new U8G2_SH1106_128X64_NONAME_F_HW_I2C(U8G2_R0, U8X8_PIN_NONE);
    else u = new U8G2_SSD1306_128X64_NONAME_F_HW_I2C(U8G2_R0, U8X8_PIN_NONE);
    u->setI2CAddress(addr * 2);
    u->begin();
    u->setFontPosTop();
    u->setFontMode(1);
    u->setContrast(200);
    return true;
  }
  int width() override { return 128; }
  int height() override { return 64; }
  bool color() override { return false; }
  void clear() override { u->clearBuffer(); }
  void flush() override { u->sendBuffer(); }
  void font(FontSize f) override {
    static const uint8_t* fonts[] = {u8g2_font_5x8_tf, u8g2_font_6x10_tf, u8g2_font_7x13B_tf, u8g2_font_logisoso16_tr,
                                     u8g2_font_logisoso22_tr};
    u->setFont(fonts[f]);
    u->setFontPosTop();
  }
  int textWidth(const char* s) override { return u->getStrWidth(s); }
  int lineHeight() override { return u->getMaxCharHeight(); }
  void text(int x, int y, const char* s, Color c) override {
    u->setDrawColor(c == C_BG ? 0 : 1);
    u->drawStr(x, y, s);
    u->setDrawColor(1);
  }
  void fillRect(int x, int y, int w, int h, Color c) override {
    u->setDrawColor(c == C_BG ? 0 : 1);
    u->drawBox(x, y, w, h);
    u->setDrawColor(1);
  }
  void rect(int x, int y, int w, int h, Color c) override {
    u->setDrawColor(c == C_BG ? 0 : 1);
    u->drawFrame(x, y, w, h);
    u->setDrawColor(1);
  }
  void setDim(bool dim) override { u->setContrast(dim ? 5 : 200); }
  void setPower(bool on) override { u->setPowerSave(!on); }
  void setFlip(bool flip) override { u->setDisplayRotation(flip ? U8G2_R2 : U8G2_R0); }
  String describe() override {
    char b[40];
    snprintf(b, sizeof b, "%s 128x64 at 0x%02X", kind == "sh1106" ? "SH1106" : "SSD1306", addr);
    return b;
  }
};

}  // namespace

Gfx* makeOledGfx() { return new OledGfx(); }
