#include "control/drive_command.hpp"
#include <cmath>
namespace sakura {
namespace {
constexpr float axis_limit = 1.0f; ///< 正規化要求の絶対値上限。
} // namespace
bool neutral(DriveCommand c) { return c.vx == 0 && c.vy == 0 && c.wz == 0; }
bool valid(DriveCommand c) {
  for (float v : {c.vx, c.vy, c.wz})
    if (!std::isfinite(v) || std::abs(v) > axis_limit)
      return false;
  return true;
}
} // namespace sakura
