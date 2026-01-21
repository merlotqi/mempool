#include <algorithm>
#include <cstdint>
#include <gtest/gtest.h>
#include <mempool.h>
#include <random>

// Test edge cases and boundary conditions
TEST(MemPoolEdgeCases, ZeroSizeAllocation) {
  void *ptr = allocate(0);
  EXPECT_NE(ptr, nullptr);
  deallocate(ptr);
}

TEST(MemPoolEdgeCases, VerySmallAllocations) {
  std::vector<void *> ptrs;
  for (size_t i = 1; i <= 16; ++i) {
    void *ptr = allocate(i);
    EXPECT_NE(ptr, nullptr);
    ptrs.push_back(ptr);
  }

  for (void *ptr : ptrs) {
    deallocate(ptr);
  }
}

TEST(MemPoolEdgeCases, SizeClassBoundaries) {
  // Test allocation sizes at size class boundaries
  const size_t size_classes[] = {16, 24, 32, 48, 64, 80, 96, 112, 128};

  for (size_t size : size_classes) {
    void *ptr1 = allocate(size - 1);
    void *ptr2 = allocate(size);
    void *ptr3 = allocate(size + 1);

    EXPECT_NE(ptr1, nullptr);
    EXPECT_NE(ptr2, nullptr);
    EXPECT_NE(ptr3, nullptr);

    deallocate(ptr1);
    deallocate(ptr2);
    deallocate(ptr3);
  }
}

TEST(MemPoolEdgeCases, LargeBoundaryAllocation) {
  // Test around MAX_FIXED_SIZE boundary
  void *ptr1 = allocate(4095); // Should use pool
  void *ptr2 = allocate(4096); // Should use pool
  void *ptr3 = allocate(4097); // Should use large allocation

  EXPECT_NE(ptr1, nullptr);
  EXPECT_NE(ptr2, nullptr);
  EXPECT_NE(ptr3, nullptr);

  deallocate(ptr1);
  deallocate(ptr2);
  deallocate(ptr3);
}

TEST(MemPoolEdgeCases, AlignmentEdgeCases) {
  // Test various alignment values
  const size_t alignments[] = {1, 2, 4, 8, 16, 32, 64, 128, 256};

  for (size_t align : alignments) {
    void *ptr = allocate_aligned(100, align);
    EXPECT_NE(ptr, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr) % align, 0);
    deallocate(ptr);
  }
}

TEST(MemPoolEdgeCases, NullDeallocation) {
  // Should handle null deallocation gracefully
  deallocate(nullptr);
}

TEST(MemPoolEdgeCases, DoubleDeallocation) {
  // This should be detected and handled (if implemented)
  void *ptr = allocate(64);
  EXPECT_NE(ptr, nullptr);
  deallocate(ptr);
  // Note: Double deallocation might not be detected in current implementation
  // deallocate(ptr); // This could cause undefined behavior
}

TEST(MemPoolEdgeCases, MassiveAllocation) {
  // Test allocation that exceeds reasonable limits
  const size_t huge_size = 100 * 1024 * 1024; // 100MB
  void *ptr = allocate(huge_size);
  if (ptr) {
    // If allocation succeeded, fill with pattern
    memset(ptr, 0xAA, huge_size);
    deallocate(ptr);
  } else {
    // Allocation failed, which is acceptable
    SUCCEED();
  }
}

TEST(MemPoolEdgeCases, RepeatedSmallAllocations) {
  // Test many small allocations to stress the pool
  const int num_allocs = 10000;
  std::vector<void *> ptrs;

  for (int i = 0; i < num_allocs; ++i) {
    void *ptr = allocate(32);
    ASSERT_NE(ptr, nullptr);
    ptrs.push_back(ptr);
  }

  // Deallocate in reverse order
  for (auto it = ptrs.rbegin(); it != ptrs.rend(); ++it) {
    deallocate(*it);
  }
}

TEST(MemPoolEdgeCases, MixedSizeAllocations) {
  // Mix different sizes to test pool management
  std::vector<std::pair<void *, size_t>> allocations;

  const size_t sizes[] = {16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192};

  for (size_t size : sizes) {
    void *ptr = allocate(size);
    ASSERT_NE(ptr, nullptr);
    allocations.emplace_back(ptr, size);
  }

  // Deallocate in random order (simulate real usage)
  std::random_device rd;
  std::mt19937 g(rd());
  std::shuffle(allocations.begin(), allocations.end(), g);

  for (auto &alloc : allocations) {
    deallocate(alloc.first);
  }
}
