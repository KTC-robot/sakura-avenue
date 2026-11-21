/**
 * @file can_selftest.cpp
 * @brief ESP32-C5 -> CANトランシーバー -> 受信ピンの経路を、C620なしで確認する。
 *
 * セルフテストモード(ACK不要)で0x200を送り続け、自分の送信が受信ピンで
 * 正しく読み返されるかを数える。バスにつながるのは自分だけでよい。
 * モーターに電流指令は出さない(データは全て0)。
 */
#include "esp_twai.h"
#include "esp_twai_onchip.h"
#include <Arduino.h>

namespace {
#ifndef CAN_TX_GPIO
#define CAN_TX_GPIO 23
#endif
#ifndef CAN_RX_GPIO
#define CAN_RX_GPIO 24
#endif
constexpr int kTx = CAN_TX_GPIO, kRx = CAN_RX_GPIO;
volatile uint32_t txOk, txFail, rxCount, errBit, errForm, errStuff, errAck,
    errArb, stateChanges;
twai_node_handle_t node;
uint8_t buf[8] = {};
twai_frame_t frame{};

bool onTx(twai_node_handle_t, const twai_tx_done_event_data_t *e, void *) {
  (e->is_tx_success ? txOk : txFail) = (e->is_tx_success ? txOk : txFail) + 1;
  return false;
}
bool onRx(twai_node_handle_t n, const twai_rx_done_event_data_t *, void *) {
  uint8_t d[8];
  twai_frame_t f{};
  f.buffer = d;
  f.buffer_len = 8;
  if (twai_node_receive_from_isr(n, &f) == ESP_OK)
    rxCount = rxCount + 1;
  return false;
}
bool onErr(twai_node_handle_t, const twai_error_event_data_t *e, void *) {
  if (e->err_flags.bit_err)
    errBit = errBit + 1;
  if (e->err_flags.form_err)
    errForm = errForm + 1;
  if (e->err_flags.stuff_err)
    errStuff = errStuff + 1;
  if (e->err_flags.ack_err)
    errAck = errAck + 1;
  if (e->err_flags.arb_lost)
    errArb = errArb + 1;
  return false;
}
bool onState(twai_node_handle_t, const twai_state_change_event_data_t *,
             void *) {
  stateChanges = stateChanges + 1;
  return false;
}
} // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  // TWAIを使わず、TXピンを出力、RXピンを入力として直結の導通を確認する。
  {
    pinMode(kRx, INPUT);
    pinMode(kTx, OUTPUT);
    int ok = 0;
    String seq;
    for (int i = 0; i < 6; i++) {
      int w = i & 1;
      digitalWrite(kTx, w);
      delay(2);
      int r = digitalRead(kRx);
      seq += String(w) + "->" + String(r) + " ";
      if (r == w)
        ok++;
    }
    Serial.printf("[selftest] 直結 TX=%d -> RX=%d : %s (%d/6) 書込->読出: %s\n",
                  kTx, kRx, ok == 6 ? "PASS" : "FAIL", ok, seq.c_str());
    // 各ピンを単独で入力にし、内部プルアップ/ダウンで外部に引かれていないか見る。
    pinMode(kTx, INPUT);
    int pins[2] = {kTx, kRx};
    for (int p : pins) {
      pinMode(p, INPUT_PULLUP);
      delay(2);
      int up = digitalRead(p);
      pinMode(p, INPUT_PULLDOWN);
      delay(2);
      int dn = digitalRead(p);
      pinMode(p, INPUT);
      Serial.printf("[selftest] GPIO%d 内部PU時=%d PD時=%d (他方が浮いていれば"
                    "PU=1,PD=0)\n",
                    p, up, dn);
    }
  }
  twai_onchip_node_config_t cfg{};
  cfg.io_cfg.tx = static_cast<gpio_num_t>(kTx);
  cfg.io_cfg.rx = static_cast<gpio_num_t>(kRx);
  cfg.io_cfg.quanta_clk_out = GPIO_NUM_NC;
  cfg.io_cfg.bus_off_indicator = GPIO_NUM_NC;
  cfg.bit_timing.bitrate = 1000000;
  cfg.tx_queue_depth = 1;
  cfg.fail_retry_cnt = 0;
  cfg.flags.enable_self_test = 1; // ACK不要
  cfg.flags.enable_loopback = 1;  // 自分の送信を受信側へ返す
  esp_err_t r = twai_new_node_onchip(&cfg, &node);
  twai_event_callbacks_t cb{};
  cb.on_tx_done = onTx;
  cb.on_rx_done = onRx;
  cb.on_error = onErr;
  cb.on_state_change = onState;
  if (r == ESP_OK)
    r = twai_node_register_event_callbacks(node, &cb, nullptr);
  if (r == ESP_OK)
    r = twai_node_enable(node);
  Serial.printf("[selftest] TX=%d RX=%d init=%s (電流は常に0)\n", kTx, kRx,
                esp_err_to_name(r));
  frame.header.id = 0x200;
  frame.header.dlc = 8;
  frame.buffer = buf;
  frame.buffer_len = 8;
}

void loop() {
  static uint32_t lastTx = 0, lastLog = 0;
  uint32_t now = millis();
  if (node && now - lastTx >= 100) {
    lastTx = now;
    twai_node_transmit(node, &frame, 0);
  }
  if (now - lastLog >= 1000) {
    lastLog = now;
    twai_node_status_t st{};
    twai_node_record_t rec{};
    if (node)
      twai_node_get_info(node, &st, &rec);
    Serial.printf(
        "tx ok=%lu fail=%lu | rx=%lu | err bit=%lu form=%lu stuff=%lu "
        "ack=%lu arb=%lu | state=%d tec=%u rec=%u\n",
        (unsigned long)txOk, (unsigned long)txFail, (unsigned long)rxCount,
        (unsigned long)errBit, (unsigned long)errForm, (unsigned long)errStuff,
        (unsigned long)errAck, (unsigned long)errArb, (int)st.state,
        st.tx_error_count, st.rx_error_count);
  }
}
