/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief The named colours of the 1.3.0 dark look. Pure LVGL.
 * @details LVGL's default theme runs in dark mode (CONFIG_LV_THEME_DEFAULT_DARK),
 *          which already gives every unstyled label light text. These are for
 *          the places that set a colour on purpose. The rule from the 1.3.0
 *          spec: no grey text on dark backgrounds - secondary text is SILVER
 *          (UI_TEXT_DIM), which measures ~11:1 on UI_PANEL.
 *
 *          Status colours stay LVGL's palette (green/orange/red), which read
 *          fine on black; they are named here so new code does not reach for
 *          a fourth shade.
 */
#pragma once

#include "lvgl.h"

#define UI_BG        lv_color_hex(0x000000)   /* screen */
#define UI_PANEL     lv_color_hex(0x1C1F24)   /* charcoal blocks */
#define UI_TEXT      lv_color_white()
#define UI_TEXT_DIM  lv_color_hex(0xC0C4CA)   /* silver: labels, captions */
#define UI_ACCENT    lv_color_hex(0x1565C0)   /* MENU, buttons */
#define UI_TITLE     lv_color_hex(0x64B5F6)   /* page titles */
#define UI_RAIN      lv_color_hex(0x4FA3FF)   /* rain chance */
#define UI_OK        lv_palette_main(LV_PALETTE_GREEN)
#define UI_WARN      lv_palette_main(LV_PALETTE_ORANGE)
#define UI_ERR       lv_palette_main(LV_PALETTE_RED)

/* Calm Paper (gallery concept 02): the light look Mike chose for the survey
 * and the music remote. Warm paper, ink type, hairline rules instead of
 * panels; rust only for a failure, and always with a word. */
#define PAPER_BG       lv_color_hex(0xF2EFE8)
#define PAPER_INK      lv_color_hex(0x1F1D1A)
#define PAPER_GREY     lv_color_hex(0x655F55)   /* secondary text, ~5.9:1 on paper */
#define PAPER_HAIRLINE lv_color_hex(0xC9C2B4)
#define PAPER_FADED    lv_color_hex(0xA39C8E)   /* disabled only - never text to read */
#define PAPER_RUST     lv_color_hex(0x8E3A1C)
#define PAPER_PRESSED  lv_color_hex(0xE2DDD2)
