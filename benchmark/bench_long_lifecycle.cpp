#include "mempool.h"
#include <benchmark/benchmark.h>
#include <chrono>
#include <random>
#include <thread>
#include <vector>

// Benchmark long-running allocation patterns and memory lifecycle

static void BM_LongLivedObjects(benchmark::State &state) {
  const int num_long_lived = state.range(0);
  const int num_short_lived = state.range(1);
  const int iterations = state.range(2);

  for (auto _ : state) {
    std::vector<void *> long_lived;
    long_lived.reserve(num_long_lived);

    // Create long-lived objects
    for (int i = 0; i < num_long_lived; ++i) {
      void *ptr = allocate(256);
      benchmark::DoNotOptimize(ptr);
      long_lived.push_back(ptr);
    }

    // Simulate workload with short-lived allocations
    for (int iter = 0; iter < iterations; ++iter) {
      std::vector<void *> short_lived;
      short_lived.reserve(num_short_lived);

      // Allocate short-lived objects
      for (int i = 0; i < num_short_lived; ++i) {
        void *ptr = allocate(64);
        benchmark::DoNotOptimize(ptr);
        short_lived.push_back(ptr);
      }

      // Use the objects (simulate work)
      for (void *ptr : short_lived) {
        benchmark::DoNotOptimize(ptr);
        // Simulate some work with the pointer
        volatile char *data = static_cast<char *>(ptr);
        *data = 42;
      }

      // Deallocate short-lived objects
      for (void *ptr : short_lived) {
        deallocate(ptr);
      }
    }

    // Finally deallocate long-lived objects
    for (void *ptr : long_lived) {
      deallocate(ptr);
    }
  }

  state.SetItemsProcessed(state.iterations() *
                          (num_long_lived + num_short_lived * iterations));
}

static void BM_ObjectPoolPattern(benchmark::State &state) {
  const int pool_size = state.range(0);
  const int reuse_iterations = state.range(1);

  std::vector<void *> object_pool;
  object_pool.reserve(pool_size);

  for (auto _ : state) {
    // Initialize object pool
    object_pool.clear();
    for (int i = 0; i < pool_size; ++i) {
      void *ptr = allocate(128);
      benchmark::DoNotOptimize(ptr);
      object_pool.push_back(ptr);
    }

    // Reuse objects multiple times
    for (int reuse = 0; reuse < reuse_iterations; ++reuse) {
      // "Use" all objects
      for (void *ptr : object_pool) {
        benchmark::DoNotOptimize(ptr);
        volatile int *data = static_cast<int *>(ptr);
        *data = reuse;
      }

      // Simulate some work between uses
      benchmark::DoNotOptimize(object_pool.data());
    }

    // Deallocate pool
    for (void *ptr : object_pool) {
      deallocate(ptr);
    }
  }

  state.SetItemsProcessed(state.iterations() * pool_size * reuse_iterations);
}

static void BM_MemoryArenaPattern(benchmark::State &state) {
  const int arena_size = state.range(0);
  const int num_arenas = state.range(1);

  for (auto _ : state) {
    std::vector<std::vector<void *>> arenas(num_arenas);

    // Allocate multiple "arenas"
    for (auto &arena : arenas) {
      arena.reserve(arena_size);
      for (int i = 0; i < arena_size; ++i) {
        void *ptr = allocate(32 + (i % 8) * 16); // Vary sizes within arena
        benchmark::DoNotOptimize(ptr);
        arena.push_back(ptr);
      }
    }

    // Use arenas (simulate processing)
    for (auto &arena : arenas) {
      for (void *ptr : arena) {
        benchmark::DoNotOptimize(ptr);
        volatile char *data = static_cast<char *>(ptr);
        *data = 1;
      }
    }

    // Deallocate arenas in different orders
    std::random_device rd;
    std::mt19937 gen(rd());
    std::shuffle(arenas.begin(), arenas.end(), gen);

    for (auto &arena : arenas) {
      for (void *ptr : arena) {
        deallocate(ptr);
      }
    }
  }

  state.SetItemsProcessed(state.iterations() * arena_size * num_arenas);
}

static void BM_ConcurrentLifecycles(benchmark::State &state) {
  const int num_threads = state.range(0);
  const int ops_per_thread = state.range(1);

  for (auto _ : state) {
    std::vector<std::thread> threads;
    std::atomic<int> completed_threads{0};

    for (int t = 0; t < num_threads; ++t) {
      threads.emplace_back([t, ops_per_thread, &completed_threads]() {
        std::vector<void *> ptrs;
        ptrs.reserve(ops_per_thread / 2); // Half will be long-lived

        // Create some long-lived objects
        for (int i = 0; i < ops_per_thread / 4; ++i) {
          void *ptr = allocate(64 + t * 16); // Thread-specific sizes
          benchmark::DoNotOptimize(ptr);
          ptrs.push_back(ptr);
        }

        // Mix with short allocations/deallocations
        for (int i = 0; i < ops_per_thread / 2; ++i) {
          void *short_ptr = allocate(32);
          benchmark::DoNotOptimize(short_ptr);
          deallocate(short_ptr);
        }

        // Keep long-lived objects
        for (void *ptr : ptrs) {
          benchmark::DoNotOptimize(ptr);
        }

        // Finally clean up
        for (void *ptr : ptrs) {
          deallocate(ptr);
        }

        completed_threads++;
      });
    }

    // Wait for all threads
    while (completed_threads.load() < num_threads) {
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    for (auto &t : threads) {
      t.join();
    }
  }

  state.SetItemsProcessed(state.iterations() * num_threads * ops_per_thread);
}

static void BM_FragmentationBuildup(benchmark::State &state) {
  const int num_objects = state.range(0);
  const int fragmentation_rounds = state.range(1);

  for (auto _ : state) {
    // Create a fragmented heap by allocating/deallocating in patterns
    for (int round = 0; round < fragmentation_rounds; ++round) {
      std::vector<void *> objects;
      objects.reserve(num_objects);

      // Allocate objects of mixed sizes
      for (int i = 0; i < num_objects; ++i) {
        size_t size = 32 * (1 + (i % 8)); // Sizes: 32, 64, 96, ..., 256
        void *ptr = allocate(size);
        benchmark::DoNotOptimize(ptr);
        objects.push_back(ptr);
      }

      // Deallocate every other object to create holes
      for (size_t i = 0; i < objects.size(); i += 2) {
        deallocate(objects[i]);
        objects[i] = nullptr;
      }

      // Allocate new objects to fill some holes
      for (size_t i = 0; i < objects.size(); i += 2) {
        if (objects[i] == nullptr) {
          void *ptr = allocate(16 + (round % 8) * 8); // Different sizes
          benchmark::DoNotOptimize(ptr);
          objects[i] = ptr;
        }
      }

      // Clean up all remaining objects
      for (void *ptr : objects) {
        if (ptr) {
          deallocate(ptr);
        }
      }
    }
  }

  state.SetItemsProcessed(state.iterations() * num_objects *
                          fragmentation_rounds);
}

// Benchmark configurations
BENCHMARK(BM_LongLivedObjects)
    ->Args(
        {100, 1000,
         10}) // 100 long-lived, 1000 short-lived per iteration, 10 iterations
    ->Args(
        {1000, 10000,
         5}); // 1000 long-lived, 10000 short-lived per iteration, 5 iterations

BENCHMARK(BM_ObjectPoolPattern)
    ->Args({1000, 100})  // Pool of 1000 objects, reuse 100 times
    ->Args({10000, 10}); // Pool of 10000 objects, reuse 10 times

BENCHMARK(BM_MemoryArenaPattern)
    ->Args({1000, 10})  // 10 arenas of 1000 objects each
    ->Args({100, 100}); // 100 arenas of 100 objects each

BENCHMARK(BM_ConcurrentLifecycles)
    ->Args({2, 10000}) // 2 threads, 10000 ops each
    ->Args({4, 5000}); // 4 threads, 5000 ops each

BENCHMARK(BM_FragmentationBuildup)
    ->Args({1000, 10})  // 1000 objects, 10 fragmentation rounds
    ->Args({10000, 5}); // 10000 objects, 5 fragmentation rounds

BENCHMARK_MAIN();
