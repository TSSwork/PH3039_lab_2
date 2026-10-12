#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <PDM.h>
#include "fmt.h"

#include <TSSwork-project-1_inferencing.h>

#define EXTERNAL_LED_PIN 2

constexpr uint32_t SAMPLE_RATE = EI_CLASSIFIER_FREQUENCY;
constexpr size_t SAMPLE_COUNT = EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE;

int16_t pdmBuffer[SAMPLE_COUNT];
volatile int pdmSamplesRead = 0;

int16_t tempPdmBuffer[256];

void onPDMdata() {
    int bytesAvailable = PDM.available();
    if (bytesAvailable <= 0) return;

    if (bytesAvailable > sizeof(tempPdmBuffer)) {
        bytesAvailable = sizeof(tempPdmBuffer);
    }

    int bytesRead = PDM.read(tempPdmBuffer, bytesAvailable);
    if (bytesRead <= 0) return;

    int newSamples = bytesRead / sizeof(int16_t);

    // Calculate exactly how much space is left in the main buffer
    int spaceLeft = (int)SAMPLE_COUNT - pdmSamplesRead;

    if (spaceLeft > 0) {
        // Only copy as many samples as we have space for
        int toCopy = (newSamples < spaceLeft) ? newSamples : spaceLeft;
        memcpy(pdmBuffer + pdmSamplesRead, tempPdmBuffer, toCopy * sizeof(int16_t));
        pdmSamplesRead += toCopy;
    }
    // If spaceLeft <= 0, we just ignore the new samples until the loop resets the buffer.
}

void setup() {
    Serial.begin(115200);
    unsigned long start = millis();
    while (!Serial && millis() - start < 3000) { delay(10); }

    logln("Edge Impulse Inference Starting...");

    pinMode(EXTERNAL_LED_PIN, OUTPUT);
    digitalWrite(EXTERNAL_LED_PIN, LOW);

    PDM.onReceive(onPDMdata);
    PDM.setGain(30);

    if (!PDM.begin(1, SAMPLE_RATE)) {
        logln("ERROR: Failed to start PDM microphone!");
        while (1) { delay(1000); }
    }

    csvln("time_ms,label,score");
    logln("Listening... Need {} samples for AI", SAMPLE_COUNT);
}

void loop() {
    static unsigned long lastPrint = 0;
    if (millis() - lastPrint > 2000) {
        logln("Board alive. Samples collected: {}/{}", pdmSamplesRead, SAMPLE_COUNT);
        lastPrint = millis();
    }

    // Once we hit exactly 9600, run the AI
    if (pdmSamplesRead >= (int)SAMPLE_COUNT) {
        signal_t signal;
        signal.total_length = SAMPLE_COUNT;
        signal.get_data = &get_audio_data;

        ei_impulse_result_t result = {0};
        EI_IMPULSE_ERROR res = run_classifier(&signal, &result, false);

        // Reset buffer for the next window
        pdmSamplesRead = 0;

        if (res == EI_IMPULSE_OK) {
            float max_score = 0.0;
            const char* best_label = "unknown";

            for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
                if (result.classification[ix].value > max_score) {
                    max_score = result.classification[ix].value;
                    best_label = result.classification[ix].label;
                }
            }

            csvln("{},{},{}", millis(), best_label, max_score);

            // Control the EXTERNAL LED based on the prediction
            // Using strcasecmp so it works whether Edge Impulse outputs "on", "ON", "On", etc.
            if (max_score > 0.60) {
                if (strcasecmp(best_label, "on") == 0) {
                    digitalWrite(EXTERNAL_LED_PIN, HIGH);
                    logln("LED turned ON");
                } else if (strcasecmp(best_label, "off") == 0) {
                    digitalWrite(EXTERNAL_LED_PIN, LOW);
                    logln("LED turned OFF");
                }
            }
        } else {
            logln("Classifier error: {}", res);
        }
    }
}

static int get_audio_data(size_t offset, size_t length, float *out_buffer) {
    for (size_t i = 0; i < length; i++) {
        out_buffer[i] = pdmBuffer[offset + i] / 32768.0f;
    }
    return 0;
}
