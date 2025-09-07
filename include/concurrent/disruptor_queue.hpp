// disruptor_queue.hpp
// Lock-Free Disruptor Queue Implementation for Statistical Arbitrage Backtesting Engine
// High-performance, cache-aligned ring buffer with statistics

#pragma once

#include <atomic>
#include <array>
#include <optional>
#include <cstdint>
#include <thread>
#include <new>
#ifdef __x86_64__
    #include <immintrin.h>  // For CPU pause instruction on x86/x64
#endif

// Portable fallback for hardware interference sizes
#if defined(__cpp_lib_hardware_interference_size) && __cpp_lib_hardware_interference_size >= 201703L
constexpr std::size_t kConstructive = std::hardware_constructive_interference_size;
constexpr std::size_t kDestructive  = std::hardware_destructive_interference_size;
#else
// Fallback guesses. Many modern CPUs use 64B; Apple Silicon coherence granule is often 128B.
// Using more conservative alignment for ARM compatibility
#ifdef __aarch64__
constexpr std::size_t kConstructive = 64;  // Conservative for ARM
constexpr std::size_t kDestructive  = 64;  // Reduced from 128 for ARM compatibility
#else
constexpr std::size_t kConstructive = 64;
constexpr std::size_t kDestructive  = 128;
#endif
#endif

static_assert((kConstructive & (kConstructive - 1)) == 0, "kConstructive must be power of two");
static_assert((kDestructive  & (kDestructive  - 1)) == 0, "kDestructive must be power of two");


namespace backtesting {

// ============================================================================
// Lock-Free Disruptor Queue Implementation (Enhanced with Stats)
// ============================================================================

template<typename T, size_t Size>
class DisruptorQueue {
    static_assert((Size & (Size - 1)) == 0, "Size must be power of 2");
    
private:
    // Cache line size for padding (typical x86_64)
    static constexpr size_t CACHE_LINE_SIZE = 64;
    
    // // Ring buffer storage
    // alignas(CACHE_LINE_SIZE) std::array<T, Size> buffer_;
    
    // // Sequence counters with cache line padding to prevent false sharing
    // alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> write_sequence_{0};
    // alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> read_sequence_{0};
    // alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> cached_read_sequence_{0};
    // alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> cached_write_sequence_{0};
    
    // // Performance statistics
    // alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> total_published_{0};
    // alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> total_consumed_{0};
    // alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> failed_publishes_{0};

    // Ring buffer storage - using dynamic allocation for large arrays
    std::unique_ptr<std::array<T, Size>> buffer_;

    // Sequence counters - removing alignment for ARM compatibility testing  
    std::atomic<std::uint64_t> write_sequence_{0};
    std::atomic<std::uint64_t> read_sequence_{0};
    std::atomic<std::uint64_t> cached_read_sequence_{0};
    std::atomic<std::uint64_t> cached_write_sequence_{0};

    // Performance statistics - removing alignment for ARM compatibility testing
    std::atomic<std::uint64_t> total_published_{0};
    std::atomic<std::uint64_t> total_consumed_{0};
    std::atomic<std::uint64_t> failed_publishes_{0};
  
    static constexpr uint64_t MASK = Size - 1;
    
    // CPU pause for spin-wait loops (reduces power consumption)
    inline void cpu_pause() const {
        #ifdef __x86_64__
            _mm_pause();
        #else
            std::this_thread::yield();
        #endif
    }
    
public:
    DisruptorQueue() {
        // Allocate buffer dynamically to avoid stack overflow on large sizes
        buffer_ = std::make_unique<std::array<T, Size>>();
    }
    
    // Non-copyable, non-movable for safety
    DisruptorQueue(const DisruptorQueue&) = delete;
    DisruptorQueue& operator=(const DisruptorQueue&) = delete;
    DisruptorQueue(DisruptorQueue&&) = delete;
    DisruptorQueue& operator=(DisruptorQueue&&) = delete;
    
    // Producer interface - returns true if published successfully
    bool try_publish(const T& item) {
        const uint64_t current_write = write_sequence_.load(std::memory_order_relaxed);
        const uint64_t next_write = current_write + 1;
        
        // Check if buffer is full (with caching for performance)
        uint64_t cached_read = cached_read_sequence_.load(std::memory_order_relaxed);
        if (next_write > cached_read + Size) {
            cached_read = read_sequence_.load(std::memory_order_acquire);
            cached_read_sequence_.store(cached_read, std::memory_order_relaxed);
            
            if (next_write > cached_read + Size) {
                failed_publishes_.fetch_add(1, std::memory_order_relaxed);
                return false;  // Queue is full
            }
        }
        
        // Write to buffer
        (*buffer_)[current_write & MASK] = item;
        
        // Publish the write
        write_sequence_.store(next_write, std::memory_order_release);
        total_published_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    
    // Blocking publish with spin-wait
    void publish(const T& item) {
        while (!try_publish(item)) {
            cpu_pause();
        }
    }
    
    // Consumer interface - returns empty optional if no data available
    std::optional<T> try_consume() {
        const uint64_t current_read = read_sequence_.load(std::memory_order_relaxed);
        
        // Check if data is available (with caching)
        uint64_t cached_write = cached_write_sequence_.load(std::memory_order_relaxed);
        if (current_read >= cached_write) {
            cached_write = write_sequence_.load(std::memory_order_acquire);
            cached_write_sequence_.store(cached_write, std::memory_order_relaxed);
            
            if (current_read >= cached_write) {
                return std::nullopt;  // No data available
            }
        }
        
        // Read from buffer
        T item = (*buffer_)[current_read & MASK];
        
        // Update read sequence
        read_sequence_.store(current_read + 1, std::memory_order_release);
        total_consumed_.fetch_add(1, std::memory_order_relaxed);
        return item;
    }
    
    // Blocking consume with spin-wait
    T consume() {
        std::optional<T> item;
        while (!(item = try_consume())) {
            cpu_pause();
        }
        return *item;
    }
    
    // Utility methods
    bool empty() const {
        return read_sequence_.load(std::memory_order_acquire) >= 
               write_sequence_.load(std::memory_order_acquire);
    }
    
    size_t size() const {
        const uint64_t write = write_sequence_.load(std::memory_order_acquire);
        const uint64_t read = read_sequence_.load(std::memory_order_acquire);
        return write >= read ? write - read : 0;
    }
    
    constexpr size_t capacity() const { return Size; }
    
    // Performance statistics
    struct QueueStats {
        uint64_t total_published;
        uint64_t total_consumed;
        uint64_t failed_publishes;
        size_t current_size;
        double utilization_pct;
    };
    
    QueueStats getStats() const {
        auto pub = total_published_.load(std::memory_order_relaxed);
        auto con = total_consumed_.load(std::memory_order_relaxed);
        auto fail = failed_publishes_.load(std::memory_order_relaxed);
        auto sz = size();
        
        return {
            pub, con, fail, sz,
            (static_cast<double>(sz) / Size) * 100.0
        };
    }
    
    void resetStats() {
        total_published_.store(0, std::memory_order_relaxed);
        total_consumed_.store(0, std::memory_order_relaxed);
        failed_publishes_.store(0, std::memory_order_relaxed);
    }
};

} // namespace backtesting
