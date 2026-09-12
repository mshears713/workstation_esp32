/*
 * SPDX-FileCopyrightText: 2021-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief What firmware is this, and where is it running from.
 * @details Thin accessors over esp_app_desc_t and the OTA partition APIs.
 *          There is deliberately no bookkeeping of our own here: ESP-IDF
 *          already stamps every image with a version, a build date and time,
 *          the IDF version it was built against, and a SHA-256 of the ELF,
 *          and the bootloader already knows which slot it booted. Inventing
 *          a parallel record of the same facts is how the two drift apart.
 *
 *          The version string is set by PROJECT_VER in the top-level
 *          CMakeLists.txt and has the shape:
 *
 *              <semver>+<short git sha>[-dirty]      e.g. 1.1.0+cb93a81
 *
 *          so a device can answer "what commit are you running?" without a
 *          separate lookup table. firmware_git_sha() returns the part after
 *          the '+'; a build made from an unclean tree carries "-dirty",
 *          which is a fact worth seeing on screen during a migration.
 */
#pragma once

#include <stdbool.h>
#include "esp_ota_ops.h"

/** @return Full version string, e.g. "1.1.0+cb93a81". Never NULL. */
const char *firmware_version(void);

/** @return Short git SHA parsed out of the version, e.g. "cb93a81", or
 *          "unknown" if the version was not built with one. Never NULL. */
const char *firmware_git_sha(void);

/** @return Build date and time as "MMM DD YYYY HH:MM:SS". Never NULL. */
const char *firmware_built_at(void);

/** @return ESP-IDF version the image was built against. Never NULL. */
const char *firmware_idf_version(void);

/** @return Label of the partition actually running, e.g. "ota_0". */
const char *firmware_running_partition_label(void);

/** @return Label of the slot the next OTA would be written to, e.g. "ota_1",
 *          or "none" if there is no second slot. */
const char *firmware_next_ota_partition_label(void);

/** @return The running image's OTA state (ESP_OTA_IMG_VALID,
 *          ESP_OTA_IMG_PENDING_VERIFY, ...). ESP_OTA_IMG_VALID is reported
 *          for a factory/non-OTA image, which has no otadata entry. */
esp_ota_img_states_t firmware_ota_state(void);

/** @return true while the running image is on probation - booted from OTA
 *          but not yet marked valid, so a reset right now would roll back. */
bool firmware_is_pending_verify(void);

/** @return Human-readable one-liner for logs and the FIRMWARE page, e.g.
 *          "1.1.0+cb93a81 @ ota_0 (valid)". Points at a static buffer. */
const char *firmware_identity_summary(void);

/** @brief Log the full identity block once at boot. */
void firmware_identity_log(void);
