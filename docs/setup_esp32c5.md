# ESP32-C5 / PlatformIO (Arduino)

## 構成

VS Codeの PlatformIO IDE が認識できるよう、リポジトリルートに `platformio.ini` を置き、`src_dir = platform` でファームウェアを指定します(夏のUnagiFestivalと同じ構成)。春の正規ビルドは `env:esp32c5_rx`(本番)です。実機の確認用に、別envとして `esp32c5_can_selftest` / `esp32c5_motor_bench` / `esp32c5_omni_bench` があります([ベンチ試験](bench.md))。

- platform: pioarduino `55.03.312`(Arduino-ESP32 3.3.12 / ESP-IDF 5.5系)。公式 `espressif32` はESP32-C5未対応のためURLで固定しています。更新時はC5ビルドとAPI差分を再確認してください。
- board: `esp32-c5-devkitc-1` をフラッシュ16MBに上書きしています。モジュールは ESP32-C5-WROOM-1-N16R8 と推定(未確認)です。N16R8用の定義は無く、PSRAMは使わないためPSRAMは有効化しません。型番が違うと分かったら `board_upload.*` とパーティションを直してください。
- C++例外: 純粋制御の設定エラーを例外で拒否するため `-fexceptions` を指定しています。
- 自作ソースのみ `-std=gnu++17 -Wall -Wextra` を適用します。

## ビルド

```bash
pio run -e esp32c5_rx      # 本番ファームのビルドのみ
pio check -e esp32c5_rx    # cppcheck
pio run -e esp32c5_omni_bench   # ベンチ(can_selftest / motor_bench / omni_bench)
```

新しいenvの初回ビルドは、ビルドディレクトリ作成の競合で `opening dependency file` エラーになることがあります。コードの問題ではなく、もう一度実行すれば通ります。

VS Codeでは拡張 `platformio.platformio-ide` を使用します(`pioarduino-ide` とは併用しません)。書き込み(`pio run -e <env> -t upload`)は、車輪を浮かせ、物理非常停止に手が届く状態で行います。

## 私有設定

`platform/config.example.hpp` を `platform/config.local.hpp` にコピーして実値を入れます。`config.local.*` はGit管理外です。

| 項目 | 内容 |
|---|---|
| `wifi_ssid` / `wifi_password` / `wifi_country` | 5GHz APの資格情報(パスワードは8〜63文字)と国コード |
| `peer_ip` / `peer_port` | 許可する送信元(Pi)。`peer_port` はPiの `--source-port` と一致させる。ハートビートの返送先でもある |
| `static_ip` / `static_gateway` / `static_subnet` | ESPの固定IP。3つとも指定、空欄ならDHCP。例 `10.42.0.50` / `10.42.0.1` / `255.255.255.0` |
| `listen_port` | ESPが待ち受けるUDPポート。Piの `--port` と一致させる |

`config.local.hpp` が無い環境(CIなど)では例ファイルが使われ、空欄のため無線を開始せず、駆動もしません(`esp32c5_rx` のビルドは通ります)。

## TWAI

Arduino-ESP32 3.3.12に含まれるESP-IDF 5.5系の新API `esp_twai.h` / `esp_twai_onchip.h`、`twai_new_node_onchip` を使います。受信はISR callbackから `twai_node_receive_from_isr`、その場でコピーしてFreeRTOSキューへ渡し、PIをISRで実行しません。送信バッファーはon_tx_doneまで寿命を維持します。

## 本番ファームの起動

`platform/main.cpp` が、Wi-Fi/UDP受信・安全状態・4輪オムニ・CAN送信を結合しています。起動時は必ずDISARMで、Piからのニュートラル DISARM → ARM → DRIVE でのみ駆動します([アーキテクチャ](architecture.md))。

- 車体設定(寸法・ID配置・符号・速度上限・CANピン・インターロック)は `platform/control/vehicle_config.hpp`。
- 設定が無効(`peer_ip` 空など)なら、駆動せずログを出して待機します。
- Wi-Fiは5GHz限定STA。資格情報は永続化せず、ログに出しません。切断時はFAULT、3秒ごとに再接続を試みます。
- 1秒に1回、状態(`STATUS`)をPiへ返します([仕様](../protocol/specification.md))。
- シリアルログ(115200)は1秒ごとに `[sakura] <STATE> wifi= can= fb= inter= rpm=…` を出します。

物理インターロックのGPIOは未配線(`kInterlockGpio = -1`)で、起動時に警告を出します。その場合は駆動電源の物理遮断のみに依存します。

Classic CAN 1Mbps、標準ID、FD/BRS無効。外付けトランシーバー(WCMCU-1051 = TJA1051T/3 モジュール)を使用します。[配線](wiring.md)を参照してください。

CANは受信(フィードバック)のみ起動時に開始し、電流はARM中と停止直後のゼロ電流5フレームだけ送ります。DISARM中に送らないのは、バスにACK相手がいないとbus offになるためです。CAN異常はDISARM中だけ自動復旧を試みます。

実機未確認の範囲は [検証記録](validation.md) を参照してください。ビルド成功は動作の保証ではありません。
