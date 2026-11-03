/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief LOG page model - see ui_log_format.h.
 */

#include "ui_log_format.h"

#include <stdio.h>
#include <string.h>

const char *ui_log_tag(int type)
{
    static const char *const tags[UI_LOG_TYPE_COUNT] = {
        "BOT", "RST", "CMD", "SEN", "SYS", "NET", "HS ", "AUD", "VOX", "API", "RPT",
    };
    return (type >= 0 && type < UI_LOG_TYPE_COUNT) ? tags[type] : "???";
}

void ui_log_fmt_time(uint32_t s, char *b, size_t n)
{
    if (s < 3600u) {
        snprintf(b, n, "%u:%02u", (unsigned)(s / 60u), (unsigned)(s % 60u));
    } else if (s < 86400u) {
        snprintf(b, n, "%uh%02u", (unsigned)(s / 3600u), (unsigned)(s / 60u % 60u));
    } else {
        unsigned d = (unsigned)(s / 86400u);
        /* keep five characters: the column must not push into the message */
        if (d < 100u) {
            snprintf(b, n, "%ud%02u", d, (unsigned)(s / 3600u % 24u));
        } else if (d < 1000u) {
            snprintf(b, n, "%ud", d);
        } else {
            snprintf(b, n, ">999d");
        }
    }
}

bool ui_log_is_fault(int type, const char *m)
{
    if (m == NULL) {
        return false;
    }
    if (type == UI_LOG_RESET) {
        /* "RST POWERON" / "RST SW RESTART" are normal starts (an update
         * reboots with SW RESTART); anything else - PANIC, watchdogs,
         * BROWNOUT, "PREV: ..." crash crumbs - is why you opened this page. */
        return strstr(m, "POWERON") == NULL && strstr(m, "SW RESTART") == NULL;
    }
    static const char *const words[] = {
        "FAIL", "STALE", "LOST", "DOWN", "PANIC", "WATCHDOG", "WDT", "TIMEOUT", "TIMED OUT", "ERROR",
        "ERR ", "BUSY", "DEGRADED", "BROWNOUT", "NOT SET", "REFUSED", "UNREACH", " OFF",
    };
    for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
        if (strstr(m, words[i])) {
            return true;
        }
    }
    return false;
}
