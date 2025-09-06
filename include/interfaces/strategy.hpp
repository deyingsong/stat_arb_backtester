// strategy.hpp
// Strategy Interface and Implementations for Statistical Arbitrage Backtesting Engine

#pragma once

#include <string>

namespace backtesting {

// Forward declarations only for types used in interfaces
struct MarketEvent;
struct SignalEvent;

// ============================================================================
// Strategy Interface (Clean, Dependency-Free)
// ============================================================================

class IStrategy {
public:
    virtual ~IStrategy() = default;
    virtual void calculateSignals(const MarketEvent& event) = 0;
    virtual void reset() = 0;
    virtual void initialize() {}
    virtual void shutdown() {}
    virtual std::string getName() const { return "UnnamedStrategy"; }
    
    // Template method - implementation in event_system.hpp will provide proper type
    template<typename QueueType>
    void setEventQueue(QueueType* queue) { 
        event_queue_ = static_cast<void*>(queue); 
    }
    
protected:
    void* event_queue_ = nullptr;  // Type-erased pointer
    
    // Declaration only - implementation will be provided when full headers are included
    void emitSignal(const SignalEvent& signal);
};

} // namespace backtesting