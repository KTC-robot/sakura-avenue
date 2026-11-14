#include "device/twai_bus.hpp"
#include "driver/gpio.h"
namespace sakura {
bool TwaiBus::received(twai_node_handle_t node,
                       const twai_rx_done_event_data_t *, void *ctx) {
  auto &self = *static_cast<TwaiBus *>(ctx);
  CanFrame f;
  twai_frame_t raw{};
  raw.buffer = f.data.data();
  raw.buffer_len = f.data.size();
  if (twai_node_receive_from_isr(node, &raw) != ESP_OK) {
    self.fault_ = true;
    return false;
  }
  f.id = raw.header.id;
  f.size = raw.header.dlc;
  f.extended = raw.header.ide;
  f.remote = raw.header.rtr;
  f.fd = raw.header.fdf;
  BaseType_t wake = pdFALSE;
  if (xQueueSendFromISR(self.rx_, &f, &wake) != pdTRUE)
    self.fault_ = true;
  return wake == pdTRUE;
}
bool TwaiBus::sent(twai_node_handle_t, const twai_tx_done_event_data_t *event,
                   void *ctx) {
  auto &self = *static_cast<TwaiBus *>(ctx);
  if (!event->is_tx_success)
    self.fault_ = true;
  self.busy_ = false;
  return false;
}
bool TwaiBus::error(twai_node_handle_t, const twai_error_event_data_t *,
                    void *ctx) {
  static_cast<TwaiBus *>(ctx)->fault_ = true;
  return false;
}
bool TwaiBus::changed(twai_node_handle_t,
                      const twai_state_change_event_data_t *event, void *ctx) {
  if (event->new_sta != TWAI_ERROR_ACTIVE)
    static_cast<TwaiBus *>(ctx)->fault_ = true;
  return false;
}
TwaiBus::~TwaiBus() {
  if (node_) {
    twai_node_disable(node_);
    twai_node_delete(node_);
  }
  if (rx_)
    vQueueDelete(rx_);
}
esp_err_t TwaiBus::begin(int tx, int rx) {
  if (node_ || rx_ || tx == rx || !GPIO_IS_VALID_OUTPUT_GPIO(tx) ||
      !GPIO_IS_VALID_GPIO(rx))
    return ESP_ERR_INVALID_ARG;
  rx_ = xQueueCreate(16, sizeof(CanFrame));
  if (!rx_)
    return ESP_ERR_NO_MEM;
  twai_onchip_node_config_t cfg{};
  cfg.io_cfg.tx = static_cast<gpio_num_t>(tx);
  cfg.io_cfg.rx = static_cast<gpio_num_t>(rx);
  cfg.io_cfg.quanta_clk_out = GPIO_NUM_NC;
  cfg.io_cfg.bus_off_indicator = GPIO_NUM_NC;
  cfg.bit_timing.bitrate = 1000000;
  cfg.tx_queue_depth = 1;
  cfg.fail_retry_cnt = 0;
  auto result = twai_new_node_onchip(&cfg, &node_);
  if (result != ESP_OK)
    return result;
  twai_event_callbacks_t cb{};
  cb.on_rx_done = received;
  cb.on_tx_done = sent;
  cb.on_error = error;
  cb.on_state_change = changed;
  result = twai_node_register_event_callbacks(node_, &cb, this);
  if (result != ESP_OK)
    return result;
  fault_ = false;
  result = twai_node_enable(node_);
  if (result != ESP_OK)
    fault_ = true;
  return result;
}
bool TwaiBus::send(const CanFrame &f) {
  // Fail latched; zero-current frames can still be attempted. Never queue stale
  // drive frames.
  if (!node_ || f.size != 8 || f.id != c620_command_id || f.extended ||
      f.remote || f.fd)
    return false;
  for (uint8_t b : f.data)
    if (fault_ && b != 0)
      return false;
  bool expected = false;
  if (!busy_.compare_exchange_strong(expected, true)) {
    fault_ = true;
    return false;
  }
  tx_bytes_ = f.data;
  tx_frame_ = {};
  tx_frame_.header.id = f.id;
  tx_frame_.header.dlc = 8;
  tx_frame_.buffer = tx_bytes_.data();
  tx_frame_.buffer_len = 8;
  if (twai_node_transmit(node_, &tx_frame_, 0) != ESP_OK) {
    busy_ = false;
    fault_ = true;
    return false;
  }
  return true;
}
bool TwaiBus::read(CanFrame &f) {
  return rx_ && xQueueReceive(rx_, &f, 0) == pdTRUE;
}
} // namespace sakura
