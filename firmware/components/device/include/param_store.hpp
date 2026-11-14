/**
 * @file param_store.hpp
 * @brief PIゲイン永続化先の抽象。
 */
#pragma once
#include "speed_controller.hpp"
namespace sakura {
/**
 * @brief PIゲインの永続化先。実機はNVS、ホストテストはモック。
 */
class ParamStore {
public:
  virtual ~ParamStore() = default;
  /// @return 保存値があり読めたらtrue（値の検証は呼び出し側）。
  virtual bool load(SpeedGains &g) = 0;
  virtual bool save(const SpeedGains &g) = 0;
  virtual bool erase() = 0;
};
} // namespace sakura
