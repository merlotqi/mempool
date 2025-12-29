#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void *allocate(size_t size, size_t align = 16);
void deallocate(void* ptr);

#ifdef __cplusplus
}
#endif