# Sakura UDP protocol v1（隔離ベンチ検証用）

1 datagram = 20 bytes。ネットワークバイト順（big endian）。C++構造体のメモリを直接送信しません。

| offset | bytes | 型 | 内容 |
|---|---|---|---|
| 0 | 2 | ASCII | `SA` |
| 2 | 1 | uint8 | version = 1 |
| 3 | 1 | uint8 | action: DISARM=0 / ARM=1 / DRIVE=2 / STOP=3 |
| 4 | 4 | uint32 | sequence |
| 8 | 4 | IEEE754 binary32 | vx |
| 12 | 4 | IEEE754 binary32 | vy |
| 16 | 4 | IEEE754 binary32 | wz |

`vx/vy/wz` は有限値 -1..+1 の正規化要求です。+X前進、+Y左、+wz反時計回り。実機で設定した m/s・rad/s 上限をファームウェアで乗算してから運動学へ渡します。夏の±127を±1へ正規化しました。DRIVE以外は3軸すべてゼロ必須。-0もゼロとして認めます。

長さ不一致、magic/version/action不正、NaN/Inf、範囲超過、非DRIVEの非ゼロ値は拒否。信頼した送信元からの不正パケットはFAULTとしタイムアウトを更新しません。未登録の送信元は無視し、駆動状態にも受信期限にも影響させません。

sequenceは送信パケットごとに1増加。`0 < uint32(new-old) < 2^31` のみ新しいと認めます。重複・順序逆転・半周以上の差はFAULT。受信期限を更新しません。uint32 wrapを許可します。送信プロセス再起動で番号が戻った場合、自動再同期はしません。ベンチでは出力を切った状態で受信インスタンスを再生成してください。実運用は認証された新規セッションID/nonce導入後に設計します。

初期送信周期50Hz、受信期限180ms（受信側単調時計）、CAN feedback期限100ms。これらは実機検証が必要な初期値です。Safety.timeout_ms / Python Safety(timeout=...) で変更可能。入力イベントが無い静止時も周期送信します。送信停止は受信側の周期tickで検出します。

## 状態通知(ハートビート)

受信側(ESP)から送信元(Pi)へ、1秒に1回、10 bytesを送ります。宛先は設定の `peer_ip:peer_port`(Piの `--source-port`)、送信元は受信側のlisten portです。ネットワークバイト順。情報用で、受信側の制御・安全判定には使いません。Piは送信先と一致しない送信元のパケットを捨てます。

| offset | bytes | 型 | 内容 |
|---|---|---|---|
| 0 | 2 | ASCII | `ST` |
| 2 | 1 | uint8 | version = 1 |
| 3 | 1 | uint8 | state: DISARM=0 / ARMED=1 / STOP=2 / FAULT=3 |
| 4 | 1 | uint8 | flags: bit0 Wi-Fi、bit1 CAN、bit2 全C620のフィードバック、bit3 インターロック |
| 5 | 1 | uint8 | 予約 = 0 |
| 6 | 4 | uint32 | 送信ごとに増える連番 |

「モータOK」はCAN健全(bit1)かつ全C620のフィードバック有効(bit2)と定義します。例: ARMED・全ビット・連番0x01020304 = `535401010f0001020304`、FAULT・Wi-Fiのみ・連番0 = `53540103010000000000`。長さ・magic/version・予約・stateが不正なパケットはPiが捨てます。

## 再許可

起動DISARM。ニュートラルDISARMの受信後に、別パケットの明示ARMが来た場合だけARMEDになります。STOPとFAULTはARMを直接受け付けません。まずニュートラルDISARM、その後ARMが必要です。通信回復だけでは再許可しません。健康状態が不成立ならDISARM受信でもFAULTを解除しません。

このバージョンは時刻・セッションnonce・MACを持たず、初回に届くパケットの実年齢やなりすましを検証できません。単純なsequence検証は暗号学的リプレイ保護ではありません。Wi-Fi接続済みを駆動許可とみなしてはいけません。実運用の認証と期限検証は次バージョンで追加し、その完成までapp_mainは出力禁止です。

`golden_vectors.json` をPythonが直接読み、C++はCMake時に同じJSONからfixtureを生成してエンコード・デコードを照合します。共有JSONの変更は両言語のテストで検証します。
