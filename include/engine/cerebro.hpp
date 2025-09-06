// cerebro.hpp
// Main Engine (Cerebro) Implementation for Statistical Arbitrage Backtesting Engine
// Provides complete lifecycle management, event processing, and performance monitoring

#pragma once

#include <memory>
#include <atomic>
#include <chrono>
#include <thread>
#include <variant>
#include <cstdint>
#include "../concurrent/disruptor_queue.hpp"
#include "../core/event_types.hpp"
#include "../core/exceptions.hpp"
#include "../interfaces/data_handler.hpp"
#include "../interfaces/strategy.hpp"
#include "../interfaces/portfolio.hpp"
#include "../interfaces/execution_handler.hpp"
#include "event_dispatcher.hpp"

namespace backtesting {

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

} // namespace backtesting
