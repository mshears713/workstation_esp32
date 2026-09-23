/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * LVGL's allocator, PSRAM first (1.2.1). Selected by
 * CONFIG_LV_USE_CUSTOM_MALLOC=y (LV_STDLIB_CUSTOM): LVGL then expects the
 * application to provide exactly the functions below, which the stock
 * src/stdlib/clib/lv_mem_core_clib.c provides for CONFIG_LV_USE_CLIB_MALLOC.
 *
 * Why: with the C library's malloc and CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=
 * 4096, every LVGL object, style and label - all small - landed in internal
 * RAM. All six pages and three overlays are built at boot and never freed,
 * which is an estimated 25-45 KB of the ~231 KB the board has. The 1.2.0
 * report of 23 Sep showed internal min-free at 1,207 bytes and a PANIC on
 * the boot before. Internal RAM is what Wi-Fi, the OTA task stack, DMA and
 * MultiNet need; LVGL does not need it.
 *
 * What stays internal: the display draw buffer. esp_lvgl_port allocates it
 * itself with heap_caps_aligned_alloc(MALLOC_CAP_DMA) and hands it to
 * lv_display_set_buffers(), so it never passes through here. Nothing LVGL
 * allocates through lv_malloc is handed to a DMA engine: SW-render layers
 * are read and written by the CPU only, and the SPI panel is fed from the
 * port's own buffer.
 *
 * The one ISR that touches LVGL memory is the SPI "colour transfer done"
 * callback (lv_display_flush_ready() writes to the lv_display_t). The SPI
 * bus is initialised without ESP_INTR_FLAG_IRAM, so that ISR is masked
 * while the flash cache is disabled - the same condition that already makes
 * it safe for it to call LVGL code in flash - and so PSRAM is reachable
 * whenever it runs.
 *
 * Internal RAM remains a fallback, so a full PSRAM (it is 16 MB and mostly
 * idle) degrades to 1.2.0 behaviour rather than to a NULL. Fallbacks are
 * counted and reported.
 *
 * Threading: LVGL calls these only from the LVGL task or with the LVGL
 * lock held (bsp_display_lock), so the counters need no lock of their own.
 */
#include <stdint.h>

#include "esp_heap_caps.h"
#include "lvgl.h"   /* brings in src/stdlib/lv_mem.h and the prototypes below */

#include "lvgl_psram_alloc.h"

#if LV_USE_STDLIB_MALLOC != LV_STDLIB_CUSTOM
#error "lvgl_psram_alloc.c needs CONFIG_LV_USE_CUSTOM_MALLOC=y (see sdkconfig.defaults)"
#endif

#define LVGL_CAPS_PSRAM    (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define LVGL_CAPS_ANY      (MALLOC_CAP_8BIT)

static uint32_t s_fallbacks;   /* allocations that had to go to internal RAM */
static uint32_t s_failures;    /* allocations that got nothing at all */

void lv_mem_init(void)
{
}

void lv_mem_deinit(void)
{
}

lv_mem_pool_t lv_mem_add_pool(void *mem, size_t bytes)
{
    LV_UNUSED(mem);
    LV_UNUSED(bytes);
    return NULL;
}

void lv_mem_remove_pool(lv_mem_pool_t pool)
{
    LV_UNUSED(pool);
}

void *lv_malloc_core(size_t size)
{
    void *p = heap_caps_malloc(size, LVGL_CAPS_PSRAM);
    if (p == NULL) {
        p = heap_caps_malloc(size, LVGL_CAPS_ANY);
        if (p != NULL) {
            s_fallbacks++;
        } else {
            s_failures++;
        }
    }
    return p;
}

void *lv_realloc_core(void *p, size_t new_size)
{
    /* heap_caps_realloc() moves the block into PSRAM if it is not already
     * there, and leaves the original untouched when it fails. */
    void *n = heap_caps_realloc(p, new_size, LVGL_CAPS_PSRAM);
    if (n == NULL && new_size > 0) {
        n = heap_caps_realloc(p, new_size, LVGL_CAPS_ANY);
        if (n != NULL) {
            s_fallbacks++;
        } else {
            s_failures++;
        }
    }
    return n;
}

void lv_free_core(void *p)
{
    heap_caps_free(p);
}

void lv_mem_monitor_core(lv_mem_monitor_t *mon_p)
{
    /* What LVGL's own monitor would show: the pool it allocates from. */
    multi_heap_info_t info;
    heap_caps_get_info(&info, LVGL_CAPS_PSRAM);
    mon_p->total_size = info.total_free_bytes + info.total_allocated_bytes;
    mon_p->free_size = info.total_free_bytes;
    mon_p->free_biggest_size = info.largest_free_block;
    mon_p->free_cnt = info.free_blocks;
    mon_p->used_cnt = info.allocated_blocks;
    mon_p->max_used = info.total_free_bytes + info.total_allocated_bytes - info.minimum_free_bytes;
    mon_p->used_pct = mon_p->total_size ? (uint8_t)(100 - (100ULL * info.total_free_bytes) / mon_p->total_size) : 0;
    mon_p->frag_pct = 0;
}

lv_result_t lv_mem_test_core(void)
{
    return heap_caps_check_integrity(MALLOC_CAP_SPIRAM, false) ? LV_RESULT_OK : LV_RESULT_INVALID;
}

uint32_t lvgl_psram_alloc_fallbacks(void)
{
    return s_fallbacks;
}

uint32_t lvgl_psram_alloc_failures(void)
{
    return s_failures;
}
