/*
 * SPDX-FileCopyrightText: 2021-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Runtime device configuration held in NVS.
 * @details This replaces the compile-time backend address that used to live
 *          in backend_config.h. The reason is operational, not stylistic:
 *          the backend moved off a laptop (whose DHCP address changed every
 *          few days, and every change meant a USB reflash) onto CLAWBOX, a
 *          persistent server. Where CLAWBOX lives is now a *runtime* fact
 *          the device stores for itself, so moving the server never again
 *          requires rebuilding firmware.
 *
 *          Precedence, highest first:
 *            1. the value stored in NVS (namespace "wscfg"), set by an
 *               operator via device_config_set_*(), and
 *            2. the compiled-in default below.
 *
 *          A value set at runtime takes effect on the NEXT BOOT, not
 *          immediately. That is deliberate. The accessors below hand out a
 *          pointer into a static buffer, and several tasks (remote_client
 *          polls every ~150ms) read it concurrently without locking;
 *          rewriting that buffer underneath a snprintf() would be a genuine
 *          race for the sake of saving a reboot nobody minds.
 *
 *          Scope: these are *addresses*, not secrets. Wi-Fi credentials stay
 *          in main/wifi_credentials.h (git-ignored) and backend API keys stay
 *          on the backend. Nothing here is sensitive, which is why it is a
 *          normal tracked file and why it is safe to log.
 *
 *          The two addresses are kept separate on purpose:
 *            - backend base URL: the application API (notes, entries, voice
 *              inbox, notifications, the Roku remote queue). Plain HTTP on
 *              the LAN, unchanged from before this migration.
 *            - OTA base URL: firmware manifests and images only. HTTPS,
 *              pinned to CLAWBOX's own certificate (see ota_service.c).
 *              Installing code deserves a stronger guarantee than fetching
 *              a notification does.
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

/** Longest URL we will store or hand back, including the NUL. */
#define DEVICE_CONFIG_URL_MAX 128

/* Compiled-in fallbacks, used only when NVS holds nothing.
 *
 * A hostname rather than an IPv4 address: CLAWBOX's DHCP lease can move, its
 * name cannot. Resolution goes through lwIP's mDNS support for ".local"
 * names (CONFIG_LWIP_DNS_SUPPORT_MDNS_QUERIES), so no extra client code is
 * needed - esp_http_client resolves it like any other host. If mDNS is ever
 * unreliable on a given network, set a literal IP into NVS; that is exactly
 * the escape hatch this module exists to provide. */
#define DEVICE_CONFIG_DEFAULT_BACKEND_URL "http://clawbox.local:8000"
#define DEVICE_CONFIG_DEFAULT_OTA_URL     "https://clawbox.local:8443"

/**
 * @brief Load configuration from NVS into memory.
 * @details Call once, early in app_main(), after nvs_flash_init(). Missing
 *          keys are not an error - the compiled defaults are used instead.
 *          Safe to call twice; the second call is a no-op.
 */
esp_err_t device_config_init(void);

/** @return Backend API base URL, no trailing slash. Never NULL. */
const char *device_config_backend_base_url(void);

/** @return Firmware/OTA base URL, no trailing slash. Never NULL. */
const char *device_config_ota_base_url(void);

/**
 * @brief Persist a new backend base URL. Takes effect on next boot.
 * @param url e.g. "http://192.168.1.71:8000". Rejected if empty, longer than
 *            DEVICE_CONFIG_URL_MAX-1, or not starting with http:// or https://.
 */
esp_err_t device_config_set_backend_base_url(const char *url);

/**
 * @brief Persist a new OTA base URL. Takes effect on next boot.
 * @param url e.g. "https://192.168.1.71:8443". Same validation as above.
 */
esp_err_t device_config_set_ota_base_url(const char *url);

/** @return true once device_config_init() has run successfully. */
bool device_config_is_ready(void);

/** @return true if the active backend URL came from NVS rather than the
 *          compiled default. Useful in diagnostics: it answers "is this
 *          device using what I told it, or what it was built with?". */
bool device_config_backend_from_nvs(void);
