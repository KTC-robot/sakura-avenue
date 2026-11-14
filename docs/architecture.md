# 構成と安全状態

## 責務と現在の接続範囲

Pi側: `ControllerDevice`（evdevと自動検出）→ `ControllerState`（軸/ボタン）→ `CommandMapper`（正規化・deadzone・slow）→ `DriveCommand` → `UdpTransport`。

CLIはプレビューとDISARM送信に限定しています。マッパーとプロトコル自体はDRIVEを生成できますが、操縦ARM UIは未接続です。evdevは任意依存であり、純粋入力モデル・マッパー・通信テストは実機なしで動きます。アダプターテストは疑似デバイスとイベントを使います。

ESP側: `UdpSocket`（明示したローカルIP/portでbind）→ `Receiver`（送信元、形式、順序）→ `Safety` → `ControlLoop` → `Chassis` → `Omni` → `SpeedController ×4` → `current_frame` → `TwaiBus`。

純粋ロジックと各アダプターは用意されていますが、**setup()には接続していません**。安全に起動するための意図的な境界です。実タスク、Wi-Fi接続イベント、物理インターロック、認証が完成するまでモーターを有効化しません。

## 4輪オムニ

各車輪は位置(x,y)、駆動方向θ、半径r、減速比g（モーター回転数/車輪回転数）、motor_sign、motor_index（0..3＝CAN ID1..4）を持ちます。

```text
v_wheel = cos(θ) vx + sin(θ) vy + (-y cos(θ) + x sin(θ)) wz
wheel_rpm = v_wheel / r × 60 / (2π)
motor_rpm[motor_index] = wheel_rpm × g × motor_sign
```

θは車輪の駆動方向で、車輪軸の方向ではありません。C620 feedbackはモーター軸RPMなのでPIへ直接渡し、減速比を再適用しません。motor_signは目標RPM側で一度適用し、feedback側の追加反転は不要です。

入力の有限値/±1検証→deadzone→設定されたSI速度上限→運動学→全モーター共通係数による最大RPM制限→スルー制限→PIです。車輪半径/減速比の正値、符号±1、番号一意、有限値、運動学行列rank 3を検証します。幾何が極端に小さい場合も数値的rank判定で拒否することがあります。

## 状態

| 状態 | 出力 | 復帰 |
|---|---|---|
| DISARM | ゼロ | ニュートラルDISARM確認後、別の明示ARM |
| ARMED | 健全性成立かつDRIVE時のみ制御 | STOP/DISARM/異常で解除 |
| STOP | ゼロ、RPMとPIリセット | DISARM→ARM |
| FAULT | ゼロ、RPMとPIリセット | 原因解消後DISARM→ARM |

受信タイムアウト・Wi-Fi断・CAN異常・4台中1台でもfeedback失効・インターロック未成立・制御周期異常はFAULTです。`ControlLoop::step` は全てを検査し、停止時にはスルー制限を迂回して即時にゼロフレームを返します。初期の許容dtは0<dt<=50msで、これは実機スケジューラーの検証値ではありません。

`Receiver::receive` の healthy と `ControlLoop::step` の健康判定には同じ全体状態を渡してください。ネットワーク接続だけをhealthyとしてARMしないでください。未来の実タスクは受信がなくても周期tickし、送信失敗時には即座に `transport_fault()` を呼び、ゼロ電流の再送を試みる必要があります。

## 実機への統合条件（未実装）

- Wi-Fi接続イベントとCAN状態イベントをControlLoopの健全性へ結線
- タイマー起点の周期制御、watchdog、タスク停止時の物理インターロック
- 認証されたセッションと操縦者のニュートラル/再許可操作
- 設定検証成功と物理許可の両方がなければTWAIを開始しない
- CAN bus-offではゼロフレームも届かないので電源遮断を実行

TWAIは1フレームだけを送信中として保持し、完了通知までバッファーを再利用しません。キュー詰まり・受信オーバーフロー・送信エラーをラッチし、復旧しても非ゼロ送信は再開しません。送信完了・bus-off時の実際の遅延と停止到達は未検証です。機体側に独立した非常停止とC620電源遮断を必ず用意してください。
