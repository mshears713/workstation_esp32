/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Mission 13 - one request-ID shape shared by NOTE and GO.
 * @details Header-only (no .c/.o, nothing to add to CMakeLists) since it's
 *          one function built on esp_random() (the SoC's hardware RNG,
 *          always available, no seeding step). Not a UUID and not claiming
 *          global uniqueness - "unique enough for one device's local
 *          session" (32 bits of randomness, readable on a small screen) is
 *          the actual requirement here, matching how audio_capture.c's own
 *          capture_seq/artifact_name is scoped to this boot, not forever.
 */
#pragma once

#include <stdio.h>
#include <stdint.h>
#include "esp_random.h"

/* "NOTE-" + 8 hex chars + nul = 14; "GO-" + 8 hex chars + nul = 12. 24
 * leaves headroom for any longer prefix a future command adds without
 * every caller needing to resize. */
#define REQUEST_ID_LEN 24

/**
 * Fills `out` with "<prefix>-XXXXXXXX" (8 uppercase hex digits from
 * esp_random()). `out` must be at least REQUEST_ID_LEN bytes; `prefix`
 * should be short (e.g. "NOTE", "GO") so the result comfortably fits both
 * a small on-screen label and a filename.
 */
static inline void generate_request_id(const char *prefix, char *out, size_t out_len)
{
    uint32_t r = esp_random();
    snprintf(out, out_len, "%s-%08lX", prefix, (unsigned long)r);
}
