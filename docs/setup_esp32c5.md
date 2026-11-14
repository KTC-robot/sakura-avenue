# ESP32-C5 / ESP-IDF

## バージョンの固定

初期検証には **ESP-IDF v5.5.1**（commit `fcae32885b0296b32044cb99ecbdc50d98dddb83`）を固定します。ESP32-C5対応と新TWAI APIを備えた再現可能な基準として選びました。最新推奨版という意味ではありません。アップデート時はC5ビルドとAPI差分を再確認してください。

公式資料:

- [ESP32-C5 TWAI v5.5.1](https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32c5/api-reference/peripherals/twai.html)
- [5.5 peripheral migration](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32c5/migration-guides/release-5.x/5.5/peripherals.html)
- [ESP32-C5 Wi-Fi v5.5.1](https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32c5/api-reference/network/esp_wifi.html)

C5は旧 `driver/twai.h` APIの単純移植ではなく `esp_twai.h` / `esp_twai_onchip.h`、`twai_new_node_onchip` を使います。実装はv5.5.1の実ヘッダーと照合しています。受信はISR callbackから `twai_node_receive_from_isr`、その場でコピーしてFreeRTOSキューへ渡し、PIをISRで実行しません。送信バッファーはon_tx_doneまで寿命を維持します。

## ローカルSDK

```bash
git clone --recursive --branch v5.5.1 https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32c5
. ./export.sh
# sakura-avenue/firmware へ移動
idf.py set-target esp32c5
idf.py build
```

## 公式Dockerイメージ

リポジトリルートで実行します。これはビルドのみで書き込みません。

```bash
docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp   -v "$PWD:/project" -w /project/firmware   espressif/idf:v5.5.1 bash -lc 'idf.py set-target esp32c5 && idf.py build'
```

CIも同じタグを使用します。生成sdkconfig/buildはGit管理しません。`sdkconfig.defaults` は共通設定のみです。C++純粋制御の設定エラーは例外で拒否するためC++例外を有効化しています。

## 実機前の境界

`app_main` はDISARMログのみ。SSID/パスワード・GPIO・実機幾何を仮定せず、無線・CAN・モーター制御タスクは起動しません。`TwaiBus::begin(tx,rx)` はGPIOを明示して初めて呼べるアダプターです。Classic CAN 1Mbps、標準ID、FD/BRS無効で使用します。外付けトランシーバーが必要です。

5GHz接続では国設定・チャネル・対応ボード・アンテナ・OSのAP機能を確認し、`esp_wifi_set_band_mode(WIFI_BAND_MODE_5G_ONLY)` 等を接続実装時に検証してください。`WifiStation` は5GHz限定STA接続と接続/切断フラグを実装しています。呼び出し側がNVS/netif/イベントループを準備し、私有設定からSSID・パスワード・国コードを渡す必要があります。資格情報はRAM保存を指定しログには出しません。app_mainは呼び出さず、無線接続は未実証です。

フラッシュ、Wi-Fiリンク、CAN信号、RPM、停止時間はいずれも実機未確認です。ビルド成功はこれらの保証ではありません。
