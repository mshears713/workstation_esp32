/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Projects and repositories fetched from the backend - see
 *        backend_catalog.h.
 */

#include "backend_catalog.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "nvs.h"
#include "cJSON.h"

#include "wifi_manager.h"
#include "voice_control.h"

#if __has_include("backend_config.h")
#include "backend_config.h"
#else
#error "main/backend_config.h is required - see backend_config.h.example"
#endif

static const char *TAG = "backend_catalog";

/* Retry fast until the first catalog lands, then settle into a slow refresh.
 * The refresh is the Mission 19 change: this used to fetch once and stop,
 * which was right when the list only changed if someone edited a file on the
 * backend. It now also changes when a project is created in the AI-OS, and
 * having to reboot the workstation to see it would defeat the point.
 *
 * Five minutes is chosen against how the list is actually used, not how fast
 * it could change: nobody starts a project and walks straight to the bench
 * expecting it to already be there, and a GET of about a kilobyte every five
 * minutes is nothing next to a capture upload. */
#define FETCH_RETRY_INTERVAL_MS 10000
#define REFRESH_INTERVAL_MS (5 * 60 * 1000)
#define FETCH_TIMEOUT_MS 5000

/* The backend caps its own payload well under this. Each project costs about
 * 100 bytes (32-char id, 12-char label, 40-char cue, plus JSON), so twelve
 * projects and twelve repos is roughly 1.8KB. Sized with headroom and
 * hard-capped so a runaway response cannot exhaust the heap.
 *
 * At exactly 4096 this allocation lands in PSRAM rather than internal RAM
 * (CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096), which is deliberate - internal
 * heap is the scarce one here and this buffer has no reason to be in it. */
#define RESPONSE_BUF_LEN 4096

/* Where the last good catalog is kept so a boot with the backend down still
 * offers the list it last saw. The raw response body is stored rather than
 * the parsed entries: it is what the parser is already known to handle, so
 * the cached path and the network path cannot drift apart. */
#define CATALOG_NVS_NAMESPACE "wkstn"
#define CATALOG_NVS_KEY "catalog"

typedef struct {
    char id[CATALOG_ID_LEN];
    char label[CATALOG_LABEL_LEN];
    char cue[CATALOG_CUE_LEN];
} catalog_entry_t;

typedef struct {
    catalog_entry_t items[CATALOG_MAX_COUNT];
    volatile int count;
    volatile int index;
} catalog_list_t;

/* Written by the fetch task, read by the LVGL task and by the upload path.
 * count is published last and read first, so a reader either sees the old
 * count with consistent entries or the new count with fully written ones -
 * it never walks half-populated storage.
 *
 * That ordering does not make a refresh atomic: a reader could still catch a
 * label mid-rewrite. Two things keep that from mattering. The entries are
 * only rewritten when the response actually differs from the one already
 * published, which is rare - the catalog is configuration, not telemetry.
 * And a refresh is deferred entirely while a capture is in flight (see
 * capture_in_flight()), because the one read whose result is consequential is
 * the id sent at the end of a capture, and a wrong id there means a note
 * attached to the wrong project or an issue filed against the wrong
 * repository. Everything else that reads this only draws a label. */
static catalog_list_t s_projects;
static catalog_list_t s_repos;
static volatile bool s_ready = false;

/* Hash of the published response body, used to skip a rewrite when nothing
 * changed. FNV-1a: not cryptographic, and does not need to be - a collision
 * means a refresh is missed until the next one five minutes later. */
static uint32_t s_body_hash = 0;

/* One "nothing changed" line per boot, not one every five minutes. */
static bool s_logged_unchanged = false;

static uint32_t fnv1a(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; s++) {
        h = (h ^ (uint8_t)*s) * 16777619u;
    }
    return h;
}

/* ---- Accessors ------------------------------------------------------ */

static const char *list_label(const catalog_list_t *list)
{
    int count = list->count;
    if (count <= 0) {
        /* Two different states, two different words: the operator should be
         * able to tell "still fetching" from "the catalog is empty". */
        return s_ready ? "NONE" : "...";
    }
    int idx = list->index;
    if (idx < 0 || idx >= count) {
        idx = 0;
    }
    return list->items[idx].label;
}

static const char *list_id(const catalog_list_t *list)
{
    int count = list->count;
    if (count <= 0) {
        return "";
    }
    int idx = list->index;
    if (idx < 0 || idx >= count) {
        idx = 0;
    }
    return list->items[idx].id;
}

static void list_next(catalog_list_t *list)
{
    int count = list->count;
    if (count > 0) {
        list->index = (list->index + 1) % count;
    }
}

static void list_prev(catalog_list_t *list)
{
    int count = list->count;
    if (count > 0) {
        list->index = (list->index + count - 1) % count;
    }
}

bool backend_catalog_ready(void) { return s_ready; }

int backend_catalog_project_count(void) { return s_projects.count; }
const char *backend_catalog_project_label(void) { return list_label(&s_projects); }
const char *backend_catalog_project_id(void) { return list_id(&s_projects); }
void backend_catalog_project_next(void) { list_next(&s_projects); }
void backend_catalog_project_prev(void) { list_prev(&s_projects); }

const char *backend_catalog_project_cue(void)
{
    int count = s_projects.count;
    if (count <= 0) {
        return "";
    }
    int idx = s_projects.index;
    if (idx < 0 || idx >= count) {
        idx = 0;
    }
    return s_projects.items[idx].cue;
}

int backend_catalog_repo_count(void) { return s_repos.count; }
const char *backend_catalog_repo_label(void) { return list_label(&s_repos); }
const char *backend_catalog_repo_id(void) { return list_id(&s_repos); }
void backend_catalog_repo_next(void) { list_next(&s_repos); }
void backend_catalog_repo_prev(void) { list_prev(&s_repos); }

/* ---- Parsing -------------------------------------------------------- */

/* Returns the number of entries stored, or -1 if the array is missing or
 * malformed. Entries that do not fit the id/label fields are skipped rather
 * than truncated: a half-written id would resolve to nothing on the backend,
 * so silently sending one would turn a config typo into a confusing runtime
 * failure. An over-long cue is truncated instead, because a clipped memory
 * hook still helps and nothing is sent back from it. */
static int parse_list(const cJSON *root, const char *key, catalog_list_t *out)
{
    const cJSON *array = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsArray(array)) {
        ESP_LOGW(TAG, "catalog response has no %s array", key);
        return -1;
    }

    /* Remembered so the selection survives a refresh. Re-finding the id is
     * the point: the operator may have chosen a project minutes ago, and
     * silently moving that selection because the list grew would attach a
     * note to whatever happened to shift into the same slot. */
    char previous_id[CATALOG_ID_LEN];
    strncpy(previous_id, list_id(out), sizeof(previous_id) - 1);
    previous_id[sizeof(previous_id) - 1] = '\0';

    int stored = 0;
    const cJSON *entry = NULL;
    cJSON_ArrayForEach(entry, array) {
        if (stored >= CATALOG_MAX_COUNT) {
            ESP_LOGW(TAG, "catalog has more than %d %s - ignoring the rest", CATALOG_MAX_COUNT, key);
            break;
        }
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(entry, "id");
        const cJSON *label = cJSON_GetObjectItemCaseSensitive(entry, "label");
        if (!cJSON_IsString(id) || !cJSON_IsString(label) || !id->valuestring || !label->valuestring) {
            continue;
        }
        if (strlen(id->valuestring) >= CATALOG_ID_LEN || strlen(label->valuestring) >= CATALOG_LABEL_LEN) {
            ESP_LOGW(TAG, "skipping oversized %s entry: id=%s label=%s",
                     key, id->valuestring, label->valuestring);
            continue;
        }
        strncpy(out->items[stored].id, id->valuestring, CATALOG_ID_LEN - 1);
        out->items[stored].id[CATALOG_ID_LEN - 1] = '\0';
        strncpy(out->items[stored].label, label->valuestring, CATALOG_LABEL_LEN - 1);
        out->items[stored].label[CATALOG_LABEL_LEN - 1] = '\0';

        const cJSON *cue = cJSON_GetObjectItemCaseSensitive(entry, "cue");
        if (cJSON_IsString(cue) && cue->valuestring) {
            strncpy(out->items[stored].cue, cue->valuestring, CATALOG_CUE_LEN - 1);
            out->items[stored].cue[CATALOG_CUE_LEN - 1] = '\0';
        } else {
            out->items[stored].cue[0] = '\0'; /* repos have no cue, and a project may not either */
        }
        stored++;
    }

    /* Count published last - see the note on the statics above. */
    int restored = 0;
    if (previous_id[0] != '\0') {
        for (int i = 0; i < stored; i++) {
            if (strcmp(out->items[i].id, previous_id) == 0) {
                restored = i;
                break;
            }
        }
    }
    out->index = restored;
    out->count = stored;
    return stored;
}

/* Returns true if the body parsed into at least a projects or a repos array.
 * A response that parses but contains neither is treated as a failure rather
 * than as an empty catalog: an empty list is a real state the backend can
 * report, but it says "repos": [] to do it. */
static bool publish_catalog(const char *body, const char *origin)
{
    cJSON *root = cJSON_Parse(body);
    if (!root) {
        ESP_LOGW(TAG, "%s catalog was not valid JSON", origin);
        return false;
    }

    int projects = parse_list(root, "projects", &s_projects);
    int repos = parse_list(root, "repos", &s_repos);
    cJSON_Delete(root);

    if (projects < 0 && repos < 0) {
        return false;
    }
    s_ready = true;
    s_body_hash = fnv1a(body);
    ESP_LOGI(TAG, "catalog from %s: %d project(s), %d repo(s)", origin,
             projects < 0 ? 0 : projects, repos < 0 ? 0 : repos);
    for (int i = 0; i < s_projects.count; i++) {
        ESP_LOGI(TAG, "  project %s (%s) cue=\"%s\"",
                 s_projects.items[i].label, s_projects.items[i].id, s_projects.items[i].cue);
    }
    for (int i = 0; i < s_repos.count; i++) {
        ESP_LOGI(TAG, "  repo    %s (%s)", s_repos.items[i].label, s_repos.items[i].id);
    }
    return true;
}

/* ---- NVS cache ------------------------------------------------------ */

static void cache_store(const char *body)
{
    nvs_handle_t h;
    if (nvs_open(CATALOG_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    if (nvs_set_str(h, CATALOG_NVS_KEY, body) == ESP_OK) {
        nvs_commit(h);
    }
    nvs_close(h);
}

/* Publishes the cached catalog if there is one. Best-effort by design: a
 * missing or unreadable cache is the ordinary first-boot case, not an error
 * worth reporting to the operator, and the network fetch is about to run
 * anyway. */
static void cache_load(void)
{
    nvs_handle_t h;
    if (nvs_open(CATALOG_NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    size_t len = 0;
    if (nvs_get_str(h, CATALOG_NVS_KEY, NULL, &len) == ESP_OK && len > 0 && len <= RESPONSE_BUF_LEN) {
        char *body = malloc(len);
        if (body) {
            if (nvs_get_str(h, CATALOG_NVS_KEY, body, &len) == ESP_OK) {
                publish_catalog(body, "NVS");
            }
            free(body);
        }
    }
    nvs_close(h);
}

/* ---- Fetch ---------------------------------------------------------- */

/* esp_http_client's event handler, accumulating the body. Same pattern as
 * stream_upload.c's response_event_handler. */
typedef struct {
    char *buf;
    size_t len;
    size_t written;
} response_ctx_t;

static esp_err_t response_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id != HTTP_EVENT_ON_DATA || !evt->user_data) {
        return ESP_OK;
    }
    response_ctx_t *ctx = evt->user_data;
    int room = (int)ctx->len - 1 - (int)ctx->written;
    if (room > 0) {
        int copy_len = evt->data_len < room ? evt->data_len : room;
        memcpy(ctx->buf + ctx->written, evt->data, (size_t)copy_len);
        ctx->written += (size_t)copy_len;
        ctx->buf[ctx->written] = '\0';
    }
    return ESP_OK;
}

/* A refresh must not land in the middle of a capture. The id read at the end
 * of a capture decides which project a note is attached to and which
 * repository an issue is filed against; everything else that reads this
 * module only draws a label. Skipping the cycle costs five minutes of
 * staleness and removes the only case where a torn read would matter. */
static bool capture_in_flight(void)
{
    voice_status_t vs;
    voice_control_get_status(&vs);
    return vs.state == VOICE_STATE_SEND_ACTIVE ||
           vs.state == VOICE_STATE_NOTE_ACTIVE ||
           vs.state == VOICE_STATE_GRAPH_ACTIVE;
}

static bool fetch_once(void)
{
    char url[160];
    snprintf(url, sizeof(url), "%s%s", BACKEND_BASE_URL, PROJECTS_PATH);

    char *body = malloc(RESPONSE_BUF_LEN);
    if (!body) {
        return false;
    }
    body[0] = '\0';
    response_ctx_t ctx = { .buf = body, .len = RESPONSE_BUF_LEN, .written = 0 };

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = FETCH_TIMEOUT_MS,
        .event_handler = response_event_handler,
        .user_data = &ctx,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        free(body);
        return false;
    }

    esp_err_t err = esp_http_client_perform(client);
    int status = err == ESP_OK ? esp_http_client_get_status_code(client) : 0;
    esp_http_client_cleanup(client);

    bool ok = false;
    if (err == ESP_OK && status == 200) {
        if (s_ready && fnv1a(body) == s_body_hash) {
            /* Unchanged, which is the usual answer - and the usual answer
             * after a reboot too, because NVS just published the same body.
             * Say so once per boot: otherwise a working fetch and a failing
             * one look identical in the log, which cost a round of
             * head-scratching the first time this ran on hardware. */
            if (!s_logged_unchanged) {
                s_logged_unchanged = true;
                ESP_LOGI(TAG, "catalog fetched from backend, unchanged from the cached one");
            }
            ok = true;
        } else if (publish_catalog(body, "backend")) {
            cache_store(body);
            ok = true;
        }
    } else {
        ESP_LOGW(TAG, "catalog fetch failed: err=%s status=%d", esp_err_to_name(err), status);
    }

    free(body);
    return ok;
}

static void fetch_task(void *arg)
{
    (void)arg;
    bool loaded = false;
    for (;;) {
        wifi_mgr_status_t wst;
        wifi_mgr_get_status(&wst);

        if (wst.state == WIFI_MGR_ONLINE && !capture_in_flight()) {
            if (fetch_once()) {
                loaded = true;
            }
        }
        /* Retry quickly until the first success, then settle. A device that
         * has never reached the backend has nothing to show; one that has is
         * only waiting for a change that may never come. */
        vTaskDelay(pdMS_TO_TICKS(loaded ? REFRESH_INTERVAL_MS : FETCH_RETRY_INTERVAL_MS));
    }
}

void backend_catalog_init(void)
{
    cache_load();
    /* Priority 3, same as backend_health: this is a convenience fetch and
     * must never compete with capture or upload for the Wi-Fi path. */
    xTaskCreate(fetch_task, "catalog", 4096, NULL, 3, NULL);
}
