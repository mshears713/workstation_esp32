/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * LVGL's allocator, PSRAM first with internal RAM as a fallback (1.2.1).
 * The lv_*_core functions LVGL calls are in lvgl_psram_alloc.c; this header
 * only exposes the counters, for the "UI built" log line and the device
 * report.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** LVGL allocations that PSRAM could not take and internal RAM did. */
uint32_t lvgl_psram_alloc_fallbacks(void);

/** LVGL allocations that got no memory at all. */
uint32_t lvgl_psram_alloc_failures(void);

#ifdef __cplusplus
}
#endif
