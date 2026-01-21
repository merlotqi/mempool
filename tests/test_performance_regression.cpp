#include <chrono>
#include <gtest/gtest.h>
#include <mempool.h>
#include <thread>
#include <vector>

// Performance regression tests to ensure allocations remain fast
class PerformanceRegressionTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Warm up the pool
    std::vector<void *> warmup_ptrs;
    for (int i = 0; i < 1000; ++i) {
      void *ptr = allocate(64);
      warmup_ptrs.push_back(ptr);
    }
    for (void *ptr : warmup_ptrs) {
      deallocate(ptr);
    }
  }
};

// Baseline performance expectations (in nanoseconds per operation)
static constexpr double EXPECTED_ALLOC_TIME_NS = 1000.0;  // 1 microsecond
static constexpr double EXPECTED_DEALLOC_TIME_NS = 500.0; // 500 nanoseconds

TEST_F(PerformanceRegressionTest, AllocationPerformance) {
  const int num_operations = 10000;
  auto start = std::chrono::high_resolution_clock::now();

  std::vector<void *> ptrs;
  ptrs.reserve(num_operations);

  for (int i = 0; i < num_operations; ++i) {
    void *ptr = allocate(64);
    ASSERT_NE(ptr, nullptr);
    ptrs.push_back(ptr);
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);

  double avg_time_ns = static_cast<double>(duration.count()) / num_operations;

  // Check that performance is within expected bounds
  EXPECT_LT(avg_time_ns, EXPECTED_ALLOC_TIME_NS)
      << "Allocation performance regression: " << avg_time_ns
      << " ns/op (expected < " << EXPECTED_ALLOC_TIME_NS << " ns/op)";

  // Cleanup
  for (void *ptr : ptrs) {
    deallocate(ptr);
  }
}

TEST_F(PerformanceRegressionTest, DeallocationPerformance) {
  const int num_operations = 10000;

  // First allocate
  std::vector<void *> ptrs;
  ptrs.reserve(num_operations);
  for (int i = 0; i < num_operations; ++i) {
    ptrs.push_back(allocate(64));
  }

  // Now time deallocation
  auto start = std::chrono::high_resolution_clock::now();

  for (void *ptr : ptrs) {
    deallocate(ptr);
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);

  double avg_time_ns = static_cast<double>(duration.count()) / num_operations;

  EXPECT_LT(avg_time_ns, EXPECTED_DEALLOC_TIME_NS)
      << "Deallocation performance regression: " << avg_time_ns
      << " ns/op (expected < " << EXPECTED_DEALLOC_TIME_NS << " ns/op)";
}

TEST_F(PerformanceRegressionTest, ConcurrentPerformance) {
  const int num_threads = 4;
  const int ops_per_thread = 5000;

  auto thread_func = [](int thread_id) -> long long {
    std::vector<void *> ptrs;
    ptrs.reserve(ops_per_thread);

    auto start = std::chrono::high_resolution_clock::now();

    // Allocate
    for (int i = 0; i < ops_per_thread; ++i) {
      void *ptr = allocate(32 + (thread_id * 16)); // Vary size by thread
      if (!ptr)
        return 0LL; // Allocation failed
      ptrs.push_back(ptr);
    }

    // Deallocate
    for (void *ptr : ptrs) {
      deallocate(ptr);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    return duration.count();
  };

  std::vector<std::thread> threads;
  std::vector<long long> durations;

  // Start threads
  for (int i = 0; i < num_threads; ++i) {
    threads.emplace_back([&, i]() {
      long long duration = thread_func(i);
      durations.push_back(duration);
    });
  }

  // Wait for completion
  for (auto &t : threads) {
    t.join();
  }

  // Calculate average time per operation across all threads
  long long total_time_us = 0;
  for (long long d : durations) {
    total_time_us += d;
  }

  double avg_time_us =
      static_cast<double>(total_time_us) / (num_threads * ops_per_thread);

  // Concurrent operations should be reasonably fast (less than 10 microseconds
  // avg)
  EXPECT_LT(avg_time_us, 10.0)
      << "Concurrent performance regression: " << avg_time_us
      << " us/op (expected < 10 us/op)";
}

TEST_F(PerformanceRegressionTest, MemoryThroughput) {
  const int num_operations = 100000;
  const size_t alloc_size = 128;

  auto start = std::chrono::high_resolution_clock::now();

  std::vector<void *> ptrs;
  ptrs.reserve(num_operations);

  for (int i = 0; i < num_operations; ++i) {
    void *ptr = allocate(alloc_size);
    ASSERT_NE(ptr, nullptr);
    ptrs.push_back(ptr);
  }

  for (void *ptr : ptrs) {
    deallocate(ptr);
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  double throughput_mb_s = (num_operations * alloc_size) / (1024.0 * 1024.0) /
                           (duration.count() / 1000.0);

  // Should achieve reasonable throughput (at least 100 MB/s in debug build)
  EXPECT_GT(throughput_mb_s, 50.0)
      << "Memory throughput regression: " << throughput_mb_s
      << " MB/s (expected > 50 MB/s)";
}

TEST_F(PerformanceRegressionTest, CacheLocality) {
  // Test that repeated allocations of the same size are fast (cache locality)
  const int iterations = 1000;
  const int allocs_per_iter = 100;

  std::vector<double> times;

  for (int iter = 0; iter < iterations; ++iter) {
    auto start = std::chrono::high_resolution_clock::now();

    std::vector<void *> ptrs;
    for (int i = 0; i < allocs_per_iter; ++i) {
      ptrs.push_back(allocate(64));
    }

    for (void *ptr : ptrs) {
      deallocate(ptr);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    double avg_time = static_cast<double>(duration.count()) / allocs_per_iter;
    times.push_back(avg_time);
  }

  // Calculate average and check for consistency
  double sum = 0;
  for (double t : times) {
    sum += t;
  }
  double avg_time = sum / times.size();

  // Should be consistently fast (less than 500ns average)
  EXPECT_LT(avg_time, 500.0) << "Cache locality regression: average "
                             << avg_time << " ns/op (expected < 500 ns/op)";
}
