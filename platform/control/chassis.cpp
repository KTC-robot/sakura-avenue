#include "control/chassis.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace sakura {
Chassis::Chassis(std::array<WheelConfig, 4> wheels, Limits limits)
    : omni_(wheels), limits_(limits) {
  for (float v : {limits.vx, limits.vy, limits.wz, limits.max_motor_rpm,
                  limits.slew_rpm_s})
    if (!std::isfinite(v) || v <= 0)
      throw std::invalid_argument("unconfigured limits");
  if (!std::isfinite(limits.deadzone) || limits.deadzone < 0 ||
      limits.deadzone >= 1)
    throw std::invalid_argument("deadzone");
}
void Chassis::set_gains(float kp, float ki) {
  for (auto &p : pi_)
    p.setGains(kp, ki);
}
void Chassis::reset() {
  target_.fill(0);
  for (auto &p : pi_)
    p.reset();
}
std::array<int16_t, 4> Chassis::update(DriveCommand cmd, Four measured,
                                       float dt, bool allowed) {
  std::array<int16_t, 4> currents{};
  if (!allowed || !valid(cmd) || !std::isfinite(dt) || dt <= 0) {
    reset();
    return currents;
  }
  for (float v : measured)
    if (!std::isfinite(v)) {
      reset();
      return currents;
    }
  auto demand = [&](float v, float max) {
    return std::abs(v) < limits_.deadzone ? 0.0f : v * max;
  };
  auto rpm =
      omni_.motor_rpm({demand(cmd.vx, limits_.vx), demand(cmd.vy, limits_.vy),
                       demand(cmd.wz, limits_.wz)});
  float peak = 0;
  for (float v : rpm)
    peak = std::max(peak, std::abs(v));
  float scale = peak > limits_.max_motor_rpm ? limits_.max_motor_rpm / peak : 1;
  for (int i = 0; i < 4; i++) {
    float step = limits_.slew_rpm_s * dt;
    target_[i] += std::clamp(rpm[i] * scale - target_[i], -step, step);
    currents[i] = pi_[i].update(target_[i], measured[i], dt).currentCommand;
  }
  return currents;
}
} // namespace sakura
