/**
 * @file main.cpp
 * @brief 本番ファーム: Wi-Fi/UDP受信、安全状態、4輪オムニ制御、CAN送信を結合する。
 *
 * 起動時は必ずDISARM。駆動には、全C620のフィードバック・Wi-Fi・CAN・インターロックが
 * 健全な状態で、Piからのニュートラル DISARM → 明示ARM → DRIVE が必要。
 * 異常時はFAULTとなりゼロ電流を送る。復帰も同じ手順(DISARM→ARM)。
 *
 * 認証・リプレイ保護は未実装(docs/network.md)。物理非常停止を必ず併用すること。
 */
#include "control/vehicle_config.hpp"
#include "device/twai_bus.hpp"
#include "network/control_loop.hpp"
#include "network/udp_socket.hpp"
#include "network/wifi_station.hpp"
#include <Arduino.h>
#include <optional>
#if __has_include("config.local.hpp")
#include "config.local.hpp"
#else
#include "config.example.hpp"
#endif

namespace {
using namespace sakura;
using namespace sakura::vehicle;

constexpr uint32_t kPeriodUs = 5000;        // 制御周期 5ms(200Hz)
constexpr uint32_t kFeedbackTimeoutMs = 100; // 全C620のFB期限
constexpr uint32_t kWifiRetryMs = 3000;
constexpr uint32_t kCanRecoverMs = 500;
constexpr uint32_t kStatusMs = 1000;
constexpr uint32_t kHeartbeatMs = 1000; // ESP→Pi 状態通知の周期

TwaiBus bus;
Feedback feedback;
WifiStation wifi;
std::optional<UdpSocket> udp;
std::optional<Receiver> receiver;
std::optional<Chassis> chassis;
std::optional<ControlLoop> loop_;

bool wifiStarted = false, wifiWasUp = false, wasArmed = false;
bool configOk = false;
int stopFrames = 0;
uint32_t lastWifiTry = 0, lastRecover = 0, lastStatus = 0, lastHeartbeat = 0,
         heartbeatCount = 0;
uint64_t lastStepUs = 0;

bool interlockOk() {
  if (kInterlockGpio < 0)
    return true; // 未配線: 駆動電源の物理遮断のみに依存
  return (digitalRead(kInterlockGpio) == HIGH) == kInterlockActiveHigh;
}

/**
 * @brief C620電流フレームの各指令を、車体の電流上限kMaxCurrentへ絞る。
 */
void limitCurrents(CanFrame &frame) {
  for (int i = 0; i < 4; i++) {
    int16_t c = static_cast<int16_t>(uint16_t(frame.data[2 * i]) << 8 |
                                     frame.data[2 * i + 1]);
    c = std::clamp<int16_t>(c, -kMaxCurrent, kMaxCurrent);
    uint16_t u = static_cast<uint16_t>(c);
    frame.data[2 * i] = u >> 8;
    frame.data[2 * i + 1] = u & 255;
  }
}

const char *stateName(State s) {
  switch (s) {
  case State::Disarm: return "DISARM";
  case State::Armed: return "ARMED";
  case State::Stop: return "STOP";
  case State::Fault: return "FAULT";
  }
  return "?";
}
} // namespace

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("[sakura] boot: DISARM (駆動にはPiからのDISARM→ARMが必要)");

  try {
    chassis.emplace(wheels(), kLimits);
    chassis->set_gains(kSpeedKp, kSpeedKi);
    receiver.emplace(config::peer_ip, config::peer_port);
    loop_.emplace(*receiver, *chassis, feedback);
    configOk = true;
  } catch (const std::exception &e) {
    Serial.printf("[sakura] 設定が無効のため駆動しません: %s\n", e.what());
  }

  if (kInterlockGpio >= 0)
    pinMode(kInterlockGpio, INPUT);
  else
    Serial.println("[sakura] 注意: 物理インターロックGPIO未設定。駆動電源の物理遮断のみに依存");

  // CANは受信(フィードバック)のみ開始。電流は ARM 中だけ送る。
  esp_err_t r = bus.begin(kCanTxGpio, kCanRxGpio);
  Serial.printf("[sakura] TWAI TX=%d RX=%d: %s\n", kCanTxGpio, kCanRxGpio,
                esp_err_to_name(r));

  if (configOk) {
    wifiStarted = wifi.begin(config::wifi_ssid, config::wifi_password,
                             config::wifi_country, config::static_ip,
                             config::static_gateway, config::static_subnet);
    Serial.printf("[sakura] Wi-Fi 5GHz STA: %s\n",
                  wifiStarted ? "接続開始" : "設定不足のため無効");
  }
  lastStepUs = micros();
}

void loop() {
  uint64_t now = millis();

  // C620は4台合計で約4kHzのFBを送る。毎回キューを空にする。
  CanFrame f;
  while (bus.read(f))
    feedback.receive(f, now);

  bool wifiUp = wifiStarted && wifi.connected();
  if (wifiUp && !wifiWasUp) {
    udp.reset();
    udp.emplace();
    String ip = WiFi.localIP().toString();
    bool ok = udp->begin(ip.c_str(), config::listen_port);
    Serial.printf("[sakura] Wi-Fi接続 %s UDP:%u %s\n", ip.c_str(),
                  config::listen_port, ok ? "待受開始" : "bind失敗");
    if (!ok)
      udp.reset();
  } else if (!wifiUp && wifiWasUp) {
    udp.reset();
    Serial.println("[sakura] Wi-Fi切断 → FAULT");
  }
  wifiWasUp = wifiUp;
  if (wifiStarted && !wifiUp && now - lastWifiTry >= kWifiRetryMs) {
    lastWifiTry = now;
    WiFi.reconnect();
  }

  if (!configOk) {
    delay(10);
    return;
  }

  bool canOk = bus.healthy();
  bool fresh = feedback.fresh(now, kFeedbackTimeoutMs);
  bool inter = interlockOk();
  bool healthy = wifiUp && canOk && inter && fresh;

  bool overspeed = false;
  for (int i = 0; i < 4; i++)
    overspeed |= std::fabs(static_cast<float>(feedback.rpm[i])) > kOverspeedRpm;
  if (overspeed && receiver->safety.state != State::Fault) {
    loop_->transport_fault();
    Serial.println("[sakura] 過速度 → FAULT");
  }

  if (udp && !udp->poll(*receiver, now, healthy))
    loop_->transport_fault();

  // CAN異常はDISARM中のみ自動復旧を試みる。ARMには再びPiの明示操作が必要。
  if (receiver->safety.state != State::Armed && !canOk &&
      now - lastRecover >= kCanRecoverMs) {
    lastRecover = now;
    bus.recover();
  }

  uint64_t nowUs = micros();
  if (nowUs - lastStepUs >= kPeriodUs) {
    float dt = (nowUs - lastStepUs) * 1e-6f;
    lastStepUs = nowUs;
    CanFrame out = loop_->step(now, dt, wifiUp, canOk, inter);
    limitCurrents(out);
    bool armed = receiver->safety.state == State::Armed;
    if (wasArmed && !armed)
      stopFrames = 5;
    wasArmed = armed;
    // ARM中と停止直後のみ送信。DISARM中はバスにACK相手がいないとbus offを招く。
    if (armed || stopFrames > 0) {
      if (stopFrames > 0 && !armed)
        stopFrames--;
      if (!bus.send(out)) {
        loop_->transport_fault();
        Serial.println("[sakura] CAN送信失敗 → FAULT");
      }
    }
  }

  // 1秒に1回、Piへ状態を通知する(情報用。失敗しても制御には影響しない)。
  if (udp && now - lastHeartbeat >= kHeartbeatMs) {
    lastHeartbeat = now;
    uint8_t flags = (wifiUp ? status_wifi : 0) | (canOk ? status_can : 0) |
                    (fresh ? status_feedback : 0) | (inter ? status_interlock : 0);
    std::array<uint8_t, status_size> beat;
    encode_status(receiver->safety.state, flags, heartbeatCount++, beat);
    udp->send_to(config::peer_ip, config::peer_port, beat.data(), beat.size());
  }

  if (now - lastStatus >= kStatusMs && Serial.availableForWrite() >= 160) {
    lastStatus = now;
    Serial.printf("[sakura] %s wifi=%d can=%d fb=%d inter=%d rpm=%d,%d,%d,%d\n",
                  stateName(receiver->safety.state), wifiUp, canOk, fresh, inter,
                  feedback.rpm[0], feedback.rpm[1], feedback.rpm[2],
                  feedback.rpm[3]);
  }
}
