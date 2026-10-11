#include "radar.h"

#include "config.h"
#include "settings.h"

// LD2410 report frame:
//   F4 F3 F2 F1 | len (2, LE) | type (0x02 basic, 0x01 engineering) | 0xAA |
//   target state | moving cm (2) | moving energy | still cm (2) | still energy | detect cm (2) |
//   [engineering data...] | 0x55 | check | F8 F7 F6 F5
// The basic fields sit at the same place in both frame types, so we only read those.

static RadarState st;
static uint32_t lastFrameMs = 0;
static uint8_t buf[64];
static size_t len = 0;

static const uint8_t HEAD[4] = {0xF4, 0xF3, 0xF2, 0xF1};
static const uint8_t TAIL[4] = {0xF8, 0xF7, 0xF6, 0xF5};

static void parseFrame(const uint8_t* d, size_t n) {
  // d points at the type byte, n is the payload length from the header
  if (n < 13 || d[1] != 0xAA) return;
  uint8_t target = d[2];
  st.moving = target & 0x01;
  st.still = target & 0x02;
  st.movingCm = d[3] | (d[4] << 8);
  st.movingEnergy = d[5];
  st.stillCm = d[6] | (d[7] << 8);
  st.stillEnergy = d[8];
  st.distanceCm = d[9] | (d[10] << 8);
  st.frames++;
  lastFrameMs = millis();
}

static void feed(uint8_t b) {
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

void radarBegin() {
  pinMode(PIN_RADAR_OUT, INPUT_PULLDOWN);
  Serial2.begin(RADAR_BAUD, SERIAL_8N1, PIN_RADAR_RX, PIN_RADAR_TX);
}

void radarLoop() {
  while (Serial2.available()) feed(Serial2.read());
  st.outPin = digitalRead(PIN_RADAR_OUT);
  st.uart = st.frames > 0 && millis() - lastFrameMs < 1500;

  if (st.uart) {
    bool m = st.moving && st.movingCm <= settings.maxCm;
    bool s = st.still && st.stillCm <= settings.maxCm;
    st.detected = m || s;
  } else {
    st.moving = st.still = false;
    st.movingEnergy = st.stillEnergy = 0;
    st.detected = st.outPin;
  }
}

const RadarState& radar() { return st; }

const char* radarType() { return st.uart ? "LD2410 (UART)" : "OUT pin only"; }
