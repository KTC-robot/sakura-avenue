#include "VL53L0X.hpp"

#include <Wire.h>
#include <VL53L0X.h>

VL53L0X sensor[2];

bool initVL53L0X(uint8_t channel) {
    if (channel > 1) {
        return false;
    }

    if (selectChannel(channel) == false) {
        return false;
    }

    sensor[channel].setTimeout(500);

    return sensor[channel].init();
}

bool readDistance(uint8_t channel, uint16_t& distance) {
    if (channel > 1) {
        return false;
    }

    if (selectChannel(channel) == false) {
        return false;
    }

    distance = sensor[channel].readRangeSingleMillimeters();
    
    if (sensor[channel].timeoutOccurred()) {
        return false;
    }

    return true;
}