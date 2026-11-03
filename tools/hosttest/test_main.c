/* Host test for the report builder and the 1.2.2 loop guard. See run.sh. */
#include "host_stubs.h"
#include "diag.h"
#include "report_client.h"

extern esp_reset_reason_t g_reset;
extern int g_posts, g_post_len, g_post_json_ok;
void host_breadcrumb_worst(void);
bool host_guard_set(void);
int host_send(const char *trigger);

static int fails;
#define CHECK(c, what) do { printf("  %s  %s\n", (c) ? "PASS" : "FAIL", what); if (!(c)) fails++; } while (0)

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("1) boot after a PANIC, worst-case breadcrumb + 32 worst-case events, after-crash report\n");
    g_reset = ESP_RST_PANIC;
    host_breadcrumb_worst();
    diag_init();
    CHECK(diag_crash()->breadcrumb_valid, "breadcrumb read back as valid");
    int st = host_send("after-crash");
    CHECK(st == REPORT_SENT, "report built and sent (no abort, no overflow)");
    CHECK(g_post_json_ok, "body parses as JSON");
    printf("  body %d bytes of %d\n", g_post_len, 20 * 1024);
    CHECK(!diag_after_crash_pending(), "after-crash marked delivered");
#ifndef HOST_1_2_1
    CHECK(!host_guard_set(), "loop guard cleared after the attempt");

    printf("2) guard left set, then a normal SW restart (e.g. OTA reboot): reports stay on\n");
    diag_report_guard_begin("heartbeat");
    g_reset = ESP_RST_SW;
    diag_init();
    CHECK(!diag_auto_reports_off(), "auto reports on");
    CHECK(!host_guard_set(), "guard cleared at boot");

    printf("3) PANIC with no report in flight: reports stay on\n");
    g_reset = ESP_RST_PANIC;
    diag_init();
    CHECK(!diag_auto_reports_off(), "auto reports on");
    CHECK(diag_counters()->report_crashes == 0, "report_crashes 0");

    printf("4) PANIC while an after-crash report was in flight: auto reports OFF this boot\n");
    diag_report_guard_begin("after-crash");
    g_reset = ESP_RST_PANIC;
    diag_init();
    CHECK(diag_auto_reports_off(), "auto reports off");
    CHECK(diag_counters()->report_crashes == 1, "report_crashes counted (NVS)");
    CHECK(strcmp(diag_report_crash_trigger(), "after-crash") == 0, "crashed trigger remembered");
    st = host_send("button");
    CHECK(st == REPORT_SENT && g_post_json_ok, "RPT still sends");
#endif
    printf(fails ? "FAILED (%d)\n" : "ALL PASS\n", fails);
    return fails ? 1 : 0;
}
