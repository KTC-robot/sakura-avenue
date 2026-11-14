// Adapted from KTC-robot/UnagiFestival @ 42c839d; see docs/migration.md.
#pragma once

/**
 * @file speed_controller.hpp
 * @brief 1台のmotorに対するPI速度制御と積分状態を定義する。
 */

#include "reference_gains.hpp"
#include <cmath>
#include <cstdint>

/**
 * @brief 実行時に変更できるPIゲインと電流上限。
 * 既定値は夏の参考値（reference_gains.hpp）。実機未調整。
 */
struct SpeedGains {
  float kp = SPEED_KP;                ///< 比例ゲイン。
  float ki = SPEED_KI;                ///< 積分ゲイン。
  float integral_limit = PID_INTEGRAL_LIMIT; ///< 積分値の絶対上限。
  float max_current = MAX_CURRENT_COMMAND;   ///< C620電流指令の絶対上限。
};
/// 実行時設定でも超えられないコンパイル時上限。max_currentはC620の±3000以下。
constexpr float KP_CEILING = 10.0f;
constexpr float KI_CEILING = 5.0f;
constexpr float INTEGRAL_LIMIT_CEILING = PID_INTEGRAL_LIMIT;
constexpr float CURRENT_CEILING = MAX_CURRENT_COMMAND;
/**
 * @brief ゲインが有限で0以上かつコンパイル時上限以内か検査する。
 */
inline bool gains_valid(const SpeedGains &g) {
  auto in = [](float v, float hi) { return std::isfinite(v) && v >= 0 && v <= hi; };
  return in(g.kp, KP_CEILING) && in(g.ki, KI_CEILING) &&
         in(g.integral_limit, INTEGRAL_LIMIT_CEILING) &&
         in(g.max_current, CURRENT_CEILING);
}
inline bool operator==(const SpeedGains &a, const SpeedGains &b) {
  return a.kp == b.kp && a.ki == b.ki && a.integral_limit == b.integral_limit &&
         a.max_current == b.max_current;
}

/**
 * @brief 1回のPI速度制御計算結果。
 */
struct SpeedControllerResult {
  int16_t currentCommand; ///< C620へ渡すcurrent limit適用後の電流指令。
  float error;            ///< 目標RPMと実測RPMの差。
  float integral;         ///< anti-windup適用後の積分値。
  float output;           ///< current limit適用前のPI出力。
};

/**
 * @brief 1motor分の積分状態を保持するPI速度Controller。
 * CANや時刻を管理せず、外部から渡されたRPMとdtだけで電流指令を計算する。
 */
class SpeedController {
public:
  /**
   * @brief 目標RPMと実測RPMから電流指令を計算する。
   * @param targetRpm 目標RPM。
   * @param actualRpm 実測RPM。
   * @param dt 前回更新からの経過時間[s]。
   * @return PI制御の計算結果。
   */
  SpeedControllerResult update(float targetRpm, float actualRpm, float dt);

  /**
   * @brief 積分状態を0へ戻す。
   */
  void reset();

  /**
   * @brief ゲインを差し替える。積分状態は呼び出し側がresetする。
   * @return 検証に通り適用したらtrue。
   */
  bool set_gains(const SpeedGains &g) {
    if (!gains_valid(g))
      return false;
    gains_ = g;
    return true;
  }
  const SpeedGains &gains() const { return gains_; }

private:
  SpeedGains gains_{};
  float integral_ = 0.0f; ///< 定常偏差を補うために保持する誤差の時間積分値。
};
