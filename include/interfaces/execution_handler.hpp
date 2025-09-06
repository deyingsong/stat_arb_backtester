// execution_handler.hpp
// Execution Handler Interface and Implementations for Statistical Arbitrage Backtesting Engine

#pragma once

namespace backtesting {

// Forward declarations only for types used in interfaces
struct OrderEvent;
struct FillEvent;

// ============================================================================
// Execution Handler Interface (Clean, Dependency-Free)
// ============================================================================

class IExecutionHandler {
public:
    virtual ~IExecutionHandler() = default;
    virtual void executeOrder(const OrderEvent& event) = 0;
    virtual void initialize() {}
    virtual void shutdown() {}
    
    // Template method - implementation in event_system.hpp will provide proper type
    template<typename QueueType>
    void setEventQueue(QueueType* queue) { 
        event_queue_ = static_cast<void*>(queue); 
    }
    
protected:
    void* event_queue_ = nullptr;  // Type-erased pointer
    
    // Declaration only - implementation will be provided when full headers are included
    void emitFill(const FillEvent& fill);
};

} // namespace backtesting
