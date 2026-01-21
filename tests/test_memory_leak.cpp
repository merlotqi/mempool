#include <chrono>
#include <gtest/gtest.h>
#include <mempool.h>
#include <thread>
#include <vector>

// Test memory leak detection and pool statistics
TEST(MemPoolMemoryLeak, LeakDetectionSimulation) {
  // This test simulates memory leaks to see if they can be detected
  // In a real implementation, you might want to track allocations

  std::vector<void *> leaked_ptrs;

  // Allocate many objects but "forget" to deallocate some
  for (int i = 0; i < 1000; ++i) {
    void *ptr = allocate(64);
    ASSERT_NE(ptr, nullptr);

    // Simulate leaking every 10th allocation
    if (i % 10 != 0) {
      leaked_ptrs.push_back(ptr);
    }
    // else: leak this pointer intentionally for testing
  }

  // Deallocate the non-leaked ones
  for (void *ptr : leaked_ptrs) {
    deallocate(ptr);
  }

  // Note: In current implementation, there's no built-in leak detection
  // This test serves as a placeholder for when leak detection is added
  SUCCEED();
}

TEST(MemPoolMemoryLeak, ThreadLocalLeaks) {
  // Test that thread-local allocations are properly cleaned up
  std::vector<std::thread> threads;

  for (int t = 0; t < 4; ++t) {
    threads.emplace_back([]() {
      std::vector<void *> ptrs;
      for (int i = 0; i < 100; ++i) {
        void *ptr = allocate(32);
        ASSERT_NE(ptr, nullptr);
        ptrs.push_back(ptr);
      }

      // Deallocate all - simulating good cleanup
      for (void *ptr : ptrs) {
        deallocate(ptr);
      }
    });
  }

  for (auto &t : threads) {
    t.join();
  }

  SUCCEED();
}

TEST(MemPoolMemoryLeak, CrossThreadCleanup) {
  // Test cleanup when allocations happen in one thread but cleanup in another
  std::vector<void *> shared_ptrs;
  std::mutex mtx;

  std::thread producer([&]() {
    for (int i = 0; i < 500; ++i) {
      void *ptr = allocate(64);
      ASSERT_NE(ptr, nullptr);

      std::lock_guard<std::mutex> lock(mtx);
      shared_ptrs.push_back(ptr);
    }
  });

  std::thread consumer([&]() {
    // Small delay to let producer get some allocations
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    for (int i = 0; i < 500; ++i) {
      void *ptr = nullptr;
      {
        std::lock_guard<std::mutex> lock(mtx);
        if (!shared_ptrs.empty()) {
          ptr = shared_ptrs.back();
          shared_ptrs.pop_back();
        }
      }

      if (ptr) {
        deallocate(ptr);
      } else {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
        i--; // Try again
      }
    }
  });

  producer.join();
  consumer.join();

  // Clean up any remaining
  for (void *ptr : shared_ptrs) {
    deallocate(ptr);
  }

  SUCCEED();
}

TEST(MemPoolMemoryLeak, LargeObjectLeaks) {
  // Test leaking large objects
  std::vector<void *> large_ptrs;

  for (int i = 0; i < 10; ++i) {
    // Allocate objects larger than MAX_FIXED_SIZE
    void *ptr = allocate(500 * 1024); // 500KB
    ASSERT_NE(ptr, nullptr);

    // Leak some of them
    if (i % 3 != 0) {
      large_ptrs.push_back(ptr);
    }
  }

  // Clean up non-leaked ones
  for (void *ptr : large_ptrs) {
    deallocate(ptr);
  }

  SUCCEED();
}

TEST(MemPoolMemoryLeak, AllocationPatternStress) {
  // Stress test with various allocation patterns that might cause leaks

  // Pattern 1: Allocate in bursts, deallocate partially
  for (int burst = 0; burst < 10; ++burst) {
    std::vector<void *> burst_ptrs;
    for (int i = 0; i < 100; ++i) {
      void *ptr = allocate(48);
      ASSERT_NE(ptr, nullptr);
      burst_ptrs.push_back(ptr);
    }

    // Deallocate only half of them (simulating leaks)
    for (size_t i = 0; i < burst_ptrs.size() / 2; ++i) {
      deallocate(burst_ptrs[i]);
    }
    // The rest are "leaked" for this burst
  }

  // Pattern 2: Mixed sizes with selective cleanup
  std::vector<void *> mixed_ptrs;
  const size_t sizes[] = {16, 32, 64, 128, 256, 512};

  for (int i = 0; i < 1000; ++i) {
    size_t size = sizes[i % 6];
    void *ptr = allocate(size);
    ASSERT_NE(ptr, nullptr);
    mixed_ptrs.push_back(ptr);
  }

  // Deallocate only even indices
  for (size_t i = 0; i < mixed_ptrs.size(); i += 2) {
    deallocate(mixed_ptrs[i]);
  }

  SUCCEED();
}