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
| ESP32-C5ビルド | `pio run -e esp32c5_rx`(pioarduino 55.03.312 / Arduino-ESP32 3.3.12) | 成功、C5 .bin生成。新TWAI・UDP・5GHz STAアダプターをコンパイル |
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

## 実機ベンチの検証(2026-11)

初期整備の後、ESP32-C5-WROOM-1、WCMCU-1051、M3508 P19 / C620 ×4で、車輪を浮かせて確認しました。手順は [ベンチ試験](bench.md)。物理非常停止(駆動電源の遮断)は確認済みと報告を受けています。

### 確認できたこと

| 項目 | 結果 |
|---|---|
| ESP32-C5のTWAI(GPIO23/24) | `esp32c5_can_selftest` の直結で `tx ok` と `rx` が一致し、エラー0・`state=0` |
| トランシーバー経由のCAN | 配線を直した後、`tx ok`・`rx` が増え、エラー0 |
| C620 ×4のフィードバック | 4台とも `fresh`(ID1〜4)。LEDは1〜4回点滅(ID設定済み) |
| `can_ok` の安定 | 受信キューあふれをFAULTにしない修正後、安定 |
| 1輪の駆動 | 開ループ電流(500〜800カウント)とPI(`g 10 3`、100RPM)で回転 |
| 4輪同時(`esp32c5_omni_bench`) | 動作することを確認(符号の個別記録は未整理) |

### 試験で分かったこと

- DISARM中にCANへ送信すると、ACK相手がいない状態でbus offになる。ARM中と停止直後のみ送信する構成にした。
- C620 4台で約4kHzのFBが届くため、受信キューが16フレームではあふれる。毎周期の読み出しとキュー拡大、あふれの非FAULT化を行った。
- 動き出しの電流が不安定だった原因は取り付けによる負荷だった(電気的な問題ではない)。この間は参考ゲイン(kp=1, ki=0.2)では動き出せず、ベンチでは `g 10 3` で確認した。Pi経由でLスティックが動かなかったため、本番とomni_benchの既定を `kSpeedKp/kSpeedKi`(10 / 3)に変更した(`reference_gains.hpp` と `motor_bench` は参考値のまま)。取り付けを直した後に、参考ゲインのままで追従するかは再確認が必要。

### 自動検証(本ブランチ)

| 検証 | 結果 |
|---|---|
| `uv run ruff check .` / `ruff format --check .` | 成功 |
| `uv run pytest -q` | 52 passed(リンク確立とARM手順、ハートビートの符号化・受信・ログを含む) |
| ホストC++(CTest) | 成功(車体設定の符号構造、ハートビートの固定バイト列を含む) |
| `pio run` | `esp32c5_rx` / `esp32c5_can_selftest` / `esp32c5_motor_bench` / `esp32c5_omni_bench` すべて成功。`config.local.hpp` が無い環境でも `esp32c5_rx` は成功 |

### 未確認

- Piのコントローラー入力から、Wi-Fi/UDP経由でESPを駆動する通し試験(自動ARM、ハートビート、切断時のFAULT)。
- 取り付け符号の確定(`vehicle_config.hpp` の `kSlots` は全輪+1の仮置き)。
- 物理インターロックのGPIO(未配線)、接地走行、停止距離/時間、高速域でのゲイン妥当性、バス終端と配線長。
- アプリケーション認証とリプレイ保護(UDP v1は隔離ベンチ専用)。
