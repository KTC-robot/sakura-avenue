/**
 * @file twai_bus.hpp
 * @brief ESP-IDF 5.5.1の新TWAI APIを用いるC620用ベンチアダプター。
 */
#pragma once
#include "device/c620.hpp"
#include "esp_twai.h"
#include "esp_twai_onchip.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <atomic>
namespace sakura {
/**
 * @brief 単一タスクで使用するTWAIアダプター。
 */
class TwaiBus {
  twai_node_handle_t node_ = nullptr;
  QueueHandle_t rx_ = nullptr;
  std::atomic<bool> fault_{true}, busy_{false};
  std::atomic<uint32_t> last_error_{0}, error_count_{0}, state_count_{0},
      rx_dropped_{0};
  std::array<uint8_t, 8> tx_bytes_{};
  twai_frame_t
      tx_frame_{}; // on_tx_doneまでフレームとバッファーの寿命を維持する。
  static bool received(twai_node_handle_t, const twai_rx_done_event_data_t *,
                       void *);
  static bool sent(twai_node_handle_t, const twai_tx_done_event_data_t *,
                   void *);
  static bool changed(twai_node_handle_t,
                      const twai_state_change_event_data_t *, void *);
  static bool error(twai_node_handle_t, const twai_error_event_data_t *,
                    void *);

public:
  TwaiBus() = default;
  TwaiBus(const TwaiBus &) = delete;
  TwaiBus &operator=(const TwaiBus &) = delete;
  ~TwaiBus();
  /**
   * @brief 指定GPIOでClassic CAN 1Mbpsを開始する。
   * @return ESP_OKまたはSDKエラー。
   * @note 外付けトランシーバーが必要。
   */
  esp_err_t begin(int tx_gpio, int rx_gpio);
  /**
   * @brief 1フレームを非同期送信する。
   * @return 送信要求を受け付けた場合true。
   * @note falseなら呼び出し側でFAULT処理を行う。
   */
  bool send(const CanFrame &frame);
  /**
   * @brief 受信キューからフレームを取得する。
   * @return フレームを取得した場合true。
   */
  bool read(CanFrame &frame);
  /**
   * @brief TWAIの健全性を検査する。
   * @return 初期化済みで異常ラッチがなければtrue。
   */
  bool healthy() const { return node_ && !fault_; }
  /**
   * @brief 最後のエラー(1=arb,2=bit,4=form,8=stuff,16=ack,0x200=rx取得失敗)。
   */
  uint32_t last_error() const { return last_error_; }
  uint32_t error_count() const { return error_count_; }
  /// 受信キューあふれで捨てたフレーム数(FAULTにはしない)。
  uint32_t rx_dropped() const { return rx_dropped_; }
  uint32_t state_change_count() const { return state_count_; }
  /**
   * @brief ノード状態を取得する。
   * @return 取得できた場合true。
   */
  bool info(twai_node_status_t &status) const;
  /**
   * @brief 操作者の明示操作で異常ラッチを解除する。
   * @return 解除できた(ERROR_ACTIVEでバスオフでない)場合true。
   * @note BUS_OFFならリカバリーを開始するだけで、解除は再度の呼び出しで行う。
   */
  bool recover();
};
} // namespace sakura
