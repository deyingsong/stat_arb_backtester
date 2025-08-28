#include "data_loader.hpp"
#include "signal_generator.hpp"
#include "trade_executor.hpp"
#include "backtester.hpp"

int main() {
    auto data = load_csv("data/prices.csv"); // assumes cols: date, priceA, priceB
    SignalGenerator signalGen(1.0, 0.2, 30);
    TradeExecutor trader(100000); // starting capital
    Backtester backtester(data, signalGen, trader);
    backtester.run();
    backtester.report_results();
    return 0;
}
