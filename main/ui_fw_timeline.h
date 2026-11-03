/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief FIRMWARE timeline model: which update stage is done, current or
 *        failed, from what ota_service actually reports. Plain C (no LVGL, no
 *        ESP-IDF), shared by firmware, simulator and tools/hosttest.
 * @details The stages are ota_service.c's own sequence:
 *            CHECK     fetch CLAWBOX's desired-version manifest
 *            DOWNLOAD  write the inactive slot (progress_pct is measured bytes)
 *            VERIFY    SHA-256 of what was written against the manifest
 *            RESTART   switch boot slot and reboot
 *            HEALTH    the new image on probation: config, Wi-Fi and backend
 *                      must be OK within 90 s, or it rolls back
 *            KEPT      esp_ota_mark_app_valid: the image stays
 *          ota_status_t has no "stage that failed" field, so a FAILED state is
 *          placed by its message - ota_service.c's own fixed strings, pinned by
 *          tools/hosttest/test_fw_timeline.c. An unrecognised failure message
 *          is shown without pointing at a stage rather than at a guessed one.
 *          Nothing here invents a percentage or a duration: DOWNLOAD's percent
 *          is the updater's measured one, and the health gate shows its limit
 *          (90 s), not a countdown it does not have.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* mirrors ota_state_t */
typedef enum {
    UI_FW_IDLE = 0, UI_FW_VALIDATING, UI_FW_CHECKING, UI_FW_UPDATE_READY, UI_FW_DOWNLOADING,
    UI_FW_VERIFYING, UI_FW_REBOOTING, UI_FW_FAILED,
} ui_fw_state_t;

typedef enum { UI_FW_CHECK = 0, UI_FW_DOWNLOAD, UI_FW_VERIFY, UI_FW_RESTART, UI_FW_HEALTH, UI_FW_KEPT, UI_FW_STAGES } ui_fw_stage_t;

typedef enum {
    UI_FW_ST_PENDING = 0,   /* not reached */
    UI_FW_ST_CURRENT,
    UI_FW_ST_DONE,
    UI_FW_ST_FAILED,
} ui_fw_stage_state_t;

typedef struct {
    ui_fw_stage_state_t stage[UI_FW_STAGES];
    bool in_flight;          /* an update is under way (or just ended): draw the timeline lit */
    char headline[40];       /* "Downloading 1.3.1+9f3c1aa", "Up to date", "Hash mismatch" */
    char detail[64];         /* the updater's message, or what the stage means */
    bool show_percent;       /* DOWNLOAD: progress_pct is real */
    bool failed;
    bool failed_stage_known;
} ui_fw_timeline_t;

/** Short stage names for the timeline: CHECK, GET, VERIFY, BOOT, HEALTH, KEPT. */
const char *ui_fw_stage_name(ui_fw_stage_t s);

/** `message` is ota_status_t.message, `desired` its desired_version ("" if
 *  none), `pending_verify` firmware_is_pending_verify(), `rolled_back`
 *  ota_status_t.rolled_back. */
void ui_fw_timeline(ui_fw_state_t state, const char *message, const char *desired, bool pending_verify,
                    bool rolled_back, ui_fw_timeline_t *out);

#ifdef __cplusplus
}
#endif
