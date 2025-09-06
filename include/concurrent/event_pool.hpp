// event_pool.hpp
// Object Pool Implementation for High-Performance Event Management
// Provides lock-free object pooling to reduce memory allocation overhead

#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace backtesting {

// ============================================================================
// Event Factory with Object Pooling
// ============================================================================

template<typename T>
class EventPool {
private:
    static constexpr size_t POOL_SIZE = 1024;
    std::array<T, POOL_SIZE> pool_;
    std::atomic<size_t> next_free_{0};
    std::array<std::atomic<bool>, POOL_SIZE> in_use_{};
    std::atomic<uint64_t> allocations_{0};
    std::atomic<uint64_t> deallocations_{0};
    std::atomic<uint64_t> pool_misses_{0};
    
public:
    EventPool() {
        for (auto& flag : in_use_) {
            flag = false;
        }
    }
    
    T* acquire() {
        for (size_t attempts = 0; attempts < POOL_SIZE * 2; ++attempts) {
            size_t idx = next_free_.fetch_add(1, std::memory_order_relaxed) % POOL_SIZE;
            bool expected = false;
            if (in_use_[idx].compare_exchange_strong(expected, true, 
                                                     std::memory_order_acquire)) {
                allocations_.fetch_add(1, std::memory_order_relaxed);
                return &pool_[idx];
            }
        }
        pool_misses_.fetch_add(1, std::memory_order_relaxed);
        return nullptr;  // Pool exhausted
    }
    
    void release(T* obj) {
        if (!obj) return;
        size_t idx = obj - pool_.data();
        if (idx < POOL_SIZE) {
            *obj = T{};  // Reset object
            in_use_[idx].store(false, std::memory_order_release);
            deallocations_.fetch_add(1, std::memory_order_relaxed);
        }
    }
    
    struct PoolStats {
        uint64_t allocations;
        uint64_t deallocations;
        uint64_t pool_misses;
        double hit_rate_pct;
    };
    
    PoolStats getStats() const {
        auto allocs = allocations_.load(std::memory_order_relaxed);
        auto deallocs = deallocations_.load(std::memory_order_relaxed);
        auto misses = pool_misses_.load(std::memory_order_relaxed);
        double hit_rate = allocs > 0 ? 
            (1.0 - static_cast<double>(misses) / allocs) * 100.0 : 100.0;
        
        return { allocs, deallocs, misses, hit_rate };
    }
};

} // namespace backtesting
