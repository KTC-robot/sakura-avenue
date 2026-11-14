/**
 * @file nvs_param_store.hpp
 * @brief PIゲインをNVSへ保存・読込する。ESP-IDF専用でホストテスト対象外。
 */
#pragma once
#include "param_store.hpp"
namespace sakura {
/**
 * @brief NVSのblob（magic+version+CRC付き）でPIゲインを保持する。
 * 破損・版違いは読込失敗として扱い、呼び出し側が既定値を使う。
 */
class NvsParamStore : public ParamStore {
public:
  /// @brief NVSを初期化する。必要ならパーティションを消去して再初期化する。
  bool begin();
  bool load(SpeedGains &g) override;
  bool save(const SpeedGains &g) override;
  bool erase() override;
};
} // namespace sakura
