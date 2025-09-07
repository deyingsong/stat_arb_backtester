// basic_portfolio.hpp
// Basic Portfolio Manager Implementation for Statistical Arbitrage Backtesting Engine
// Manages positions, cash, and generates orders from signals

#pragma once

#include <unordered_map>
#include <string>
#include <vector>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <atomic>
#include "../interfaces/portfolio.hpp"
#include "../core/event_types.hpp"
#include "../core/exceptions.hpp"
#include "../concurrent/disruptor_queue.hpp"

namespace backtesting {

// ============================================================================
// Basic Portfolio Manager with Position Tracking and Risk Management
// ============================================================================

class BasicPortfolio : public IPortfolio {
public:
    struct PortfolioConfig {
        double initial_capital;
        double max_position_size;  // Max 10% per position
        double commission_per_share;
        double min_commission;
        bool allow_shorting;
        double leverage;  // 1.0 = no leverage
        size_t max_positions;  // Maximum number of concurrent positions
        
        // Default constructor with explicit initialization
        PortfolioConfig() 
            : initial_capital(100000.0)
            , max_position_size(0.1)
            , commission_per_share(0.005)
            , min_commission(1.0)
            , allow_shorting(true)
            , leverage(1.0)
            , max_positions(50) {}
            
        // Static method to get default config
        static PortfolioConfig getDefault() {
            return PortfolioConfig();
        }
    };
    
    struct Position {
        int quantity = 0;
        double avg_price = 0.0;
        double unrealized_pnl = 0.0;
        double realized_pnl = 0.0;
        std::chrono::nanoseconds entry_time;
        std::chrono::nanoseconds last_update_time;
    };
    
    struct PortfolioSnapshot {
        double cash;
        double equity;
        double unrealized_pnl;
        double realized_pnl;
        double margin_used;
        size_t num_positions;
        std::chrono::nanoseconds timestamp;
    };
    
private:
    // Core portfolio state
    double cash_;
    double initial_capital_;
    std::unordered_map<std::string, Position> positions_;
    std::unordered_map<std::string, double> current_prices_;
    
    // Performance tracking
    double total_commission_ = 0.0;
    double total_realized_pnl_ = 0.0;
    double max_equity_ = 0.0;
    double max_drawdown_ = 0.0;
    std::vector<PortfolioSnapshot> equity_curve_;
    
    // Order management
    std::atomic<uint64_t> order_id_counter_{1};
    std::unordered_map<std::string, OrderEvent> pending_orders_;
    
    // Configuration
    PortfolioConfig config_;
    bool initialized_ = false;
    
    // Event queue reference (type-erased from interface)
    DisruptorQueue<EventVariant, 65536>* getEventQueue() {
        return static_cast<DisruptorQueue<EventVariant, 65536>*>(event_queue_);
    }
    
    // Generate unique order ID
    std::string generateOrderId() {
        uint64_t id = order_id_counter_.fetch_add(1, std::memory_order_relaxed);
        return "ORD_" + std::to_string(id);
    }
    
    // Calculate position size based on signal strength and risk limits
    int calculatePositionSize(const std::string& symbol, 
                             double signal_strength, 
                             bool is_long) {
        double price = current_prices_[symbol];
        if (price <= 0) return 0;
        
        // Calculate maximum position value based on equity and config
        double max_position_value = getEquity() * config_.max_position_size;
        
        // Scale by signal strength
        double target_value = max_position_value * std::abs(signal_strength);
        
        // Convert to shares
        int shares = static_cast<int>(target_value / price);
        
        // Apply direction
        if (!is_long) shares = -shares;
        
        // Check leverage constraints
        double margin_required = std::abs(shares * price) / config_.leverage;
        if (margin_required > cash_) {
            // Reduce position to fit available margin
            shares = static_cast<int>((cash_ * config_.leverage) / price);
            if (!is_long) shares = -shares;
        }
        
        return shares;
    }
    
    // Update unrealized P&L for all positions
    void updateUnrealizedPnL() {
        for (auto& [symbol, position] : positions_) {
            if (position.quantity == 0) continue;
            
            auto price_it = current_prices_.find(symbol);
            if (price_it == current_prices_.end()) continue;
            
            double current_price = price_it->second;
            double position_value = position.quantity * current_price;
            double cost_basis = position.quantity * position.avg_price;
            position.unrealized_pnl = position_value - cost_basis;
        }
    }
    
public:
    // Default constructor using default config
    BasicPortfolio() {
        auto default_config = PortfolioConfig::getDefault();
        config_ = default_config;
        cash_ = default_config.initial_capital;
        initial_capital_ = default_config.initial_capital;
    }
    
    // Constructor with custom config
    explicit BasicPortfolio(const PortfolioConfig& config)
        : config_(config), cash_(config.initial_capital), 
          initial_capital_(config.initial_capital) {}
    
    // IPortfolio interface implementation
    void initialize(double initial_capital) override {
        if (initialized_) return;
        
        if (initial_capital > 0) {
            initial_capital_ = initial_capital;
            cash_ = initial_capital;
            config_.initial_capital = initial_capital;
        }
        
        max_equity_ = initial_capital_;
        equity_curve_.reserve(100000);  // Pre-allocate for performance
        
        // Record initial snapshot
        equity_curve_.push_back({
            cash_, cash_, 0.0, 0.0, 0.0, 0,
            std::chrono::nanoseconds(0)
        });
        
        initialized_ = true;
    }
    
    void updateMarket(const MarketEvent& event) override {
        if (!initialized_) {
            throw BacktestException("Portfolio not initialized");
        }
        
        // Update current market prices
        current_prices_[event.symbol] = event.close;
        
        // Update unrealized P&L
        updateUnrealizedPnL();
        
        // Update equity tracking
        double current_equity = getEquity();
        if (current_equity > max_equity_) {
            max_equity_ = current_equity;
        }
        
        double drawdown = (max_equity_ - current_equity) / max_equity_;
        if (drawdown > max_drawdown_) {
            max_drawdown_ = drawdown;
        }
    }
    
    void updateSignal(const SignalEvent& event) override {
        if (!initialized_) {
            throw BacktestException("Portfolio not initialized");
        }
        
        // Check if we have price data for this symbol
        auto price_it = current_prices_.find(event.symbol);
        if (price_it == current_prices_.end()) {
            return;  // Skip if no price available
        }
        
        // Determine action based on signal direction
        OrderEvent order;
        order.symbol = event.symbol;
        order.order_id = generateOrderId();
        order.portfolio_id = "BASIC_PORTFOLIO";
        order.timestamp = event.timestamp;
        order.sequence_id = event.sequence_id;
        order.order_type = OrderEvent::Type::MARKET;
        order.tif = OrderEvent::TimeInForce::DAY;
        
        // Get current position
        auto pos_it = positions_.find(event.symbol);
        int current_position = (pos_it != positions_.end()) ? pos_it->second.quantity : 0;
        
        switch (event.direction) {
            case SignalEvent::Direction::LONG: {
                if (current_position >= 0) {
                    // Add to long position or initiate new long
                    int target_shares = calculatePositionSize(event.symbol, 
                                                             event.strength, true);
                    int shares_to_buy = target_shares - current_position;
                    
                    if (shares_to_buy > 0) {
                        order.direction = OrderEvent::Direction::BUY;
                        order.quantity = shares_to_buy;
                        order.price = price_it->second;  // For tracking
                    } else {
                        return;  // No action needed
                    }
                } else {
                    // Close short position first
                    order.direction = OrderEvent::Direction::BUY;
                    order.quantity = std::abs(current_position);
                    order.price = price_it->second;
                }
                break;
            }
            
            case SignalEvent::Direction::SHORT: {
                if (!config_.allow_shorting) return;
                
                if (current_position <= 0) {
                    // Add to short position or initiate new short
                    int target_shares = calculatePositionSize(event.symbol, 
                                                             event.strength, false);
                    int shares_to_sell = current_position - target_shares;
                    
                    if (shares_to_sell > 0) {
                        order.direction = OrderEvent::Direction::SELL;
                        order.quantity = shares_to_sell;
                        order.price = price_it->second;
                    } else {
                        return;  // No action needed
                    }
                } else {
                    // Close long position first
                    order.direction = OrderEvent::Direction::SELL;
                    order.quantity = current_position;
                    order.price = price_it->second;
                }
                break;
            }
            
            case SignalEvent::Direction::EXIT:
            case SignalEvent::Direction::FLAT: {
                if (current_position == 0) return;  // Already flat
                
                // Close position
                order.direction = (current_position > 0) ? 
                    OrderEvent::Direction::SELL : OrderEvent::Direction::BUY;
                order.quantity = std::abs(current_position);
                order.price = price_it->second;
                break;
            }
        }
        
        // Validate and emit order
        if (order.validate()) {
            pending_orders_[order.order_id] = order;
            emitOrder(order);
        }
    }
    
    void updateFill(const FillEvent& event) override {
        if (!initialized_) {
            throw BacktestException("Portfolio not initialized");
        }
        
        // Remove from pending orders
        pending_orders_.erase(event.order_id);
        
        // Update cash (including commission)
        double trade_value = event.quantity * event.fill_price;
        double commission = std::max(config_.min_commission, 
                                    event.quantity * config_.commission_per_share);
        
        if (event.is_buy) {
            cash_ -= (trade_value + commission);
        } else {
            cash_ += (trade_value - commission);
        }
        
        total_commission_ += commission;
        
        // Update position
        auto& position = positions_[event.symbol];
        int old_quantity = position.quantity;
        int new_quantity = old_quantity + (event.is_buy ? event.quantity : -event.quantity);
        
        // Calculate realized P&L if closing or reducing position
        if ((old_quantity > 0 && !event.is_buy) || (old_quantity < 0 && event.is_buy)) {
            int closed_quantity = std::min(std::abs(old_quantity), event.quantity);
            double realized = closed_quantity * (event.fill_price - position.avg_price);
            if (old_quantity < 0) realized = -realized;
            
            position.realized_pnl += realized;
            total_realized_pnl_ += realized;
        }
        
        // Update position state
        if (new_quantity == 0) {
            // Position closed
            positions_.erase(event.symbol);
        } else {
            // Update average price for remaining position
            if ((old_quantity >= 0 && event.is_buy) || (old_quantity <= 0 && !event.is_buy)) {
                // Adding to position
                double old_value = std::abs(old_quantity) * position.avg_price;
                double new_value = event.quantity * event.fill_price;
                position.avg_price = (old_value + new_value) / std::abs(new_quantity);
            }
            
            position.quantity = new_quantity;
            position.last_update_time = event.timestamp;
            
            if (old_quantity == 0) {
                position.entry_time = event.timestamp;
            }
        }
        
        // Record portfolio snapshot
        equity_curve_.push_back({
            cash_, getEquity(), 
            getUnrealizedPnL(), total_realized_pnl_,
            getMarginUsed(), positions_.size(),
            event.timestamp
        });
    }
    
    double getEquity() const override {
        double equity = cash_;
        for (const auto& [symbol, position] : positions_) {
            if (position.quantity == 0) continue;
            
            auto price_it = current_prices_.find(symbol);
            if (price_it != current_prices_.end()) {
                equity += position.quantity * price_it->second;
            }
        }
        return equity;
    }
    
    double getCash() const override {
        return cash_;
    }
    
    std::unordered_map<std::string, int> getPositions() const override {
        std::unordered_map<std::string, int> result;
        for (const auto& [symbol, position] : positions_) {
            if (position.quantity != 0) {
                result[symbol] = position.quantity;
            }
        }
        return result;
    }
    
    void shutdown() override {
        // Close all positions at current market prices
        for (const auto& [symbol, position] : positions_) {
            if (position.quantity == 0) continue;
            
            SignalEvent exit_signal;
            exit_signal.symbol = symbol;
            exit_signal.direction = SignalEvent::Direction::EXIT;
            exit_signal.strength = 1.0;
            exit_signal.strategy_id = "SHUTDOWN";
            exit_signal.timestamp = std::chrono::nanoseconds(0);
            
            updateSignal(exit_signal);
        }
        
        initialized_ = false;
    }
    
    void reset() override {
        cash_ = initial_capital_;
        positions_.clear();
        current_prices_.clear();
        pending_orders_.clear();
        total_commission_ = 0.0;
        total_realized_pnl_ = 0.0;
        max_equity_ = initial_capital_;
        max_drawdown_ = 0.0;
        equity_curve_.clear();
        order_id_counter_ = 1;
        
        if (initialized_) {
            equity_curve_.push_back({
                cash_, cash_, 0.0, 0.0, 0.0, 0,
                std::chrono::nanoseconds(0)
            });
        }
    }
    
    // Additional utility methods
    double getUnrealizedPnL() const {
        double total = 0.0;
        for (const auto& [_, position] : positions_) {
            total += position.unrealized_pnl;
        }
        return total;
    }
    
    double getMarginUsed() const {
        double margin = 0.0;
        for (const auto& [symbol, position] : positions_) {
            if (position.quantity == 0) continue;
            
            auto price_it = current_prices_.find(symbol);
            if (price_it != current_prices_.end()) {
                margin += std::abs(position.quantity * price_it->second) / config_.leverage;
            }
        }
        return margin;
    }
    
    double getMaxDrawdown() const { return max_drawdown_; }
    double getTotalCommission() const { return total_commission_; }
    double getTotalRealizedPnL() const { return total_realized_pnl_; }
    
    const std::vector<PortfolioSnapshot>& getEquityCurve() const {
        return equity_curve_;
    }
    
    Position getPosition(const std::string& symbol) const {
        auto it = positions_.find(symbol);
        return (it != positions_.end()) ? it->second : Position{};
    }
};

} // namespace backtesting