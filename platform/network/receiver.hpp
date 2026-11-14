/**
 * @file receiver.hpp
 * @brief 送信元、UDP形式、連番を検証して走行要求を安全状態へ反映する。
 */
#pragma once
#include "protocol/protocol.hpp"
#include <string>
namespace sakura {
/**
 * @brief 送信元と指令を検証する受信器。暗号学的認証は未実装。
 */
class Receiver {
  std::string peer_;
  uint16_t port_;

public:
  Safety safety;
  DriveCommand demand{};
  /**
   * @brief 許可する送信元IPとポートを設定する。
   */
  Receiver(std::string peer, uint16_t port) : peer_(peer), port_(port) {
    if (peer.empty() || port == 0)
      throw std::invalid_argument("explicit trusted peer required");
  }
  /**
   * @brief 受信指令を検証し、安全状態と要求値を更新する。
   * @param now 単調時計[ms]。
   * @param healthy 通信・CAN・インターロックを含む健全性。
   * @return 更新後がARMEDならtrue。
   */
  bool receive(const std::string &ip, uint16_t port, const uint8_t *bytes,
               size_t size, uint64_t now, bool healthy) {
    if (ip != peer_ || port != port_)
      return false;
    Packet packet;
    if (!decode(bytes, size, packet)) {
      safety.fault();
      demand = {};
      return false;
    }
    bool armed = safety.accept(packet, now, healthy);
    demand = armed && packet.action == Action::Drive ? packet.command
                                                     : DriveCommand{};
    return armed;
  }
  /**
   * @brief 受信期限を監視し、停止時の要求値をゼロにする。
   */
  void tick(uint64_t now, bool healthy) {
    safety.tick(now, healthy);
    if (safety.state != State::Armed)
      demand = {};
  }
};
} // namespace sakura
