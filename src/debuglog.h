// Debug log: with settings.debug on, everything written to the serial console (and a few
// extra lines: heap, sensor status, network state every 30 s) also goes to /debug.log on
// storageFs(), with a timestamp per line, so a board run away from a PC (e.g. on PoE) can
// be brought back and read. The file is capped (rotated to /debug.old) so it can't fill the
// flash: 24 KB on LittleFS, 4 MB on an SD card.
//
// Everything that includes this header writes to `Serial` as before: the macro below points
// it at a tee that forwards to the real port and copies to the log. (debuglog.cpp itself
// defines DEBUGLOG_NO_MACRO and talks to the real port.)
#pragma once
#include <Arduino.h>

class DebugTee : public Stream {
 public:
  // Stream interface, forwarded to the real serial port
  int available() override;
  int read() override;
  int peek() override;
  void flush() override;
  size_t write(uint8_t b) override;
  size_t write(const uint8_t* buf, size_t n) override;
  // The parts of HardwareSerial / HWCDC the code uses
  void begin(unsigned long baud);
  void setRxBufferSize(size_t n);
  using Print::write;
};
extern DebugTee Dbg;

void debugBegin();                 // right after Serial.begin(): starts buffering if settings.debug
void debugLoop();                  // flushes buffered lines to the file, writes the periodic lines
void debugSetEnabled(bool on);     // turns the mode on/off now and saves it in settings
bool debugEnabled();
bool debugSendLog(Print& to);      // streams /debug.old + /debug.log; false if there is none
void debugClear();
size_t debugLogBytes();

#ifndef DEBUGLOG_NO_MACRO
#define Serial Dbg
#endif
