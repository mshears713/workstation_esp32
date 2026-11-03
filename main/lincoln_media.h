#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "bsp/esp-bsp.h"
#define LINCOLN_MEDIA_ROOT BSP_SD_MOUNT_POINT "/dottie/audio/lincoln/lincoln-scottw-two-20261030"
typedef bool (*media_wanted_t)(void);
typedef void (*media_status_t)(const char *message);
bool lincoln_media_ensure(media_wanted_t wanted,media_status_t status);
