/**
 * @file safety.hpp
 * @brief 受信指令の形式と、受信期限・連番・再許可による安全状態(セッション)を定義する。
 */
#pragma once
#include "control/drive_command.hpp"
#include <cstdint>
namespace sakura {
enum class Action : uint8_t { Disarm = 0, Arm = 1, Drive = 2, Stop = 3 };
/// Actionの最大値。これを超える値は無効。
constexpr unsigned action_max = static_cast<unsigned>(Action::Stop);
/// 受信期限の初期値[ms]。初期ベンチ値で、実機で検証する。
constexpr uint64_t default_timeout_ms = 180;
/**
 * @brief 駆動禁止・許可・通常停止・異常停止。
 */
enum class State { Disarm, Armed, Stop, Fault };
struct Packet {
  uint32_t sequence = 0;
  Action action = Action::Disarm;
  DriveCommand command{};
};
/**
 * @brief 受信期限と再許可を管理する。
 */
class Safety {
  bool have_ = false, neutral_ = false;
  uint32_t seq_ = 0;
  uint64_t last_ = 0;

public:
  State state = State::Disarm;
  uint64_t timeout_ms = default_timeout_ms;
  /**
   * @brief FAULTへ移り、ニュートラル確認を破棄する。
   */
  void fault();
  /**
   * @brief 受信期限と健全性を監視する。
   * @param now 単調時計[ms]。
   */
  void tick(uint64_t now, bool healthy);
  /**
   * @brief 指令を検証して安全状態を更新する。
   * @return 更新後がARMEDならtrue。
   * @note 復帰にはニュートラルDISARM後の明示ARMが必要。
   */
  bool accept(const Packet &p, uint64_t now, bool healthy);
};
} // namespace sakura
