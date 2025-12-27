/**
 * @file mempool.hpp
 * @brief High-performance cross-platform memory pool allocator
 *
 * @section Description
 *
 * This is a high-performance, thread-safe memory pool allocator designed for
 * efficient allocation/deallocation of small objects while supporting large
 * allocations. It features:
 * - Two-level allocation strategy: fixed-size blocks for small objects, direct
 *   system allocation for large objects
 * - Lock-free free lists for small block pools
 * - Reference counting for safe deallocation
 * - Memory alignment support (default 16 bytes)
 * - Cross-platform support (Windows/Linux/macOS)
 * - STL-compatible allocator interface
 *
 * @section Architecture
 *
 * - Small objects (<= 4KB): Managed by fixed-size block pools with power-of-two
 *   sizes from 16B to 4KB
 * - Large objects (> 4KB): Directly allocated via mmap/VirtualAlloc or
 *   posix_memalign/_aligned_malloc
 * - Each block pool maintains a lock-free stack of free blocks
 * - Large allocations are tracked in a hash map with shared_mutex protection
 *
 * @section Usage
 *
 * Basic usage:
 * @code
 * void* ptr = mempool::allocate(1024);
 * mempool::deallocate(ptr);
 *
 * // With STL containers
 * std::vector<int, mempool::Allocator<int>> vec;
 * @endcode
 *
 * @section Thread Safety
 *
 * - Free lists: Lock-free (atomic operations)
 * - Large allocations: Shared mutex for tracking
 * - Chunk allocation: Mutex protected
 * - Reference counting: Atomic operations
 *
 * @section Performance Characteristics
 *
 * - O(1) allocation/deallocation for small objects
 * - Cache-line aligned structures (64 bytes)
 * - Minimal false sharing
 * - Memory reuse through free lists
 *
 * @section Configuration Constants
 *
 * - MIN_BLOCK_SIZE: 16 bytes
 * - MAX_FIXED_SIZE: 4096 bytes (4KB)
 * - LARGE_ALLOC_THRESHOLD: 1048576 bytes (1MB)
 * - DEFAULT_CHUNK_SIZE: 65536 bytes (64KB)
 * - CACHE_LINE_SIZE: 64 bytes
 * - ALIGN_SIZE: 16 bytes
 * - MAX_POOL_COUNT: 32
 *
 * @section Memory Layout
 *
 * Small block layout:
 * [BlockHeader][User Data]
 *
 * Large block layout:
 * [LargeBlockHeader][Padding][User Data]
 *
 * Chunk layout:
 * [ChunkHeader][BlockHeader][User Data][BlockHeader][User Data]...
 *
 * @section Validation
 *
 * Each block contains a magic number for validation:
 * - Magic number: 0xDE
 * - Checked on deallocation
 * - Prevents double-free and invalid pointer usage
 *
 * @section Debug Features
 *
 * - Memory filling with 0xCC on deallocation
 * - Reference counting for safe concurrent access
 * - Size tracking and statistics
 *
 * @section Platform Support
 *
 * Windows:
 * - VirtualAlloc/VirtualFree for large allocations
 * - _aligned_malloc/_aligned_free for aligned allocations
 * - GetSystemInfo for page size
 *
 * Linux/Unix/macOS:
 * - mmap/munmap for large allocations
 * - posix_memalign/free for aligned allocations
 * - sysconf(_SC_PAGESIZE) for page size
 *
 * @section Notes
 *
 * - This allocator is designed for applications with frequent small object
 *   allocations/deallocations
 * - Large allocations (>1MB) use system allocation directly
 * - Memory is not returned to the system until clear() is called or program
 * ends
 * - Not suitable for real-time systems with hard latency requirements
 *
 * @version 0.1.0
 * @date 2025-12-26
 * @author Memory Pool Development Team
 * @copyright MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

// ============================================================================
// SYSTEM HEADERS
// ============================================================================
// Platform-specific headers for memory allocation
#if defined(_WIN32)
#define NOMINMAX
#include <Windows.h>
#include <malloc.h>
#elif defined(__linux__) || defined(__APPLE__)
#include <sys/mman.h>
#include <unistd.h>
#else
#error "Unsupported platform - only Windows, Linux, and macOS are supported"
#endif

// ============================================================================
// STANDARD LIBRARY HEADERS
// ============================================================================
#include <atomic>        // For lock-free operations and reference counting
#include <cstddef>       // For size_t, nullptr_t
#include <cstdint>       // For fixed-size integer types
#include <cstdlib>       // For memory utilities
#include <cstring>       // For memset, memcpy
#include <memory>        // For unique_ptr, make_unique
#include <mutex>         // For thread synchronization
#include <shared_mutex>  // For reader-writer locks
#include <stdio.h>       // For potential debugging output
#include <unordered_map> // For tracking large allocations
#include <vector>        // For managing memory chunks

namespace mempool {

// ============================================================================
// LIBRARY VERSION
// ============================================================================
// Current version of the memory pool library
#define MEMPOOL_VERSION "0.1.0"

// ============================================================================
// CONFIGURATION CONSTANTS
// ============================================================================
// These constants control the behavior and performance characteristics
// of the memory pool allocator.

// Size of a cache line on most modern processors (for alignment)
static constexpr uint32_t CACHE_LINE_SIZE = 64;

// Default alignment for all allocations (16 bytes for SSE/AVX compatibility)
static constexpr size_t ALIGN_SIZE = 16;

// Minimum block size for small object allocation
static constexpr size_t MIN_BLOCK_SIZE = 16;

// Maximum size that will use fixed-size block pools
static constexpr size_t MAX_FIXED_SIZE = 4096; // 4KB

// Number of different block size pools (16B, 32B, 64B, ..., 4KB)
static constexpr size_t MAX_POOL_COUNT = 32;

// Threshold above which allocations use large block handling
static constexpr size_t LARGE_ALLOC_THRESHOLD = 1024 * 1024; // 1MB

// Default size for memory chunks in small block pools
static constexpr size_t DEFAULT_CHUNK_SIZE = 64 * 1024; // 64KB

// Magic number used for memory corruption detection
static constexpr uint32_t MAGIC_NUMBER = 0xDE; // 0xDEADBEEF partial

namespace details {

// ============================================================================
// PLATFORM ABSTRACTION LAYER
// ============================================================================
// These functions provide a consistent interface for platform-specific
// memory operations (mmap/VirtualAlloc, posix_memalign/_aligned_malloc, etc.)

#if defined(_WIN32)
// Windows-specific implementations

// Get the system page size
inline size_t get_page_size() {
  SYSTEM_INFO sysInfo;
  GetSystemInfo(&sysInfo);
  return sysInfo.dwPageSize;
}

// Allocate memory using VirtualAlloc (similar to mmap)
inline void *platform_mmap(size_t size) {
  return VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
}

// Free memory allocated with VirtualAlloc
inline void platform_munmap(void *ptr, size_t size) {
  VirtualFree(ptr, 0, MEM_RELEASE);
}

// Allocate aligned memory using Windows API
inline void *platform_memalign(size_t size, size_t alignment) {
  return _aligned_malloc(size, alignment);
}

// Free aligned memory allocated with _aligned_malloc
inline void platform_free_aligned(void *ptr) { _aligned_free(ptr); }

#else
// Linux/macOS/Unix implementations

// Get the system page size using sysconf
inline size_t get_page_size() { return sysconf(_SC_PAGESIZE); }

// Allocate memory using mmap
inline void *platform_mmap(size_t size) {
  return mmap(nullptr, size, PROT_READ | PROT_WRITE,
              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
}

// Free memory allocated with mmap
inline void platform_munmap(void *ptr, size_t size) { munmap(ptr, size); }

// Allocate aligned memory using POSIX API
inline void *platform_memalign(size_t size, size_t alignment) {
  void *ptr = nullptr;
  if (posix_memalign(&ptr, alignment, size) != 0) {
    return nullptr;
  }
  return ptr;
}

// Free aligned memory (uses standard free with posix_memalign)
inline void platform_free_aligned(void *ptr) { free(ptr); }
#endif

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

// Round up a size to the nearest multiple of alignment
inline size_t align_up(size_t sz, size_t align) {
  return (sz + align - 1) & ~(align - 1);
}

// Round down a size to the nearest multiple of alignment
inline size_t align_down(size_t sz, size_t align) { return sz & ~(align - 1); }

// Calculate which pool should handle a given allocation size
// Uses power-of-two sizing from MIN_BLOCK_SIZE to MAX_FIXED_SIZE
inline size_t get_pool_id(size_t sz) {
  if (sz < MIN_BLOCK_SIZE)
    return 0;

  size_t id = 0;
  size_t threshold = MIN_BLOCK_SIZE;
  while (threshold < sz && id < MAX_POOL_COUNT - 1) {
    threshold <<= 1;
    ++id;
  }
  return id;
}

// Get the actual block size for a given pool ID
inline size_t get_pool_size(size_t pool_id) {
  size_t size = MIN_BLOCK_SIZE;
  for (size_t i = 0; i < pool_id; ++i) {
    size <<= 1;
  }
  return size;
}

// ============================================================================
// MEMORY BLOCK HEADERS
// ============================================================================
// These structures are placed before user data to track allocation metadata

// Header for small blocks (managed by BlockPool)
// Placed immediately before user data in small allocations
struct BlockHeader {
  union {
    BlockHeader *next_free; // Used when block is in free list (48 bits)
    uint64_t reserved;      // Keep structure size consistent
  };

  // Compact metadata stored in 32 bits
  struct Metadata {
    uint32_t magic : 8;    // Magic number for validation (0xDE)
    uint32_t pool_id : 6;  // Which pool this block belongs to
    uint32_t is_large : 1; // 0 for small blocks, 1 for large blocks
    uint32_t size : 17;    // Size of the block (max 128KB with 17 bits)
  } meta;

  // Reference count for thread-safe deallocation
  std::atomic<uint32_t> ref_count;

  BlockHeader() : next_free(nullptr), ref_count(0) {
    meta.size = 0;
    meta.is_large = 0;
    meta.pool_id = 0;
    meta.magic = MAGIC_NUMBER;
  }

  // Validate that this is a legitimate block header
  bool validate() const { return meta.magic == MAGIC_NUMBER; }
};

// Header for large blocks (direct system allocations)
// Used for allocations larger than MAX_FIXED_SIZE
struct LargeBlockHeader {
  // Reference count for thread-safe deallocation
  std::atomic<uint32_t> ref_count;

  // Size requested by user
  size_t size;

  // Actual allocated size (aligned to page boundary)
  size_t actual_size;

  // Whether this was allocated with mmap/VirtualAlloc
  bool is_mmap;

  // Magic number for validation
  uint32_t magic;

  LargeBlockHeader()
      : ref_count(1), size(0), actual_size(0), is_mmap(false),
        magic(MAGIC_NUMBER) {}

  bool validate() const { return magic == MAGIC_NUMBER; }
};

// ============================================================================
// MEMORY CHUNK MANAGEMENT
// ============================================================================
// Chunks are large memory regions that are subdivided into blocks

// Header for memory chunks (aligned to cache line to prevent false sharing)
struct alignas(CACHE_LINE_SIZE) ChunkHeader {
  ChunkHeader *next;                 // Linked list of chunks
  size_t size;                       // Total chunk size
  size_t used;                       // Bytes used in chunk
  std::atomic<uint32_t> free_blocks; // Number of free blocks in chunk
};

// ============================================================================
// LOCK-FREE FREE LIST
// ============================================================================
// Implements a stack of free blocks using atomic operations
// This provides thread-safe push/pop without mutexes
struct FreeList {
  // Head of the free list (nullptr when empty)
  std::atomic<BlockHeader *> head;

  // Number of blocks in the free list
  std::atomic<uint64_t> length;

  FreeList() : head(nullptr), length(0) {}

  // Push a block onto the free list (lock-free)
  bool push(BlockHeader *node) {
    BlockHeader *expected = head.load(std::memory_order_relaxed);
    node->next_free = expected;

    // CAS loop to handle concurrent pushes
    while (!head.compare_exchange_weak(
        expected, node, std::memory_order_release, std::memory_order_relaxed)) {
      node->next_free = expected;
    }

    length.fetch_add(1, std::memory_order_relaxed);
    return true;
  }

  // Pop a block from the free list (lock-free)
  BlockHeader *pop() {
    BlockHeader *expected = head.load(std::memory_order_relaxed);

    // CAS loop to handle concurrent pops
    while (expected) {
      BlockHeader *next = expected->next_free;
      if (head.compare_exchange_weak(expected, next, std::memory_order_acquire,
                                     std::memory_order_relaxed)) {
        length.fetch_sub(1, std::memory_order_relaxed);
        return expected;
      }
    }

    return nullptr;
  }

  // Get current size of free list
  size_t size() const { return length.load(std::memory_order_relaxed); }
};

// ============================================================================
// BLOCK POOL CLASS
// ============================================================================
// Manages allocations of a specific block size
// Each pool handles one size class (16B, 32B, 64B, etc.)
class BlockPool {
  size_t block_size_;                 // Size of blocks in this pool (aligned)
  size_t blocks_per_chunk_;           // How many blocks per memory chunk
  std::vector<ChunkHeader *> chunks_; // List of allocated chunks
  FreeList free_list_;                // Lock-free list of free blocks
  std::atomic<uint64_t> total_allocated_; // Total memory allocated to pool
  std::atomic<uint64_t> active_blocks_;   // Currently allocated blocks
  std::mutex chunk_mutex_;                // Protects chunk allocation

public:
  // Initialize pool for a specific block size
  BlockPool(size_t block_size)
      : block_size_(align_up(block_size, 16)), total_allocated_(0),
        active_blocks_(0) {
    // Calculate optimal number of blocks per chunk
    blocks_per_chunk_ =
        DEFAULT_CHUNK_SIZE / (block_size_ + sizeof(BlockHeader));
    if (blocks_per_chunk_ < 4) {
      blocks_per_chunk_ = 4; // Ensure at least 4 blocks per chunk
    }
  }

  ~BlockPool() { clear(); }

  // Allocate a single block from this pool
  void *allocate() {
    // Try to get a block from free list first
    BlockHeader *header = free_list_.pop();
    if (!header) {
      // Free list empty, allocate new chunk
      header = allocate_new_chunk();
      if (!header) {
        return nullptr; // Out of memory
      }
    }

    // Set reference count to 1 (in-use)
    uint32_t expected = 0;
    while (!header->ref_count.compare_exchange_weak(
        expected, 1, std::memory_order_acq_rel, std::memory_order_relaxed)) {
      expected = 0;
    }

    active_blocks_.fetch_add(1, std::memory_order_relaxed);
    // Return pointer to user data (after header)
    return reinterpret_cast<uint8_t *>(header) + sizeof(BlockHeader);
  }

  // Deallocate a block back to this pool
  void deallocate(void *ptr) {
    if (!ptr)
      return;

    // Get header from user pointer
    BlockHeader *header = reinterpret_cast<BlockHeader *>(
        reinterpret_cast<uint8_t *>(ptr) - sizeof(BlockHeader));

    // Validate header to prevent memory corruption
    if (!header->validate()) {
      return; // Invalid pointer - ignore
    }

    // Decrement reference count
    uint32_t old_ref =
        header->ref_count.fetch_sub(1, std::memory_order_acq_rel);

    // If this was the last reference, return to free list
    if (old_ref == 1) {
      active_blocks_.fetch_sub(1, std::memory_order_relaxed);

      // Fill with 0xCC for debugging (like Visual Studio)
      std::memset(ptr, 0xCC, block_size_);

      // Clear next pointer and add to free list
      header->next_free = nullptr;
      free_list_.push(header);
    }
  }

  // Get the block size managed by this pool
  size_t block_size() const { return block_size_; }

  // Release all memory back to the system
  void clear() {
    std::lock_guard<std::mutex> lock(chunk_mutex_);

    // Free all chunks
    for (ChunkHeader *chunk : chunks_) {
      void *mem = reinterpret_cast<uint8_t *>(chunk) - sizeof(ChunkHeader);
      platform_munmap(mem, chunk->size);
    }
    chunks_.clear();

    // Reset all statistics
    free_list_.head.store(nullptr, std::memory_order_relaxed);
    free_list_.length.store(0, std::memory_order_relaxed);
    total_allocated_.store(0, std::memory_order_relaxed);
    active_blocks_.store(0, std::memory_order_relaxed);
  }

private:
  // Allocate a new chunk and divide it into blocks
  BlockHeader *allocate_new_chunk() {
    std::lock_guard<std::mutex> lock(chunk_mutex_);

    // Calculate chunk size including headers
    size_t chunk_size =
        (block_size_ + sizeof(BlockHeader)) * blocks_per_chunk_ +
        sizeof(ChunkHeader);
    chunk_size = align_up(chunk_size, 4096); // Align to page boundary

    // Allocate memory from OS
    void *mem = platform_mmap(chunk_size);
    if (!mem) {
      return nullptr; // Allocation failed
    }

    // Set up chunk header
    ChunkHeader *chunk_header = reinterpret_cast<ChunkHeader *>(mem);
    chunk_header->size = chunk_size;
    chunk_header->used = sizeof(ChunkHeader);
    chunk_header->free_blocks.store(blocks_per_chunk_,
                                    std::memory_order_relaxed);
    chunk_header->next = nullptr;

    chunks_.push_back(chunk_header);
    total_allocated_.fetch_add(chunk_size, std::memory_order_relaxed);

    // Divide chunk into blocks and add to free list
    uint8_t *chunk_start = reinterpret_cast<uint8_t *>(chunk_header);
    for (size_t i = 0; i < blocks_per_chunk_; ++i) {
      BlockHeader *block =
          reinterpret_cast<BlockHeader *>(chunk_start + chunk_header->used);

      // Initialize block header
      new (block) BlockHeader();
      block->meta.size = block_size_;
      block->meta.is_large = 0;
      block->meta.pool_id = get_pool_id(block_size_);
      block->ref_count.store(0, std::memory_order_relaxed);

      // Add to free list
      free_list_.push(block);
      chunk_header->used += sizeof(BlockHeader) + block_size_;
    }

    // Return one block for immediate use
    return free_list_.pop();
  }
};

// ============================================================================
// MAIN ALLOCATOR CLASS (SINGLETON)
// ============================================================================
// Manages all memory pools and large allocations
class MemAllocator {
  // Entry for each block size pool
  struct PoolEntry {
    std::unique_ptr<BlockPool> pool;           // The pool instance
    std::atomic<uint64_t> allocation_count{0}; // Statistics
  };

  // Fixed-size pools for small allocations
  std::vector<PoolEntry> fixed_pools_;

  // Synchronization for large allocations
  mutable std::shared_mutex large_alloc_mutex_;

  // Map of large allocations (user pointer -> header)
  std::unordered_map<void *, LargeBlockHeader *> large_blocks_;

  // System page size (cached)
  size_t page_size_;

  // Statistics
  std::atomic<uint64_t> total_allocated_;    // Total memory from OS
  std::atomic<uint64_t> total_used_;         // Memory currently in use
  std::atomic<uint64_t> allocation_count_;   // Total allocations
  std::atomic<uint64_t> deallocation_count_; // Total deallocations

  // Singleton pattern
  inline static std::once_flag init_flag_;
  inline static MemAllocator *instance_;

public:
  // Get the singleton instance
  static MemAllocator &get_instance() {
    std::call_once(init_flag_, []() { instance_ = new MemAllocator(); });
    return *instance_;
  }

  // Main allocation function
  void *allocate(size_t size, size_t alignment) {
    if (size == 0)
      return nullptr;

    // Route to appropriate handler based on size
    if (size <= MAX_FIXED_SIZE) {
      return allocate_fixed(size); // Small allocation
    } else {
      return allocate_large(size, alignment); // Large allocation
    }
  }

  // Main deallocation function
  void deallocate(void *ptr) {
    if (!ptr)
      return;

    // Check if this is a small or large allocation
    BlockHeader *header = reinterpret_cast<BlockHeader *>(
        reinterpret_cast<uint8_t *>(ptr) - sizeof(BlockHeader));

    if (header->validate() && !header->meta.is_large) {
      // Small allocation - route to appropriate pool
      size_t pool_id = header->meta.pool_id;
      if (pool_id < fixed_pools_.size() && fixed_pools_[pool_id].pool) {
        fixed_pools_[pool_id].pool->deallocate(ptr);
        deallocation_count_.fetch_add(1, std::memory_order_relaxed);
        total_used_.fetch_sub(header->meta.size, std::memory_order_relaxed);
      }
    } else {
      // Large allocation
      deallocate_large(ptr);
    }
  }

  // Reallocate memory (like realloc)
  void *reallocate(void *ptr, size_t new_size) {
    if (!ptr)
      return allocate(new_size, ALIGN_SIZE);

    // Get current size
    size_t old_size = get_size(ptr);
    if (new_size <= old_size) {
      return ptr; // No need to reallocate
    }

    // Allocate new block and copy data
    void *new_ptr = allocate(new_size, ALIGN_SIZE);
    if (!new_ptr)
      return nullptr;

    std::memcpy(new_ptr, ptr, old_size);
    deallocate(ptr);

    return new_ptr;
  }

  // Get the size of an allocated block
  size_t get_size(void *ptr) const {
    if (!ptr)
      return 0;

    BlockHeader *header = reinterpret_cast<BlockHeader *>(
        reinterpret_cast<uint8_t *>(ptr) - sizeof(BlockHeader));

    if (header->validate() && !header->meta.is_large) {
      return header->meta.size; // Small allocation
    } else {
      // Large allocation - look up in map
      std::shared_lock lock(large_alloc_mutex_);
      auto it = large_blocks_.find(ptr);
      if (it != large_blocks_.end() && it->second->validate()) {
        return it->second->size;
      }
    }

    return 0; // Not our allocation
  }

  // Clear all memory (release back to system)
  void clear() {
    // Clear all fixed pools
    for (auto &entry : fixed_pools_) {
      if (entry.pool) {
        entry.pool->clear();
      }
    }

    // Clear all large allocations
    {
      std::unique_lock lock(large_alloc_mutex_);
      for (auto &[ptr, header] : large_blocks_) {
        deallocate_large_impl(ptr, header);
      }
      large_blocks_.clear();
    }

    // Reset statistics
    total_allocated_.store(0);
    total_used_.store(0);
    allocation_count_.store(0);
    deallocation_count_.store(0);
  }

private:
  // Private constructor for singleton
  MemAllocator() {
    page_size_ = get_page_size();

    // Initialize all fixed pools (lazily created)
    for (size_t i = 0; i < MAX_POOL_COUNT; ++i) {
      fixed_pools_.emplace_back();
    }

    // Initialize statistics
    total_allocated_.store(0);
    total_used_.store(0);
    allocation_count_.store(0);
    deallocation_count_.store(0);
  }

  // Allocate from fixed-size pools
  void *allocate_fixed(size_t size) {
    size_t pool_id = get_pool_id(size);
    size_t actual_size = get_pool_size(pool_id);

    auto &entry = fixed_pools_[pool_id];

    // Create pool if it doesn't exist
    if (!entry.pool) {
      entry.pool = std::make_unique<BlockPool>(actual_size);
    }

    void *ptr = entry.pool->allocate();
    if (ptr) {
      // Update statistics
      allocation_count_.fetch_add(1, std::memory_order_relaxed);
      total_used_.fetch_add(actual_size, std::memory_order_relaxed);
      entry.allocation_count.fetch_add(1, std::memory_order_relaxed);

      // Update block header metadata
      BlockHeader *header = reinterpret_cast<BlockHeader *>(
          reinterpret_cast<uint8_t *>(ptr) - sizeof(BlockHeader));
      header->meta.size = actual_size;
      header->meta.is_large = 0;
      header->meta.pool_id = pool_id;
    }

    return ptr;
  }

  // Allocate large block (direct from OS)
  void *allocate_large(size_t size, size_t alignment) {
    // Calculate total size including header and alignment padding
    size_t total_size = sizeof(LargeBlockHeader) + alignment + size;
    total_size = align_up(total_size, page_size_);

    // Use mmap/VirtualAlloc for very large allocations
    bool use_mmap = (size >= LARGE_ALLOC_THRESHOLD);
    void *memory = nullptr;

    if (use_mmap) {
      memory = platform_mmap(total_size);
      if (!memory) {
        return nullptr;
      }
    } else {
      if (platform_memalign(total_size, alignment) != 0) {
        return nullptr;
      }
    }

    // Initialize large block header
    LargeBlockHeader *header = reinterpret_cast<LargeBlockHeader *>(memory);
    new (header) LargeBlockHeader();
    header->size = size;
    header->actual_size = total_size;
    header->is_mmap = use_mmap;

    // Calculate user pointer with proper alignment
    uintptr_t user_addr =
        reinterpret_cast<uintptr_t>(memory) + sizeof(LargeBlockHeader);
    uintptr_t aligned_addr = align_up(user_addr, alignment);
    void *user_ptr = reinterpret_cast<void *>(aligned_addr);

    // Add to tracking map
    {
      std::unique_lock lock(large_alloc_mutex_);
      large_blocks_[user_ptr] = header;
    }

    // Update statistics
    allocation_count_.fetch_add(1, std::memory_order_relaxed);
    total_allocated_.fetch_add(total_size, std::memory_order_relaxed);
    total_used_.fetch_add(size, std::memory_order_relaxed);

    return user_ptr;
  }

  // Deallocate large block
  void deallocate_large(void *ptr) {
    LargeBlockHeader *header = nullptr;

    // Remove from tracking map
    {
      std::unique_lock lock(large_alloc_mutex_);
      auto it = large_blocks_.find(ptr);
      if (it != large_blocks_.end()) {
        header = it->second;
        large_blocks_.erase(it);
      }
    }

    // Actually free the memory
    if (header && header->validate()) {
      deallocate_large_impl(ptr, header);
      deallocation_count_.fetch_add(1, std::memory_order_relaxed);
      total_used_.fetch_sub(header->size, std::memory_order_relaxed);
    }
  }

  // Internal implementation of large block deallocation
  void deallocate_large_impl(void *ptr, LargeBlockHeader *header) {
    // Calculate original allocation address
    uintptr_t user_addr = reinterpret_cast<uintptr_t>(ptr);
    uintptr_t header_addr = user_addr - sizeof(LargeBlockHeader);
    void *memory =
        reinterpret_cast<void *>(align_down(header_addr, page_size_));

    // Free using appropriate method
    if (header->is_mmap) {
      platform_munmap(memory, header->actual_size);
    } else {
      platform_free_aligned(memory);
    }
  }
};

} // namespace details

// ============================================================================
// PUBLIC INTERFACE FUNCTIONS
// ============================================================================
// These functions provide the main API for using the memory pool

// Allocate memory with specified size and alignment
inline void *allocate(size_t size, size_t alignment = ALIGN_SIZE) {
  return details::MemAllocator::get_instance().allocate(size, alignment);
}

// Deallocate previously allocated memory
inline void deallocate(void *ptr) {
  details::MemAllocator::get_instance().deallocate(ptr);
}

// Reallocate memory to new size
inline void *reallocate(void *ptr, size_t new_size) {
  return details::MemAllocator::get_instance().reallocate(ptr, new_size);
}

// Get the size of an allocated block
inline size_t get_block_size(void *ptr) {
  return details::MemAllocator::get_instance().get_size(ptr);
}

// Clear all memory and reset the allocator
inline void clear() { details::MemAllocator::get_instance().clear(); }

// ============================================================================
// STL-COMPATIBLE ALLOCATOR
// ============================================================================
// Allows using the memory pool with STL containers

template <typename T> class Allocator {
public:
  using value_type = T;
  Allocator() = default;

  // Conversion constructor for different types
  template <typename U> Allocator(const Allocator<U> &) {}

  // Allocate memory for n objects of type T
  T *allocate(size_t n) {
    return static_cast<T *>(allocate(n * sizeof(T), alignof(T)));
  }

  // Deallocate memory
  void deallocate(T *p, size_t) { deallocate(p); }

  // Equality operators (all allocators are equal)
  template <typename U> bool operator==(const Allocator<U> &) const {
    return true;
  }

  template <typename U> bool operator!=(const Allocator<U> &) const {
    return false;
  }
};

} // namespace mempool