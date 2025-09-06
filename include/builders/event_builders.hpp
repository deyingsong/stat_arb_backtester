// event_builders.hpp
// Event Builder Pattern Implementation for Statistical Arbitrage Backtesting Engine
// Provides fluent interface for clean event construction with validation

#pragma once

#include <string>
#include <atomic>
#include <chrono>
#include <cstdint>
#include "../core/event_types.hpp"
#include "../core/exceptions.hpp"

namespace backtesting {

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

} // namespace backtesting
