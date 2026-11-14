#include <Arduino.h>
#include "control/control.hpp"
#include "device/c620.hpp"
#include "protocol/protocol.hpp"

static sakura::Safety safety;

void setup() {
  Serial.begin(115200);
  // ハードウェア・認証の確認が済むまでネットワーク/CANは開始しない。
  // モーターを暗黙に有効化するコンパイル時スイッチは置かない。
  Serial.println("[sakura] DISARM: geometry/GPIO/network/interlock unconfigured; outputs disabled");
}

void loop() { delay(1000); }
