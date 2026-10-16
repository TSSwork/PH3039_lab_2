#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include "fmt.h"

// ---------------------------------------------------------------------------
// Constant-rate ADC sampler v2 - direct SAADC, ~5 us per conversion.
// v1 used analogRead() in the ISR (~420 us!) which capped us at ~2.3 kS/s.
// Gain 1/6 + internal 0.6 V ref  ->  full scale 0..3.6 V, 12 bit.
// ---------------------------------------------------------------------------

constexpr uint8_t  ADC_PIN  = A0;            // XIAO D0/A0 = P0.02 = AIN0
constexpr uint32_t FS       = 20000;         // [Hz] keep in sync with --fs
constexpr uint32_t TICK_US  = 1000000 / FS;
constexpr size_t   RING_N   = 8192;

// Variant table mapping Arduino analog pin -> SAADC channel.
// If the linker complains, replace its use below with 0 (A0 = AIN0 on XIAO).
extern const uint8_t g_ADCPin2SAADCChannel[];

volatile uint16_t ring[RING_N];
volatile uint32_t ring_head = 0;
volatile uint32_t ring_tail = 0;
volatile uint32_t overruns  = 0;

static uint16_t saadc_buf __attribute__((aligned(4)));

static void saadcInit(uint8_t ch) {
    NRF_SAADC->ENABLE   = 0;
    NRF_SAADC->RESOLUTION = SAADC_RESOLUTION_RES_12bit;
    NRF_SAADC->OVERSAMPLE = SAADC_OVERSAMPLE_OVERSAMPLE_Bypass;
    NRF_SAADC->SAMPLERATE = SAADC_SAMPLERATE_MODE_Task;

    NRF_SAADC->CH[ch].PSELP = SAADC_CH_PSELP_PSELP_AnalogInput0 + ch;
    NRF_SAADC->CH[ch].PSELN = SAADC_CH_PSELN_PSELN_NC;
    NRF_SAADC->CH[ch].CONFIG =
        (SAADC_CH_CONFIG_RESP_Bypass     << SAADC_CH_CONFIG_RESP_Pos)   |
        (SAADC_CH_CONFIG_RESN_Bypass     << SAADC_CH_CONFIG_RESN_Pos)   |
        (SAADC_CH_CONFIG_GAIN_Gain1_6    << SAADC_CH_CONFIG_GAIN_Pos)   |
        (SAADC_CH_CONFIG_REFSEL_Internal << SAADC_CH_CONFIG_REFSEL_Pos) |
        (SAADC_CH_CONFIG_TACQ_3us        << SAADC_CH_CONFIG_TACQ_Pos)   |
        (SAADC_CH_CONFIG_MODE_SE         << SAADC_CH_CONFIG_MODE_Pos)   |
        (SAADC_CH_CONFIG_BURST_Disabled  << SAADC_CH_CONFIG_BURST_Pos);
    NRF_SAADC->CH[ch].LIMIT =
        (SAADC_CH_LIMIT_HIGH_Max << SAADC_CH_LIMIT_HIGH_Pos) |
        (SAADC_CH_LIMIT_LOW_Min  << SAADC_CH_LIMIT_LOW_Pos);

    NRF_SAADC->RESULT.PTR    = (uint32_t)&saadc_buf;
    NRF_SAADC->RESULT.MAXCNT = 1;

    NRF_SAADC->ENABLE = SAADC_ENABLE_ENABLE_Enabled;
    NRF_SAADC->TASKS_CALIBRATEOFFSET = 1;
    while (!NRF_SAADC->EVENTS_CALIBRATEDONE) {}
    NRF_SAADC->EVENTS_CALIBRATEDONE = 0;

    NRF_SAADC->TASKS_START = 1;              // leave it started forever
    while (!NRF_SAADC->EVENTS_STARTED) {}
    NRF_SAADC->EVENTS_STARTED = 0;
}

extern "C" void TIMER3_IRQHandler(void) {
    if (NRF_TIMER3->EVENTS_COMPARE[0]) {
        NRF_TIMER3->EVENTS_COMPARE[0] = 0;
        NRF_TIMER3->CC[0] += TICK_US;        // constant rate, jitter-free

        NRF_SAADC->EVENTS_END = 0;
        NRF_SAADC->TASKS_SAMPLE = 1;         // sample + convert (~5 us)
        for (volatile uint32_t w = 0; w < 2000 && !NRF_SAADC->EVENTS_END; w++) {}
        uint16_t v = saadc_buf;

        uint32_t next = ring_head + 1;
        if (next - ring_tail >= RING_N) {
            overruns++;                      // printer/USB fell behind
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
    NRF_TIMER3->PRESCALER = 4;               // 1 MHz ticks
    NRF_TIMER3->CC[0]     = TICK_US;
    NRF_TIMER3->EVENTS_COMPARE[0] = 0;
    NRF_TIMER3->INTENSET  = TIMER_INTENSET_COMPARE0_Msk;
    NVIC_SetPriority(TIMER3_IRQn, 6);
    NVIC_EnableIRQ(TIMER3_IRQn);
    NRF_TIMER3->TASKS_START = 1;
}

void setup() {
    Serial.begin(115200);
    unsigned long t0 = millis();
    while (!Serial && millis() - t0 < 3000) delay(10);

    saadcInit(g_ADCPin2SAADCChannel[ADC_PIN]);
    csvln("idx,time_ms,adc_raw");
    logln("SAADC direct: {} Hz on A0, 12-bit, 0..3.6 V", FS);
    startSampleTimer();
}

void loop() {
    static uint32_t lastHb = 0;
    if (millis() - lastHb > 2000) {
        lastHb = millis();
        logln("overruns={}", overruns);      // must stay 0
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
