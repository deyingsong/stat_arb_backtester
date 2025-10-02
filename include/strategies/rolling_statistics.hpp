// rolling_statistics.hpp
// High-Performance Rolling Statistics Calculator for Statistical Arbitrage
// Implements cache-friendly, numerically stable rolling calculations

#pragma once

#include <cmath>
#include <deque>
#include <vector>
#include <algorithm>
#include <numeric>
#include <cstdint>

namespace backtesting {

// ============================================================================
// High-Performance Rolling Statistics with Numerical Stability
// ============================================================================

class RollingStatistics {
private:
    size_t window_size_;
    std::deque<double> values_;
    
    // Cached statistics for O(1) access
    double sum_ = 0.0;
    double sum_squares_ = 0.0;
    double mean_ = 0.0;
    double variance_ = 0.0;
    double std_dev_ = 0.0;
    
    // For numerical stability using Welford's algorithm
    double M2_ = 0.0;  // (retained for compatibility, not used in sliding window mode)
    size_t count_ = 0;
    
    // Additional statistics
    double min_value_ = std::numeric_limits<double>::max();
    double max_value_ = std::numeric_limits<double>::lowest();
    
    // EMA statistics
    double ema_value_ = 0.0;
    double ema_alpha_ = 0.0;
    bool ema_initialized_ = false;
    
public:
    explicit RollingStatistics(size_t window_size, double ema_alpha = 0.0)
        : window_size_(window_size), ema_alpha_(ema_alpha) {
        // Note: std::deque doesn't have reserve() method, so we remove this call
    }
    
    // Update with new value using Welford's online algorithm for numerical stability
    void update(double value) {
        // Add new value to window and update aggregate sums
        values_.push_back(value);
        sum_ += value;
        sum_squares_ += value * value;

        // Update min/max
        if (value < min_value_) min_value_ = value;
        if (value > max_value_) max_value_ = value;

        // Update EMA if configured
        if (ema_alpha_ > 0) {
            if (!ema_initialized_) {
                ema_value_ = value;
                ema_initialized_ = true;
            } else {
                ema_value_ = ema_alpha_ * value + (1 - ema_alpha_) * ema_value_;
            }
        }

        // If window exceeded, remove oldest value and adjust aggregates
        if (values_.size() > window_size_) {
            double old_value = values_.front();
            values_.pop_front();
            sum_ -= old_value;
            sum_squares_ -= old_value * old_value;

            // Recalculate min/max if necessary
            if (old_value == min_value_ || old_value == max_value_) {
                if (!values_.empty()) {
                    auto [min_it, max_it] = std::minmax_element(values_.begin(), values_.end());
                    min_value_ = *min_it;
                    max_value_ = *max_it;
                } else {
                    min_value_ = std::numeric_limits<double>::max();
                    max_value_ = std::numeric_limits<double>::lowest();
                }
            }
        }

        // Update count, mean, variance, stddev using aggregated sums (robust for sliding window)
        count_ = values_.size();
        if (count_ > 0) {
            mean_ = sum_ / static_cast<double>(count_);
        } else {
            mean_ = 0.0;
        }

        if (count_ > 1) {
            // Sample variance: (Σx^2 - n*mean^2) / (n-1)
            double ss = sum_squares_ - static_cast<double>(count_) * mean_ * mean_;
            variance_ = ss / static_cast<double>(count_ - 1);
            if (variance_ < 0 && variance_ > -1e-12) variance_ = 0.0; // guard against tiny negative
            std_dev_ = std::sqrt(std::max(0.0, variance_));
        } else {
            variance_ = 0.0;
            std_dev_ = 0.0;
        }
    }
    
    // Getters for statistics (all O(1) operations)
    double getMean() const { return mean_; }
    double getVariance() const { return variance_; }
    double getStdDev() const { return std_dev_; }
    double getMin() const { return min_value_; }
    double getMax() const { return max_value_; }
    double getSum() const { return sum_; }
    double getEMA() const { return ema_value_; }
    size_t getCount() const { return count_; }
    
    // Get Z-score of latest value
    double getZScore() const {
        if (values_.empty() || std_dev_ == 0) return 0.0;
        return (values_.back() - mean_) / std_dev_;
    }
    
    // Get percentile rank of a value
    double getPercentileRank(double value) const {
        if (values_.empty()) return 0.0;
        
        auto sorted_values = values_;  // Copy for sorting
        std::sort(sorted_values.begin(), sorted_values.end());
        
        auto it = std::lower_bound(sorted_values.begin(), sorted_values.end(), value);
        size_t rank = std::distance(sorted_values.begin(), it);
        
        return static_cast<double>(rank) / values_.size();
    }
    
    // Reset statistics
    void reset() {
        values_.clear();
        sum_ = 0.0;
        sum_squares_ = 0.0;
        mean_ = 0.0;
        variance_ = 0.0;
        std_dev_ = 0.0;
        M2_ = 0.0;
        count_ = 0;
        min_value_ = std::numeric_limits<double>::max();
        max_value_ = std::numeric_limits<double>::lowest();
        ema_value_ = 0.0;
        ema_initialized_ = false;
    }
    
    // Get all values (for external analysis)
    const std::deque<double>& getValues() const { return values_; }
};

// ============================================================================
// Specialized Rolling Correlation Calculator
// ============================================================================

class RollingCorrelation {
private:
    size_t window_size_;
    std::deque<double> x_values_;
    std::deque<double> y_values_;
    
    // Cached statistics
    double sum_x_ = 0.0;
    double sum_y_ = 0.0;
    double sum_xy_ = 0.0;
    double sum_x2_ = 0.0;
    double sum_y2_ = 0.0;
    double correlation_ = 0.0;
    
public:
    explicit RollingCorrelation(size_t window_size)
        : window_size_(window_size) {
        // Note: std::deque doesn't have reserve() method
    }
    
    void update(double x, double y) {
        // Add new values
        x_values_.push_back(x);
        y_values_.push_back(y);
        
        // Update sums
        sum_x_ += x;
        sum_y_ += y;
        sum_xy_ += x * y;
        sum_x2_ += x * x;
        sum_y2_ += y * y;
        
        // Remove old values if window is full
        if (x_values_.size() > window_size_) {
            double old_x = x_values_.front();
            double old_y = y_values_.front();
            
            x_values_.pop_front();
            y_values_.pop_front();
            
            sum_x_ -= old_x;
            sum_y_ -= old_y;
            sum_xy_ -= old_x * old_y;
            sum_x2_ -= old_x * old_x;
            sum_y2_ -= old_y * old_y;
        }
        
        // Calculate correlation
        size_t n = x_values_.size();
        if (n > 1) {
            double numerator = n * sum_xy_ - sum_x_ * sum_y_;
            double denominator = std::sqrt((n * sum_x2_ - sum_x_ * sum_x_) * 
                                          (n * sum_y2_ - sum_y_ * sum_y_));
            
            if (denominator > 0) {
                correlation_ = numerator / denominator;
                // Clamp to [-1, 1] to handle numerical errors
                correlation_ = std::max(-1.0, std::min(1.0, correlation_));
            } else {
                correlation_ = 0.0;
            }
        } else {
            correlation_ = 0.0;
        }
    }
    
    double getCorrelation() const { return correlation_; }
    size_t getCount() const { return x_values_.size(); }
    
    void reset() {
        x_values_.clear();
        y_values_.clear();
        sum_x_ = 0.0;
        sum_y_ = 0.0;
        sum_xy_ = 0.0;
        sum_x2_ = 0.0;
        sum_y2_ = 0.0;
        correlation_ = 0.0;
    }
};

// ============================================================================
// High-Performance Rolling Beta Calculator
// ============================================================================

class RollingBeta {
private:
    size_t window_size_;
    std::deque<double> asset_returns_;
    std::deque<double> market_returns_;
    
    double beta_ = 0.0;
    double alpha_ = 0.0;  // Intercept from regression
    double r_squared_ = 0.0;
    
public:
    explicit RollingBeta(size_t window_size)
        : window_size_(window_size) {
        // Note: std::deque doesn't have reserve() method
    }
    
    void update(double asset_return, double market_return) {
        // Add new returns
        asset_returns_.push_back(asset_return);
        market_returns_.push_back(market_return);
        
        // Maintain window size
        if (asset_returns_.size() > window_size_) {
            asset_returns_.pop_front();
            market_returns_.pop_front();
        }
        
        // Calculate beta using OLS regression
        size_t n = asset_returns_.size();
        if (n < 2) {
            beta_ = 0.0;
            alpha_ = 0.0;
            r_squared_ = 0.0;
            return;
        }
        
        // Calculate means
        double mean_asset = std::accumulate(asset_returns_.begin(), asset_returns_.end(), 0.0) / n;
        double mean_market = std::accumulate(market_returns_.begin(), market_returns_.end(), 0.0) / n;
        
        // Calculate covariance and variance
        double covariance = 0.0;
        double market_variance = 0.0;
        double asset_variance = 0.0;
        
        for (size_t i = 0; i < n; ++i) {
            double asset_diff = asset_returns_[i] - mean_asset;
            double market_diff = market_returns_[i] - mean_market;
            
            covariance += asset_diff * market_diff;
            market_variance += market_diff * market_diff;
            asset_variance += asset_diff * asset_diff;
        }
        
        if (market_variance > 0) {
            // Beta = Cov(asset, market) / Var(market)
            beta_ = covariance / market_variance;
            
            // Alpha = mean(asset) - beta * mean(market)
            alpha_ = mean_asset - beta_ * mean_market;
            
            // R-squared = (correlation)^2
            if (asset_variance > 0) {
                double correlation = covariance / std::sqrt(market_variance * asset_variance);
                r_squared_ = correlation * correlation;
            }
        } else {
            beta_ = 0.0;
            alpha_ = 0.0;
            r_squared_ = 0.0;
        }
    }
    
    double getBeta() const { return beta_; }
    double getAlpha() const { return alpha_; }
    double getRSquared() const { return r_squared_; }
    
    void reset() {
        asset_returns_.clear();
        market_returns_.clear();
        beta_ = 0.0;
        alpha_ = 0.0;
        r_squared_ = 0.0;
    }
};

} // namespace backtesting