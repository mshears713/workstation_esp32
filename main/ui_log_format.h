/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief LOG page model: Black Box event type tags, time-since-boot stamps and
 *        which lines read as faults. Plain C, shared by firmware, simulator
 *        and tools/hosttest.
 * @details Types mirror status_deck_ui.c's event_type_t, in its order (the
 *          app passes the raw value). The fault rule is a word list over the
 *          message, not a field the recorder stores: the Black Box keeps
 *          (type, uptime, text) and nothing else, and its messages are
 *          written to be read ("UPLOAD FAILED: TIMEOUT", "SOURCE STALE"). A
 *          new message that means trouble without one of these words would
 *          print as a normal line - add the word here.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* = event_type_t in status_deck_ui.c */
typedef enum {
    UI_LOG_BOOT = 0, UI_LOG_RESET, UI_LOG_COMMAND, UI_LOG_SENSOR, UI_LOG_SYSTEM, UI_LOG_NETWORK,
    UI_LOG_HANDSHAKE, UI_LOG_AUDIO, UI_LOG_VOICE, UI_LOG_BACKEND, UI_LOG_REPORT, UI_LOG_TYPE_COUNT,
} ui_log_type_t;

/** Three-letter tag for the type column: BOOT->BOT, NETWORK->NET, BACKEND->API ... "???" if unknown. */
const char *ui_log_tag(int type);

/** Time since boot, at most 5 characters so the column never moves:
 *  "0:07", "12:04", "3h02", "51d07", "412d", ">999d". */
void ui_log_fmt_time(uint32_t uptime_s, char *b, size_t n);

/** True if the line reports something gone wrong (FAIL, STALE, LOST, DOWN,
 *  PANIC, WATCHDOG, TIMEOUT, ...). A reset is a fault unless it is a power-on
 *  or a deliberate software restart. */
bool ui_log_is_fault(int type, const char *message);

#ifdef __cplusplus
}
#endif
