/**
 * @file c620.hpp
 * @brief 夏のC620フレーム仕様とフィードバック監視をTWAIから分離する。
 */
#pragma once
#include <array>
#include <cstdint>
namespace sakura {
constexpr uint32_t c620_command_id = 0x200;
/// C620フィードバックのID範囲(モーターID1〜4)。
constexpr uint32_t c620_feedback_id_first = 0x201;
constexpr int c620_motor_count = 4;
constexpr uint32_t c620_feedback_id_last =
    c620_feedback_id_first + c620_motor_count - 1;
/// C620生指令値の参考上限(±)。
constexpr int c620_current_limit = 3000;
/// Classic CANフレームのデータ長。
constexpr uint8_t can_data_size = 8;
/// フィードバック受信の期限既定値[ms]。
constexpr uint64_t c620_feedback_timeout_ms = 100;
/**
 * @brief Classic CANの8バイトフレーム。
 */
struct CanFrame {
  uint32_t id = 0;
  uint8_t size = 0;
  bool extended = false, remote = false, fd = false;
  std::array<uint8_t, 8> data{};
};
/**
 * @brief 電流指令を0x200フレームへ格納する。
 * @param currents C620生指令値（Aではない）。
 * @return 参考上限±3000を適用したフレーム。
 */
CanFrame current_frame(std::array<int16_t, 4> currents);
/**
 * @brief ID1〜4のフィードバックと受信時刻を保持する。
 */
class Feedback {
  std::array<uint64_t, 4> time_{};
  std::array<bool, 4> seen_{};

public:
  std::array<int16_t, 4> rpm{}, current{};
  std::array<uint16_t, 4> angle{};
  std::array<uint8_t, 4> temperature{};
  /**
   * @brief C620フィードバックを更新する。
   * @param now 単調時計[ms]。
   * @return 有効なフレームならtrue。
   */
  bool receive(const CanFrame &f, uint64_t now);
  /**
   * @brief 全モーターの受信期限を検査する。
   * @param now 単調時計[ms]。
   * @return 4台すべて受信済みかつ期限内ならtrue。
   */
  bool fresh(uint64_t now,
             uint64_t timeout_ms = c620_feedback_timeout_ms) const;
};
} // namespace sakura
