// simulated_execution_handler.hpp
// Simulated Execution Handler Implementation for Statistical Arbitrage Backtesting Engine
// Models realistic trade execution with slippage, market impact, and transaction costs

#pragma once

#include <random>
#include <chrono>
#include <string>
#include <unordered_map>
#include <atomic>
#include <cmath>
#include "../interfaces/execution_handler.hpp"
#include "../interfaces/data_handler.hpp"
#include "../core/event_types.hpp"
#include "../core/exceptions.hpp"
#include "../concurrent/disruptor_queue.hpp"

namespace backtesting {

// ============================================================================
// Simulated Execution Handler with Realistic Market Microstructure Modeling
// ============================================================================

class SimulatedExecutionHandler : public IExecutionHandler {
public:
    struct ExecutionConfig {
        // Commission structure
        double commission_per_share;
        double min_commission;
        double max_commission;  // As percentage of trade value
        
        // Slippage model parameters
        double base_slippage_bps;  // Base slippage in basis points
        double volatility_slippage_multiplier;  // Slippage increases with volatility
        double size_slippage_multiplier;  // Additional slippage per 1% ADV
        
        // Market impact model parameters
        double temporary_impact_bps;  // Temporary market impact
        double permanent_impact_bps;   // Permanent market impact
        double impact_decay_halflife_ms;  // Impact decay half-life
        
        // Execution constraints
        double max_participation_rate;  // Max 10% of volume
        bool enable_partial_fills;
        double fill_probability;  // Probability of complete fill
        
        // Latency simulation
        std::chrono::milliseconds min_latency;
        std::chrono::milliseconds max_latency;
        
        // Risk checks
        bool enable_risk_checks;
        double max_order_value;
        double max_order_quantity;
        
        // Default constructor
        ExecutionConfig()
            : commission_per_share(0.005), min_commission(1.0), max_commission(0.005),
              base_slippage_bps(5.0), volatility_slippage_multiplier(0.5), 
              size_slippage_multiplier(0.1), temporary_impact_bps(10.0),
              permanent_impact_bps(5.0), impact_decay_halflife_ms(5000),
              max_participation_rate(0.1), enable_partial_fills(true),
              fill_probability(0.95), min_latency(1), max_latency(10),
              enable_risk_checks(true), max_order_value(1000000.0),
              max_order_quantity(10000) {}
        
        // Static factory for default config
        static ExecutionConfig getDefault() {
            return ExecutionConfig();
        }
    };
    
    struct ExecutionStats {
        uint64_t total_orders = 0;
        uint64_t filled_orders = 0;
        uint64_t rejected_orders = 0;
        uint64_t partial_fills = 0;
        double total_commission = 0.0;
        double total_slippage = 0.0;
        double total_market_impact = 0.0;
        double avg_latency_ms = 0.0;
        double worst_slippage = 0.0;
        double best_execution = 0.0;
    };
    
private:
    // Configuration
    ExecutionConfig config_;
    
    // Market data reference for execution decisions
    IDataHandler* data_handler_ = nullptr;
    
    // Random number generation for slippage simulation
    std::mt19937 rng_;
    std::normal_distribution<> slippage_dist_;
    std::uniform_real_distribution<> fill_prob_dist_;
    std::uniform_int_distribution<> latency_dist_;
    
    // Execution tracking
    std::atomic<uint64_t> fill_id_counter_{1};
    ExecutionStats stats_;
    
    // Market impact tracking (symbol -> recent impact)
    struct MarketImpact {
        double temporary_impact = 0.0;
        double permanent_impact = 0.0;
        std::chrono::nanoseconds last_update;
    };
    std::unordered_map<std::string, MarketImpact> market_impacts_;
    
    // Volume tracking for participation rate
    std::unordered_map<std::string, double> daily_volumes_;
    std::unordered_map<std::string, double> executed_volumes_;
    
    // Event queue reference (type-erased from interface)
    DisruptorQueue<EventVariant, 65536>* getEventQueue() {
        return static_cast<DisruptorQueue<EventVariant, 65536>*>(event_queue_);
    }
    
    // Generate unique fill ID
    std::string generateFillId() {
        uint64_t id = fill_id_counter_.fetch_add(1, std::memory_order_relaxed);
        return "FILL_" + std::to_string(id);
    }
    
    // Calculate commission for a trade
    double calculateCommission(int quantity, double price) {
        double per_share = quantity * config_.commission_per_share;
        double percentage = quantity * price * config_.max_commission;
        double commission = std::max(config_.min_commission, 
                                    std::min(per_share, percentage));
        return commission;
    }
    
    // Calculate slippage based on market conditions
    double calculateSlippage(const std::string& symbol, int quantity, 
                            double price, bool is_buy) {
        // Base slippage
        double slippage_bps = config_.base_slippage_bps;
        
        // Add volatility-based slippage (simplified - in production use actual volatility)
        double volatility = 0.02;  // 2% daily volatility assumption
        slippage_bps += volatility * config_.volatility_slippage_multiplier * 100;
        
        // Add size-based slippage
        auto vol_it = daily_volumes_.find(symbol);
        if (vol_it != daily_volumes_.end() && vol_it->second > 0) {
            double participation = quantity / vol_it->second;
            slippage_bps += participation * config_.size_slippage_multiplier * 10000;
        }
        
        // Add random component
        double random_factor = slippage_dist_(rng_);
        slippage_bps *= (1.0 + random_factor * 0.5);  // ±50% randomness
        
        // Convert to price impact
        double slippage = price * slippage_bps / 10000.0;
        
        // Direction: adverse for buyer/seller
        return is_buy ? slippage : -slippage;
    }
    
    // Calculate market impact
    double calculateMarketImpact(const std::string& symbol, int quantity, 
                                double price, bool is_buy,
                                std::chrono::nanoseconds current_time) {
        auto& impact = market_impacts_[symbol];
        
        // Decay previous impact
        if (impact.last_update.count() > 0) {
            auto time_diff = current_time - impact.last_update;
            double decay_factor = std::exp(-0.693 * time_diff.count() / 
                                         (config_.impact_decay_halflife_ms * 1000000.0));
            impact.temporary_impact *= decay_factor;
        }
        
        // Calculate new impact
        auto vol_it = daily_volumes_.find(symbol);
        double participation = 0.01;  // Default 1%
        if (vol_it != daily_volumes_.end() && vol_it->second > 0) {
            participation = quantity / vol_it->second;
        }
        
        // Square-root market impact model (simplified Almgren-Chriss)
        double temp_impact = config_.temporary_impact_bps * std::sqrt(participation) / 10000.0;
        double perm_impact = config_.permanent_impact_bps * participation / 10000.0;
        
        // Accumulate impact
        impact.temporary_impact += temp_impact;
        impact.permanent_impact += perm_impact;
        impact.last_update = current_time;
        
        // Total impact on execution price
        double total_impact = (impact.temporary_impact + impact.permanent_impact) * price;
        
        // Direction: adverse for buyer/seller
        return is_buy ? total_impact : -total_impact;
    }
    
    // Simulate execution latency
    std::chrono::nanoseconds simulateLatency() {
        int latency_ms = latency_dist_(rng_);
        return std::chrono::nanoseconds(latency_ms * 1000000);
    }
    
public:
    // Default constructor using default config
    SimulatedExecutionHandler() {
        auto default_config = ExecutionConfig::getDefault();
        config_ = default_config;
        rng_.seed(std::chrono::steady_clock::now().time_since_epoch().count());
        slippage_dist_ = std::normal_distribution<>(0.0, 1.0);
        fill_prob_dist_ = std::uniform_real_distribution<>(0.0, 1.0);
        latency_dist_ = std::uniform_int_distribution<>(default_config.min_latency.count(), 
                                                       default_config.max_latency.count());
    }
    
    // Constructor with custom config
    explicit SimulatedExecutionHandler(const ExecutionConfig& config)
        : config_(config),
          rng_(std::chrono::steady_clock::now().time_since_epoch().count()),
          slippage_dist_(0.0, 1.0),
          fill_prob_dist_(0.0, 1.0),
          latency_dist_(config.min_latency.count(), config.max_latency.count()) {}
    
    // Set data handler for market data access
    void setDataHandler(IDataHandler* handler) {
        data_handler_ = handler;
    }
    
    // IExecutionHandler interface implementation
    void executeOrder(const OrderEvent& order) override {
        stats_.total_orders++;
        
        // Risk checks
        if (config_.enable_risk_checks) {
            if (order.quantity > config_.max_order_quantity ||
                order.quantity * order.price > config_.max_order_value) {
                stats_.rejected_orders++;
                return;  // Reject order
            }
        }
        
        // Get current market data
        double bid = order.price - 0.01;  // Default spread
        double ask = order.price + 0.01;
        double volume = 100000;  // Default daily volume
        
        if (data_handler_) {
            auto market_data = data_handler_->getLatestBar(order.symbol);
            if (market_data) {
                bid = market_data->bid;
                ask = market_data->ask;
                volume = market_data->volume;
                daily_volumes_[order.symbol] = volume;
            }
        }
        
        // Simulate execution latency
        auto latency = simulateLatency();
        auto execution_time = order.timestamp + latency;
        stats_.avg_latency_ms = (stats_.avg_latency_ms * stats_.filled_orders + 
                                latency.count() / 1000000.0) / (stats_.filled_orders + 1);
        
        // Determine fill price based on order type
        double fill_price = 0.0;
        bool is_buy = (order.direction == OrderEvent::Direction::BUY);
        
        switch (order.order_type) {
            case OrderEvent::Type::MARKET:
                // Market orders cross the spread
                fill_price = is_buy ? ask : bid;
                break;
                
            case OrderEvent::Type::LIMIT:
                // Limit orders may not fill
                if ((is_buy && order.price >= ask) || (!is_buy && order.price <= bid)) {
                    fill_price = order.price;
                } else if (fill_prob_dist_(rng_) > config_.fill_probability) {
                    stats_.rejected_orders++;
                    return;  // Order doesn't fill
                } else {
                    fill_price = order.price;
                }
                break;
                
            case OrderEvent::Type::STOP:
            case OrderEvent::Type::STOP_LIMIT:
                // Simplified: assume stop is triggered and fill at market
                fill_price = is_buy ? ask : bid;
                break;
        }
        
        // Apply slippage
        double slippage = calculateSlippage(order.symbol, order.quantity, fill_price, is_buy);
        fill_price += slippage;
        stats_.total_slippage += std::abs(slippage * order.quantity);
        
        if (std::abs(slippage) > stats_.worst_slippage) {
            stats_.worst_slippage = std::abs(slippage);
        }
        
        // Apply market impact
        double impact = calculateMarketImpact(order.symbol, order.quantity, 
                                             fill_price, is_buy, execution_time);
        fill_price += impact;
        stats_.total_market_impact += std::abs(impact * order.quantity);
        
        // Calculate commission
        double commission = calculateCommission(order.quantity, fill_price);
        stats_.total_commission += commission;
        
        // Determine fill quantity (partial fills)
        int fill_quantity = order.quantity;
        if (config_.enable_partial_fills && fill_prob_dist_(rng_) > 0.8) {
            // 20% chance of partial fill
            fill_quantity = static_cast<int>(order.quantity * 
                                            (0.5 + fill_prob_dist_(rng_) * 0.5));
            stats_.partial_fills++;
        }
        
        // Track executed volume for participation rate
        executed_volumes_[order.symbol] += fill_quantity;
        
        // Create fill event
        FillEvent fill;
        fill.symbol = order.symbol;
        fill.quantity = fill_quantity;
        fill.fill_price = fill_price;
        fill.commission = commission;
        fill.slippage = slippage;
        fill.order_id = order.order_id;
        fill.exchange = "SIMULATED";
        fill.is_buy = is_buy;
        fill.timestamp = execution_time;
        fill.sequence_id = order.sequence_id;
        
        // Validate and emit fill
        if (fill.validate()) {
            stats_.filled_orders++;
            emitFill(fill);
        }
    }
    
    void initialize() override {
        stats_ = ExecutionStats{};
        market_impacts_.clear();
        daily_volumes_.clear();
        executed_volumes_.clear();
        fill_id_counter_ = 1;
    }
    
    void shutdown() override {
        // Clean up if needed
    }
    
    // Get execution statistics
    const ExecutionStats& getStats() const {
        return stats_;
    }
    
    // Reset daily volumes (call at start of each trading day)
    void resetDailyVolumes() {
        executed_volumes_.clear();
    }
    
    // Update market conditions (optional - for dynamic adjustment)
    void updateMarketConditions(const std::string& symbol, double volatility, double volume) {
        // Store for more sophisticated slippage/impact models
        // This is a placeholder for future enhancements
    }
};

} // namespace backtesting