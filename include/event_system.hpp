// event_system.hpp
// High-Performance Event System Foundation for Statistical Arbitrage Backtesting Engine
// Implements lock-free Disruptor pattern for ultra-low latency event processing

#pragma once

#include <chrono>
#include <string>
#include <variant>
#include <atomic>
#include <memory>
#include <array>
#include <functional>
#include <optional>
#include <cstdint>
#include <algorithm>
#include <thread>
#ifdef __x86_64__
    #include <immintrin.h>  // For CPU pause instruction on x86/x64
#endif

namespace backtesting {

// ============================================================================
// Core Event Types
// ============================================================================

struct Event {
    std::chrono::nanoseconds timestamp;
    uint64_t sequence_id;
    
    Event() : timestamp(0), sequence_id(0) {}
    Event(std::chrono::nanoseconds ts, uint64_t seq) 
        : timestamp(ts), sequence_id(seq) {}
};

struct MarketEvent : Event {
    std::string symbol;
    double open, high, low, close, volume;
    double bid, ask;  // For spread modeling
    
    MarketEvent() : Event(), open(0), high(0), low(0), close(0), 
                    volume(0), bid(0), ask(0) {}
};

struct SignalEvent : Event {
    std::string symbol;
    enum class Direction { LONG, SHORT, EXIT };
    Direction direction;
    double strength;  // Signal confidence [0,1]
    
    SignalEvent() : Event(), direction(Direction::EXIT), strength(0.0) {}
};

struct OrderEvent : Event {
    std::string symbol;
    enum class Type { MARKET, LIMIT };
    enum class Direction { BUY, SELL };
    Type order_type;
    Direction direction;
    int quantity;
    double price;  // For limit orders
    
    OrderEvent() : Event(), order_type(Type::MARKET), 
                   direction(Direction::BUY), quantity(0), price(0.0) {}
};

struct FillEvent : Event {
    std::string symbol;
    int quantity;
    double fill_price;
    double commission;
    double slippage;
    
    FillEvent() : Event(), quantity(0), fill_price(0.0), 
                  commission(0.0), slippage(0.0) {}
};

using EventVariant = std::variant<MarketEvent, SignalEvent, OrderEvent, FillEvent>;

// ============================================================================
// Lock-Free Disruptor Queue Implementation
// ============================================================================

template<typename T, size_t Size>
class DisruptorQueue {
    static_assert((Size & (Size - 1)) == 0, "Size must be power of 2");
    
private:
    // Cache line size for padding (typical x86_64)
    static constexpr size_t CACHE_LINE_SIZE = 64;
    
    // Ring buffer storage
    alignas(CACHE_LINE_SIZE) std::array<T, Size> buffer_;
    
    // Sequence counters with cache line padding to prevent false sharing
    alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> write_sequence_{0};
    alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> read_sequence_{0};
    alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> cached_read_sequence_{0};
    alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> cached_write_sequence_{0};
    
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
    DisruptorQueue() = default;
    
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
                return false;  // Queue is full
            }
        }
        
        // Write to buffer
        buffer_[current_write & MASK] = item;
        
        // Publish the write
        write_sequence_.store(next_write, std::memory_order_release);
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
        T item = buffer_[current_read & MASK];
        
        // Update read sequence
        read_sequence_.store(current_read + 1, std::memory_order_release);
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
};

// ============================================================================
// Event Handler Interfaces
// ============================================================================

class IEventHandler {
public:
    virtual ~IEventHandler() = default;
    virtual void onEvent(const EventVariant& event) = 0;
};

class IDataHandler {
public:
    virtual ~IDataHandler() = default;
    virtual bool hasMoreData() const = 0;
    virtual void updateBars() = 0;
    virtual std::optional<MarketEvent> getLatestBar(const std::string& symbol) const = 0;
};

class IStrategy {
public:
    virtual ~IStrategy() = default;
    virtual void calculateSignals(const MarketEvent& event) = 0;
    virtual void reset() = 0;
};

class IPortfolio {
public:
    virtual ~IPortfolio() = default;
    virtual void updateSignal(const SignalEvent& event) = 0;
    virtual void updateFill(const FillEvent& event) = 0;
    virtual void updateMarket(const MarketEvent& event) = 0;
    virtual double getEquity() const = 0;
};

class IExecutionHandler {
public:
    virtual ~IExecutionHandler() = default;
    virtual void executeOrder(const OrderEvent& event) = 0;
};

// ============================================================================
// Event Visitor for Type-Safe Dispatch
// ============================================================================

class EventDispatcher {
private:
    IStrategy* strategy_;
    IPortfolio* portfolio_;
    IExecutionHandler* execution_;
    
public:
    EventDispatcher(IStrategy* strat, IPortfolio* port, IExecutionHandler* exec)
        : strategy_(strat), portfolio_(port), execution_(exec) {}
    
    void operator()(const MarketEvent& e) {
        if (portfolio_) portfolio_->updateMarket(e);
        if (strategy_) strategy_->calculateSignals(e);
    }
    
    void operator()(const SignalEvent& e) {
        if (portfolio_) portfolio_->updateSignal(e);
    }
    
    void operator()(const OrderEvent& e) {
        if (execution_) execution_->executeOrder(e);
    }
    
    void operator()(const FillEvent& e) {
        if (portfolio_) portfolio_->updateFill(e);
    }
};

// ============================================================================
// Main Engine (Cerebro) Core Loop
// ============================================================================

class Cerebro {
private:
    static constexpr size_t QUEUE_SIZE = 65536;  // Must be power of 2
    
    DisruptorQueue<EventVariant, QUEUE_SIZE> event_queue_;
    std::unique_ptr<IDataHandler> data_handler_;
    std::unique_ptr<IStrategy> strategy_;
    std::unique_ptr<IPortfolio> portfolio_;
    std::unique_ptr<IExecutionHandler> execution_handler_;
    
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> events_processed_{0};
    std::atomic<uint64_t> total_latency_ns_{0};
    
    // Performance monitoring
    std::chrono::high_resolution_clock::time_point start_time_;
    
public:
    Cerebro() = default;
    
    // Component injection for modularity
    void setDataHandler(std::unique_ptr<IDataHandler> handler) {
        data_handler_ = std::move(handler);
    }
    
    void setStrategy(std::unique_ptr<IStrategy> strategy) {
        strategy_ = std::move(strategy);
    }
    
    void setPortfolio(std::unique_ptr<IPortfolio> portfolio) {
        portfolio_ = std::move(portfolio);
    }
    
    void setExecutionHandler(std::unique_ptr<IExecutionHandler> handler) {
        execution_handler_ = std::move(handler);
    }
    
    // Public interface to queue for components
    DisruptorQueue<EventVariant, QUEUE_SIZE>& getEventQueue() {
        return event_queue_;
    }
    
    // Main simulation loop
    void run() {
        if (!data_handler_ || !strategy_ || !portfolio_ || !execution_handler_) {
            throw std::runtime_error("All components must be set before running");
        }
        
        running_ = true;
        start_time_ = std::chrono::high_resolution_clock::now();
        
        EventDispatcher dispatcher(strategy_.get(), portfolio_.get(), 
                                  execution_handler_.get());
        
        // Main heartbeat loop
        while (running_ && data_handler_->hasMoreData()) {
            // Update market data (generates MarketEvents)
            data_handler_->updateBars();
            
            // Process all events in queue
            while (!event_queue_.empty()) {
                auto event_opt = event_queue_.try_consume();
                if (!event_opt) break;
                
                auto event_start = std::chrono::high_resolution_clock::now();
                
                // Type-safe event dispatch
                std::visit(dispatcher, *event_opt);
                
                auto event_end = std::chrono::high_resolution_clock::now();
                auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>
                              (event_end - event_start).count();
                
                events_processed_.fetch_add(1, std::memory_order_relaxed);
                total_latency_ns_.fetch_add(latency, std::memory_order_relaxed);
            }
        }
        
        running_ = false;
    }
    
    void stop() {
        running_ = false;
    }
    
    // Performance metrics
    struct PerformanceStats {
        uint64_t events_processed;
        double avg_latency_ns;
        double throughput_events_per_sec;
        double runtime_seconds;
    };
    
    PerformanceStats getStats() const {
        auto end_time = std::chrono::high_resolution_clock::now();
        auto runtime = std::chrono::duration_cast<std::chrono::seconds>
                      (end_time - start_time_).count();
        
        uint64_t events = events_processed_.load(std::memory_order_relaxed);
        uint64_t total_latency = total_latency_ns_.load(std::memory_order_relaxed);
        
        return {
            events,
            events > 0 ? static_cast<double>(total_latency) / events : 0.0,
            runtime > 0 ? static_cast<double>(events) / runtime : 0.0,
            static_cast<double>(runtime)
        };
    }
};

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
                return &pool_[idx];
            }
        }
        return nullptr;  // Pool exhausted
    }
    
    void release(T* obj) {
        if (!obj) return;
        size_t idx = obj - pool_.data();
        if (idx < POOL_SIZE) {
            *obj = T{};  // Reset object
            in_use_[idx].store(false, std::memory_order_release);
        }
    }
};

}  // namespace backtesting