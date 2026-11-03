/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Event-feedback survey (experiment, Calm Paper): welcome, three
 *        question screens, review/submit, thank-you with a QR link, reset for
 *        the next visitor. Pure LVGL - the same flow runs in the simulator.
 * @details A kiosk on top of the Workstation, not a page of it: opening it
 *          covers the whole screen (no HOME button for a visitor to find) and
 *          deleting it frees everything it built. The organiser leaves by
 *          holding the top bar of the welcome screen for 3 s.
 *
 *          The questions, the QR link and where answers go are NOT decided
 *          yet. ui_survey_sample_questions is labelled sample content, the
 *          screens say SAMPLE, and the app's submit callback decides what
 *          "saved" means - in this prototype, kept on the device only.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UI_SURVEY_QUESTIONS 3
#define UI_SURVEY_UNANSWERED (-1)

typedef enum {
    UI_SURVEY_RATING = 0,   /* 1..5 as five tap tiles, words under the ends */
    UI_SURVEY_SLIDER,       /* min..max on a slider with big - / + step keys */
    UI_SURVEY_CHOICE,       /* one of up to three labelled tiles */
} ui_survey_kind_t;

typedef struct {
    const char *prompt;        /* the question, wraps to two lines at 20 px */
    const char *short_label;   /* the review screen's row name, ~10 chars */
    ui_survey_kind_t kind;
    int min, max;              /* RATING / SLIDER range */
    const char *low_word;      /* RATING / SLIDER: what min means */
    const char *high_word;     /* RATING / SLIDER: what max means */
    const char *choices[3];    /* CHOICE: tile texts (may hold one '\n') */
} ui_survey_question_t;

typedef enum {
    UI_SURVEY_STEP_WELCOME = 0,
    UI_SURVEY_STEP_Q1,
    UI_SURVEY_STEP_Q2,
    UI_SURVEY_STEP_Q3,
    UI_SURVEY_STEP_REVIEW,
    UI_SURVEY_STEP_THANKS,
} ui_survey_step_t;

/** Submit: the app stores answers[] (UI_SURVEY_QUESTIONS values) and returns
 *  true with a response number, or false if it could not. Called once per
 *  visitor - the flow refuses a second submit until the reset. */
typedef bool (*ui_survey_submit_fn)(const int *answers, uint32_t *response_no, void *ctx);
/** The organiser's 3 s hold on the welcome screen. The app closes the survey. */
typedef void (*ui_survey_exit_fn)(void *ctx);

typedef struct {
    const ui_survey_question_t *questions;   /* UI_SURVEY_QUESTIONS of them */
    const char *qr_url;                      /* what the thank-you QR encodes */
    const char *qr_caption;                  /* printed under it */
    const char *saved_note;                  /* thank-you line saying where answers went */
    ui_survey_submit_fn submit;
    ui_survey_exit_fn exit;
    void *ctx;
    uint32_t idle_reset_s;     /* mid-survey, no touch this long: back to welcome, answers cleared (0 = never) */
    uint32_t thanks_reset_s;   /* thank-you screen returns to welcome after this */
} ui_survey_config_t;

/** Sample content - placeholders until Mike writes the real questions. */
extern const ui_survey_question_t ui_survey_sample_questions[UI_SURVEY_QUESTIONS];

/** Opens the survey full-screen on `scr` at the welcome screen. One at a
 *  time; `cfg` must outlive it. Returns the sheet. */
lv_obj_t *ui_survey_open(lv_obj_t *scr, const ui_survey_config_t *cfg);
/** Deletes everything the survey built (timer included). Safe if not open. */
void ui_survey_close(void);
bool ui_survey_is_open(void);

/** For previews and tests: jump to a step with given answers, as if the
 *  visitor had got there. `save_failed` shows the review screen's
 *  could-not-save notice. */
void ui_survey_preview(ui_survey_step_t step, const int *answers, bool save_failed, uint32_t response_no);

#ifdef __cplusplus
}
#endif
