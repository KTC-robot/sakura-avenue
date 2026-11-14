# 2027関西春ロボコン — sakura-avenue

Raspberry Pi（Python）でPSコントローラーを読み、5GHz Wi-Fi / UDPでESP32-C5へ指令を送る、4輪オムニロボットの開発基盤です。M3508 P19 ×4 / C620 ×4、Classic CAN 1Mbps、夏のPI速度制御を継承します。ESP-NOW、IM920、送信専用ESP32は使用しません。

**現在は実機駆動前の初期整備段階です。** firmwareの起動処理はDISARMを表示するだけでWi-Fi/CANを開始しません。Pi CLIは入力プレビューとDISARM送信のみです。純粋な制御モジュール・UDPアダプター・状態管理のテストは実施できますが、無線操縦できる完成機ではありません。

## ハードウェアと役割

| 項目 | 役割・状態 |
|---|---|
| Raspberry Pi + PlayStationコントローラー | evdev入力・正規化・操作指令生成・UDP送信。Pi型番未定 |
| 5GHz IEEE 802.11 | PiのAPまたは外部ルーター。同じUDPペイロードを使用 |
| ESP32-C5 | パケット検証・安全管理・運動学・PI・CAN。開発ボード未定 |
| M3508 P19 / C620 各4台 | モーター軸RPMフィードバック・電流指令 |
| 外付けCANトランシーバー | 必須。型番・GPIO・配線は未確定 |
| 物理非常停止 | 駆動電源を遮断する独立回路が必須。ソフトウェア停止では代替不可 |

## 構成

- `raspberryPi/`: uv管理のPythonパッケージ、evdevアダプター、入力モデル、マッパー、UDP、テスト
- `firmware/`: ESP-IDF 5.5.1 / ESP32-C5プロジェクト。`components/control` は純粋C++、`protocol` は共通パケット、`device` はC620とTWAI、`network` はUDPと制御ループ
- `protocol/`: バイナリー仕様とPython/C++共通テストベクトル
- `docs/`: セットアップ、ネットワーク、安全設計、配線、移植記録、検証結果
- `legacy/arduino-template/`: 元のArduino/PlatformIOテンプレートの保存用。C5用のビルド対象ではありません
- `.github/workflows/ci.yml`: Python解析・テスト、ホストC++、C5ビルド

## 開発を始める

必要環境: Git、Python 3.12以上、uv、ホストC++17コンパイラー、CMake 3.19以上。C5ビルドにはESP-IDF **v5.5.1** または同タグの公式Dockerイメージを使います。

```bash
git clone https://github.com/KTC-robot/sakura-avenue.git
cd sakura-avenue/raspberryPi
uv sync --locked --extra controller
uv run ruff check .
uv run ruff format --check .
uv run pytest -q
uv run sakura-pilot --verbose   # 入力プレビュー。モーター駆動なし
# フィードバックログ表示とPIゲイン変更（ESP32の信頼済み送信元portを指定）
uv run sakura-tune --host <ESP32> --port <PORT> --source-port <PORT> monitor --csv log.csv
uv run sakura-tune --host <ESP32> --port <PORT> --source-port <PORT> param set --kp 1.2 --save
# 操縦と同じプロセスで実行する場合（標準入力に monitor / param ... を入力）
uv run sakura-pilot --host <ESP32> --port <PORT> --source-port <PORT> --console
```

```bash
# リポジトリルートから。実機不要
cmake -S firmware/tests -B build/host -DCMAKE_BUILD_TYPE=Debug
cmake --build build/host
ctest --test-dir build/host --output-on-failure

# ESP-IDF v5.5.1 の export.sh を読み込んだシェルで
cd firmware
idf.py set-target esp32c5
idf.py build
```

ドキュメントの読み順と夏の資料との対応は [docs一覧](docs/README.md)を参照してください。

詳しくは [Piセットアップ](docs/setup_pi.md)、[ESP32-C5セットアップ](docs/setup_esp32c5.md)、[検証記録](docs/validation.md)を参照してください。今回、実機への書き込みは行いません。

## 通信と安全

v1は20バイトのUDPパケット。正規化した `vx/vy/wz`、バージョン、シーケンス、DISARM/ARM/DRIVE/STOPを持ちます。車体は+X前、+Y左、+Z反時計回り。正規化指令からSI速度への換算上限は実機設定が必要です。[仕様](protocol/specification.md)を両言語の基準とします。

50Hz、指令タイムアウト180ms、CANフィードバック100msは初期検証値です。通信・Wi-Fi・CAN・フィードバック・インターロック異常時はFAULTにして、目標RPMとPI積分をリセットしゼロ電流フレームを生成します。復帰にはニュートラルDISARM、その後の明示ARMが必要です。送信失敗時にはゼロ電流の到達も保証できません。独立した駆動電源遮断が必要です。

現状の送信元IP/port検証とWi-Fi暗号化だけではアプリケーション認証になりません。**認証・リプレイ保護・非常停止連携を整備するまで実機の駆動経路に接続しないでください。** [ネットワーク設計](docs/network.md)、[安全状態と構成](docs/architecture.md)に制約を記載しています。

## 未確定・未実装

Pi/開発ボード型番、車輪位置・駆動方向・半径・実減速比・モーター符号、GPIO、速度上限、SSID/パスワード/IP、競技機構は未確定です。`firmware/config.example.json` は未設定項目のスキーマ例で、ファームウェアは読み込みません。テスト用の幾何配置は実機設定に使いません。

5GHz Wi-Fiの接続・切断イベントの実タスクへの統合、アプリケーション認証、ARM操作UI、設定ローダー、実機制御タスクの開始、物理インターロック、PI調整、実機検証は今後の作業です。初期整備では安全に出力禁止のまま残しています。

夏のコードの出典・差分・権利確認事項は [移植記録](docs/migration.md)を参照してください。
