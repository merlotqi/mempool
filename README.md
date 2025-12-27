# MemPool: High-Performance Cross-Platform Memory Pool

A lightweight, thread-safe, and efficient memory allocator designed for C++ applications requiring frequent allocation and deallocation of small-to-medium sized objects.

## 🚀 Key Features

* **Two-Level Allocation Strategy**:
* **Small Objects ( 4KB)**: Managed by fixed-size block pools (power-of-two sizes) for  performance.
* **Large Objects (> 4KB)**: Direct system allocation with page alignment.


* **Lock-Free Free Lists**: Utilizes atomic CAS (Compare-And-Swap) operations for small block management to minimize thread contention.
* **Thread Safety**: High-concurrency support using a mix of lock-free structures and shared mutexes for large block tracking.
* **Cross-Platform**: Native support for **Windows** (VirtualAlloc), **Linux**, and **macOS** (mmap).
* **STL Compatible**: Includes a standard-compliant `mempool::Allocator<T>` for use with `std::vector`, `std::map`, etc.
* **Memory Hygiene**: Magic number validation and debug memory filling (`0xCC`) to detect double-frees and corruption.

---

## 🛠 Usage

### Basic Allocation

```cpp
#include "mempool.hpp"

// Allocate 1024 bytes
void* ptr = mempool::allocate(1024);

// Use memory...

// Deallocate
mempool::deallocate(ptr);

```

### Integration with STL

```cpp
#include <vector>
#include "mempool.hpp"

int main() {
    // Use the custom allocator with std::vector
    std::vector<int, mempool::Allocator<int>> my_vec;
    
    for(int i = 0; i < 1000; ++i) {
        my_vec.push_back(i);
    }
    
    return 0;
}

```

---

## 📐 Architecture

The library organizes memory into **Chunks** and **Blocks**:

1. **Chunk**: A large memory region (default 64KB) requested from the OS.
2. **Block**: A subdivision of a Chunk. Each block contains a `BlockHeader` for metadata and a payload for user data.
3. **FreeList**: A lock-free stack that keeps track of available blocks within a specific size pool.

---

## ⚙️ Configuration

You can tune the following constants in `mempool.hpp`:

| Constant | Default Value | Description |
| --- | --- | --- |
| `MIN_BLOCK_SIZE` | 16 Bytes | Smallest allocatable unit. |
| `MAX_FIXED_SIZE` | 4096 Bytes | Upper limit for fixed-pool allocation. |
| `DEFAULT_CHUNK_SIZE` | 64 KB | Size of memory chunks requested from OS. |
| `ALIGN_SIZE` | 16 Bytes | Default alignment for all pointers. |

---

## 📊 Performance Characteristics

* **Allocation**:  for small objects by popping from a lock-free stack.
* **Deallocation**:  by pushing back to the stack.
* **Fragmentation**: Significantly reduced by reusing fixed-size blocks.
* **Concurrency**: Excellent scaling on multi-core systems due to the reduction of global heap locks.

---

## ⚖️ License

This project is licensed under the **MIT License**.
