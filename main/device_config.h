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
 *          polls every ~500ms) read it concurrently without locking;
 *          rewriting that buffer underneath a snprintf() would be a genuine
 *          race for the sake of saving a reboot nobody minds.
 *
 *          1.2.0 adds a fallback without breaking that rule: there are two
 *          buffers (primary and fallback), both filled once at init, and a
 *          switch swaps which one the accessor returns. The primary is the
 *          NVS value if one is stored, else the compiled IP default; the
 *          fallback is the compiled default the primary is not (the mDNS
 *          name, normally).
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

/* Compiled-in defaults, used only when NVS holds nothing.
 *
 * 1.2.0: CLAWBOX's LAN address first, its mDNS name second. Until 1.1.3 the
 * name was the only address, and it proved to be the weak link: on CLAWBOX
 * both avahi and OctoPrint's zeroconf answer for "clawbox.local", avahi then
 * renames itself clawbox-2.local, and OctoPrint answers with its Tailscale
 * address (unreachable from the LAN) as well as the real one. When that
 * happened every backend task went silent while Wi-Fi and ping stayed up,
 * and nothing on the device noticed. The address is reserved on the router;
 * the name is kept as the fallback in case it ever moves.
 *
 * The OTA default can be an IP because CLAWBOX's pinned certificate carries
 * "IP Address:192.168.1.71" in its subjectAltName, and mbedTLS matches IP
 * SANs (x509_crt_check_san_ip) when the hostname handed to
 * mbedtls_ssl_set_hostname() - here, the URL's host - is a literal IPv4. */
#define DEVICE_CONFIG_DEFAULT_BACKEND_URL "http://192.168.1.71:8000"
#define DEVICE_CONFIG_DEFAULT_OTA_URL     "https://192.168.1.71:8443"
#define DEVICE_CONFIG_FALLBACK_BACKEND_URL "http://clawbox.local:8000"
#define DEVICE_CONFIG_FALLBACK_OTA_URL     "https://clawbox.local:8443"

/**
 * @brief Load configuration from NVS into memory.
 * @details Call once, early in app_main(), after nvs_flash_init(). Missing
 *          keys are not an error - the compiled defaults are used instead.
 *          Safe to call twice; the second call is a no-op.
 */
esp_err_t device_config_init(void);

/**
 * @return Backend API base URL currently in use, no trailing slash. Never
 *         NULL. Always one of two static strings that are written once in
 *         device_config_init() and never again, so a pointer a task got a
 *         moment ago stays valid and unchanged even after a switch - a
 *         switch only changes which of the two the NEXT caller gets.
 */
const char *device_config_backend_base_url(void);

/** @return Primary OTA base URL, no trailing slash. Never NULL. */
const char *device_config_ota_base_url(void);

/** @return The OTA URL to retry with when the primary cannot be reached
 *          (connect/TLS failure). Never NULL; may equal the primary if no
 *          distinct fallback exists. */
const char *device_config_ota_fallback_url(void);

/**
 * @brief Switch the backend between primary and fallback.
 * @details Called by backend_health.c after repeated /health failures.
 *          A single pointer store - no buffer is rewritten, so readers
 *          mid-snprintf are unaffected. Not persisted: every boot starts on
 *          the primary.
 * @return true if the fallback is now in use, false if the primary is.
 */
bool device_config_backend_switch(void);

/** @return true if the backend URL in use is the fallback, not the primary. */
bool device_config_backend_on_fallback(void);

/** @return The backend URL that is NOT currently in use (for log lines). */
const char *device_config_backend_other_url(void);

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
