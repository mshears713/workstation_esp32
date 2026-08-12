/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Wireless Roku IR remote: background poll of the backend's remote
 *        command queue, firing each one through ir_roku.c - see
 *        remote_client.c.
 * @details Replaces roku-ir-remote's serial-typed keymap (Stage A) with a
 *          wifi-delivered one (Stage B from that project's README), routed
 *          through the same backend the rest of this device already talks
 *          to (see backend_config.h's REMOTE_BASE_PATH) instead of a
 *          dedicated ESP-hosted server - the PC-side control script POSTs a
 *          key name, this module polls for it and blasts it.
 *
 *          Polled much faster than notification_client.c's 5s interval
 *          (REMOTE_POLL_INTERVAL_MS below): this is driving live TV
 *          navigation, where 5s of lag per keypress would be unusable.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** Starts roku_ir_init() and the background poll task. Call once, from any
 * task, after wifi is up. */
void remote_client_init(void);

#ifdef __cplusplus
}
#endif
