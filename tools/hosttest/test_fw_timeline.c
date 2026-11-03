/* Host test for the FIRMWARE timeline model (main/ui_fw_timeline.c): every
 * ota_state, failures placed on the right stage by ota_service.c's own
 * messages, nothing marked done or failed that was not. run.sh also checks
 * that those messages still exist in ota_service.c. */
#include <stdio.h>
#include <string.h>
#include "ui_fw_timeline.h"

static int fails;
#define CHECK(c, what) do { printf("  %s  %s\n", (c) ? "PASS" : "FAIL", what); if (!(c)) fails++; } while (0)

static const char *pattern(const ui_fw_timeline_t *t)
{
    static char p[UI_FW_STAGES + 1];
    for (int i = 0; i < UI_FW_STAGES; i++) {
        p[i] = "-*+x"[t->stage[i]];   /* pending, current, done, failed */
    }
    p[UI_FW_STAGES] = '\0';
    return p;
}

#define STAGES(state, msg, pv, want, what) do { ui_fw_timeline_t t; \
    ui_fw_timeline(state, msg, "1.2.3+9f3c1aa", pv, false, &t); \
    CHECK(strcmp(pattern(&t), want) == 0, what); \
    if (strcmp(pattern(&t), want)) printf("        got %s want %s\n", pattern(&t), want); } while (0)

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("T1) each state lights the stages it has really reached\n");
    STAGES(UI_FW_IDLE, "up to date (1.2.2+2d189af)", false, "------", "idle up to date: nothing lit");
    STAGES(UI_FW_CHECKING, "", false, "*-----", "checking");
    STAGES(UI_FW_UPDATE_READY, "desired 1.2.3+9f3c1aa", false, "+*----", "update ready");
    STAGES(UI_FW_DOWNLOADING, "1.2.3 -> ota_0", false, "+*----", "downloading");
    STAGES(UI_FW_VERIFYING, "checking hash", false, "++*---", "verifying");
    STAGES(UI_FW_REBOOTING, "installed 1.2.3, rebooting", false, "+++*--", "rebooting");
    STAGES(UI_FW_VALIDATING, "new image on probation, validating", true, "++++*-", "probation");
    STAGES(UI_FW_IDLE, "image marked valid", false, "++++++", "kept after the gate");

    printf("T2) failures land on the stage that failed\n");
    STAGES(UI_FW_FAILED, "cannot fetch image", false, "+x----", "fetch -> GET");
    STAGES(UI_FW_FAILED, "image 5767168 > slot 5636096", false, "+x----", "too big -> GET");
    STAGES(UI_FW_FAILED, "out of memory", false, "+x----", "no memory -> GET");
    STAGES(UI_FW_FAILED, "hash mismatch, not installing", false, "++x---", "hash -> VERIFY");
    STAGES(UI_FW_FAILED, "unhealthy (wifi not online), rolling back", true, "++++x-", "gate -> HEALTH");
    STAGES(UI_FW_FAILED, "a message nobody wrote yet", false, "------", "unknown failure: no stage blamed");

    ui_fw_timeline_t t;
    ui_fw_timeline(UI_FW_DOWNLOADING, "x", "", false, false, &t);
    CHECK(t.show_percent, "download shows the measured percent");
    ui_fw_timeline(UI_FW_VALIDATING, "x", "", true, false, &t);
    CHECK(!t.show_percent && strstr(t.detail, "90 s") != NULL, "health gate: limit stated, no percent");
    ui_fw_timeline(UI_FW_IDLE, "running 1.2.2", "", false, true, &t);
    CHECK(strstr(t.detail, "rolled back") != NULL, "rolled back boot is said");
    ui_fw_timeline(UI_FW_IDLE, NULL, NULL, false, false, &t);
    CHECK(t.headline[0] != '\0', "NULL message/desired: no crash");

    printf(fails ? "FW TIMELINE FAIL (%d)\n" : "FW TIMELINE PASS\n", fails);
    return fails ? 1 : 0;
}
