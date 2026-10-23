/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Music remote (experiment, Calm Paper): previous / play-pause / next
 *        and volume down / up for Spotify on Mike's laptop. Pure LVGL - the
 *        same screen runs in the simulator.
 * @details An experiment, not a page: ui_music_open() builds it full-screen
 *          over whatever is showing and ui_music_close() deletes everything
 *          it built. The way out is its own "< HOME" key in the top-left
 *          corner, a full key-row away from the transport keys.
 *
 *          The screen never decides whether the laptop is really being
 *          controlled. The app says so through ui_music_view_t.link, and the
 *          screen prints it on every state: SIMULATED (the mock in
 *          ui_music_mock.c - nothing leaves the device), CONNECTED (a real
 *          bridge acknowledged), NOT CONNECTED, or CONNECTING (unknown).
 *          Keys only send commands through the callback; the app reports
 *          back what happened with ui_music_apply().
 */
#pragma once

#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_MUSIC_LINK_UNKNOWN = 0,   /* not known yet: "CONNECTING", keys off */
    UI_MUSIC_LINK_SIMULATED,     /* the mock answers; nothing reaches the laptop */
    UI_MUSIC_LINK_CONNECTED,     /* a real bridge to the laptop answered */
    UI_MUSIC_LINK_DISCONNECTED,  /* bridge unreachable: "NOT CONNECTED", keys off */
} ui_music_link_t;

typedef enum {
    UI_MUSIC_PLAY_UNKNOWN = 0,
    UI_MUSIC_PLAY_PLAYING,
    UI_MUSIC_PLAY_PAUSED,
} ui_music_play_t;

typedef enum {
    UI_MUSIC_CMD_PREV = 0,
    UI_MUSIC_CMD_PLAY_PAUSE,
    UI_MUSIC_CMD_NEXT,
    UI_MUSIC_CMD_VOL_DOWN,
    UI_MUSIC_CMD_VOL_UP,
    UI_MUSIC_CMD_COUNT,
    UI_MUSIC_CMD_NONE = -1,
} ui_music_cmd_t;

/** What happened to the last command. SENT means the other end acknowledged
 *  it - with media keys that is "the laptop pressed the key", not proof that
 *  Spotify reacted. */
typedef enum {
    UI_MUSIC_RESULT_NONE = 0,
    UI_MUSIC_RESULT_SENT,
    UI_MUSIC_RESULT_FAILED,
    UI_MUSIC_RESULT_TIMED_OUT,
} ui_music_result_t;

/** WHICH volume the number is. These are different controls on the laptop:
 *  the system (speaker) volume moves everything; Spotify's own volume moves
 *  only Spotify. The label on screen says which one. */
typedef enum {
    UI_MUSIC_VOLUME_UNKNOWN = 0,   /* "Volume" */
    UI_MUSIC_VOLUME_LAPTOP,        /* "Laptop volume" - Windows system volume */
    UI_MUSIC_VOLUME_SPOTIFY,       /* "Spotify volume" - Spotify's own level */
} ui_music_volume_kind_t;

#define UI_MUSIC_VOLUME_NONE (-1)  /* volume not known */

typedef struct {
    ui_music_link_t link;
    ui_music_play_t play;
    const char *title;              /* optional; NULL or "" = not shown */
    const char *artist;             /* optional */
    ui_music_volume_kind_t volume_kind;
    int volume;                     /* 0..100, or UI_MUSIC_VOLUME_NONE */
    ui_music_cmd_t pending;         /* waiting for an answer to this; NONE = idle */
    ui_music_cmd_t last_cmd;        /* what last_result is about */
    ui_music_result_t last_result;
} ui_music_view_t;

/** A key was tapped. The screen has already checked the link is usable, no
 *  other command is pending, and the volume is not at that end; the app
 *  sends it and later calls ui_music_apply() with pending set, then with
 *  the result. */
typedef void (*ui_music_command_fn)(ui_music_cmd_t cmd, void *ctx);
/** The "< HOME" key. The app closes the remote (ui_music_close()). Called
 *  deferred, never from inside the key's own event. */
typedef void (*ui_music_exit_fn)(void *ctx);

/** Short word for a command: "Previous", "Play/pause", "Next",
 *  "Volume down", "Volume up". */
const char *ui_music_cmd_name(ui_music_cmd_t cmd);

/** Builds the remote full-screen on `scr` showing `view`. One at a time.
 *  Returns the sheet. */
lv_obj_t *ui_music_open(lv_obj_t *scr, const ui_music_view_t *view, ui_music_command_fn command,
                        ui_music_exit_fn exit, void *ctx);
/** Shows a new state (strings are copied). Safe if not open. */
void ui_music_apply(const ui_music_view_t *view);
/** Deletes everything the remote built. Safe if not open. */
void ui_music_close(void);
bool ui_music_is_open(void);

#ifdef __cplusplus
}
#endif
