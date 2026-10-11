// Web page and JSON API on port 80.
//   /                 dashboard (live status, event log with verification, settings, Wi-Fi, update)
//   /api/status       live JSON
//   /api/log?n=50     last n log lines, plain text
//   /api/log.txt      the whole log as a download
//   /api/verify       checks the log chain, JSON
//   /api/settings     GET current, POST to change (form fields: name hold maxcm oledsleep oleddrv oledflip)
//   /api/wifi         POST ssid + pass
//   /api/scan         nearby networks, JSON
//   /update           POST a firmware .bin (over-the-air update)
#pragma once

void webBegin();
void webLoop();
