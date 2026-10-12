#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <PDM.h>
#include "fmt.h"

// Include your downloaded Edge Impulse library
#include <TSSwork-project-1_inferencing.h>

// Define the external LED pin (The pin labeled D2 on the XIAO)
#define EXTERNAL_LED_PIN 10

constexpr uint32_t SAMPLE_RATE = EI_CLASSIFIER_FREQUENCY;
constexpr size_t SAMPLE_COUNT = EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE;

int16_t sampleBuffer[SAMPLE_COUNT];
volatile bool data_ready = false;

// PDM interrupt callback
void onPDMdata() {
    int bytesAvailable = PDM.available();
    if (bytesAvailable >= (int)(SAMPLE_COUNT * sizeof(int16_t))) {
        PDM.read(sampleBuffer, SAMPLE_COUNT * sizeof(int16_t));
        data_ready = true;
        // Clear the rest of the buffer to prevent overflow
        while (PDM.available()) {
            int16_t dummy;
            PDM.read(&dummy, sizeof(int16_t));
.        }
    }
}

void setup() {
    Serial.begin(115200);

    unsigned long start = millis();
    while (!Serial && millis() - start < 5000) {
        delay(10);
    }

    logln("Edge Impulse Inference Starting...");

    // Setup the built-in LED (Exercise 3 requirement)
    pinMode(EXTERNAL_LED_PIN, OUTPUT);
    digitalWrite(EXTERNAL_LED_PIN, LOW);

    // Initialize PDM Microphone
    PDM.onReceive(onPDMdata);
    PDM.setGain(30); // Adjust if audio is too quiet or clipping

    if (!PDM.begin(1, SAMPLE_RATE)) {
        logln("ERROR: Failed to start PDM microphone!");
        while (1) { delay(1000); }
    }

    csvln("time_ms,label,score");
    logln("Listening for voice commands...");
}

void loop() {
    if (data_ready) {
        data_ready = false;

        signal_t signal;
        signal.total_length = SAMPLE_COUNT;
        signal.get_data = &get_audio_data;

        ei_impulse_result_t result = {0};

        // Run the classifier
        EI_IMPULSE_ERROR res = run_classifier(&signal, &result, false);

        if (res == EI_IMPULSE_OK) {
            float max_score = 0.0;
            const char* best_label = "unknown";

            // Find the class with the highest confidence score
            for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
                if (result.classification[ix].value > max_score) {
                    max_score = result.classification[ix].value;
                    best_label = result.classification[ix].label;
                }
            }

            // Only act and log if the model is reasonably confident (> 60%)
            if (max_score > 0.60) {
                csvln("{},{},{}", millis(), best_label, max_score);

                // Control the EXTERNAL LED based on the prediction
                if (strcmp(best_label, "on") == 0) {
                    digitalWrite(EXTERNAL_LED_PIN, HIGH); // Turn ON
                    logln("LED turned ON");
                } else if (strcmp(best_label, "off") == 0) {
                    digitalWrite(EXTERNAL_LED_PIN, LOW);  // Turn OFF
                    logln("LED turned OFF");
                }
            }
        } else {
            logln("Failed to run classifier: {}", res);
        }
    }
}

// Helper function to convert int16_t PDM data to float [-1.0, 1.0] for Edge Impulse
static int get_audio_data(size_t offset, size_t length, float *out_buffer) {
    for (size_t i = 0; i < length; i++) {
        out_buffer[i] = sampleBuffer[offset + i] / 32768.0f;
    }
    return 0;
}
