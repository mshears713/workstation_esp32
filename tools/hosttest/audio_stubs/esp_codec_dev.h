#pragma once
#include <stdint.h>
typedef void *esp_codec_dev_handle_t;
#define ESP_CODEC_DEV_OK 0
typedef struct { int bits_per_sample,channel,channel_mask,sample_rate,mclk_multiple; } esp_codec_dev_sample_info_t;
int esp_codec_dev_set_out_vol(esp_codec_dev_handle_t dev,int volume);
int esp_codec_dev_open(esp_codec_dev_handle_t dev,esp_codec_dev_sample_info_t *info);
int esp_codec_dev_write(esp_codec_dev_handle_t dev,void *data,int bytes);
int esp_codec_dev_close(esp_codec_dev_handle_t dev);
