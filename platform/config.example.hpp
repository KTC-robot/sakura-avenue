// コピーして config.local.hpp を作り実値を入れる(config.local.* はGit管理外)。
//   cp platform/config.example.hpp platform/config.local.hpp
// config.local.hpp は接続試験用env(esp32c5_bench)でのみ読み込まれる。
#pragma once
#include <cstdint>
namespace sakura::config {
// Piのホットスポット(5GHz)
constexpr const char *wifi_ssid = "";
constexpr const char *wifi_password = ""; // 8〜63文字
constexpr const char *wifi_country = "JP";
// 許可する送信元 = Pi側。peer_port は sakura-pilot の --source-port と一致させる。
constexpr const char *peer_ip = "10.42.0.1";
constexpr uint16_t peer_port = 50001;
// ESP32が待ち受けるUDPポート。sakura-pilot の --port と一致させる。
constexpr uint16_t listen_port = 50000;
} // namespace sakura::config
