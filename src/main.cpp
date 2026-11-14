#include <Arduino.h>

#include "device/c620_driver.hpp"
#include "chassis_ctrl/chassis_ctrl.hpp"

void setup()
{
    Serial.begin(115200);
    delay(1000);

    // C620・M3508用CAN通信初期化
    if (!c620DriverBegin()) {
        Serial.println("[C620] CAN初期化失敗");
    }

    // オムニホイール足回り制御初期化
    chassisCtrlBegin();

    // 起動時は停止状態にする
    chassisCtrlStop();

    Serial.println("[SYSTEM] 初期化完了");
}

void loop()
{
    // CANフィードバック受信
    c620DriverReadFrames();

    // 足回り制御更新
    chassisCtrlUpdate();

    // C620へ電流指令送信
    c620DriverSendPeriodically();
}
