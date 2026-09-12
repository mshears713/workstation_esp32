/*
 * SPDX-FileCopyrightText: 2021-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <string.h>
#include "firmware_identity.h"
#include "esp_app_desc.h"
#include "esp_log.h"

static const char *TAG = "fw_id";

static char s_git_sha[24] = "";
static char s_summary[96] = "";
static char s_built_at[40] = "";

static const esp_app_desc_t *desc(void)
{
    /* Never NULL for a normally linked application image. */
    return esp_app_get_description();
}

const char *firmware_version(void)
{
    return desc()->version;
}

const char *firmware_git_sha(void)
{
    if (s_git_sha[0] != '\0') {
        return s_git_sha;
    }
    const char *v = desc()->version;
    const char *plus = strchr(v, '+');
    if (plus != NULL && plus[1] != '\0') {
        strlcpy(s_git_sha, plus + 1, sizeof(s_git_sha));
    } else {
        strlcpy(s_git_sha, "unknown", sizeof(s_git_sha));
    }
    return s_git_sha;
}

const char *firmware_built_at(void)
{
    if (s_built_at[0] == '\0') {
        /* esp_app_desc_t keeps date and time as two fixed-width fields. */
        snprintf(s_built_at, sizeof(s_built_at), "%s %s", desc()->date, desc()->time);
    }
    return s_built_at;
}

const char *firmware_idf_version(void)
{
    return desc()->idf_ver;
}

const char *firmware_running_partition_label(void)
{
    const esp_partition_t *p = esp_ota_get_running_partition();
    return p ? p->label : "unknown";
}

const char *firmware_next_ota_partition_label(void)
{
    const esp_partition_t *p = esp_ota_get_next_update_partition(NULL);
    return p ? p->label : "none";
}

esp_ota_img_states_t firmware_ota_state(void)
{
    const esp_partition_t *p = esp_ota_get_running_partition();
    if (p == NULL) {
        return ESP_OTA_IMG_VALID;
    }
    esp_ota_img_states_t state = ESP_OTA_IMG_VALID;
    if (esp_ota_get_state_partition(p, &state) != ESP_OK) {
        /* A factory image, or a slot with no otadata entry yet. Treating
         * that as "valid" is correct: there is nothing to roll back to and
         * nothing on probation. */
        return ESP_OTA_IMG_VALID;
    }
    return state;
}

bool firmware_is_pending_verify(void)
{
    return firmware_ota_state() == ESP_OTA_IMG_PENDING_VERIFY;
}

static const char *state_word(esp_ota_img_states_t s)
{
    switch (s) {
    case ESP_OTA_IMG_NEW:            return "new";
    case ESP_OTA_IMG_PENDING_VERIFY: return "pending-verify";
    case ESP_OTA_IMG_VALID:          return "valid";
    case ESP_OTA_IMG_INVALID:        return "invalid";
    case ESP_OTA_IMG_ABORTED:        return "aborted";
    default:                         return "undefined";
    }
}

const char *firmware_identity_summary(void)
{
    if (s_summary[0] == '\0') {
        snprintf(s_summary, sizeof(s_summary), "%s @ %s (%s)",
                 firmware_version(),
                 firmware_running_partition_label(),
                 state_word(firmware_ota_state()));
    }
    return s_summary;
}

/* Called once at boot from app_main() so the serial log always opens with
 * the single most useful line for anyone debugging a deployment: which
 * build this is, where it is running from, and whether it is on probation. */
void firmware_identity_log(void)
{
    ESP_LOGI(TAG, "version   %s", firmware_version());
    ESP_LOGI(TAG, "git       %s", firmware_git_sha());
    ESP_LOGI(TAG, "built     %s", firmware_built_at());
    ESP_LOGI(TAG, "idf       %s", firmware_idf_version());
    ESP_LOGI(TAG, "running   %s (%s)", firmware_running_partition_label(),
             state_word(firmware_ota_state()));
    ESP_LOGI(TAG, "next slot %s", firmware_next_ota_partition_label());
}
