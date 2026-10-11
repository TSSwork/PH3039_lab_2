#include <Arduino.h>
#include <Adafruit_TinyUSB.h> // Required for XIAO nRF52840 Serial over USB
#include <PDM.h>
#include <math.h>
#include "fmt.h"

constexpr int SAMPLE_COUNT = 256;
constexpr float SOUND_THRESHOLD = 700.0f;

int16_t sampleBuffer[SAMPLE_COUNT];
volatile int samplesRead = 0;

void onPDMdata() {
    int bytesAvailable = PDM.available();
    if (bytesAvailable <= 0) return;

    int bytesToRead = bytesAvailable;
    if (bytesToRead > sizeof(sampleBuffer)) {
        bytesToRead = sizeof(sampleBuffer);
    }

    int bytesRead = PDM.read(sampleBuffer, bytesToRead);
    if (bytesRead > 0) {
        samplesRead = bytesRead / sizeof(int16_t);
    }
}

void setup() {
    Serial.begin(115200);

    // Give the USB serial connection time to open.
    unsigned long start = millis();
    while (!Serial && millis() - start < 5000) {
        delay(10);
    }

    logln("Sound detection starting...");

    PDM.onReceive(onPDMdata);
    PDM.setGain(30);

    if (!PDM.begin(1, 16000)) {
        logln("ERROR: Could not start PDM microphone");
        while (true) {
            delay(1000);
        }
    }

    // Print the CSV header exactly as it will appear in the log
    csvln("time_ms,state,rms");
    logln("Microphone started");
}

void loop() {
    if (samplesRead <= 0) {
        delay(1); // Small yield to prevent watchdog resets
        return;
    }

    // Copy the available samples for processing.
    noInterrupts();
    int count = samplesRead;
    samplesRead = 0;
    interrupts();

    if (count <= 0) return;

    // Remove the DC offset before calculating sound level.
    float mean = 0.0f;
    for (int i = 0; i < count; i++) {
        mean += sampleBuffer[i];
    }
    mean /= count;

    float sumSquares = 0.0f;
    for (int i = 0; i < count; i++) {
        float value = sampleBuffer[i] - mean;
        sumSquares += value * value;
    }

    float rms = sqrtf(sumSquares / count);
    int state = (rms > SOUND_THRESHOLD) ? 1 : 0;

    // Log every chunk processed (~62 times a second).
    // This gives you a beautiful continuous plot of sound volume over time.
    csvln("{},{},{}", millis(), state, rms);
}
