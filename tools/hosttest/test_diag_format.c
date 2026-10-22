/* Host test for the DIAGNOSTICS page model (main/ui_diag_format.c): formats at
 * their extremes, freshness from timestamps, and the truthfulness rules
 * (nothing reads OK before its source has reported). See run.sh. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "ui_diag_format.h"

static int fails;
#define CHECK(c, what) do { printf("  %s  %s\n", (c) ? "PASS" : "FAIL", what); if (!(c)) fails++; } while (0)
#define EQ(got, want) do { CHECK(strcmp((got), (want)) == 0, want); \
    if (strcmp((got), (want)) != 0) printf("        got \"%s\"\n", (got)); } while (0)

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    char b[32];

    printf("D1) uptime: days line + h/m line, up to uint32 seconds\n");
    ui_diag_fmt_days(b, sizeof b, 0); EQ(b, "0 days");
    ui_diag_fmt_days(b, sizeof b, 86400); EQ(b, "1 day");
    ui_diag_fmt_days(b, sizeof b, 365ull * 86400 + 6 * 3600 + 52 * 60); EQ(b, "365 days");
    ui_diag_fmt_hm(b, sizeof b, 365ull * 86400 + 6 * 3600 + 52 * 60); EQ(b, "06 h 52 m");
    ui_diag_fmt_days(b, sizeof b, UINT32_MAX); EQ(b, "49710 days");
    ui_diag_fmt_hm(b, sizeof b, UINT32_MAX); EQ(b, "06 h 28 m");
    ui_diag_fmt_days(b, sizeof b, UINT64_MAX); CHECK(strlen(b) > 0 && strlen(b) < sizeof b, "uint64 max: no overflow");

    printf("D2) ages: seconds -> days, no 32-bit overflow past 49 days\n");
    ui_diag_fmt_age_ms(b, sizeof b, 0); EQ(b, "<1 s");
    ui_diag_fmt_age_ms(b, sizeof b, 999); EQ(b, "<1 s");
    ui_diag_fmt_age_ms(b, sizeof b, 42000); EQ(b, "42 s");
    ui_diag_fmt_age_ms(b, sizeof b, 134000); EQ(b, "2m 14s");
    ui_diag_fmt_age_ms(b, sizeof b, 3ull * 3600000 + 5 * 60000); EQ(b, "3h 05m");
    ui_diag_fmt_age_ms(b, sizeof b, 50ull * 86400000); EQ(b, "50d 0h");
    ui_diag_view_t v = { .wifi = UI_DIAG_WIFI_ONLINE, .api = UI_DIAG_API_DOWN, .api_ever_ok = true, .api_secs_since_ok = UINT32_MAX };
    ui_diag_line_t l = ui_diag_api_line(&v);
    EQ(l.detail, "49710d 6h");

    printf("D3) heap KiB, one decimal, no unit repeated\n");
    ui_diag_fmt_kib(b, sizeof b, 1207); EQ(b, "1.2");
    ui_diag_fmt_kib(b, sizeof b, 6144); EQ(b, "6.0");
    ui_diag_fmt_kib(b, sizeof b, 42190); EQ(b, "41.2");
    ui_diag_fmt_kib(b, sizeof b, 0); EQ(b, "0.0");
    ui_diag_fmt_kib(b, sizeof b, UINT32_MAX); EQ(b, "4194304");
    ui_diag_view_t h = { .heap_min = 1207, .heap_largest = 6144 };
    CHECK(ui_diag_heap_min_sev(&h) == UI_DIAG_SEV_ALM, "min 1,207 B is under the 2 KiB reference");
    CHECK(ui_diag_heap_block_sev(&h) == UI_DIAG_SEV_CAU, "largest 6,144 B is under the 7 KiB reference");
    h.heap_min = 9830; h.heap_largest = 18432;
    CHECK(ui_diag_heap_min_sev(&h) == UI_DIAG_SEV_NONE && ui_diag_heap_block_sev(&h) == UI_DIAG_SEV_NONE,
          "above references: no verdict (NONE, never NOM)");

    printf("D4) sensor freshness from timestamps\n");
    uint64_t age;
    const int64_t T = 3 * 100 * 1000; /* accel: 3 x 100 ms */
    CHECK(ui_diag_freshness(false, false, 0, 5000000, T, &age) == UI_DIAG_SRC_UNKNOWN, "not probed -> UNKNOWN");
    CHECK(ui_diag_freshness(true, false, 0, 5000000, T, &age) == UI_DIAG_SRC_FAILED, "probe failed -> FAILED (not absent)");
    CHECK(ui_diag_freshness(true, true, 0, 5000000, T, &age) == UI_DIAG_SRC_WAITING, "no sample yet -> WAITING");
    CHECK(ui_diag_freshness(true, true, 4900000, 5000000, T, &age) == UI_DIAG_SRC_LIVE && age == 100, "100 ms -> LIVE");
    CHECK(ui_diag_freshness(true, true, 4700000, 5000000, T, &age) == UI_DIAG_SRC_LIVE, "exactly 300 ms -> LIVE");
    CHECK(ui_diag_freshness(true, true, 4699999, 5000000, T, &age) == UI_DIAG_SRC_STALE, "300 ms + 1 us -> STALE");
    CHECK(ui_diag_freshness(true, true, 5000100, 5000000, T, &age) == UI_DIAG_SRC_LIVE && age == 0,
          "sample stamped after 'now' -> LIVE, age 0");
    CHECK(ui_diag_freshness(true, true, 1, INT64_MAX, 3000000, &age) == UI_DIAG_SRC_STALE, "huge age: no overflow");

    printf("D5) truthful words\n");
    ui_diag_view_t u = { .wifi = UI_DIAG_WIFI_ONLINE };
    l = ui_diag_api_line(&u);
    CHECK(strcmp(l.word, "WAITING") == 0 && l.sev == UI_DIAG_SEV_NONE, "backend not probed: WAITING, not OK");
    u.api = UI_DIAG_API_NO_NET;
    l = ui_diag_api_line(&u);
    CHECK(strcmp(l.word, "NO NET") == 0 && l.sev == UI_DIAG_SEV_NONE, "no Wi-Fi: backend NO NET");
    u.api = UI_DIAG_API_OK;
    l = ui_diag_api_line(&u);
    CHECK(strcmp(l.word, "OK") == 0 && l.detail[0] == 0, "backend OK carries no stale latency");
    u.wifi = UI_DIAG_WIFI_OFF; /* backend snapshot still says OK */
    l = ui_diag_api_line(&u);
    CHECK(strcmp(l.word, "NO NET") == 0 && l.sev == UI_DIAG_SEV_NONE,
          "Wi-Fi loss overrides a cached backend OK");
    u.wifi = UI_DIAG_WIFI_ONLINE;
    l = ui_diag_wifi_line(&u);
    EQ(l.detail, "RSSI --");
    u.wifi = UI_DIAG_WIFI_RETRY; u.wifi_reason = "XXXXXXXXXXXXXXXXXXX"; u.wifi_retry_s = 100000;
    l = ui_diag_wifi_line(&u);
    CHECK(strcmp(l.word, "RETRY 999s") == 0 && strlen(l.detail) == 19, "retry: capped countdown, 19-char cause kept whole");
    u.wifi_reason = NULL;
    l = ui_diag_wifi_line(&u);
    CHECK(l.detail[0] == 0, "retry with NULL reason: empty detail");
    l = ui_diag_src_line(UI_DIAG_SRC_FAILED, 0, true);
    CHECK(strcmp(l.word, "ERROR") == 0 && l.sev == UI_DIAG_SEV_ALM, "onboard IMU init fail: ERROR, alarm");
    l = ui_diag_src_line(UI_DIAG_SRC_FAILED, 0, false);
    CHECK(strcmp(l.word, "ERROR") == 0 && l.sev == UI_DIAG_SEV_CAU, "dock AHT30 init fail: ERROR, caution");
    l = ui_diag_src_line(UI_DIAG_SRC_STALE, 42000, false);
    CHECK(strcmp(l.word, "STALE") == 0 && strcmp(l.detail, "42 s") == 0, "stale shows its age");

    printf(fails ? "DIAG FORMAT FAILED (%d)\n" : "DIAG FORMAT PASS\n", fails);
    return fails ? 1 : 0;
}
