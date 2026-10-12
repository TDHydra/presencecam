Arducam_Mega 2.0.9 (https://github.com/dennis-ard/Arducam_Mega, MIT), vendored because the
registry copy doesn't compile on Arduino core 3: `return SPI.begin();` and
`return SPI.transfer(buff, len);` return a value from a void function (src/Arducam/ArducamSpi.cpp).
Those two lines are the only change. Examples and the UNO update scripts were dropped.
