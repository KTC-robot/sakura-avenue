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

## 再許可

起動DISARM。ニュートラルDISARMの受信後に、別パケットの明示ARMが来た場合だけARMEDになります。STOPとFAULTはARMを直接受け付けません。まずニュートラルDISARM、その後ARMが必要です。通信回復だけでは再許可しません。健康状態が不成立ならDISARM受信でもFAULTを解除しません。

このバージョンは時刻・セッションnonce・MACを持たず、初回に届くパケットの実年齢やなりすましを検証できません。単純なsequence検証は暗号学的リプレイ保護ではありません。Wi-Fi接続済みを駆動許可とみなしてはいけません。実運用の認証と期限検証は次バージョンで追加し、その完成までapp_mainは出力禁止です。

`golden_vectors.json` をPythonが直接読み、C++はCMake時に同じJSONからfixtureを生成してエンコード・デコードを照合します。共有JSONの変更は両言語のテストで検証します。

## サービスパケット（ログ・PIゲイン、magic `SC`）

走行用v1（`SA`、20バイト）とは別系統です。`SA`のdecode・FAULT規則・sequence規則は変更しません。サービスパケットは`Safety`へ渡さず、**不正でもFAULTにしません**（ACKのstatusで返すか黙って無視）。信頼済み送信元以外は無視します。big endian、1データグラム（最大128B）。

ヘッダー6B: `S` `C` ver(=1) type(u8) req_id(u16)。req_idはPiが単調増加させ、ACKが同じ値を返します。

| type | 方向 | payload |
|---|---|---|
| 1 LOG_START | Pi→ESP | rate_hz u8 (1..50), lease_ms u16 (1..5000) |
| 2 LOG_STOP | Pi→ESP | なし |
| 3 PARAM_GET | Pi→ESP | なし |
| 4 PARAM_SET | Pi→ESP | kp, ki, integral_limit, max_current（f32×4） |
| 5 PARAM_SAVE | Pi→ESP | なし。現在のRAM値をNVSへ保存 |
| 6 PARAM_RESET | Pi→ESP | なし。既定値へ戻しNVS保存値を消去 |
| 0x81 ACK | ESP→Pi | status u8 + 現在のゲインf32×4（23B） |
| 0x82 LOG_SAMPLE | ESP→Pi | seq u32, t_ms u32, state u8, fresh_mask u8, モーター×4（68B） |

モーター1台13B: rpm i16, current i16, angle u16, temperature u8, target_rpm i16, command i16, integral i16（target・integralは丸めて飽和）。fresh_maskのbit iはモーターiのフィードバックが100ms以内。stateは0=DISARM,1=ARMED,2=STOP,3=FAULT。

status: 0 OK, 1 BAD_REQUEST(長さ等), 2 NOT_DISARMED, 3 INVALID_VALUE, 4 STORAGE_ERROR, 5 UNKNOWN_TYPE。

- **ログは要求時のみ。** LOG_STARTはleaseを持ち、lease_ms以内に再送（更新）しないとESP32は送信を止めます。Pi側が落ちても垂れ流しません。LOG_SAMPLEのseqで欠落を検知します。ログは助言用で、安全判定の入力にしません。
- **PARAM_SET / PARAM_RESETはDISARMの時だけ。** それ以外はNOT_DISARMED。適用時は全PIの積分と目標をリセットします。値は有限・0以上で、`kp<=10, ki<=5, integral_limit<=8000, max_current<=3000`（コンパイル時上限。C620の±3000クランプ以下）。1項目でも範囲外なら全体を拒否し現在値を保ちます。
- 保存はPARAM_SAVEの明示操作のみ。起動時にNVSから読み、magic・CRC・範囲検証に失敗したら既定値（夏の参考値）を使います。
- Pi側は未ACKの要求を同じreq_idで再送（0.5秒×3回）。SET/SAVE/RESETは冪等です。
- **認証・リプレイ保護はありません。** 書き込み系コマンドが無認証チャネル上にあります。ベンチ限定で、実機駆動経路へ接続する前に認証が必須です。

`service_golden_vectors.json`をPythonとC++が共有して検証します。
