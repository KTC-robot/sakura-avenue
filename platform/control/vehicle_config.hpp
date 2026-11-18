/**
 * @file vehicle_config.hpp
 * @brief 実機の車体寸法・車輪とC620のID配置・速度上限を1か所に定義する。
 *
 * 本番ファームとベンチで共有する。値は実測/確認済みのもののみ。
 * 取り付け符号は実機の回転方向確認後に更新する。
 */
#pragma once
#include "control/control.hpp"
#include <cmath>

namespace sakura::vehicle {
/// CAN(TWAI)のGPIO。外付けトランシーバー(WCMCU-1051)へ接続。
constexpr int kCanTxGpio = 23;
constexpr int kCanRxGpio = 24;
/// 物理インターロック入力のGPIO。-1は未配線(駆動電源の物理遮断のみに依存)。
constexpr int kInterlockGpio = -1;
/// trueなら入力がHIGHで駆動許可、falseならLOWで許可。
constexpr bool kInterlockActiveHigh = true;
constexpr float kPi = 3.14159265358979f;
constexpr float kCenterToWheel = 0.25f; ///< 車体中心から各輪まで[m]
constexpr float kWheelRadius = 0.05f;   ///< 車輪半径[m](直径100mm)
constexpr float kGearRatio = 19.0f;     ///< M3508 P19
/**
 * @brief 輪の位置角[deg]、C620のID(1〜4)、取り付け符号。
 * 上から見て前=+X、左=+Y、反時計回りが正。
 *   前左=ID3  前右=ID1
 *   後左=ID2  後右=ID4
 */
struct Slot {
  float angle_deg;
  uint8_t id;
  int8_t sign;
};
constexpr Slot kSlots[4] = {
    {135.0f, 3, +1}, // 前左
    {45.0f, 1, +1},  // 前右
    {225.0f, 2, +1}, // 後左
    {315.0f, 4, +1}, // 後右
};
constexpr const char *kSlotNames[4] = {"前左", "前右", "後左", "後右"};
/**
 * @brief 正規化要求1.0あたりの車体速度とRPM/slew/deadzone。
 * 初期の低速値。正規化1.0でモーター約300RPMに収まる。実走行前に見直す。
 */
constexpr Limits kLimits = {0.08f, 0.08f, 0.32f, 300.0f, 600.0f, 0.05f};
/// 電流指令(C620生値)の上限。参考上限3000の半分。
constexpr int16_t kMaxCurrent = 1500;
/// 実測がこれを超えたらFAULT[RPM]。
constexpr float kOverspeedRpm = 500.0f;

/**
 * @brief kSlotsから車輪設定を生成する。駆動方向は接線(位置角+90度)。
 */
inline std::array<WheelConfig, 4> wheels() {
  std::array<WheelConfig, 4> w{};
  for (int i = 0; i < 4; i++) {
    float phi = kSlots[i].angle_deg * kPi / 180.0f;
    w[i] = {kCenterToWheel * std::cos(phi), kCenterToWheel * std::sin(phi),
            phi + kPi / 2.0f, kWheelRadius, kGearRatio, kSlots[i].sign,
            static_cast<uint8_t>(kSlots[i].id - 1)};
  }
  return w;
}
} // namespace sakura::vehicle
