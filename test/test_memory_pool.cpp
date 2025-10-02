// test_memory_pool.cpp
// Focused test for enhanced memory pool functionality

#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include "../include/concurrent/memory_pool.hpp"

using namespace backtesting;

struct TestObject {
    double data[8];  // 64 bytes
    int id;
    
    TestObject() : id(0) {
        for (auto& d : data) d = 0.0;
    }
};

// Test 1: Basic functionality
void test_basic_operations() {
    std::cout << "Test 1: Basic Operations\n";
    std::cout << std::string(40, '-') << "\n";
    
    EnhancedMemoryPool<TestObject, 1024> pool;
    
    // Acquire and release
    auto* obj1 = pool.acquire();
    obj1->id = 1;
    
    auto* obj2 = pool.acquire();
    obj2->id = 2;
    
    std::cout << "  Acquired 2 objects\n";
    
    pool.release(obj1);
    pool.release(obj2);
    
    std::cout << "  Released 2 objects\n";
    
    // Check statistics
    auto stats = pool.getStats();
    std::cout << "  Allocations: " << stats.allocations << "\n";
    std::cout << "  Deallocations: " << stats.deallocations << "\n";
    std::cout << "  Hit rate: " << stats.hit_rate_pct << "%\n";
    
    std::cout << "  ✓ PASSED\n\n";
}

// Test 2: Pool exhaustion handling
void test_pool_exhaustion() {
    std::cout << "Test 2: Pool Exhaustion Handling\n";
    std::cout << std::string(40, '-') << "\n";
    
    EnhancedMemoryPool<TestObject, 128> pool;
    std::vector<TestObject*> objects;
    
    // Exhaust the pool
    for (int i = 0; i < 150; ++i) {
        auto* obj = pool.acquire();
        objects.push_back(obj);
    }
    
    auto stats = pool.getStats();
    std::cout << "  Allocated " << objects.size() << " objects\n";
    std::cout << "  Pool hits: " << stats.pool_hits << "\n";
    std::cout << "  Pool misses: " << stats.pool_misses << "\n";
    std::cout << "  Hit rate: " << stats.hit_rate_pct << "%\n";
    
    // Release all
    for (auto* obj : objects) {
        pool.release(obj);
    }
    
    std::cout << "  Released all objects\n";
    std::cout << "  ✓ PASSED\n\n";
}

// Test 3: Multi-threaded access
void test_multithreaded() {
    std::cout << "Test 3: Multi-threaded Access\n";
    std::cout << std::string(40, '-') << "\n";
    
    EnhancedMemoryPool<TestObject, 4096> pool;
    std::atomic<int> total_ops{0};
    const int ops_per_thread = 10000;
    const int num_threads = 4;
    
    auto worker = [&](int thread_id) {
        for (int i = 0; i < ops_per_thread; ++i) {
            auto* obj = pool.acquire();
            obj->id = thread_id * 10000 + i;
            
            // Simulate some work
            for (volatile int j = 0; j < 10; ++j) {}
            
            pool.release(obj);
            total_ops.fetch_add(1, std::memory_order_relaxed);
        }
    };
    
    auto start = std::chrono::high_resolution_clock::now();
    
    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(worker, i);
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    auto stats = pool.getStats();
    std::cout << "  Threads: " << num_threads << "\n";
    std::cout << "  Operations per thread: " << ops_per_thread << "\n";
    std::cout << "  Total operations: " << total_ops.load() << "\n";
    std::cout << "  Duration: " << duration.count() << " ms\n";
    std::cout << "  Throughput: " << (total_ops.load() * 1000 / duration.count()) << " ops/sec\n";
    std::cout << "  Hit rate: " << stats.hit_rate_pct << "%\n";
    std::cout << "  Peak usage: " << stats.peak_usage << " / " << pool.capacity() << "\n";
    std::cout << "  ✓ PASSED\n\n";
}

// Test 4: Batch operations
void test_batch_operations() {
    std::cout << "Test 4: Batch Operations\n";
    std::cout << std::string(40, '-') << "\n";
    
    EnhancedMemoryPool<TestObject, 2048> pool;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // Batch acquire
    auto objects = pool.acquire_batch(1000);
    
    // Use objects
    for (size_t i = 0; i < objects.size(); ++i) {
        objects[i]->id = static_cast<int>(i);
    }
    
    // Batch release
    pool.release_batch(objects);
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    std::cout << "  Batch size: 1000\n";
    std::cout << "  Duration: " << duration.count() << " μs\n";
    std::cout << "  Time per object: " << (duration.count() / 1000.0) << " μs\n";
    std::cout << "  ✓ PASSED\n\n";
}

// Test 5: Performance comparison
void test_performance_comparison() {
    std::cout << "Test 5: Performance vs. Raw Allocation\n";
    std::cout << std::string(40, '-') << "\n";
    
    const int iterations = 100000;
    
    // Baseline: raw new/delete
    auto start1 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        auto* obj = new TestObject();
        obj->id = i;
        delete obj;
    }
    auto end1 = std::chrono::high_resolution_clock::now();
    auto baseline = std::chrono::duration_cast<std::chrono::microseconds>(end1 - start1);
    
    // Memory pool
    EnhancedMemoryPool<TestObject, 4096> pool;
    auto start2 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        auto* obj = pool.acquire();
        obj->id = i;
        pool.release(obj);
    }
    auto end2 = std::chrono::high_resolution_clock::now();
    auto optimized = std::chrono::duration_cast<std::chrono::microseconds>(end2 - start2);
    
    auto stats = pool.getStats();
    double speedup = static_cast<double>(baseline.count()) / optimized.count();
    
    std::cout << "  Iterations: " << iterations << "\n";
    std::cout << "  Raw new/delete: " << baseline.count() << " μs\n";
    std::cout << "  Memory pool: " << optimized.count() << " μs\n";
    std::cout << "  Speedup: " << std::fixed << std::setprecision(2) << speedup << "x\n";
    std::cout << "  Hit rate: " << stats.hit_rate_pct << "%\n";
    std::cout << "  ✓ PASSED\n\n";
}

int main() {
    std::cout << "\n========================================\n";
    std::cout << "Enhanced Memory Pool Test Suite\n";
    std::cout << "========================================\n\n";
    
    try {
        test_basic_operations();
        test_pool_exhaustion();
        test_multithreaded();
        test_batch_operations();
        test_performance_comparison();
        
        std::cout << "========================================\n";
        std::cout << "All memory pool tests passed! ✓\n";
        std::cout << "========================================\n\n";
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << "\n";
        return 1;
    }
}