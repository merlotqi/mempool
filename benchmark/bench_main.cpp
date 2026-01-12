#include "mempool.h"
#include <benchmark/benchmark.h>
#include <cstdlib>

static void BM_System_Malloc(benchmark::State& state) {
  size_t bytes = state.range(0);
  for (auto _ : state) {
    void* p = std::malloc(bytes);
    benchmark::DoNotOptimize(p);
    std::free(p);
  }
}

BENCHMARK(BM_System_Malloc)->Range(16, 4096)->ThreadRange(1, 8);

static void BM_My_MemPool(benchmark::State& state) {
    size_t bytes = state.range(0);
    for (auto _ : state) {
        void* p = allocate(bytes); 
        benchmark::DoNotOptimize(p);
        deallocate(p);
    }
}
BENCHMARK(BM_My_MemPool)->Range(16, 4096)->ThreadRange(1, 8);

BENCHMARK_MAIN();