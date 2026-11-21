// Adapted from KTC-robot/UnagiFestival @ 42c839d; see docs/migration.md.
#pragma once

/**
 * @file speed_controller.hpp
 * @brief 1台のmotorに対するPI速度制御と積分状態を定義する。
 */

#include "control/reference_gains.hpp"

#include <cstdint>

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
   * @brief PIゲインを変更する(ベンチ調整用)。既定は reference_gains.hpp の値。
   * @param kp 比例ゲイン[カウント/RPM]。
   * @param ki 積分ゲイン[カウント/(RPM・s)]。
   */
  void setGains(float kp, float ki);

private:
  float kp_ = SPEED_KP;   ///< 比例ゲイン。
  float ki_ = SPEED_KI;   ///< 積分ゲイン。
  float integral_ = 0.0f; ///< 定常偏差を補うために保持する誤差の時間積分値。
};
