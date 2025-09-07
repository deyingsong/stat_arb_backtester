// simple_ma_strategy.hpp
// Simple Moving Average Crossover Strategy for Statistical Arbitrage Backtesting Engine
// Basic strategy implementation for end-to-end system testing

#pragma once

#include <string>
#include <unordered_map>
#include <deque>
#include <numeric>
#include <cmath>
#include "../interfaces/strategy.hpp"
#include "../core/event_types.hpp"
#include "../concurrent/disruptor_queue.hpp"

namespace backtesting {

// ============================================================================
// Simple Moving Average Crossover Strategy
// ============================================================================

class SimpleMAStrategy : public IStrategy {
public:
    struct MAConfig {
        size_t fast_period;
        size_t slow_period;
        double signal_threshold;  // Minimum MA difference for signal
        bool use_volume_filter;
        double volume_multiplier;  // Volume must be 1.5x average
        size_t warmup_period;  // Set to slow_period if not specified
        
        // Default constructor with explicit initialization
        MAConfig() 
            : fast_period(10)
            , slow_period(30)
            , signal_threshold(0.001)
            , use_volume_filter(false)
            , volume_multiplier(1.5)
            , warmup_period(0) {}
            
        // Static method to get default config
        static MAConfig getDefault() {
            return MAConfig();
        }
    };
    
private:
    // Price history for each symbol
    struct PriceData {
        std::deque<double> prices;
        std::deque<double> volumes;
        double fast_ma = 0.0;
        double slow_ma = 0.0;
        double prev_fast_ma = 0.0;
        double prev_slow_ma = 0.0;
        bool is_warmed_up = false;
        int current_position = 0;  // 1 = long, -1 = short, 0 = flat
    };
    
    std::unordered_map<std::string, PriceData> symbol_data_;
    MAConfig config_;
    std::string strategy_name_;
    
    // Performance tracking
    uint64_t signals_generated_ = 0;
    uint64_t long_signals_ = 0;
    uint64_t short_signals_ = 0;
    uint64_t exit_signals_ = 0;
    
    // Event queue reference (type-erased from interface)
    DisruptorQueue<EventVariant, 65536>* getEventQueue() {
        return static_cast<DisruptorQueue<EventVariant, 65536>*>(event_queue_);
    }
    
    // Calculate simple moving average
    double calculateSMA(const std::deque<double>& prices, size_t period) {
        if (prices.size() < period) return 0.0;
        
        double sum = 0.0;
        auto it = prices.rbegin();  // Start from most recent
        for (size_t i = 0; i < period && it != prices.rend(); ++i, ++it) {
            sum += *it;
        }
        return sum / period;
    }
    
    // Check volume filter
    bool passesVolumeFilter(const PriceData& data, double current_volume) {
        if (!config_.use_volume_filter) return true;
        if (data.volumes.size() < config_.slow_period) return false;
        
        double avg_volume = std::accumulate(data.volumes.begin(), 
                                           data.volumes.end(), 0.0) / data.volumes.size();
        return current_volume > avg_volume * config_.volume_multiplier;
    }
    
public:
    // Default constructor using default config
    SimpleMAStrategy(const std::string& name = "SimpleMA")
        : config_(MAConfig::getDefault()), strategy_name_(name) {
        if (config_.warmup_period == 0) {
            config_.warmup_period = config_.slow_period;
        }
    }
    
    // Constructor with custom config
    explicit SimpleMAStrategy(const MAConfig& config, const std::string& name = "SimpleMA")
        : config_(config), strategy_name_(name) {
        if (config_.warmup_period == 0) {
            config_.warmup_period = config_.slow_period;
        }
    }
    
    // IStrategy interface implementation
    void calculateSignals(const MarketEvent& event) override {
        auto& data = symbol_data_[event.symbol];
        
        // Update price history
        data.prices.push_back(event.close);
        data.volumes.push_back(event.volume);
        
        // Maintain window size
        if (data.prices.size() > config_.slow_period * 2) {
            data.prices.pop_front();
            data.volumes.pop_front();
        }
        
        // Check if we have enough data
        if (data.prices.size() < config_.slow_period) {
            return;  // Still warming up
        }
        
        // Store previous MAs
        data.prev_fast_ma = data.fast_ma;
        data.prev_slow_ma = data.slow_ma;
        
        // Calculate current MAs
        data.fast_ma = calculateSMA(data.prices, config_.fast_period);
        data.slow_ma = calculateSMA(data.prices, config_.slow_period);
        
        // Mark as warmed up
        if (!data.is_warmed_up && data.prices.size() >= config_.warmup_period) {
            data.is_warmed_up = true;
        }
        
        // Don't generate signals until warmed up
        if (!data.is_warmed_up || data.prev_fast_ma == 0 || data.prev_slow_ma == 0) {
            return;
        }
        
        // Check for crossover signals
        bool fast_above_slow = data.fast_ma > data.slow_ma;
        bool prev_fast_above_slow = data.prev_fast_ma > data.prev_slow_ma;
        
        // Calculate signal strength based on MA divergence
        double ma_diff = std::abs(data.fast_ma - data.slow_ma) / event.close;
        double signal_strength = std::min(1.0, ma_diff / config_.signal_threshold);
        
        // Volume filter
        if (!passesVolumeFilter(data, event.volume)) {
            signal_strength *= 0.5;  // Reduce signal strength if volume is low
        }
        
        SignalEvent signal;
        signal.symbol = event.symbol;
        signal.timestamp = event.timestamp;
        signal.sequence_id = event.sequence_id;
        signal.strategy_id = strategy_name_;
        signal.strength = signal_strength;
        
        // Detect crossovers and generate signals
        if (fast_above_slow && !prev_fast_above_slow) {
            // Golden cross - buy signal
            signal.direction = SignalEvent::Direction::LONG;
            signal.metadata["fast_ma"] = data.fast_ma;
            signal.metadata["slow_ma"] = data.slow_ma;
            signal.metadata["crossover_type"] = 1.0;  // Golden cross
            
            data.current_position = 1;
            signals_generated_++;
            long_signals_++;
            
            emitSignal(signal);
            
        } else if (!fast_above_slow && prev_fast_above_slow) {
            // Death cross - sell signal
            signal.direction = SignalEvent::Direction::SHORT;
            signal.metadata["fast_ma"] = data.fast_ma;
            signal.metadata["slow_ma"] = data.slow_ma;
            signal.metadata["crossover_type"] = -1.0;  // Death cross
            
            data.current_position = -1;
            signals_generated_++;
            short_signals_++;
            
            emitSignal(signal);
            
        } else if (data.current_position != 0) {
            // Check for exit conditions
            double position_ma = (data.current_position > 0) ? data.slow_ma : data.fast_ma;
            double exit_threshold = 0.02;  // Exit if price moves 2% against position
            
            bool should_exit = false;
            if (data.current_position > 0 && event.close < position_ma * (1 - exit_threshold)) {
                should_exit = true;  // Stop loss for long
            } else if (data.current_position < 0 && event.close > position_ma * (1 + exit_threshold)) {
                should_exit = true;  // Stop loss for short
            }
            
            if (should_exit) {
                signal.direction = SignalEvent::Direction::EXIT;
                signal.strength = 1.0;
                signal.metadata["exit_reason"] = (data.current_position > 0) ? -1.0 : 1.0;
                
                data.current_position = 0;
                signals_generated_++;
                exit_signals_++;
                
                emitSignal(signal);
            }
        }
    }
    
    void reset() override {
        symbol_data_.clear();
        signals_generated_ = 0;
        long_signals_ = 0;
        short_signals_ = 0;
        exit_signals_ = 0;
    }
    
    void initialize() override {
        reset();
    }
    
    void shutdown() override {
        // Clean up if needed
    }
    
    std::string getName() const override {
        return strategy_name_;
    }
    
    // Additional utility methods
    struct StrategyStats {
        uint64_t total_signals;
        uint64_t long_signals;
        uint64_t short_signals;
        uint64_t exit_signals;
        size_t symbols_tracked;
    };
    
    StrategyStats getStats() const {
        return {
            signals_generated_,
            long_signals_,
            short_signals_,
            exit_signals_,
            symbol_data_.size()
        };
    }
    
    MAConfig getConfig() const {
        return config_;
    }
    
    void setConfig(const MAConfig& config) {
        config_ = config;
        if (config_.warmup_period == 0) {
            config_.warmup_period = config_.slow_period;
        }
    }
};

} // namespace backtesting