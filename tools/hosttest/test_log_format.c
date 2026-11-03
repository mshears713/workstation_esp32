/* Host test for the LOG page model (main/ui_log_format.c): time stamps stay
 * five characters, type tags, and which real firmware messages read as faults.
 * See run.sh. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "ui_log_format.h"

static int fails;
#define CHECK(c, what) do { printf("  %s  %s\n", (c) ? "PASS" : "FAIL", what); if (!(c)) fails++; } while (0)
#define EQ(got, want) do { CHECK(strcmp((got), (want)) == 0, want); \
    if (strcmp((got), (want)) != 0) printf("        got \"%s\"\n", (got)); } while (0)

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    char b[16];

    printf("L1) time since boot, never wider than 5 characters\n");
    ui_log_fmt_time(0, b, sizeof b); EQ(b, "0:00");
    ui_log_fmt_time(7, b, sizeof b); EQ(b, "0:07");
    ui_log_fmt_time(724, b, sizeof b); EQ(b, "12:04");
    ui_log_fmt_time(3599, b, sizeof b); EQ(b, "59:59");
    ui_log_fmt_time(3600, b, sizeof b); EQ(b, "1h00");
    ui_log_fmt_time(86399, b, sizeof b); EQ(b, "23h59");
    ui_log_fmt_time(51 * 86400 + 7 * 3600, b, sizeof b); EQ(b, "51d07");
    ui_log_fmt_time(412u * 86400, b, sizeof b); EQ(b, "412d");
    ui_log_fmt_time(4294967295u, b, sizeof b); EQ(b, ">999d");

    printf("L2) tags\n");
    EQ(ui_log_tag(UI_LOG_NETWORK), "NET");
    EQ(ui_log_tag(UI_LOG_BACKEND), "API");
    EQ(ui_log_tag(99), "???");
    EQ(ui_log_tag(-1), "???");

    printf("L3) faults: real firmware lines\n");
    const char *bad[] = { "UPLOAD FAILED: UPLOAD TIMEOUT", "API LOST", "SOURCE STALE", "HUMITURE INIT FAILED",
                          "WIFI LOST, RETRY 25s", "REPORT FAIL: HTTP 500", "REPORT BUSY", "WDT NO API 600s",
                          "AUTO RPT OFF: CRASH IN REPORT", "COMMAND TIMEOUT", "GO START FAILED" };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        CHECK(ui_log_is_fault(UI_LOG_SYSTEM, bad[i]), bad[i]);
    }
    const char *ok[] = { "WIFI ONLINE", "API: OK", "API BACK AFTER 412s", "REPORT SENT #14", "FIRST DATA",
                         "SOURCE RECOVERED", "NOTIFICATION DELIVERED", "CMD CLR LOG", "LISTENING RESTORED" };
    for (size_t i = 0; i < sizeof ok / sizeof ok[0]; i++) {
        CHECK(!ui_log_is_fault(UI_LOG_SYSTEM, ok[i]), ok[i]);
    }
    CHECK(!ui_log_is_fault(UI_LOG_RESET, "RST POWERON"), "RST POWERON is a normal start");
    CHECK(!ui_log_is_fault(UI_LOG_RESET, "RST SW RESTART"), "RST SW RESTART (update) is normal");
    CHECK(ui_log_is_fault(UI_LOG_RESET, "RST TASK WATCHDOG"), "RST TASK WATCHDOG is a fault");
    CHECK(ui_log_is_fault(UI_LOG_RESET, "PREV: PANIC (no crumb)"), "PREV: PANIC is a fault");
    CHECK(!ui_log_is_fault(UI_LOG_SYSTEM, NULL), "NULL message: not a fault, no crash");

    printf(fails ? "LOG FORMAT FAIL (%d)\n" : "LOG FORMAT PASS\n", fails);
    return fails ? 1 : 0;
}
