// event_system.hpp
// High-Performance Event System Foundation for Statistical Arbitrage Backtesting Engine
// Implements lock-free Disruptor pattern for ultra-low latency event processing
// Phase 1: Complete Core Infrastructure

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
#include <vector>
#include <unordered_map>
#include <exception>
#include <stdexcept>
#ifdef __x86_64__
    #include <immintrin.h>  // For CPU pause instruction on x86/x64
#endif

namespace backtesting {

// ============================================================================
// Forward Declarations
// ============================================================================
class EventDispatcher;
class Cerebro;

// ============================================================================
// Exception Types for Better Error Handling
// ============================================================================

class BacktestException : public std::runtime_error {
public:
    explicit BacktestException(const std::string& msg) : std::runtime_error(msg) {}
};

class DataException : public BacktestException {
public:
    explicit DataException(const std::string& msg) : BacktestException("Data Error: " + msg) {}
};

class ExecutionException : public BacktestException {
public:
    explicit ExecutionException(const std::string& msg) : BacktestException("Execution Error: " + msg) {}
};

// ============================================================================
// Enhanced Event Types with Validation
// ============================================================================

struct Event {
    std::chrono::nanoseconds timestamp;
    uint64_t sequence_id;
    
    Event() : timestamp(0), sequence_id(0) {}
    Event(std::chrono::nanoseconds ts, uint64_t seq) 
        : timestamp(ts), sequence_id(seq) {}
    
    virtual ~Event() = default;
    virtual bool validate() const { return sequence_id > 0; }
};

struct MarketEvent : Event {
    std::string symbol;
    double open, high, low, close, volume;
    double bid, ask;  // For spread modeling
    double bid_size, ask_size;  // Market depth
    
    MarketEvent() : Event(), open(0), high(0), low(0), close(0), 
                    volume(0), bid(0), ask(0), bid_size(0), ask_size(0) {}
    
    bool validate() const override {
        return Event::validate() && 
               !symbol.empty() && 
               high >= low && 
               high >= open && high >= close &&
               low <= open && low <= close &&
               bid <= ask && bid > 0 && ask > 0 &&
               volume >= 0;
    }
};

struct SignalEvent : Event {
    std::string symbol;
    enum class Direction { LONG, SHORT, EXIT, FLAT };
    Direction direction;
    double strength;  // Signal confidence [0,1]
    std::string strategy_id;  // Which strategy generated this
    std::unordered_map<std::string, double> metadata;  // Additional signal info
    
    SignalEvent() : Event(), direction(Direction::FLAT), strength(0.0) {}
    
    bool validate() const override {
        return Event::validate() && 
               !symbol.empty() && 
               strength >= 0.0 && strength <= 1.0;
    }
};

struct OrderEvent : Event {
    std::string symbol;
    enum class Type { MARKET, LIMIT, STOP, STOP_LIMIT };
    enum class Direction { BUY, SELL };
    enum class TimeInForce { DAY, GTC, IOC, FOK };
    Type order_type;
    Direction direction;
    int quantity;
    double price;  // For limit orders
    double stop_price;  // For stop orders
    TimeInForce tif;
    std::string order_id;
    std::string portfolio_id;
    
    OrderEvent() : Event(), order_type(Type::MARKET), 
                   direction(Direction::BUY), quantity(0), price(0.0),
                   stop_price(0.0), tif(TimeInForce::DAY) {}
    
    bool validate() const override {
        return Event::validate() && 
               !symbol.empty() && 
               quantity > 0 &&
               (order_type == Type::MARKET || price > 0) &&
               !order_id.empty();
    }
};

struct FillEvent : Event {
    std::string symbol;
    int quantity;
    double fill_price;
    double commission;
    double slippage;
    std::string order_id;
    std::string exchange;
    bool is_buy;
    
    FillEvent() : Event(), quantity(0), fill_price(0.0), 
                  commission(0.0), slippage(0.0), is_buy(true) {}
    
    bool validate() const override {
        return Event::validate() && 
               !symbol.empty() && 
               quantity > 0 && 
               fill_price > 0 &&
               !order_id.empty();
    }
};

// New event type for risk management
struct RiskEvent : Event {
    enum class Type { MARGIN_CALL, STOP_LOSS, POSITION_LIMIT, DRAWDOWN_LIMIT };
    Type risk_type;
    std::string message;
    double current_value;
    double limit_value;
    
    RiskEvent() : Event(), risk_type(Type::MARGIN_CALL), 
                  current_value(0), limit_value(0) {}
    
    bool validate() const override {
        return Event::validate() && !message.empty();
    }
};

using EventVariant = std::variant<MarketEvent, SignalEvent, OrderEvent, FillEvent, RiskEvent>;

// ============================================================================
// Lock-Free Disruptor Queue Implementation (Enhanced with Stats)
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
    
    // Performance statistics
    alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> total_published_{0};
    alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> total_consumed_{0};
    alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> failed_publishes_{0};
    
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
                failed_publishes_.fetch_add(1, std::memory_order_relaxed);
                return false;  // Queue is full
            }
        }
        
        // Write to buffer
        buffer_[current_write & MASK] = item;
        
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
        T item = buffer_[current_read & MASK];
        
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

// ============================================================================
// Enhanced Event Handler Interfaces with Lifecycle Management
// ============================================================================

class IEventHandler {
public:
    virtual ~IEventHandler() = default;
    virtual void onEvent(const EventVariant& event) = 0;
    virtual void initialize() {}  // Called before simulation starts
    virtual void shutdown() {}    // Called after simulation ends
    virtual std::string getName() const { return "UnnamedHandler"; }
};

class IDataHandler {
public:
    virtual ~IDataHandler() = default;
    virtual bool hasMoreData() const = 0;
    virtual void updateBars() = 0;
    virtual std::optional<MarketEvent> getLatestBar(const std::string& symbol) const = 0;
    virtual std::vector<std::string> getSymbols() const = 0;
    virtual void initialize() {}
    virtual void shutdown() {}
    virtual void reset() {}  // Reset to beginning of data
};

class IStrategy {
public:
    virtual ~IStrategy() = default;
    virtual void calculateSignals(const MarketEvent& event) = 0;
    virtual void reset() = 0;
    virtual void initialize() {}
    virtual void shutdown() {}
    virtual std::string getName() const { return "UnnamedStrategy"; }
    virtual void setEventQueue(DisruptorQueue<EventVariant, 65536>* queue) { event_queue_ = queue; }
    
protected:
    DisruptorQueue<EventVariant, 65536>* event_queue_ = nullptr;
    
    void emitSignal(const SignalEvent& signal) {
        if (event_queue_ && signal.validate()) {
            event_queue_->publish(signal);
        }
    }
};

class IPortfolio {
public:
    virtual ~IPortfolio() = default;
    virtual void updateSignal(const SignalEvent& event) = 0;
    virtual void updateFill(const FillEvent& event) = 0;
    virtual void updateMarket(const MarketEvent& event) = 0;
    virtual double getEquity() const = 0;
    virtual double getCash() const = 0;
    virtual std::unordered_map<std::string, int> getPositions() const = 0;
    virtual void initialize(double initial_capital) {}
    virtual void shutdown() {}
    virtual void reset() {}
    virtual void setEventQueue(DisruptorQueue<EventVariant, 65536>* queue) { event_queue_ = queue; }
    
protected:
    DisruptorQueue<EventVariant, 65536>* event_queue_ = nullptr;
    
    void emitOrder(const OrderEvent& order) {
        if (event_queue_ && order.validate()) {
            event_queue_->publish(order);
        }
    }
};

class IExecutionHandler {
public:
    virtual ~IExecutionHandler() = default;
    virtual void executeOrder(const OrderEvent& event) = 0;
    virtual void initialize() {}
    virtual void shutdown() {}
    virtual void setEventQueue(DisruptorQueue<EventVariant, 65536>* queue) { event_queue_ = queue; }
    
protected:
    DisruptorQueue<EventVariant, 65536>* event_queue_ = nullptr;
    
    void emitFill(const FillEvent& fill) {
        if (event_queue_ && fill.validate()) {
            event_queue_->publish(fill);
        }
    }
};

// ============================================================================
// Enhanced Event Visitor with Error Handling
// ============================================================================

class EventDispatcher {
private:
    IStrategy* strategy_;
    IPortfolio* portfolio_;
    IExecutionHandler* execution_;
    std::atomic<uint64_t> errors_{0};
    
public:
    EventDispatcher(IStrategy* strat, IPortfolio* port, IExecutionHandler* exec)
        : strategy_(strat), portfolio_(port), execution_(exec) {}
    
    void operator()(const MarketEvent& e) {
        try {
            if (!e.validate()) {
                errors_.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            if (portfolio_) portfolio_->updateMarket(e);
            if (strategy_) strategy_->calculateSignals(e);
        } catch (const std::exception& ex) {
            errors_.fetch_add(1, std::memory_order_relaxed);
            // In production, log the error
        }
    }
    
    void operator()(const SignalEvent& e) {
        try {
            if (!e.validate()) {
                errors_.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            if (portfolio_) portfolio_->updateSignal(e);
        } catch (const std::exception& ex) {
            errors_.fetch_add(1, std::memory_order_relaxed);
        }
    }
    
    void operator()(const OrderEvent& e) {
        try {
            if (!e.validate()) {
                errors_.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            if (execution_) execution_->executeOrder(e);
        } catch (const std::exception& ex) {
            errors_.fetch_add(1, std::memory_order_relaxed);
        }
    }
    
    void operator()(const FillEvent& e) {
        try {
            if (!e.validate()) {
                errors_.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            if (portfolio_) portfolio_->updateFill(e);
        } catch (const std::exception& ex) {
            errors_.fetch_add(1, std::memory_order_relaxed);
        }
    }
    
    void operator()(const RiskEvent& e) {
        // Handle risk events - could stop trading, reduce positions, etc.
        // For now, just validate
        if (!e.validate()) {
            errors_.fetch_add(1, std::memory_order_relaxed);
        }
    }
    
    uint64_t getErrorCount() const {
        return errors_.load(std::memory_order_relaxed);
    }
};

// ============================================================================
// Enhanced Main Engine (Cerebro) with Complete Lifecycle Management
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
    std::atomic<bool> initialized_{false};
    std::atomic<uint64_t> events_processed_{0};
    std::atomic<uint64_t> total_latency_ns_{0};
    std::atomic<uint64_t> max_latency_ns_{0};
    std::atomic<uint64_t> min_latency_ns_{UINT64_MAX};
    
    // Performance monitoring
    std::chrono::high_resolution_clock::time_point start_time_;
    std::chrono::high_resolution_clock::time_point end_time_;
    
    // Configuration
    struct Config {
        double initial_capital = 100000.0;
        bool enable_risk_checks = true;
        size_t max_events_per_tick = 1000;  // Prevent infinite loops
        std::chrono::milliseconds heartbeat_interval{0};  // Throttling if needed
    } config_;
    
public:
    Cerebro() = default;
    ~Cerebro() {
        if (initialized_) {
            shutdown();
        }
    }
    
    // Component injection for modularity
    void setDataHandler(std::unique_ptr<IDataHandler> handler) {
        if (running_) throw BacktestException("Cannot change components while running");
        data_handler_ = std::move(handler);
    }
    
    void setStrategy(std::unique_ptr<IStrategy> strategy) {
        if (running_) throw BacktestException("Cannot change components while running");
        strategy_ = std::move(strategy);
        if (strategy_) {
            strategy_->setEventQueue(&event_queue_);
        }
    }
    
    void setPortfolio(std::unique_ptr<IPortfolio> portfolio) {
        if (running_) throw BacktestException("Cannot change components while running");
        portfolio_ = std::move(portfolio);
        if (portfolio_) {
            portfolio_->setEventQueue(&event_queue_);
        }
    }
    
    void setExecutionHandler(std::unique_ptr<IExecutionHandler> handler) {
        if (running_) throw BacktestException("Cannot change components while running");
        execution_handler_ = std::move(handler);
        if (execution_handler_) {
            execution_handler_->setEventQueue(&event_queue_);
        }
    }
    
    // Configuration
    void setInitialCapital(double capital) {
        if (capital <= 0) throw BacktestException("Initial capital must be positive");
        config_.initial_capital = capital;
    }
    
    void setRiskChecksEnabled(bool enabled) {
        config_.enable_risk_checks = enabled;
    }
    
    // Public interface to queue for components
    DisruptorQueue<EventVariant, QUEUE_SIZE>& getEventQueue() {
        return event_queue_;
    }
    
    // Initialize all components
    void initialize() {
        if (initialized_) return;
        
        if (!data_handler_ || !strategy_ || !portfolio_ || !execution_handler_) {
            throw BacktestException("All components must be set before initialization");
        }
        
        // Initialize components in order
        data_handler_->initialize();
        portfolio_->initialize(config_.initial_capital);
        strategy_->initialize();
        execution_handler_->initialize();
        
        // Reset statistics
        events_processed_.store(0, std::memory_order_relaxed);
        total_latency_ns_.store(0, std::memory_order_relaxed);
        max_latency_ns_.store(0, std::memory_order_relaxed);
        min_latency_ns_.store(UINT64_MAX, std::memory_order_relaxed);
        event_queue_.resetStats();
        
        initialized_ = true;
    }
    
    // Shutdown all components
    void shutdown() {
        if (!initialized_) return;
        
        running_ = false;
        
        // Shutdown in reverse order
        execution_handler_->shutdown();
        strategy_->shutdown();
        portfolio_->shutdown();
        data_handler_->shutdown();
        
        initialized_ = false;
    }
    
    // Main simulation loop
    void run() {
        if (!initialized_) {
            initialize();
        }
        
        running_ = true;
        start_time_ = std::chrono::high_resolution_clock::now();
        
        EventDispatcher dispatcher(strategy_.get(), portfolio_.get(), 
                                  execution_handler_.get());
        
        // Main heartbeat loop
        while (running_ && data_handler_->hasMoreData()) {
            auto tick_start = std::chrono::high_resolution_clock::now();
            
            // Update market data (generates MarketEvents)
            data_handler_->updateBars();
            
            // Process events with safety limit
            size_t events_this_tick = 0;
            while (!event_queue_.empty() && events_this_tick < config_.max_events_per_tick) {
                auto event_opt = event_queue_.try_consume();
                if (!event_opt) break;
                
                auto event_start = std::chrono::high_resolution_clock::now();
                
                // Type-safe event dispatch
                std::visit(dispatcher, *event_opt);
                
                auto event_end = std::chrono::high_resolution_clock::now();
                auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>
                              (event_end - event_start).count();
                
                // Update statistics
                events_processed_.fetch_add(1, std::memory_order_relaxed);
                total_latency_ns_.fetch_add(latency, std::memory_order_relaxed);
                
                // Update min/max latency (not perfectly thread-safe but good enough)
                uint64_t current_max = max_latency_ns_.load(std::memory_order_relaxed);
                if (latency > current_max) {
                    max_latency_ns_.store(latency, std::memory_order_relaxed);
                }
                uint64_t current_min = min_latency_ns_.load(std::memory_order_relaxed);
                if (latency < current_min) {
                    min_latency_ns_.store(latency, std::memory_order_relaxed);
                }
                
                events_this_tick++;
            }
            
            // Optional throttling
            if (config_.heartbeat_interval.count() > 0) {
                auto tick_end = std::chrono::high_resolution_clock::now();
                auto tick_duration = std::chrono::duration_cast<std::chrono::milliseconds>
                                   (tick_end - tick_start);
                if (tick_duration < config_.heartbeat_interval) {
                    std::this_thread::sleep_for(config_.heartbeat_interval - tick_duration);
                }
            }
        }
        
        end_time_ = std::chrono::high_resolution_clock::now();
        running_ = false;
    }
    
    void stop() {
        running_ = false;
    }
    
    bool isRunning() const {
        return running_.load(std::memory_order_acquire);
    }
    
    // Enhanced Performance metrics
    struct PerformanceStats {
        uint64_t events_processed;
        double avg_latency_ns;
        uint64_t max_latency_ns;
        uint64_t min_latency_ns;
        double throughput_events_per_sec;
        double runtime_seconds;
        
        // Queue statistics
        uint64_t queue_publishes;
        uint64_t queue_consumes;
        uint64_t queue_failures;
        double queue_utilization_pct;
        
        // Component statistics
        uint64_t dispatcher_errors;
        double final_equity;
        double final_cash;
    };
    
    PerformanceStats getStats() const {
        auto runtime = std::chrono::duration_cast<std::chrono::seconds>
                      (end_time_ - start_time_).count();
        if (runtime == 0 && running_) {
            // Still running, calculate current runtime
            auto now = std::chrono::high_resolution_clock::now();
            runtime = std::chrono::duration_cast<std::chrono::seconds>
                     (now - start_time_).count();
        }
        
        uint64_t events = events_processed_.load(std::memory_order_relaxed);
        uint64_t total_latency = total_latency_ns_.load(std::memory_order_relaxed);
        uint64_t max_lat = max_latency_ns_.load(std::memory_order_relaxed);
        uint64_t min_lat = min_latency_ns_.load(std::memory_order_relaxed);
        
        auto queue_stats = event_queue_.getStats();
        
        return {
            events,
            events > 0 ? static_cast<double>(total_latency) / events : 0.0,
            max_lat,
            min_lat == UINT64_MAX ? 0 : min_lat,
            runtime > 0 ? static_cast<double>(events) / runtime : 0.0,
            static_cast<double>(runtime),
            
            queue_stats.total_published,
            queue_stats.total_consumed,
            queue_stats.failed_publishes,
            queue_stats.utilization_pct,
            
            0,  // Will be set by dispatcher
            portfolio_ ? portfolio_->getEquity() : 0.0,
            portfolio_ ? portfolio_->getCash() : 0.0
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

// ============================================================================
// Event Builder Pattern for Clean Event Construction
// ============================================================================

class MarketEventBuilder {
private:
    MarketEvent event_;
    static std::atomic<uint64_t> sequence_counter_;
    
public:
    MarketEventBuilder& withSymbol(const std::string& symbol) {
        event_.symbol = symbol;
        return *this;
    }
    
    MarketEventBuilder& withOHLC(double o, double h, double l, double c) {
        event_.open = o;
        event_.high = h;
        event_.low = l;
        event_.close = c;
        return *this;
    }
    
    MarketEventBuilder& withVolume(double vol) {
        event_.volume = vol;
        return *this;
    }
    
    MarketEventBuilder& withBidAsk(double bid, double ask, 
                                   double bid_sz = 100, double ask_sz = 100) {
        event_.bid = bid;
        event_.ask = ask;
        event_.bid_size = bid_sz;
        event_.ask_size = ask_sz;
        return *this;
    }
    
    MarketEventBuilder& withTimestamp(std::chrono::nanoseconds ts) {
        event_.timestamp = ts;
        return *this;
    }
    
    MarketEvent build() {
        event_.sequence_id = sequence_counter_.fetch_add(1, std::memory_order_relaxed);
        if (!event_.validate()) {
            throw BacktestException("Invalid MarketEvent configuration");
        }
        return event_;
    }
};

std::atomic<uint64_t> MarketEventBuilder::sequence_counter_{1};

}  // namespace backtesting