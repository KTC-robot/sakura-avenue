#include "device/c620.hpp"
#include <algorithm>
namespace sakura {
CanFrame current_frame(std::array<int16_t, 4> currents) {
  CanFrame f;
  f.id = c620_command_id;
  f.size = can_data_size;
  for (int i = 0; i < 4; i++) {
    int16_t c =
        std::clamp<int>(currents[i], -c620_current_limit, c620_current_limit);
    uint16_t u = static_cast<uint16_t>(c);
    f.data[2 * i] = u >> 8;
    f.data[2 * i + 1] = u & 255;
  }
  return f;
}
bool Feedback::receive(const CanFrame &f, uint64_t now) {
  if (f.extended || f.remote || f.fd || f.size != can_data_size ||
      f.id < c620_feedback_id_first || f.id > c620_feedback_id_last)
    return false;
  int i = f.id - c620_feedback_id_first;
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
bool Feedback::fresh(uint64_t now, uint64_t timeout_ms) const {
  for (int i = 0; i < 4; i++)
    if (!seen_[i] || now < time_[i] || now - time_[i] > timeout_ms)
      return false;
  return true;
}
} // namespace sakura
