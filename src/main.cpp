#include <Arduino.h>

#include "config.hpp"

void setup()
{
    pinMode(LED_PIN, OUTPUT);

    Serial.begin(SERIAL_BAUD);
    Serial.println("ESP32 started");
}

void loop()
{
    digitalWrite(LED_PIN, HIGH);
    Serial.println("LED ON");

    delay(LED_BLINK_INTERVAL_MS);

    digitalWrite(LED_PIN, LOW);
    Serial.println("LED OFF");

    delay(LED_BLINK_INTERVAL_MS);
}