// simd_rolling_statistics.hpp
// SIMD-Optimized Rolling Statistics for High-Performance Backtesting
// Uses ARM NEON where available with scalar fallback

#pragma once

#include <deque>
#include <vector>
#include <cmath>
#include <algorithm>
#include "../math/simd_math.hpp"
#include "../core/branch_hints.hpp"

namespace backtesting {

// ============================================================================
// High-Performance SIMD-Optimized Rolling Statistics
// ============================================================================

class SIMDRollingStatistics {
private:
    size_t window_size_;
    std::deque<double> values_;
    
    // Cached statistics with cache-line alignment
    struct ALIGN_CACHE Stats {
        double sum = 0.0;
        double sum_squares = 0.0;
        double mean = 0.0;
        double variance = 0.0;
        double std_dev = 0.0;
        double min_value = std::numeric_limits<double>::max();
        double max_value = std::numeric_limits<double>::lowest();
    } stats_;
    
    size_t count_ = 0;
    
    // Buffered values for SIMD operations (aligned for NEON)
    std::vector<double> buffer_;
    bool buffer_dirty_ = true;
    
    // Update buffer from deque when needed
    FORCE_INLINE void update_buffer() {
        if (buffer_dirty_ && !values_.empty()) {
            buffer_.resize(values_.size());
            std::copy(values_.begin(), values_.end(), buffer_.begin());
            buffer_dirty_ = false;
        }
    }
    
    // Recalculate min/max by scanning
    NO_INLINE void recalculate_min_max() {
        if (values_.empty()) {
            stats_.min_value = std::numeric_limits<double>::max();
            stats_.max_value = std::numeric_limits<double>::lowest();
            return;
        }
        
        update_buffer();
        auto [min_it, max_it] = std::minmax_element(buffer_.begin(), buffer_.end());
        stats_.min_value = *min_it;
        stats_.max_value = *max_it;
    }
    
public:
    explicit SIMDRollingStatistics(size_t window_size)
        : window_size_(window_size) {
        buffer_.reserve(window_size);
    }
    
    // Hot path: update with new value using incremental algorithm
    HOT_FUNCTION
    void update(double value) {
        // Validate input (optimized for valid case)
        if (UNLIKELY(!FastValidation::is_finite(value))) {
            return;
        }
        
        // Add new value
        values_.push_back(value);
        stats_.sum += value;
        stats_.sum_squares += value * value;
        buffer_dirty_ = true;
        
        // Update min/max incrementally
        if (LIKELY(value < stats_.min_value)) stats_.min_value = value;
        if (LIKELY(value > stats_.max_value)) stats_.max_value = value;
        
        // Remove old value if window exceeded
        if (values_.size() > window_size_) {
            double old_value = values_.front();
            values_.pop_front();
            stats_.sum -= old_value;
            stats_.sum_squares -= old_value * old_value;
            
            // Check if we need to recalculate min/max
            if (UNLIKELY(old_value == stats_.min_value || old_value == stats_.max_value)) {
                recalculate_min_max();
            }
        }
        
        // Update count and derived statistics
        count_ = values_.size();
        if (LIKELY(count_ > 0)) {
            stats_.mean = stats_.sum / static_cast<double>(count_);
            
            if (LIKELY(count_ > 1)) {
                // Numerically stable variance calculation
                double variance_numerator = stats_.sum_squares - 
                    (stats_.sum * stats_.sum) / static_cast<double>(count_);
                stats_.variance = variance_numerator / static_cast<double>(count_ - 1);
                
                // Guard against numerical errors
                if (UNLIKELY(stats_.variance < 0.0 && stats_.variance > -1e-10)) {
                    stats_.variance = 0.0;
                }
                stats_.std_dev = std::sqrt(std::max(0.0, stats_.variance));
            } else {
                stats_.variance = 0.0;
                stats_.std_dev = 0.0;
            }
        }
    }
    
    // Hot path getters (all inlined and const)
    FORCE_INLINE double getMean() const { return stats_.mean; }
    FORCE_INLINE double getVariance() const { return stats_.variance; }
    FORCE_INLINE double getStdDev() const { return stats_.std_dev; }
    FORCE_INLINE double getMin() const { return stats_.min_value; }
    FORCE_INLINE double getMax() const { return stats_.max_value; }
    FORCE_INLINE double getSum() const { return stats_.sum; }
    FORCE_INLINE size_t getCount() const { return count_; }
    
    // Z-score of latest value (hot path)
    FORCE_INLINE double getZScore() const {
        if (UNLIKELY(values_.empty() || stats_.std_dev == 0.0)) {
            return 0.0;
        }
        return (values_.back() - stats_.mean) / stats_.std_dev;
    }
    
    // SIMD-optimized operations requiring buffer access
    double getPercentileRank(double value) const {
        if (UNLIKELY(values_.empty())) return 0.0;
        
        const_cast<SIMDRollingStatistics*>(this)->update_buffer();
        
        auto it = std::lower_bound(buffer_.begin(), buffer_.end(), value);
        size_t rank = std::distance(buffer_.begin(), it);
        
        return static_cast<double>(rank) / buffer_.size();
    }
    
    // Calculate correlation with another series using SIMD
    double correlation(const SIMDRollingStatistics& other) const {
        if (UNLIKELY(count_ != other.count_ || count_ < 2)) {
            return 0.0;
        }
        
        const_cast<SIMDRollingStatistics*>(this)->update_buffer();
        const_cast<SIMDRollingStatistics*>(&other)->update_buffer();
        
        return simd::StatisticalOps::correlation(
            buffer_.data(), 
            other.buffer_.data(), 
            count_
        );
    }
    
    // Get normalized values (z-scores) using SIMD
    std::vector<double> getNormalizedValues() const {
        if (UNLIKELY(values_.empty())) {
            return {};
        }
        
        const_cast<SIMDRollingStatistics*>(this)->update_buffer();
        
        std::vector<double> result(buffer_.size());
        simd::StatisticalOps::z_score_normalize(
            buffer_.data(), 
            result.data(), 
            buffer_.size()
        );
        
        return result;
    }
    
    void reset() {
        values_.clear();
        buffer_.clear();
        buffer_dirty_ = true;
        stats_ = Stats{};
        count_ = 0;
    }
    
    const std::deque<double>& getValues() const { return values_; }
};

// ============================================================================
// SIMD-Optimized Rolling Correlation
// ============================================================================

class SIMDRollingCorrelation {
private:
    size_t window_size_;
    std::deque<double> x_values_;
    std::deque<double> y_values_;
    
    // Buffered values for SIMD
    std::vector<double> x_buffer_;
    std::vector<double> y_buffer_;
    bool buffer_dirty_ = true;
    
    double correlation_ = 0.0;
    
    void update_buffer() {
        if (buffer_dirty_ && !x_values_.empty()) {
            x_buffer_.resize(x_values_.size());
            y_buffer_.resize(y_values_.size());
            std::copy(x_values_.begin(), x_values_.end(), x_buffer_.begin());
            std::copy(y_values_.begin(), y_values_.end(), y_buffer_.begin());
            buffer_dirty_ = false;
        }
    }
    
    void recalculate_correlation() {
        if (UNLIKELY(x_values_.size() < 2)) {
            correlation_ = 0.0;
            return;
        }
        
        update_buffer();
        correlation_ = simd::StatisticalOps::correlation(
            x_buffer_.data(),
            y_buffer_.data(),
            x_buffer_.size()
        );
    }
    
public:
    explicit SIMDRollingCorrelation(size_t window_size)
        : window_size_(window_size) {
        x_buffer_.reserve(window_size);
        y_buffer_.reserve(window_size);
    }
    
    HOT_FUNCTION
    void update(double x, double y) {
        if (UNLIKELY(!FastValidation::is_finite(x) || !FastValidation::is_finite(y))) {
            return;
        }
        
        x_values_.push_back(x);
        y_values_.push_back(y);
        buffer_dirty_ = true;
        
        if (x_values_.size() > window_size_) {
            x_values_.pop_front();
            y_values_.pop_front();
        }
        
        // Recalculate correlation (could be optimized with incremental algorithm)
        recalculate_correlation();
    }
    
    FORCE_INLINE double getCorrelation() const { return correlation_; }
    FORCE_INLINE size_t getCount() const { return x_values_.size(); }
    
    void reset() {
        x_values_.clear();
        y_values_.clear();
        x_buffer_.clear();
        y_buffer_.clear();
        buffer_dirty_ = true;
        correlation_ = 0.0;
    }
};

// ============================================================================
// SIMD-Optimized Rolling Beta
// ============================================================================

class SIMDRollingBeta {
private:
    size_t window_size_;
    std::deque<double> asset_returns_;
    std::deque<double> market_returns_;
    
    std::vector<double> asset_buffer_;
    std::vector<double> market_buffer_;
    bool buffer_dirty_ = true;
    
    double beta_ = 0.0;
    double alpha_ = 0.0;
    double r_squared_ = 0.0;
    
    void update_buffer() {
        if (buffer_dirty_ && !asset_returns_.empty()) {
            asset_buffer_.resize(asset_returns_.size());
            market_buffer_.resize(market_returns_.size());
            std::copy(asset_returns_.begin(), asset_returns_.end(), asset_buffer_.begin());
            std::copy(market_returns_.begin(), market_returns_.end(), market_buffer_.begin());
            buffer_dirty_ = false;
        }
    }
    
    void recalculate_regression() {
        size_t n = asset_returns_.size();
        if (UNLIKELY(n < 2)) {
            beta_ = 0.0;
            alpha_ = 0.0;
            r_squared_ = 0.0;
            return;
        }
        
        update_buffer();
        
        // Calculate means using SIMD
        double mean_asset = simd::VectorOps::mean(asset_buffer_.data(), n);
        double mean_market = simd::VectorOps::mean(market_buffer_.data(), n);
        
        // Calculate covariance and variances
        double covariance = 0.0;
        double market_variance = 0.0;
        double asset_variance = 0.0;
        
        for (size_t i = 0; i < n; ++i) {
            double asset_diff = asset_buffer_[i] - mean_asset;
            double market_diff = market_buffer_[i] - mean_market;
            
            covariance += asset_diff * market_diff;
            market_variance += market_diff * market_diff;
            asset_variance += asset_diff * asset_diff;
        }
        
        if (LIKELY(market_variance > 1e-10)) {
            beta_ = covariance / market_variance;
            alpha_ = mean_asset - beta_ * mean_market;
            
            if (asset_variance > 1e-10) {
                double correlation = covariance / std::sqrt(market_variance * asset_variance);
                r_squared_ = correlation * correlation;
            }
        }
    }
    
public:
    explicit SIMDRollingBeta(size_t window_size)
        : window_size_(window_size) {
        asset_buffer_.reserve(window_size);
        market_buffer_.reserve(window_size);
    }
    
    HOT_FUNCTION
    void update(double asset_return, double market_return) {
        if (UNLIKELY(!FastValidation::is_finite(asset_return) || 
                     !FastValidation::is_finite(market_return))) {
            return;
        }
        
        asset_returns_.push_back(asset_return);
        market_returns_.push_back(market_return);
        buffer_dirty_ = true;
        
        if (asset_returns_.size() > window_size_) {
            asset_returns_.pop_front();
            market_returns_.pop_front();
        }
        
        recalculate_regression();
    }
    
    FORCE_INLINE double getBeta() const { return beta_; }
    FORCE_INLINE double getAlpha() const { return alpha_; }
    FORCE_INLINE double getRSquared() const { return r_squared_; }
    
    void reset() {
        asset_returns_.clear();
        market_returns_.clear();
        asset_buffer_.clear();
        market_buffer_.clear();
        buffer_dirty_ = true;
        beta_ = 0.0;
        alpha_ = 0.0;
        r_squared_ = 0.0;
    }
};

} // namespace backtesting