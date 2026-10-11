#include <Arduino.h>
#include "fmt.h"

void setup() {
    Serial.begin(115200);

    // Wait briefly for USB CDC serial on nRF52840 boards.
    // Do not wait forever, otherwise the board can appear stuck
    // if no serial monitor is open.
    while (!Serial && millis() < 3000) {
        delay(10);
    }

    csvln("time_ms,count");
}

void loop() {
    static uint32_t count = 0;

    csvln("{},{}", millis(), count);
    count++;

    delay(250);
}
