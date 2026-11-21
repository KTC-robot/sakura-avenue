#include "network/wifi_station.hpp"
#include "esp_wifi.h"
#include <cstring>

namespace sakura {
namespace {
constexpr size_t max_ssid_length = 32;
constexpr size_t min_password_length = 8, max_password_length = 63;
constexpr size_t country_code_length = 2; ///< ISO 3166-1 alpha-2。
} // namespace
bool WifiStation::begin(const char *ssid, const char *password,
                        const char *country, const char *static_ip,
                        const char *gateway, const char *subnet) {
  if (!ssid || !password || !country || std::strlen(ssid) == 0 ||
      std::strlen(ssid) > max_ssid_length ||
      std::strlen(password) < min_password_length ||
      std::strlen(password) > max_password_length ||
      std::strlen(country) != country_code_length)
    return false;
  // 暗黙の再接続や永続化を避ける。
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_STA);
  if (esp_wifi_set_country_code(country, true) != ESP_OK)
    return false;
  if (!WiFi.setBandMode(WIFI_BAND_MODE_5G_ONLY))
    return false;
  if (static_ip && std::strlen(static_ip) > 0) {
    IPAddress ip, gw, mask;
    if (!gateway || !subnet || !ip.fromString(static_ip) ||
        !gw.fromString(gateway) || !mask.fromString(subnet))
      return false; // 固定IP設定が不完全なら接続しない
    if (!WiFi.config(ip, gw, mask))
      return false;
  }
  WiFi.begin(ssid, password);
  return true;
}
} // namespace sakura
