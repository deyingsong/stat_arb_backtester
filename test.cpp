#include <iostream>
#include <memory>
#include "include/event_system.hpp"

// Concrete implementations of the interfaces from event_system.hpp
class CsvDataHandler : public backtesting::IDataHandler {
public:
    bool hasMoreData() const override { return false; } // Simple test implementation
    void updateBars() override {}
    std::optional<backtesting::MarketEvent> getLatestBar(const std::string& symbol) const override {
        return std::nullopt;
    }
};

class StatArbStrategy : public backtesting::IStrategy {
public:
    void calculateSignals(const backtesting::MarketEvent& event) override {}
    void reset() override {}
};

class BasicPortfolio : public backtesting::IPortfolio {
public:
    void updateSignal(const backtesting::SignalEvent& event) override {}
    void updateFill(const backtesting::FillEvent& event) override {}
    void updateMarket(const backtesting::MarketEvent& event) override {}
    double getEquity() const override { return 100000.0; }
};

class SimulatedExecutionHandler : public backtesting::IExecutionHandler {
public:
    void executeOrder(const backtesting::OrderEvent& event) override {}
};

int main() {
    try {
        std::cout << "Starting backtest..." << std::endl;
        
        // Create engine
        backtesting::Cerebro engine;

        // Inject components
        engine.setDataHandler(std::make_unique<CsvDataHandler>());
        engine.setStrategy(std::make_unique<StatArbStrategy>());
        engine.setPortfolio(std::make_unique<BasicPortfolio>());
        engine.setExecutionHandler(std::make_unique<SimulatedExecutionHandler>());

        // Run backtest
        engine.run();

        // Get performance stats
        auto stats = engine.getStats();
        
        std::cout << "Backtest completed successfully!" << std::endl;
        std::cout << "Events processed: " << stats.events_processed << std::endl;
        std::cout << "Average latency: " << stats.avg_latency_ns << " ns" << std::endl;
        std::cout << "Throughput: " << stats.throughput_events_per_sec << " events/sec" << std::endl;
        std::cout << "Runtime: " << stats.runtime_seconds << " seconds" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}