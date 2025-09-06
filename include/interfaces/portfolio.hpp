// portfolio.hpp
// Portfolio Interface and Implementations for Statistical Arbitrage Backtesting Engine

#pragma once

#include <string>
#include <unordered_map>

namespace backtesting {

// Forward declarations only for types used in interfaces
struct SignalEvent;
struct FillEvent;
struct MarketEvent;
struct OrderEvent;

// ============================================================================
// Portfolio Interface (Clean, Dependency-Free)
// ============================================================================

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
    
    // Template method - implementation in event_system.hpp will provide proper type
    template<typename QueueType>
    void setEventQueue(QueueType* queue) { 
        event_queue_ = static_cast<void*>(queue); 
    }
    
protected:
    void* event_queue_ = nullptr;  // Type-erased pointer
    
    // Declaration only - implementation will be provided when full headers are included
    void emitOrder(const OrderEvent& order);
};

} // namespace backtesting
