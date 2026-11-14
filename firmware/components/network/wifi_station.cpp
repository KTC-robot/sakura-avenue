#include "wifi_station.hpp"
#include <cstring>
namespace sakura {
void WifiStation::event(void *context, esp_event_base_t base, int32_t id,
                        void *) {
  auto &self = *static_cast<WifiStation *>(context);
  if (base == WIFI_EVENT &&
      (id == WIFI_EVENT_STA_DISCONNECTED || id == WIFI_EVENT_STA_STOP))
    self.connected_ = false;
  if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP)
    self.connected_ = false;
  if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
    self.connected_ = true;
  // No implicit reconnection or drive permission here.
}
WifiStation::~WifiStation() {
  connected_ = false;
  if (started_)
    esp_wifi_stop();
  if (wifi_handler_)
    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                          wifi_handler_);
  if (ip_handler_)
    esp_event_handler_instance_unregister(IP_EVENT, ESP_EVENT_ANY_ID,
                                          ip_handler_);
  if (initialized_)
    esp_wifi_deinit();
  if (netif_)
    esp_netif_destroy_default_wifi(netif_);
}
esp_err_t WifiStation::begin(const char *ssid, const char *password,
                             const char *country) {
  if (netif_ || !ssid || !password || !country || std::strlen(ssid) == 0 ||
      std::strlen(ssid) > 32 || std::strlen(password) < 8 ||
      std::strlen(password) > 63 || std::strlen(country) != 2)
    return ESP_ERR_INVALID_ARG;
  netif_ = esp_netif_create_default_wifi_sta();
  if (!netif_)
    return ESP_ERR_NO_MEM;
  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  auto err = esp_wifi_init(&init);
  if (err != ESP_OK)
    return err;
  initialized_ = true;
  // RAM storage avoids writing injected credentials into NVS by this adapter.
  if ((err = esp_wifi_set_storage(WIFI_STORAGE_RAM)) != ESP_OK)
    return err;
  if ((err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                 event, this,
                                                 &wifi_handler_)) != ESP_OK)
    return err;
  if ((err = esp_event_handler_instance_register(
           IP_EVENT, ESP_EVENT_ANY_ID, event, this, &ip_handler_)) != ESP_OK)
    return err;
  if ((err = esp_wifi_set_mode(WIFI_MODE_STA)) != ESP_OK)
    return err;
  if ((err = esp_wifi_set_country_code(country, true)) != ESP_OK)
    return err;
  if ((err = esp_wifi_set_band_mode(WIFI_BAND_MODE_5G_ONLY)) != ESP_OK)
    return err;
  wifi_config_t config{};
  std::memcpy(config.sta.ssid, ssid, std::strlen(ssid));
  std::memcpy(config.sta.password, password, std::strlen(password));
  config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
  err = esp_wifi_set_config(WIFI_IF_STA, &config);
  std::memset(&config, 0, sizeof(config));
  if (err != ESP_OK)
    return err;
  if ((err = esp_wifi_start()) != ESP_OK)
    return err;
  started_ = true;
  return esp_wifi_connect();
}
} // namespace sakura
