#include <cstdint>
#include <gtest/gtest.h>
#include <mempool.h>

TEST(MemPoolBasic, SmallAllocations) {
  void* p1 = allocate(8);
  void* p2 = allocate(100);

  EXPECT_NE(p1, nullptr);
  EXPECT_NE(p2, nullptr);
  EXPECT_NE(p1, p2);

  deallocate(p1);
  deallocate(p2);
}

TEST(MemPoolBasic, Alignment) {
  size_t alignments[] = {16, 32, 64, 128};
  for (size_t align : alignments) {
    void* p = allocate_aligned(128, align);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(p) % align , 0);
    deallocate(p);
  }
}

TEST(MemPoolBasic, LargeAllocation) {
    void* p = allocate(2 * 1024 * 1024); 
    EXPECT_NE(p, nullptr);
    memset(p, 0xAA, 2 * 1024 * 1024);
    deallocate(p);
}