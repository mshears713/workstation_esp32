/*
 * SPDX-FileCopyrightText: 2021-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <string.h>
#include "device_config.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "device_config";

/* Our own namespace. The Mission 07 "Black Box" (boot_count/last_fault/
 * last_cmd) lives in its own namespace in the same nvs partition and is
 * untouched by anything here - that partition keeps its original offset and
 * size across this migration precisely so that data survives. */
#define CFG_NAMESPACE   "wscfg"
#define KEY_BACKEND_URL "backend_url"
#define KEY_OTA_URL     "ota_url"

static char s_backend_url[DEVICE_CONFIG_URL_MAX] = DEVICE_CONFIG_DEFAULT_BACKEND_URL;
static char s_ota_url[DEVICE_CONFIG_URL_MAX]     = DEVICE_CONFIG_DEFAULT_OTA_URL;
static bool s_backend_from_nvs = false;
static bool s_ready            = false;

static bool url_is_sane(const char *url)
{
    if (url == NULL) {
        return false;
    }
    size_t len = strlen(url);
    if (len == 0 || len >= DEVICE_CONFIG_URL_MAX) {
        return false;
    }
    /* Deliberately shallow. We are guarding against an empty or truncated
     * string that would produce a nonsense request, not trying to be a URL
     * parser - esp_http_client will reject anything genuinely malformed and
     * say so in the log. */
    if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) {
        return false;
    }
    /* A trailing slash would produce "http://host:8000//api/v1/notes" once a
     * path is appended. Harmless on most servers, confusing in every log. */
    if (url[len - 1] == '/') {
        return false;
    }
    return true;
}

/* Reads one key into dst, leaving dst untouched if the key is absent or the
 * stored value fails validation. Returns true if dst was replaced. */
static bool load_key(nvs_handle_t handle, const char *key, char *dst, size_t dst_size)
{
    char buf[DEVICE_CONFIG_URL_MAX];
    size_t len = sizeof(buf);

    esp_err_t err = nvs_get_str(handle, key, buf, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return false;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "%s: nvs_get_str failed (%s), keeping default", key, esp_err_to_name(err));
        return false;
    }
    if (!url_is_sane(buf)) {
        ESP_LOGW(TAG, "%s: stored value '%s' is not a usable URL, keeping default", key, buf);
        return false;
    }
    strlcpy(dst, buf, dst_size);
    return true;
}

esp_err_t device_config_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(CFG_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        /* First boot after this firmware, or after an nvs erase. Not a
         * failure: the compiled defaults are the whole point of having
         * compiled defaults. */
        ESP_LOGI(TAG, "no stored config yet; using compiled defaults");
        s_ready = true;
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s - using compiled defaults", esp_err_to_name(err));
        s_ready = true;
        return err;
    } else {
        s_backend_from_nvs = load_key(handle, KEY_BACKEND_URL, s_backend_url, sizeof(s_backend_url));
        (void)load_key(handle, KEY_OTA_URL, s_ota_url, sizeof(s_ota_url));
        nvs_close(handle);
        s_ready = true;
    }

    ESP_LOGI(TAG, "backend = %s (%s)", s_backend_url, s_backend_from_nvs ? "from NVS" : "compiled default");
    ESP_LOGI(TAG, "ota     = %s", s_ota_url);
    return ESP_OK;
}

const char *device_config_backend_base_url(void)
{
    return s_backend_url;
}

const char *device_config_ota_base_url(void)
{
    return s_ota_url;
}

bool device_config_is_ready(void)
{
    return s_ready;
}

bool device_config_backend_from_nvs(void)
{
    return s_backend_from_nvs;
}

static esp_err_t store_url(const char *key, const char *url)
{
    if (!url_is_sane(url)) {
        ESP_LOGE(TAG, "%s: refusing to store '%s'", key, url ? url : "(null)");
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(CFG_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open rw failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_str(handle, key, url);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err == ESP_OK) {
        /* Stored, not applied. See the header for why the in-memory copy is
         * left alone until the next boot. */
        ESP_LOGI(TAG, "%s stored as '%s'; effective after reboot", key, url);
    } else {
        ESP_LOGE(TAG, "%s store failed: %s", key, esp_err_to_name(err));
    }
    return err;
}

esp_err_t device_config_set_backend_base_url(const char *url)
{
    return store_url(KEY_BACKEND_URL, url);
}

esp_err_t device_config_set_ota_base_url(const char *url)
{
    return store_url(KEY_OTA_URL, url);
}
