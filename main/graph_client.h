/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Mission 13 - GO: one-shot backend graph-trigger request.
 * @details One blocking function, same shape as note_client.c's
 *          note_client_submit() - no worker task/queue, since the caller
 *          (voice_control.c) already blocks the same way for NOTE and GO's
 *          own directive holds the result on screen for 5s regardless.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Fires one graph-trigger POST and blocks until it completes or times out.
 * On success, result_out is a short human-readable outcome ("GRAPH
 * ACCEPTED") and this returns true. On failure, result_out says why
 * ("CONNECTION FAILED", "TIMEOUT", "ENDPOINT NOT SET", "REQUEST REJECTED
 * 500") and this returns false. If GRAPH_TRIGGER_PATH (backend_config.h) is
 * empty, returns false immediately without attempting a request.
 */
bool trigger_graph_run(const char *request_id, char *result_out, size_t result_out_len);

#ifdef __cplusplus
}
#endif
