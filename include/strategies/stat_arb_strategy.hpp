// stat_arb_strategy.hpp
// Statistical Arbitrage Pairs Trading Strategy for Backtesting Engine
// Implements cointegration-based pairs trading with dynamic hedge ratios

#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <deque>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <optional>
#include <chrono>
#include "../interfaces/strategy.hpp"
#include "../core/event_types.hpp"
#include "../core/exceptions.hpp"
#include "../concurrent/disruptor_queue.hpp"
#include "rolling_statistics.hpp"
#include "simd_rolling_statistics.hpp"
#include "cointegration_analyzer.hpp"
#include <iostream>

namespace backtesting {

// ============================================================================
// Statistical Arbitrage Pairs Trading Strategy
// ============================================================================

class StatArbStrategy : public IStrategy {
public:
    struct PairConfig {
        // Pair selection parameters
        double cointegration_pvalue_threshold;  // Max p-value for cointegration test
        size_t lookback_period;  // Days for cointegration test
        size_t recalibration_frequency;  // Recalibrate every N trading days
        
        // Signal generation parameters
        double entry_zscore_threshold;
        double exit_zscore_threshold;
        double stop_loss_zscore;
        size_t zscore_window;  // Rolling window for z-score calculation
        
        // Risk management
        double max_position_value;  // Max $ exposure per pair
        double max_pairs;  // Maximum concurrent pairs
        double min_half_life;  // Minimum mean reversion half-life (days)
        double max_half_life;  // Maximum mean reversion half-life
        
        // Execution
        bool use_dynamic_hedge_ratio;
        double hedge_ratio_ema_alpha;  // EMA smoothing for hedge ratio
        bool enable_intraday_execution;
        
        // Filters
        double min_liquidity;  // Minimum daily dollar volume
        double max_spread_bps;  // Maximum bid-ask spread in basis points
        
        // Default constructor
        PairConfig()
            : cointegration_pvalue_threshold(0.05)
            , lookback_period(252)
            , recalibration_frequency(21)
            , entry_zscore_threshold(2.0)
            , exit_zscore_threshold(0.5)
            , stop_loss_zscore(4.0)
            , zscore_window(60)
            , max_position_value(100000.0)
            , max_pairs(10)
            , min_half_life(5)
            , max_half_life(120)
            , use_dynamic_hedge_ratio(true)
            , hedge_ratio_ema_alpha(0.95)
            , enable_intraday_execution(false)
            , min_liquidity(1000000.0)
            , max_spread_bps(10.0) {}

        // Toggle verbose debug printing
        bool verbose = false;
        
        // Static factory for default config
        static PairConfig getDefault() {
            return PairConfig();
        }
    };
    
    struct PairState {
        std::string symbol1;
        std::string symbol2;
        
        // Cointegration parameters
        double hedge_ratio = 1.0;  // Units of symbol2 per unit of symbol1
        double spread_mean = 0.0;
        double spread_std = 1.0;
        double half_life = 0.0;
        double cointegration_pvalue = 1.0;
        
        // Rolling statistics
        // RollingStatistics spread_stats;
        SIMDRollingStatistics spread_stats;
        std::deque<double> spread_history;
        
        // Current state
        double current_spread = 0.0;
        double current_zscore = 0.0;
        int position_state = 0;  // 1 = long spread, -1 = short spread, 0 = flat
        double entry_spread = 0.0;
        double entry_zscore = 0.0;
        std::chrono::nanoseconds entry_time;
        
        // Performance tracking
        double unrealized_pnl = 0.0;
        double realized_pnl = 0.0;
        int num_trades = 0;
        int num_wins = 0;
        
        // Price data buffers
        std::deque<double> prices1;
        std::deque<double> prices2;
        double latest_price1 = 0.0;
        double latest_price2 = 0.0;
        
        // Timing
        size_t bars_since_recalibration = 0;
        bool is_active = true;
        
        PairState(const std::string& s1, const std::string& s2, size_t window)
            : symbol1(s1), symbol2(s2), spread_stats(window) {}
    };
    
private:
    PairConfig config_;
    std::string strategy_name_;
    
    // Pair management
    std::unordered_map<std::string, std::vector<std::string>> symbol_pairs_;  // symbol -> paired symbols
    std::unordered_map<std::string, PairState> active_pairs_;  // "SYM1_SYM2" -> state
    
    // Market data cache
    std::unordered_map<std::string, MarketEvent> latest_market_data_;
    std::unordered_map<std::string, std::deque<double>> price_history_;
    std::unordered_map<std::string, double> average_volumes_;
    
    // Analysis components
    std::unique_ptr<CointegrationAnalyzer> coint_analyzer_;
    
    // Performance tracking
    uint64_t signals_generated_ = 0;
    uint64_t pairs_traded_ = 0;
    uint64_t recalibrations_ = 0;
    double total_pnl_ = 0.0;
    
    // Helpers
    DisruptorQueue<EventVariant, 65536>* getEventQueue() {
        return static_cast<DisruptorQueue<EventVariant, 65536>*>(event_queue_);
    }
    
    std::string getPairKey(const std::string& s1, const std::string& s2) const {
        return (s1 < s2) ? s1 + "_" + s2 : s2 + "_" + s1;
    }
    
    // Calculate spread: spread = price1 - hedge_ratio * price2
    double calculateSpread(double price1, double price2, double hedge_ratio) const {
        return price1 - hedge_ratio * price2;
    }
    
    // Calculate hedge ratio using OLS regression
    double calculateHedgeRatio(const std::deque<double>& prices1, 
                               const std::deque<double>& prices2) {
        if (prices1.size() != prices2.size() || prices1.size() < 20) {
            return 1.0;  // Default to 1:1 if insufficient data
        }
        
        // Simple OLS: hedge_ratio = Cov(p1, p2) / Var(p2)
        double mean1 = std::accumulate(prices1.begin(), prices1.end(), 0.0) / prices1.size();
        double mean2 = std::accumulate(prices2.begin(), prices2.end(), 0.0) / prices2.size();
        
        double covariance = 0.0;
        double variance2 = 0.0;
        
        for (size_t i = 0; i < prices1.size(); ++i) {
            double diff1 = prices1[i] - mean1;
            double diff2 = prices2[i] - mean2;
            covariance += diff1 * diff2;
            variance2 += diff2 * diff2;
        }
        
        if (variance2 > 0) {
            return covariance / variance2;
        }
        return 1.0;
    }
    
    // Calculate half-life of mean reversion using OLS on spread changes
    double calculateHalfLife(const std::deque<double>& spread_history) {
        if (spread_history.size() < 20) return 0.0;
        
        // Regression: spread_change = lambda * lagged_spread + error
        // Half-life = -log(2) / lambda
        std::vector<double> y_changes;
        std::vector<double> x_lagged;
        
        for (size_t i = 1; i < spread_history.size(); ++i) {
            y_changes.push_back(spread_history[i] - spread_history[i-1]);
            x_lagged.push_back(spread_history[i-1]);
        }
        
        // OLS regression
        double mean_x = std::accumulate(x_lagged.begin(), x_lagged.end(), 0.0) / x_lagged.size();
        double mean_y = std::accumulate(y_changes.begin(), y_changes.end(), 0.0) / y_changes.size();
        
        double numerator = 0.0;
        double denominator = 0.0;
        
        for (size_t i = 0; i < x_lagged.size(); ++i) {
            numerator += (x_lagged[i] - mean_x) * (y_changes[i] - mean_y);
            denominator += (x_lagged[i] - mean_x) * (x_lagged[i] - mean_x);
        }
        
        if (denominator > 0) {
            double beta = numerator / denominator; // regression slope (should be negative for mean reversion)
            if (config_.verbose) std::cout << "[Strategy::half] beta=" << beta << ", numerator=" << numerator << ", denominator=" << denominator << std::endl;
            if (beta < 0.0) {
                double lambda = -beta;
                if (lambda > 1e-12) {
                    return std::log(2.0) / lambda;
                }
            }
        }

        return 0.0;  // No mean reversion detected
    }
    
    // Recalibrate pair parameters
    void recalibratePair(PairState& pair) {
        if (pair.prices1.size() < config_.lookback_period) return;
        
        // Recalculate hedge ratio
        if (config_.use_dynamic_hedge_ratio) {
            double new_ratio = calculateHedgeRatio(pair.prices1, pair.prices2);
            // Smooth the hedge ratio using EMA
            pair.hedge_ratio = config_.hedge_ratio_ema_alpha * pair.hedge_ratio + 
                               (1 - config_.hedge_ratio_ema_alpha) * new_ratio;
        }
        
        // Recalculate spread statistics
        pair.spread_history.clear();
        for (size_t i = 0; i < pair.prices1.size(); ++i) {
            double spread = calculateSpread(pair.prices1[i], pair.prices2[i], pair.hedge_ratio);
            pair.spread_history.push_back(spread);
        }
        
        // Update rolling statistics
        pair.spread_stats.reset();
        for (double spread : pair.spread_history) {
            pair.spread_stats.update(spread);
        }
        
        pair.spread_mean = pair.spread_stats.getMean();
        pair.spread_std = pair.spread_stats.getStdDev();
        
        // Calculate half-life
        pair.half_life = calculateHalfLife(pair.spread_history);
        
        // Activate/deactivate pair based on half-life bounds
        if (pair.half_life >= config_.min_half_life && pair.half_life <= config_.max_half_life) {
            pair.is_active = true;
        } else {
            pair.is_active = false;
        }
        
        pair.bars_since_recalibration = 0;
        recalibrations_++;
    }
    
    // Generate trading signals for a pair
    void generatePairSignals(PairState& pair, const MarketEvent& event) {
        if (config_.verbose) std::cout << "generatePairSignals called for " << pair.symbol1 << "-" << pair.symbol2 << std::endl;
        
        // Update current spread and z-score
        // Update current spread and z-score
        pair.current_spread = calculateSpread(pair.latest_price1, pair.latest_price2, pair.hedge_ratio);
        pair.spread_stats.update(pair.current_spread);
        
        // Use the rolling statistics' stddev (fresh) for z-score calculation
        double spread_std = pair.spread_stats.getStdDev();
        pair.spread_std = spread_std; // keep cached value in sync
        if (spread_std > 0.0) {
            pair.current_zscore = (pair.current_spread - pair.spread_stats.getMean()) / spread_std;
        } else {
            pair.current_zscore = 0.0;
        }
        
    if (config_.verbose) std::cout << "Z-score for pair " << pair.symbol1 << "-" << pair.symbol2 << ": " << pair.current_zscore << std::endl;
        
        // Check liquidity filter
        bool liquidity_ok = true;
        double dollar_volume1 = 0.0;
        double dollar_volume2 = 0.0;
        auto vol1_it = average_volumes_.find(pair.symbol1);
        auto vol2_it = average_volumes_.find(pair.symbol2);
        if (vol1_it != average_volumes_.end() && vol2_it != average_volumes_.end()) {
            dollar_volume1 = vol1_it->second * pair.latest_price1;
            dollar_volume2 = vol2_it->second * pair.latest_price2;
            liquidity_ok = (dollar_volume1 >= config_.min_liquidity && 
                           dollar_volume2 >= config_.min_liquidity);
        }
        
        // Debug: print average volumes used in liquidity check
        double avg_vol1 = (vol1_it != average_volumes_.end()) ? vol1_it->second : 0.0;
        double avg_vol2 = (vol2_it != average_volumes_.end()) ? vol2_it->second : 0.0;
    if (config_.verbose) std::cout << "Liquidity check for pair " << pair.symbol1 << "-" << pair.symbol2 << ": " << liquidity_ok << " (avg_vol1: " << avg_vol1 << ", avg_vol2: " << avg_vol2 << ", dollar1: " << dollar_volume1 << ", dollar2: " << dollar_volume2 << ", min: " << config_.min_liquidity << ")" << std::endl;
        
        if (!liquidity_ok || !pair.is_active) {
            if (config_.verbose) std::cout << "Skipping signal generation for pair " << pair.symbol1 << "-" << pair.symbol2 << ": liquidity_ok=" << liquidity_ok << ", is_active=" << pair.is_active << std::endl;
            return;
        }
        
        // Signal generation logic
        SignalEvent signal;
        signal.timestamp = event.timestamp;
        signal.sequence_id = event.sequence_id;
        signal.strategy_id = strategy_name_;
        
        if (pair.position_state == 0) {
            // No position - check for entry signals
            if (std::abs(pair.current_zscore) > config_.entry_zscore_threshold) {
                // Enter position
                if (pair.current_zscore > config_.entry_zscore_threshold) {
                    // Spread is too high - short the spread (short sym1, long sym2)
                    signal.symbol = pair.symbol1;
                    signal.direction = SignalEvent::Direction::SHORT;
                    signal.strength = std::min(1.0, std::abs(pair.current_zscore) / 4.0);
                    signal.metadata["pair_symbol"] = 1.0;  // Identifier for pair's first symbol
                    signal.metadata["hedge_ratio"] = pair.hedge_ratio;
                    signal.metadata["zscore"] = pair.current_zscore;
                    signal.metadata["half_life"] = pair.half_life;
                    emitSignal(signal);
                    
                    // Hedge leg
                    signal.symbol = pair.symbol2;
                    signal.direction = SignalEvent::Direction::LONG;
                    signal.metadata["pair_symbol"] = 2.0;  // Identifier for pair's second symbol
                    emitSignal(signal);
                    
                    pair.position_state = -1;
                    pair.entry_spread = pair.current_spread;
                    pair.entry_zscore = pair.current_zscore;
                    pair.entry_time = event.timestamp;
                    pairs_traded_++;
                    
                } else if (pair.current_zscore < -config_.entry_zscore_threshold) {
                    // Spread is too low - long the spread (long sym1, short sym2)
                    signal.symbol = pair.symbol1;
                    signal.direction = SignalEvent::Direction::LONG;
                    signal.strength = std::min(1.0, std::abs(pair.current_zscore) / 4.0);
                    signal.metadata["pair_symbol"] = 1.0;
                    signal.metadata["hedge_ratio"] = pair.hedge_ratio;
                    signal.metadata["zscore"] = pair.current_zscore;
                    signal.metadata["half_life"] = pair.half_life;
                    emitSignal(signal);
                    
                    // Hedge leg
                    signal.symbol = pair.symbol2;
                    signal.direction = SignalEvent::Direction::SHORT;
                    signal.metadata["pair_symbol"] = 2.0;
                    emitSignal(signal);
                    
                    pair.position_state = 1;
                    pair.entry_spread = pair.current_spread;
                    pair.entry_zscore = pair.current_zscore;
                    pair.entry_time = event.timestamp;
                    pairs_traded_++;
                }
                
                signals_generated_ += 2;  // Two signals per pair trade
                if (config_.verbose) std::cout << "Generated entry signals for pair " << pair.symbol1 << "-" << pair.symbol2 << std::endl;
            }
            
        } else {
            // Have position - check for exit signals
            bool should_exit = false;
            std::string exit_reason;
            
            // Exit on mean reversion
            if (std::abs(pair.current_zscore) < config_.exit_zscore_threshold) {
                should_exit = true;
                exit_reason = "mean_reversion";
            }
            
            // Exit on stop loss
            if (std::abs(pair.current_zscore) > config_.stop_loss_zscore) {
                should_exit = true;
                exit_reason = "stop_loss";
            }
            
            // Exit if z-score flips sign (went too far through mean)
            if ((pair.position_state == 1 && pair.current_zscore > config_.exit_zscore_threshold) ||
                (pair.position_state == -1 && pair.current_zscore < -config_.exit_zscore_threshold)) {
                should_exit = true;
                exit_reason = "zscore_flip";
            }
            
            if (should_exit) {
                // Close both legs
                signal.symbol = pair.symbol1;
                signal.direction = SignalEvent::Direction::EXIT;
                signal.strength = 1.0;
                signal.metadata["exit_reason"] = (exit_reason == "stop_loss") ? -1.0 : 1.0;
                signal.metadata["final_zscore"] = pair.current_zscore;
                emitSignal(signal);
                
                signal.symbol = pair.symbol2;
                signal.direction = SignalEvent::Direction::EXIT;
                emitSignal(signal);
                
                // Calculate P&L (simplified)
                double spread_change = pair.current_spread - pair.entry_spread;
                double pnl = spread_change * pair.position_state;  // Simplified P&L
                
                pair.realized_pnl += pnl;
                pair.num_trades++;
                if (pnl > 0) pair.num_wins++;
                
                // Reset position
                pair.position_state = 0;
                pair.entry_spread = 0.0;
                pair.entry_zscore = 0.0;
                
                signals_generated_ += 2;
                if (config_.verbose) std::cout << "Generated exit signals for pair " << pair.symbol1 << "-" << pair.symbol2 << " reason: " << exit_reason << std::endl;
            }
        }
    }
    
public:
    explicit StatArbStrategy(const PairConfig& config = PairConfig(), 
                            const std::string& name = "StatArb")
        : config_(config), strategy_name_(name) {
        if (config_.verbose) std::cout << "StatArbStrategy created: " << strategy_name_ << std::endl;
        coint_analyzer_ = std::make_unique<CointegrationAnalyzer>();
    }
    
    // Add a trading pair
    void addPair(const std::string& symbol1, const std::string& symbol2) {
        std::string key = getPairKey(symbol1, symbol2);
        if (active_pairs_.find(key) == active_pairs_.end()) {
            active_pairs_.emplace(key, PairState(symbol1, symbol2, config_.zscore_window));
            
            // Register symbols for quick lookup
            symbol_pairs_[symbol1].push_back(symbol2);
            symbol_pairs_[symbol2].push_back(symbol1);
            if (config_.verbose) std::cout << "Added pair: " << symbol1 << "-" << symbol2 << std::endl;
        }
    }
    
    // IStrategy interface implementation
    void calculateSignals(const MarketEvent& event) override {
        if (config_.verbose) std::cout << "calculateSignals called for symbol: " << event.symbol << std::endl;
        // Update market data cache
        latest_market_data_[event.symbol] = event;
        
        // Update price history
        auto& prices = price_history_[event.symbol];
        prices.push_back(event.close);
        if (prices.size() > config_.lookback_period * 2) {
            prices.pop_front();
        }
        
        // Update volume tracking
        auto& avg_vol = average_volumes_[event.symbol];
        avg_vol = avg_vol * 0.95 + event.volume * 0.05;  // EMA of volume

    // Debug: print per-symbol updated avg vol and latest price
    if (config_.verbose) std::cout << "  Event: " << event.symbol << " close=" << event.close << " volume=" << event.volume \
          << " avg_vol=" << avg_vol << std::endl;
        
        // Check all pairs involving this symbol
        auto pairs_it = symbol_pairs_.find(event.symbol);
        if (pairs_it == symbol_pairs_.end()) return;
        
        for (const auto& paired_symbol : pairs_it->second) {
            std::string pair_key = getPairKey(event.symbol, paired_symbol);
            auto pair_it = active_pairs_.find(pair_key);
            if (pair_it == active_pairs_.end()) continue;
            
            auto& pair = pair_it->second;
            
            // Update pair's price data
            if (event.symbol == pair.symbol1) {
                pair.latest_price1 = event.close;
                pair.prices1.push_back(event.close);
                if (pair.prices1.size() > config_.lookback_period) {
                    pair.prices1.pop_front();
                }
            } else {
                pair.latest_price2 = event.close;
                pair.prices2.push_back(event.close);
                if (pair.prices2.size() > config_.lookback_period) {
                    pair.prices2.pop_front();
                }
            }
            
            // Only process if we have both prices
            if (pair.latest_price1 <= 0 || pair.latest_price2 <= 0) continue;

            // Debug: print pair buffer sizes and latest prices
            if (config_.verbose) std::cout << "    Pair check: " << pair.symbol1 << "-" << pair.symbol2 \
                      << " prices1_sz=" << pair.prices1.size() << " prices2_sz=" << pair.prices2.size() \
                      << " latest1=" << pair.latest_price1 << " latest2=" << pair.latest_price2 << std::endl;
            
            // Check if recalibration is needed
            pair.bars_since_recalibration++;
            if (pair.bars_since_recalibration >= config_.recalibration_frequency) {
                recalibratePair(pair);
            }
            
            // Generate trading signals: ensure we have enough history for the effective z-score window
            size_t effective_window = std::min(config_.zscore_window, config_.lookback_period);
            if (pair.prices1.size() >= effective_window && pair.prices2.size() >= effective_window) {
                if (config_.verbose) std::cout << "Calling generatePairSignals for " << pair.symbol1 << "-" << pair.symbol2 << " (effective_window=" << effective_window << ")" << std::endl;
                generatePairSignals(pair, event);
            } else {
                if (config_.verbose) std::cout << "Insufficient history for pair " << pair.symbol1 << "-" << pair.symbol2 << ": " << pair.prices1.size() << "," << pair.prices2.size() << " needed=" << effective_window << std::endl;
            }
        }
    }
    
    void reset() override {
        symbol_pairs_.clear();
        active_pairs_.clear();
        latest_market_data_.clear();
        price_history_.clear();
        average_volumes_.clear();
        signals_generated_ = 0;
        pairs_traded_ = 0;
        recalibrations_ = 0;
        total_pnl_ = 0.0;
    }
    
    void initialize() override {
        std::cout << "StatArbStrategy initialized" << std::endl;
        // Do not clear configured pairs here; only reset runtime counters and performance tracking
        signals_generated_ = 0;
        pairs_traded_ = 0;
        recalibrations_ = 0;
        total_pnl_ = 0.0;
    }
    
    void shutdown() override {
        // Close all open positions
        for (auto& [key, pair] : active_pairs_) {
            if (pair.position_state != 0) {
                SignalEvent signal;
                signal.direction = SignalEvent::Direction::EXIT;
                signal.strength = 1.0;
                signal.strategy_id = strategy_name_;
                
                signal.symbol = pair.symbol1;
                emitSignal(signal);
                
                signal.symbol = pair.symbol2;
                emitSignal(signal);
            }
        }

        // Diagnostic dump: final pair buffer sizes and state
        std::cout << "StatArbStrategy shutdown: pair diagnostics" << std::endl;
        for (const auto& [key, pair] : active_pairs_) {
            double avg1 = 0.0, avg2 = 0.0;
            auto it1 = average_volumes_.find(pair.symbol1);
            auto it2 = average_volumes_.find(pair.symbol2);
            if (it1 != average_volumes_.end()) avg1 = it1->second;
            if (it2 != average_volumes_.end()) avg2 = it2->second;
            std::cout << "  Pair " << pair.symbol1 << "-" << pair.symbol2
                      << " prices1_sz=" << pair.prices1.size()
                      << " prices2_sz=" << pair.prices2.size()
                      << " is_active=" << pair.is_active
                      << " half_life=" << pair.half_life
                      << " avg_vol1=" << avg1 << " avg_vol2=" << avg2 << std::endl;
        }
    }
    
    std::string getName() const override {
        return strategy_name_;
    }
    
    // Additional methods for analysis
    struct PairStats {
        std::string symbol1;
        std::string symbol2;
        double hedge_ratio;
        double current_zscore;
        double half_life;
        int position_state;
        double realized_pnl;
        double win_rate;
    };
    
    std::vector<PairStats> getPairStatistics() const {
        std::vector<PairStats> stats;
        for (const auto& [key, pair] : active_pairs_) {
            stats.push_back({
                pair.symbol1, pair.symbol2,
                pair.hedge_ratio, pair.current_zscore,
                pair.half_life, pair.position_state,
                pair.realized_pnl,
                pair.num_trades > 0 ? static_cast<double>(pair.num_wins) / pair.num_trades : 0.0
            });
        }
        return stats;
    }
    
    struct StrategyStats {
        uint64_t total_signals;
        uint64_t pairs_traded;
        uint64_t recalibrations;
        size_t active_pairs;
        size_t pairs_with_positions;
        double total_pnl;
    };
    
    StrategyStats getStats() const {
        size_t pairs_with_pos = 0;
        double pnl = 0.0;
        for (const auto& [_, pair] : active_pairs_) {
            if (pair.position_state != 0) pairs_with_pos++;
            pnl += pair.realized_pnl;
        }
        
        return {
            signals_generated_,
            pairs_traded_,
            recalibrations_,
            active_pairs_.size(),
            pairs_with_pos,
            pnl
        };
    }
};

} // namespace backtesting