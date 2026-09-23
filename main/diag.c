/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * See diag.h.
 */
#include "diag.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_cpu.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

static const char *TAG = "diag";

/* ---- breadcrumb (RTC slow memory, survives panic/WDT/SW resets) -------- */

#define DIAG_MAGIC      0xD1A6C0DEu
#define DIAG_RING_SIZE  2048
/* Longest log line kept in the breadcrumb (see s_scratch below). The serial
 * output is unaffected; only the breadcrumb copy is truncated. */
#define DIAG_LINE_MAX   96

typedef struct {
    uint32_t magic;
    uint32_t boot;
    uint32_t head;              /* bytes ever written; index = head % DIAG_RING_SIZE */
    uint32_t uptime_s;
    uint32_t internal_free;
    uint32_t internal_min_free;
    uint32_t internal_largest;
    uint32_t psram_free;
    char page[12];
    char cmd[16];
    uint32_t crc;               /* esp_rom_crc32_le over everything above */
    /* Not covered by the CRC (recomputing 2 KB on every log line is not
     * "cheap"); validity rests on the header, and the text is sanitised to
     * printable ASCII when it is read back. */
    char ring[DIAG_RING_SIZE];
} diag_rtc_t;

static RTC_NOINIT_ATTR diag_rtc_t s_rtc;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_armed;            /* s_rtc initialised for this boot */
static vprintf_like_t s_orig_vprintf;

static inline uint32_t header_crc(void)
{
    return esp_rom_crc32_le(0, (const uint8_t *)&s_rtc, offsetof(diag_rtc_t, crc));
}

/* Caller holds s_lock. */
static inline void seal(void)
{
    s_rtc.crc = header_crc();
}

static void ring_append(const char *s, size_t n)
{
    portENTER_CRITICAL_SAFE(&s_lock);
    if (s_armed) {
        uint32_t h = s_rtc.head;
        for (size_t i = 0; i < n; i++) {
            s_rtc.ring[(h + i) % DIAG_RING_SIZE] = s[i];
        }
        s_rtc.head = h + (uint32_t)n;
        seal();
    }
    portEXIT_CRITICAL_SAFE(&s_lock);
}

/* One shared scratch line, not a stack buffer: this hook runs on every task
 * that logs, including sys_evt (2304-byte stack) and the FreeRTOS timer task
 * (2048), and a 96-byte local would come straight out of their margin. The
 * owner is claimed with a compare-and-set; a task that finds it taken (two
 * tasks logging at the same instant) skips the breadcrumb copy for that one
 * line rather than wait. The serial log itself is never skipped. */
static RTC_NOINIT_ATTR char s_scratch[DIAG_LINE_MAX];
static volatile uint32_t s_scratch_busy;

static int diag_vprintf(const char *fmt, va_list ap)
{
    if (esp_cpu_compare_and_set(&s_scratch_busy, 0, 1)) {
        va_list cp;
        va_copy(cp, ap);
        int n = vsnprintf(s_scratch, sizeof(s_scratch), fmt, cp);
        va_end(cp);
        if (n > 0) {
            size_t len = (size_t)n < sizeof(s_scratch) ? (size_t)n : sizeof(s_scratch) - 1;
            if ((size_t)n >= sizeof(s_scratch)) {
                s_scratch[len - 1] = '\n';   /* truncated: keep the line structure */
            }
            ring_append(s_scratch, len);
        }
        s_scratch_busy = 0;
    }
    return s_orig_vprintf ? s_orig_vprintf(fmt, ap) : vprintf(fmt, ap);
}

static void copy_str(char *dst, size_t cap, const char *src)
{
    if (src == NULL) {
        src = "";
    }
    size_t i = 0;
    for (; i + 1 < cap && src[i]; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

void diag_set_page(const char *name)
{
    portENTER_CRITICAL_SAFE(&s_lock);
    if (s_armed) {
        copy_str(s_rtc.page, sizeof(s_rtc.page), name);
        seal();
    }
    portEXIT_CRITICAL_SAFE(&s_lock);
}

void diag_set_cmd(const char *name)
{
    portENTER_CRITICAL_SAFE(&s_lock);
    if (s_armed) {
        copy_str(s_rtc.cmd, sizeof(s_rtc.cmd), name);
        seal();
    }
    portEXIT_CRITICAL_SAFE(&s_lock);
}

void diag_tick(void)
{
    /* Read outside the lock: these walk heap metadata under their own locks. */
    uint32_t f = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    uint32_t m = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    uint32_t l = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    uint32_t p = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    uint32_t up = (uint32_t)(esp_timer_get_time() / 1000000ULL);

    portENTER_CRITICAL_SAFE(&s_lock);
    if (s_armed) {
        s_rtc.internal_free = f;
        s_rtc.internal_min_free = m;
        s_rtc.internal_largest = l;
        s_rtc.psram_free = p;
        s_rtc.uptime_s = up;
        seal();
    }
    portEXIT_CRITICAL_SAFE(&s_lock);
}

/* ---- boot-time capture ------------------------------------------------- */

static diag_counters_t s_counters;
static diag_crash_t s_crash = { .last_words = "" };
static volatile bool s_after_crash_pending;

static const char *classify(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_PANIC:
    case ESP_RST_CPU_LOCKUP:
        return "panic";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
        return "watchdog";
    case ESP_RST_BROWNOUT:
    case ESP_RST_PWR_GLITCH:
        return "brownout";
    default:
        return NULL;
    }
}

static bool breadcrumb_valid(void)
{
    return s_rtc.magic == DIAG_MAGIC && s_rtc.crc == header_crc();
}

/* Copies the ring, oldest byte first, into PSRAM as printable ASCII. */
static void capture_last_words(void)
{
    uint32_t head = s_rtc.head;
    size_t len = head < DIAG_RING_SIZE ? head : DIAG_RING_SIZE;
    uint32_t start = head < DIAG_RING_SIZE ? 0 : head % DIAG_RING_SIZE;

    char *buf = heap_caps_malloc(len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buf == NULL) {
        return;
    }
    size_t o = 0;
    bool skip_partial = head > DIAG_RING_SIZE;   /* first line was overwritten mid-way */
    for (size_t i = 0; i < len; i++) {
        char c = s_rtc.ring[(start + i) % DIAG_RING_SIZE];
        if (skip_partial) {
            if (c == '\n') {
                skip_partial = false;
            }
            continue;
        }
        if (c == '\r') {
            continue;
        }
        buf[o++] = (c == '\n' || (c >= 0x20 && c < 0x7f)) ? c : '?';
    }
    buf[o] = '\0';
    s_crash.last_words = buf;
}

static void nvs_get_u32_or(nvs_handle_t h, const char *key, uint32_t *v)
{
    uint32_t t;
    if (nvs_get_u32(h, key, &t) == ESP_OK) {
        *v = t;
    }
}

static void update_counters(esp_reset_reason_t reason, const char *kind)
{
    diag_counters_t *c = &s_counters;
    nvs_handle_t h;

    /* The Black Box namespace belongs to status_deck_ui.c, which bumps
     * bootcnt and erases selfrst later in boot. Read-only here. */
    uint32_t bootcnt = 0;
    uint8_t selfrst = 0;
    if (nvs_open("blackbox", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u32(h, "bootcnt", &bootcnt);
        nvs_get_u8(h, "selfrst", &selfrst);
        nvs_close(h);
    }
    c->boot = bootcnt + 1;

    if (nvs_open("diag", NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "NVS namespace 'diag' unavailable - counters not persisted this boot");
        c->counting_since = c->boot;
        return;
    }
    bool dirty = false;
    if (nvs_get_u32(h, "since", &c->counting_since) != ESP_OK) {
        c->counting_since = c->boot;
        nvs_set_u32(h, "since", c->counting_since);
        dirty = true;
    }
    nvs_get_u32_or(h, "panics", &c->panics);
    nvs_get_u32_or(h, "panic_at", &c->panic_last_boot);
    nvs_get_u32_or(h, "wdts", &c->wdt_resets);
    nvs_get_u32_or(h, "wdt_at", &c->wdt_last_boot);
    nvs_get_u32_or(h, "selfrst", &c->self_restarts);
    nvs_get_u32_or(h, "selfrst_at", &c->self_restart_last_boot);
    nvs_get_u32_or(h, "brownouts", &c->brownouts);
    nvs_get_u32_or(h, "brown_at", &c->brownout_last_boot);

    if (kind != NULL && strcmp(kind, "panic") == 0) {
        c->panics++;
        c->panic_last_boot = c->boot;
        nvs_set_u32(h, "panics", c->panics);
        nvs_set_u32(h, "panic_at", c->panic_last_boot);
        dirty = true;
    } else if (kind != NULL && strcmp(kind, "watchdog") == 0) {
        c->wdt_resets++;
        c->wdt_last_boot = c->boot;
        nvs_set_u32(h, "wdts", c->wdt_resets);
        nvs_set_u32(h, "wdt_at", c->wdt_last_boot);
        dirty = true;
    } else if (kind != NULL && strcmp(kind, "brownout") == 0) {
        c->brownouts++;
        c->brownout_last_boot = c->boot;
        nvs_set_u32(h, "brownouts", c->brownouts);
        nvs_set_u32(h, "brown_at", c->brownout_last_boot);
        dirty = true;
    }
    if (selfrst != 0 && reason == ESP_RST_SW) {
        c->self_restarts++;
        c->self_restart_last_boot = c->boot;
        nvs_set_u32(h, "selfrst", c->self_restarts);
        nvs_set_u32(h, "selfrst_at", c->self_restart_last_boot);
        dirty = true;
    }
    if (dirty) {
        nvs_commit(h);
    }
    nvs_close(h);
}

void diag_init(void)
{
    esp_reset_reason_t reason = esp_reset_reason();
    const char *kind = classify(reason);

    update_counters(reason, kind);

    if (kind != NULL) {
        s_crash.happened = true;
        s_crash.kind = kind;
        s_after_crash_pending = true;
        if (breadcrumb_valid()) {
            s_crash.breadcrumb_valid = true;
            s_crash.boot = s_rtc.boot;
            s_crash.uptime_s = s_rtc.uptime_s;
            copy_str(s_crash.page, sizeof(s_crash.page), s_rtc.page);
            copy_str(s_crash.cmd, sizeof(s_crash.cmd), s_rtc.cmd);
            s_crash.internal_free = s_rtc.internal_free;
            s_crash.internal_min_free = s_rtc.internal_min_free;
            s_crash.internal_largest = s_rtc.internal_largest;
            s_crash.psram_free = s_rtc.psram_free;
            capture_last_words();
        }
    }

    /* A fresh breadcrumb for this boot, whatever the last one held. */
    portENTER_CRITICAL_SAFE(&s_lock);
    memset(&s_rtc, 0, sizeof(s_rtc));
    s_rtc.magic = DIAG_MAGIC;
    s_rtc.boot = s_counters.boot;
    copy_str(s_rtc.page, sizeof(s_rtc.page), "BOOT");
    copy_str(s_rtc.cmd, sizeof(s_rtc.cmd), "NONE");
    seal();
    s_armed = true;
    portEXIT_CRITICAL_SAFE(&s_lock);

    s_orig_vprintf = esp_log_set_vprintf(diag_vprintf);

    const diag_counters_t *c = &s_counters;
    ESP_LOGI(TAG, "boot #%lu: panics %lu (last #%lu), wdt %lu (last #%lu), self-restarts %lu (last #%lu), "
             "brownouts %lu (last #%lu), counting since #%lu",
             (unsigned long)c->boot, (unsigned long)c->panics, (unsigned long)c->panic_last_boot,
             (unsigned long)c->wdt_resets, (unsigned long)c->wdt_last_boot,
             (unsigned long)c->self_restarts, (unsigned long)c->self_restart_last_boot,
             (unsigned long)c->brownouts, (unsigned long)c->brownout_last_boot,
             (unsigned long)c->counting_since);
    if (s_crash.happened) {
        ESP_LOGW(TAG, "previous boot ended in a %s; breadcrumb %s (page %s, cmd %s, internal min-free %lu, %u bytes of log)",
                 s_crash.kind, s_crash.breadcrumb_valid ? "valid" : "missing/invalid",
                 s_crash.page, s_crash.cmd, (unsigned long)s_crash.internal_min_free,
                 (unsigned)strlen(s_crash.last_words));
    }
}

const diag_counters_t *diag_counters(void)
{
    return &s_counters;
}

const diag_crash_t *diag_crash(void)
{
    return &s_crash;
}

bool diag_after_crash_pending(void)
{
    return s_after_crash_pending;
}

void diag_after_crash_delivered(void)
{
    s_after_crash_pending = false;
}

/* ---- stacks ------------------------------------------------------------ */

/* Every task this firmware creates, plus the ESP-IDF ones it depends on.
 * Looked up by name so no module has to export its handle; a task that does
 * not exist (not started, or failed) is simply skipped. */
static const char *const s_task_names[] = {
    "taskLVGL", "ota", "report", "backend_health", "catalog", "notif_poll",
    "remote_poll", "audio_worker", "audio_uploader", "voice_detect", "voice_feed",
    "handshake_worker", "tiT", "wifi", "sys_evt", "esp_timer", "ipc0", "ipc1",
    "IDLE0", "IDLE1",
};

void diag_stack_hwm(diag_stack_cb_t cb, void *ctx)
{
    for (size_t i = 0; i < sizeof(s_task_names) / sizeof(s_task_names[0]); i++) {
        TaskHandle_t t = xTaskGetHandle(s_task_names[i]);
        if (t != NULL) {
            /* ESP-IDF's StackType_t is a byte, so this is bytes. */
            cb(s_task_names[i], (uint32_t)uxTaskGetStackHighWaterMark(t), ctx);
        }
    }
}

#if DIAG_TEST_CRASH
void diag_test_crash(const char *why)
{
    diag_set_cmd("TESTCRASH");
    diag_tick();
    ESP_LOGE(TAG, "DELIBERATE TEST CRASH (%s) - abort() now; expect an after-crash report", why);
    abort();
}
#endif
