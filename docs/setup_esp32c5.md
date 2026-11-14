# ESP32-C5 / PlatformIO (Arduino)

## 構成

VS Codeの PlatformIO IDE が認識できるよう、リポジトリルートに `platformio.ini` を置き、`src_dir = platform` でファームウェアを指定します(夏のUnagiFestivalと同じ構成)。春の正規ビルドは `env:esp32c5_rx` です。

- platform: pioarduino `55.03.312`(Arduino-ESP32 3.3.12 / ESP-IDF 5.5系)。公式 `espressif32` はESP32-C5未対応のためURLで固定しています。更新時はC5ビルドとAPI差分を再確認してください。
- board: `esp32-c5-devkitc-1` をフラッシュ16MBに上書きしています。モジュールは ESP32-C5-WROOM-1-N16R8 と推定(未確認)です。N16R8用の定義は無く、PSRAMは使わないためPSRAMは有効化しません。型番が違うと分かったら `board_upload.*` とパーティションを直してください。
- C++例外: 純粋制御の設定エラーを例外で拒否するため `-fexceptions` を指定しています。
- 自作ソースのみ `-std=gnu++17 -Wall -Wextra` を適用します。

## ビルド

```bash
pio run -e esp32c5_rx      # ビルドのみ
pio check -e esp32c5_rx    # cppcheck
```

VS Codeでは拡張 `platformio.platformio-ide` を使用します(`pioarduino-ide` とは併用しません)。書き込み(`pio run -t upload`)は実機確認後に行います。

## 私有設定

`platform/config.example.hpp` を `platform/config.local.hpp` にコピーしてSSID・パスワード・国コードを入れます。`config.local.*` はGit管理外です。

## TWAI

Arduino-ESP32 3.3.12に含まれるESP-IDF 5.5系の新API `esp_twai.h` / `esp_twai_onchip.h`、`twai_new_node_onchip` を使います。受信はISR callbackから `twai_node_receive_from_isr`、その場でコピーしてFreeRTOSキューへ渡し、PIをISRで実行しません。送信バッファーはon_tx_doneまで寿命を維持します。

## 実機前の境界

`setup()` はDISARMログのみ。SSID/パスワード・GPIO・実機幾何を仮定せず、無線・CAN・モーター制御タスクは起動しません。`TwaiBus::begin(tx,rx)` はGPIOを明示して初めて呼べるアダプターです。Classic CAN 1Mbps、標準ID、FD/BRS無効で使用します。外付けトランシーバーが必要です。

5GHz接続では国設定・チャネル・対応ボード・アンテナ・OSのAP機能を確認し、`WiFi.setBandMode(WIFI_BAND_MODE_5G_ONLY)` 等を接続実装時に検証してください。`WifiStation` は5GHz限定STA接続を実装しています。私有設定からSSID・パスワード・国コードを渡す必要があります。資格情報は永続化せず(`WiFi.persistent(false)`)ログには出しません。`setup()` は呼び出さず、無線接続は未実証です。

フラッシュ、Wi-Fiリンク、CAN信号、RPM、停止時間はいずれも実機未確認です。ビルド成功はこれらの保証ではありません。
