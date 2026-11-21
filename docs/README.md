# 開発ドキュメント

2026夏の [UnagiFestival](https://github.com/KTC-robot/UnagiFestival/tree/42c839d91e20022c8ee491fa69abcb9204a9e393) を基に、2027春の開発手順とモジュールの責務を整理しています。元の資料をそのまま春の実機手順として使わず、通信方式・SDK・未確定の機体設定を区別します。

## 最初に読む順序

実機を動かすだけなら、先に [始め方](getting_started.md)(IP固定・書き込み・Pi起動)を読んでください。

1. [プロジェクト概要](../README.md): 構成と現時点の出力禁止範囲
2. [移植記録](migration.md): 夏のファイルと春の対応、変更点、権利確認事項
3. [アーキテクチャ](architecture.md): 入力から電流指令までの責務と安全状態
4. [Piセットアップ](setup_pi.md) / [ESP32-C5セットアップ](setup_esp32c5.md): 依存導入とビルド
5. [ネットワーク](network.md) / [配線準備](wiring.md): 実機接続前の未確定事項
6. [開発・コメントの書き方](development.md): 夏から継承する記述形式と検証の基準
7. [オフライン準備](offline_env_setup.md): PiのAP運用に備えた依存の準備
8. [実機ベンチ試験](bench.md): 車輪を浮かせたCAN・1輪・4輪の確認手順と切り分けの知見
9. [検証記録](validation.md): 実行済みの検証と実機未確認範囲

## 夏の資料から引き継ぐこと

| 夏の資料・コード | 春での扱い |
|---|---|
| `docs/offline_env_setup.md` | AP運用前に依存を用意する考え方を継承。Pi 5/aarch64前提や夏のtask/依存一覧は転用しない |
| `docs/im920sl_pi_esp_setup.md` | IM920配線・無線設定は使用しない。春は5GHz Wi-Fi/UDPとしてnetwork.mdへ分離 |
| `docs/platform_change.md` とPlatformIO設定 | 開発対象を明示する考え方を継承。春の正規ビルドはPlatformIO(Arduino)/esp32c5 |
| PS入力のdocstring | 短い要約を基準に、必要な引数・戻り値の説明だけを残す |
| PI/C620のヘッダー | 日本語Doxygenと単位・戻り値・副作用の説明を継承 |

実装の一覧は移植記録、最新の再実行コマンドは各セットアップ、過去の実行結果は検証記録へ記載します。ビルド成功と実機検証を同じ意味で使いません。
