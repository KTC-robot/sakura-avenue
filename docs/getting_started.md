# 始め方(実機を動かすまで)

PiのコントローラーでESP32-C5の4輪オムニを動かす最短手順です。**最初は必ず車輪を浮かせ、物理非常停止に手が届く状態で行います。** Pi側とESP側は、必ず同じブランチのコードを使ってください(ARMとハートビートは `main` に未反映の場合があります)。

## ネットワークの前提(IP固定)

PiがAPになる構成を標準とし、アドレスは次のとおり固定します。

| 機器 | IP | ポート |
|---|---|---|
| Pi(AP) | `10.42.0.1` | 送信元 `50001`(`--source-port`) |
| ESP32-C5 | `10.42.0.50`(固定) | 受信 `50000` |

- ESPの固定IPは `platform/config.local.hpp` の `static_ip` / `static_gateway` / `static_subnet`(`10.42.0.50` / `10.42.0.1` / `255.255.255.0`)で決まります。3つとも必要で、空欄ならDHCPです。不完全な設定ではWi-Fiを開始しません。
- ESPのIPをDHCPの払い出し範囲と重ならない値にしてください。重なる場合はAP側で払い出し範囲から外します。
- 外部ルーターを使う場合は、IPを同じセグメントの未使用アドレスに変え、Pi側もDHCP予約か固定にします。`peer_ip` はPiの実IPに合わせます。

## 1. Pi: APを立てる

5GHzのAPを作ります(NetworkManagerの共有モードの例。SSIDとパスワードは自分で決める)。

```bash
nmcli device wifi hotspot ifname wlan0 ssid <SSID> password <8〜63文字> band a
nmcli connection modify Hotspot ipv4.addresses 10.42.0.1/24 ipv4.method shared
nmcli connection up Hotspot
```

SSIDとパスワードはGitに入れません。`ip -4 addr show wlan0` で `10.42.0.1` を確認します。5GHzの可否はPiの無線機器に依存します。

## 2. ESP: 設定して書き込む

```bash
cp platform/config.example.hpp platform/config.local.hpp   # 初回のみ(Git管理外)
```

`platform/config.local.hpp` を編集します。

| 項目 | 値 |
|---|---|
| `wifi_ssid` / `wifi_password` | 手順1のAPと同じ |
| `peer_ip` / `peer_port` | `10.42.0.1` / `50001`(Pi。ハートビートの返送先でもある) |
| `static_ip` / `static_gateway` / `static_subnet` | `10.42.0.50` / `10.42.0.1` / `255.255.255.0` |
| `listen_port` | `50000` |

古い `config.local.hpp` を使い回している場合は、`static_*` の3項目を追記しないとビルドが通りません。

```bash
pio run -e esp32c5_rx -t upload
pio device monitor -b 115200
```

シリアルに `[sakura] Wi-Fi接続 10.42.0.50 UDP:50000 待受開始` が出れば接続成功です。以降、1秒ごとに状態行が出ます。ベンチ用env(`*_bench`、`can_selftest`)は通しの試験では書き込みません。

## 3. Pi: コントローラーをつないで起動

```bash
uv sync --locked --extra controller
uv run sakura-pilot --host 10.42.0.50 --port 50000 --source-port 50001
```

1. コントローラー(PS)をBluetoothまたはUSBで接続します。
2. スティックをニュートラルのまま約1秒保つと、自動でARMされます。
3. 以降、左スティックで並進、右スティック左右で旋回、L2/R2で低速(0.25倍)です。

スティック全開の速度は、`platform/control/vehicle_config.hpp` の `kSpeedFraction`(最高速に対する割合、現在0.8)で決まります。最高回転数は `kOutputRatedRpm`(M3508 P19の定格482RPM×減速比19)で、全開は約7300RPM(車輪周速約2m/s)です。**初めての接地走行は `kSpeedFraction` を小さくして始めてください。**
4. PSボタンまたはCtrl-Cで停止します(DISARMを送って終了)。

Piのログに `STATUS=ARMED モータOK` が1秒ごとに出れば、通信とモーターは健全です。`--no-arm` を付けるとDISARMのみで、ARM/DRIVEは送りません。

## うまくいかないとき

| 症状 | 確認 |
|---|---|
| ESPがWi-Fiにつながらない | SSID/パスワード、5GHzのAPか、`static_*` が3つ揃っているか |
| `STATUS応答なし` | `peer_ip`/`peer_port` がPiと一致しているか、`--source-port` が `peer_port` と同じか |
| ARMされない | スティックがニュートラルか、`STATUS` とESPのシリアルログのFAULT理由 |
| `モータNG` | CAN配線、C620の電源、`feedback` の鮮度([bench.md](bench.md)) |
| 動かない・FAULT | 切断やタイムアウト後は、ニュートラルでDISARM→再ARM(Piを再起動) |

隔離ベンチ専用です。認証がないため、他の機器がつながる網では使わないでください([network.md](network.md))。より詳しい情報は [setup_esp32c5.md](setup_esp32c5.md)、[setup_pi.md](setup_pi.md)、[bench.md](bench.md) を参照してください。
