#include "mempool.h"
#include <benchmark/benchmark.h>
#include <vector>

constexpr int kBatch = 64;

static void BM_System_Malloc_Batch(benchmark::State &state) {
  size_t bytes = state.range(0);
  std::vector<void *> ptrs(kBatch);

  for (auto _ : state) {
    for (int i = 0; i < kBatch; ++i) {
      ptrs[i] = std::malloc(bytes);
      benchmark::DoNotOptimize(ptrs[i]);
    }
    for (int i = 0; i < kBatch; ++i) {
      std::free(ptrs[i]);
    }
  }
  state.SetItemsProcessed(state.iterations() * kBatch);
}

static void BM_My_MemPool_Batch(benchmark::State &state) {
  size_t bytes = state.range(0);
  std::vector<void *> ptrs(kBatch);

  for (auto _ : state) {
    for (int i = 0; i < kBatch; ++i) {
      ptrs[i] = allocate(bytes);
      benchmark::DoNotOptimize(ptrs[i]);
    }
    for (int i = 0; i < kBatch; ++i) {
      deallocate(ptrs[i]);
    }
  }
  state.SetItemsProcessed(state.iterations() * kBatch);
}

BENCHMARK(BM_System_Malloc_Batch)
    ->RangeMultiplier(2)
    ->Range(8, 4096)
    ->ThreadRange(1, 12);
BENCHMARK(BM_My_MemPool_Batch)
    ->RangeMultiplier(2)
    ->Range(8, 4096)
    ->ThreadRange(1, 12);

BENCHMARK_MAIN();