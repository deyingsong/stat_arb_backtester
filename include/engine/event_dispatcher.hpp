// event_dispatcher.hpp
// Event Dispatcher Implementation for Statistical Arbitrage Backtesting Engine
// Provides event routing and error handling with visitor pattern

#pragma once

#include <atomic>
#include <exception>
#include <cstdint>
#include "../core/event_types.hpp"
#include "../interfaces/strategy.hpp"
#include "../interfaces/portfolio.hpp"
#include "../interfaces/execution_handler.hpp"

namespace backtesting {

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

} // namespace backtesting
