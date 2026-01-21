#include "mempool.h"
#include <algorithm>
#include <benchmark/benchmark.h>
#include <random>
#include <vector>

// Benchmark random allocation patterns to simulate real-world usage

static void BM_RandomSizeAllocations(benchmark::State &state) {
  const int num_allocations = state.range(0);
  const size_t max_size = state.range(1);

  // Prepare random sizes
  std::vector<size_t> sizes(num_allocations);
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<size_t> size_dist(16, max_size);

  for (auto &size : sizes) {
    size = size_dist(gen);
  }

  std::vector<void *> ptrs;
  ptrs.reserve(num_allocations);

  for (auto _ : state) {
    // Allocate phase
    for (size_t size : sizes) {
      void *ptr = allocate(size);
      benchmark::DoNotOptimize(ptr);
      ptrs.push_back(ptr);
    }

    // Deallocate phase (in reverse order to test different patterns)
    for (auto it = ptrs.rbegin(); it != ptrs.rend(); ++it) {
      deallocate(*it);
    }

    ptrs.clear();
  }

  state.SetItemsProcessed(state.iterations() * num_allocations);
}

static void BM_RandomLifecycle(benchmark::State &state) {
  const int total_operations = state.range(0);
  const size_t max_size = state.range(1);
  const int max_lifetime = state.range(2);

  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<size_t> size_dist(16, max_size);
  std::uniform_int_distribution<int> lifetime_dist(1, max_lifetime);

  struct Allocation {
    void *ptr;
    size_t size;
    int remaining_lifetime;
  };

  std::vector<Allocation> live_allocations;

  for (auto _ : state) {
    live_allocations.clear();

    for (int op = 0; op < total_operations; ++op) {
      // Randomly decide to allocate or age existing allocations
      if (live_allocations.size() < 1000 && (gen() % 2 == 0)) {
        // Allocate new object
        size_t size = size_dist(gen);
        void *ptr = allocate(size);
        benchmark::DoNotOptimize(ptr);

        int lifetime = lifetime_dist(gen);
        live_allocations.push_back({ptr, size, lifetime});
      }

      // Age existing allocations
      for (auto it = live_allocations.begin(); it != live_allocations.end();) {
        it->remaining_lifetime--;
        if (it->remaining_lifetime <= 0) {
          deallocate(it->ptr);
          it = live_allocations.erase(it);
        } else {
          ++it;
        }
      }
    }

    // Clean up remaining allocations
    for (const auto &alloc : live_allocations) {
      deallocate(alloc.ptr);
    }
    live_allocations.clear();
  }

  state.SetItemsProcessed(state.iterations() * total_operations);
}

static void BM_SizeClassFragmentation(benchmark::State &state) {
  const int allocations_per_class = state.range(0);

  // Test allocations across different size classes to measure fragmentation
  const size_t size_classes[] = {16, 32, 64, 128, 256, 512, 1024, 2048, 4096};

  for (auto _ : state) {
    std::vector<void *> ptrs;

    // Allocate from each size class
    for (size_t size : size_classes) {
      for (int i = 0; i < allocations_per_class; ++i) {
        void *ptr = allocate(size);
        benchmark::DoNotOptimize(ptr);
        ptrs.push_back(ptr);
      }
    }

    // Deallocate in a different order (mix size classes)
    std::random_device rd;
    std::mt19937 gen(rd());
    std::shuffle(ptrs.begin(), ptrs.end(), gen);

    for (void *ptr : ptrs) {
      deallocate(ptr);
    }
  }

  state.SetItemsProcessed(state.iterations() * sizeof(size_classes) /
                          sizeof(size_classes[0]) * allocations_per_class);
}

static void BM_AllocationBurst(benchmark::State &state) {
  const int burst_size = state.range(0);
  const size_t alloc_size = state.range(1);

  for (auto _ : state) {
    std::vector<void *> ptrs;
    ptrs.reserve(burst_size);

    // Burst allocation
    for (int i = 0; i < burst_size; ++i) {
      void *ptr = allocate(alloc_size);
      benchmark::DoNotOptimize(ptr);
      ptrs.push_back(ptr);
    }

    // Burst deallocation
    for (void *ptr : ptrs) {
      deallocate(ptr);
    }
  }

  state.SetItemsProcessed(state.iterations() * burst_size);
}

// Benchmark configurations
BENCHMARK(BM_RandomSizeAllocations)
    ->Args({1000, 4096})   // 1000 allocations, max size 4KB
    ->Args({10000, 1024})  // 10000 allocations, max size 1KB
    ->Args({100000, 256}); // 100000 allocations, max size 256B

BENCHMARK(BM_RandomLifecycle)
    ->Args({10000, 2048, 10})  // 10K operations, max 2KB, max lifetime 10
    ->Args({50000, 1024, 50}); // 50K operations, max 1KB, max lifetime 50

BENCHMARK(BM_SizeClassFragmentation)
    ->Arg(100)   // 100 allocations per size class
    ->Arg(1000); // 1000 allocations per size class

BENCHMARK(BM_AllocationBurst)
    ->Args({100, 64})     // 100 allocations of 64B each
    ->Args({1000, 128})   // 1000 allocations of 128B each
    ->Args({10000, 256}); // 10000 allocations of 256B each

BENCHMARK_MAIN();
