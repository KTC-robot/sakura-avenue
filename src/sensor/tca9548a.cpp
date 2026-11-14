#include "tca9548a.hpp"

#include <Wire.h>

bool selectChannel(uint8_t channel){
    if (channel > 1) {
        return false;
    }

    Wire.beginTransmission(TCA9548A_ADDRESS);
    Wire.write(1 << channel);
    byte result = Wire.endTransmission();

    if (result == 0) {
        return true;
    }else {
        return false;
    }
}