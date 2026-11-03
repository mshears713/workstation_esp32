/* Render target: the music remote (main/ui_music.c) with the mock bridge
 * (main/ui_music_mock.c), opened the way status_deck_ui.c would open it.
 *
 * mock=true (default): the fixture seeds the mock's state and taps run the
 * real mock - pending, then an answer after ack_ms. The mock forces the link
 * to SIMULATED whatever `link` says, exactly as on the device.
 * mock=false: the fixture is shown as-is (how a real bridge's states look,
 * CONNECTED included); taps reach the command callback but nothing answers.
 *
 * trace=true adds a small line in the header listing every command the
 * callback received, so tap tests can prove which handler fired. The exit
 * callback closes the remote and leaves "MUSIC CLOSED". */
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "ws_sim.h"
#include "ui_music.h"
#include "ui_music_mock.h"

static lv_obj_t *screen;
static lv_obj_t *trace_label;
static char trace[160];
static bool use_mock;
static ui_music_mock_t mock;

static const char *const cmd_keys[] = { "prev", "play_pause", "next", "vol_down", "vol_up" };

static ui_music_cmd_t fx_cmd(const char *field)
{
    for (int i = 0; i < UI_MUSIC_CMD_COUNT; i++) {
        if (ws_fx_is(field, cmd_keys[i])) {
            return (ui_music_cmd_t)i;
        }
    }
    return UI_MUSIC_CMD_NONE;
}

static void on_command(ui_music_cmd_t cmd, void *ctx)
{
    (void)ctx;
    if (trace_label) {
        size_t n = strlen(trace);
        snprintf(trace + n, sizeof(trace) - n, "%s%s", n ? "," : "cmds:", cmd_keys[cmd]);
        lv_label_set_text(trace_label, trace);
    }
    if (use_mock) {
        ui_music_mock_command(cmd, &mock);
    }
}

static void on_exit(void *ctx)
{
    (void)ctx;
    ui_music_mock_stop(&mock);
    ui_music_close();
    lv_obj_t *l = lv_label_create(screen);
    lv_label_set_text(l, "MUSIC CLOSED");
    lv_obj_center(l);
}

void ws_render(lv_obj_t *scr)
{
    screen = scr;
    trace[0] = '\0';
    trace_label = NULL;
    use_mock = ws_fx_bool("mock", true);

    ui_music_mock_init(&mock);
    ui_music_view_t *v = &mock.view;
    v->link = ws_fx_is("link", "connected")      ? UI_MUSIC_LINK_CONNECTED
              : ws_fx_is("link", "disconnected") ? UI_MUSIC_LINK_DISCONNECTED
              : ws_fx_is("link", "unknown")      ? UI_MUSIC_LINK_UNKNOWN
                                                 : UI_MUSIC_LINK_SIMULATED;
    v->play = ws_fx_is("play", "playing")  ? UI_MUSIC_PLAY_PLAYING
              : ws_fx_is("play", "paused") ? UI_MUSIC_PLAY_PAUSED
                                           : UI_MUSIC_PLAY_UNKNOWN;
    v->title = ws_fx_str("title", "");
    v->artist = ws_fx_str("artist", "");
    v->volume = ws_fx_int("volume", 40);
    v->volume_kind = ws_fx_is("volume_kind", "spotify")   ? UI_MUSIC_VOLUME_SPOTIFY
                     : ws_fx_is("volume_kind", "unknown") ? UI_MUSIC_VOLUME_UNKNOWN
                                                          : UI_MUSIC_VOLUME_LAPTOP;
    v->pending = fx_cmd("pending");
    v->last_cmd = fx_cmd("result_cmd");
    v->last_result = ws_fx_is("result", "sent")        ? UI_MUSIC_RESULT_SENT
                     : ws_fx_is("result", "failed")    ? UI_MUSIC_RESULT_FAILED
                     : ws_fx_is("result", "timed_out") ? UI_MUSIC_RESULT_TIMED_OUT
                                                       : UI_MUSIC_RESULT_NONE;
    mock.ack_ms = (uint32_t)ws_fx_int("ack_ms", 400);
    mock.timeout_ms = (uint32_t)ws_fx_int("timeout_ms", 3000);
    mock.outcome = ws_fx_is("outcome", "fail")     ? UI_MUSIC_MOCK_FAIL
                   : ws_fx_is("outcome", "silent") ? UI_MUSIC_MOCK_SILENT
                                                   : UI_MUSIC_MOCK_ACK;
    ui_music_open(scr, v, on_command, on_exit, NULL);
    if (use_mock) {
        ui_music_mock_show(&mock);   /* the mock, not this adapter, decides the link word */
    }

    if (ws_fx_bool("trace", false)) {
        trace_label = lv_label_create(scr);   /* created after the sheet, so on top */
        lv_obj_set_style_text_font(trace_label, &lv_font_montserrat_12, 0);
        lv_label_set_text(trace_label, "cmds:");
        lv_obj_set_pos(trace_label, 116, 36);
    }
}
