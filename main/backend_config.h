/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Backend address + endpoint paths - the one place to change any of
 *        this device's backend targets.
 * @details BACKEND_BASE_URL is no longer a literal. It expands to a call
 *          into device_config.h, which reads the address from NVS at boot
 *          and falls back to a compiled default. The address of the backend
 *          is now operational state the device carries, not a build-time
 *          constant, so relocating the server - which is exactly what this
 *          migration did, from a laptop onto CLAWBOX - no longer requires a
 *          USB reflash. See device_config.h for the precedence rules and for
 *          why a change takes effect on the next boot rather than instantly.
 *
 *          Every client below still writes `BACKEND_BASE_URL` exactly as it
 *          did before; only the definition moved. The one thing that is no
 *          longer legal is string-literal concatenation ("x" BACKEND_BASE_URL
 *          "y"), which nothing here does.
 *
 *          Plain HTTP, no TLS, on a trusted LAN. That is unchanged by this
 *          migration and is a deliberate scope boundary: firmware images are
 *          fetched over a separate, certificate-pinned HTTPS channel (see
 *          device_config_ota_base_url() and ota_service.c), because
 *          installing code warrants a guarantee that fetching a notification
 *          does not. Application traffic here is still readable by anything
 *          on the same network.
 *
 *          NOTE_UPLOAD_PATH: where a voice-triggered SEND recording gets
 *          POSTed once it finishes - see note_client.c. multipart/form-data:
 *          request_id, source, duration_seconds, sample_rate_hz, and an
 *          `audio` file part (a real WAV, header included). 202 (new) or
 *          200 (duplicate request_id, idempotent) mean accepted;
 *          transcription/interpretation happens in the backend afterward -
 *          this firmware does not poll for that result.
 *
 *          VOICE_INBOX_UPLOAD_PATH: where a voice-triggered NOTE recording
 *          gets POSTed once it finishes - see voice_inbox_client.c. Same
 *          multipart/form-data shape as NOTE_UPLOAD_PATH above, but this is
 *          a completely different backend destination: the backend
 *          transcribes it and creates a page directly in a Notion "Voice
 *          Inbox" database rather than running it through the local
 *          LangGraph pipeline. Same 202/200 semantics, same
 *          no-polling-for-the-result behavior.
 *
 *          GRAPH_TRIGGER_PATH: where GO's old one-shot design-review graph
 *          trigger POSTs - see graph_client.c. POST {request_id} -> 202
 *          (new run queued) / 200 (duplicate request_id) / 409 (another run
 *          already active), body {run_id, request_id, status, created_at,
 *          duplicate}. Kept dormant, not deleted: voice_control.c's GO
 *          branch no longer calls trigger_graph_run() (see
 *          ENTRY_UPLOAD_PATH below), but graph_client.c and the backend's
 *          /runs + design-review graph still work if called directly.
 *
 *          ENTRY_UPLOAD_PATH: where a voice-triggered GO recording gets
 *          POSTed once it finishes - see entry_client.c. Same multipart/
 *          form-data shape as NOTE_UPLOAD_PATH/VOICE_INBOX_UPLOAD_PATH
 *          above (request_id, source, duration_seconds, sample_rate_hz,
 *          audio), plus one field the other two don't send: project_hint
 *          ("van1"/"van2"/"general"/"none" - see project_selector.h), Mike's
 *          VAN1/VAN2/GEN/NONE button selection at record time. The backend
 *          transcribes it, checks audio reliability, then runs it through
 *          the entry-architect/Notion/semantic-verifier pipeline (Sources +
 *          Van Build Log) - project_hint tells the architect which
 *          van_or_scope to use outright instead of inferring it, and "none"
 *          tells it this recording isn't part of the van-build project at
 *          all. This firmware does not poll for the result, same
 *          no-polling contract as the other two upload paths.
 *
 *          NOTIFICATIONS_BASE_PATH: root of the low-confidence spoken-
 *          notification resource - see notification_client.c.
 *          GET {base}/pending -> {pending, count, notification_id,
 *          created_at}, polled lightly in the background to drive the
 *          listening ring's blue/orange color. GET {base}/{id}/audio ->
 *          raw 24kHz/16-bit/mono PCM (no WAV header - see the backend's
 *          app/voice/tts.py), fetched only after the wake word + "yes".
 *          POST {base}/{id}/ack -> marks it delivered. Notifications are
 *          created by the backend's own low-confidence-transcript pipeline,
 *          never by this device - there is no upload/create endpoint here.
 *
 *          REMOTE_BASE_PATH: root of the wireless Roku IR remote command
 *          queue - see remote_client.c. GET {base}/pending -> {pending,
 *          count, command_id, key, created_at}, polled aggressively (every
 *          ~150ms, not lightly like notifications) since this drives live
 *          TV navigation. POST {base}/{id}/ack -> drains the command after
 *          it's been sent (or found unrecognized). Commands are created by
 *          the PC-side control script's POST {base}/keys/{key} - see
 *          roku-ir-remote/remote_wifi.ps1 - never by this device.
 */
#pragma once

#include "device_config.h"

/* Resolves at run time to the NVS-stored address, or to
 * DEVICE_CONFIG_DEFAULT_BACKEND_URL when nothing is stored. Expands to a
 * `const char *`: valid as a "%s" argument, not as part of a literal. */
#define BACKEND_BASE_URL (device_config_backend_base_url())

/* Chunk uploads run while the recording is still in progress and must fit,
 * retries included, inside audio_capture.c's one spare buffer of slack
 * (AUDIO_STREAM_CHUNK_MS). See stream_upload.c's UPLOAD_CHUNK_MAX_ATTEMPTS.
 * The per-client *_UPLOAD_TIMEOUT_MS values still apply to finish/cancel,
 * which run after the mic is closed and so cost no audio. */
#define CHUNK_UPLOAD_TIMEOUT_MS 5000

/* Liveness probe for the backend itself, polled by backend_health.c. This
 * is deliberately not the handshake endpoint: the handshake ships
 * accelerometer samples and runs a real pipeline, whereas /health returns
 * a constant and costs the backend nothing, so it is safe to call on a
 * timer forever. */
#define HEALTH_PATH "/health"

#define NOTE_UPLOAD_PATH "/api/v1/notes"

#define VOICE_INBOX_UPLOAD_PATH "/api/v1/voice-inbox"

#define GRAPH_TRIGGER_PATH "/runs"

#define ENTRY_UPLOAD_PATH "/api/v1/entries"

/* GO's destination: chunk/finish exactly like the other kinds, but /finish
 * transcribes and creates the GitHub issue before it answers, so the device
 * gets the issue number back instead of having to poll. */
#define ISSUE_UPLOAD_PATH "/api/v1/issues"

/* The approved projects and repositories the device may select, so adding a
 * target is a backend edit rather than a reflash. */
#define PROJECTS_PATH "/api/v1/projects"

#define NOTIFICATIONS_BASE_PATH "/api/v1/notifications"

#define REMOTE_BASE_PATH "/api/v1/remote"
