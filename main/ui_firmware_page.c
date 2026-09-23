/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include "ui_firmware_page.h"
#include "ui_kit.h"

void ui_firmware_page_build(ui_firmware_page_t *w, lv_obj_t *page, lv_event_cb_t check_cb)
{
    ui_page_title(page, "FIRMWARE");

    ui_page_panel(page, 58, 70);    /* identity: version, build, backend */
    ui_page_panel(page, 134, 40);   /* which slot, and is it confirmed   */
    ui_page_panel(page, 180, 24);   /* what the updater is doing         */

    w->version = lv_label_create(page);
    lv_obj_set_style_text_font(w->version, &lv_font_montserrat_20, 0);
    lv_obj_align(w->version, LV_ALIGN_TOP_LEFT, 14, 64);
    lv_label_set_text(w->version, "...");

    w->build = lv_label_create(page);
    lv_obj_set_style_text_font(w->build, &lv_font_montserrat_14, 0);
    lv_obj_align(w->build, LV_ALIGN_TOP_LEFT, 14, 94);
    lv_label_set_text(w->build, "");

    w->backend = lv_label_create(page);
    lv_obj_set_style_text_font(w->backend, &lv_font_montserrat_14, 0);
    lv_obj_align(w->backend, LV_ALIGN_TOP_LEFT, 14, 110);
    lv_label_set_text(w->backend, "");

    w->slot = lv_label_create(page);
    lv_obj_align(w->slot, LV_ALIGN_TOP_LEFT, 14, 146);
    lv_label_set_text(w->slot, "");

    w->ota = lv_label_create(page);
    lv_obj_set_style_text_font(w->ota, &lv_font_montserrat_14, 0);
    lv_obj_align(w->ota, LV_ALIGN_TOP_LEFT, 14, 184);
    lv_label_set_text(w->ota, "");

    lv_obj_t *check_btn = lv_btn_create(page);
    lv_obj_set_size(check_btn, 304, 28);
    lv_obj_align(check_btn, LV_ALIGN_TOP_LEFT, 8, 208);
    if (check_cb) {
        lv_obj_add_event_cb(check_btn, check_cb, LV_EVENT_CLICKED, NULL);
    }
    lv_obj_t *check_lbl = lv_label_create(check_btn);
    lv_label_set_text(check_lbl, "CHECK CLAWBOX NOW");
    lv_obj_center(check_lbl);
}

void ui_firmware_page_apply(const ui_firmware_page_t *w, const ui_firmware_view_t *v)
{
    if (w->version == NULL) {
        return;   /* page not built yet */
    }

    char buf[96];
    lv_label_set_text(w->version, v->version);

    snprintf(buf, sizeof(buf), "built %s", v->built_at);
    lv_label_set_text(w->build, buf);

    /* Which backend this device is actually talking to. Worth a line of its
     * own: it is now a runtime value, so "what is it configured for" is a
     * real question with a real answer rather than something you read off
     * the source you think you flashed. */
    snprintf(buf, sizeof(buf), "api %s%s", v->backend_url, v->backend_fallback ? " (fb)" : "");
    lv_label_set_text(w->backend, buf);

    snprintf(buf, sizeof(buf), "SLOT %s   %s", v->slot, v->pending_verify ? "ON PROBATION" : "CONFIRMED");
    lv_label_set_text(w->slot, buf);

    if (v->ota_downloading) {
        snprintf(buf, sizeof(buf), "OTA: downloading %d%%", v->ota_progress_pct);
    } else if (v->ota_message && v->ota_message[0] != '\0') {
        snprintf(buf, sizeof(buf), "OTA: %s", v->ota_message);
    } else {
        snprintf(buf, sizeof(buf), "OTA: %s", v->ota_state);
    }
    lv_label_set_text(w->ota, buf);
}
