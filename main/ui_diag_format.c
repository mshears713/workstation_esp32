/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief DIAGNOSTICS page model - see ui_diag_format.h. Plain C.
 */

#include "ui_diag_format.h"

#include <stdio.h>
#include <string.h>

ui_diag_src_t ui_diag_freshness(bool probed, bool present, int64_t last_good_us, int64_t now_us,
                                int64_t stale_after_us, uint64_t *age_ms)
{
    *age_ms = 0;
    if (!probed) {
        return UI_DIAG_SRC_UNKNOWN;
    }
    if (!present) {
        return UI_DIAG_SRC_FAILED;
    }
    if (last_good_us <= 0) {
        return UI_DIAG_SRC_WAITING;
    }
    /* a sample stamped after `now` (read order) is simply fresh */
    int64_t age_us = now_us > last_good_us ? now_us - last_good_us : 0;
    *age_ms = (uint64_t)age_us / 1000u;
    return age_us > stale_after_us ? UI_DIAG_SRC_STALE : UI_DIAG_SRC_LIVE;
}

void ui_diag_fmt_age_ms(char *b, size_t n, uint64_t ms)
{
    uint64_t s = ms / 1000u;
    if (ms < 1000u) {
        snprintf(b, n, "<1 s");
    } else if (s < 60u) {
        snprintf(b, n, "%u s", (unsigned)s);
    } else if (s < 3600u) {
        snprintf(b, n, "%um %02us", (unsigned)(s / 60u), (unsigned)(s % 60u));
    } else if (s < 86400u) {
        snprintf(b, n, "%uh %02um", (unsigned)(s / 3600u), (unsigned)(s / 60u % 60u));
    } else {
        /* compact: it has to fit beside "BACKEND" at 14 px */
        snprintf(b, n, "%llud %uh", (unsigned long long)(s / 86400u), (unsigned)(s / 3600u % 24u));
    }
}

void ui_diag_fmt_days(char *b, size_t n, uint64_t up_s)
{
    unsigned long long d = (unsigned long long)(up_s / 86400u);
    snprintf(b, n, "%llu %s", d, d == 1 ? "day" : "days");
}

void ui_diag_fmt_hm(char *b, size_t n, uint64_t up_s)
{
    snprintf(b, n, "%02u h %02u m", (unsigned)(up_s / 3600u % 24u), (unsigned)(up_s / 60u % 60u));
}

void ui_diag_fmt_kib(char *b, size_t n, uint32_t bytes)
{
    uint64_t tenths = ((uint64_t)bytes * 10u + 512u) / 1024u;
    if (tenths < 10000u) {
        snprintf(b, n, "%u.%u", (unsigned)(tenths / 10u), (unsigned)(tenths % 10u));
    } else {
        snprintf(b, n, "%u", (unsigned)(((uint64_t)bytes + 512u) / 1024u));
    }
}

ui_diag_line_t ui_diag_wifi_line(const ui_diag_view_t *v)
{
    ui_diag_line_t l = { "", "", UI_DIAG_SEV_NONE };
    switch (v->wifi) {
    case UI_DIAG_WIFI_ONLINE:
        snprintf(l.word, sizeof l.word, "ONLINE");
        if (v->have_rssi) {
            snprintf(l.detail, sizeof l.detail, "%d dBm", v->rssi_dbm);
        } else {
            snprintf(l.detail, sizeof l.detail, "RSSI --");
        }
        l.sev = UI_DIAG_SEV_NOM;
        break;
    case UI_DIAG_WIFI_JOINING:
        snprintf(l.word, sizeof l.word, "JOINING");
        snprintf(l.detail, sizeof l.detail, "try %u", (unsigned)v->wifi_attempts);
        l.sev = UI_DIAG_SEV_CAU;
        break;
    case UI_DIAG_WIFI_RETRY:
        /* the countdown rides with the word; the cause (up to 19 chars) is the detail */
        snprintf(l.word, sizeof l.word, "RETRY %us", (unsigned)(v->wifi_retry_s > 999u ? 999u : v->wifi_retry_s));
        snprintf(l.detail, sizeof l.detail, "%s", (v->wifi_reason && v->wifi_reason[0]) ? v->wifi_reason : "");
        l.sev = UI_DIAG_SEV_ALM;
        break;
    case UI_DIAG_WIFI_OFF:
    default:
        snprintf(l.word, sizeof l.word, "OFF");
        l.sev = UI_DIAG_SEV_ALM;
        break;
    }
    return l;
}

ui_diag_line_t ui_diag_api_line(const ui_diag_view_t *v)
{
    ui_diag_line_t l = { "", "", UI_DIAG_SEV_NONE };
    /* Wi-Fi updates can precede the backend poll. Never show its cached OK
     * beside an unavailable network; this is shared by firmware and fixtures. */
    if (v->wifi != UI_DIAG_WIFI_ONLINE) {
        snprintf(l.word, sizeof l.word, "NO NET");
        return l;
    }
    switch (v->api) {
    case UI_DIAG_API_OK:
        /* no latency: the last probe's round trip beside a live "OK" would read as current */
        snprintf(l.word, sizeof l.word, "OK");
        l.sev = UI_DIAG_SEV_NOM;
        break;
    case UI_DIAG_API_DOWN:
        snprintf(l.word, sizeof l.word, "DOWN");
        if (v->api_ever_ok) {
            ui_diag_fmt_age_ms(l.detail, sizeof l.detail, (uint64_t)v->api_secs_since_ok * 1000u);
        } else {
            snprintf(l.detail, sizeof l.detail, "never");   /* not once this boot */
        }
        l.sev = UI_DIAG_SEV_ALM;
        break;
    case UI_DIAG_API_NO_NET:
        /* the cause is Wi-Fi, which already carries the alarm */
        snprintf(l.word, sizeof l.word, "NO NET");
        break;
    case UI_DIAG_API_UNKNOWN:
    default:
        snprintf(l.word, sizeof l.word, "WAITING");
        break;
    }
    return l;
}

ui_diag_line_t ui_diag_src_line(ui_diag_src_t s, uint64_t age_ms, bool onboard)
{
    ui_diag_line_t l = { "", "", UI_DIAG_SEV_NONE };
    switch (s) {
    case UI_DIAG_SRC_LIVE:
        snprintf(l.word, sizeof l.word, "LIVE");
        ui_diag_fmt_age_ms(l.detail, sizeof l.detail, age_ms);
        l.sev = UI_DIAG_SEV_NOM;
        break;
    case UI_DIAG_SRC_STALE:
        snprintf(l.word, sizeof l.word, "STALE");
        ui_diag_fmt_age_ms(l.detail, sizeof l.detail, age_ms);
        l.sev = UI_DIAG_SEV_CAU;
        break;
    case UI_DIAG_SRC_WAITING:
        snprintf(l.word, sizeof l.word, "WAITING");
        break;
    case UI_DIAG_SRC_FAILED:
        snprintf(l.word, sizeof l.word, "ERROR");
        snprintf(l.detail, sizeof l.detail, "init fail");
        l.sev = onboard ? UI_DIAG_SEV_ALM : UI_DIAG_SEV_CAU;
        break;
    case UI_DIAG_SRC_UNKNOWN:
    default:
        snprintf(l.word, sizeof l.word, "UNKNOWN");
        break;
    }
    return l;
}

ui_diag_sev_t ui_diag_heap_min_sev(const ui_diag_view_t *v)
{
    return v->heap_min < UI_DIAG_HEAP_MIN_REF ? UI_DIAG_SEV_ALM : UI_DIAG_SEV_NONE;
}

ui_diag_sev_t ui_diag_heap_block_sev(const ui_diag_view_t *v)
{
    return v->heap_largest < UI_DIAG_HEAP_BLOCK_REF ? UI_DIAG_SEV_CAU : UI_DIAG_SEV_NONE;
}
