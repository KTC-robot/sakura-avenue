// コピーして config.local.hpp を作り実値を入れる(config.local.* はGit管理外)。
//   cp platform/config.example.hpp platform/config.local.hpp
// config.local.hpp が無い場合(CIなど)は本ファイルが使われ、空欄のため無線は開始しない。
#pragma once
#include <cstdint>
namespace sakura::config {
constexpr const char *wifi_ssid = "";
constexpr const char *wifi_password = ""; // 8〜63文字
constexpr const char *wifi_country = "JP";
// 許可する送信元 = Pi側。peer_port は sakura-pilot の --source-port と一致させる。
constexpr const char *peer_ip = "";
constexpr uint16_t peer_port = 0;
// ESP32のIPを固定する場合に設定する(空欄ならDHCP)。3つとも必要。
// Pi(NetworkManager共有AP)なら Pi=10.42.0.1 なので、例: 10.42.0.50 / 10.42.0.1 / 255.255.255.0
constexpr const char *static_ip = "";
constexpr const char *static_gateway = "";
constexpr const char *static_subnet = "";
// ESP32が待ち受けるUDPポート。sakura-pilot の --port と一致させる。
constexpr uint16_t listen_port = 50000;
} // namespace sakura::config
