/*
 * AT581X configuration follows the register sequence of espp's at581x
 * component (esp-cpp/espp components/at581x, write_config()), which Espressif's
 * BSP factory demo also drives with its default config. Only the values this
 * exhibit needs are exposed; the rest stay at the vendor defaults.
 */
#include "radar_presence.h"
#include "presence_logic.h"
#include <string.h>
#include <stdio.h>
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "radar";

#define RADAR_OUT_GPIO GPIO_NUM_21     /* BSP_RADAR_OUT_IO in esp-box factory BSP */
#define AT581X_ADDR 0x28
/* The BSP creates the dock bus on the port it does not use itself. */
#define DOCK_I2C_PORT ((CONFIG_BSP_I2C_NUM == 1) ? 0 : 1)
#define NVS_NS "radar"

/* espp at581x register map */
#define REG_RESET 0x00
#define REG_THRESHOLD_LO 0x10
#define REG_THRESHOLD_HI 0x11
#define REG_SELF_CHECK_TIME 0x38
#define REG_TRIGGER_BASE_TIME 0x3D
#define REG_TIME_ENABLE 0x41
#define REG_TRIGGER_KEEP_TIME 0x42
#define REG_PROTECT_TIME 0x4E
#define REG_CHIP_ENABLE 0x55
#define REG_GAIN_5C 0x5C
#define REG_FREQ_5F 0x5F
#define REG_FREQ_60 0x60
#define REG_FREQ_MODE 0x61
#define REG_GAIN_63 0x63
#define REG_POWER_LO 0x67
#define REG_POWER_HI 0x68

/* Vendor defaults (espp Config): 5800 MHz, gain 3, 70 uA. Keep time is 1000 ms
 * here (espp's example value) so the output tracks a person rather than
 * lingering 1.5 s after they leave; presence_logic forgives short gaps. */
#define FREQ_5F 0x46     /* 5800 MHz */
#define FREQ_60 0x9d
#define GAIN_IDX 3
#define POWER_67 0x3     /* 70 uA */
#define POWER_68 0x8
#define BASE_TIME_MS 500u
#define KEEP_TIME_MS 1000u
#define PROTECT_TIME_MS 1000u
#define SELFCHECK_MS 2000u

static const uint8_t GAIN_5C_TABLE[13] = {0x08,0x18,0x28,0x38,0x48,0x58,0x68,0x78,0x88,0x98,0xa8,0xb8,0xc8};
static const uint8_t GAIN_63_TABLE[7] = {0,1,2,3,4,5,6};

static i2c_master_dev_handle_t s_dev;
static radar_status_t s_st;
static bool s_last;
static uint32_t s_high_since;

/* Level 1..10 -> threshold 623..983 in steps of 40, level 6 = 823 (the vendor
 * default). Capped below 1023: the chip is written 1023 - threshold, and a
 * zero margin plausibly triggers on noise (an always-on output would welcome
 * an empty room once and then never re-arm). */
static int reach_threshold(int level) { return 823 + (level - RADAR_REACH_DEFAULT) * 40; }

static esp_err_t wr(uint8_t reg, uint8_t val)
{
    uint8_t b[2] = { reg, val };
    return i2c_master_transmit(s_dev, b, 2, 50);
}

static esp_err_t write_config(int threshold)
{
    esp_err_t e = ESP_OK;
    int delta = 1023 - threshold;
    e |= wr(REG_FREQ_MODE, 0xCA);
    e |= wr(REG_FREQ_5F, FREQ_5F);
    e |= wr(REG_FREQ_60, FREQ_60);
    e |= wr(REG_THRESHOLD_LO, (uint8_t)(delta & 0xFF));
    e |= wr(REG_THRESHOLD_HI, (uint8_t)((delta >> 8) & 0xFF));
    e |= wr(REG_POWER_LO, 0x80 | 0x08 | POWER_67);   /* THRESH_VAL_EN | WORK_TIME_EN */
    e |= wr(REG_POWER_HI, 0x20 | 0x40 | POWER_68);   /* BURST_TIME_EN | THRESH_EN */
    e |= wr(REG_GAIN_5C, GAIN_5C_TABLE[GAIN_IDX]);
    e |= wr(REG_GAIN_63, GAIN_63_TABLE[GAIN_IDX >> 1]);
    for (int i = 0; i < 4; i++) e |= wr(REG_TRIGGER_BASE_TIME + i, (uint8_t)(BASE_TIME_MS >> (8 * i)));
    for (int i = 0; i < 4; i++) e |= wr(REG_TRIGGER_KEEP_TIME + i, (uint8_t)(KEEP_TIME_MS >> (8 * i)));
    for (int i = 0; i < 2; i++) e |= wr(REG_PROTECT_TIME + i, (uint8_t)(PROTECT_TIME_MS >> (8 * i)));
    for (int i = 0; i < 2; i++) e |= wr(REG_SELF_CHECK_TIME + i, (uint8_t)(SELFCHECK_MS >> (8 * i)));
    e |= wr(REG_TIME_ENABLE, 0x01);
    e |= wr(REG_CHIP_ENABLE, 0x04);
    e |= wr(REG_RESET, 0x00);
    e |= wr(REG_RESET, 0x01);
    return e;
}

static uint32_t nvs_u32(nvs_handle_t h, const char *k, uint32_t dflt)
{
    uint32_t v;
    return nvs_get_u32(h, k, &v) == ESP_OK ? v : dflt;
}

void radar_presence_init(void)
{
    memset(&s_st, 0, sizeof(s_st));
    s_st.reach = RADAR_REACH_DEFAULT;
    s_st.dwell_ms = PRESENCE_DEFAULT_DWELL_MS;
    s_st.rearm_ms = PRESENCE_DEFAULT_REARM_MS;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        uint32_t r = nvs_u32(h, "reach", RADAR_REACH_DEFAULT);
        if (r >= 1 && r <= RADAR_REACH_LEVELS) s_st.reach = (int)r;
        /* dwell/rearm: NVS-only tuning (no screen), clamped to sane bounds. */
        uint32_t d = nvs_u32(h, "dwell_ms", s_st.dwell_ms), a = nvs_u32(h, "rearm_ms", s_st.rearm_ms);
        if (d >= 2000 && d <= 20000) s_st.dwell_ms = d;
        if (a >= 5000 && a <= 600000) s_st.rearm_ms = a;
        nvs_close(h);
    }
    s_st.threshold = reach_threshold(s_st.reach);

    /* Output pin first: harmless even without the dock (pull-down = "nobody"). */
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << RADAR_OUT_GPIO, .mode = GPIO_MODE_INPUT,
        .pull_down_en = GPIO_PULLDOWN_ENABLE, .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    gpio_config(&io);

    if (bsp_i2c_init() != ESP_OK) { snprintf(s_st.error, sizeof(s_st.error), "I2C INIT FAILED"); return; }
    i2c_master_bus_handle_t bus = NULL;
    if (i2c_master_get_bus_handle(DOCK_I2C_PORT, &bus) != ESP_OK || bus == NULL) {
        snprintf(s_st.error, sizeof(s_st.error), "NO DOCK BUS");
        return;
    }
    if (i2c_master_probe(bus, AT581X_ADDR, 100) != ESP_OK) {
        snprintf(s_st.error, sizeof(s_st.error), "NOT FOUND 0x28");
        ESP_LOGW(TAG, "AT581X not answering at 0x%02x on dock bus - no radar welcome this boot", AT581X_ADDR);
        return;
    }
    i2c_device_config_t dc = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = AT581X_ADDR, .scl_speed_hz = 100000 };
    if (i2c_master_bus_add_device(bus, &dc, &s_dev) != ESP_OK) {
        snprintf(s_st.error, sizeof(s_st.error), "ADD DEVICE FAILED");
        return;
    }
    if (write_config(s_st.threshold) != ESP_OK) {
        snprintf(s_st.error, sizeof(s_st.error), "CONFIG WRITE FAILED");
        return;
    }
    s_st.present = true;
    ESP_LOGI(TAG, "AT581X ready: reach %d (threshold %d), dwell %lu ms, rearm %lu ms, output GPIO%d",
             s_st.reach, s_st.threshold, (unsigned long)s_st.dwell_ms, (unsigned long)s_st.rearm_ms, RADAR_OUT_GPIO);
}

bool radar_presence_poll(uint32_t now_ms)
{
    if (!s_st.present) return false;
    bool d = gpio_get_level(RADAR_OUT_GPIO) != 0;
    if (d && !s_last) { s_st.rises++; s_high_since = now_ms; }
    s_last = d;
    s_st.detected = d;
    s_st.high_ms = d ? now_ms - s_high_since : 0;
    return d;
}

void radar_presence_get(radar_status_t *out) { *out = s_st; }

void radar_presence_set_reach(int level)
{
    if (level < 1) level = 1;
    if (level > RADAR_REACH_LEVELS) level = RADAR_REACH_LEVELS;
    s_st.reach = level;
    s_st.threshold = reach_threshold(level);
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u32(h, "reach", (uint32_t)level);
        nvs_commit(h);
        nvs_close(h);
    }
    if (s_st.present && write_config(s_st.threshold) != ESP_OK)
        snprintf(s_st.error, sizeof(s_st.error), "CONFIG WRITE FAILED");
}
