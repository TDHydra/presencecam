#define DEBUGLOG_NO_MACRO
#include "debuglog.h"

#include <freertos/FreeRTOS.h>

#include "health.h"
#include "net.h"
#include "sensors/sensor.h"
#include "settings.h"
#include "storage.h"

DebugTee Dbg;

static const char* LOG = "/debug.log";
static const char* OLD = "/debug.old";

// Bytes printed but not yet in the file. Serial is written from more than one task (Wi-Fi
// events, the web server), so the ring is guarded by a spinlock and never allocates.
static char ring[4096];
static size_t head = 0, used = 0;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static bool lineStart = true;
static bool capturing = true;  // until debugBegin() knows the setting: keeps the boot messages
static uint32_t lastFlush = 0, lastStatus = 0;

static void put(char c) {
  ring[(head + used) % sizeof ring] = c;
  if (used < sizeof ring) used++;
  else head = (head + 1) % sizeof ring;  // full: drop the oldest byte
}

static void capture(uint8_t b) {
  if (!capturing) return;
  if (b == '\r') return;
  if (b != '\n' && b != '\t' && (b < 0x20 || b > 0x7e)) return;  // Improv packets and the like
  char stamp[24] = "";
  if (lineStart && b != '\n') {
    time_t now = time(nullptr);
    if (now > 1600000000) {
      struct tm t;
      localtime_r(&now, &t);
      strftime(stamp, sizeof stamp, "%m-%d %H:%M:%S ", &t);
    } else {
      snprintf(stamp, sizeof stamp, "+%lu.%03lus ", (unsigned long)(millis() / 1000), (unsigned long)(millis() % 1000));
    }
  }
  portENTER_CRITICAL(&lock);
  for (const char* p = stamp; *p; p++) put(*p);
  put((char)b);
  portEXIT_CRITICAL(&lock);
  lineStart = b == '\n';
}

int DebugTee::available() { return Serial.available(); }
int DebugTee::read() { return Serial.read(); }
int DebugTee::peek() { return Serial.peek(); }
void DebugTee::flush() { Serial.flush(); }
size_t DebugTee::write(uint8_t b) {
  capture(b);
  return Serial.write(b);
}
size_t DebugTee::write(const uint8_t* buf, size_t n) {
  for (size_t i = 0; i < n; i++) capture(buf[i]);
  return Serial.write(buf, n);
}
void DebugTee::begin(unsigned long baud) { Serial.begin(baud); }
void DebugTee::setRxBufferSize(size_t n) { Serial.setRxBufferSize(n); }

// The file is capped so it can't fill the flash; the older half is kept in /debug.old.
static size_t cap() { return storageOnSd() ? 4 * 1024 * 1024 : 24 * 1024; }

static void flushToFile() {
  static char out[sizeof ring];
  size_t n = 0;
  portENTER_CRITICAL(&lock);
  while (used) {
    out[n++] = ring[head];
    head = (head + 1) % sizeof ring;
    used--;
  }
  portEXIT_CRITICAL(&lock);
  if (!n || storageTotal() == 0) return;
  fs::FS& fs = storageFs();
  File f = fs.open(LOG, "a");
  if (!f) return;
  f.write((const uint8_t*)out, n);
  size_t size = f.size();
  f.close();
  if (size > cap() / 2) {
    fs.remove(OLD);
    fs.rename(LOG, OLD);
  }
}

static void statusLine() {
  String s = "debug: heap " + String(ESP.getFreeHeap() / 1024) + " KB (min " + String(ESP.getMinFreeHeap() / 1024) +
             " KB)";
  if (psramFound()) s += ", psram " + String(ESP.getFreePsram() / 1024) + " KB";
  s += ", net " + (netConnected() ? netLink() + " " + netIp() : String("down")) + " (" + netAddressing() + ")";
  if (netRssi()) s += " " + String(netRssi()) + " dBm";
  for (int i = 0; i < sensorsCount(); i++) s += "; " + String(sensorAt(i)->title()) + ": " + sensorAt(i)->status();
  s += "; health " + healthSummary();
  Dbg.println(s);
}

void debugBegin() {
  capturing = settings.debug;
  if (!capturing) {
    portENTER_CRITICAL(&lock);
    used = 0;
    portEXIT_CRITICAL(&lock);
  } else {
    Dbg.println("debug: mode on, logging to " + String(storageOnSd() ? "microSD" : "flash"));
  }
}

void debugLoop() {
  if (!capturing) return;
  uint32_t now = millis();
  if (now - lastStatus >= 30000) {
    lastStatus = now;
    statusLine();
  }
  if (now - lastFlush >= 5000 || used > sizeof ring / 2) {
    lastFlush = now;
    flushToFile();
  }
}

void debugSetEnabled(bool on) {
  if (on == settings.debug) return;
  if (!on) {
    Dbg.println("debug: mode off");
    flushToFile();
  }
  settings.debug = on;
  settingsSave();
  capturing = on;
  if (on) {
    lastStatus = 0;  // a status line right away
    Dbg.println("debug: mode on, logging to " + String(storageOnSd() ? "microSD" : "flash"));
  }
}

bool debugEnabled() { return settings.debug; }

bool debugSendLog(Print& to) {
  if (capturing) flushToFile();
  if (storageTotal() == 0) return false;
  // "debug show" sends it to the console, which would copy it back into the log
  bool was = capturing;
  capturing = false;
  struct Restore { bool v; ~Restore() { capturing = v; } } restore{was};
  bool any = false;
  uint8_t buf[512];
  for (const char* path : {OLD, LOG}) {
    if (!storageFs().exists(path)) continue;
    File f = storageFs().open(path, "r");
    if (!f) continue;
    any = true;
    while (f.available()) {
      size_t n = f.read(buf, sizeof buf);
      if (to.write(buf, n) != n) { f.close(); return true; }
    }
    f.close();
  }
  return any;
}

void debugClear() {
  portENTER_CRITICAL(&lock);
  used = 0;
  portEXIT_CRITICAL(&lock);
  if (storageTotal() == 0) return;
  storageFs().remove(LOG);
  storageFs().remove(OLD);
}

size_t debugLogBytes() {
  if (storageTotal() == 0) return 0;
  size_t n = used;
  for (const char* path : {OLD, LOG}) {
    if (!storageFs().exists(path)) continue;
    File f = storageFs().open(path, "r");
    if (!f) continue;
    n += f.size();
    f.close();
  }
  return n;
}
