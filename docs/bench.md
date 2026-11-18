# 実機ベンチ試験

車輪を浮かせた状態で、CAN経路・C620・1輪・4輪を段階的に確認するためのファームと手順です。いずれも `platformio.ini` の別envで、本番(`esp32c5_rx`)には含まれません。書き込み: `pio run -e <env> -t upload`、モニター: `pio device monitor`(115200, Enterで送信)。

## 前提

- 車輪を床から浮かせ、回っても何にも当たらない。
- 物理非常停止(駆動電源の遮断)に手が届く。
- CANは TX=GPIO23 / RX=GPIO24(`platform/control/vehicle_config.hpp`)。

## 段階

| 順 | env | 目的 | C620電源 |
|---|---|---|---|
| 1 | `esp32c5_can_selftest` | ESP→トランシーバー→受信ピンの経路確認 | 切る |
| 2 | `esp32c5_motor_bench` | C620を1台ずつ確認(開ループ/PI) | 入れる |
| 3 | `esp32c5_omni_bench` | 4輪同時の運動学とPI | 入れる |

### 1. CAN経路(`esp32c5_can_selftest`)

セルフテストモード(ACK不要)で `0x200` を送り、自分の送信が受信ピンで読み返せるかを数えます。モーターには電流を出しません。起動時にGPIO直結テスト(TXを出力、RXを入力にして導通を確認)も行います。

1秒ごとの行 `tx ok=… fail=… | rx=… | err bit=… form=… …` で判定します。

| 結果 | 意味 |
|---|---|
| `tx ok`・`rx` が増え、`err` が0、`state=0` | 経路は正常 |
| `err bit` / `tx fail` が増える | TX/RXの向き、トランシーバーの電源・待機ピン、5V専用品の誤用を疑う |
| 直結(GPIO23-24)でも失敗 | 直結線の接触不良。テスターで導通を確認する |

ピンはビルドフラグで変更できます: `--project-option="build_flags=-I platform -fexceptions -DCAN_TX_GPIO=4 -DCAN_RX_GPIO=5"`。

### 2. 1輪ずつ(`esp32c5_motor_bench`)

起動時はDISARM。DISARM中はCANへ送信しません。

| コマンド | 動作 |
|---|---|
| `status` | CAN状態(`can_ok`, `twai state/tec/rec/errors/last_err/rx_dropped`)と全IDのFB・ゲイン |
| `recover` | CANの異常ラッチを解除(DISARM中のみ) |
| `arm` | 駆動を許可(これだけでは回らない) |
| `m <id> <rpm>` | PIでRPM追従を3秒。|rpm|≦300 |
| `c <id> <counts>` | 開ループ電流(C620生値)を3秒。|counts|≦800。動き出しの確認用 |
| `g <kp> <ki>` | PIゲイン変更(停止中のみ。既定 kp=1, ki=0.2) |
| `s` / `disarm` | 即時ゼロ電流・DISARM |

安全制限: 動かす輪は1つ、電流±1500、実測500RPM超・CAN異常・FB100ms欠落・3秒経過で即停止。試験手順:

1. `status` で4台が `fresh`、`can_ok=1` を確認(`can_ok=0` なら `recover`)。
2. `arm` → `c <id> 500` で動き出す電流を確認 → `m <id> 100` でPI追従を確認。
3. 各IDの `+` 方向の回転方向を記録する(取り付け符号の根拠)。

### 3. 4輪同時(`esp32c5_omni_bench`)

車体設定は `platform/control/vehicle_config.hpp`(本番と共通)。

| コマンド | 動作 |
|---|---|
| `plan <vx> <vy> <wz>` | 動かさず、各IDの目標RPMだけを表示(符号確認用) |
| `d <vx> <vy> <wz>` | 正規化要求(各軸 |v|≦1)を3秒。+X前、+Y左、+wz反時計回り |
| `status` / `recover` / `arm` / `g` / `s` | motor_benchと同じ |

4台すべてのFBが新しくないと `arm` / `d` を拒否します。幾何が不正なら起動時にエラーを出して駆動できません。符号は `plan` の出力と実際の回転方向を比べ、合わなければ `vehicle_config.hpp` の `kSlots` の符号を反転します。

## 切り分けの知見

- **`node is bus off` が出続ける**: バスに誰もACKを返さない状態で送信している。DISARM中は送信しない(実装済み)。トランシーバーの経路を `can_selftest` で確認する。
- **`can_ok=0` だがFBは届く**: 異常ラッチが立っている。`status` の `last_err` と `state` を見て `recover`。受信キューのあふれはFAULTにせず `rx_dropped` に数える(FBは最新値のみ必要で、古さは100msの期限が検出する)。C620 4台で約4kHzのFBが来るため、`loop()` は毎周期キューを空にする。長いシリアル出力で `loop()` が止まるとあふれる。
- **電流が流れているのに回らない(開ループ約400〜800カウントで動く、またはその後動かない)**: 今回の原因は取り付けによる負荷だった。電気的な問題ではない。回らないときは、手で回して引っかかりを確認し、`c` で動き出す電流と `angle` の動きを見る。
- **参考ゲイン(kp=1, ki=0.2)では動き出せない**: 初期出力が `kp×目標RPM` で、積分は毎秒 `ki×誤差` ずつしか増えない。動き出しに数百カウント必要な状態では、3秒内に届かない。ベンチでは `g 10 3` で確認した。**コードの既定値は変えていない**。取り付けを直したあとで、参考ゲインのまま追従するか再確認すること。

## 未確認

取り付け符号の確定、Pi経由の駆動、物理インターロックのGPIO、接地走行、停止距離/時間、高速域でのゲイン妥当性。結果は [検証記録](validation.md) を参照。
