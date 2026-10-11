// Tamper-evident event log, kept from the original PresenceCam.
//
// Every event is one text line in LittleFS (/log.txt):
//
//   n|unix_time|uptime_s|event|detail|prev|mac
//
// n     line number, never reused (the counter lives in NVS, so it survives log rotation
//       and deleting the file)
// prev  the mac of the line before, so lines can't be removed or reordered unnoticed
// mac   HMAC-SHA256 (first 16 bytes, hex) of everything before it, keyed with a random
//       secret made on first boot and kept in NVS
//
// Editing, deleting or reordering lines shows up in verify. The key is in the chip's
// flash, so this protects against someone changing the downloaded log or the file on the
// device, not against someone who dumps and rewrites the whole flash.
#pragma once
#include <Arduino.h>

void logBegin();
void logEvent(const char* event, const String& detail = "");
uint32_t logNextLine();

// Calls cb(line) for every kept line, oldest first, across log.old.txt and log.txt.
void logForEach(void (*cb)(const String& line, void* ctx), void* ctx);

// Calls out(line) for the last n lines (oldest first), across log.old.txt and log.txt.
void logTail(size_t n, void (*out)(const String& line, void* ctx), void* ctx);

struct VerifyResult {
  uint32_t lines = 0;
  uint32_t first = 0, last = 0;
  bool startsMidChain = false;  // the oldest kept line isn't line 1 (older lines rotated out)
  uint32_t issues = 0;
  String report;                // one problem per line, empty if all good
};
VerifyResult logVerify();

void logClearAll();  // new key, counter back to 1 (factory reset)
