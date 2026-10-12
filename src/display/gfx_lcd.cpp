// ST7789 SPI colour LCDs through LovyanGFX, set up at runtime from the hardware config
// (pins, size, offsets). Frames are drawn into a sprite and pushed in one go, so nothing flickers.
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "gfx.h"
#include "hwconfig.h"
#include "storage.h"

namespace {

class Panel : public lgfx::LGFX_Device {
 public:
  lgfx::Panel_ST7789 panel;
  lgfx::Bus_SPI bus;
  lgfx::Light_PWM light;

  void configure(JsonVariantConst d) {
    auto b = bus.config();
    b.spi_host = SPI2_HOST;
    b.spi_mode = 0;
    b.freq_write = 40000000;
    b.pin_sclk = hwPin(d, "sclk");
    b.pin_mosi = hwPin(d, "mosi");
    // A microSD card on the same bus (ESP32-C6-LCD-1.47) needs MISO on it and turns about
    bool sharedSd = storageCardSharesDisplay();
    b.pin_miso = sharedSd ? hwPin(hw["sd"], "miso") : -1;
    b.pin_dc = hwPin(d, "dc");
    b.dma_channel = SPI_DMA_CH_AUTO;
    bus.config(b);
    panel.setBus(&bus);

    auto p = panel.config();
    p.pin_cs = hwPin(d, "cs");
    p.pin_rst = hwPin(d, "rst");
    p.panel_width = d["w"] | 240;
    p.panel_height = d["h"] | 320;
    p.memory_width = 240;
    p.memory_height = 320;
    p.offset_x = d["xoff"] | 0;
    p.offset_y = d["yoff"] | 0;
    p.invert = d["invert"] | true;
    p.readable = false;
    p.bus_shared = sharedSd;
    panel.config(p);

    if (hwPin(d, "bl") >= 0) {
      auto l = light.config();
      l.pin_bl = hwPin(d, "bl");
      l.pwm_channel = 7;
      l.freq = 12000;
      light.config(l);
      panel.setLight(&light);
    }
    setPanel(&panel);
  }
};

const uint16_t PALETTE[] = {
    0x0000,  // C_BG     black
    0xFFFF,  // C_FG     white
    0x8C71,  // C_MUTED  grey
    0x3D7F,  // C_ACCENT blue
    0x2E6A,  // C_GOOD   green
    0xFD20,  // C_WARN   amber
};

class LcdGfx : public Gfx {
  Panel lcd;
  LGFX_Sprite canvas{&lcd};
  bool buffered = false;
  int w = 0, h = 0;
  uint8_t bright = 200;

  lgfx::LovyanGFX& g() { return buffered ? (lgfx::LovyanGFX&)canvas : (lgfx::LovyanGFX&)lcd; }

 public:
  bool begin(JsonVariantConst cfg) override {
    lcd.configure(cfg);
    if (!lcd.init()) return false;
    lcd.setRotation(0);
    lcd.setBrightness(bright);
    lcd.fillScreen(PALETTE[C_BG]);
    w = lcd.width();
    h = lcd.height();
    canvas.setPsram(true);
    canvas.setColorDepth(16);
    buffered = canvas.createSprite(w, h) != nullptr;
    if (!buffered) {  // no PSRAM: try a smaller 8-bit frame
      canvas.setPsram(false);
      canvas.setColorDepth(8);
      buffered = canvas.createSprite(w, h) != nullptr;
    }
    g().setTextDatum(lgfx::top_left);
    return true;
  }
  int width() override { return w; }
  int height() override { return h; }
  bool color() override { return true; }
  void clear() override { g().fillScreen(PALETTE[C_BG]); }
  void flush() override {
    if (buffered) canvas.pushSprite(0, 0);
  }
  void font(FontSize f) override {
    static const lgfx::IFont* fonts[] = {&fonts::Font0, &fonts::Font2, &fonts::FreeSansBold9pt7b, &fonts::FreeSansBold18pt7b,
                                         &fonts::FreeSansBold24pt7b};
    g().setFont(fonts[f]);
    g().setTextDatum(lgfx::top_left);
  }
  int textWidth(const char* s) override { return g().textWidth(s); }
  int lineHeight() override { return g().fontHeight(); }
  void text(int x, int y, const char* s, Color c) override {
    g().setTextColor(PALETTE[c]);
    g().drawString(s, x, y);
  }
  void fillRect(int x, int y, int ww, int hh, Color c) override { g().fillRect(x, y, ww, hh, PALETTE[c]); }
  void rect(int x, int y, int ww, int hh, Color c) override { g().drawRect(x, y, ww, hh, PALETTE[c]); }
  void setDim(bool dim) override { lcd.setBrightness(dim ? 20 : bright); }
  void setPower(bool on) override {
    if (on) lcd.wakeup();
    else lcd.sleep();
  }
  void setFlip(bool flip) override { lcd.setRotation(flip ? 2 : 0); }
  String describe() override { return "ST7789 " + String(w) + "x" + String(h) + (buffered ? "" : " (unbuffered)"); }
};

}  // namespace

Gfx* makeLcdGfx() { return new LcdGfx(); }
