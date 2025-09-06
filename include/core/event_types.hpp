// event_types.hpp
// Event Type Definitions for Statistical Arbitrage Backtesting Engine
// Provides comprehensive event hierarchy for market data, signals, orders, fills, and risk management

#pragma once

#include <chrono>
#include <string>
#include <variant>
#include <unordered_map>
#include <cstdint>

namespace backtesting {

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

} // namespace backtesting
