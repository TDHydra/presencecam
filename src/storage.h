// Where files live: the microSD card when the board has a slot (hw["sd"]) and a card is in,
// otherwise the flash's LittleFS partition. The event log, photos and the debug log all go
// through storageFs(), so they move to the card by themselves.
//
// hw["sd"]: {"type":"spi","cs":4,"sck":7,"mosi":6,"miso":5}  or
//           {"type":"sdmmc","clk":43,"cmd":44,"d0":39,"d1":40,"d2":41,"d3":42,"width":4,"power":45,"powerLevel":0}
//           (width 1 uses clk/cmd/d0 only; "power"/"powerLevel" (optional): a GPIO that switches the
//           slot's supply, driven to powerLevel before mounting)
#pragma once
#include <Arduino.h>
#include <FS.h>

void storageBegin();        // mounts LittleFS (always) and the SD card (when configured and present)
fs::FS& storageFs();        // the card when mounted, else LittleFS
bool storageOnSd();
String storageDescribe();   // "microSD 7.4 GB, 3.1 GB free" / "flash 128 KB, 40 KB free" / "microSD: no card"
uint64_t storageFree();     // bytes
uint64_t storageTotal();
void storageLoop();         // notices a card going away (then falls back to flash) — cheap, call every loop
