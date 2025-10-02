// event_types.hpp
// Event Type Definitions for Statistical Arbitrage Backtesting Engine
// Phase 4 Optimized: Includes branch prediction hints for hot path optimization
// Provides comprehensive event hierarchy for market data, signals, orders, fills, and risk management

#pragma once

#include <chrono>
#include <string>
#include <variant>
#include <unordered_map>
#include <cstdint>
#include <cmath>

// Phase 4: Branch optimization hints
#ifdef __has_include
    #if __has_include("../core/branch_hints.hpp")
        #include "../core/branch_hints.hpp"
    #else
        // Fallback if branch_hints.hpp not available yet
        #define LIKELY(x)   (x)
        #define UNLIKELY(x) (x)
        #define HOT_FUNCTION
        #define COLD_FUNCTION
    #endif
#else
    // Compiler doesn't support __has_include
    #include "../core/branch_hints.hpp"
#endif

namespace backtesting {

// ============================================================================
// Enhanced Event Types with Validation and Phase 4 Optimizations
// ============================================================================

struct Event {
    std::chrono::nanoseconds timestamp;
    uint64_t sequence_id;
    
    Event() : timestamp(0), sequence_id(0) {}
    Event(std::chrono::nanoseconds ts, uint64_t seq) 
        : timestamp(ts), sequence_id(seq) {}
    
    virtual ~Event() = default;
    
    // Base validation with branch hint
    virtual bool validate() const { 
        return LIKELY(sequence_id > 0); 
    }
};

// ============================================================================
// Market Event - Primary data feed event (HOT PATH)
// ============================================================================

struct MarketEvent : Event {
    std::string symbol;
    double open, high, low, close, volume;
    double bid, ask;  // For spread modeling
    double bid_size, ask_size;  // Market depth
    
    MarketEvent() : Event(), open(0), high(0), low(0), close(0), 
                    volume(0), bid(0), ask(0), bid_size(0), ask_size(0) {}
    
    // Hot path: optimized validation with branch hints
    // This is called for EVERY market data update
    HOT_FUNCTION
    bool validate() const override {
        // Common case: valid market event (~99.9% of events)
        // CPU will prefetch this path
        if (LIKELY(Event::validate() && 
                   !symbol.empty() && 
                   high >= low && 
                   high >= open && high >= close &&
                   low <= open && low <= close &&
                   bid <= ask && bid > 0 && ask > 0 &&
                   volume >= 0)) {
            return true;
        }
        return false;
    }
};

// ============================================================================
// Signal Event - Strategy decision event (HOT PATH)
// ============================================================================

struct SignalEvent : Event {
    std::string symbol;
    enum class Direction { LONG, SHORT, EXIT, FLAT };
    Direction direction;
    double strength;  // Signal confidence [0,1]
    std::string strategy_id;  // Which strategy generated this
    std::unordered_map<std::string, double> metadata;  // Additional signal info
    
    SignalEvent() : Event(), direction(Direction::FLAT), strength(0.0) {}
    
    // Hot path: optimized validation
    HOT_FUNCTION
    bool validate() const override {
        // Common case: valid signal event
        if (LIKELY(Event::validate() && 
                   !symbol.empty() && 
                   strength >= 0.0 && strength <= 1.0)) {
            return true;
        }
        return false;
    }
};

// ============================================================================
// Order Event - Trading order request (HOT PATH)
// ============================================================================

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
    
    // Hot path: optimized validation with mixed branch predictions
    HOT_FUNCTION
    bool validate() const override {
        // Common case: valid order with basic fields
        if (LIKELY(Event::validate() && 
                   !symbol.empty() && 
                   quantity > 0 &&
                   !order_id.empty())) {
            // Less common case: limit order needs price validation
            // Most orders are market orders, so this check is UNLIKELY
            if (UNLIKELY(order_type != Type::MARKET && price <= 0)) {
                return false;
            }
            return true;
        }
        return false;
    }
};

// ============================================================================
// Fill Event - Trade execution confirmation (HOT PATH)
// ============================================================================

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
    
    // Hot path: optimized validation
    HOT_FUNCTION
    bool validate() const override {
        // Common case: valid fill event
        if (LIKELY(Event::validate() && 
                   !symbol.empty() && 
                   quantity > 0 && 
                   fill_price > 0 &&
                   !order_id.empty())) {
            return true;
        }
        return false;
    }
};

// ============================================================================
// Risk Event - Risk management alert (COLD PATH - RARE)
// ============================================================================

struct RiskEvent : Event {
    enum class Type { MARGIN_CALL, STOP_LOSS, POSITION_LIMIT, DRAWDOWN_LIMIT };
    Type risk_type;
    std::string message;
    double current_value;
    double limit_value;
    
    RiskEvent() : Event(), risk_type(Type::MARGIN_CALL), 
                  current_value(0), limit_value(0) {}
    
    // Cold path: risk events are exceptional/rare
    // Compiler can de-prioritize this code
    COLD_FUNCTION
    bool validate() const override {
        // Rare case: risk event (optimize for NOT executing this)
        if (UNLIKELY(!Event::validate() || message.empty())) {
            return false;
        }
        return true;
    }
};

// ============================================================================
// Event Variant - Type-safe event container
// ============================================================================

using EventVariant = std::variant<MarketEvent, SignalEvent, OrderEvent, FillEvent, RiskEvent>;

// ============================================================================
// Event Utility Functions
// ============================================================================

// Get event type name for logging/debugging
inline const char* getEventTypeName(const EventVariant& event) {
    return std::visit([](auto&& arg) -> const char* {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, MarketEvent>) return "MarketEvent";
        else if constexpr (std::is_same_v<T, SignalEvent>) return "SignalEvent";
        else if constexpr (std::is_same_v<T, OrderEvent>) return "OrderEvent";
        else if constexpr (std::is_same_v<T, FillEvent>) return "FillEvent";
        else if constexpr (std::is_same_v<T, RiskEvent>) return "RiskEvent";
        else return "UnknownEvent";
    }, event);
}

// Validate any event variant
inline bool validateEvent(const EventVariant& event) {
    return std::visit([](auto&& arg) { return arg.validate(); }, event);
}

// Get event timestamp
inline std::chrono::nanoseconds getEventTimestamp(const EventVariant& event) {
    return std::visit([](auto&& arg) { return arg.timestamp; }, event);
}

// Get event sequence ID
inline uint64_t getEventSequenceId(const EventVariant& event) {
    return std::visit([](auto&& arg) { return arg.sequence_id; }, event);
}

} // namespace backtesting