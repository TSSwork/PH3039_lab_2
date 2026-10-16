#include <Arduino.h>
#include <mbed.h>
#include "fmt.h"

// Arduino Nano 33 BLE (mbed core) port of sampling.cpp. Same serial protocol.
// Differences: A0 is SAADC AIN2 (P0.04), the sample clock is an mbed Ticker
// instead of a bare TIMER3 ISR, and there is no TinyUSB.
// v6: capture-then-transfer. PC sends "C <N>\n"; we sample N points at FS
// into RAM with no printing, then dump "idx,raw" rows and "# END".

constexpr uint32_t FS      = 10000;          // Hz - keep in sync with --fs (SWEEP_FS)
constexpr uint32_t TICK_US = 1000000 / FS;
constexpr uint32_t MAX_N   = 50000;          // 100 KB of int16 (safe for 256KB RAM)

static int16_t buf[MAX_N];
static volatile uint32_t cap_n = 0, cap_i = 0, cap_bad = 0;
static volatile bool capturing = false;
static bool saadc_ok = false;
static mbed::Ticker sampler;

static int16_t saadc_buf __attribute__((aligned(4)));

static bool saadcInit(uint8_t ch) {   // ch = SAADC channel slot, pin is fixed to AIN2
    NRF_SAADC->ENABLE     = 0;
    NRF_SAADC->RESOLUTION = SAADC_RESOLUTION_VAL_12bit;
    NRF_SAADC->OVERSAMPLE = SAADC_OVERSAMPLE_OVERSAMPLE_Bypass;
    NRF_SAADC->SAMPLERATE = SAADC_SAMPLERATE_MODE_Task;
    NRF_SAADC->CH[ch].PSELP = SAADC_CH_PSELP_PSELP_AnalogInput2;
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

static void onTick() {
    int16_t v = -32768;                          // marker if a conversion fails
    uint32_t w = 0;
    NRF_SAADC->EVENTS_STARTED = 0;
    NRF_SAADC->TASKS_START = 1;
    while (w++ < 640 && !NRF_SAADC->EVENTS_STARTED) {}
    if (NRF_SAADC->EVENTS_STARTED) {
        NRF_SAADC->EVENTS_END = 0;
        NRF_SAADC->TASKS_SAMPLE = 1;
        w = 0;
        while (w++ < 640 && !NRF_SAADC->EVENTS_END) {}
        if (NRF_SAADC->EVENTS_END) v = saadc_buf;
    }
    if (v == -32768) cap_bad++;

    if (cap_i < cap_n) buf[cap_i++] = v;
    if (cap_i >= cap_n) {                        // done: stop the timer
        sampler.detach();
        capturing = false;
    }
}

static void startCapture(uint32_t n) {
    cap_n = n; cap_i = 0; cap_bad = 0; capturing = true;
    sampler.attach_us(onTick, TICK_US);
}

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

static void dump(uint32_t n) {
    logln("BEGIN n={} fs={} bad={}", n, FS, cap_bad);
    static char out[512];
    char* p = out;
    for (uint32_t i = 0; i < n; i++) {
        p = put_u32(p, i);
        *p++ = ',';
        p = put_i32(p, buf[i]);
        *p++ = '\n';
        if (p >= out + sizeof(out) - 24) {
            Serial.write((const uint8_t*)out, p - out);
            p = out;
        }
    }
    if (p != out) Serial.write((const uint8_t*)out, p - out);
    logln("END");
    Serial.flush();
}

void setup() {
    Serial.begin(115200);
    unsigned long t0 = millis();
    while (!Serial && millis() - t0 < 3000) delay(10);
    saadc_ok = saadcInit(0);
    logln("boot v6 nano33ble capture mode, FS={} Hz, MAX_N={}, saadc_ok={}", FS, MAX_N, saadc_ok ? 1 : 0);
}

void loop() {
    if (!Serial.available()) return;
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.length() < 3 || line[0] != 'C') return;

    uint32_t n = (uint32_t)line.substring(1).toInt();
    if (n == 0) return;
    if (n > MAX_N) n = MAX_N;
    if (!saadc_ok) { logln("ERR saadc"); return; }

    startCapture(n);
    while (capturing) {}                          // wait for capture to finish (~n/FS seconds)
    dump(n);
}
