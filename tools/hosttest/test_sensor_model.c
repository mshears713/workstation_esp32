/* Host test for the SENSORS page model (main/ui_sensor_model.c): number
 * formats at their extremes, dial positions and pinning, float -> fixed
 * rounding, short ages. See run.sh. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "ui_sensor_model.h"

static int fails;
#define CHECK(c, what) do { printf("  %s  %s\n", (c) ? "PASS" : "FAIL", what); if (!(c)) fails++; } while (0)
#define EQ(got, want) do { CHECK(strcmp((got), (want)) == 0, want); \
    if (strcmp((got), (want)) != 0) printf("        got \"%s\"\n", (got)); } while (0)

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    char b[24];
    int pin;

    printf("S1) values: never clamped, sign kept, fixed decimals\n");
    ui_sens_fmt_value(UI_SENS_ACCEL, 101, b, sizeof b); EQ(b, "1.01");
    ui_sens_fmt_value(UI_SENS_ACCEL, 4, b, sizeof b); EQ(b, "0.04");
    ui_sens_fmt_value(UI_SENS_ACCEL, 346, b, sizeof b); EQ(b, "3.46");
    ui_sens_fmt_value(UI_SENS_TEMP, 763, b, sizeof b); EQ(b, "76.3");
    ui_sens_fmt_value(UI_SENS_TEMP, -400, b, sizeof b); EQ(b, "-40.0");
    ui_sens_fmt_value(UI_SENS_TEMP, -5, b, sizeof b); EQ(b, "-0.5");
    ui_sens_fmt_value(UI_SENS_TEMP, 1850, b, sizeof b); EQ(b, "185.0");
    ui_sens_fmt_value(UI_SENS_HUMI, 100, b, sizeof b); EQ(b, "100");
    ui_sens_fmt_value(UI_SENS_TEMP, INT32_MIN, b, sizeof b); CHECK(strlen(b) > 0 && b[0] == '-', "INT32_MIN: no overflow");

    printf("S2) dial positions: 1 g at the top, pinning at both ends\n");
    CHECK(ui_sens_dial_pos(UI_SENS_ACCEL, 100, &pin) == 500 && pin == 0, "1.00 g -> 500 (top of the sweep)");
    CHECK(ui_sens_dial_pos(UI_SENS_ACCEL, 200, &pin) == 1000 && pin == 0, "2.00 g -> end, not pinned");
    CHECK(ui_sens_dial_pos(UI_SENS_ACCEL, 201, &pin) == 1000 && pin == 1, "2.01 g -> pinned high");
    CHECK(ui_sens_dial_pos(UI_SENS_TEMP, 600, &pin) == 0 && pin == 0, "60.0 F -> start");
    CHECK(ui_sens_dial_pos(UI_SENS_TEMP, 599, &pin) == 0 && pin == -1, "59.9 F -> pinned low");
    CHECK(ui_sens_dial_pos(UI_SENS_TEMP, 750, &pin) == 500 && pin == 0, "75.0 F -> middle");
    CHECK(ui_sens_dial_pos(UI_SENS_HUMI, 48, NULL) == 480, "48 % -> 480, NULL pinned ok");

    printf("S3) float -> fixed, rounded half away from zero\n");
    CHECK(ui_sens_fixed(UI_SENS_ACCEL, 1.005f) == 101 || ui_sens_fixed(UI_SENS_ACCEL, 1.005f) == 100,
          "1.005 g -> 100/101 (float edge)");
    CHECK(ui_sens_fixed(UI_SENS_ACCEL, 0.98f) == 98, "0.98 g -> 98");
    CHECK(ui_sens_fixed(UI_SENS_TEMP, 76.34f) == 763, "76.34 F -> 763");
    CHECK(ui_sens_fixed(UI_SENS_TEMP, -40.0f) == -400, "-40 F -> -400");
    CHECK(ui_sens_fixed(UI_SENS_TEMP, -0.06f) == -1, "-0.06 F -> -1");
    CHECK(ui_sens_fixed(UI_SENS_HUMI, 47.5f) == 48, "47.5 % -> 48");

    printf("S4) short ages fit under a 92 px dial\n");
    ui_sens_fmt_age(0, b, sizeof b); EQ(b, "<1 s");
    ui_sens_fmt_age(4200, b, sizeof b); EQ(b, "4 s");
    ui_sens_fmt_age(2520000, b, sizeof b); EQ(b, "42 m");
    ui_sens_fmt_age(3ull * 3600000 + 59 * 60000, b, sizeof b); EQ(b, "3 h");
    ui_sens_fmt_age(1987200000ull, b, sizeof b); EQ(b, "23 d");
    ui_sens_fmt_age(UINT64_MAX, b, sizeof b); CHECK(strlen(b) > 0 && strlen(b) < sizeof b, "uint64 max: no overflow");

    printf(fails ? "SENSOR MODEL FAIL (%d)\n" : "SENSOR MODEL PASS\n", fails);
    return fails ? 1 : 0;
}
