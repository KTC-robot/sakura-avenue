/**
 * @file chassis.hpp
 * @brief 運動学・RPM制限・slew・PIを結合し、4輪の電流指令を生成する。
 */
#pragma once
#include "control/omni.hpp"
#include "control/speed_controller.hpp"
#include <array>
#include <cstdint>
namespace sakura {
/**
 * @brief 速度[m/s・rad/s]、RPM上限、slew[RPM/s]、deadzoneの設定。
 */
struct Limits {
  float vx, vy, wz, max_motor_rpm, slew_rpm_s, deadzone;
};
/**
 * @brief 運動学・RPM制限・slew・PIを結合する。
 */
class Chassis {
  Omni omni_;
  Limits limits_;
  Four target_{};
  std::array<SpeedController, 4> pi_{};

public:
  /**
   * @brief 幾何と制御上限を検証する。
   * @throws std::invalid_argument 上限・deadzoneが無効な場合。
   */
  Chassis(std::array<WheelConfig, 4> wheels, Limits limits);
  /**
   * @brief 全輪のPIゲインを変更する(ベンチ調整用)。
   */
  void set_gains(float kp, float ki);
  /**
   * @brief 目標RPMと全PI積分をリセットする。
   */
  void reset();
  /**
   * @brief 制限後の目標RPMを取得する。
   * @return motor_index順のモーター軸RPM。
   */
  Four targets() const { return target_; }
  /**
   * @brief 走行要求から電流指令を計算する。
   * @param cmd 正規化要求（各軸±1）。
   * @param measured モーター軸RPM。
   * @param dt 制御間隔[s]。
   * @param allowed falseなら即時リセットする。
   * @return motor_index順のC620電流指令。
   */
  std::array<int16_t, 4> update(DriveCommand cmd, Four measured, float dt,
                                bool allowed);
};
} // namespace sakura
