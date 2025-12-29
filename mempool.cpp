#include "mempool.h"
#include "mempool-inl.hpp"

void *allocate(size_t size, size_t align) {
  return mempool::MemAllocator::instance().allocate(size, align);
}

void deallocate(void *ptr) {
  mempool::MemAllocator::instance().deallocate(ptr);
}