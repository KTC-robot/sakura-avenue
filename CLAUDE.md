# sakura-avenue (LLM/エージェント向け入口)

2027関西春ロボコン用。Raspberry Pi(Python)がPSコントローラーを読み、5GHz Wi-Fi/UDPでESP32-C5へ指令を送り、ESP32-C5が運動学・PI速度制御・CAN(C620×4)を担当する4輪オムニロボット。**実機駆動前の整備段階。** 構成の詳細は [docs/architecture.md](docs/architecture.md)、通信仕様は [protocol/specification.md](protocol/specification.md)。

## 絶対に崩さない不変条件
- `firmware/main/main.cpp` の `app_main` は Wi-Fi/CAN/モーターを起動しない。起動処理を足して出力を有効化しない(認証・物理インターロック・実タスクが未完成)。
- 出力はフェイルセーフ側に倒す。通信断・CAN異常・feedback失効・周期異常は FAULT でゼロ電流。復帰は「ニュートラルDISARM → 別パケットの明示ARM」。通信回復だけで再許可しない。
- 走行パケット(`SA`, 20B)とサービスパケット(`SC`)は別系統。サービス側の不正入力で FAULT にしない。サービスの PARAM_SET/RESET は DISARM 中のみ。
- プロトコルを変えるときは `protocol/specification.md`、`protocol/*golden_vectors.json`、Python `protocol.py`/`service_protocol.py`、C++ `protocol.hpp`/`service_protocol.hpp` を同時に更新し、両言語のテストを通す。
- 幾何・半径・減速比・符号・GPIO・速度上限・SSID/IP は未確定。テスト内の値(半径0.1m、減速比2等)は架空で、実機設定へ転用しない。
- 秘密情報(Wi-Fi資格情報)は `firmware/sdkconfig.local` のみ。`sdkconfig.defaults` やコミットに入れない。生成物 `sdkconfig*`・`.pio/`・`build/` は git 管理外。
- 「ビルド成功」「テスト成功」を「実機で動作確認済み」と書かない。未検証は未検証と書く。

## コマンド
```bash
# Python (raspberryPi/)
uv sync --locked --extra controller
uv run ruff check . && uv run ruff format --check . && uv run pytest -q

# ホストC++ (リポジトリルート)。実機不要
cmake -S firmware/tests -B build/host -DCMAKE_BUILD_TYPE=Debug
cmake --build build/host && ctest --test-dir build/host --output-on-failure

# ESP32-C5ファームウェア (firmware/)。PlatformIO標準。ESP-IDF 5.5.x(PIOは5.5.5同梱)
pio run
```
CIは同じ3系統(`.github/workflows/ci.yml`)。ローカルESP-IDFやDockerでのビルド手順は廃止した。

## 規約
- 言語: コメント・ドキュメントは日本語。Python docstring と C++ Doxygen の書き方は [docs/development.md](docs/development.md)。
- 座標: +X前、+Y左、+wz反時計回り。正規化指令は -1..+1。
- 過去資料(夏からの移植記録、旧検証記録、旧Arduinoテンプレート)は [archive/](archive/README.md)。現行の根拠として使わない。
