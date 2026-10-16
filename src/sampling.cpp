#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include "fmt.h"

// v5: fast printer. ISR stamps every conversion with a global index;
// loop() hand-formats "idx,raw\n" into a block buffer, one Serial.write
// per burst. No doubles, no fmt in the hot path.

constexpr uint8_t  ADC_PIN = A0;            // XIAO D0/A0 = P0.02 = AIN0
constexpr uint32_t FS      = 20000;         // Hz - keep in sync with --fs
constexpr uint32_t TICK_US = 1000000 / FS;
constexpr size_t   RING_N  = 8192;

static volatile int16_t  ring[RING_N];
static volatile uint32_t ring_no[RING_N];   // global conversion index per slot
static volatile uint32_t ring_head = 0;
static volatile uint32_t ring_tail = 0;
static volatile uint32_t overruns  = 0;
static volatile uint32_t ticks     = 0;     // timer ISR entries
static volatile uint32_t convs     = 0;     // successful conversions
static volatile bool     saadc_ok  = false;

static int16_t saadc_buf __attribute__((aligned(4)));

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
    NRF_SAADC->RESULT.MAXCNT = 1;

    NRF_SAADC->ENABLE = SAADC_ENABLE_ENABLE_Enabled;

    NRF_SAADC->EVENTS_CALIBRATEDONE = 0;
    NRF_SAADC->TASKS_CALIBRATEOFFSET = 1;
    for (uint32_t i = 0; i < 200000 && !NRF_SAADC->EVENTS_CALIBRATEDONE; i++) {}
    return NRF_SAADC->EVENTS_CALIBRATEDONE != 0;
}

extern "C" void TIMER3_IRQHandler(void) {
    if (!NRF_TIMER3->EVENTS_COMPARE[0]) return;
    NRF_TIMER3->EVENTS_COMPARE[0] = 0;
    NRF_TIMER3->CC[0] += TICK_US;
    ticks++;

    NRF_SAADC->EVENTS_STARTED = 0;
    NRF_SAADC->TASKS_START = 1;                 // re-arm EasyDMA (v4 fix)
    uint32_t w = 0;
    while (w++ < 640 && !NRF_SAADC->EVENTS_STARTED) {}
    if (!NRF_SAADC->EVENTS_STARTED) return;

    NRF_SAADC->EVENTS_END = 0;
    NRF_SAADC->TASKS_SAMPLE = 1;
    w = 0;
    while (w++ < 640 && !NRF_SAADC->EVENTS_END) {}
    if (!NRF_SAADC->EVENTS_END) return;

    int16_t v = saadc_buf;
    uint32_t n = convs;                         // global index of this conversion
    convs++;

    uint32_t next = ring_head + 1;
    if (next - ring_tail >= RING_N) {
        overruns++;                             // gap in n becomes visible downstream
    } else {
        ring[ring_head & (RING_N - 1)]   = v;
        ring_no[ring_head & (RING_N - 1)] = n;
        ring_head = next;
    }
}

static void startSampleTimer(void) {
    NRF_TIMER3->TASKS_SHUTDOWN = 1;
    NRF_TIMER3->MODE      = TIMER_MODE_MODE_Timer;
    NRF_TIMER3->BITMODE   = TIMER_BITMODE_BITMODE_32Bit;
    NRF_TIMER3->PRESCALER = 4;                  // 1 MHz ticks
    NRF_TIMER3->CC[0]     = TICK_US;
    NRF_TIMER3->EVENTS_COMPARE[0] = 0;
    NRF_TIMER3->INTENSET  = TIMER_INTENSET_COMPARE0_Msk;
    NVIC_SetPriority(TIMER3_IRQn, 6);
    NVIC_EnableIRQ(TIMER3_IRQn);
    NRF_TIMER3->TASKS_START = 1;
}

// ---- tiny integer formatters (no libc, no doubles) ----
static char* put_u32(char* p, uint32_t v) {
    char tmp[10]; int n = 0;
    if (v == 0) { *p++ = '0'; return p; }
    while (v) { tmp[n++] = char('0' + v % 10); v /= 10; }
    while (n) *p++ = tmp[--n];
    return p;
}
static char* put_i32(char* p, int32_t v) {
    if (v < 0) { *p++ = '-'; v = -v; }
    return put_u32(p, (uint32_t)v);
}

void setup() {
    Serial.begin(115200);
    unsigned long t0 = millis();
    while (!Serial && millis() - t0 < 3000) delay(10);

    logln("idx,adc_raw");                       // header (PC computes time from idx)
    logln("boot v5 fast printer, FS={} Hz", FS);
    saadc_ok = saadcInit(0);
    logln("SAADC init: {}", saadc_ok ? 1 : 0);
    if (saadc_ok) startSampleTimer();
}

void loop() {
    static uint32_t lastHb = 0, lastTicks = 0, lastConvs = 0, lastPrinted = 0;
    static uint32_t printed = 0;

    if (millis() - lastHb > 2000) {
        logln("hb dt={}ms ticks+={} convs+={} printed+={} overruns={}",
              millis() - lastHb, ticks - lastTicks, convs - lastConvs,
              printed - lastPrinted, overruns);
        lastHb = millis(); lastTicks = ticks; lastConvs = convs; lastPrinted = printed;
    }
    if (!saadc_ok) { delay(100); return; }

    static char buf[512];
    char* p = buf;
    char* limit = buf + sizeof(buf) - 24;

    while (ring_tail != ring_head) {
        uint32_t h = ring_tail;
        int16_t  raw = ring[h & (RING_N - 1)];
        uint32_t n   = ring_no[h & (RING_N - 1)];
        ring_tail = h + 1;

        p = put_u32(p, n);
        *p++ = ',';
        p = put_i32(p, raw);
        *p++ = '\n';
        printed++;

        if (p >= limit) {
            Serial.write((const uint8_t*)buf, p - buf);
            p = buf;
        }
    }
    if (p != buf) Serial.write((const uint8_t*)buf, p - buf);
}
