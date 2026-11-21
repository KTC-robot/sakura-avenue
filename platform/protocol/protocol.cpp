#include "protocol/protocol.hpp"
#include <cmath>
#include <cstring>
#include <limits>
namespace sakura {
uint32_t read32(const uint8_t *b) {
  return uint32_t(b[0]) << 24 | uint32_t(b[1]) << 16 | uint32_t(b[2]) << 8 |
         b[3];
}
void write32(uint8_t *b, uint32_t v) {
  for (int i = 3; i >= 0; i--) {
    b[i] = v & 255;
    v >>= 8;
  }
}
bool decode(const uint8_t *b, size_t n, Packet &p) {
  if (n != packet_size || b[0] != packet_magic0 || b[1] != command_magic1 ||
      b[2] != protocol_version || b[offset_action] > action_max)
    return false;
  p.sequence = read32(b + offset_sequence);
  p.action = static_cast<Action>(b[offset_action]);
  float *values[] = {&p.command.vx, &p.command.vy, &p.command.wz};
  static_assert(sizeof(float) == value_size &&
                    std::numeric_limits<float>::is_iec559,
                "IEEE754 binary32 required");
  for (size_t i = 0; i < value_count; i++) {
    uint32_t bits = read32(b + offset_values + value_size * i);
    std::memcpy(values[i], &bits, value_size);
  }
  return valid(p.command) && (p.action == Action::Drive || neutral(p.command));
}
bool encode(const Packet &p, std::array<uint8_t, packet_size> &b) {
  if (!valid(p.command) || static_cast<unsigned>(p.action) > action_max ||
      (p.action != Action::Drive && !neutral(p.command)))
    return false;
  b[0] = packet_magic0;
  b[1] = command_magic1;
  b[2] = protocol_version;
  b[offset_action] = static_cast<uint8_t>(p.action);
  write32(b.data() + offset_sequence, p.sequence);
  float values[] = {p.command.vx, p.command.vy, p.command.wz};
  for (size_t i = 0; i < value_count; i++) {
    uint32_t bits;
    std::memcpy(&bits, &values[i], value_size);
    write32(b.data() + offset_values + value_size * i, bits);
  }
  return true;
}
void encode_status(State state, uint8_t flags, uint32_t counter,
                   std::array<uint8_t, status_size> &b) {
  b[0] = packet_magic0;
  b[1] = status_magic1;
  b[2] = protocol_version;
  b[offset_status_state] = static_cast<uint8_t>(state);
  b[offset_status_flags] = flags;
  b[offset_status_reserved] = 0;
  write32(b.data() + offset_status_counter, counter);
}
} // namespace sakura
