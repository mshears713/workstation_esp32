/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Backend address + endpoint paths - the one place to change any of
 *        this device's backend targets.
 * @details BACKEND_BASE_URL: update if the ESP32 needs to reach the backend
 *          at a different address - nothing else in handshake_client.c/
 *          audio_capture.c/note_client.c/graph_client.c needs to change.
 *
 *          Must be your development laptop's LAN IP (e.g. 192.168.x.x),
 *          never 127.0.0.1 or "localhost" - those resolve to the ESP32
 *          itself, not your laptop. Run `ipconfig` (Windows) and use the
 *          IPv4 address of your Wi-Fi adapter - the same network the
 *          ESP32 joins via main/wifi_credentials.h. Update this whenever
 *          that address changes (new DHCP lease, different network).
 *
 *          Plain HTTP, no TLS - acceptable for this local learning mission
 *          only. Do not reuse this pattern for anything beyond the LAN.
 *
 *          Not git-ignored (unlike wifi_credentials.h): a LAN IP isn't a
 *          credential, and keeping it a normal tracked file is what makes
 *          it "change in one place" rather than "regenerate a local-only
 *          file after every clone."
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
 *          audio). The backend transcribes it, checks audio reliability,
 *          then runs it through the entry-architect/Notion/semantic-
 *          verifier pipeline (Sources + Van Build Log) - this firmware
 *          does not poll for that result, same no-polling contract as the
 *          other two upload paths.
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
 */
#pragma once

#define BACKEND_BASE_URL "http://10.0.0.187:8000"

#define NOTE_UPLOAD_PATH "/api/v1/notes"

#define VOICE_INBOX_UPLOAD_PATH "/api/v1/voice-inbox"

#define GRAPH_TRIGGER_PATH "/runs"

#define ENTRY_UPLOAD_PATH "/api/v1/entries"

#define NOTIFICATIONS_BASE_PATH "/api/v1/notifications"
