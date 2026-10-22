/* Render target: the event-feedback survey (main/ui_survey.c), built the way
 * status_deck_ui.c opens it. Fixture state picks the step and the answers;
 * taps then drive the real flow. The submit callback stands in for the app's:
 * it "stores" unless save_fails is set. The exit callback closes the survey
 * and leaves a marker, so a hold-to-exit test can see it fired. */
#include "lvgl.h"
#include "ws_sim.h"
#include "ui_survey.h"

/* Each stored submit takes the next number, so a double submit would show
 * on the thank-you screen as response_no + 1 instead of response_no. */
static uint32_t stored;

static bool fake_submit(const int *answers, uint32_t *response_no, void *ctx)
{
    (void)answers;
    (void)ctx;
    if (ws_fx_bool("save_fails", false)) {
        return false;
    }
    *response_no = (uint32_t)ws_fx_i64("response_no", 12) + stored++;
    return true;
}

static lv_obj_t *screen;

static void fake_exit(void *ctx)
{
    (void)ctx;
    ui_survey_close();
    lv_obj_t *l = lv_label_create(screen);
    lv_label_set_text(l, "SURVEY EXITED");
    lv_obj_center(l);
}

static ui_survey_config_t cfg;
static ui_survey_question_t questions[UI_SURVEY_QUESTIONS];

void ws_render(lv_obj_t *scr)
{
    static const char *const steps[] = { "welcome", "q1", "q2", "q3", "review", "thanks" };
    screen = scr;
    for (int i = 0; i < UI_SURVEY_QUESTIONS; i++) {
        questions[i] = ui_survey_sample_questions[i];
    }
    if (ws_fx_str("q1_prompt", "")[0]) {
        questions[0].prompt = ws_fx_str("q1_prompt", "");
    }
    cfg = (ui_survey_config_t){
        .questions = questions,
        .qr_url = ws_fx_str("qr_url", "https://example.com/feedback"),
        .qr_caption = ws_fx_str("qr_caption", "Sample link:\nexample.com"),
        .saved_note = ws_fx_str("saved_note", "Saved on this device only (sample mode)."),
        .submit = fake_submit,
        .exit = fake_exit,
        .idle_reset_s = (uint32_t)ws_fx_int("idle_reset_s", 90),
        .thanks_reset_s = (uint32_t)ws_fx_int("thanks_reset_s", 30),
    };
    ui_survey_open(scr, &cfg);

    int step = 0;
    for (int i = 0; i < 6; i++) {
        if (ws_fx_is("step", steps[i])) {
            step = i;
        }
    }
    int answers[UI_SURVEY_QUESTIONS] = {
        ws_fx_int("a1", UI_SURVEY_UNANSWERED), ws_fx_int("a2", UI_SURVEY_UNANSWERED),
        ws_fx_int("a3", UI_SURVEY_UNANSWERED),
    };
    if (step != 0 || answers[0] != UI_SURVEY_UNANSWERED) {
        ui_survey_preview((ui_survey_step_t)step, answers, ws_fx_bool("save_failed", false),
                          (uint32_t)ws_fx_i64("response_no", 12));
    }
}
