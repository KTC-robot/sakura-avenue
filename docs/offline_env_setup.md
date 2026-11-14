# AP運用前のオフライン環境準備

夏の `UnagiFestival/docs/offline_env_setup.md` を基に、外部ネットワークを使えないPiの準備を整理します。夏の資料のPi 5/aarch64や追加機器の依存は春の確定事項ではありません。Pi型番・CPUアーキテクチャ・Python/OSが未定なので、クロス環境用wheel収集は未検証です。

## 1. オンラインで実際の開発環境を準備する

ネットワークをAP専用へ切り替える前に、対象Pi上でuvとPython、ビルドに必要なOSパッケージを準備します。evdevがソース配布の場合はCコンパイラーとPython開発ヘッダーなどが必要です。ホストPCで作った仮想環境をそのまま別アーキテクチャへコピーしません。

対象Piのcheckoutで次を行います。

```bash
cd raspberryPi
uv sync --locked --extra controller
uv run ruff check .
uv run pytest -q
```

この手順の実Pi上での検証は未実施です。PC上の成功記録は[旧検証記録](../archive/docs/validation.md)にあります。夏の `uv run task whl`、IM920用pyserial、GPIO/I2C用ライブラリーは春へ持ち込みません。

## 2. 接続を切る前にキャッシュを確認する

```bash
# raspberryPi/ 内。ネットワークを使わず依存が揃うか確認する
uv sync --locked --offline --extra controller
uv run --offline --no-sync sakura-pilot --help
```

キャッシュ不足ならオンラインのうちに解決してください。`--offline`は必要なパッケージやPython自体を生成しません。venvやuvキャッシュを消した後の再構築も自動的に保証されません。

## 3. AP専用運用へ切り替える

5GHz/AP対応と国・チャネルを確認した上でOS側を設定します。実運転前の手順はnetwork.mdを参照してください。入力プレビューは次のように既存環境を使います。

```bash
uv run --offline --no-sync sakura-pilot --verbose
```

現CLIはARM/DRIVEを送信しません。ネットワークが復旧しても自動的に駆動許可へ遷移しません。

## ESP-IDFと別PCでの準備

PlatformIOのプラットフォーム・ESP-IDF・ツールチェーンも事前取得が必要です(初回の `pio run` で `~/.platformio` に保存されます)。ESP-IDF初回導入までオフラインで可能とは扱いません。完全にクリーンなPiへ持ち込める配布物を用意する場合は、対象アーキテクチャのwheelとbuild依存を確定し、隔離環境で導入試験を行う作業が残っています。
