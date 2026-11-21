#include "network/receiver.hpp"
#include <stdexcept>
#include <utility>
namespace sakura {
Receiver::Receiver(std::string peer, uint16_t port)
    : peer_(std::move(peer)), port_(port) {
  if (peer_.empty() || port == 0)
    throw std::invalid_argument("explicit trusted peer required");
}
bool Receiver::receive(const std::string &ip, uint16_t port,
                       const uint8_t *bytes, size_t size, uint64_t now,
                       bool healthy) {
  if (ip != peer_ || port != port_)
    return false;
  Packet packet;
  if (!decode(bytes, size, packet)) {
    safety.fault();
    demand = {};
    return false;
  }
  bool armed = safety.accept(packet, now, healthy);
  demand =
      armed && packet.action == Action::Drive ? packet.command : DriveCommand{};
  return armed;
}
void Receiver::tick(uint64_t now, bool healthy) {
  safety.tick(now, healthy);
  if (safety.state != State::Armed)
    demand = {};
}
} // namespace sakura
