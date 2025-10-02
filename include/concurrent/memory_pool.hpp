// memory_pool.hpp
// Enhanced Lock-Free Memory Pool with Per-Thread Caching
// Optimized for high-frequency object allocation/deallocation

#pragma once

#include <atomic>
#include <array>
#include <vector>
#include <memory>
#include <thread>
#include <cstdint>
#include <new>

namespace backtesting {

// ============================================================================
// Per-Thread Local Cache for Reduced Contention
// ============================================================================

template<typename T, size_t LocalCacheSize = 32>
class ThreadLocalCache {
private:
    std::array<T*, LocalCacheSize> cache_;
    size_t count_ = 0;
    
public:
    ThreadLocalCache() {
        cache_.fill(nullptr);
    }
    
    ~ThreadLocalCache() {
        // Return all cached objects to global pool
        for (size_t i = 0; i < count_; ++i) {
            if (cache_[i]) {
                ::operator delete(cache_[i]);
            }
        }
    }
    
    T* try_acquire() {
        if (count_ > 0) {
            return cache_[--count_];
        }
        return nullptr;
    }
    
    bool try_release(T* obj) {
        if (count_ < LocalCacheSize) {
            cache_[count_++] = obj;
            return true;
        }
        return false;
    }
    
    size_t size() const { return count_; }
    bool empty() const { return count_ == 0; }
    bool full() const { return count_ >= LocalCacheSize; }
};

// ============================================================================
// Enhanced Memory Pool with Multiple Allocation Strategies
// ============================================================================

template<typename T, size_t PoolSize = 4096>
class EnhancedMemoryPool {
private:
    // Cache-aligned storage for pool objects
    static constexpr size_t CACHE_LINE_SIZE = 64;
    
    struct alignas(CACHE_LINE_SIZE) PoolNode {
        union {
            T object;
            PoolNode* next;
        };
        std::atomic<bool> in_use{false};
    };
    
    // Pool storage
    std::unique_ptr<std::array<PoolNode, PoolSize>> pool_;
    
    // Free list head
    std::atomic<PoolNode*> free_list_head_{nullptr};
    
    // Statistics
    std::atomic<uint64_t> allocations_{0};
    std::atomic<uint64_t> deallocations_{0};
    std::atomic<uint64_t> pool_hits_{0};
    std::atomic<uint64_t> pool_misses_{0};
    std::atomic<uint64_t> current_usage_{0};
    std::atomic<uint64_t> peak_usage_{0};
    
    // Thread-local caches
    thread_local static ThreadLocalCache<T, 32> local_cache_;
    
    // Initialize free list
    void initialize_free_list() {
        for (size_t i = 0; i < PoolSize - 1; ++i) {
            (*pool_)[i].next = &(*pool_)[i + 1];
        }
        (*pool_)[PoolSize - 1].next = nullptr;
        free_list_head_.store(&(*pool_)[0], std::memory_order_release);
    }
    
    // Lock-free acquire from free list
    PoolNode* acquire_from_free_list() {
        PoolNode* head = free_list_head_.load(std::memory_order_acquire);
        
        while (head != nullptr) {
            PoolNode* next = head->next;
            if (free_list_head_.compare_exchange_weak(
                    head, next,
                    std::memory_order_release,
                    std::memory_order_acquire)) {
                head->in_use.store(true, std::memory_order_release);
                return head;
            }
        }
        
        return nullptr;
    }
    
    // Lock-free release to free list
    void release_to_free_list(PoolNode* node) {
        node->in_use.store(false, std::memory_order_release);
        
        PoolNode* head = free_list_head_.load(std::memory_order_acquire);
        do {
            node->next = head;
        } while (!free_list_head_.compare_exchange_weak(
                    head, node,
                    std::memory_order_release,
                    std::memory_order_acquire));
    }
    
    // Update peak usage
    void update_peak_usage(uint64_t current) {
        uint64_t peak = peak_usage_.load(std::memory_order_relaxed);
        while (current > peak) {
            if (peak_usage_.compare_exchange_weak(
                    peak, current,
                    std::memory_order_relaxed,
                    std::memory_order_relaxed)) {
                break;
            }
        }
    }
    
public:
    EnhancedMemoryPool() {
        pool_ = std::make_unique<std::array<PoolNode, PoolSize>>();
        initialize_free_list();
    }
    
    // Non-copyable, non-movable
    EnhancedMemoryPool(const EnhancedMemoryPool&) = delete;
    EnhancedMemoryPool& operator=(const EnhancedMemoryPool&) = delete;
    
    // Acquire object with thread-local caching
    template<typename... Args>
    T* acquire(Args&&... args) {
        allocations_.fetch_add(1, std::memory_order_relaxed);
        
        // Try thread-local cache first (no contention)
        T* obj = local_cache_.try_acquire();
        if (obj) {
            pool_hits_.fetch_add(1, std::memory_order_relaxed);
            new (obj) T(std::forward<Args>(args)...);
            return obj;
        }
        
        // Try global pool
        PoolNode* node = acquire_from_free_list();
        if (node) {
            pool_hits_.fetch_add(1, std::memory_order_relaxed);
            uint64_t current = current_usage_.fetch_add(1, std::memory_order_relaxed) + 1;
            update_peak_usage(current);
            
            new (&node->object) T(std::forward<Args>(args)...);
            return &node->object;
        }
        
        // Pool exhausted - fallback to heap allocation
        pool_misses_.fetch_add(1, std::memory_order_relaxed);
        return new T(std::forward<Args>(args)...);
    }
    
    // Release object with thread-local caching
    void release(T* obj) {
        if (!obj) return;
        
        deallocations_.fetch_add(1, std::memory_order_relaxed);
        
        // Destroy object
        obj->~T();
        
        // Try thread-local cache first
        if (local_cache_.try_release(obj)) {
            return;
        }
        
        // Check if object is from pool
        uintptr_t obj_addr = reinterpret_cast<uintptr_t>(obj);
        uintptr_t pool_start = reinterpret_cast<uintptr_t>(pool_->data());
        uintptr_t pool_end = pool_start + sizeof(PoolNode) * PoolSize;
        
        if (obj_addr >= pool_start && obj_addr < pool_end) {
            // Object from pool - return to free list
            PoolNode* node = reinterpret_cast<PoolNode*>(
                pool_start + ((obj_addr - pool_start) / sizeof(PoolNode)) * sizeof(PoolNode)
            );
            release_to_free_list(node);
            current_usage_.fetch_sub(1, std::memory_order_relaxed);
        } else {
            // Heap-allocated object
            delete obj;
        }
    }
    
    // Batch acquire (more efficient for multiple allocations)
    template<typename... Args>
    std::vector<T*> acquire_batch(size_t count, Args&&... args) {
        std::vector<T*> objects;
        objects.reserve(count);
        
        for (size_t i = 0; i < count; ++i) {
            objects.push_back(acquire(std::forward<Args>(args)...));
        }
        
        return objects;
    }
    
    // Batch release
    void release_batch(const std::vector<T*>& objects) {
        for (T* obj : objects) {
            release(obj);
        }
    }
    
    // Statistics
    struct PoolStats {
        uint64_t allocations;
        uint64_t deallocations;
        uint64_t pool_hits;
        uint64_t pool_misses;
        uint64_t current_usage;
        uint64_t peak_usage;
        double hit_rate_pct;
        double utilization_pct;
    };
    
    PoolStats getStats() const {
        auto allocs = allocations_.load(std::memory_order_relaxed);
        auto deallocs = deallocations_.load(std::memory_order_relaxed);
        auto hits = pool_hits_.load(std::memory_order_relaxed);
        auto misses = pool_misses_.load(std::memory_order_relaxed);
        auto current = current_usage_.load(std::memory_order_relaxed);
        auto peak = peak_usage_.load(std::memory_order_relaxed);
        
        double hit_rate = allocs > 0 ? 
            (100.0 * hits) / allocs : 100.0;
        double utilization = (100.0 * peak) / PoolSize;
        
        return {
            allocs, deallocs, hits, misses,
            current, peak,
            hit_rate, utilization
        };
    }
    
    void resetStats() {
        allocations_.store(0, std::memory_order_relaxed);
        deallocations_.store(0, std::memory_order_relaxed);
        pool_hits_.store(0, std::memory_order_relaxed);
        pool_misses_.store(0, std::memory_order_relaxed);
        // Don't reset current_usage or peak_usage as they track actual state
    }
    
    // Capacity info
    constexpr size_t capacity() const { return PoolSize; }
    size_t available() const { 
        return PoolSize - current_usage_.load(std::memory_order_relaxed);
    }
};

// Thread-local cache definition
template<typename T, size_t PoolSize>
thread_local ThreadLocalCache<T, 32> EnhancedMemoryPool<T, PoolSize>::local_cache_;

} // namespace backtesting