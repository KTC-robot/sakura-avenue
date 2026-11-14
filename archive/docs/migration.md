# 2026夏からの選択移植記録

調査元: [KTC-robot/UnagiFestival](https://github.com/KTC-robot/UnagiFestival/tree/42c839d91e20022c8ee491fa69abcb9204a9e393)、commit `42c839d91e20022c8ee491fa69abcb9204a9e393`。参照用cloneは作業リポジトリ外に置き、変更しません。

## ライセンスと依存

調査時点で元リポジトリにLICENSE/COPYINGを見つけられませんでした。対象ソースにも独立した著作権・ライセンス文言はありません。ライセンス不在を自由利用の許諾とはみなしません。本依頼の同組織内移植として出典とcommitを保持し、**外部再配布やライセンス付与前に権利者の許諾・適用条件を確認する必要があります**。新たなOSSライセンスを推測で付けていません。元コメントは直接移植ファイルで維持しています。

元のevdev入力はevdev任意依存として採用。IM920用pyserial、GPIO用rpi-lgpio、I2C用smbus2、servoや機構ライブラリーは採用しません。evdev自体のライセンスはその配布パッケージに従います（vendoringなし）。

## 対応表

表中の `controller/...`、`command.py`、`main.py` は `raspberryPi/src/sakura_avenue/` 配下です。コードコメントは夏を参考に、Python docstringとC++ Doxygenを区別して簡潔に記載します。[開発・コメント規約](../../docs/development.md)に、単位・戻り値・安全上の副作用の書き方をまとめています。

| 元ファイル（基準ディレクトリ） | 移植先 | 変更 |
|---|---|---|
| `src/unagifestival/tools/ps_controller/enum.py` | `raspberryPi/src/sakura_avenue/controller/enum.py` | 型と軸/ボタン対応を継承。Linuxイベント番号を純粋Python定義にしてevdev依存を境界へ限定 |
| 同 `model.py` | `controller/model.py` | 軸範囲/入力イベント/状態を移植、ボタン状態を追加 |
| 同 `device.py` | `controller/device.py` | 機能/名前のスコア、自動検出、select/evdev入力を移植。非ゲームパッド排除、未選択handle解放、SYN_DROPPED停止を追加 |
| 同 `handler/handler.py` と `handler/constants.py` | `command.py`、`main.py` | 中央/半範囲正規化、deadzone 0.08、trigger 0.1、slow 0.25、イベントログを分離。IM920/servo依存を除去 |
| 同 `im920/model.py` のDriveCommand概念 | `command.py` と C++ `control.hpp` | vx/vy/wzを継承、±127→±1へ変更。ネットワークモデルとは別クラス |
| `platform/chassis_ctrl/speed_controller.hpp/.cpp` | `firmware/components/control/include/speed_controller.hpp` と `speed_controller.cpp` | PI計算・積分・飽和時anti-windup・resetを直接移植。include変更、非有限値/不正dtでresetを追加 |
| 同 `constants.hpp` | `control/include/reference_gains.hpp` | Kp=1, Ki=.2、積分上限8000、電流指令3000を参考値として選択移植。機体で調整済みではない |
| `platform/device/c620_driver.cpp/.hpp` | `device/include/c620.hpp` と `twai_bus.cpp/.hpp` | 0x200/0x201..204、big endian、RPM/電流/温度/角度、freshnessを抽出。Arduinoと旧TWAI/固定GPIOを除去し新APIアダプターへ分離 |
| `platform/chassis_ctrl/chassis_ctrl.cpp` | `control/include/control.hpp` | 正規化→RPM制限→slew→PIの処理構造を継承。メカナム符号表・取付反転・段差制御を移植せず汎用オムニへ置換 |

`vy=+LX` は夏のハンドラーでは右向きで、今回の+Y左と一致しません。春のマッパーは `vy=-LX` に変更しています。`vx=-LY`、`wz=-RX` は維持。旧VX_INVERT/WZ_INVERTやWHEEL_TO_MOTORは移植せず、WheelConfigで明示します。

## 新規部分

共通v1 UDP仕様/ベクトル、送受信アダプター、パラメーター式4輪オムニ、rank検証、安全状態機械、ホスト制御結合、CI、日本語ドキュメント。従来のテンプレートは `archive/arduino-template/` へ移動し、C5対応済みとは扱いません。

## 残作業

権利条件の確定、認証/セッション管理、実機設定ローダー、STAの実起動と切断連携、操縦ARM UI、実時間制御タスク、物理インターロック、競技機構、実機ゲイン調整。これらを完成と表現しません。

## ドキュメントの継承

夏の `docs/offline_env_setup.md` のAP運用前に依存を準備する方針を `docs/offline_env_setup.md` へ整理しました。Pi 5/aarch64、夏固有のtaskと追加機器依存は転用していません。IM920のセットアップは採用せず、春のネットワーク設計へ置き換えています。資料の案内は `docs/README.md` に集約しています。
