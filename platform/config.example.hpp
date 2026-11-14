// コピーして config.local.hpp を作り実値を入れる(config.local.* はGit管理外)。
//   cp platform/config.example.hpp platform/config.local.hpp
#pragma once
namespace sakura::config {
constexpr const char *wifi_ssid = "";
constexpr const char *wifi_password = "";
constexpr const char *wifi_country = "JP";
} // namespace sakura::config
