#include "eventlog.h"
#include "debuglog.h"

#include <Preferences.h>
#include <esp_random.h>
#include <mbedtls/md.h>

#include "config.h"
#include "storage.h"

static const char* LOG = "/log.txt";
static const char* OLD = "/log.old.txt";

static uint8_t key[32];
static uint32_t nextLine = 1;
static String lastMac = "0";  // the first line's prev is "0"
static bool fsOk = false;

static String hmacHex(const String& msg) {
  uint8_t out[32];
  const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  mbedtls_md_hmac(md, key, sizeof key, (const uint8_t*)msg.c_str(), msg.length(), out);
  char hex[33];
  for (int i = 0; i < 16; i++) sprintf(hex + i * 2, "%02x", out[i]);
  return String(hex);
}

static void loadState() {
  Preferences p;
  p.begin("pcamlog", false);
  if (p.getBytes("key", key, sizeof key) != sizeof key) {
    esp_fill_random(key, sizeof key);
    p.putBytes("key", key, sizeof key);
    p.putUInt("next", 1);
    p.putString("last", "0");
  }
  nextLine = p.getUInt("next", 1);
  lastMac = p.getString("last", "0");
  p.end();
}

static void saveState() {
  Preferences p;
  p.begin("pcamlog", false);
  p.putUInt("next", nextLine);
  p.putString("last", lastMac);
  p.end();
}

void logBegin() {
  storageBegin();
  fsOk = storageTotal() > 0;
  if (!fsOk) Serial.println("log: no storage mounted, events will not be saved");
  loadState();
}

uint32_t logNextLine() { return nextLine; }

void logEvent(const char* event, const String& detail) {
  String d = detail;
  d.replace("|", "/");
  d.replace("\n", " ");
  String body = String(nextLine) + "|" + String((uint32_t)time(nullptr)) + "|" +
                String(millis() / 1000) + "|" + event + "|" + d + "|" + lastMac;
  String mac = hmacHex(body);
  String line = body + "|" + mac;

  if (fsOk) {
    File f = storageFs().open(LOG, "a");
    if (f) {
      f.println(line);
      size_t size = f.size();
      f.close();
      if (size > LOG_ROTATE_BYTES) {
        storageFs().remove(OLD);
        storageFs().rename(LOG, OLD);
      }
    }
  }
  lastMac = mac;
  nextLine++;
  saveState();
  Serial.println("event " + line);
}

void logForEach(void (*cb)(const String&, void*), void* ctx) {
  if (!fsOk) return;
  for (const char* path : {OLD, LOG}) {
    File f = storageFs().open(path, "r");
    if (!f) continue;
    while (f.available()) {
      String l = f.readStringUntil('\n');
      l.trim();
      if (l.length()) cb(l, ctx);
    }
    f.close();
  }
}

void logTail(size_t n, void (*out)(const String&, void*), void* ctx) {
  if (n == 0) return;
  struct Ring { String* lines; size_t n, count; } r{new String[n], n, 0};
  logForEach([](const String& l, void* c) {
    Ring* r = (Ring*)c;
    r->lines[r->count++ % r->n] = l;
  }, &r);
  size_t start = r.count > n ? r.count - n : 0;
  for (size_t i = start; i < r.count; i++) out(r.lines[i % n], ctx);
  delete[] r.lines;
}

// Splits "a|b|c" into at most 7 fields. Returns the field count.
static int split(const String& line, String f[7]) {
  int count = 0, from = 0;
  while (count < 7) {
    int bar = line.indexOf('|', from);
    if (bar < 0 || count == 6) { f[count++] = line.substring(from); break; }
    f[count++] = line.substring(from, bar);
    from = bar + 1;
  }
  return count;
}

VerifyResult logVerify() {
  struct Ctx { VerifyResult r; uint32_t prevN; String prevMac; } c{};
  logForEach([](const String& line, void* p) {
    Ctx* c = (Ctx*)p;
    VerifyResult& r = c->r;
    String f[7];
    auto issue = [&](const String& what) {
      r.issues++;
      if (r.report.length() < 3000) r.report += what + "\n";
    };
    if (split(line, f) != 7) { issue("unreadable line: " + line.substring(0, 40)); return; }
    uint32_t n = f[0].toInt();
    String body = line.substring(0, line.lastIndexOf('|'));
    if (hmacHex(body) != f[6]) issue("line " + f[0] + ": signature does not match (edited or not from this device)");
    if (r.lines == 0) {
      r.first = n;
      if (n != 1) r.startsMidChain = true;
      else if (f[5] != "0") issue("line 1 does not start the chain");
    } else {
      if (n != c->prevN + 1) issue("lines " + String(c->prevN + 1) + "-" + String(n - 1) + " are missing");
      if (f[5] != c->prevMac) issue("line " + f[0] + " does not follow line " + String(c->prevN));
    }
    c->prevN = n;
    c->prevMac = f[6];
    r.last = n;
    r.lines++;
  }, &c);
  if (c.r.lines == 0 && nextLine > 1) {
    c.r.issues++;
    c.r.report += "log is empty but the device has written " + String(nextLine - 1) + " line(s)\n";
  } else if (c.r.lines && c.r.last != nextLine - 1) {
    c.r.issues++;
    c.r.report += "device has written up to line " + String(nextLine - 1) + " but the log ends at " +
                  String(c.r.last) + "\n";
  }
  return c.r;
}

void logClearAll() {
  if (fsOk) {
    storageFs().remove(LOG);
    storageFs().remove(OLD);
  }
  Preferences p;
  p.begin("pcamlog", false);
  p.clear();
  p.end();
  loadState();
}
