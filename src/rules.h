// Rules: "when a reading crosses a value, do something". Stored in NVS as a JSON array:
//   [{"when":"Distance","op":"<","value":100,"do":"photo","every":30},
//    {"when":"presence","op":"is","value":1,"do":"led"}]
// "when" is a reading label (as shown on the web page) or "presence"; "op" is "<", ">" or "is"
// (booleans: value 1 = yes); "do" is "photo" (take a picture), "led" (status LED on while
// true) or "log" (an event log line); "every" is the least seconds between two photos/log
// lines of the same rule. A rule fires when its condition becomes true (not on every loop).
#pragma once
#include <Arduino.h>

const int MAX_RULES = 8;

void rulesBegin();
void rulesLoop();
String rulesJson();                  // the stored rules
String rulesSet(const String& json);  // validates and stores; "" on success, else what's wrong
String rulesStatus();                 // one line per rule: "Distance < 100 -> photo: fired 3x, last 12 s ago"
