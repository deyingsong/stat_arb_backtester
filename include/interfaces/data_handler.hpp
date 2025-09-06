// data_handler.hpp
// Data Handler Interface and Implementations for Statistical Arbitrage Backtesting Engine

#pragma once

#include <string>
#include <vector>
#include <optional>

// Forward declare MarketEvent to avoid circular dependency
namespace backtesting {
    struct MarketEvent;
}

namespace backtesting {

// ============================================================================
// Data Handler Interface
// ============================================================================

class IDataHandler {
public:
    virtual ~IDataHandler() = default;
    virtual bool hasMoreData() const = 0;
    virtual void updateBars() = 0;
    virtual std::optional<MarketEvent> getLatestBar(const std::string& symbol) const = 0;
    virtual std::vector<std::string> getSymbols() const = 0;
    virtual void initialize() {}
    virtual void shutdown() {}
    virtual void reset() {}  // Reset to beginning of data
};

}  // namespace backtesting