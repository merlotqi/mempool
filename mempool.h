#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void *allocate(size_t size);
void *allocate_aligned(size_t size, size_t align);
void deallocate(void* ptr);

#ifdef __cplusplus
}
#endif