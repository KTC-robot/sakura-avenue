#include "control/omni.hpp"
#include <cmath>
#include <stdexcept>
#include <utility>
namespace sakura {
namespace {
constexpr int wheel_count = 4;
constexpr int axis_count = 3; ///< vx, vy, wz。
/// グラム行列のピボットがこれ未満ならrank不足とみなす。
constexpr double rank_epsilon = 1e-10;
constexpr float seconds_per_minute = 60.0f;
} // namespace
Omni::Omni(std::array<WheelConfig, 4> wheels) : wheels_(wheels) {
  bool seen[wheel_count] = {};
  double gram[axis_count][axis_count] = {};
  for (const auto &w : wheels_) {
    for (float v : {w.x_m, w.y_m, w.drive_angle_rad, w.radius_m, w.gear_ratio})
      if (!std::isfinite(v))
        throw std::invalid_argument("nonfinite geometry");
    if (w.radius_m <= 0 || w.gear_ratio <= 0 ||
        (w.motor_sign != 1 && w.motor_sign != -1) ||
        w.motor_index >= wheel_count || seen[w.motor_index])
      throw std::invalid_argument("invalid wheel");
    seen[w.motor_index] = true;
    double c = std::cos(w.drive_angle_rad), s = std::sin(w.drive_angle_rad);
    double row[] = {c, s, -w.y_m * c + w.x_m * s};
    for (int i = 0; i < axis_count; i++)
      for (int j = 0; j < axis_count; j++)
        gram[i][j] += row[i] * row[j];
  }
  // ピボット付き消去でrank不足を検出する。特定の車輪配置は仮定しない。
  for (int k = 0; k < axis_count; k++) {
    int pivot = k;
    for (int i = k + 1; i < axis_count; i++)
      if (std::abs(gram[i][k]) > std::abs(gram[pivot][k]))
        pivot = i;
    if (std::abs(gram[pivot][k]) < rank_epsilon)
      throw std::invalid_argument("rank deficient geometry");
    for (int j = 0; j < axis_count; j++)
      std::swap(gram[k][j], gram[pivot][j]);
    for (int i = k + 1; i < axis_count; i++) {
      double factor = gram[i][k] / gram[k][k];
      for (int j = k; j < axis_count; j++)
        gram[i][j] -= factor * gram[k][j];
    }
  }
}
Four Omni::motor_rpm(DriveCommand cmd) const {
  Four out{};
  for (float v : {cmd.vx, cmd.vy, cmd.wz})
    if (!std::isfinite(v))
      throw std::invalid_argument("nonfinite demand");
  for (const auto &w : wheels_) {
    float c = std::cos(w.drive_angle_rad), s = std::sin(w.drive_angle_rad);
    float linear = c * cmd.vx + s * cmd.vy + (-w.y_m * c + w.x_m * s) * cmd.wz;
    // 車輪rad/s→車輪RPM→モーターRPM。feedbackは既にモーター軸RPM。
    out[w.motor_index] = linear / w.radius_m * seconds_per_minute /
                         (2 * static_cast<float>(M_PI)) * w.gear_ratio *
                         w.motor_sign;
    if (!std::isfinite(out[w.motor_index]))
      throw std::invalid_argument("rpm overflow");
  }
  return out;
}
} // namespace sakura
