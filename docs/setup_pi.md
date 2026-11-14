# Raspberry Pi / PC のセットアップ

Pi型番は未確定です。LinuxとPython 3.12以上、uvを用意してください。5GHz対応とAPモードの可否は選定したPi/無線機器ごとに確認が必要です。

```bash
cd raspberryPi
uv sync --locked                 # 純粋ロジックの開発
uv run ruff check .
uv run ruff format --check .
uv run pytest -q
uv sync --locked --extra controller  # Linux evdevアダプターも使用
uv run sakura-pilot --verbose
```

controller extraなしではevdevテストはskipされ、実デバイスCLIは使用できません。CIはLinuxでcontroller extraを入れます。コントローラーをBluetoothまたはUSBでOSに接続し、`/dev/input/event*` の読取権限を付与してください。無差別なchmod 666やroot常用は避け、OSのinputグループ/udevで対象端末の権限を整備します。

自動検出は夏のスコア方式を継承し、基本軸とゲームパッドボタンがないデバイスを除外します。PS型番ごとの軸割当と切断イベントは実機で要確認です。デバイス切断・SYN_DROPPEDは入力継続せず停止します。再接続はプロセス再起動で行い、自動再許可はしません。

CLIは入力をDEBUGログへ出し、PSボタンで終了します。ボタンの押下・解放はINFOログへ記録。左スティック前=+vx、左=+vy、右スティック左=+wz。L2/R2を10%以上押すと0.25倍のslowです。deadzone 0.08は夏の参考値です。

隔離したUDP受信テストへDISARMを50Hzで送る例（IP/portはベンチ専用）:

```bash
uv run sakura-pilot --host 127.0.0.1 --port 50000 --source-port 50001
```

ホスト/送信元ポートは明示的に選んでください。既定ではネットワーク送信しません。送信処理は終了時にDISARMを試みますが、kill -9・電源断では送れないので受信側タイムアウトが必須です。CLIはARM/DRIVEを送りません。

`config.example.toml` は設定案であり現在のCLIは読みません。実値を保存する場合は `config.local.toml` を使ってください。SSID/パスワードはOSで設定しGitに含めません。依存は標準ライブラリー中心で、evdevのみ任意追加、開発用はpytestとRuffです。IM920・serial・GPIO・I2Cライブラリーは不要です。

## AP運用・オフライン準備

夏のオフライン構築手順を春の依存構成へ整理した [オフライン準備](offline_env_setup.md)を参照してください。Pi型番・アーキテクチャは未確定なので、夏のPi 5/aarch64向けwheel手順は実機手順として流用しません。
