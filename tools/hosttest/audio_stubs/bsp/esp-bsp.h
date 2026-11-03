#pragma once
#include <stddef.h>
#include <stdbool.h>
#ifndef BSP_SD_MOUNT_POINT
#error "Pass fixture directory as BSP_SD_MOUNT_POINT"
#endif
#define BSP_SD_POWER 43
#define ESP_OK 0
typedef int esp_err_t;
typedef struct { bool format_if_mount_failed; int max_files; size_t allocation_unit_size; } esp_vfs_fat_sdmmc_mount_config_t;
typedef struct { const esp_vfs_fat_sdmmc_mount_config_t *mount; } bsp_sdcard_cfg_t;
int bsp_sdcard_sdmmc_mount(bsp_sdcard_cfg_t *cfg);
int bsp_sdcard_unmount(void);
const char *esp_err_to_name(int err);
