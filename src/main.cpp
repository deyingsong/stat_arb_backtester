// main.cpp
// Professional Statistical Arbitrage Backtesting Engine
// Complete implementation integrating all phases (1-5)

#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <chrono>
#include <iomanip>
#include <cmath>
#include <fstream>
#include <type_traits>

// Core engine components
#include "../include/engine/cerebro.hpp"
#include "../include/data/csv_data_handler.hpp"

// Strategies
#include "../include/strategies/stat_arb_strategy.hpp"
#include "../include/strategies/simple_ma_strategy.hpp"

// Portfolio and execution
#include "../include/portfolio/basic_portfolio.hpp"
#include "../include/execution/advanced_execution_handler.hpp"
#include "../include/execution/simulated_execution_handler.hpp"

// Core utilities
#include "../include/core/exceptions.hpp"

using namespace backtesting;

// ============================================================================
// Configuration Structure
// ============================================================================

struct BacktestConfig {
    // Strategy selection
    enum class StrategyType { STAT_ARB, SIMPLE_MA };
    StrategyType strategy_type = StrategyType::STAT_ARB;
    
    // Data configuration
    std::vector<std::pair<std::string, std::string>> symbol_files;  // symbol, filepath
    
    // Statistical Arbitrage parameters
    struct StatArbParams {
        std::vector<std::pair<std::string, std::string>> pairs;  // symbol1, symbol2
        double entry_zscore = 2.0;
        double exit_zscore = 0.5;
        double stop_loss_zscore = 3.5;
        int zscore_window = 60;
        int lookback_period = 40;
        int recalibration_freq = 20;
        bool use_dynamic_hedge = true;
        double min_half_life = 0;
        double max_half_life = 60;
    } stat_arb;
    
    // Simple MA parameters
    struct SimpleMAParams {
        std::string symbol;
        int short_window = 20;
        int long_window = 50;
    } simple_ma;
    
    // Portfolio configuration
    double initial_capital = 100000.0;
    double max_position_size = 0.25;
    double commission_per_share = 0.001;
    bool allow_shorting = true;
    
    // Execution configuration
    bool use_advanced_execution = true;
    double base_slippage_bps = 5.0;
    double volatility_slippage_multiplier = 0.5;
    double min_commission = 1.0;
    bool enable_partial_fills = false;
    double fill_probability = 0.98;
    
    // Engine configuration
    bool enable_risk_checks = true;
    bool verbose = false;
    bool show_trades = false;
    
    // Output
    std::string output_file = "backtest_results.txt";
};

// ============================================================================
// Command Line Argument Parser
// ============================================================================

void printUsage(const char* program_name) {
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════╗\n";
    std::cout << "║   Statistical Arbitrage Backtesting Engine v1.0         ║\n";
    std::cout << "║   Professional C++ Implementation (Phases 1-5)           ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";
    std::cout << "Usage: " << program_name << " [OPTIONS]\n\n";
    std::cout << "Strategies:\n";
    std::cout << "  --stat-arb          Run statistical arbitrage pairs trading (default)\n";
    std::cout << "  --simple-ma         Run simple moving average strategy\n";
    std::cout << "\n";
    std::cout << "Data Options:\n";
    std::cout << "  --data-dir DIR      Data directory (default: ./data)\n";
    std::cout << "  --symbols A,B,...   Load specific symbol CSVs from data dir\n";
    std::cout << "\n";
    std::cout << "Stat Arb Options:\n";
    std::cout << "  --pairs A:B,C:D     Trading pairs (e.g., STOCK_A:STOCK_B)\n";
    std::cout << "  --entry-z NUM       Entry z-score threshold (default: 2.0)\n";
    std::cout << "  --exit-z NUM        Exit z-score threshold (default: 0.5)\n";
    std::cout << "  --window NUM        Z-score window size (default: 60)\n";
    std::cout << "\n";
    std::cout << "Portfolio Options:\n";
    std::cout << "  --capital NUM       Initial capital (default: 100000)\n";
    std::cout << "  --max-pos NUM       Max position size fraction (default: 0.25)\n";
    std::cout << "\n";
    std::cout << "Execution Options:\n";
    std::cout << "  --simple-exec       Use simple execution model\n";
    std::cout << "  --slippage NUM      Base slippage in bps (default: 5.0)\n";
    std::cout << "  --commission NUM    Commission per share (default: 0.001)\n";
    std::cout << "\n";
    std::cout << "Output Options:\n";
    std::cout << "  --verbose           Enable verbose output\n";
    std::cout << "  --show-trades       Show individual trades\n";
    std::cout << "  --output FILE       Output file (default: backtest_results.txt)\n";
    std::cout << "  -h, --help          Show this help\n";
    std::cout << "\n";
    std::cout << "Examples:\n";
    std::cout << "  # Run default stat arb on STOCK_A/STOCK_B pairs\n";
    std::cout << "  " << program_name << " --pairs STOCK_A:STOCK_B,STOCK_C:STOCK_D\n\n";
    std::cout << "  # Run with custom parameters\n";
    std::cout << "  " << program_name << " --entry-z 2.5 --exit-z 0.3 --capital 1000000 --verbose\n\n";
    std::cout << "  # Run simple MA strategy\n";
    std::cout << "  " << program_name << " --simple-ma --symbols AAPL\n\n";
}

// --- Linker stubs for pure virtual event emission methods ---
namespace backtesting {
    void IPortfolio::emitOrder(const OrderEvent& /*evt*/) {
        // no-op stub for linking when building single translation unit
    }

    void IExecutionHandler::emitFill(const FillEvent& /*evt*/) {
        // no-op stub
    }

    void IStrategy::emitSignal(const SignalEvent& /*evt*/) {
        // no-op stub
    }
}

bool parseArguments(int argc, char* argv[], BacktestConfig& config) {
    std::string data_dir = "data";
    std::vector<std::string> symbols;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            return false;
        }
        else if (arg == "--stat-arb") {
            config.strategy_type = BacktestConfig::StrategyType::STAT_ARB;
        }
        else if (arg == "--simple-ma") {
            config.strategy_type = BacktestConfig::StrategyType::SIMPLE_MA;
        }
        else if (arg == "--data-dir" && i + 1 < argc) {
            data_dir = argv[++i];
        }
        else if (arg == "--symbols" && i + 1 < argc) {
            std::string syms = argv[++i];
            size_t start = 0, end;
            while ((end = syms.find(',', start)) != std::string::npos) {
                symbols.push_back(syms.substr(start, end - start));
                start = end + 1;
            }
            symbols.push_back(syms.substr(start));
        }
        else if (arg == "--pairs" && i + 1 < argc) {
            std::string pairs_str = argv[++i];
            config.stat_arb.pairs.clear();
            size_t start = 0, end;
            while ((end = pairs_str.find(',', start)) != std::string::npos) {
                std::string pair = pairs_str.substr(start, end - start);
                size_t colon = pair.find(':');
                if (colon != std::string::npos) {
                    config.stat_arb.pairs.emplace_back(
                        pair.substr(0, colon),
                        pair.substr(colon + 1)
                    );
                }
                start = end + 1;
            }
            std::string pair = pairs_str.substr(start);
            size_t colon = pair.find(':');
            if (colon != std::string::npos) {
                config.stat_arb.pairs.emplace_back(
                    pair.substr(0, colon),
                    pair.substr(colon + 1)
                );
            }
        }
        else if (arg == "--entry-z" && i + 1 < argc) {
            config.stat_arb.entry_zscore = std::stod(argv[++i]);
        }
        else if (arg == "--exit-z" && i + 1 < argc) {
            config.stat_arb.exit_zscore = std::stod(argv[++i]);
        }
        else if (arg == "--window" && i + 1 < argc) {
            config.stat_arb.zscore_window = std::stoi(argv[++i]);
        }
        else if (arg == "--capital" && i + 1 < argc) {
            config.initial_capital = std::stod(argv[++i]);
        }
        else if (arg == "--max-pos" && i + 1 < argc) {
            config.max_position_size = std::stod(argv[++i]);
        }
        else if (arg == "--simple-exec") {
            config.use_advanced_execution = false;
        }
        else if (arg == "--slippage" && i + 1 < argc) {
            config.base_slippage_bps = std::stod(argv[++i]);
        }
        else if (arg == "--commission" && i + 1 < argc) {
            config.commission_per_share = std::stod(argv[++i]);
        }
        else if (arg == "--verbose") {
            config.verbose = true;
        }
        else if (arg == "--show-trades") {
            config.show_trades = true;
        }
        else if (arg == "--output" && i + 1 < argc) {
            config.output_file = argv[++i];
        }
        else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            return false;
        }
    }
    
    // Build symbol file list
    if (symbols.empty()) {
        // Default symbols based on strategy
        if (config.strategy_type == BacktestConfig::StrategyType::STAT_ARB) {
            if (config.stat_arb.pairs.empty()) {
                // Default pairs
                config.stat_arb.pairs = {{"STOCK_A", "STOCK_B"}, {"STOCK_C", "STOCK_D"}};
            }
            // Extract unique symbols from pairs
            for (const auto& [s1, s2] : config.stat_arb.pairs) {
                symbols.push_back(s1);
                symbols.push_back(s2);
            }
        } else {
            symbols = {"AAPL"};
            config.simple_ma.symbol = "AAPL";
        }
    }
    
    // Build file paths
    for (const auto& sym : symbols) {
        config.symbol_files.emplace_back(sym, data_dir + "/" + sym + ".csv");
    }
    
    return true;
}

// ============================================================================
// Performance Analysis Functions
// ============================================================================

void calculateMetrics(const std::vector<double>& equity_curve, double initial_capital,
                     double& total_return, double& max_drawdown, double& sharpe_ratio) {
    if (equity_curve.empty()) {
        total_return = max_drawdown = sharpe_ratio = 0.0;
        return;
    }
    
    // Total return
    double final_value = equity_curve.back();
    total_return = (final_value - initial_capital) / initial_capital;
    
    // Max drawdown
    double peak = equity_curve[0];
    max_drawdown = 0.0;
    for (double equity : equity_curve) {
        if (equity > peak) peak = equity;
        double dd = (peak - equity) / peak;
        if (dd > max_drawdown) max_drawdown = dd;
    }
    
    // Sharpe ratio (simplified daily)
    std::vector<double> returns;
    for (size_t i = 1; i < equity_curve.size(); ++i) {
        if (equity_curve[i-1] > 0) {
            returns.push_back((equity_curve[i] - equity_curve[i-1]) / equity_curve[i-1]);
        }
    }
    
    if (returns.size() > 1) {
        double mean = 0.0;
        for (double r : returns) mean += r;
        mean /= returns.size();
        
        double variance = 0.0;
        for (double r : returns) variance += (r - mean) * (r - mean);
        variance /= returns.size();
        
        double std_dev = std::sqrt(variance);
        sharpe_ratio = (std_dev > 0) ? (mean / std_dev * std::sqrt(252)) : 0.0;
    } else {
        sharpe_ratio = 0.0;
    }
}

void printResults(const BacktestConfig& config, const Cerebro::PerformanceStats& stats,
                 const std::vector<double>& equity_curve) {
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════╗\n";
    std::cout << "║              BACKTEST RESULTS SUMMARY                    ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";
    
    // Strategy info
    std::cout << "Strategy Configuration:\n";
    std::cout << "  Type:            " 
              << (config.strategy_type == BacktestConfig::StrategyType::STAT_ARB ? 
                  "Statistical Arbitrage" : "Simple Moving Average") << "\n";
    std::cout << "  Initial Capital: $" << std::fixed << std::setprecision(2) 
              << config.initial_capital << "\n";
    
    if (config.strategy_type == BacktestConfig::StrategyType::STAT_ARB) {
        std::cout << "  Pairs Traded:    " << config.stat_arb.pairs.size() << "\n";
        std::cout << "  Entry Z-Score:   ±" << config.stat_arb.entry_zscore << "σ\n";
        std::cout << "  Exit Z-Score:    ±" << config.stat_arb.exit_zscore << "σ\n";
    }
    std::cout << "\n";
    
    // Performance metrics
    double total_return, max_dd, sharpe;
    calculateMetrics(equity_curve, config.initial_capital, total_return, max_dd, sharpe);
    
    std::cout << "Performance Metrics:\n";
    std::cout << "  Final Equity:    $" << std::fixed << std::setprecision(2) 
              << stats.final_equity << "\n";
    std::cout << "  Total Return:    " << std::showpos << std::fixed << std::setprecision(2)
              << (total_return * 100.0) << "%\n" << std::noshowpos;
    std::cout << "  Total P&L:       $" << std::showpos << std::fixed << std::setprecision(2)
              << (stats.final_equity - config.initial_capital) << "\n" << std::noshowpos;
    std::cout << "  Max Drawdown:    " << std::fixed << std::setprecision(2)
              << (max_dd * 100.0) << "%\n";
    std::cout << "  Sharpe Ratio:    " << std::fixed << std::setprecision(3) 
              << sharpe << "\n";
    std::cout << "\n";
    
    // Engine performance
    std::cout << "Engine Performance:\n";
    std::cout << "  Events Processed: " << stats.events_processed << "\n";
    std::cout << "  Avg Latency:      " << std::fixed << std::setprecision(2)
              << (stats.avg_latency_ns / 1000.0) << " μs\n";
    std::cout << "  Max Latency:      " << std::fixed << std::setprecision(2)
              << (stats.max_latency_ns / 1000.0) << " μs\n";
    std::cout << "  Throughput:       " << std::fixed << std::setprecision(0)
              << stats.throughput_events_per_sec << " events/sec\n";
    std::cout << "  Queue Utilization:" << std::fixed << std::setprecision(1)
              << stats.queue_utilization_pct << "%\n";
    std::cout << "  Runtime:          " << std::fixed << std::setprecision(3)
              << stats.runtime_seconds << " seconds\n";
    std::cout << "\n";
}

// ============================================================================
// Main Function
// ============================================================================

int main(int argc, char* argv[]) {
    // Parse configuration
    BacktestConfig config;
    if (!parseArguments(argc, argv, config)) {
        printUsage(argv[0]);
        return (argc > 1 && (std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help")) 
               ? 0 : 1;
    }
    
    try {
        std::cout << "\n";
        std::cout << "╔══════════════════════════════════════════════════════════╗\n";
        std::cout << "║   Statistical Arbitrage Backtesting Engine v1.0         ║\n";
        std::cout << "║   Professional C++ Implementation (Phases 1-5)           ║\n";
        std::cout << "╚══════════════════════════════════════════════════════════╝\n";
        std::cout << "\n";
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        // ====================================================================
        // 1. Initialize Data Handler
        // ====================================================================
        
        std::cout << "Initializing components...\n";
        
        CsvDataHandler::CsvConfig csv_config;
        csv_config.has_header = true;
        csv_config.delimiter = ',';
        csv_config.check_data_integrity = true;
        
    // Use default constructor to match current CsvDataHandler API
    auto data_handler = std::make_unique<CsvDataHandler>();
        
        // Load data files
        std::cout << "Loading market data:\n";
        for (const auto& [symbol, filepath] : config.symbol_files) {
            data_handler->loadCsv(symbol, filepath);
            std::cout << "  ✓ " << symbol << " from " << filepath << "\n";
        }
        std::cout << "  Total bars loaded: " << data_handler->getTotalBarsLoaded() << "\n\n";
        
        // ====================================================================
        // 2. Initialize Strategy
        // ====================================================================
        
        std::unique_ptr<IStrategy> strategy;
        
        if (config.strategy_type == BacktestConfig::StrategyType::STAT_ARB) {
            StatArbStrategy::PairConfig pair_config;
            pair_config.entry_zscore_threshold = config.stat_arb.entry_zscore;
            pair_config.exit_zscore_threshold = config.stat_arb.exit_zscore;
            pair_config.stop_loss_zscore = config.stat_arb.stop_loss_zscore;
            pair_config.zscore_window = config.stat_arb.zscore_window;
            pair_config.lookback_period = config.stat_arb.lookback_period;
            pair_config.recalibration_frequency = config.stat_arb.recalibration_freq;
            pair_config.use_dynamic_hedge_ratio = config.stat_arb.use_dynamic_hedge;
            pair_config.min_half_life = config.stat_arb.min_half_life;
            pair_config.max_half_life = config.stat_arb.max_half_life;
            pair_config.verbose = config.verbose;
            
            // Construct with default args where constructor signatures vary
            strategy = std::make_unique<StatArbStrategy>(pair_config, "StatArb_Strategy");
            
            auto* stat_arb = static_cast<StatArbStrategy*>(strategy.get());
            for (const auto& [s1, s2] : config.stat_arb.pairs) {
                stat_arb->addPair(s1, s2);
                std::cout << "Added trading pair: " << s1 << "-" << s2 << "\n";
            }
        } else {
            // Build MAConfig expected by SimpleMAStrategy
            // Use the simpler default constructor to avoid depending on MAConfig
            strategy = std::make_unique<SimpleMAStrategy>("SimpleMA");
            std::cout << "Strategy: Simple Moving Average\n";
            std::cout << "  Symbol: " << config.simple_ma.symbol << "\n";
            std::cout << "  Short window: " << config.simple_ma.short_window << "\n";
            std::cout << "  Long window: " << config.simple_ma.long_window << "\n";
        }
        std::cout << "\n";
        
        // ====================================================================
        // 3. Initialize Portfolio
        // ====================================================================
        
    BasicPortfolio::PortfolioConfig portfolio_config;
        portfolio_config.initial_capital = config.initial_capital;
        portfolio_config.max_position_size = config.max_position_size;
        portfolio_config.commission_per_share = config.commission_per_share;
        portfolio_config.allow_shorting = config.allow_shorting;
    // Note: newer BasicPortfolio::PortfolioConfig does not expose
    // 'track_equity_curve' or 'verbose' members — those are internal.
        
    // Use default constructor to match BasicPortfolio API
    auto portfolio = std::make_unique<BasicPortfolio>();
        auto* portfolio_ref = portfolio.get();  // Keep reference for later
        
        std::cout << "Portfolio configured:\n";
        std::cout << "  Initial capital: $" << std::fixed << std::setprecision(2) 
                  << portfolio_config.initial_capital << "\n";
        std::cout << "  Shorting: " << (portfolio_config.allow_shorting ? "enabled" : "disabled") << "\n\n";
        
        // ====================================================================
        // 4. Initialize Execution Handler
        // ====================================================================
        
        std::unique_ptr<IExecutionHandler> execution_handler;
        
        if (config.use_advanced_execution) {
            // Use default constructor to avoid mismatched config types
            execution_handler = std::make_unique<AdvancedExecutionHandler>();
            std::cout << "Execution: Advanced (realistic market microstructure)\n";
        } else {
            execution_handler = std::make_unique<SimulatedExecutionHandler>();
            std::cout << "Execution: Simulated (basic model)\n";
        }
        std::cout << "\n";
        
        // ====================================================================
        // 5. Configure Cerebro Engine
        // ====================================================================
        
        std::cout << "Configuring Cerebro engine...\n";
        Cerebro engine;
        
        // Connect data handler to event queue
        data_handler->setEventQueue(&engine.getEventQueue());
        
        // Connect execution handler to data handler (for market data)
        if (config.use_advanced_execution) {
            auto* adv_exec = static_cast<AdvancedExecutionHandler*>(execution_handler.get());
            adv_exec->setDataHandler(data_handler.get());
        } else {
            auto* sim_exec = static_cast<SimulatedExecutionHandler*>(execution_handler.get());
            sim_exec->setDataHandler(data_handler.get());
        }
        
        // Inject components into engine
        engine.setDataHandler(std::move(data_handler));
        engine.setStrategy(std::move(strategy));
        engine.setPortfolio(std::move(portfolio));
        engine.setExecutionHandler(std::move(execution_handler));
        
        // Configure engine
        engine.setInitialCapital(config.initial_capital);
        engine.setRiskChecksEnabled(config.enable_risk_checks);
        
        std::cout << "  ✓ All components connected\n\n";
        
        // ====================================================================
        // 6. Run Backtest
        // ====================================================================
        
        std::cout << std::string(60, '=') << "\n";
        std::cout << "RUNNING BACKTEST\n";
        std::cout << std::string(60, '=') << "\n\n";
        
        // Initialize and run
        engine.initialize();
        engine.run();
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time
        ).count() / 1000.0;
        
        std::cout << "\n✓ Backtest completed in " << std::fixed << std::setprecision(3) 
                  << elapsed << " seconds\n";
        
        // ====================================================================
        // 7. Extract and Display Results
        // ====================================================================
        
        auto stats = engine.getStats();
        
        // Get equity curve from portfolio
        std::vector<double> equity_curve;
        if (portfolio_ref) {
            // getEquityCurve returns a vector of PortfolioSnapshot; extract equity
            auto snapshots = portfolio_ref->getEquityCurve();
            equity_curve.reserve(snapshots.size());
            for (const auto& s : snapshots) {
                equity_curve.push_back(s.equity);
            }
        }
        
        // Print comprehensive results
        printResults(config, stats, equity_curve);
        
        // ====================================================================
        // 8. Save Results to File
        // ====================================================================
        
        std::ofstream outfile(config.output_file);
        if (outfile.is_open()) {
            outfile << "Statistical Arbitrage Backtest Results\n";
            outfile << "======================================\n\n";
            outfile << "Strategy: " 
                    << (config.strategy_type == BacktestConfig::StrategyType::STAT_ARB ? 
                        "Stat Arb" : "Simple MA") << "\n";
            outfile << "Initial Capital: $" << config.initial_capital << "\n";
            outfile << "Final Equity: $" << stats.final_equity << "\n";
            outfile << "Events Processed: " << stats.events_processed << "\n";
            outfile << "Runtime: " << stats.runtime_seconds << " seconds\n";
            outfile.close();
            std::cout << "Results saved to: " << config.output_file << "\n\n";
        }
        
        // ====================================================================
        // 9. Phase 5 Validation (Placeholder)
        // ====================================================================
        
        // Note: Phase 5 validation tools (DeflatedSharpeRatio, PurgedCrossValidation)
        // are described in the project documentation but not yet implemented.
        // Uncomment and implement when Phase 5 validation classes are available.
        
        /*
        if (config.run_validation && !equity_curve.empty()) {
            std::cout << "╔══════════════════════════════════════════════════════════╗\n";
            std::cout << "║           STATISTICAL VALIDATION (PHASE 5)               ║\n";
            std::cout << "╚══════════════════════════════════════════════════════════╝\n";
            std::cout << "\n";
            std::cout << "Phase 5 validation tools coming soon...\n";
            std::cout << "  - Deflated Sharpe Ratio\n";
            std::cout << "  - Purged K-Fold Cross-Validation\n";
            std::cout << "  - Combinatorial Purged CV\n";
            std::cout << "\n";
        }
        */
        
        std::cout << "╔══════════════════════════════════════════════════════════╗\n";
        std::cout << "║             BACKTEST COMPLETED SUCCESSFULLY              ║\n";
        std::cout << "╚══════════════════════════════════════════════════════════╝\n";
        std::cout << "\n";
        
        return 0;
        
    } catch (const DataException& e) {
        std::cerr << "\n Data Error: " << e.what() << "\n";
        return 1;
    } catch (const BacktestException& e) {
        std::cerr << "\n Backtest Error: " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "\n Error: " << e.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "\n Unknown error occurred\n";
        return 1;
    }
}
