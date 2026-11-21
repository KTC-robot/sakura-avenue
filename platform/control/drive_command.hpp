/**
 * @file drive_command.hpp
 * @brief 正規化車体要求と、その検証を定義する。
 */
#pragma once
#include <array>
namespace sakura {
using Four = std::array<float, 4>;
/**
 * @brief 正規化車体要求（各軸±1）。+X前、+Y左、+wz反時計回り。
 */
struct DriveCommand {
  float vx = 0, vy = 0, wz = 0;
};
/**
 * @brief 全軸が0ならtrue。
 */
bool neutral(DriveCommand c);
/**
 * @brief 全軸が有限かつ±1以内ならtrue。
 */
bool valid(DriveCommand c);
} // namespace sakura
