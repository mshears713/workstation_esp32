/* Host test for notification_client.c's json_string_field() - the no-allocation
 * scan that pulls spoken_message out of GET /notifications/{id}. The function
 * is copied out of the firmware file by run.sh (it is static there), so this
 * tests the exact text that ships. */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "json_string_field.inc"

static int fails;
#define CHECK(c, what) do { printf("  %s  %s\n", (c) ? "PASS" : "FAIL", what); if (!(c)) fails++; } while (0)

int main(void)
{
    char out[192];
    const char *real = "{\"notification_id\":\"n_42\",\"status\":\"pending\",\"spoken_message\":"
                       "\"The print finished. Bed is cooling.\",\"transcript\":\"print done\"}";
    CHECK(json_string_field(real, "spoken_message", out, sizeof out) &&
          strcmp(out, "The print finished. Bed is cooling.") == 0, "plain value");
    CHECK(json_string_field("{\"spoken_message\": \"say \\\"hi\\\" now\"}", "spoken_message", out, sizeof out) &&
          strcmp(out, "say \"hi\" now") == 0, "escaped quotes, space after colon");
    CHECK(json_string_field("{\"spoken_message\":\"a\\nb\\u00e9c\"}", "spoken_message", out, sizeof out) &&
          strcmp(out, "a b?c") == 0, "newline -> space, non-ASCII escape -> ?");
    CHECK(!json_string_field("{\"spoken_message\":null}", "spoken_message", out, sizeof out), "null -> not loaded");
    CHECK(!json_string_field("{\"other\":\"x\"}", "spoken_message", out, sizeof out), "missing key");
    CHECK(!json_string_field("{\"spoken_message\":\"cut off mid-str", "spoken_message", out, sizeof out),
          "truncated body -> not loaded");
    char small[8];
    CHECK(json_string_field(real, "spoken_message", small, sizeof small) && strcmp(small, "The pri") == 0,
          "long value truncated to the buffer, still terminated");
    printf(fails ? "NOTIFY TEXT FAIL (%d)\n" : "NOTIFY TEXT PASS\n", fails);
    return fails ? 1 : 0;
}
