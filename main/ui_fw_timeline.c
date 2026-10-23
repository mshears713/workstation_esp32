/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief FIRMWARE timeline model - see ui_fw_timeline.h.
 */

#include "ui_fw_timeline.h"

#include <stdio.h>
#include <string.h>

const char *ui_fw_stage_name(ui_fw_stage_t s)
{
    static const char *const n[UI_FW_STAGES] = { "CHECK", "GET", "VERIFY", "BOOT", "HEALTH", "KEPT" };
    return s < UI_FW_STAGES ? n[s] : "?";
}

/* Stages before `cur` done, `cur` in `st`, the rest pending. */
static void upto(ui_fw_timeline_t *t, ui_fw_stage_t cur, ui_fw_stage_state_t st)
{
    for (int i = 0; i < UI_FW_STAGES; i++) {
        t->stage[i] = i < (int)cur ? UI_FW_ST_DONE : i == (int)cur ? st : UI_FW_ST_PENDING;
    }
}

static bool starts(const char *m, const char *p)
{
    return m && strncmp(m, p, strlen(p)) == 0;
}

void ui_fw_timeline(ui_fw_state_t state, const char *message, const char *desired, bool pending_verify,
                    bool rolled_back, ui_fw_timeline_t *t)
{
    memset(t, 0, sizeof(*t));
    const char *m = message ? message : "";
    const char *want = desired && desired[0] ? desired : NULL;
    snprintf(t->detail, sizeof(t->detail), "%s", m);
    t->in_flight = true;

    switch (state) {
    case UI_FW_CHECKING:
        upto(t, UI_FW_CHECK, UI_FW_ST_CURRENT);
        snprintf(t->headline, sizeof(t->headline), "Asking CLAWBOX");
        snprintf(t->detail, sizeof(t->detail), "fetching the desired-version manifest");
        break;
    case UI_FW_UPDATE_READY:
        upto(t, UI_FW_DOWNLOAD, UI_FW_ST_CURRENT);
        snprintf(t->headline, sizeof(t->headline), "Update to %s", want ? want : "a new version");
        break;
    case UI_FW_DOWNLOADING:
        upto(t, UI_FW_DOWNLOAD, UI_FW_ST_CURRENT);
        snprintf(t->headline, sizeof(t->headline), "Downloading");
        t->show_percent = true;
        break;
    case UI_FW_VERIFYING:
        upto(t, UI_FW_VERIFY, UI_FW_ST_CURRENT);
        snprintf(t->headline, sizeof(t->headline), "Checking the hash");
        break;
    case UI_FW_REBOOTING:
        upto(t, UI_FW_RESTART, UI_FW_ST_CURRENT);
        snprintf(t->headline, sizeof(t->headline), "Restarting");
        break;
    case UI_FW_VALIDATING:
        /* Booted into the new image: the first four stages happened (this
         * image could not be running otherwise). */
        upto(t, UI_FW_HEALTH, UI_FW_ST_CURRENT);
        snprintf(t->headline, sizeof(t->headline), "On probation");
        snprintf(t->detail, sizeof(t->detail), "must reach Wi-Fi and the backend within 90 s, or it rolls back");
        break;
    case UI_FW_FAILED:
        t->failed = true;
        t->failed_stage_known = true;
        /* ota_service.c's messages, in the order the stages run */
        if (starts(m, "image ") || starts(m, "cannot fetch image") || starts(m, "out of memory")) {
            upto(t, UI_FW_DOWNLOAD, UI_FW_ST_FAILED);
            snprintf(t->headline, sizeof(t->headline), "Download failed");
        } else if (starts(m, "hash mismatch")) {
            upto(t, UI_FW_VERIFY, UI_FW_ST_FAILED);
            snprintf(t->headline, sizeof(t->headline), "Hash mismatch");
        } else if (starts(m, "unhealthy")) {
            upto(t, UI_FW_HEALTH, UI_FW_ST_FAILED);
            snprintf(t->headline, sizeof(t->headline), "Health gate failed");
        } else {
            t->failed_stage_known = false;
            upto(t, UI_FW_CHECK, UI_FW_ST_PENDING);
            t->stage[UI_FW_CHECK] = UI_FW_ST_PENDING;
            snprintf(t->headline, sizeof(t->headline), "Update failed");
        }
        break;
    case UI_FW_IDLE:
    default:
        if (pending_verify) {
            /* IDLE while still pending verify should not happen; say so
             * rather than calling the image kept. */
            upto(t, UI_FW_HEALTH, UI_FW_ST_CURRENT);
            snprintf(t->headline, sizeof(t->headline), "On probation");
        } else if (starts(m, "image marked valid")) {
            /* this boot was an update, and it passed */
            upto(t, UI_FW_STAGES, UI_FW_ST_DONE);
            snprintf(t->headline, sizeof(t->headline), "Update kept");
            snprintf(t->detail, sizeof(t->detail), "passed the health gate this boot");
        } else {
            t->in_flight = false;
            upto(t, UI_FW_CHECK, UI_FW_ST_PENDING);
            t->stage[UI_FW_CHECK] = UI_FW_ST_PENDING;
            if (starts(m, "up to date")) {
                snprintf(t->headline, sizeof(t->headline), "Up to date");
                snprintf(t->detail, sizeof(t->detail), "CLAWBOX wants this version; checks every 60 s");
            } else if (starts(m, "no manifest")) {
                snprintf(t->headline, sizeof(t->headline), "No answer from CLAWBOX");
                snprintf(t->detail, sizeof(t->detail), "could not read the desired version; tries again in 60 s");
            } else {
                snprintf(t->headline, sizeof(t->headline), "No update in progress");
            }
        }
        break;
    }
    if (rolled_back && state != UI_FW_FAILED) {
        /* the slot this boot came from failed; worth saying even when idle */
        snprintf(t->detail, sizeof(t->detail), "previous image failed its health gate and was rolled back");
    }
}
