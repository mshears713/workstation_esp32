#pragma once
#include <stddef.h>
typedef int nvs_handle_t;
typedef int esp_err_t;
#define ESP_OK 0
#define NVS_READONLY 0
#define NVS_READWRITE 1
int nvs_open(const char *name,int mode,nvs_handle_t *out);
int nvs_get_blob(nvs_handle_t h,const char *key,void *out,size_t *size);
int nvs_set_blob(nvs_handle_t h,const char *key,const void *data,size_t size);
int nvs_commit(nvs_handle_t h);
void nvs_close(nvs_handle_t h);
const char *esp_err_to_name(int error);
