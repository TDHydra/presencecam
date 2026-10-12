// Internal header for the sensor modules.
#pragma once
#include "hwconfig.h"
#include "sensor.h"

Sensor* makeLD2410();
Sensor* makeVL53L0X();
Sensor* makeVL53L1X();
Sensor* makePIR();
Sensor* makeHCSR04();
