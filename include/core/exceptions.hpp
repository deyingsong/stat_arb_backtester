// exceptions.hpp
// Exception Types for Statistical Arbitrage Backtesting Engine
// Provides custom exception hierarchy for better error handling and debugging

#pragma once

#include <stdexcept>
#include <string>

namespace backtesting {

// ============================================================================
// Exception Types for Better Error Handling
// ============================================================================

class BacktestException : public std::runtime_error {
public:
    explicit BacktestException(const std::string& msg) : std::runtime_error(msg) {}
};

class DataException : public BacktestException {
public:
    explicit DataException(const std::string& msg) : BacktestException("Data Error: " + msg) {}
};

class ExecutionException : public BacktestException {
public:
    explicit ExecutionException(const std::string& msg) : BacktestException("Execution Error: " + msg) {}
};

} // namespace backtesting
