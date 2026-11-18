/**
 * @file omni_bench.cpp
 * @brief 車輪を浮かせた状態で、4輪オムニの運動学とPIを同時に確認するベンチ。
 *
 * シリアル(115200, LF)コマンド:
 *   status               4輪のFBとゲインを表示する。
 *   plan <vx> <vy> <wz>  動かさず、各IDの目標RPMだけを表示する(符号確認用)。
 *   g <kp> <ki>          PIゲインを変更する(停止中のみ)。
 *   recover              CAN異常ラッチを解除する(DISARM中のみ)。
 *   arm                  駆動を許可する。
 *   d <vx> <vy> <wz>     正規化要求(各軸 |v|<=1)を3秒間出す。+X前、+Y左、+wz反時計回り。
 *   s | disarm           即時ゼロ電流にしてDISARMへ戻る。
 *
 * 車体の幾何は下の「車体設定」で定義する。取り付け符号は実機確認で決める。
 */
#include "control/control.hpp"
#include "control/vehicle_config.hpp"
#include "device/c620.hpp"
#include "device/twai_bus.hpp"
#include <Arduino.h>
#include <cmath>
#include <cstdlib>
#include <optional>

namespace {
// ---- 車体設定は control/vehicle_config.hpp ----
using namespace sakura::vehicle;
constexpr int kTxGpio = kCanTxGpio, kRxGpio = kCanRxGpio;

// ---- 安全上の固定値 ----
constexpr uint32_t kRunMs = 3000, kFeedbackTimeoutMs = 100, kPeriodUs = 5000,
                   kLogMs = 100;
constexpr float kMaxKp = 20.0f, kMaxKi = 10.0f;

sakura::TwaiBus bus;
sakura::Feedback feedback;
std::array<uint32_t, 4> lastSeen{};
std::array<bool, 4> everSeen{};
std::optional<sakura::Omni> omni;
std::optional<sakura::Chassis> chassis;
bool armed = false, running = false;
int stopFrames = 0;
sakura::DriveCommand demand{};
uint32_t runStart = 0;
float gainKp = SPEED_KP, gainKi = SPEED_KI;
String line;

const char *slotName(int idx) { return kSlotNames[idx]; }

void stopAll(const char *reason) {
  armed = false;
  running = false;
  demand = {};
  stopFrames = 5;
  if (chassis)
    chassis->reset();
  Serial.printf("[omni] STOP: %s\n", reason);
}

bool freshAll(uint32_t now) {
  for (int i = 0; i < 4; i++)
    if (!everSeen[i] || now - lastSeen[i] > kFeedbackTimeoutMs)
      return false;
  return true;
}

void printStatus(uint32_t now) {
  twai_node_status_t st{};
  bool ok = bus.info(st);
  Serial.printf("[omni] armed=%d running=%d can_ok=%d gains kp=%.2f ki=%.2f\n",
                armed, running, bus.healthy(), gainKp, gainKi);
  Serial.printf("  twai state=%d tec=%u rec=%u errors=%lu last_err=0x%02lx "
                "rx_dropped=%lu\n",
                ok ? (int)st.state : -1, ok ? st.tx_error_count : 0,
                ok ? st.rx_error_count : 0, (unsigned long)bus.error_count(),
                (unsigned long)bus.last_error(),
                (unsigned long)bus.rx_dropped());
  for (int i = 0; i < 4; i++) {
    int id = kSlots[i].id - 1;
    if (!everSeen[id])
      Serial.printf("  ID%d(%s): no feedback\n", id + 1, slotName(i));
    else
      Serial.printf("  ID%d(%s): %s rpm=%d cur=%d temp=%u\n", id + 1,
                    slotName(i),
                    now - lastSeen[id] <= kFeedbackTimeoutMs ? "fresh" : "STALE",
                    feedback.rpm[id], feedback.current[id],
                    feedback.temperature[id]);
  }
}

// 動かさずに目標RPMを表示する。Chassisと同じ制限(スケール)を適用する。
void plan(float vx, float vy, float wz) {
  if (!omni) {
    Serial.println("[omni] geometry invalid");
    return;
  }
  auto dz = [](float v, float max) {
    return std::fabs(v) < kLimits.deadzone ? 0.0f : v * max;
  };
  auto rpm = omni->motor_rpm({dz(vx, kLimits.vx), dz(vy, kLimits.vy),
                              dz(wz, kLimits.wz)});
  float peak = 0;
  for (float v : rpm)
    peak = std::max(peak, std::fabs(v));
  float scale = peak > kLimits.max_motor_rpm ? kLimits.max_motor_rpm / peak : 1;
  Serial.printf("[omni] plan vx=%.2f vy=%.2f wz=%.2f (scale=%.2f)\n", vx, vy, wz,
                scale);
  for (int i = 0; i < 4; i++)
    Serial.printf("  ID%d(%s): target %+.0f rpm\n", kSlots[i].id, slotName(i),
                  rpm[kSlots[i].id - 1] * scale);
}

bool parse3(const String &s, float &a, float &b, float &c) {
  if (sscanf(s.c_str(), "%f %f %f", &a, &b, &c) != 3)
    return false;
  return std::isfinite(a) && std::isfinite(b) && std::isfinite(c) &&
         std::fabs(a) <= 1 && std::fabs(b) <= 1 && std::fabs(c) <= 1;
}

void handle(const String &cmd, uint32_t now) {
  float a, b, c;
  if (cmd == "status") {
    printStatus(now);
  } else if (cmd == "recover") {
    if (armed || running)
      Serial.println("[omni] refuse: disarm first");
    else if (bus.recover())
      Serial.println("[omni] CAN fault cleared");
    else
      Serial.println("[omni] not recovered yet. check 'status', retry");
  } else if (cmd == "arm") {
    if (!chassis)
      Serial.println("[omni] refuse: geometry/limits invalid");
    else if (!bus.healthy())
      Serial.println("[omni] refuse: CAN unhealthy (try 'recover')");
    else if (!freshAll(now))
      Serial.println("[omni] refuse: feedback not fresh on all 4 IDs");
    else {
      armed = true;
      Serial.println("[omni] ARMED (no motor runs until 'd')");
    }
  } else if (cmd == "disarm" || cmd == "s") {
    stopAll("operator");
  } else if (cmd.startsWith("plan ")) {
    if (parse3(cmd.substring(5), a, b, c))
      plan(a, b, c);
    else
      Serial.println("[omni] usage: plan <vx> <vy> <wz> (|v|<=1)");
  } else if (cmd.startsWith("g ")) {
    float kp = 0, ki = 0;
    if (running || sscanf(cmd.c_str() + 2, "%f %f", &kp, &ki) != 2 ||
        !std::isfinite(kp) || !std::isfinite(ki) || kp < 0 || ki < 0 ||
        kp > kMaxKp || ki > kMaxKi || !chassis) {
      Serial.printf("[omni] usage: g <kp 0-%.0f> <ki 0-%.0f> (停止中のみ)\n",
                    kMaxKp, kMaxKi);
      return;
    }
    gainKp = kp;
    gainKi = ki;
    chassis->set_gains(kp, ki);
    Serial.printf("[omni] gains kp=%.2f ki=%.2f\n", kp, ki);
  } else if (cmd.startsWith("d ")) {
    if (!parse3(cmd.substring(2), a, b, c)) {
      Serial.println("[omni] usage: d <vx> <vy> <wz> (|v|<=1)");
      return;
    }
    if (!armed || !chassis) {
      Serial.println("[omni] refuse: not armed");
      return;
    }
    if (!freshAll(now)) {
      Serial.println("[omni] refuse: feedback not fresh");
      return;
    }
    chassis->reset();
    demand = {a, b, c};
    running = true;
    runStart = now;
    Serial.printf("[omni] RUN vx=%.2f vy=%.2f wz=%.2f for %lu ms\n", a, b, c,
                  static_cast<unsigned long>(kRunMs));
  } else if (cmd.length()) {
    Serial.println("[omni] status | plan | g | recover | arm | d | s");
  }
}
} // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  try {
    omni.emplace(wheels());
    chassis.emplace(wheels(), kLimits);
    Serial.println("[omni] geometry OK (rank=3)");
  } catch (const std::exception &e) {
    Serial.printf("[omni] geometry/limits rejected: %s. 駆動不可\n", e.what());
  }
  esp_err_t r = bus.begin(kTxGpio, kRxGpio);
  Serial.printf("[omni] TWAI TX=%d RX=%d begin=%s. DISARMED.\n", kTxGpio, kRxGpio,
                esp_err_to_name(r));
  Serial.println("[omni] wheels must be off the ground. 'status' then 'plan'.");
}

void loop() {
  static uint32_t lastTick = micros(), lastLog = 0;
  uint32_t now = millis();

  // C620は4台合計で約4kHzのFBを送る。制御周期とは別に毎回キューを空にする。
  sakura::CanFrame f;
  while (bus.read(f))
    if (feedback.receive(f, now)) {
      int i = f.id - 0x201;
      lastSeen[i] = now;
      everSeen[i] = true;
    }

  uint32_t nowUs = micros();
  if (nowUs - lastTick < kPeriodUs)
    return;
  float dt = (nowUs - lastTick) * 1e-6f;
  lastTick = nowUs;

  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      line.trim();
      handle(line, now);
      line = "";
    } else if (line.length() < 48) {
      line += c;
    }
  }

  if (running) {
    bool over = false;
    for (int i = 0; i < 4; i++)
      over |= std::fabs(static_cast<float>(feedback.rpm[i])) > kOverspeedRpm;
    if (!bus.healthy())
      stopAll("CAN fault");
    else if (!freshAll(now))
      stopAll("feedback lost");
    else if (over)
      stopAll("overspeed");
    else if (now - runStart > kRunMs)
      stopAll("run time elapsed");
  }

  std::array<int16_t, 4> cur{};
  if (armed && running && chassis) {
    sakura::Four measured{};
    for (int i = 0; i < 4; i++)
      measured[i] = feedback.rpm[i];
    try {
      cur = chassis->update(demand, measured, dt, true);
      for (auto &c : cur)
        c = std::clamp<int16_t>(c, -kMaxCurrent, kMaxCurrent);
    } catch (const std::exception &) {
      stopAll("kinematics error");
      cur = {};
    }
    if (running && now - lastLog >= kLogMs) {
      lastLog = now;
      auto t = chassis->targets();
      Serial.printf("tgt/rpm/cmd: ");
      for (int i = 0; i < 4; i++)
        Serial.printf("ID%d %+.0f/%d/%d  ", i + 1, t[i], feedback.rpm[i], cur[i]);
      Serial.println();
    }
  }
  // ARM中と停止直後だけ送信する(DISARM中はバスにACK相手がいないとbus offを招く)。
  if (armed || stopFrames > 0) {
    if (stopFrames > 0 && !armed)
      stopFrames--;
    if (!bus.send(sakura::current_frame(cur)) && running)
      stopAll("CAN send failed");
  }
}
