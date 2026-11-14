/**
 * @file wifi_station.hpp
 * @brief 私有設定を注入して5GHz限定STA接続を行う。実リンクは未検証。
 */
#pragma once
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include <atomic>
namespace sakura {
/**
 * @brief 5GHz限定のSTA接続アダプター。
 * @note NVS/netif/既定イベントループは事前初期化する。
 */
class WifiStation {
  esp_netif_t *netif_ = nullptr;
  esp_event_handler_instance_t wifi_handler_ = nullptr, ip_handler_ = nullptr;
  std::atomic<bool> connected_{false};
  bool initialized_ = false, started_ = false;
  static void event(void *, esp_event_base_t, int32_t, void *);

public:
  WifiStation() = default;
  WifiStation(const WifiStation &) = delete;
  WifiStation &operator=(const WifiStation &) = delete;
  ~WifiStation();
  /**
   * @brief 指定した資格情報と国コードで接続を開始する。
   * @return ESP_OKまたはSDKエラー。リンク確立は別途確認する。
   * @note 資格情報はRAMに保存する。
   */
  esp_err_t begin(const char *ssid, const char *password,
                  const char *country_code);
  /**
   * @brief 接続状態を取得する。
   * @return IP取得済みならtrue。
   */
  bool connected() const { return connected_; }
};
} // namespace sakura
