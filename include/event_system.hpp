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
#include "interfaces/data_handler.hpp"  // Use relative path for data handler interface
#include "interfaces/strategy.hpp"  // Include strategy interface
#include "interfaces/portfolio.hpp"  // Include portfolio interface
#include "interfaces/execution_handler.hpp"  // Include execution handler interface
#include "concurrent/disruptor_queue.hpp"  // Include lock-free disruptor queue
#include "concurrent/event_pool.hpp"  // Include event object pool
#include "core/exceptions.hpp"  // Include custom exception types
#include "core/event_types.hpp"  // Include event type definitions
#include "engine/event_dispatcher.hpp"  // Include event dispatcher
#include "engine/cerebro.hpp"  // Include main engine
#include "builders/event_builders.hpp"  // Include event builders
#ifdef __x86_64__
    #include <immintrin.h>  //  For CPU pause instruction on x86/x64
#endif

namespace backtesting {

// ============================================================================
// Forward Declarations
// ============================================================================
class EventDispatcher;
class Cerebro;

// ============================================================================
// Implementation of IStrategy, IPortfolio, and IExecutionHandler methods (after all types are defined)
// ============================================================================

inline void IStrategy::emitSignal(const SignalEvent& signal) {
    if (event_queue_ && signal.validate()) {
        auto* queue = static_cast<DisruptorQueue<EventVariant, 65536>*>(event_queue_);
        queue->publish(signal);
    }
}

inline void IPortfolio::emitOrder(const OrderEvent& order) {
    if (event_queue_ && order.validate()) {
        auto* queue = static_cast<DisruptorQueue<EventVariant, 65536>*>(event_queue_);
        queue->publish(order);
    }
}

inline void IExecutionHandler::emitFill(const FillEvent& fill) {
    if (event_queue_ && fill.validate()) {
        auto* queue = static_cast<DisruptorQueue<EventVariant, 65536>*>(event_queue_);
        queue->publish(fill);
    }
}

}  // namespace backtesting