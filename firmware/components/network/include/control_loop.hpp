/**
 * @file control_loop.hpp
 * @brief 受信監視、フィードバック、PIリセットとゼロ電流生成を結合する。
 */
#pragma once
#include "c620.hpp"
#include "receiver.hpp"
namespace sakura {
/**
 * @brief 安全監視と電流指令の生成を結合する。
 * @note 返却フレームの送信と電源遮断は呼び出し側が行う。
 */
class ControlLoop {
  Receiver &receiver_;
  Chassis &chassis_;
  Feedback &feedback_;

public:
  ControlLoop(Receiver &receiver, Chassis &chassis, Feedback &feedback)
      : receiver_(receiver), chassis_(chassis), feedback_(feedback) {}
  /**
   * @brief FAULTへ移り、要求値とPIをリセットする。
   */
  void transport_fault() {
    receiver_.safety.fault();
    receiver_.demand = {};
    chassis_.reset();
  }
  /**
   * @brief 1制御周期を実行する。
   * @param now 単調時計[ms]。
   * @param dt 制御間隔[s]（0より大きく50ms以下）。
   * @param wifi_ok Wi-Fi/IPの健全性。
   * @param can_ok TWAIの健全性。
   * @param interlock_ok 物理駆動許可。
   * @return C620電流フレーム。停止・異常時はゼロ電流。
   */
  CanFrame step(uint64_t now, float dt, bool wifi_ok, bool can_ok,
                bool interlock_ok) {
    bool healthy = wifi_ok && can_ok && interlock_ok && feedback_.fresh(now) &&
                   std::isfinite(dt) && dt > 0 && dt <= 0.05f;
    receiver_.tick(now, healthy);
    Four measured{};
    for (int i = 0; i < 4; i++)
      measured[i] = feedback_.rpm[i];
    bool allowed = healthy && receiver_.safety.state == State::Armed;
    try {
      auto currents = chassis_.update(receiver_.demand, measured, dt, allowed);
      return current_frame(currents);
    } catch (const std::invalid_argument &) {
      transport_fault();
      return current_frame({});
    }
  }
};
} // namespace sakura
