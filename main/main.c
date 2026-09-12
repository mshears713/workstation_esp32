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

    bsp_display_start();

    ESP_LOGI("workstation", "status deck starting (%s)", firmware_identity_summary());
    bsp_display_lock(0);
    lv_obj_t *scr = lv_disp_get_scr_act(NULL);
    status_deck_ui(scr);
    bsp_display_unlock();
    bsp_display_backlight_on();

    /* The UI is on screen and status_deck_ui() has started Wi-Fi and the
     * backend health poller. That is the health gate's definition of "this
     * image got far enough to be worth keeping". */
    ota_service_report_ui_ready();
    ESP_ERROR_CHECK(ota_service_start());
}
