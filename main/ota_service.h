/*
 * SPDX-FileCopyrightText: 2021-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Wireless firmware updates from CLAWBOX, and the rollback gate.
 * @details Two jobs live here, and they are related closely enough that
 *          splitting them would mean splitting one state machine:
 *
 *          1. INSTALLING a new image. The device asks CLAWBOX what version
 *             it is supposed to be running, and if that is not what it is
 *             running, it downloads that image into the inactive OTA slot,
 *             checks it byte-for-byte against the hash CLAWBOX published,
 *             switches the boot slot and reboots.
 *
 *          2. DECIDING whether the image that just booted deserves to stay.
 *             With CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE, a freshly
 *             installed image boots in PENDING_VERIFY and is on probation:
 *             if the device resets before the app calls
 *             esp_ota_mark_app_valid_cancel_rollback(), the bootloader goes
 *             back to the previous slot. This module runs a bounded health
 *             check and makes that call - or gives up and rolls back on
 *             purpose. See ota_service.c for what "healthy" means.
 *
 *          Deliberately NOT here: writing to arbitrary flash. The only
 *          writes this module makes are through esp_ota_write() into the
 *          slot ESP-IDF nominates. There is no API to point it at an
 *          address, and there should not be one - the control surface an
 *          agent on CLAWBOX gets is "install the image at this URL whose
 *          hash is this", nothing wider.
 *
 *          BUILD is not DEPLOY. This module only ever acts on the version
 *          CLAWBOX names in its "desired" manifest. Publishing a new build
 *          on CLAWBOX does nothing to the device until somebody sets that
 *          build as desired.
 *
 *          Transport is HTTPS with CLAWBOX's own certificate compiled in
 *          (main/certs/clawbox_ota_ca.pem) - not a public CA, not the
 *          bundle, and not disabled. Combined with the SHA-256 check, the
 *          device will only install an image that came from the machine
 *          holding that private key AND matches the published hash.
 *          What this does NOT give: the image itself is unsigned, so
 *          anything that can write to CLAWBOX's artifact directory can
 *          publish firmware this device will accept. Signed app images
 *          (CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT) are the next seam and
 *          are documented in doc/OTA.md; they were left out of this
 *          migration because they change bootloader behaviour.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OTA_STATE_IDLE = 0,       /* nothing happening; running image is fine   */
    OTA_STATE_VALIDATING,     /* on probation, health gate running          */
    OTA_STATE_CHECKING,       /* fetching the desired-version manifest      */
    OTA_STATE_UPDATE_READY,   /* a different version is desired             */
    OTA_STATE_DOWNLOADING,    /* writing the inactive slot                  */
    OTA_STATE_VERIFYING,      /* hashing what was written                   */
    OTA_STATE_REBOOTING,      /* verified; about to restart into the new slot */
    OTA_STATE_FAILED,         /* see message; running image is untouched    */
} ota_state_t;

typedef struct {
    ota_state_t state;
    int         progress_pct;        /* 0-100 while DOWNLOADING, else 0     */
    char        message[80];         /* short human-readable last event     */
    char        desired_version[40]; /* "" until a manifest has been read   */
    uint32_t    checks;              /* manifest fetches attempted          */
    uint32_t    failures;            /* update attempts that did not install */
    bool        rolled_back;         /* this boot came after a failed image */
} ota_status_t;

/**
 * @brief Start the OTA task.
 * @details Call once, after nvs_flash_init(), device_config_init(),
 *          wifi_mgr_init() and backend_health_init(). Returns as soon as the
 *          task exists; all network work happens on that task, never on the
 *          caller and never on the LVGL task.
 */
esp_err_t ota_service_start(void);

/** Thread-safe snapshot for rendering or logging. */
void ota_service_get_status(ota_status_t *out);

/** Ask for an out-of-band manifest check now. Non-blocking. */
void ota_service_request_check(void);

/**
 * @brief Install the desired version now, if it differs from the running one.
 * @details Non-blocking; the work happens on the OTA task. Ignored if an
 *          update is already in progress.
 */
void ota_service_request_update(void);

/**
 * @brief Tell the OTA service the display and LVGL are up.
 * @details Called once by app_main after the UI is on screen. It is one of
 *          the conditions the rollback health gate waits for, and the only
 *          one this module cannot observe for itself.
 */
void ota_service_report_ui_ready(void);

/** Short label for a status row, e.g. "OTA: idle". Never NULL. */
const char *ota_service_state_label(ota_state_t state);

#ifdef __cplusplus
}
#endif
