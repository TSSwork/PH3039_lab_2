#include <Arduino.h>
#include "fmt.h"

// Seeed nRF52 core: Serial needs TinyUSB pulled in explicitly.
#ifdef ARDUINO_ARCH_NRF52
#include <Adafruit_TinyUSB.h>
#endif

void setup() {
  Serial.begin(115200);
  // Wait briefly for USB CDC (XIAO nRF52840) but never forever.
  while (!Serial && millis() < 3000) delay(10);

  csvln("time_ms,count");
}

void loop() {
  static uint32_t count = 0;
  csvln("{},{}", millis(), count++);
  delay(250);
}
