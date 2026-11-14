/**
 * @file protocol.hpp
 * @brief protocol/specification.mdの20バイトUDP v1を符号化・検証する。
 */
#pragma once
#include "control/control.hpp"
#include <cstring>
#include <limits>
namespace sakura {
constexpr size_t packet_size = 20;
inline uint32_t read32(const uint8_t *b) {
  return uint32_t(b[0]) << 24 | uint32_t(b[1]) << 16 | uint32_t(b[2]) << 8 |
         b[3];
}
inline void write32(uint8_t *b, uint32_t v) {
  for (int i = 3; i >= 0; i--) {
    b[i] = v & 255;
    v >>= 8;
  }
}
/**
 * @brief 受信bytesを検証してPacketへ変換する。
 * @return 有効ならtrue。falseの場合は出力を使用しない。
 */
inline bool decode(const uint8_t *b, size_t n, Packet &p) {
  if (n != packet_size || b[0] != 'S' || b[1] != 'A' || b[2] != 1 || b[3] > 3)
    return false;
  p.sequence = read32(b + 4);
  p.action = static_cast<Action>(b[3]);
  float *values[] = {&p.command.vx, &p.command.vy, &p.command.wz};
  static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
                "IEEE754 binary32 required");
  for (int i = 0; i < 3; i++) {
    uint32_t bits = read32(b + 8 + 4 * i);
    std::memcpy(values[i], &bits, 4);
  }
  return valid(p.command) && (p.action == Action::Drive || neutral(p.command));
}
/**
 * @brief Packetをネットワークバイト順へ符号化する。
 * @return 仕様に適合する場合true。
 */
inline bool encode(const Packet &p, std::array<uint8_t, packet_size> &b) {
  if (!valid(p.command) || static_cast<unsigned>(p.action) > 3 ||
      (p.action != Action::Drive && !neutral(p.command)))
    return false;
  b[0] = 'S';
  b[1] = 'A';
  b[2] = 1;
  b[3] = static_cast<uint8_t>(p.action);
  write32(b.data() + 4, p.sequence);
  float values[] = {p.command.vx, p.command.vy, p.command.wz};
  for (int i = 0; i < 3; i++) {
    uint32_t bits;
    std::memcpy(&bits, &values[i], 4);
    write32(b.data() + 8 + 4 * i, bits);
  }
  return true;
}
} // namespace sakura
