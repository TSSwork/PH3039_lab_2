#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include "fmt.h"

// ---------------------------------------------------------------------------
// Constant-rate ADC sampler for the Sampling & FFT lab.
// TIMER3 interrupt fires at exactly FS Hz; the ISR takes one 12-bit sample
// into a ring buffer; loop() only prints. Sampling cadence never depends on
// USB/serial timing (labsheet requirement: constant sampling rate).
// ---------------------------------------------------------------------------

constexpr uint8_t  ADC_PIN  = A0;            // XIAO pin D0 / A0 (BNC signal wire)
constexpr uint32_t FS       = 20000;         // sampling rate [Hz]  <-- keep in sync with --fs
constexpr uint32_t TICK_US  = 1000000 / FS;  // timer period [us]
constexpr size_t   RING_N   = 8192;          // power of two

volatile uint16_t ring[RING_N];
volatile uint32_t ring_head = 0;             // written by ISR
volatile uint32_t ring_tail = 0;             // read by printer
volatile uint32_t overruns  = 0;             // printer fell behind

extern "C" void TIMER3_IRQHandler(void) {
    if (NRF_TIMER3->EVENTS_COMPARE[0]) {
        NRF_TIMER3->EVENTS_COMPARE[0] = 0;
        NRF_TIMER3->CC[0] += TICK_US;        // jitter-free constant rate

        uint16_t v = (uint16_t)analogRead(ADC_PIN);
        uint32_t next = ring_head + 1;
        if (next - ring_tail >= RING_N) {
            overruns++;
        } else {
            ring[ring_head & (RING_N - 1)] = v;
            ring_head = next;
        }
    }
}

static void startSampleTimer(void) {
    NRF_TIMER3->TASKS_SHUTDOWN = 1;
    NRF_TIMER3->MODE      = TIMER_MODE_MODE_Timer;
    NRF_TIMER3->BITMODE   = TIMER_BITMODE_BITMODE_32Bit;
    NRF_TIMER3->PRESCALER = 4;               // 16 MHz / 2^4 = 1 MHz ticks
    NRF_TIMER3->CC[0]     = TICK_US;
    NRF_TIMER3->EVENTS_COMPARE[0] = 0;
    NRF_TIMER3->INTENSET  = TIMER_INTENSET_COMPARE0_Msk;
    NVIC_SetPriority(TIMER3_IRQn, 6);
    NVIC_EnableIRQ(TIMER3_IRQn);
    NRF_TIMER3->TASKS_START = 1;
}

void setup() {
    Serial.begin(115200);                    // native USB: real speed is USB, not 115200
    unsigned long t0 = millis();
    while (!Serial && millis() - t0 < 3000) delay(10);

    analogReadResolution(12);
    analogRead(ADC_PIN);                     // discard first conversion

    csvln("idx,time_ms,adc_raw");
    logln("Sampling at {} Hz on A0 (12-bit)", FS);
    startSampleTimer();
}

void loop() {
    static uint32_t lastHb = 0;
    if (millis() - lastHb > 2000) {
        lastHb = millis();
        logln("overruns={}", overruns);      // should stay 0
    }

    int burst = 0;
    while (ring_tail != ring_head && burst < 256) {
        uint32_t idx = ring_tail;
        uint16_t raw = ring[idx & (RING_N - 1)];
        ring_tail = idx + 1;

        double t_ms = (double)idx * 1000.0 / (double)FS;
        csvln("{},{},{}", idx, t_ms, raw);
        burst++;
    }
}
