#pragma once
#include <cstdint>
// SUMMER REFERENCE ONLY: not tuned or authorized for spring hardware.
constexpr int16_t MAX_CURRENT_COMMAND = 3000;
constexpr float SPEED_KP = 1.0f;
constexpr float SPEED_KI = 0.20f;
constexpr float PID_INTEGRAL_LIMIT = 8000.0f;
