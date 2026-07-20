/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Operation Homebound Mission 05 — The Status Deck
 * @details Integrated command-and-telemetry deck built on the Mission 04
 *          command console foundation (bsp_display_start / LVGL touch indev).
 */

#include <stdio.h>
#include "bsp/esp-bsp.h"
#include "lvgl.h"
#include "esp_log.h"
#include "nvs_flash.h"

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
    bsp_display_start();

    ESP_LOGI("mission05", "Status deck starting");
    bsp_display_lock(0);
    lv_obj_t *scr = lv_disp_get_scr_act(NULL);
    status_deck_ui(scr);
    bsp_display_unlock();
    bsp_display_backlight_on();
}
