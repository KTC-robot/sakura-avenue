// Adapted from KTC-robot/UnagiFestival @ 42c839d; see docs/migration.md.
#include "speed_controller.hpp"

#include <algorithm>
#include <cmath>

SpeedControllerResult SpeedController::update(float targetRpm, float actualRpm,
                                              float dt) {
  if (!std::isfinite(targetRpm) || !std::isfinite(actualRpm) ||
      !std::isfinite(dt) || dt <= 0.0f) {
    reset();
    return {};
  }
  // RPM差を電流指令へ変換する。P項は現在の誤差へ即座に反応し、
  // I項は残り続ける定常偏差を時間とともに補う。
  const float error = targetRpm - actualRpm;
  const float integralCandidate = std::clamp(
      integral_ + error * dt, -gains_.integral_limit, gains_.integral_limit);
  const float outputCandidate = gains_.kp * error + gains_.ki * integralCandidate;
  const bool saturatedHigh =
      outputCandidate >= gains_.max_current;
  const bool saturatedLow =
      outputCandidate <= -gains_.max_current;
  // 出力上限へ張り付いている間は、飽和を悪化させる向きの積分を止める。
  // 飽和から戻す向きの誤差は許可し、復帰を妨げない。
  const bool allowIntegral = (!saturatedHigh && !saturatedLow) ||
                             (saturatedHigh && error < 0.0f) ||
                             (saturatedLow && error > 0.0f);

  if (allowIntegral) {
    integral_ = integralCandidate;
  }

  const float output = gains_.kp * error + gains_.ki * integral_;
  // 最後にC620へ渡せる電流指令範囲へ制限する。
  const float limitedOutput =
      std::clamp(output, -gains_.max_current,
                 gains_.max_current);

  return {static_cast<int16_t>(std::lround(limitedOutput)), error, integral_,
          output};
}

void SpeedController::reset() { integral_ = 0.0f; }
