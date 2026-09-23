/*
 * Host (Linux gcc) stand-ins for the ESP-IDF APIs report_client.c and
 * diag.c use - just enough to compile the real files and run the report
 * JSON builder and the loop guard off-device. See run.sh.
 */
#pragma once
#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* esp_err / attributes */
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NOT_FOUND 0x105
#define RTC_NOINIT_ATTR
const char *esp_err_to_name(esp_err_t e);

/* FreeRTOS, as configured on the device (sdkconfig) */
#define configMAX_TASK_NAME_LEN 16
#define configASSERT(x) assert(x)
#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY 0xffffffffu
typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef uint32_t TickType_t;
typedef void *TaskHandle_t;
typedef struct { int x; } portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED { 0 }
#define portENTER_CRITICAL(m) ((void)(m))
#define portEXIT_CRITICAL(m) ((void)(m))
#define portENTER_CRITICAL_SAFE(m) ((void)(m))
#define portEXIT_CRITICAL_SAFE(m) ((void)(m))
TaskHandle_t xTaskGetHandle(const char *name);
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t t);
BaseType_t xTaskCreate(void (*fn)(void *), const char *name, uint32_t stack, void *arg, UBaseType_t prio, TaskHandle_t *out);
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t wait);
void xTaskNotifyGive(TaskHandle_t t);

/* heap */
#define MALLOC_CAP_INTERNAL (1 << 11)
#define MALLOC_CAP_SPIRAM (1 << 10)
#define MALLOC_CAP_8BIT (1 << 2)
void *heap_caps_malloc(size_t n, uint32_t caps);
void *heap_caps_calloc(size_t n, size_t sz, uint32_t caps);
size_t heap_caps_get_free_size(uint32_t caps);
size_t heap_caps_get_minimum_free_size(uint32_t caps);
size_t heap_caps_get_largest_free_block(uint32_t caps);

/* log */
typedef int (*vprintf_like_t)(const char *, va_list);
vprintf_like_t esp_log_set_vprintf(vprintf_like_t f);
void host_log(const char *lvl, const char *tag, const char *fmt, ...);
#define ESP_LOGE(tag, ...) host_log("E", tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) host_log("W", tag, __VA_ARGS__)
#define ESP_LOGI(tag, ...) host_log("I", tag, __VA_ARGS__)

/* misc system */
typedef enum { ESP_RST_UNKNOWN, ESP_RST_POWERON, ESP_RST_SW, ESP_RST_PANIC, ESP_RST_INT_WDT,
               ESP_RST_TASK_WDT, ESP_RST_WDT, ESP_RST_BROWNOUT, ESP_RST_CPU_LOCKUP,
               ESP_RST_PWR_GLITCH } esp_reset_reason_t;
esp_reset_reason_t esp_reset_reason(void);
int64_t esp_timer_get_time(void);
uint32_t esp_rom_crc32_le(uint32_t crc, const uint8_t *buf, uint32_t len);
bool esp_cpu_compare_and_set(volatile uint32_t *addr, uint32_t cmp, uint32_t val);
esp_err_t esp_efuse_mac_get_default(uint8_t *mac);

/* NVS (in-memory, u8/u32 only) */
typedef uint32_t nvs_handle_t;
typedef enum { NVS_READONLY, NVS_READWRITE } nvs_open_mode_t;
esp_err_t nvs_open(const char *ns, nvs_open_mode_t mode, nvs_handle_t *h);
void nvs_close(nvs_handle_t h);
esp_err_t nvs_get_u32(nvs_handle_t h, const char *key, uint32_t *v);
esp_err_t nvs_set_u32(nvs_handle_t h, const char *key, uint32_t v);
esp_err_t nvs_get_u8(nvs_handle_t h, const char *key, uint8_t *v);
esp_err_t nvs_commit(nvs_handle_t h);

/* wifi */
typedef struct { int8_t rssi; } wifi_ap_record_t;
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap);

/* OTA image state (firmware_identity.h) */
typedef enum { ESP_OTA_IMG_VALID = 2 } esp_ota_img_states_t;

/* HTTP client */
typedef enum { HTTP_EVENT_ON_DATA = 5 } esp_http_client_event_id_t;
typedef enum { HTTP_METHOD_POST = 1 } esp_http_client_method_t;
typedef struct { esp_http_client_event_id_t event_id; void *data; int data_len; } esp_http_client_event_t;
typedef esp_err_t (*http_event_handle_cb)(esp_http_client_event_t *evt);
typedef struct { const char *url; esp_http_client_method_t method; int timeout_ms; http_event_handle_cb event_handler; } esp_http_client_config_t;
typedef void *esp_http_client_handle_t;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *c);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t c, const char *k, const char *v);
esp_err_t esp_http_client_set_post_field(esp_http_client_handle_t c, const char *d, int len);
esp_err_t esp_http_client_perform(esp_http_client_handle_t c);
int esp_http_client_get_status_code(esp_http_client_handle_t c);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t c);
