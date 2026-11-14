#pragma once

#include <Arduino.h>

constexpr uint8_t TCA9548A_ADDRESS = 0x70;

bool selectChannel(uint8_t channel);