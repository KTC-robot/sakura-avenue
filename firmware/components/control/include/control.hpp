/**
 * @file control.hpp
 * @brief 車体要求、安全状態、配置可変の4輪オムニとPI制御の結合を定義する。
 */
#pragma once
#include "speed_controller.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
namespace sakura {
using Four = std::array<float, 4>;
/**
 * @brief 正規化車体要求（各軸±1）。+X前、+Y左、+wz反時計回り。
 */
struct DriveCommand {
  float vx = 0, vy = 0, wz = 0;
};
/**
 * @brief 車輪の幾何とモーター対応。実機値は未確定。
 * 位置[m]、駆動方向[rad]、半径[m]、減速比（モーター/車輪）、符号±1、番号0〜3。
 */
struct WheelConfig {
  float x_m, y_m, drive_angle_rad, radius_m, gear_ratio;
  int8_t motor_sign;
  uint8_t motor_index;
};
/**
 * @brief 配置パラメーターに基づく4輪オムニ逆運動学。
 */
class Omni {
  std::array<WheelConfig, 4> wheels_;

public:
  /**
   * @brief 車輪設定を検証して保持する。
   * @throws std::invalid_argument 幾何・番号・rankが無効な場合。
   */
  explicit Omni(std::array<WheelConfig, 4> wheels) : wheels_(wheels) {
    bool seen[4] = {};
    double gram[3][3] = {};
    for (const auto &w : wheels_) {
      for (float v :
           {w.x_m, w.y_m, w.drive_angle_rad, w.radius_m, w.gear_ratio})
        if (!std::isfinite(v))
          throw std::invalid_argument("nonfinite geometry");
      if (w.radius_m <= 0 || w.gear_ratio <= 0 ||
          (w.motor_sign != 1 && w.motor_sign != -1) || w.motor_index >= 4 ||
          seen[w.motor_index])
        throw std::invalid_argument("invalid wheel");
      seen[w.motor_index] = true;
      double c = std::cos(w.drive_angle_rad), s = std::sin(w.drive_angle_rad);
      double row[] = {c, s, -w.y_m * c + w.x_m * s};
      for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
          gram[i][j] += row[i] * row[j];
    }
    // ピボット付き消去でrank不足を検出する。特定の車輪配置は仮定しない。
    for (int k = 0; k < 3; k++) {
      int pivot = k;
      for (int i = k + 1; i < 3; i++)
        if (std::abs(gram[i][k]) > std::abs(gram[pivot][k]))
          pivot = i;
      if (std::abs(gram[pivot][k]) < 1e-10)
        throw std::invalid_argument("rank deficient geometry");
      for (int j = 0; j < 3; j++)
        std::swap(gram[k][j], gram[pivot][j]);
      for (int i = k + 1; i < 3; i++) {
        double factor = gram[i][k] / gram[k][k];
        for (int j = k; j < 3; j++)
          gram[i][j] -= factor * gram[k][j];
      }
    }
  }
  /**
   * @brief 車体速度をモーター軸RPMへ変換する。
   * @param cmd vx/vy[m/s]、wz[rad/s]。
   * @return motor_index順の目標RPM。
   */
  Four motor_rpm(DriveCommand cmd) const {
    Four out{};
    for (float v : {cmd.vx, cmd.vy, cmd.wz})
      if (!std::isfinite(v))
        throw std::invalid_argument("nonfinite demand");
    for (const auto &w : wheels_) {
      float c = std::cos(w.drive_angle_rad), s = std::sin(w.drive_angle_rad);
      float linear =
          c * cmd.vx + s * cmd.vy + (-w.y_m * c + w.x_m * s) * cmd.wz;
      // 車輪rad/s→車輪RPM→モーターRPM。feedbackは既にモーター軸RPM。
      out[w.motor_index] = linear / w.radius_m * 60 /
                           (2 * 3.14159265358979323846f) * w.gear_ratio *
                           w.motor_sign;
      if (!std::isfinite(out[w.motor_index]))
        throw std::invalid_argument("rpm overflow");
    }
    return out;
  }
};
enum class Action : uint8_t { Disarm = 0, Arm = 1, Drive = 2, Stop = 3 };
/**
 * @brief 駆動禁止・許可・通常停止・異常停止。
 */
enum class State { Disarm, Armed, Stop, Fault };
struct Packet {
  uint32_t sequence = 0;
  Action action = Action::Disarm;
  DriveCommand command{};
};
inline bool neutral(DriveCommand c) {
  return c.vx == 0 && c.vy == 0 && c.wz == 0;
}
inline bool valid(DriveCommand c) {
  for (float v : {c.vx, c.vy, c.wz})
    if (!std::isfinite(v) || std::abs(v) > 1)
      return false;
  return true;
}
/**
 * @brief 受信期限と再許可を管理する。
 */
class Safety {
  bool have_ = false, neutral_ = false;
  uint32_t seq_ = 0;
  uint64_t last_ = 0;

public:
  State state = State::Disarm;
  uint64_t timeout_ms = 180; // 初期ベンチ値。実機で期限を検証する。
  /**
   * @brief FAULTへ移り、ニュートラル確認を破棄する。
   */
  void fault() {
    state = State::Fault;
    neutral_ = false;
  }
  /**
   * @brief 受信期限と健全性を監視する。
   * @param now 単調時計[ms]。
   */
  void tick(uint64_t now, bool healthy) {
    if (!healthy || (have_ && (now < last_ || now - last_ > timeout_ms)))
      fault();
  }
  /**
   * @brief 指令を検証して安全状態を更新する。
   * @return 更新後がARMEDならtrue。
   * @note 復帰にはニュートラルDISARM後の明示ARMが必要。
   */
  bool accept(const Packet &p, uint64_t now, bool healthy) {
    tick(now, healthy);
    uint32_t delta = p.sequence - seq_;
    if (!valid(p.command) || static_cast<unsigned>(p.action) > 3 ||
        (p.action != Action::Drive && !neutral(p.command)) ||
        (have_ && (now < last_ || delta == 0 || delta >= 0x80000000U))) {
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
};
/**
 * @brief 速度[m/s・rad/s]、RPM上限、slew[RPM/s]、deadzoneの設定。
 */
struct Limits {
  float vx, vy, wz, max_motor_rpm, slew_rpm_s, deadzone;
};
/**
 * @brief 運動学・RPM制限・slew・PIを結合する。
 */
class Chassis {
  Omni omni_;
  Limits limits_;
  Four target_{};
  std::array<SpeedController, 4> pi_{};

public:
  /**
   * @brief 幾何と制御上限を検証する。
   */
  Chassis(std::array<WheelConfig, 4> wheels, Limits limits)
      : omni_(wheels), limits_(limits) {
    for (float v : {limits.vx, limits.vy, limits.wz, limits.max_motor_rpm,
                    limits.slew_rpm_s})
      if (!std::isfinite(v) || v <= 0)
        throw std::invalid_argument("unconfigured limits");
    if (!std::isfinite(limits.deadzone) || limits.deadzone < 0 ||
        limits.deadzone >= 1)
      throw std::invalid_argument("deadzone");
  }
  /**
   * @brief 目標RPMと全PI積分をリセットする。
   */
  void reset() {
    target_.fill(0);
    for (auto &p : pi_)
      p.reset();
  }
  /**
   * @brief 制限後の目標RPMを取得する。
   * @return motor_index順のモーター軸RPM。
   */
  Four targets() const { return target_; }
  /**
   * @brief 走行要求から電流指令を計算する。
   * @param cmd 正規化要求（各軸±1）。
   * @param measured モーター軸RPM。
   * @param dt 制御間隔[s]。
   * @param allowed falseなら即時リセットする。
   * @return motor_index順のC620電流指令。
   */
  std::array<int16_t, 4> update(DriveCommand cmd, Four measured, float dt,
                                bool allowed) {
    std::array<int16_t, 4> currents{};
    if (!allowed || !valid(cmd) || !std::isfinite(dt) || dt <= 0) {
      reset();
      return currents;
    }
    for (float v : measured)
      if (!std::isfinite(v)) {
        reset();
        return currents;
      }
    auto demand = [&](float v, float max) {
      return std::abs(v) < limits_.deadzone ? 0.0f : v * max;
    };
    auto rpm =
        omni_.motor_rpm({demand(cmd.vx, limits_.vx), demand(cmd.vy, limits_.vy),
                         demand(cmd.wz, limits_.wz)});
    float peak = 0;
    for (float v : rpm)
      peak = std::max(peak, std::abs(v));
    float scale =
        peak > limits_.max_motor_rpm ? limits_.max_motor_rpm / peak : 1;
    for (int i = 0; i < 4; i++) {
      float step = limits_.slew_rpm_s * dt;
      target_[i] += std::clamp(rpm[i] * scale - target_[i], -step, step);
      currents[i] = pi_[i].update(target_[i], measured[i], dt).currentCommand;
    }
    return currents;
  }
};
} // namespace sakura
