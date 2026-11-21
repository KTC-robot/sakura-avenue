#include "control/vehicle_config.hpp"
#include <cmath>

namespace sakura::vehicle {
std::array<WheelConfig, 4> wheels() {
  std::array<WheelConfig, 4> w{};
  for (int i = 0; i < 4; i++) {
    float phi = kSlots[i].angle_deg * kPi / kHalfTurnDeg;
    w[i] = {kCenterToWheel * std::cos(phi),
            kCenterToWheel * std::sin(phi),
            phi + kPi / 2.0f,
            kWheelRadius,
            kGearRatio,
            kSlots[i].sign,
            static_cast<uint8_t>(kSlots[i].id - 1)};
  }
  return w;
}
} // namespace sakura::vehicle
