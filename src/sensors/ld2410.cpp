// HLK-LD2410 (B/C) 24 GHz radar. Over UART it gives distance and signal energy for moving
// and still targets; its OUT pin goes high on presence. Either or both can be wired. With
// only OUT wired (or no UART frames arriving), it behaves like an RCWL-0516.
#include <soc/soc_caps.h>
#include "../debuglog.h"
#ifndef SOC_UART_HP_NUM  // IDF 4.4 (Arduino core 2)
#define SOC_UART_HP_NUM SOC_UART_NUM
#endif

#include "sensors.h"

#include "settings.h"

namespace {

// Report frame:
//   F4 F3 F2 F1 | len (2, LE) | type (0x02 basic, 0x01 engineering) | 0xAA |
//   target state | moving cm (2) | moving energy | still cm (2) | still energy | detect cm (2) |
//   [engineering data...] | 0x55 | check | F8 F7 F6 F5
// The basic fields sit at the same place in both frame types, so we only read those.
const uint8_t HEAD[4] = {0xF4, 0xF3, 0xF2, 0xF1};
const uint8_t TAIL[4] = {0xF8, 0xF7, 0xF6, 0xF5};

class LD2410 : public Sensor {
  HardwareSerial* port = nullptr;
  int outPin = -1;
  uint8_t buf[64];
  size_t len = 0;
  uint32_t lastFrameMs = 0;
  uint32_t frames = 0;
  bool uart = false, out = false, moving = false, still = false, detected = false;
  uint16_t movingCm = 0, stillCm = 0, distanceCm = 0;
  uint8_t movingEnergy = 0, stillEnergy = 0;

  void parseFrame(const uint8_t* d, size_t n) {
    // d points at the type byte, n is the payload length from the header
    if (n < 13 || d[1] != 0xAA) return;
    moving = d[2] & 0x01;
    still = d[2] & 0x02;
    movingCm = d[3] | (d[4] << 8);
    movingEnergy = d[5];
    stillCm = d[6] | (d[7] << 8);
    stillEnergy = d[8];
    distanceCm = d[9] | (d[10] << 8);
    frames++;
    lastFrameMs = millis();
  }

  void feed(uint8_t b) {
    if (len < 4) {  // still matching the header
      if (b == HEAD[len]) buf[len++] = b;
      else len = (b == HEAD[0]) ? 1 : 0;
      if (len == 1) buf[0] = b;
      return;
    }
    buf[len++] = b;
    if (len < 6) return;
    size_t payload = buf[4] | (buf[5] << 8);
    size_t total = 4 + 2 + payload + 4;
    if (total > sizeof buf) { len = 0; return; }  // not a report frame (or garbage)
    if (len < total) return;
    if (memcmp(buf + total - 4, TAIL, 4) == 0) parseFrame(buf + 6, payload);
    len = 0;
  }

 public:
  bool begin(JsonVariantConst cfg) override {
    outPin = hwPin(cfg, "out");
    int rx = hwPin(cfg, "rx"), tx = hwPin(cfg, "tx");
    if (outPin >= 0) pinMode(outPin, INPUT_PULLDOWN);
    if (rx >= 0) {
#if SOC_UART_HP_NUM > 2
      port = (cfg["uart"] | 2) == 1 ? &Serial1 : &Serial2;
#else
      port = &Serial1;  // ESP32-C3 / C6: UART1 is the only free one
#endif
      port->begin(256000, SERIAL_8N1, rx, tx);
    }
    return true;  // the LD2410 never answers on its own; status() shows whether frames arrive
  }

  void loop() override {
    if (port) while (port->available()) feed(port->read());
    out = outPin >= 0 && digitalRead(outPin);
    uart = frames > 0 && millis() - lastFrameMs < 1500;
    if (uart) {
      bool m = moving && movingCm <= settings.maxCm;
      bool s = still && stillCm <= settings.maxCm;
      detected = m || s;
    } else {
      moving = still = false;
      movingEnergy = stillEnergy = 0;
      detected = out;
    }
  }

  int readings(Reading* r, int max) override {
    int n = 0;
    bool target = moving || still;
    if (n < max) r[n++] = {"Radar", (float)detected, "", 0, 0, true, true};
    if (port && n < max) r[n++] = {"Distance", (float)distanceCm, "cm", (float)settings.maxCm, 0, uart && target, false};
    if (port && n < max) r[n++] = {"Moving", (float)movingEnergy, "%", 100, 0, uart, false};
    if (port && n < max) r[n++] = {"Still", (float)stillEnergy, "%", 100, 0, uart, false};
    return n;
  }

  bool givesPresence() override { return true; }
  bool presence() override { return detected; }
  String presenceDetail() override {
    if (!uart) return "radar OUT";
    return String(moving ? movingCm : stillCm) + " cm " + (moving ? "moving" : "still");
  }
  const char* type() override { return "ld2410"; }
  const char* title() override { return "LD2410 radar"; }
  String status() override {
    String s = uart ? "UART ok, " + String(frames) + " frames" : (port ? "no UART frames (check TX/RX)" : "OUT pin only");
    if (outPin >= 0) s += ", OUT " + String(out ? "high" : "low");
    if (uart) s += ", moving " + String(moving) + " " + String(movingCm) + "cm/" + String(movingEnergy) +
                   ", still " + String(still) + " " + String(stillCm) + "cm/" + String(stillEnergy);
    return s;
  }
};

}  // namespace

Sensor* makeLD2410() { return new LD2410(); }
