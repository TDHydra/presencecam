// Extra jobs for the board's two buttons, on top of what they do in hardware.
//
// BOOT (GPIO0) still puts the chip in download mode when held during reset or power-up.
// Once the firmware runs, it reads these gestures (actions set in the hardware config,
// "buttons": {...}; defaults shown):
//   short press   "short"  next   - next screen page
//   double press  "double" dim    - screen dim / bright
//   hold 3 s      "hold3"  info   - address + QR code for the web page
//   hold 10 s     "hold10" forget_wifi - forget Wi-Fi and restart into setup
// Other actions: none, setup_ap (open the setup network for 10 min), reboot.
//
// RST (EN) cuts power to the chip, so the firmware can't read it; it always resets. Quick
// taps are counted across restarts instead (each within ~3 s of the last):
//   2 taps  open the setup network for 10 minutes
//   3 taps  safe mode for this boot: built-in default hardware config, no sensors
#pragma once
#include <Arduino.h>

// Call first thing in setup(). Returns the number of quick RST taps so far (1 = normal boot).
int buttonsCountResetTaps();
void buttonsBegin();
void buttonsLoop();
