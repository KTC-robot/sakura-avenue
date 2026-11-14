# 2027関西春ロボコン

## 必要環境

以下をインストールしてください。

* Git
* Visual Studio Code
* PlatformIO 拡張機能

Visual Studio Code の拡張機能画面から `PlatformIO` を検索してインストールします。

PlatformIO Core を別途インストールする必要はありません。

## リポジトリを取得

```bash
git clone htttps://github.com/KTC-robot/sakura-avenue.git
cd sakura-avenue
```

取得したディレクトリを Visual Studio Code で開きます。

```bash
code .
```

## ESP32を接続

ESP32をUSBでPCへ接続します。

Linuxでは通常、以下のようなシリアルデバイスとして認識されます。

```text
/dev/ttyUSB0
```

または、

```text
/dev/ttyACM0
```

PlatformIOが接続先を自動検出するため、通常はポートを手動指定する必要はありません。

## ビルド

Visual Studio Code 左側の PlatformIO アイコンを開きます。

`PROJECT TASKS` から以下を実行します。

```text
esp32dev
└── General
    └── Build
```

または、Visual Studio Code 下部のチェックマーク `✓` からビルドできます。

正常に完了すると、ターミナルに以下のように表示されます。

```text
SUCCESS
```

## ESP32へ書き込み

`PROJECT TASKS` から以下を実行します。

```text
esp32dev
└── General
    └── Upload
```

または、Visual Studio Code 下部の矢印 `→` から書き込めます。

書き込み完了後、ESP32が再起動します。

## シリアルモニター

PlatformIO の `PROJECT TASKS` から以下を実行します。

```text
esp32dev
└── Monitor
```

または、Visual Studio Code 下部のシリアルモニターアイコンから起動できます。

正常に動作していれば、以下のような出力が表示されます。

```text
ESP32 started
LED ON
LED OFF
LED ON
LED OFF
```

`platformio.ini` では受信時刻を表示するよう設定しています。

```ini
monitor_filters =
    default
    time
```

そのため、実際には以下のような形式で表示されます。

```text
12:30:01.123 > ESP32 started
12:30:01.124 > LED ON
12:30:01.625 > LED OFF
```

## シリアル設定

シリアル通信速度は `115200` に設定しています。

```ini
monitor_speed = 115200
```

ESP32側の通信速度は `include/config.hpp` で設定します。

```cpp
constexpr uint32_t SERIAL_BAUD = 115200;
```

両方の値を一致させてください。

入力文字はシリアルモニター上にも表示されます。

```ini
monitor_echo = yes
```

Enterを押した場合は `LF` を送信します。

```ini
monitor_eol = LF
```

そのため、シリアルコマンドを実装する場合は以下のように改行単位で処理できます。

```cpp
String command = Serial.readStringUntil('\n');
```

## ディレクトリ構成

```text
.
├── include/
│   └── config.hpp
├── lib/
│   └── .gitkeep
├── src/
│   └── main.cpp
├── test/
│   └── .gitkeep
├── .gitignore
├── platformio.ini
└── README.md
```

### `include`

プロジェクト全体で使用するヘッダーファイルを配置します。

GPIO番号や通信速度などの設定値は `config.hpp` にまとめます。

### `lib`

プロジェクト固有のライブラリを配置します。

初期状態では空ディレクトリを保持するため `.gitkeep` を配置しています。

### `src`

ESP32で実行するソースコードを配置します。

### `test`

PlatformIOのテストコードを配置します。

初期状態では `.gitkeep` のみ配置しています。

## 基本操作

Visual Studio Code の PlatformIO 拡張機能から、主に以下を使用します。

```text
Build   : プログラムをビルド
Upload  : ESP32へ書き込み
Monitor : シリアルモニターを開く
Clean   : ビルド成果物を削除
```

通常の開発では、

```text
Build
  ↓
Upload
  ↓
Monitor
```

の順で確認します。
