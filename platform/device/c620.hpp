/**
 * @file c620.hpp
 * @brief 夏のC620フレーム仕様とフィードバック監視をTWAIから分離する。
 */
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
namespace sakura {
constexpr uint32_t c620_command_id = 0x200;
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
inline CanFrame current_frame(std::array<int16_t, 4> currents) {
  CanFrame f;
  f.id = c620_command_id;
  f.size = 8;
  for (int i = 0; i < 4; i++) {
    int16_t c = std::clamp<int>(currents[i], -3000, 3000);
    uint16_t u = static_cast<uint16_t>(c);
    f.data[2 * i] = u >> 8;
    f.data[2 * i + 1] = u & 255;
  }
  return f;
}
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
  bool receive(const CanFrame &f, uint64_t now) {
    if (f.extended || f.remote || f.fd || f.size != 8 || f.id < 0x201 ||
        f.id > 0x204)
      return false;
    int i = f.id - 0x201;
    auto u = [&](int p) {
      return uint16_t(uint16_t(f.data[p]) << 8 | f.data[p + 1]);
    };
    angle[i] = u(0);
    rpm[i] = static_cast<int16_t>(u(2));
    current[i] = static_cast<int16_t>(u(4));
    temperature[i] = f.data[6];
    time_[i] = now;
    seen_[i] = true;
    return true;
  }
  /**
   * @brief 全モーターの受信期限を検査する。
   * @param now 単調時計[ms]。
   * @return 4台すべて受信済みかつ期限内ならtrue。
   */
  bool fresh(uint64_t now, uint64_t timeout_ms = 100) const {
    for (int i = 0; i < 4; i++)
      if (!seen_[i] || now < time_[i] || now - time_[i] > timeout_ms)
        return false;
    return true;
  }
};
} // namespace sakura
