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
    double M2_ = 0.0;  // Sum of squared differences from mean
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
        values_.reserve(window_size);
    }
    
    // Update with new value using Welford's online algorithm for numerical stability
    void update(double value) {
        // Add new value
        values_.push_back(value);
        count_++;
        
        // Update running sums
        double delta = value - mean_;
        mean_ += delta / count_;
        double delta2 = value - mean_;
        M2_ += delta * delta2;
        
        // Update simple sums (for alternative calculations)
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
        
        // Remove old value if window is full
        if (values_.size() > window_size_) {
            double old_value = values_.front();
            values_.pop_front();
            
            // Update statistics after removal
            double old_delta = old_value - mean_;
            mean_ = (sum_ - old_value) / (count_ - 1);
            double new_delta = old_value - mean_;
            M2_ -= old_delta * new_delta;
            
            sum_ -= old_value;
            sum_squares_ -= old_value * old_value;
            count_--;
            
            // Recalculate min/max if necessary
            if (old_value == min_value_ || old_value == max_value_) {
                auto [min_it, max_it] = std::minmax_element(values_.begin(), values_.end());
                min_value_ = *min_it;
                max_value_ = *max_it;
            }
        }
        
        // Update variance and standard deviation
        if (count_ > 1) {
            variance_ = M2_ / (count_ - 1);  // Sample variance
            std_dev_ = std::sqrt(variance_);
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
        x_values_.reserve(window_size);
        y_values_.reserve(window_size);
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
        asset_returns_.reserve(window_size);
        market_returns_.reserve(window_size);
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

// ============================================================================
// Cointegration Analyzer for Pairs Trading
// ============================================================================

class CointegrationAnalyzer {
private:
    // Augmented Dickey-Fuller test critical values (5% significance)
    static constexpr double ADF_CRITICAL_VALUE = -2.86;  // Simplified
    
    // Calculate ADF test statistic (simplified version)
    double calculateADF(const std::vector<double>& series) {
        if (series.size() < 20) return 0.0;
        
        // Create differenced series
        std::vector<double> diff_series;
        for (size_t i = 1; i < series.size(); ++i) {
            diff_series.push_back(series[i] - series[i-1]);
        }
        
        // Regression: diff_y = alpha + beta * y_lag + error
        double sum_y_lag = 0.0;
        double sum_diff = 0.0;
        double sum_y_lag_sq = 0.0;
        double sum_y_lag_diff = 0.0;
        
        for (size_t i = 0; i < diff_series.size(); ++i) {
            double y_lag = series[i];  // Lagged value
            double diff = diff_series[i];
            
            sum_y_lag += y_lag;
            sum_diff += diff;
            sum_y_lag_sq += y_lag * y_lag;
            sum_y_lag_diff += y_lag * diff;
        }
        
        size_t n = diff_series.size();
        double mean_y_lag = sum_y_lag / n;
        double mean_diff = sum_diff / n;
        
        // Calculate beta (coefficient of lagged y)
        double numerator = sum_y_lag_diff - n * mean_y_lag * mean_diff;
        double denominator = sum_y_lag_sq - n * mean_y_lag * mean_y_lag;
        
        if (denominator == 0) return 0.0;
        
        double beta = numerator / denominator;
        
        // Calculate standard error (simplified)
        double sse = 0.0;
        for (size_t i = 0; i < diff_series.size(); ++i) {
            double predicted = mean_diff + beta * (series[i] - mean_y_lag);
            double error = diff_series[i] - predicted;
            sse += error * error;
        }
        
        double se = std::sqrt(sse / (n - 2) / denominator);
        
        // ADF test statistic
        return se > 0 ? beta / se : 0.0;
    }
    
public:
    struct CointegrationResult {
        double hedge_ratio;
        double adf_statistic;
        double p_value;  // Simplified p-value
        bool is_cointegrated;
        double half_life;  // Mean reversion half-life
    };
    
    // Test for cointegration between two price series
    CointegrationResult testCointegration(const std::vector<double>& prices1,
                                         const std::vector<double>& prices2) {
        CointegrationResult result;
        result.hedge_ratio = 1.0;
        result.adf_statistic = 0.0;
        result.p_value = 1.0;
        result.is_cointegrated = false;
        result.half_life = 0.0;
        
        if (prices1.size() != prices2.size() || prices1.size() < 20) {
            return result;
        }
        
        // Step 1: Calculate hedge ratio using OLS
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
            result.hedge_ratio = covariance / variance2;
        }
        
        // Step 2: Calculate spread and test for stationarity
        std::vector<double> spread;
        for (size_t i = 0; i < prices1.size(); ++i) {
            spread.push_back(prices1[i] - result.hedge_ratio * prices2[i]);
        }
        
        // Step 3: Run ADF test on spread
        result.adf_statistic = calculateADF(spread);
        
        // Simplified p-value calculation
        if (result.adf_statistic < ADF_CRITICAL_VALUE) {
            result.is_cointegrated = true;
            result.p_value = 0.01;  // Simplified
        } else {
            result.p_value = 0.5;  // Simplified
        }
        
        // Step 4: Calculate half-life of mean reversion
        if (result.is_cointegrated) {
            // Use OLS on spread changes
            std::vector<double> spread_changes;
            std::vector<double> lagged_spread;
            
            for (size_t i = 1; i < spread.size(); ++i) {
                spread_changes.push_back(spread[i] - spread[i-1]);
                lagged_spread.push_back(spread[i-1]);
            }
            
            // Simple regression
            double mean_change = std::accumulate(spread_changes.begin(), spread_changes.end(), 0.0) 
                                / spread_changes.size();
            double mean_lag = std::accumulate(lagged_spread.begin(), lagged_spread.end(), 0.0) 
                             / lagged_spread.size();
            
            double num = 0.0;
            double den = 0.0;
            
            for (size_t i = 0; i < spread_changes.size(); ++i) {
                num += (lagged_spread[i] - mean_lag) * (spread_changes[i] - mean_change);
                den += (lagged_spread[i] - mean_lag) * (lagged_spread[i] - mean_lag);
            }
            
            if (den > 0) {
                double lambda = num / den;
                if (lambda < 0 && lambda > -1) {
                    result.half_life = -std::log(2) / std::log(1 + lambda);
                }
            }
        }
        
        return result;
    }
};

} // namespace backtesting