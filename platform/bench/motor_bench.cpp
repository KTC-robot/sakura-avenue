/**
 * @file motor_bench.cpp
 * @brief 車輪を浮かせた状態でC620を1台ずつ確認するベンチ用ファーム。
 *
 * シリアル(115200, LF)コマンド:
 *   status        全IDのフィードバック(RPM/電流/温度/角度)を表示する。
 *   recover       CAN異常ラッチを解除する(DISARM中のみ)。
 *   arm           駆動を許可する(起動直後はDISARM)。
 *   disarm | s    即時ゼロ電流にしてDISARMへ戻る。
 *   m <id> <rpm>  ID(1〜4)のモーターを目標RPMで回す。+1回=最大3秒。
 *   g <kp> <ki>   PIゲインを変更する(停止中のみ。既定 kp=1 ki=0.2)。
 *   c <id> <cnt>  開ループで電流指令(生値, |cnt|<=800)を3秒出す。起動確認用。
 *
 * 他のモーターは常にゼロ電流。ネットワーク・車体運動学は使わない。
 */
#include "control/speed_controller.hpp"
#include "device/c620.hpp"
#include "device/twai_bus.hpp"
#include <Arduino.h>
#include <cmath>
#include <cstdlib>

namespace {
constexpr int kTxGpio = 23;
constexpr int kRxGpio = 24;
constexpr float kMaxRpm = 300.0f; // 試験で許す目標RPMの絶対上限
constexpr int16_t kMaxCurrent = 1500;
constexpr int16_t kMaxOpenLoop =
    800; // `c`コマンドの電流上限(生値)    // C620生値。参考上限3000の半分
constexpr float kOverspeedRpm =
    500.0f; // 実測がこれを超えたら即停止(開ループ含む)
constexpr float kMaxKp = 20.0f, kMaxKi = 10.0f; // `g`コマンドのゲイン上限
constexpr uint32_t kRunMs = 3000;               // 1回の指示の継続時間
constexpr uint32_t kFeedbackTimeoutMs = 100;
constexpr uint32_t kPeriodUs = 5000; // 制御周期 5ms
constexpr uint32_t kLogMs = 100;

sakura::TwaiBus bus;
sakura::Feedback feedback;
std::array<uint32_t, 4> lastSeen{};
std::array<bool, 4> everSeen{};
std::array<SpeedController, 4> pi;

bool armed = false;
int stopFrames = 0;    // 停止直後に送るゼロ電流フレームの残数
int active = -1;       // 0〜3、停止中は-1
bool openLoop = false; // trueなら targetRpm を電流指令[カウント]として直接出す
float targetRpm = 0;
float gainKp = SPEED_KP, gainKi = SPEED_KI;
uint32_t runStart = 0;
String line;

void stopAll(const char *reason) {
  armed = false;
  active = -1;
  openLoop = false;
  targetRpm = 0;
  stopFrames = 5;
  for (auto &p : pi)
    p.reset();
  Serial.printf("[bench] STOP: %s\n", reason);
}

bool fresh(int i, uint32_t now) {
  return everSeen[i] && now - lastSeen[i] <= kFeedbackTimeoutMs;
}

void printStatus(uint32_t now) {
  twai_node_status_t st{};
  bool ok = bus.info(st);
  Serial.printf("[bench] armed=%d can_ok=%d active=%d gains kp=%.2f ki=%.2f\n",
                armed, bus.healthy(), active + 1, gainKp, gainKi);
  Serial.printf(
      "  twai state=%d(0=active,1=warn,2=passive,3=busoff) tec=%u rec=%u "
      "errors=%lu "
      "last_err=0x%02lx(1=arb,2=bit,4=form,8=stuff,16=ack,0x200=rx取得失敗) "
      "state_changes=%lu rx_dropped=%lu\n",
      ok ? (int)st.state : -1, ok ? st.tx_error_count : 0,
      ok ? st.rx_error_count : 0, (unsigned long)bus.error_count(),
      (unsigned long)bus.last_error(), (unsigned long)bus.state_change_count(),
      (unsigned long)bus.rx_dropped());
  for (int i = 0; i < 4; i++) {
    if (!everSeen[i]) {
      Serial.printf("  ID%d: no feedback\n", i + 1);
      continue;
    }
    Serial.printf("  ID%d: %s rpm=%d cur=%d temp=%u angle=%u\n", i + 1,
                  fresh(i, now) ? "fresh" : "STALE", feedback.rpm[i],
                  feedback.current[i], feedback.temperature[i],
                  feedback.angle[i]);
  }
}

void handle(const String &cmd, uint32_t now) {
  if (cmd == "status") {
    printStatus(now);
  } else if (cmd == "recover") {
    if (armed || active >= 0) {
      Serial.println("[bench] refuse: disarm first");
    } else if (bus.recover()) {
      Serial.println("[bench] CAN fault cleared (state ERROR_ACTIVE)");
    } else {
      Serial.println("[bench] not recovered yet. check 'status', retry");
    }
  } else if (cmd == "arm") {
    if (!bus.healthy()) {
      Serial.println("[bench] refuse: CAN unhealthy");
      return;
    }
    armed = true;
    Serial.println("[bench] ARMED (no motor runs until 'm <id> <rpm>')");
  } else if (cmd == "disarm" || cmd == "s") {
    stopAll("operator");
  } else if (cmd.startsWith("m ")) {
    int id = 0;
    float rpm = 0;
    if (sscanf(cmd.c_str() + 2, "%d %f", &id, &rpm) != 2 || id < 1 || id > 4 ||
        !std::isfinite(rpm) || std::fabs(rpm) > kMaxRpm) {
      Serial.printf("[bench] refuse: usage m <1-4> <rpm |rpm|<=%.0f>\n",
                    kMaxRpm);
      return;
    }
    if (!armed) {
      Serial.println("[bench] refuse: not armed");
      return;
    }
    if (!fresh(id - 1, now)) {
      Serial.printf("[bench] refuse: ID%d feedback not fresh\n", id);
      return;
    }
    for (auto &p : pi)
      p.reset();
    active = id - 1;
    targetRpm = rpm;
    runStart = now;
    Serial.printf("[bench] RUN ID%d target=%.0f rpm for %lu ms\n", id, rpm,
                  static_cast<unsigned long>(kRunMs));
  } else if (cmd.startsWith("g ")) {
    float kp = 0, ki = 0;
    if (active >= 0 || sscanf(cmd.c_str() + 2, "%f %f", &kp, &ki) != 2 ||
        !std::isfinite(kp) || !std::isfinite(ki) || kp < 0 || ki < 0 ||
        kp > kMaxKp || ki > kMaxKi) {
      Serial.printf(
          "[bench] refuse: usage g <kp 0-%.0f> <ki 0-%.0f> (停止中のみ)\n",
          kMaxKp, kMaxKi);
      return;
    }
    gainKp = kp;
    gainKi = ki;
    for (auto &p : pi)
      p.setGains(kp, ki);
    Serial.printf("[bench] gains kp=%.2f ki=%.2f\n", kp, ki);
  } else if (cmd.startsWith("c ")) {
    int id = 0, counts = 0;
    if (sscanf(cmd.c_str() + 2, "%d %d", &id, &counts) != 2 || id < 1 ||
        id > 4 || std::abs(counts) > kMaxOpenLoop) {
      Serial.printf("[bench] refuse: usage c <1-4> <counts |counts|<=%d>\n",
                    kMaxOpenLoop);
      return;
    }
    if (!armed) {
      Serial.println("[bench] refuse: not armed");
      return;
    }
    if (!fresh(id - 1, now)) {
      Serial.printf("[bench] refuse: ID%d feedback not fresh\n", id);
      return;
    }
    for (auto &p : pi)
      p.reset();
    active = id - 1;
    openLoop = true;
    targetRpm = static_cast<float>(counts);
    runStart = now;
    Serial.printf("[bench] OPEN-LOOP ID%d current=%d counts for %lu ms\n", id,
                  counts, static_cast<unsigned long>(kRunMs));
  } else if (cmd.length()) {
    Serial.println("[bench] commands: status | recover | arm | disarm | s | m "
                   "<id> <rpm> | c <id> <counts> | g <kp> <ki>");
  }
}
} // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  esp_err_t r = bus.begin(kTxGpio, kRxGpio);
  Serial.printf("[bench] TWAI TX=%d RX=%d begin=%s. DISARMED.\n", kTxGpio,
                kRxGpio, esp_err_to_name(r));
  Serial.println("[bench] wheels must be off the ground. 'status' first.");
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
    } else if (line.length() < 32) {
      line += c;
    }
  }

  if (active >= 0) {
    if (!bus.healthy())
      stopAll("CAN fault");
    else if (!fresh(active, now))
      stopAll("feedback lost");
    else if (std::fabs(static_cast<float>(feedback.rpm[active])) >
             kOverspeedRpm)
      stopAll("overspeed");
    else if (now - runStart > kRunMs)
      stopAll("run time elapsed");
  }

  std::array<int16_t, 4> cur{};
  if (armed && active >= 0) {
    if (openLoop) {
      cur[active] = static_cast<int16_t>(targetRpm);
    } else {
      auto r = pi[active].update(targetRpm, feedback.rpm[active], dt);
      cur[active] =
          std::clamp<int16_t>(r.currentCommand, -kMaxCurrent, kMaxCurrent);
    }
    if (now - lastLog >= kLogMs) {
      lastLog = now;
      Serial.printf("ID%d tgt=%.0f rpm=%d cmd=%d fbcur=%d temp=%u angle=%u\n",
                    active + 1, targetRpm, feedback.rpm[active], cur[active],
                    feedback.current[active], feedback.temperature[active],
                    feedback.angle[active]);
    }
  }
  // ARM中と停止直後だけ送信する。DISARM中の送信は、バスに相手がいないと
  // ACK無しでbus offを招くため行わない(C620は指令なしでもFBを返す)。
  if (armed || stopFrames > 0) {
    if (stopFrames > 0 && !armed)
      stopFrames--;
    if (!bus.send(sakura::current_frame(cur)) && active >= 0)
      stopAll("CAN send failed");
  }
}
