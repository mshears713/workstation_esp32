#pragma once
#include <stddef.h>
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_SPIRAM 4
void *heap_caps_calloc(size_t count,size_t size,int caps);
size_t heap_caps_get_free_size(int caps);
size_t heap_caps_get_largest_free_block(int caps);
size_t heap_caps_get_minimum_free_size(int caps);
