
#include "device/c620_driver.hpp"

#include <Arduino.h>
#include <esp_twai.h>
#include <esp_twai_onchip.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {

constexpr int C620_MOTOR_COUNT = 4;

constexpr gpio_num_t CAN_TX_PIN = GPIO_NUM_4;
constexpr gpio_num_t CAN_RX_PIN = GPIO_NUM_5;

constexpr uint32_t C620_COMMAND_ID = 0x200;
constexpr uint32_t C620_FEEDBACK_ID_BASE = 0x201;

constexpr uint32_t CAN_TX_INTERVAL_US = 2000;
constexpr uint32_t FEEDBACK_TIMEOUT_MS = 100;

twai_node_handle_t canNode = nullptr;

bool canReady = false;
volatile bool txBusy = false;

int16_t currentCommands[C620_MOTOR_COUNT] = {};
uint16_t rotorAngle[C620_MOTOR_COUNT] = {};
int16_t motorRpm[C620_MOTOR_COUNT] = {};
int16_t measuredCurrent[C620_MOTOR_COUNT] = {};
uint8_t motorTemperature[C620_MOTOR_COUNT] = {};

bool feedbackValid[C620_MOTOR_COUNT] = {};
TickType_t feedbackTicks[C620_MOTOR_COUNT] = {};

uint32_t canTxCount = 0;
uint32_t canFeedbackCount = 0;
uint32_t lastCanTxUs = 0;
uint32_t lastCanErrorPrintMs = 0;

// 送信中もデータが消えないようにstaticで保持する。
uint8_t txData[8] = {};

twai_frame_t txFrame = {};

int16_t readInt16BigEndian(uint8_t highByte, uint8_t lowByte)
{
    return static_cast<int16_t>(
        (static_cast<uint16_t>(highByte) << 8) | lowByte
    );
}

int motorIndexFromFeedbackId(uint32_t identifier)
{
    if (
        identifier < C620_FEEDBACK_ID_BASE ||
        identifier >= C620_FEEDBACK_ID_BASE + C620_MOTOR_COUNT
    ) {
        return -1;
    }

    return static_cast<int>(
        identifier - C620_FEEDBACK_ID_BASE
    );
}

// 受信データを保存する。
// この関数はTWAIの受信コールバックから呼ばれる。
void handleFeedback(const twai_frame_t& frame)
{
    if (
        frame.header.ide ||
        frame.header.rtr ||
        frame.header.dlc < 7
    ) {
        return;
    }

    const int motorIndex =
        motorIndexFromFeedbackId(frame.header.id);

    if (motorIndex < 0) {
        return;
    }

    const uint8_t* data = frame.buffer;

    rotorAngle[motorIndex] =
        (static_cast<uint16_t>(data[0]) << 8) | data[1];

    motorRpm[motorIndex] =
        readInt16BigEndian(data[2], data[3]);

    measuredCurrent[motorIndex] =
        readInt16BigEndian(data[4], data[5]);

    motorTemperature[motorIndex] = data[6];

    feedbackValid[motorIndex] = true;
    feedbackTicks[motorIndex] = xTaskGetTickCountFromISR();

    ++canFeedbackCount;
}

// CAN受信イベント。
// 新APIでは受信データをこのコールバック内で取得する。
bool onCanReceive(
    twai_node_handle_t handle,
    const twai_rx_done_event_data_t* eventData,
    void* userContext
)
{
    (void)eventData;
    (void)userContext;

    uint8_t rxBuffer[8] = {};

    twai_frame_t rxFrame = {};
    rxFrame.buffer = rxBuffer;
    rxFrame.buffer_len = sizeof(rxBuffer);

    if (twai_node_receive_from_isr(handle, &rxFrame) == ESP_OK) {
        handleFeedback(rxFrame);
    }

    return false;
}

// CAN送信完了イベント。
bool onCanTransmitDone(
    twai_node_handle_t handle,
    const twai_tx_done_event_data_t* eventData,
    void* userContext
)
{
    (void)handle;
    (void)eventData;
    (void)userContext;

    txBusy = false;

    return false;
}

bool sendCurrentFrame()
{
    if (!canReady || txBusy) {
        return false;
    }

    for (int motorIndex = 0;
         motorIndex < C620_MOTOR_COUNT;
         ++motorIndex) {

        const int16_t command = currentCommands[motorIndex];

        txData[motorIndex * 2] =
            static_cast<uint8_t>((command >> 8) & 0xFF);

        txData[motorIndex * 2 + 1] =
            static_cast<uint8_t>(command & 0xFF);
    }

    // txFrameとtxDataは送信完了まで保持する。
    txBusy = true;

    const esp_err_t result =
        twai_node_transmit(canNode, &txFrame, 0);

    if (result != ESP_OK) {
        txBusy = false;

        const uint32_t now = millis();

        if (now - lastCanErrorPrintMs >= 1000) {
            lastCanErrorPrintMs = now;

            Serial.print("[C620] CAN送信失敗 error=");
            Serial.println(static_cast<int>(result));
        }

        return false;
    }

    ++canTxCount;

    return true;
}

} // namespace

bool c620DriverBegin()
{
    twai_onchip_node_config_t nodeConfig = {};

    nodeConfig.io_cfg.tx = CAN_TX_PIN;
    nodeConfig.io_cfg.rx = CAN_RX_PIN;

    nodeConfig.bit_timing.bitrate = 1000000;
    nodeConfig.tx_queue_depth = 10;

    esp_err_t result =
        twai_new_node_onchip(&nodeConfig, &canNode);

    if (result != ESP_OK) {
        Serial.print("[C620] TWAI node作成失敗 error=");
        Serial.println(static_cast<int>(result));
        return false;
    }

    // 送受信イベントを登録する。
    twai_event_callbacks_t callbacks = {};

    callbacks.on_rx_done = onCanReceive;
    callbacks.on_tx_done = onCanTransmitDone;

    result = twai_node_register_event_callbacks(
        canNode,
        &callbacks,
        nullptr
    );

    if (result != ESP_OK) {
        Serial.print("[C620] callback登録失敗 error=");
        Serial.println(static_cast<int>(result));

        twai_node_delete(canNode);
        canNode = nullptr;

        return false;
    }

    // 送信フレームの初期設定。
    txFrame = {};
    txFrame.header.id = C620_COMMAND_ID;
    txFrame.header.ide = false;
    txFrame.buffer = txData;
    txFrame.buffer_len = sizeof(txData);

    result = twai_node_enable(canNode);

    if (result != ESP_OK) {
        Serial.print("[C620] TWAI開始失敗 error=");
        Serial.println(static_cast<int>(result));

        twai_node_delete(canNode);
        canNode = nullptr;

        return false;
    }

    canReady = true;
    lastCanTxUs = micros();

    Serial.println(
        "[C620] CAN初期化完了 1Mbps TX=GPIO4 RX=GPIO5"
    );

    return true;
}

bool c620DriverIsReady()
{
    return canReady;
}

void c620DriverReadFrames()
{
    // 新TWAI APIでは受信コールバック内で
    // フレームを取得するため、ここでの処理は不要。
}

void c620DriverSendPeriodically()
{
    if (!canReady) {
        return;
    }

    const uint32_t nowUs = micros();

    if (nowUs - lastCanTxUs < CAN_TX_INTERVAL_US) {
        return;
    }

    lastCanTxUs = nowUs;

    sendCurrentFrame();
}

void c620DriverSetCurrentCommand(
    int motorIndex,
    int16_t command
)
{
    if (
        motorIndex < 0 ||
        motorIndex >= C620_MOTOR_COUNT
    ) {
        return;
    }

    currentCommands[motorIndex] = command;
}

int16_t c620DriverGetCurrentCommand(int motorIndex)
{
    if (
        motorIndex < 0 ||
        motorIndex >= C620_MOTOR_COUNT
    ) {
        return 0;
    }

    return currentCommands[motorIndex];
}

void c620DriverZeroAllImmediate()
{
    for (int motorIndex = 0;
         motorIndex < C620_MOTOR_COUNT;
         ++motorIndex) {

        currentCommands[motorIndex] = 0;
    }

    sendCurrentFrame();
}

bool c620DriverFeedbackFresh(int motorIndex)
{
    if (
        motorIndex < 0 ||
        motorIndex >= C620_MOTOR_COUNT
    ) {
        return false;
    }

    if (!feedbackValid[motorIndex]) {
        return false;
    }

    const TickType_t now = xTaskGetTickCount();

    return (
        now - feedbackTicks[motorIndex]
        <= pdMS_TO_TICKS(FEEDBACK_TIMEOUT_MS)
    );
}

int16_t c620DriverGetMotorRpm(int motorIndex)
{
    if (
        motorIndex < 0 ||
        motorIndex >= C620_MOTOR_COUNT
    ) {
        return 0;
    }

    return motorRpm[motorIndex];
}

int16_t c620DriverGetMeasuredCurrent(int motorIndex)
{
    if (
        motorIndex < 0 ||
        motorIndex >= C620_MOTOR_COUNT
    ) {
        return 0;
    }

    return measuredCurrent[motorIndex];
}

uint8_t c620DriverGetMotorTemperature(int motorIndex)
{
    if (
        motorIndex < 0 ||
        motorIndex >= C620_MOTOR_COUNT
    ) {
        return 0;
    }

    return motorTemperature[motorIndex];
}

uint16_t c620DriverGetRotorAngle(int motorIndex)
{
    if (
        motorIndex < 0 ||
        motorIndex >= C620_MOTOR_COUNT
    ) {
        return 0;
    }

    return rotorAngle[motorIndex];
}

uint8_t c620DriverGetFeedbackMask()
{
    uint8_t mask = 0;

    for (int motorIndex = 0;
         motorIndex < C620_MOTOR_COUNT;
         ++motorIndex) {

        if (c620DriverFeedbackFresh(motorIndex)) {
            mask |= static_cast<uint8_t>(1U << motorIndex);
        }
    }

    return mask;
}

uint32_t c620DriverGetTxCount()
{
    return canTxCount;
}

uint32_t c620DriverGetFeedbackCount()
{
    return canFeedbackCount;
}
