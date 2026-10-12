#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <PDM.h>
#include <math.h>
#include <string.h>
#include "fmt.h"

// -----------------------------------------------------------------------------
// Normal sound detection settings
// -----------------------------------------------------------------------------

constexpr int SAMPLE_COUNT = 256;
constexpr float SOUND_THRESHOLD = 700.0f;

// -----------------------------------------------------------------------------
// Edge Impulse raw audio capture settings
// -----------------------------------------------------------------------------

constexpr uint32_t SAMPLE_RATE = 16000;
constexpr uint32_t CAPTURE_MS = 2000;
constexpr uint32_t CAPTURE_SAMPLES = (SAMPLE_RATE * CAPTURE_MS) / 1000;

// PDM gives us chunks.
int16_t sampleBuffer[SAMPLE_COUNT];

// Local copy used after the interrupt-safe copy.
int16_t chunkBuffer[SAMPLE_COUNT];

volatile int samplesRead = 0;

// Capture state.
bool captureActive = false;
uint32_t captureRemaining = 0;

// -----------------------------------------------------------------------------
// PDM callback
// -----------------------------------------------------------------------------

void onPDMdata() {
    int bytesAvailable = PDM.available();

    if (bytesAvailable <= 0) {
        return;
    }

    int bytesToRead = bytesAvailable;

    if (bytesToRead > sizeof(sampleBuffer)) {
        bytesToRead = sizeof(sampleBuffer);
    }

    int bytesRead = PDM.read(sampleBuffer, bytesToRead);

    if (bytesRead > 0) {
        samplesRead = bytesRead / sizeof(int16_t);
    }
}

// -----------------------------------------------------------------------------
// Binary capture protocol
// -----------------------------------------------------------------------------
//
// Header:
//   "PCMW"
//   uint32 sample_rate little-endian
//   uint32 total_samples little-endian
//
// Then repeated packets:
//   0xA5 0x5A
//   uint16 sample_count little-endian
//   uint16 bitwise NOT of sample_count little-endian
//   int16 samples[sample_count] little-endian
//
// The Python script understands this format.

void sendCaptureHeader() {
    const uint8_t magic[4] = {'P', 'C', 'M', 'W'};
    Serial.write(magic, 4);

    uint32_t rate = SAMPLE_RATE;
    uint32_t total = CAPTURE_SAMPLES;

    Serial.write((const uint8_t*)&rate, sizeof(rate));
    Serial.write((const uint8_t*)&total, sizeof(total));
}

void sendChunk(const int16_t* samples, int count) {
    if (count <= 0) {
        return;
    }

    uint8_t lo = count & 0xFF;
    uint8_t hi = (count >> 8) & 0xFF;

    uint8_t header[6] = {
        0xA5,
        0x5A,
        lo,
        hi,
        (uint8_t)(~lo),
        (uint8_t)(~hi)
    };

    Serial.write(header, sizeof(header));
    Serial.write((const uint8_t*)samples, count * sizeof(int16_t));
}

void startCapture() {
    if (captureActive) {
        return;
    }

    captureActive = true;
    captureRemaining = CAPTURE_SAMPLES;

    sendCaptureHeader();
    Serial.flush();
}

void handleSerialCommands() {
    while (Serial.available()) {
        char c = Serial.read();

        if (c == 'R' || c == 'r') {
            startCapture();
        }
    }
}

// -----------------------------------------------------------------------------
// Arduino setup/loop
// -----------------------------------------------------------------------------

void setup() {
    Serial.begin(115200);

    // Give the USB serial connection time to open.
    unsigned long start = millis();
    while (!Serial && millis() - start < 5000) {
        delay(10);
    }

    logln("Sound/Raw capture ready");
    logln("Send 'R' to capture a 2-second 16 kHz PCM clip");

    csvln("time_ms,state,rms");

    PDM.onReceive(onPDMdata);
    PDM.setGain(30);

    if (!PDM.begin(1, SAMPLE_RATE)) {
        logln("ERROR: Could not start PDM microphone");
        while (true) {
            delay(1000);
        }
    }

    logln("Microphone started");
}

void loop() {
    handleSerialCommands();

    if (samplesRead <= 0) {
        delay(1);
        return;
    }

    // Copy the chunk safely.
    noInterrupts();

    int count = samplesRead;
    samplesRead = 0;

    if (count > SAMPLE_COUNT) {
        count = SAMPLE_COUNT;
    }

    if (count > 0) {
        memcpy(chunkBuffer, sampleBuffer, count * sizeof(int16_t));
    }

    interrupts();

    if (count <= 0) {
        return;
    }

    // ---------------------------------------------------------------------
    // Raw PCM capture mode for Edge Impulse
    // ---------------------------------------------------------------------

    if (captureActive) {
        int sendCount = count;

        if ((uint32_t)sendCount > captureRemaining) {
            sendCount = (int)captureRemaining;
        }

        if (sendCount > 0) {
            sendChunk(chunkBuffer, sendCount);
            captureRemaining -= sendCount;
        }

        if (captureRemaining == 0) {
            captureActive = false;
            Serial.flush();
            logln("Capture complete: {} samples", CAPTURE_SAMPLES);
        }

        return;
    }

    // ---------------------------------------------------------------------
    // Normal RMS sound detection mode
    // ---------------------------------------------------------------------

    float mean = 0.0f;

    for (int i = 0; i < count; i++) {
        mean += chunkBuffer[i];
    }

    mean /= count;

    float sumSquares = 0.0f;

    for (int i = 0; i < count; i++) {
        float value = chunkBuffer[i] - mean;
        sumSquares += value * value;
    }

    float rms = sqrtf(sumSquares / count);
    int state = (rms > SOUND_THRESHOLD) ? 1 : 0;

    csvln("{},{},{}", millis(), state, rms);
}
