/*
 * SPDX-FileCopyrightText: 2021-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief WORKSTATION - boot order for the status deck.
 * @details Started life as Operation Homebound Mission 05 (the Status Deck,
 *          built on the Mission 04 command console). What it does now is
 *          mostly elsewhere; this file exists to get the pieces started in
 *          an order that works, and the order is the interesting part:
 *
 *            1. NVS, because everything persistent needs it.
 *            2. device_config, because it reads NVS and because every
 *               backend client resolves BACKEND_BASE_URL through it - a
 *               client that ran first would ask for the address before
 *               anyone had loaded it.
 *            3. the identity block into the log, so a serial capture taken
 *               during a failed deployment already says which build it is.
 *            4. the display and the UI. status_deck_ui() is where Wi-Fi,
 *               backend health, the project catalog and the voice pipeline
 *               are started.
 *            5. the OTA service, last, because its rollback health gate
 *               watches the things steps 2-4 brought up and would otherwise
 *               judge a perfectly good image by whether it had finished
 *               booting.
 */

#include <stdio.h>
#include "bsp/esp-bsp.h"
#include "lvgl.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "device_config.h"
#include "firmware_identity.h"
#include "ota_service.h"

extern void status_deck_ui(lv_obj_t *scr);

static void nvs_init_or_erase(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void app_main(void)
{
    nvs_init_or_erase();

    /* Where CLAWBOX is, read from NVS with a compiled fallback. Must precede
     * any backend client. */
    device_config_init();

    /* One block, every boot. During a wireless deployment this is the only
     * cheap way to tell "it installed and came back" from "it never left". */
    firmware_identity_log();

    /* Before the display, deliberately. The updater needs 6KB of contiguous
     * internal RAM for its stack, and internal RAM is the scarce resource on
     * this board - once LVGL and the audio pipeline have taken their share
     * the largest free block is around 7.6KB and the allocation can fail
     * outright. It waits for ota_service_report_ui_ready() below before
     * touching anything the UI starts. */
    esp_err_t ota_err = ota_service_start();
    if (ota_err != ESP_OK) {
        ESP_LOGE("workstation", "OTA updater did not start: %s - USB updates only this boot",
                 esp_err_to_name(ota_err));
    }

    bsp_display_start();

    ESP_LOGI("workstation", "status deck starting (%s)", firmware_identity_summary());
    bsp_display_lock(0);
    lv_obj_t *scr = lv_disp_get_scr_act(NULL);
    status_deck_ui(scr);
    bsp_display_unlock();
    bsp_display_backlight_on();

    /* Releases the updater, which has been waiting for exactly this: the UI
     * is up and status_deck_ui() has started Wi-Fi and the backend poller.
     * If this image arrived over the air, its probation clock starts now. */
    ota_service_report_ui_ready();
}
