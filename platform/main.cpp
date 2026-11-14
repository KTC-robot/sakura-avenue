#include <Arduino.h>
#include "control/control.hpp"
#include "device/c620.hpp"
#include "protocol/protocol.hpp"

#ifdef SAKURA_BENCH_NET
// 接続試験専用: Wi-Fi接続とUDP受信のみ。CAN/モーターは起動せず、ARMも受け付けない運用
// (Pi側はDISARMしか送らない)。暗号認証なしのため隔離ベンチ限定。
#include <memory>
#include "config.local.hpp"
#include "network/receiver.hpp"
#include "network/udp_socket.hpp"
#include "network/wifi_station.hpp"

static sakura::WifiStation wifi;
static sakura::UdpSocket udp;
static std::unique_ptr<sakura::Receiver> receiver;
static bool udp_ready = false;

static const char *state_name(sakura::State s) {
  switch (s) {
  case sakura::State::Disarm: return "DISARM";
  case sakura::State::Armed: return "ARMED";
  case sakura::State::Stop: return "STOP";
  default: return "FAULT";
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("[bench] network-only test: CAN/motors are NOT started");
  receiver.reset(new sakura::Receiver(sakura::config::peer_ip,
                                      sakura::config::peer_port));
  if (!wifi.begin(sakura::config::wifi_ssid, sakura::config::wifi_password,
                  sakura::config::wifi_country))
    Serial.println("[bench] wifi.begin rejected (check config.local.hpp)");
  else
    Serial.println("[bench] connecting (5GHz only)...");
}

void loop() {
  uint64_t now = millis();
  bool ok = wifi.connected();
  if (ok && !udp_ready) {
    String ip = WiFi.localIP().toString();
    udp_ready = udp.begin(ip.c_str(), sakura::config::listen_port);
    Serial.printf("[bench] ip=%s listen=%u udp=%s\n", ip.c_str(),
                  sakura::config::listen_port, udp_ready ? "ok" : "FAILED");
  }
  if (udp_ready)
    udp.poll(*receiver, now, ok);
  static uint64_t last_print = 0;
  if (now - last_print >= 1000) {
    last_print = now;
    Serial.printf("[bench] wifi=%s rssi=%d ch=%d state=%s\n", ok ? "up" : "down",
                  ok ? WiFi.RSSI() : 0, ok ? WiFi.channel() : 0,
                  state_name(receiver->safety.state));
  }
  delay(2);
}
#else
static sakura::Safety safety;

void setup() {
  Serial.begin(115200);
  // ハードウェア・認証の確認が済むまでネットワーク/CANは開始しない。
  // モーターを暗黙に有効化するコンパイル時スイッチは置かない。
  Serial.println("[sakura] DISARM: geometry/GPIO/network/interlock unconfigured; outputs disabled");
}

void loop() { delay(1000); }
#endif
