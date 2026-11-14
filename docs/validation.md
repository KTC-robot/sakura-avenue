# 初期整備の検証記録

実施日: 2026-11-14。実機接続・書き込みは行っていません。

| 検証 | 環境/コマンド | 結果 |
|---|---|---|
| Python依存解決 | uv 0.11.17 / Python 3.12.13、`uv sync --extra controller` | 成功、uv.lock作成 |
| Python静的解析 | `uv run ruff check .` | 成功 |
| Python書式 | `uv run ruff format --check .` | 成功 |
| Python単体/結合 | `uv run pytest -q` | **32 passed** |
| ホストC++ | GNU C++ 16.2.1、CMake Debug、`cmake --build build/host` | 成功、-Wall -Wextra -Werror |
| C++テスト | `ctest --test-dir build/host --output-on-failure` | **1/1 suite passed**（複数の検証assertを含む） |
| ESP32-C5ビルド | 公式 `espressif/idf:v5.5.1`、`idf.py set-target esp32c5` / `idf.py build` | 成功、C5 .bin生成。新TWAI・UDP・5GHz STAアダプターをコンパイル |
| 再利用元 | reference cloneの `git status --short` | 変更なし |
| GitHub Actions | workflow追加 | リモートCIは未実行 |

公式イメージの取得digest: `sha256:dfa2d076c796769c07c155eba6c672b9f395aec943b2ba3701b73379b5f9e884`。
ビルドログには、ホストUIDでの実行に伴うSDK同梱openthreadリポジトリの所有者警告が一度出ましたが、configureとビルドの終了コードは0でした。これはC5通信の実証ではありません。イメージのグローバルGit設定やホストの安全ディレクトリ設定は変更していません。

## テスト範囲

Python: 軸正規化・クランプ・deadzone・欠損情報、slowと座標符号、ボタン押下/解放/repeat拒否、デバイス選別とhandle解放、SYN_DROPPED、切断時のCLI DISARM/終了処理、共通ベクトル、長さ/ヘッダー/NaN/Inf/範囲不正、タイムアウト/STOP/FAULT/再許可/sequence wrap、localhost実UDP送受信。

C++: テスト専用4輪配置の前後/左右/旋回の方向とRPM・減速比・motor_index、半径/減速比/番号/符号/有限値/rank検証、PIと積分reset/anti-windup/電流飽和、slew/一括スケール、C620パック/feedback/RPM/期限、共通JSONの意味値とbytes両方、バージョン/長さ/NaN拒否、状態遷移、信頼送信元制限、指令期限、feedback失効からのゼロ電流/RPMリセット。

テストは既知の架空配置・半径0.1m・減速比2などをfixture内だけで使用します。実機設定には含めません。32件のPythonテストと1つのC++suiteは、実機での動作保証や安全認証を意味しません。

## 未検証

PS実デバイスでの軸・ボタン・無線切断、Piの5GHz AP、C5の無線接続/切断、UDP実ネットワークの遅延/欠落、TWAI ISR/送信完了/バスオフ、C620との実通信、M3508回転方向、実RPM、PIゲイン、物理非常停止、実停止距離/時間、実時間タスクは未検証です。

## 次に行う開発

1. Pi/C5ボード・トランシーバー・電源遮断回路を決定する。
2. 権利条件を確認し、実機幾何・半径・実減速比・取付符号・GPIO・速度上限を測定/記録する。
3. アプリケーション認証、セッションnonce、送信期限、設定ローダー、ニュートラル/ARM UIを実装する。
4. Wi-Fi/UDP/CAN/インターロックを実タスクへ結合し、電源OFFで切断・異常復帰試験を行う。
5. 物理非常停止を確認した後、浮かせた車輪でCAN・符号・PIを段階的に調整する。

## ドキュメント・コメント整備後の確認

初期整備のコミットを取り消し、実装を作業ツリーへ保持した上で、夏のdocstring/Doxygen形式に合わせて日本語コメントを追加しました。

- Python: Ruff check/formatとpytestを再実行し、32件成功。
- C++: ホストビルドとCTestを再実行し、1/1 suite成功。
- 取り消し前の実装と比較し、Python 10ファイルはdocstringを除いたAST、C++ 16ファイルはコメント・書式を除いたtokenが一致。実行処理の変更なし。
- README/docs内のローカルMarkdownリンクに参照切れなし。
- ESP32-C5ビルドは上表の初期整備時の結果。今回のコメント整備後には再実行していない。

追加したオフライン準備手順は対象Piで未検証です。夏のPi 5/aarch64前提を春の実機条件として確定させていません。

## ログ確認・PIゲイン変更（サービスパケット）

- Python: `uv run ruff check/format --check` と pytest で52件成功（サービス関連20件。ゴールデンベクタ、再送・タイムアウト・他送信元無視、CSV保存、欠落検知、事前範囲検証、模擬ESPとのループバック、`sakura-pilot --console`の対話処理）。
- C++: ホストビルドとCTestで成功。ゴールデンベクタ、ゲイン検証と反映、DISARM以外での拒否、lease切れ、不正パケットでFAULTにならないこと、NVS破損時の既定値フォールバックをモックで確認。
- `udp_socket.cpp`と`nvs_param_store.cpp`は実ESP-IDFヘッダーがない環境のため、スタブヘッダーで構文確認のみ。**C5ビルド（idf.py）は未実行。** CIで確認が必要です。
- 未検証: 実機での送受信、NVSの実書込み、実フィードバックでのログ、ゲイン変更後の挙動。`app_main`が何も起動しないため実機では動作しません（architecture.md参照）。
