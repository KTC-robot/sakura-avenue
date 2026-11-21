/**
 * @file wifi_station.hpp
 * @brief 私有設定を注入して5GHz限定STA接続を行う。実リンクは未検証。
 */
#pragma once
#include <WiFi.h>

namespace sakura {
/**
 * @brief 5GHz限定のSTA接続アダプター。
 */
class WifiStation {
public:
  /**
   * @brief 指定した資格情報と国コードで接続を開始する。
   * @param static_ip 固定IP。nullptrまたは空ならDHCP。指定時はgateway/subnetも必須。
   * @return 接続要求を開始できた場合true。リンク確立はconnected()で確認する。
   * @note 資格情報はRAMにのみ保持し、フラッシュへ永続化しない。
   */
  bool begin(const char *ssid, const char *password, const char *country_code,
             const char *static_ip = nullptr, const char *gateway = nullptr,
             const char *subnet = nullptr);
  /**
   * @brief 接続状態を取得する。
   * @return IP取得済みならtrue。
   */
  bool connected() const { return WiFi.status() == WL_CONNECTED; }
};
} // namespace sakura
