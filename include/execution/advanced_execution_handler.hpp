// advanced_execution_handler.hpp
// Advanced Execution Handler with Market Microstructure Modeling
// Implements sophisticated slippage, market impact, and order book simulation

#pragma once

#include <random>
#include <chrono>
#include <string>
#include <unordered_map>
#include <queue>
#include <cmath>
#include <algorithm>
#include <memory>
#include "../interfaces/execution_handler.hpp"
#include "../interfaces/data_handler.hpp"
#include "../core/event_types.hpp"
#include "../core/exceptions.hpp"
#include "../concurrent/disruptor_queue.hpp"

namespace backtesting {

// ============================================================================
// Advanced Execution Handler with Market Microstructure Modeling
// ============================================================================

class AdvancedExecutionHandler : public IExecutionHandler {
public:
    // Market impact models
    enum class ImpactModel {
        LINEAR,           // Linear permanent + temporary impact
        SQUARE_ROOT,      // Almgren-Chriss square-root model
        POWER_LAW,        // Power law impact function
        BARRA              // BARRA market impact model
    };
    
    // Slippage models
    enum class SlippageModel {
        FIXED_BPS,        // Fixed basis points
        VOLATILITY_BASED, // Proportional to volatility
        VOLUME_BASED,     // Based on participation rate
        HYBRID            // Combination of factors
    };
    
    struct AdvancedExecutionConfig {
        // Market impact parameters
        ImpactModel impact_model;
        double permanent_impact_coefficient;  // Permanent price impact
        double temporary_impact_coefficient;  // Temporary price impact
        double impact_decay_rate;  // Decay rate for temporary impact
        
        // Almgren-Chriss model parameters
        double eta;  // Permanent impact constant
        double gamma;  // Temporary impact constant
        double alpha;  // Power for permanent impact (0.5 = square root)
        double beta;   // Power for temporary impact
        
        // Slippage model parameters
        SlippageModel slippage_model;
        double base_slippage_bps;
        double volatility_multiplier;
        double participation_penalty;  // BPS per 1% of ADV
        
        // Order book simulation
        bool simulate_order_book;
        double book_depth_factor;  // Fraction of ADV available at each price level
        double tick_size;
        size_t book_levels;
        
        // Latency and rejection
        std::chrono::microseconds min_latency;
        std::chrono::microseconds max_latency;
        double rejection_probability;
        double partial_fill_probability;
        
        // Transaction costs
        double commission_per_share;
        double min_commission;
        double sec_fee_per_million;  // SEC transaction fee
        double taf_fee_per_share;  // FINRA TAF
        
        // Risk limits
        double max_order_size_pct_adv;  // Max 10% of ADV
        double max_participation_rate;   // Max 25% of volume
        
        // Advanced features
        bool enable_dark_pool;
        double dark_pool_probability;
        double dark_pool_improvement_bps;
        
        bool enable_iceberg_orders;
        double iceberg_display_ratio;  // Show only 10% of order
        
        // Default constructor
        AdvancedExecutionConfig()
            : impact_model(ImpactModel::SQUARE_ROOT)
            , permanent_impact_coefficient(0.1)
            , temporary_impact_coefficient(0.5)
            , impact_decay_rate(0.5)
            , eta(2.5e-7)
            , gamma(2.5e-7)
            , alpha(0.5)
            , beta(0.5)
            , slippage_model(SlippageModel::HYBRID)
            , base_slippage_bps(2.0)
            , volatility_multiplier(1.5)
            , participation_penalty(10.0)
            , simulate_order_book(true)
            , book_depth_factor(0.1)
            , tick_size(0.01)
            , book_levels(10)
            , min_latency(std::chrono::microseconds{100})
            , max_latency(std::chrono::microseconds{1000})
            , rejection_probability(0.02)
            , partial_fill_probability(0.1)
            , commission_per_share(0.005)
            , min_commission(1.0)
            , sec_fee_per_million(22.10)
            , taf_fee_per_share(0.000119)
            , max_order_size_pct_adv(0.10)
            , max_participation_rate(0.25)
            , enable_dark_pool(false)
            , dark_pool_probability(0.3)
            , dark_pool_improvement_bps(0.5)
            , enable_iceberg_orders(false)
            , iceberg_display_ratio(0.1) {}
    };
    
private:
    AdvancedExecutionConfig config_;
    IDataHandler* data_handler_ = nullptr;
    
    // Random number generators
    std::mt19937 rng_;
    std::normal_distribution<> normal_dist_;
    std::uniform_real_distribution<> uniform_dist_;
    std::exponential_distribution<> latency_dist_;
    
    // Market microstructure state
    struct MarketState {
        double volatility = 0.02;  // Daily volatility
        double avg_spread_bps = 5.0;
        double imbalance = 0.0;  // Order flow imbalance
        double momentum = 0.0;   // Short-term price momentum
        std::deque<double> recent_volumes;
        std::deque<double> recent_spreads;
        std::chrono::nanoseconds last_update;
    };
    std::unordered_map<std::string, MarketState> market_states_;
    
    // Market impact tracking
    struct ImpactState {
        double permanent_impact = 0.0;
        double temporary_impact = 0.0;
        double cumulative_volume = 0.0;
        std::chrono::nanoseconds last_trade_time;
        std::deque<std::pair<double, std::chrono::nanoseconds>> impact_decay_queue;
    };
    std::unordered_map<std::string, ImpactState> impact_states_;
    
    // Order book simulation
    struct OrderBookLevel {
        double price;
        double quantity;
        int num_orders;
    };
    
    struct SimulatedOrderBook {
        std::vector<OrderBookLevel> bids;
        std::vector<OrderBookLevel> asks;
        double mid_price;
        double spread;
        std::chrono::nanoseconds last_update;
    };
    std::unordered_map<std::string, SimulatedOrderBook> order_books_;
    
    // Execution analytics
    struct ExecutionStats {
        uint64_t total_orders = 0;
        uint64_t filled_orders = 0;
        uint64_t rejected_orders = 0;
        uint64_t partial_fills = 0;
        uint64_t dark_pool_fills = 0;
        double total_slippage = 0.0;
        double total_market_impact = 0.0;
        double total_commission = 0.0;
        double total_fees = 0.0;
        double worst_slippage = 0.0;
        double best_execution = std::numeric_limits<double>::max();
        double total_notional = 0.0;
        double vwap_slippage = 0.0;
    } stats_;
    
    // Helper to get event queue
    DisruptorQueue<EventVariant, 65536>* getEventQueue() {
        return static_cast<DisruptorQueue<EventVariant, 65536>*>(event_queue_);
    }
    
    // Generate unique fill ID
    std::atomic<uint64_t> fill_id_counter_{1};
    std::string generateFillId() {
        return "FILL_ADV_" + std::to_string(fill_id_counter_.fetch_add(1));
    }
    
    // Update market microstructure state
    void updateMarketState(const std::string& symbol, const MarketEvent& market) {
        auto& state = market_states_[symbol];
        
        // Update volatility estimate (simplified EWMA)
        if (state.last_update.count() > 0) {
            double return_val = std::log(market.close / latest_prices_[symbol]);
            double new_vol = std::abs(return_val);
            state.volatility = 0.94 * state.volatility + 0.06 * new_vol;
        }
        
        // Update spread tracking
        double spread_bps = 10000.0 * (market.ask - market.bid) / market.close;
        state.recent_spreads.push_back(spread_bps);
        if (state.recent_spreads.size() > 100) {
            state.recent_spreads.pop_front();
        }
        state.avg_spread_bps = std::accumulate(state.recent_spreads.begin(), 
                                              state.recent_spreads.end(), 0.0) / 
                               state.recent_spreads.size();
        
        // Update volume tracking
        state.recent_volumes.push_back(market.volume);
        if (state.recent_volumes.size() > 20) {
            state.recent_volumes.pop_front();
        }
        
        // Calculate order flow imbalance (simplified)
        double bid_vol = market.bid_size;
        double ask_vol = market.ask_size;
        state.imbalance = (bid_vol - ask_vol) / (bid_vol + ask_vol + 1.0);
        
        // Calculate short-term momentum
        if (latest_prices_.find(symbol) != latest_prices_.end()) {
            state.momentum = 0.7 * state.momentum + 0.3 * (market.close - latest_prices_[symbol]);
        }
        
        state.last_update = market.timestamp;
        latest_prices_[symbol] = market.close;
    }
    
    // Simulate order book
    void simulateOrderBook(const std::string& symbol, double mid_price, double spread, double volume) {
        if (!config_.simulate_order_book) return;
        
        auto& book = order_books_[symbol];
        book.mid_price = mid_price;
        book.spread = spread;
        book.bids.clear();
        book.asks.clear();
        
        // Generate synthetic order book levels
        double level_volume = volume * config_.book_depth_factor / config_.book_levels;
        
        for (size_t i = 0; i < config_.book_levels; ++i) {
            // Bid levels
            OrderBookLevel bid_level;
            bid_level.price = mid_price - spread/2 - i * config_.tick_size;
            bid_level.quantity = level_volume * (1.0 + normal_dist_(rng_) * 0.3);
            bid_level.num_orders = static_cast<int>(5 + uniform_dist_(rng_) * 10);
            book.bids.push_back(bid_level);
            
            // Ask levels
            OrderBookLevel ask_level;
            ask_level.price = mid_price + spread/2 + i * config_.tick_size;
            ask_level.quantity = level_volume * (1.0 + normal_dist_(rng_) * 0.3);
            ask_level.num_orders = static_cast<int>(5 + uniform_dist_(rng_) * 10);
            book.asks.push_back(ask_level);
        }
        
        book.last_update = std::chrono::steady_clock::now().time_since_epoch();
    }
    
    // Calculate market impact based on chosen model
    double calculateMarketImpact(const std::string& symbol, double order_size, 
                                double price, double adv, bool is_buy) {
        auto& impact = impact_states_[symbol];
        auto& market = market_states_[symbol];
        
        double participation_rate = std::abs(order_size) / (adv + 1.0);
        double impact_bps = 0.0;
        
        switch (config_.impact_model) {
            case ImpactModel::LINEAR: {
                // Linear impact model
                impact_bps = config_.permanent_impact_coefficient * participation_rate * 10000;
                break;
            }
            
            case ImpactModel::SQUARE_ROOT: {
                // Almgren-Chriss square-root model
                double sigma = market.volatility * std::sqrt(252);  // Annualized volatility
                double permanent = config_.eta * std::pow(participation_rate, config_.alpha);
                double temporary = config_.gamma * std::pow(participation_rate, config_.beta);
                
                impact.permanent_impact += permanent * sigma * (is_buy ? 1 : -1);
                impact.temporary_impact += temporary * sigma * (is_buy ? 1 : -1);
                
                impact_bps = (permanent + temporary) * sigma * 10000;
                break;
            }
            
            case ImpactModel::POWER_LAW: {
                // Power law: Impact = a * (Q/V)^b
                double power = 0.6;  // Empirical estimate
                impact_bps = 100 * config_.permanent_impact_coefficient * 
                           std::pow(participation_rate, power);
                break;
            }
            
            case ImpactModel::BARRA: {
                // BARRA model: considers volatility and imbalance
                double vol_factor = market.volatility / 0.02;  // Normalized to 2% daily vol
                double imbalance_factor = 1.0 + std::abs(market.imbalance) * 0.5;
                
                impact_bps = config_.permanent_impact_coefficient * 
                           std::sqrt(participation_rate) * vol_factor * imbalance_factor * 10000;
                break;
            }
        }
        
        // Add momentum adjustment
        if ((is_buy && market.momentum > 0) || (!is_buy && market.momentum < 0)) {
            impact_bps *= 1.2;  // Trading with momentum costs more
        }
        
        // Decay temporary impact over time
        auto now = std::chrono::steady_clock::now().time_since_epoch();
        if (impact.last_trade_time.count() > 0) {
            auto time_diff = now - impact.last_trade_time;
            double decay_factor = std::exp(-config_.impact_decay_rate * 
                                          time_diff.count() / 1e9);  // Convert to seconds
            impact.temporary_impact *= decay_factor;
        }
        
        impact.last_trade_time = now;
        impact.cumulative_volume += std::abs(order_size);
        
        return price * impact_bps / 10000.0;
    }
    
    // Calculate slippage
    double calculateSlippage(const std::string& symbol, double order_size, 
                            double price, double adv, bool is_buy) {
        auto& market = market_states_[symbol];
        double slippage_bps = 0.0;
        
        switch (config_.slippage_model) {
            case SlippageModel::FIXED_BPS:
                slippage_bps = config_.base_slippage_bps;
                break;
                
            case SlippageModel::VOLATILITY_BASED:
                slippage_bps = config_.base_slippage_bps * 
                             (1.0 + config_.volatility_multiplier * market.volatility / 0.02);
                break;
                
            case SlippageModel::VOLUME_BASED: {
                double participation = std::abs(order_size) / (adv + 1.0);
                slippage_bps = config_.base_slippage_bps + 
                             config_.participation_penalty * participation * 100;
                break;
            }
            
            case SlippageModel::HYBRID: {
                // Combine multiple factors
                double vol_factor = 1.0 + config_.volatility_multiplier * market.volatility / 0.02;
                double participation = std::abs(order_size) / (adv + 1.0);
                double spread_factor = market.avg_spread_bps / 5.0;  // Normalized to 5bps
                
                slippage_bps = config_.base_slippage_bps * vol_factor * spread_factor +
                             config_.participation_penalty * participation * 100;
                
                // Add randomness
                slippage_bps *= (1.0 + normal_dist_(rng_) * 0.2);
                break;
            }
        }
        
        // Adjust for order flow imbalance
        if ((is_buy && market.imbalance > 0) || (!is_buy && market.imbalance < 0)) {
            slippage_bps *= (1.0 + std::abs(market.imbalance) * 0.3);
        }
        
        return price * slippage_bps / 10000.0;
    }
    
    // Calculate comprehensive transaction costs
    double calculateTransactionCosts(int quantity, double price) {
        double notional = quantity * price;
        
        // Commission
        double commission = std::max(config_.min_commission, 
                                    quantity * config_.commission_per_share);
        
        // SEC fee (on sales only)
        double sec_fee = notional / 1000000.0 * config_.sec_fee_per_million;
        
        // FINRA TAF
        double taf_fee = quantity * config_.taf_fee_per_share;
        
        return commission + sec_fee + taf_fee;
    }
    
    // Map to track latest prices
    std::unordered_map<std::string, double> latest_prices_;
    
public:
    explicit AdvancedExecutionHandler(const AdvancedExecutionConfig& config = {})
        : config_(config),
          rng_(std::chrono::steady_clock::now().time_since_epoch().count()),
          normal_dist_(0.0, 1.0),
          uniform_dist_(0.0, 1.0),
          latency_dist_(1.0 / 500.0)  // Average 500 microseconds
    {}
    
    void setDataHandler(IDataHandler* handler) {
        data_handler_ = handler;
    }
    
    // Main execution method
    void executeOrder(const OrderEvent& order) override {
        stats_.total_orders++;
        
        // Simulate rejection
        if (uniform_dist_(rng_) < config_.rejection_probability) {
            stats_.rejected_orders++;
            return;
        }
        
        // Get market data
        double bid = 0.0, ask = 0.0, mid_price = 0.0;
        double volume = 100000;  // Default
        
        if (data_handler_) {
            auto market_opt = data_handler_->getLatestBar(order.symbol);
            if (market_opt) {
                const auto& market = *market_opt;
                bid = market.bid;
                ask = market.ask;
                mid_price = (bid + ask) / 2.0;
                volume = market.volume;
                
                // Update market state
                updateMarketState(order.symbol, market);
                
                // Simulate order book if enabled
                simulateOrderBook(order.symbol, mid_price, ask - bid, volume);
            }
        }
        
        if (mid_price <= 0) {
            // No market data available
            stats_.rejected_orders++;
            return;
        }
        
        // Check risk limits
        auto& market_state = market_states_[order.symbol];
        double adv = 0.0;
        if (!market_state.recent_volumes.empty()) {
            adv = std::accumulate(market_state.recent_volumes.begin(),
                                 market_state.recent_volumes.end(), 0.0) /
                  market_state.recent_volumes.size();
        }
        
        if (adv > 0) {
            double order_pct_adv = order.quantity / adv;
            if (order_pct_adv > config_.max_order_size_pct_adv) {
                // Order too large relative to ADV
                stats_.rejected_orders++;
                return;
            }
        }
        
        // Simulate execution latency
        auto latency_us = static_cast<int>(
            config_.min_latency.count() +
            latency_dist_(rng_) * (config_.max_latency.count() - config_.min_latency.count())
        );
        auto execution_time = order.timestamp + std::chrono::microseconds(latency_us);
        
        bool is_buy = (order.direction == OrderEvent::Direction::BUY);
        
        // Determine base fill price
        double fill_price = 0.0;
        switch (order.order_type) {
            case OrderEvent::Type::MARKET:
                // Market orders cross the spread
                fill_price = is_buy ? ask : bid;
                break;
                
            case OrderEvent::Type::LIMIT:
                // Check if limit price is marketable
                if ((is_buy && order.price >= ask) || (!is_buy && order.price <= bid)) {
                    fill_price = order.price;  // Immediate execution at limit
                } else {
                    // Queue in order book (simplified: random fill probability)
                    if (uniform_dist_(rng_) > 0.7) {  // 70% fill rate for passive orders
                        stats_.rejected_orders++;
                        return;
                    }
                    fill_price = order.price;
                }
                break;
                
            case OrderEvent::Type::STOP:
            case OrderEvent::Type::STOP_LIMIT:
                // Simplified: execute at market when triggered
                fill_price = is_buy ? ask : bid;
                break;
        }
        
        // Check for dark pool execution
        if (config_.enable_dark_pool && uniform_dist_(rng_) < config_.dark_pool_probability) {
            // Dark pool execution at midpoint with improvement
            fill_price = mid_price;
            if (is_buy) {
                fill_price -= mid_price * config_.dark_pool_improvement_bps / 10000.0;
            } else {
                fill_price += mid_price * config_.dark_pool_improvement_bps / 10000.0;
            }
            stats_.dark_pool_fills++;
        }
        
        // Calculate and apply slippage
        double slippage = calculateSlippage(order.symbol, order.quantity, 
                                           fill_price, adv, is_buy);
        if (is_buy) {
            fill_price += slippage;
        } else {
            fill_price -= slippage;
        }
        stats_.total_slippage += std::abs(slippage) * order.quantity;
        
        // Calculate and apply market impact
        double impact = calculateMarketImpact(order.symbol, order.quantity,
                                             fill_price, adv, is_buy);
        if (is_buy) {
            fill_price += impact;
        } else {
            fill_price -= impact;
        }
        stats_.total_market_impact += std::abs(impact) * order.quantity;
        
        // Determine fill quantity (handle partial fills)
        int fill_quantity = order.quantity;
        if (config_.simulate_order_book && order_books_.find(order.symbol) != order_books_.end()) {
            const auto& book = order_books_[order.symbol];
            const auto& levels = is_buy ? book.asks : book.bids;
            
            // Check available liquidity at fill price
            double available_quantity = 0.0;
            for (const auto& level : levels) {
                if ((is_buy && level.price <= fill_price) || 
                    (!is_buy && level.price >= fill_price)) {
                    available_quantity += level.quantity;
                }
            }
            
            if (available_quantity < order.quantity) {
                // Partial fill
                fill_quantity = static_cast<int>(available_quantity);
                stats_.partial_fills++;
            }
        } else if (uniform_dist_(rng_) < config_.partial_fill_probability) {
            // Random partial fill
            fill_quantity = static_cast<int>(order.quantity * 
                                            (0.5 + uniform_dist_(rng_) * 0.5));
            stats_.partial_fills++;
        }
        
        // Calculate total transaction costs
        double costs = calculateTransactionCosts(fill_quantity, fill_price);
        stats_.total_commission += costs;
        stats_.total_fees += costs;
        
        // Track execution quality
        double spread_cost = is_buy ? (fill_price - mid_price) : (mid_price - fill_price);
        if (spread_cost > stats_.worst_slippage) {
            stats_.worst_slippage = spread_cost;
        }
        if (spread_cost < stats_.best_execution) {
            stats_.best_execution = spread_cost;
        }
        
        // Update statistics
        stats_.filled_orders++;
        stats_.total_notional += fill_quantity * fill_price;
        
        // Create and emit fill event
        FillEvent fill;
        fill.symbol = order.symbol;
        fill.quantity = fill_quantity;
        fill.fill_price = fill_price;
        fill.commission = costs;
        fill.slippage = slippage;
        fill.order_id = order.order_id;
        fill.exchange = config_.enable_dark_pool && 
                       (stats_.dark_pool_fills > stats_.filled_orders - stats_.dark_pool_fills) 
                       ? "DARK" : "NASDAQ";
        fill.is_buy = is_buy;
        fill.timestamp = execution_time;
        fill.sequence_id = order.sequence_id;
        
        if (fill.validate()) {
            emitFill(fill);
        }
        
        // Handle iceberg orders (remaining quantity)
        if (config_.enable_iceberg_orders && fill_quantity < order.quantity) {
            // Create new order for remaining quantity
            OrderEvent remaining_order = order;
            remaining_order.quantity = order.quantity - fill_quantity;
            remaining_order.order_id = order.order_id + "_ICEBERG";
            
            // Recursively execute remaining portion
            executeOrder(remaining_order);
        }
    }
    
    void initialize() override {
        stats_ = {};
        market_states_.clear();
        impact_states_.clear();
        order_books_.clear();
        latest_prices_.clear();
        fill_id_counter_ = 1;
    }
    
    void shutdown() override {
        // Clean up if needed
    }
    
    // Get execution statistics
    struct DetailedExecutionStats {
        uint64_t total_orders;
        uint64_t filled_orders;
        uint64_t rejected_orders;
        uint64_t partial_fills;
        uint64_t dark_pool_fills;
        double fill_rate;
        double avg_slippage_bps;
        double avg_market_impact_bps;
        double total_costs;
        double cost_per_share;
        double worst_slippage_bps;
        double best_execution_bps;
        double implementation_shortfall;
        double effective_spread;
    };
    
    DetailedExecutionStats getDetailedStats() const {
        DetailedExecutionStats detailed;
        
        detailed.total_orders = stats_.total_orders;
        detailed.filled_orders = stats_.filled_orders;
        detailed.rejected_orders = stats_.rejected_orders;
        detailed.partial_fills = stats_.partial_fills;
        detailed.dark_pool_fills = stats_.dark_pool_fills;
        
        detailed.fill_rate = stats_.total_orders > 0 ? 
            static_cast<double>(stats_.filled_orders) / stats_.total_orders : 0.0;
        
        if (stats_.total_notional > 0) {
            detailed.avg_slippage_bps = stats_.total_slippage / stats_.total_notional * 10000;
            detailed.avg_market_impact_bps = stats_.total_market_impact / stats_.total_notional * 10000;
            detailed.worst_slippage_bps = stats_.worst_slippage / (stats_.total_notional / stats_.filled_orders) * 10000;
            detailed.best_execution_bps = stats_.best_execution / (stats_.total_notional / stats_.filled_orders) * 10000;
        }
        
        detailed.total_costs = stats_.total_commission + stats_.total_fees;
        detailed.cost_per_share = stats_.filled_orders > 0 ? 
            detailed.total_costs / stats_.filled_orders : 0.0;
        
        // Implementation shortfall (simplified)
        detailed.implementation_shortfall = (stats_.total_slippage + stats_.total_market_impact + 
                                            detailed.total_costs) / stats_.total_notional * 10000;
        
        // Effective spread (average execution cost vs midpoint)
        detailed.effective_spread = (stats_.total_slippage + stats_.total_market_impact) / 
                                   stats_.total_notional * 10000;
        
        return detailed;
    }
    
    // Get market microstructure state for analysis
    MarketState getMarketState(const std::string& symbol) const {
        auto it = market_states_.find(symbol);
        return it != market_states_.end() ? it->second : MarketState{};
    }
    
    // Get impact state for a symbol
    ImpactState getImpactState(const std::string& symbol) const {
        auto it = impact_states_.find(symbol);
        return it != impact_states_.end() ? it->second : ImpactState{};
    }
    
    // Reset statistics
    void resetStats() {
        stats_ = {};
    }
    
    // Configuration setters
    void setImpactModel(ImpactModel model) { config_.impact_model = model; }
    void setSlippageModel(SlippageModel model) { config_.slippage_model = model; }
    void enableDarkPool(bool enable) { config_.enable_dark_pool = enable; }
    void enableOrderBook(bool enable) { config_.simulate_order_book = enable; }
};

} // namespace backtesting