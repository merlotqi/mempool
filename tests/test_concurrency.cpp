#include <gtest/gtest.h>
#include <mempool.h>
#include <thread>

TEST(MemPoolConcurrency, ThreadLocalIsolation) {
    const int num_threads = 8;
    const int allocs_per_thread = 1000;
    std::vector<std::thread> threads;

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&]() {
            std::vector<void*> ptrs;
            for (int j = 0; j < allocs_per_thread; ++j) {
                ptrs.push_back(allocate(32));
            }
            for (void* p : ptrs) {
                deallocate(p);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }
}

TEST(MemPoolConcurrency, CrossThreadFree) {
    void* shared_ptr = nullptr;
    std::thread t1([&]() {
        shared_ptr = allocate(64);
    });
    t1.join();

    std::thread t2([&]() {
        deallocate(shared_ptr);
    });
    t2.join();
}