/**
 * @file protocol.hpp
 * @brief protocol/specification.mdの20バイトUDP v1を符号化・検証する。
 */
#pragma once
#include "control/safety.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
namespace sakura {
constexpr size_t packet_size = 20;
/// 先頭バイト(マジック)とバージョン。コマンドは"SA"、状態通知は"ST"。
constexpr uint8_t packet_magic0 = 'S', command_magic1 = 'A',
                  status_magic1 = 'T', protocol_version = 1;
/// コマンドパケットのバイト位置。
constexpr size_t offset_action = 3, offset_sequence = 4, offset_values = 8;
constexpr size_t value_count = 3, value_size = 4; ///< vx, vy, wz のfloat32。
/// 状態通知パケットのバイト位置。
constexpr size_t offset_status_state = 3, offset_status_flags = 4,
                 offset_status_reserved = 5, offset_status_counter = 6;
uint32_t read32(const uint8_t *b);
void write32(uint8_t *b, uint32_t v);
/**
 * @brief 受信bytesを検証してPacketへ変換する。
 * @return 有効ならtrue。falseの場合は出力を使用しない。
 */
bool decode(const uint8_t *b, size_t n, Packet &p);
/**
 * @brief Packetをネットワークバイト順へ符号化する。
 * @return 仕様に適合する場合true。
 */
bool encode(const Packet &p, std::array<uint8_t, packet_size> &b);
constexpr size_t status_size = 10;
constexpr uint8_t status_wifi = 1, status_can = 2, status_feedback = 4,
                  status_interlock = 8;
/**
 * @brief 受信側の状態をハートビート用の10バイトへ符号化する。
 * @param flags status_* のビット和。
 * @param counter 送信ごとに増える連番。
 */
void encode_status(State state, uint8_t flags, uint32_t counter,
                   std::array<uint8_t, status_size> &b);
} // namespace sakura
