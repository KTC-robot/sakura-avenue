#include "network/control_loop.hpp"
#include <cmath>
#include <stdexcept>
namespace sakura {
namespace {
constexpr float max_dt_s = 0.05f; ///< 制御間隔の上限[s]。
} // namespace
void ControlLoop::transport_fault() {
  receiver_.safety.fault();
  receiver_.demand = {};
  chassis_.reset();
}
CanFrame ControlLoop::step(uint64_t now, float dt, bool wifi_ok, bool can_ok,
                           bool interlock_ok) {
  bool healthy = wifi_ok && can_ok && interlock_ok && feedback_.fresh(now) &&
                 std::isfinite(dt) && dt > 0 && dt <= max_dt_s;
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
} // namespace sakura
