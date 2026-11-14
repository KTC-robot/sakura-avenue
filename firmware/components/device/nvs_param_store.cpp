#include "nvs_param_store.hpp"
#include "nvs.h"
#include "nvs_flash.h"
#include <cstddef>
#include <cstring>
namespace sakura {
namespace {
constexpr const char *ns = "sakura";
constexpr const char *key = "pi_gains";
constexpr uint32_t magic = 0x53474131; // "SGA1"
struct Blob {
  uint32_t magic;
  SpeedGains gains;
  uint32_t crc;
};
uint32_t crc32(const uint8_t *p, size_t n) {
  uint32_t c = 0xFFFFFFFFU;
  for (size_t i = 0; i < n; i++) {
    c ^= p[i];
    for (int k = 0; k < 8; k++)
      c = (c >> 1) ^ (0xEDB88320U & (0U - (c & 1U)));
  }
  return ~c;
}
uint32_t blob_crc(const Blob &b) {
  return crc32(reinterpret_cast<const uint8_t *>(&b), offsetof(Blob, crc));
}
} // namespace
bool NvsParamStore::begin() {
  esp_err_t e = nvs_flash_init();
  if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    if (nvs_flash_erase() != ESP_OK)
      return false;
    e = nvs_flash_init();
  }
  return e == ESP_OK;
}
bool NvsParamStore::load(SpeedGains &g) {
  nvs_handle_t h;
  if (nvs_open(ns, NVS_READONLY, &h) != ESP_OK)
    return false;
  Blob b{};
  size_t n = sizeof(b);
  esp_err_t e = nvs_get_blob(h, key, &b, &n);
  nvs_close(h);
  if (e != ESP_OK || n != sizeof(b) || b.magic != magic ||
      b.crc != blob_crc(b) || !gains_valid(b.gains))
    return false;
  g = b.gains;
  return true;
}
bool NvsParamStore::save(const SpeedGains &g) {
  if (!gains_valid(g))
    return false;
  Blob b{};
  b.magic = magic;
  b.gains = g;
  b.crc = blob_crc(b);
  nvs_handle_t h;
  if (nvs_open(ns, NVS_READWRITE, &h) != ESP_OK)
    return false;
  bool ok = nvs_set_blob(h, key, &b, sizeof(b)) == ESP_OK &&
            nvs_commit(h) == ESP_OK;
  nvs_close(h);
  return ok;
}
bool NvsParamStore::erase() {
  nvs_handle_t h;
  if (nvs_open(ns, NVS_READWRITE, &h) != ESP_OK)
    return false;
  esp_err_t e = nvs_erase_key(h, key);
  bool ok = (e == ESP_OK || e == ESP_ERR_NVS_NOT_FOUND) && nvs_commit(h) == ESP_OK;
  nvs_close(h);
  return ok;
}
} // namespace sakura
