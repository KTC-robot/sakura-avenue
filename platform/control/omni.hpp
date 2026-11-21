/**
 * @file omni.hpp
 * @brief 配置可変の4輪オムニ逆運動学を定義する。
 */
#pragma once
#include "control/drive_command.hpp"
#include <array>
#include <cstdint>
namespace sakura {
/**
 * @brief 車輪の幾何とモーター対応。実機値は未確定。
 * 位置[m]、駆動方向[rad]、半径[m]、減速比（モーター/車輪）、符号±1、番号0〜3。
 */
struct WheelConfig {
  float x_m, y_m, drive_angle_rad, radius_m, gear_ratio;
  int8_t motor_sign;
  uint8_t motor_index;
};
/**
 * @brief 配置パラメーターに基づく4輪オムニ逆運動学。
 */
class Omni {
  std::array<WheelConfig, 4> wheels_;

public:
  /**
   * @brief 車輪設定を検証して保持する。
   * @throws std::invalid_argument 幾何・番号・rankが無効な場合。
   */
  explicit Omni(std::array<WheelConfig, 4> wheels);
  /**
   * @brief 車体速度をモーター軸RPMへ変換する。
   * @param cmd vx/vy[m/s]、wz[rad/s]。
   * @return motor_index順の目標RPM。
   */
  Four motor_rpm(DriveCommand cmd) const;
};
} // namespace sakura
