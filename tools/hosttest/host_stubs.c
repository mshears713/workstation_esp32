/* Implementations of stubs/host_stubs.h plus the project functions
 * report_client.c calls. Host test only. */
#include "host_stubs.h"
#include "cJSON.h"
#include "backend_health.h"
#include "device_config.h"
#include "firmware_identity.h"
#include "lvgl_psram_alloc.h"
#include "ota_service.h"
#include "wifi_manager.h"

esp_reset_reason_t g_reset = ESP_RST_POWERON;
int g_posts, g_post_len, g_post_json_ok;

const char *esp_err_to_name(esp_err_t e) { return e == ESP_OK ? "ESP_OK" : "ESP_FAIL"; }

/* Tasks: registered as FreeRTOS does - name cut to configMAX_TASK_NAME_LEN-1. */
static const char *const s_device_tasks[] = {
    "taskLVGL", "ota", "report", "backend_health", "catalog", "notif_poll", "remote_poll",
    "audio_worker", "audio_uploader", "voice_detect", "voice_feed", "handshake_worker",
    "tiT", "wifi", "sys_evt", "esp_timer", "ipc0", "ipc1", "IDLE0", "IDLE1",
};
static char s_stored[32][configMAX_TASK_NAME_LEN];

TaskHandle_t xTaskGetHandle(const char *name)
{
    /* Same first line as ESP-IDF 5.5's FreeRTOS-Kernel/tasks.c:2864. */
    configASSERT(strlen(name) < configMAX_TASK_NAME_LEN);
    for (size_t i = 0; i < sizeof(s_device_tasks) / sizeof(s_device_tasks[0]); i++) {
        strncpy(s_stored[i], s_device_tasks[i], configMAX_TASK_NAME_LEN - 1);
        s_stored[i][configMAX_TASK_NAME_LEN - 1] = '\0';
        if (strcmp(s_stored[i], name) == 0) {
            return (TaskHandle_t)(uintptr_t)(i + 1);
        }
    }
    return NULL;
}
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t t) { return 1000 + (UBaseType_t)(uintptr_t)t; }
BaseType_t xTaskCreate(void (*fn)(void *), const char *name, uint32_t stack, void *arg, UBaseType_t prio, TaskHandle_t *out)
{ (void)fn; (void)name; (void)stack; (void)arg; (void)prio; if (out) *out = (TaskHandle_t)1; return pdPASS; }
uint32_t ulTaskNotifyTake(BaseType_t c, TickType_t w) { (void)c; (void)w; return 1; }
void xTaskNotifyGive(TaskHandle_t t) { (void)t; }

void *heap_caps_malloc(size_t n, uint32_t caps) { (void)caps; return malloc(n); }
void *heap_caps_calloc(size_t n, size_t sz, uint32_t caps) { (void)caps; return calloc(n, sz); }
size_t heap_caps_get_free_size(uint32_t caps) { return caps & MALLOC_CAP_SPIRAM ? 16000000 : 60000; }
size_t heap_caps_get_minimum_free_size(uint32_t caps) { return caps & MALLOC_CAP_SPIRAM ? 15000000 : 20000; }
size_t heap_caps_get_largest_free_block(uint32_t caps) { (void)caps; return 30000; }

static vprintf_like_t s_vp;
vprintf_like_t esp_log_set_vprintf(vprintf_like_t f) { vprintf_like_t o = s_vp; s_vp = f; return o; }
static int s_quiet;
void host_log(const char *lvl, const char *tag, const char *fmt, ...)
{
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (!s_quiet) {
        printf("  [%s %s] %s\n", lvl, tag, line);
    }
}

esp_reset_reason_t esp_reset_reason(void) { return g_reset; }
int64_t g_now_us = 200 * 1000000LL;
int64_t esp_timer_get_time(void) { return g_now_us; }
uint32_t esp_rom_crc32_le(uint32_t crc, const uint8_t *buf, uint32_t len)
{
    crc = ~crc;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1));
    }
    return ~crc;
}
bool esp_cpu_compare_and_set(volatile uint32_t *a, uint32_t c, uint32_t v) { if (*a == c) { *a = v; return true; } return false; }
esp_err_t esp_efuse_mac_get_default(uint8_t *mac) { for (int i = 0; i < 6; i++) mac[i] = (uint8_t)(0xa0 + i); return ESP_OK; }

/* NVS: one flat table, "ns/key" */
static struct { char k[40]; uint32_t v; } s_nvs[64];
static int s_nvs_n;
static const char *s_ns[4];
esp_err_t nvs_open(const char *ns, nvs_open_mode_t m, nvs_handle_t *h) { (void)m; for (int i = 0; i < 4; i++) if (!s_ns[i] || !strcmp(s_ns[i], ns)) { s_ns[i] = ns; *h = (nvs_handle_t)i; return ESP_OK; } return ESP_FAIL; }
void nvs_close(nvs_handle_t h) { (void)h; }
static int nvs_find(nvs_handle_t h, const char *key, int create)
{
    char k[40];
    assert(strlen(key) <= 15);   /* NVS_KEY_NAME_MAX_SIZE - 1 */
    snprintf(k, sizeof(k), "%s/%s", s_ns[h], key);
    for (int i = 0; i < s_nvs_n; i++) if (!strcmp(s_nvs[i].k, k)) return i;
    if (!create) return -1;
    strcpy(s_nvs[s_nvs_n].k, k);
    return s_nvs_n++;
}
esp_err_t nvs_get_u32(nvs_handle_t h, const char *key, uint32_t *v) { int i = nvs_find(h, key, 0); if (i < 0) return ESP_ERR_NOT_FOUND; *v = s_nvs[i].v; return ESP_OK; }
esp_err_t nvs_set_u32(nvs_handle_t h, const char *key, uint32_t v) { s_nvs[nvs_find(h, key, 1)].v = v; return ESP_OK; }
esp_err_t nvs_get_u8(nvs_handle_t h, const char *key, uint8_t *v) { uint32_t t; esp_err_t e = nvs_get_u32(h, key, &t); if (!e) *v = (uint8_t)t; return e; }
esp_err_t nvs_commit(nvs_handle_t h) { (void)h; return ESP_OK; }

esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap) { ap->rssi = -61; return ESP_OK; }

/* HTTP: the "backend" parses the body as JSON and answers {"id":"42"}. */
static esp_http_client_config_t s_cfg;
static const char *s_post; static int s_post_n;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *c) { s_cfg = *c; return (void *)&s_cfg; }
esp_err_t esp_http_client_set_header(esp_http_client_handle_t c, const char *k, const char *v) { (void)c; (void)k; (void)v; return ESP_OK; }
esp_err_t esp_http_client_set_post_field(esp_http_client_handle_t c, const char *d, int n) { (void)c; s_post = d; s_post_n = n; return ESP_OK; }
esp_err_t esp_http_client_perform(esp_http_client_handle_t c)
{
    (void)c;
    g_posts++;
    g_post_len = s_post_n;
    cJSON *j = cJSON_ParseWithLength(s_post, (size_t)s_post_n);
    g_post_json_ok = j != NULL;
    if (getenv("HOST_DUMP")) fwrite(s_post, 1, (size_t)s_post_n, stderr);
    cJSON_Delete(j);
    char resp[] = "{\"id\":\"42\"}";
    esp_http_client_event_t ev = { HTTP_EVENT_ON_DATA, resp, (int)strlen(resp) };
    s_cfg.event_handler(&ev);
    return ESP_OK;
}
int esp_http_client_get_status_code(esp_http_client_handle_t c) { (void)c; return g_post_json_ok ? 201 : 422; }
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t c) { (void)c; return ESP_OK; }

/* Project modules report_client.c reads. */
const char *firmware_version(void) { return "1.2.2+host000"; }
const char *firmware_running_partition_label(void) { return "ota_1"; }
bool firmware_is_pending_verify(void) { return false; }
void ota_service_get_status(ota_status_t *o) { memset(o, 0, sizeof(*o)); memset(o->message, '"', sizeof(o->message) - 1); memset(o->desired_version, '\\', sizeof(o->desired_version) - 1); o->checks = 3; }
const char *ota_service_state_label(ota_state_t s) { (void)s; return "idle"; }
void backend_health_get_status(backend_health_status_t *o) { memset(o, 0, sizeof(*o)); o->state = BACKEND_HEALTH_OK; }
void backend_health_get_stats(backend_health_stats_t *o) { memset(o, 0, sizeof(*o)); }
void backend_health_report(bool ok) { (void)ok; }
const char *backend_health_label(const backend_health_status_t *s) { (void)s; return "OK"; }
void wifi_mgr_get_status(wifi_mgr_status_t *o) { memset(o, 0, sizeof(*o)); o->state = WIFI_MGR_ONLINE; strcpy(o->ip_addr, "192.168.100.100"); }
const char *device_config_backend_base_url(void) { return "http://192.168.1.71:8000"; }
const char *device_config_ota_base_url(void) { return "https://192.168.1.71:8443"; }
bool device_config_backend_on_fallback(void) { return false; }
const char *device_config_backend_other_url(void) { return "http://clawbox.local:8000"; }
bool device_config_backend_from_nvs(void) { return false; }
uint32_t lvgl_psram_alloc_fallbacks(void) { return 0; }
uint32_t lvgl_psram_alloc_failures(void) { return 0; }
void host_quiet(int q) { s_quiet = q; }
