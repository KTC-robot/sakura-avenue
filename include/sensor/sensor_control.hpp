#pragma once

#include <Arduino.h>

enum SensorState {
    NORMAL,
    SLOW,
    STOP
};

SensorState judgeDistance(uint16_t distance);