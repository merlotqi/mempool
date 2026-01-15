#pragma once

#if defined(_WIN32)
#define NOMINMAX
#include <Windows.h>
#include <malloc.h>
#elif defined(__linux__) || defined(__APPLE__)
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>
#else
#error "Unsupported platform"
#endif

#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace mempool {

#define MEMPOOL_VERSION "0.1.0"

static constexpr uint32_t CACHE_LINE_SIZE = 64;
static constexpr size_t ALIGN_SIZE = 16;
static constexpr size_t MIN_BLOCK_SIZE = 16;
static constexpr size_t MAX_FIXED_SIZE = 4096;
static constexpr size_t MAX_POOL_COUNT = 32;
static constexpr size_t LARGE_ALLOC_THRESHOLD = 1024 * 1024;
static constexpr size_t DEFAULT_CHUNK_SIZE = 64 * 1024;
static constexpr uint32_t MAGIC_NUMBER = 0xDEADBEEF;

#if defined(_WIN32)

inline size_t get_page_size() {
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  return si.dwPageSize;
}

inline void *platform_mmap(size_t size) {
  return VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
}

inline void platform_munmap(void *ptr, size_t) {
  VirtualFree(ptr, 0, MEM_RELEASE);
}

inline void *platform_memalign(size_t size, size_t alignment) {
  return _aligned_malloc(size, alignment);
}

inline void platform_free_aligned(void *ptr) { _aligned_free(ptr); }

#else

inline size_t get_page_size() { return sysconf(_SC_PAGESIZE); }

inline void *platform_mmap(size_t size) {
  return mmap(nullptr, size, PROT_READ | PROT_WRITE,
              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
}

inline void platform_munmap(void *ptr, size_t size) { munmap(ptr, size); }

inline void *platform_memalign(size_t size, size_t alignment) {
  void *ptr = nullptr;
  if (posix_memalign(&ptr, alignment, size) != 0) {
    return nullptr;
  }
  return ptr;
}

inline void platform_free_aligned(void *ptr) { free(ptr); }

#endif

inline size_t align_up(size_t v, size_t a) { return (v + a - 1) & ~(a - 1); }

static constexpr size_t SIZE_CLASSES[] = {
    16,   24,   32,   48,   64,   80,   96,   112,  128, 160,
    192,  224,  256,  320,  384,  448,  512,  640,  768, 896,
    1024, 1280, 1536, 1792, 2048, 2560, 3072, 3584, 4096};

static constexpr size_t NUM_SIZE_CLASSES =
    sizeof(SIZE_CLASSES) / sizeof(SIZE_CLASSES[0]);

inline size_t get_pool_id(size_t sz) {
  static constexpr size_t LARGE_POOL_ID = NUM_SIZE_CLASSES;
  static constexpr size_t ALIGN_SHIFT = 3;
  static constexpr size_t LOOKUP_TABLE_SIZE =
      (MAX_FIXED_SIZE >> ALIGN_SHIFT) + 1;
  static constexpr size_t ALIGN_SIZE = 1u << ALIGN_SHIFT;

  static_assert(
      [] {
        for (size_t sz : SIZE_CLASSES) {
          if (sz % ALIGN_SIZE != 0)
            return false;
        }
        return true;
      }(),
      "All entries in SIZE_CLASSES must be a multiple of ALIGN_SIZE (8)!");

  if (sz == 0) [[unlikely]] {
    sz = 1;
  }

  static constexpr auto create_lookup_table = []() constexpr {
    std::array<uint8_t, LOOKUP_TABLE_SIZE> table{};
    size_t class_idx = 0;
    for (size_t i = 0; i < LOOKUP_TABLE_SIZE; ++i) {
      size_t actual_size = i << ALIGN_SHIFT;
      while (class_idx + 1 < NUM_SIZE_CLASSES &&
             actual_size > SIZE_CLASSES[class_idx]) {
        ++class_idx;
      }
      table[i] = static_cast<uint8_t>(class_idx);
    }
    return table;
  };

  static constexpr auto LOOKUP_TABLE = create_lookup_table();

  if (sz > 4096) [[unlikely]] {
    return LARGE_POOL_ID;
  }

  return LOOKUP_TABLE[(sz + 7) >> ALIGN_SHIFT];
}

inline size_t get_pool_size(size_t id) {
  assert(id < NUM_SIZE_CLASSES);
  return SIZE_CLASSES[id];
}

struct BlockHeader {
  union {
    BlockHeader *next_free;
    uint64_t _pad;
  };

  struct {
    uint32_t magic : 32;
    uint32_t pool_id : 6;
    uint32_t is_large : 1;
    uint32_t size : 25;
  } meta;

  std::atomic<uint32_t> ref_count;

  BlockHeader() : next_free(nullptr), ref_count(1) {
    meta.magic = MAGIC_NUMBER;
    meta.pool_id = 0;
    meta.is_large = 0;
    meta.size = 0;
  }

  bool validate() const { return meta.magic == MAGIC_NUMBER; }
};

struct LargeBlockHeader {
  void *base;
  size_t size;
  size_t actual_size;
  bool is_mmap;
  uint32_t magic;

  LargeBlockHeader()
      : base(nullptr), size(0), actual_size(0), is_mmap(false),
        magic(MAGIC_NUMBER) {}

  bool validate() const { return magic == MAGIC_NUMBER; }
};

struct FreeList {
  std::atomic<BlockHeader *> head{nullptr};

  void push(BlockHeader *n) {
    BlockHeader *old = head.load(std::memory_order_relaxed);
    do {
      n->next_free = old;
    } while (!head.compare_exchange_weak(old, n, std::memory_order_release,
                                         std::memory_order_relaxed));
  }

  BlockHeader *pop() {
    BlockHeader *n = head.load(std::memory_order_relaxed);
    while (n) {
      BlockHeader *next = n->next_free;
      if (head.compare_exchange_weak(n, next, std::memory_order_acquire,
                                     std::memory_order_relaxed)) {
        return n;
      }
    }
    return nullptr;
  }
};

struct alignas(CACHE_LINE_SIZE) ChunkHeader {
  size_t size;
  size_t used;
};

class alignas(CACHE_LINE_SIZE) BlockPool {
  size_t block_size_;
  size_t blocks_per_chunk_;
  std::vector<ChunkHeader *> chunks_;
  FreeList free_list_;
  std::mutex mutex_;

public:
  explicit BlockPool(size_t sz) : block_size_(align_up(sz, ALIGN_SIZE)) {
    blocks_per_chunk_ =
        DEFAULT_CHUNK_SIZE / (block_size_ + sizeof(BlockHeader));
    if (blocks_per_chunk_ < 4)
      blocks_per_chunk_ = 4;
  }

  ~BlockPool() { clear(); }

  BlockHeader *allocate_header() {
    BlockHeader *h = free_list_.pop();
    if (!h)
      h = allocate_chunk();
    return h;
  }

  void return_header(BlockHeader *h) { free_list_.push(h); }

  void clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto *c : chunks_) {
      platform_munmap(c, c->size);
    }
    chunks_.clear();
  }

private:
  BlockHeader *allocate_chunk() {
    std::lock_guard<std::mutex> lock(mutex_);

    size_t chunk_size =
        align_up(sizeof(ChunkHeader) +
                     blocks_per_chunk_ * (sizeof(BlockHeader) + block_size_),
                 4096);

    void *mem = platform_mmap(chunk_size);
    if (!mem)
      return nullptr;

    auto *chunk = reinterpret_cast<ChunkHeader *>(mem);
    chunk->size = chunk_size;
    chunk->used = sizeof(ChunkHeader);
    chunks_.push_back(chunk);

    uint8_t *p = reinterpret_cast<uint8_t *>(chunk);
    for (size_t i = 0; i < blocks_per_chunk_; ++i) {
      auto *b = reinterpret_cast<BlockHeader *>(p + chunk->used);
      new (b) BlockHeader();
      b->meta.size = block_size_;
      b->meta.pool_id = get_pool_id(block_size_);
      free_list_.push(b);
      chunk->used += sizeof(BlockHeader) + block_size_;
    }

    return free_list_.pop();
  }
};

// ChunkHeader is cache-line aligned to avoid false sharing
// during global refill/drain paths (cold but contended).
struct ThreadCache {
  static constexpr size_t REFILL_BATCH = 32;
  FreeList local[MAX_POOL_COUNT];

  std::atomic<bool> drained{false};
};

#if defined(_WIN32)
static DWORD tls_key = TLS_OUT_OF_INDEXES;
extern "C" BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason,
                               LPVOID lpvReserved);
#else
static pthread_key_t tls_key;
static pthread_once_t key_once = PTHREAD_ONCE_INIT;
static void make_key();
#endif

inline ThreadCache &get_thread_cache() {
#if defined(_WIN32)
  ThreadCache *cache = static_cast<ThreadCache *>(TlsGetValue(tls_key));
  if (!cache && tls_key != TLS_OUT_OF_INDEXES) {
    cache = new ThreadCache();
    cache->drained.store(false);
    TlsSetValue(tls_key, cache);
  }

  static ThreadCache fallback_cache;
  return cache ? *cache : fallback_cache;
#else
  pthread_once(&key_once, make_key);
  ThreadCache *cache = static_cast<ThreadCache *>(pthread_getspecific(tls_key));
  if (!cache) {
    cache = new ThreadCache();
    cache->drained.store(false);
    pthread_setspecific(tls_key, cache);
  }
  return *cache;
#endif
}

class MemAllocator {
  struct PoolEntry {
    std::unique_ptr<BlockPool> pool;
  };

  std::vector<PoolEntry> pools_;
  std::unordered_map<void *, LargeBlockHeader *> large_;
  std::shared_mutex large_mutex_;
  size_t page_size_;

  inline static std::once_flag once_;
  inline static MemAllocator *instance_;

public:
  static MemAllocator &instance() {
    std::call_once(once_, [] { instance_ = new MemAllocator(); });
    return *instance_;
  }

  void *allocate(size_t size, size_t align) {
    if (size > MAX_FIXED_SIZE || align > ALIGN_SIZE) {
      return alloc_large(size, align);
    }

    size_t id = get_pool_id(size);

    if (auto *h = get_thread_cache().local[id].pop()) {
      h->ref_count.store(1, std::memory_order_relaxed);
      return reinterpret_cast<uint8_t *>(h) + sizeof(BlockHeader);
    }

    refill_tls(id);

    if (auto *h = get_thread_cache().local[id].pop()) {
      h->ref_count.store(1, std::memory_order_relaxed);
      return reinterpret_cast<uint8_t *>(h) + sizeof(BlockHeader);
    }

    return nullptr;
  }

  void deallocate(void *ptr) {
    if (!ptr)
      return;

    auto *h = reinterpret_cast<BlockHeader *>(reinterpret_cast<uint8_t *>(ptr) -
                                              sizeof(BlockHeader));

    if (h->validate() && !h->meta.is_large) {
      size_t id = h->meta.pool_id;
      get_thread_cache().local[id].push(h);
      return;
    }

    free_large(ptr);
  }

  static void drain_thread_cache(ThreadCache *cache) {
    if (!cache)
      return;

    bool expected = false;
    if (!cache->drained.compare_exchange_strong(expected, true)) {
      return;
    }

    MemAllocator &alloc = instance();
    for (size_t id = 0; id < MAX_POOL_COUNT; ++id) {
      BlockHeader *h;
      while ((h = cache->local[id].pop()) != nullptr) {
        if (alloc.pools_[id].pool) {
          alloc.pools_[id].pool->return_header(h);
        }
      }
    }
    delete cache;
  }

private:
  MemAllocator() : page_size_(get_page_size()) {
    pools_.resize(MAX_POOL_COUNT);
#if defined(_WIN32)
    if (tls_key == TLS_OUT_OF_INDEXES) {
      tls_key = TlsAlloc();
    }
#endif
  }

  void refill_tls(size_t id) {
    auto &e = pools_[id];
    if (!e.pool) {
      e.pool = std::make_unique<BlockPool>(get_pool_size(id));
    }

    for (size_t i = 0; i < ThreadCache::REFILL_BATCH; ++i) {
      if (auto *h = e.pool->allocate_header()) {
        get_thread_cache().local[id].push(h);
      } else {
        break;
      }
    }
  }

  void *alloc_large(size_t size, size_t align) {
    size_t total =
        align_up(sizeof(LargeBlockHeader) + size + align, page_size_);
    bool use_mmap = size >= LARGE_ALLOC_THRESHOLD;

    void *base =
        use_mmap ? platform_mmap(total) : platform_memalign(total, align);

    if (!base)
      return nullptr;

    auto *h = new (base) LargeBlockHeader();
    h->base = base;
    h->size = size;
    h->actual_size = total;
    h->is_mmap = use_mmap;

    uintptr_t addr = align_up(
        reinterpret_cast<uintptr_t>(base) + sizeof(LargeBlockHeader), align);
    void *user = reinterpret_cast<void *>(addr);

    std::unique_lock lock(large_mutex_);
    large_[user] = h;
    return user;
  }

  void free_large(void *ptr) {
    LargeBlockHeader *h = nullptr;
    {
      std::unique_lock lock(large_mutex_);
      auto it = large_.find(ptr);
      if (it == large_.end())
        return;
      h = it->second;
      large_.erase(it);
    }

    if (!h->validate())
      return;

    if (h->is_mmap)
      platform_munmap(h->base, h->actual_size);
    else
      platform_free_aligned(h->base);
  }
};

#if defined(_WIN32)
extern "C" BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason,
                               LPVOID lpvReserved) {
  switch (fdwReason) {
  case DLL_PROCESS_ATTACH: {
    tls_key = TlsAlloc();
    if (tls_key == TLS_OUT_OF_INDEXES) {
      return FALSE;
    }
    break;
  }
  case DLL_THREAD_ATTACH: {
    ThreadCache *cache = new ThreadCache();
    TlsSetValue(tls_key, cache);
    break;
  }
  case DLL_THREAD_DETACH: {
    ThreadCache *cache = static_cast<ThreadCache *>(TlsGetValue(tls_key));
    MemAllocator::drain_thread_cache(cache);
    TlsSetValue(tls_key, nullptr);
    break;
  }
  case DLL_PROCESS_DETACH: {
    if (tls_key != TLS_OUT_OF_INDEXES) {
      TlsFree(tls_key);
      tls_key = TLS_OUT_OF_INDEXES;
    }
    break;
  }
  default:
    break;
  }
  return TRUE;
}
#else

static void make_key() {
  pthread_key_create(&tls_key, [](void *ptr) {
    ThreadCache *cache = static_cast<ThreadCache *>(ptr);
    if (!cache)
      return;

    MemAllocator::drain_thread_cache(cache);
  });
}
#endif

} // namespace mempool
