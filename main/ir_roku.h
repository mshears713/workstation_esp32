/*
 * ir_roku -- Roku TV IR transmit for the ESP32-S3-BOX-3 + SENSOR accessory.
 * SPDX-License-Identifier: CC0-1.0
 *
 * All the hardware/protocol findings from the Phase 0 spike live in
 * ir_roku.c. Callers just init, then send key codes.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/*
 * Roku TV NEC extended address, LSB-first (the form ESP-IDF's NEC encoder
 * wants). Equivalent to the 0x57E3 seen in MSB-first code tables.
 * Confirmed working against the ONN Roku TV in Phase 0.
 */
#define ROKU_NEC_ADDRESS  (0xC7EA)

/*
 * Command bytes, from a real captured Onn Roku TV remote
 * (Flipper-IRDB TVs/Onn/Onn_Roku_TV.ir).
 *
 * CONFIRMED against the actual ONN panel:
 *   POWER                     -- Phase 0 go/no-go.
 *   UP, DOWN, LEFT, RIGHT, OK -- implicitly but conclusively: these drove the
 *                                TV's setup screens end to end, including the
 *                                on-screen keyboard used to type the wifi
 *                                password. Setup cannot be completed without
 *                                all five working.
 *   BACK                      -- almost certainly exercised during setup.
 *
 * This also settles the labelling ambiguity that worried us: the capture
 * calls 0x1E Left and 0x66 Back, while the Roku *player* table calls 0x66
 * Left. Since the on-screen keyboard needs real left/right movement and it
 * worked, the Onn labelling above is the correct one for this TV.
 *
 * STILL UNVERIFIED (nothing in setup would have exercised them): HOME,
 * VOL_UP, VOL_DOWN, MUTE, STAR, REPLAY, REWIND, PLAY_PAUSE, FORWARD, SLEEP,
 * POWER_ALT. Same address and same capture, so they are likely fine --
 * correct them here if any turns out wrong.
 */
typedef enum {
    ROKU_KEY_POWER      = 0x17,
    ROKU_KEY_HOME       = 0x03,
    ROKU_KEY_BACK       = 0x66,
    ROKU_KEY_UP         = 0x19,
    ROKU_KEY_DOWN       = 0x33,
    ROKU_KEY_LEFT       = 0x1E,
    ROKU_KEY_RIGHT      = 0x2D,
    ROKU_KEY_OK         = 0x2A,
    ROKU_KEY_REPLAY     = 0x78,
    ROKU_KEY_STAR       = 0x61,
    ROKU_KEY_REWIND     = 0x34,
    ROKU_KEY_PLAY_PAUSE = 0x4C,
    ROKU_KEY_FORWARD    = 0x55,
    ROKU_KEY_VOL_UP     = 0x0F,
    ROKU_KEY_VOL_DOWN   = 0x10,
    ROKU_KEY_MUTE       = 0x20,
    ROKU_KEY_SLEEP      = 0x62,
    /* Documented alternate power byte, in case 0x17 ever stops working. */
    ROKU_KEY_POWER_ALT  = 0x97,
} roku_key_t;

/* Powers the IR rail and brings up the RMT TX/RX channels. */
esp_err_t roku_ir_init(void);

/* Transmits one NEC frame. Blocks until the frame is off the wire. */
esp_err_t roku_ir_send(uint8_t cmd);

/*
 * Loopback self-test: transmit one frame and listen for it on the SENSOR's
 * own IR receiver. True means the emitter is provably firing -- no phone
 * camera, no TV needed. Worth running at every boot; it is the check that
 * distinguishes "emitter dead" from "TV not listening".
 */
bool roku_ir_selftest(void);

/* Human-readable name for a command byte, or "?" if unknown. */
const char *roku_key_name(uint8_t cmd);
