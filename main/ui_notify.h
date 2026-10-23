/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief NOTIFY page (gallery concept 05 Card Stack) and the playback overlay
 *        that goes with it. Pure LVGL.
 * @details What exists, and so what the cards can honestly be:
 *            - the backend queues spoken notifications; /pending gives how
 *              many and the OLDEST one's id and time. Only that one can be
 *              described: its text comes from GET /notifications/{id}.
 *              The others are a count - drawn as cards behind, with no
 *              invented content;
 *            - the device keeps no history, so "earlier" cards are only the
 *              outcomes this device saw since boot (status_deck_ui.c keeps
 *              the last few);
 *            - playing and acknowledging are separate steps, and the cards
 *              keep them separate: PLAYED (ack failed) is not DELIVERED.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_NOTIFY_LINK_UNKNOWN = 0,   /* not polled yet */
    UI_NOTIFY_LINK_OK,
    UI_NOTIFY_LINK_DOWN,          /* backend not answering: the count may be old */
    UI_NOTIFY_LINK_NO_NET,
} ui_notify_link_t;

typedef enum {
    UI_NOTIFY_DELIVERED = 0,      /* played to the end, ack accepted */
    UI_NOTIFY_ACK_FAILED,         /* played to the end, ack refused/failed: it will come back */
    UI_NOTIFY_STOPPED,            /* STOP pressed: not acknowledged, still queued */
    UI_NOTIFY_FETCH_FAILED,       /* audio not fetched: not played, still queued */
} ui_notify_outcome_t;

typedef struct {
    ui_notify_outcome_t outcome;
    uint32_t uptime_s;            /* when, since boot */
    const char *text;             /* what it said, if the text was loaded, else NULL */
    const char *reason;           /* FETCH_FAILED detail, else NULL */
} ui_notify_done_t;

typedef struct {
    ui_notify_link_t link;
    int pending;                  /* /pending count */
    const char *front_text;       /* oldest pending one's spoken_message, NULL if not loaded */
    bool front_text_failed;       /* tried and could not load it (too long / error) */
    uint32_t front_age_s;         /* since it was created, if known */
    bool front_age_known;
    const ui_notify_done_t *done; /* newest first */
    int done_count;
    uint32_t now_s;               /* uptime now, for "4 min ago" on done cards */
} ui_notify_view_t;

typedef struct {
    lv_obj_t *page;
    lv_obj_t *card, *ghost1, *ghost2;
    lv_obj_t *kicker, *headline, *body, *meta;
    lv_obj_t *play, *prev, *next, *dots;
    int index;                    /* which card is in front: 0 = the queue's front */
    int total;
    ui_notify_view_t view;        /* last applied (strings borrowed) */
} ui_notify_page_t;

typedef struct {
    lv_event_cb_t play;           /* start playback of the oldest pending one */
} ui_notify_actions_t;

void ui_notify_build(ui_notify_page_t *p, lv_obj_t *page, const ui_notify_actions_t *a);
void ui_notify_apply(ui_notify_page_t *p, const ui_notify_view_t *v);

/* ---- playback overlay (replaces the old one-line NOTIFICATION overlay) ---- */

typedef enum {
    UI_NOTIFY_PB_FETCHING = 0,
    UI_NOTIFY_PB_PLAYING,
    UI_NOTIFY_PB_ACKING,          /* played; telling the backend */
    UI_NOTIFY_PB_DONE,            /* outcome below */
} ui_notify_pb_phase_t;

typedef struct {
    ui_notify_pb_phase_t phase;
    ui_notify_outcome_t outcome;  /* when DONE */
    const char *text;             /* message text if loaded */
    const char *reason;           /* fetch failure detail */
    int remaining;                /* still queued after this one, if known (-1 unknown) */
} ui_notify_pb_view_t;

typedef struct {
    lv_obj_t *root, *kicker, *text, *step[3], *step_word[3], *result, *stop;
} ui_notify_overlay_t;

void ui_notify_overlay_build(ui_notify_overlay_t *o, lv_obj_t *scr, lv_event_cb_t stop_cb);
void ui_notify_overlay_apply(ui_notify_overlay_t *o, const ui_notify_pb_view_t *v);
void ui_notify_overlay_hide(ui_notify_overlay_t *o);

#ifdef __cplusplus
}
#endif
