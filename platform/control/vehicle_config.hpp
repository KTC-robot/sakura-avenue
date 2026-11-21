/**
 * @file vehicle_config.hpp
 * @brief 実機の車体寸法・車輪とC620のID配置・速度上限を1か所に定義する。
 *
 * 本番ファームとベンチで共有する。値は実測/確認済みのもののみ。
 * 取り付け符号は実機の回転方向確認後に更新する。
 */
#pragma once
#include "control/chassis.hpp"
#include <array>
#include <cmath>
#include <cstdint>

namespace sakura::vehicle {
/// CAN(TWAI)のGPIO。外付けトランシーバー(WCMCU-1051)へ接続。
constexpr int kCanTxGpio = 23;
constexpr int kCanRxGpio = 24;
/// 物理インターロック入力のGPIO。-1は未配線(駆動電源の物理遮断のみに依存)。
constexpr int kInterlockGpio = -1;
/// trueなら入力がHIGHで駆動許可、falseならLOWで許可。
constexpr bool kInterlockActiveHigh = true;
constexpr float kPi = static_cast<float>(M_PI);
constexpr float kHalfTurnDeg = 180.0f;  ///< kPi[rad]に相当する角度[deg]。
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
 * @name 最高出力の制限
 * 正規化1.0(スティック全開)でモーターが出す速度は、ここだけで決まる。
 * 速度を変えたいときは kSpeedFraction を変える。
 * @{
 */
/// M3508 P19の定格出力軸回転数[RPM](メーカー値。電源電圧24V前提)。
constexpr float kOutputRatedRpm = 482.0f;
/// モーター軸(C620のフィードバックと同じ軸)の最高回転数[RPM]。
constexpr float kMotorMaxRpm = kOutputRatedRpm * kGearRatio;
/// 最高速に対する使用割合。スティック全開でこの割合まで出す。
constexpr float kSpeedFraction = 0.8f;
/// 制御上のモーター最大RPM(正規化1.0で出る値)。
constexpr float kMaxMotorRpm = kMotorMaxRpm * kSpeedFraction;
/// 上記RPMのときの車輪の周速[m/s]。
constexpr float kMaxWheelSpeed =
    kMaxMotorRpm / kGearRatio * 2.0f * kPi * kWheelRadius / 60.0f;
/// 加減速の上限[RPM/s]。0から最大まで約1秒。
constexpr float kSlewRpmPerS = kMaxMotorRpm;
/// @}
/**
 * @brief 正規化要求1.0あたりの車体速度とRPM/slew/deadzone。
 * 並進は45度配置の車輪1本あたり cos45 倍になるので、車体速度は周速/cos45。
 * 旋回は全輪が半径kCenterToWheelの接線なので、角速度は周速/半径。
 */
constexpr Limits kLimits = {kMaxWheelSpeed / 0.70710678f,
                            kMaxWheelSpeed / 0.70710678f,
                            kMaxWheelSpeed / kCenterToWheel,
                            kMaxMotorRpm,
                            kSlewRpmPerS,
                            0.05f};
/**
 * @brief 車輪を浮かせたベンチ用の低速値。正規化1.0でモーター約300RPM。
 * omni_benchはこちらを使う(最高速の試験は接地走行でのみ行う)。
 */
constexpr Limits kBenchLimits = {0.08f, 0.08f, 0.32f, 300.0f, 600.0f, 0.05f};
/// ベンチの過速度停止[RPM]。
constexpr float kBenchOverspeedRpm = 500.0f;
/**
 * 速度PIゲイン。夏の参考値(kp=1, ki=0.2)では取り付け負荷を超えられず、
 * 低速指令で車輪が回らない。実機ベンチで100RPMが回った 10 / 3 を使う。
 */
constexpr float kSpeedKp = 10.0f;
constexpr float kSpeedKi = 3.0f;
/// 電流指令(C620生値)の上限。参考上限3000の半分。
constexpr int16_t kMaxCurrent = 1500;
/// 実測がこれを超えたらFAULT[RPM]。モーター最高回転数。
constexpr float kOverspeedRpm = kMotorMaxRpm;

/**
 * @brief kSlotsから車輪設定を生成する。駆動方向は接線(位置角+90度)。
 */
std::array<WheelConfig, 4> wheels();
} // namespace sakura::vehicle
