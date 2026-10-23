/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Simulated laptop bridge for the music remote - see ui_music_mock.h.
 */

#include "ui_music_mock.h"

#include <string.h>

static void publish(ui_music_mock_t *m)
{
    m->view.link = UI_MUSIC_LINK_SIMULATED;   /* never anything else */
    ui_music_apply(&m->view);
}

static void answer_cb(lv_timer_t *t)
{
    ui_music_mock_t *m = lv_timer_get_user_data(t);
    m->timer = NULL;   /* one-shot: LVGL deletes it after this call */
    ui_music_cmd_t cmd = m->view.pending;
    if (cmd == UI_MUSIC_CMD_NONE) {
        return;
    }
    m->view.pending = UI_MUSIC_CMD_NONE;
    m->view.last_cmd = cmd;

    if (m->outcome == UI_MUSIC_MOCK_FAIL) {
        m->view.last_result = UI_MUSIC_RESULT_FAILED;
    } else if (m->outcome == UI_MUSIC_MOCK_SILENT) {
        m->view.last_result = UI_MUSIC_RESULT_TIMED_OUT;
    } else {
        m->view.last_result = UI_MUSIC_RESULT_SENT;
        switch (cmd) {
        case UI_MUSIC_CMD_PLAY_PAUSE:
            m->view.play = m->view.play == UI_MUSIC_PLAY_PLAYING ? UI_MUSIC_PLAY_PAUSED : UI_MUSIC_PLAY_PLAYING;
            break;
        case UI_MUSIC_CMD_VOL_DOWN:
            if (m->view.volume != UI_MUSIC_VOLUME_NONE) {
                m->view.volume = m->view.volume > m->volume_step ? m->view.volume - m->volume_step : 0;
            }
            break;
        case UI_MUSIC_CMD_VOL_UP:
            if (m->view.volume != UI_MUSIC_VOLUME_NONE) {
                m->view.volume = m->view.volume + m->volume_step < 100 ? m->view.volume + m->volume_step : 100;
            }
            break;
        default:
            break;   /* prev/next: the mock has no track list */
        }
    }
    publish(m);
}

void ui_music_mock_init(ui_music_mock_t *m)
{
    memset(m, 0, sizeof(*m));
    m->view.link = UI_MUSIC_LINK_SIMULATED;
    m->view.play = UI_MUSIC_PLAY_PAUSED;
    m->view.volume_kind = UI_MUSIC_VOLUME_LAPTOP;
    m->view.volume = 40;
    m->view.pending = UI_MUSIC_CMD_NONE;
    m->view.last_cmd = UI_MUSIC_CMD_NONE;
    m->ack_ms = 400;
    m->timeout_ms = 3000;
    m->volume_step = 5;
    m->outcome = UI_MUSIC_MOCK_ACK;
}

void ui_music_mock_command(ui_music_cmd_t cmd, void *ctx)
{
    ui_music_mock_t *m = ctx;
    if (m->view.pending != UI_MUSIC_CMD_NONE || cmd < 0 || cmd >= UI_MUSIC_CMD_COUNT) {
        return;   /* one at a time, as the screen also enforces */
    }
    m->view.pending = cmd;
    publish(m);
    uint32_t wait = m->outcome == UI_MUSIC_MOCK_SILENT ? m->timeout_ms : m->ack_ms;
    m->timer = lv_timer_create(answer_cb, wait, m);
    lv_timer_set_repeat_count(m->timer, 1);
}

void ui_music_mock_show(ui_music_mock_t *m)
{
    publish(m);
}

void ui_music_mock_stop(ui_music_mock_t *m)
{
    if (m->timer) {
        lv_timer_delete(m->timer);
        m->timer = NULL;
    }
    m->view.pending = UI_MUSIC_CMD_NONE;
}
