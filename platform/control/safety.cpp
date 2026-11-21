#include "control/safety.hpp"
namespace sakura {
namespace {
/// 連番の差がこれ以上なら「古い/巻き戻り」とみなす(32bit循環連番の半分)。
constexpr uint32_t sequence_half_range = 0x80000000U;
} // namespace
void Safety::fault() {
  state = State::Fault;
  neutral_ = false;
}
void Safety::tick(uint64_t now, bool healthy) {
  if (!healthy || (have_ && (now < last_ || now - last_ > timeout_ms)))
    fault();
}
bool Safety::accept(const Packet &p, uint64_t now, bool healthy) {
  tick(now, healthy);
  uint32_t delta = p.sequence - seq_;
  if (!valid(p.command) || static_cast<unsigned>(p.action) > action_max ||
      (p.action != Action::Drive && !neutral(p.command)) ||
      (have_ && (now < last_ || delta == 0 || delta >= sequence_half_range))) {
    fault();
    return false;
  }
  have_ = true;
  seq_ = p.sequence;
  last_ = now;
  if (!healthy) {
    fault();
    return false;
  }
  if (p.action == Action::Disarm) {
    state = State::Disarm;
    neutral_ = neutral(p.command);
  } else if (p.action == Action::Stop) {
    state = State::Stop;
    neutral_ = false;
  } else if (p.action == Action::Arm && state == State::Disarm && neutral_ &&
             neutral(p.command)) {
    state = State::Armed;
    neutral_ = false;
  } else if (state != State::Armed)
    neutral_ = false;
  return state == State::Armed;
}
} // namespace sakura
