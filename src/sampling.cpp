#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include "fmt.h"

constexpr uint8_t  ADC_PIN = A0;          // XIAO D0/A0 = P0.02 = AIN0
constexpr uint32_t FS      = 20000;       // Hz
constexpr uint32_t TICK_US = 1000000 / FS; // 50 us
constexpr size_t   RING_N  = 8192;

static volatile uint16_t ring[RING_N];
static volatile uint32_t ring_head = 0;
static volatile uint32_t ring_tail = 0;
static volatile uint32_t overruns  = 0;
static volatile bool     saadc_ok  = false;

static uint16_t saadc_buf __attribute__((aligned(4)));

static bool saadcInit(uint8_t ch) {
    NRF_SAADC->ENABLE     = 0;
    NRF_SAADC->RESOLUTION = SAADC_RESOLUTION_VAL_12bit;
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

    NRF_SAADC->INTENSET      = 0;
    NRF_SAADC->RESULT.PTR    = (uint32_t)&saadc_buf;
    NRF_SAADC->RESULT.MAXCNT = 1; // 1 sample per DMA transfer

    NRF_SAADC->ENABLE = SAADC_ENABLE_ENABLE_Enabled;

    NRF_SAADC->EVENTS_CALIBRATEDONE = 0;
    NRF_SAADC->TASKS_CALIBRATEOFFSET = 1;
    for (uint32_t i = 0; i < 200000 && !NRF_SAADC->EVENTS_CALIBRATEDONE; i++) {}
    if (!NRF_SAADC->EVENTS_CALIBRATEDONE) return false;

    return true;
}

extern "C" void TIMER3_IRQHandler(void) {
    if (NRF_TIMER3->EVENTS_COMPARE[0]) {
        NRF_TIMER3->EVENTS_COMPARE[0] = 0;
        NRF_TIMER3->CC[0] += TICK_US;

        // 1. Re-arm the EasyDMA buffer for exactly 1 sample
        NRF_SAADC->EVENTS_STARTED = 0;
        NRF_SAADC->TASKS_START = 1;
        while (!NRF_SAADC->EVENTS_STARTED) {} // Takes ~5us

        // 2. Trigger the actual ADC conversion
        NRF_SAADC->EVENTS_END = 0;
        NRF_SAADC->TASKS_SAMPLE = 1;

        // 3. Wait for conversion to finish
        while (!NRF_SAADC->EVENTS_END) {}     // Takes ~5us

        uint16_t v = saadc_buf;

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
    NRF_TIMER3->PRESCALER = 4;                    // 1 MHz ticks
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

    csvln("idx,time_ms,adc_raw");
    logln("boot: sampling firmware v4 (re-arm fix), FS={} Hz", FS);

    saadc_ok = saadcInit(0);
    logln("SAADC init: {}", saadc_ok ? 1 : 0);
    if (!saadc_ok) logln("SAADC FAILED - timer NOT started");
    else startSampleTimer();
}

void loop() {
    static uint32_t lastHb = 0;
    if (millis() - lastHb > 2000) {
        lastHb = millis();
        logln("hb ok={} overruns={} rows={}", saadc_ok ? 1 : 0, overruns, ring_head);
    }
    if (!saadc_ok) { delay(100); return; }

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
