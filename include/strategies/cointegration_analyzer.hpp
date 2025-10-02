// cointegration_analyzer.hpp
// Cointegration Analysis for Statistical Arbitrage Pairs Trading
// Implements Augmented Dickey-Fuller test and half-life calculations

#pragma once

#include <vector>
#include <cmath>
#include <numeric>
#include <algorithm>
#include <iostream>

namespace backtesting {

// ============================================================================
// Cointegration Analyzer for Pairs Trading
// ============================================================================

class CointegrationAnalyzer {
private:
    // Augmented Dickey-Fuller test critical values
    // These are approximations for different significance levels
    struct ADFCriticalValues {
        static constexpr double SIGNIFICANCE_1_PERCENT = -3.43;   // 1% significance
        static constexpr double SIGNIFICANCE_5_PERCENT = -2.86;   // 5% significance  
        static constexpr double SIGNIFICANCE_10_PERCENT = -2.57;  // 10% significance
    };
    
    // Calculate ADF test statistic using OLS regression
    double calculateADF(const std::vector<double>& series, int max_lag = 1) {
        if (series.size() < 20) return 0.0;  // Need minimum data
        
        // Create differenced series: y_t - y_{t-1}
        std::vector<double> diff_series;
        diff_series.reserve(series.size() - 1);
        
        for (size_t i = 1; i < series.size(); ++i) {
            diff_series.push_back(series[i] - series[i-1]);
        }
        
        // Regression: Δy_t = α + β*y_{t-1} + Σ(γ_i * Δy_{t-i}) + ε_t
        // For simplicity, using lag-1 model: Δy_t = α + β*y_{t-1} + ε_t
        
        size_t n = diff_series.size();
        if (n < 2) return 0.0;
        
        // Calculate regression coefficients
        double sum_y_lag = 0.0;      // Σ(y_{t-1})
        double sum_diff = 0.0;       // Σ(Δy_t)
        double sum_y_lag_sq = 0.0;   // Σ(y_{t-1}^2)
        double sum_y_lag_diff = 0.0; // Σ(y_{t-1} * Δy_t)
        
        for (size_t i = 0; i < n; ++i) {
            double y_lag = series[i];  // y_{t-1} for diff_series[i]
            double diff = diff_series[i];
            
            sum_y_lag += y_lag;
            sum_diff += diff;
            sum_y_lag_sq += y_lag * y_lag;
            sum_y_lag_diff += y_lag * diff;
        }
        
        double mean_y_lag = sum_y_lag / n;
        double mean_diff = sum_diff / n;
        
        // Calculate beta coefficient (slope)
        double numerator = sum_y_lag_diff - n * mean_y_lag * mean_diff;
        double denominator = sum_y_lag_sq - n * mean_y_lag * mean_y_lag;
        
        if (std::abs(denominator) < 1e-10) return 0.0;  // Avoid division by zero
        
        double beta = numerator / denominator;
        double alpha = mean_diff - beta * mean_y_lag;  // Intercept
        
        // Calculate residual sum of squares for standard error
        double sse = 0.0;
        for (size_t i = 0; i < n; ++i) {
            double y_lag = series[i];
            double predicted = alpha + beta * y_lag;
            double residual = diff_series[i] - predicted;
            sse += residual * residual;
        }
        
        // Standard error of beta
        double se_beta = std::sqrt(sse / (n - 2) / denominator);
        
        // ADF test statistic (t-statistic for beta)
        if (se_beta > 1e-10) {
            return beta / se_beta;
        }
        
        return 0.0;
    }
    
    // MacKinnon approximate p-value for ADF test
    double calculatePValue(double adf_stat, size_t sample_size) {
        // Simplified p-value calculation
        // In practice, would use MacKinnon (1994) critical value tables
        
        if (adf_stat < ADFCriticalValues::SIGNIFICANCE_1_PERCENT) {
            return 0.01;
        } else if (adf_stat < ADFCriticalValues::SIGNIFICANCE_5_PERCENT) {
            return 0.05;
        } else if (adf_stat < ADFCriticalValues::SIGNIFICANCE_10_PERCENT) {
            return 0.10;
        } else {
            // Linear interpolation for p-value (simplified)
            double p = 0.10 + (adf_stat - ADFCriticalValues::SIGNIFICANCE_10_PERCENT) * 0.1;
            return std::min(1.0, std::max(0.0, p));
        }
    }
    
public:
    struct CointegrationResult {
        double hedge_ratio;        // Optimal hedge ratio from OLS
        double adf_statistic;      // ADF test statistic
        double p_value;            // Approximate p-value
        bool is_cointegrated;      // True if cointegrated at 5% level
        double half_life;          // Mean reversion half-life in periods
        double spread_mean;        // Mean of the spread
        double spread_std;         // Standard deviation of spread
        size_t sample_size;        // Number of observations used
    };
    
    // Test for cointegration between two price series
    CointegrationResult testCointegration(const std::vector<double>& prices1,
                                         const std::vector<double>& prices2,
                                         double significance_level = 0.05) {
        CointegrationResult result;
        result.hedge_ratio = 1.0;
        result.adf_statistic = 0.0;
        result.p_value = 1.0;
        result.is_cointegrated = false;
        result.half_life = 0.0;
        result.spread_mean = 0.0;
        result.spread_std = 0.0;
        result.sample_size = 0;
        
        // Validate input
        if (prices1.size() != prices2.size() || prices1.size() < 20) {
            return result;  // Need sufficient data
        }
        
        result.sample_size = prices1.size();
        
        // Step 1: Calculate hedge ratio using OLS regression
        // Model: prices1 = α + β * prices2 + ε
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
        
        if (variance2 > 1e-10) {
            result.hedge_ratio = covariance / variance2;
        } else {
            return result;  // Cannot calculate hedge ratio
        }

        // Debug: report hedge ratio
        std::cout << "[Coint] Hedge ratio: " << result.hedge_ratio << "\n";
        
        // Step 2: Calculate spread using hedge ratio
        std::vector<double> spread;
        spread.reserve(prices1.size());
        
        for (size_t i = 0; i < prices1.size(); ++i) {
            spread.push_back(prices1[i] - result.hedge_ratio * prices2[i]);
        }
        
        // Calculate spread statistics
        result.spread_mean = std::accumulate(spread.begin(), spread.end(), 0.0) / spread.size();
        
        double sum_sq_diff = 0.0;
        for (double s : spread) {
            double diff = s - result.spread_mean;
            sum_sq_diff += diff * diff;
        }
        result.spread_std = std::sqrt(sum_sq_diff / (spread.size() - 1));
        
        // Step 3: Run ADF test on spread
        result.adf_statistic = calculateADF(spread);
        result.p_value = calculatePValue(result.adf_statistic, spread.size());
    std::cout << "[Coint] ADF stat: " << result.adf_statistic << ", p-value: " << result.p_value << "\n";
        
        // Determine if cointegrated
        result.is_cointegrated = (result.p_value < significance_level);
        
        // Step 4: Calculate half-life of mean reversion if cointegrated
        if (result.is_cointegrated) {
            result.half_life = calculateHalfLife(spread);
            std::cout << "[Coint] Half-life: " << result.half_life << "\n";
        }
        
        return result;
    }
    
    // Calculate half-life of mean reversion using Ornstein-Uhlenbeck process
    double calculateHalfLife(const std::vector<double>& spread) {
        if (spread.size() < 2) return 0.0;
        
        // Model: y_t - y_{t-1} = λ * (μ - y_{t-1}) + ε
        // Rearranged: Δy_t = λ*μ - λ*y_{t-1} + ε
        // OLS regression: Δy_t = a + b*y_{t-1} + ε
        // Where b = -λ, so λ = -b
        // Half-life = -ln(2) / λ = ln(2) / |b|
        
        std::vector<double> spread_changes;
        std::vector<double> lagged_spread;
        
        for (size_t i = 1; i < spread.size(); ++i) {
            spread_changes.push_back(spread[i] - spread[i-1]);
            lagged_spread.push_back(spread[i-1]);
        }
        
        if (spread_changes.empty()) return 0.0;
        
        // OLS regression
        double mean_change = std::accumulate(spread_changes.begin(), spread_changes.end(), 0.0) 
                            / spread_changes.size();
        double mean_lag = std::accumulate(lagged_spread.begin(), lagged_spread.end(), 0.0) 
                         / lagged_spread.size();
        
        double numerator = 0.0;
        double denominator = 0.0;
        
        for (size_t i = 0; i < spread_changes.size(); ++i) {
            double x_diff = lagged_spread[i] - mean_lag;
            double y_diff = spread_changes[i] - mean_change;
            numerator += x_diff * y_diff;
            denominator += x_diff * x_diff;
        }
        
        if (std::abs(denominator) < 1e-10) return 0.0;
        
        double beta = numerator / denominator;
        std::cout << "[Coint::half] beta=" << beta << ", numerator=" << numerator << ", denominator=" << denominator << "\n";

        // Calculate half-life using continuous OU approximation: half-life = ln(2) / (-beta)
        // Accept any negative beta as mean-reverting (beta < 0). Guard tiny beta values.
        if (beta < 0.0) {
            double lambda = -beta;
            if (lambda > 1e-12) {
                return std::log(2.0) / lambda;
            }
        }

        return 0.0;  // No mean reversion detected
    }
    
    // Engle-Granger two-step cointegration test
    CointegrationResult engleGrangerTest(const std::vector<double>& prices1,
                                        const std::vector<double>& prices2) {
        // This is essentially what testCointegration does
        // Provided as an alias for clarity
        return testCointegration(prices1, prices2);
    }
    
    // Johansen cointegration test (simplified version)
    // Note: Full Johansen test requires matrix operations and is more complex
    struct JohansenResult {
        bool has_cointegration;
        int num_cointegrating_vectors;
        double trace_statistic;
        double max_eigenvalue_statistic;
    };
    
    JohansenResult johansenTest(const std::vector<std::vector<double>>& price_series) {
        JohansenResult result;
        result.has_cointegration = false;
        result.num_cointegrating_vectors = 0;
        result.trace_statistic = 0.0;
        result.max_eigenvalue_statistic = 0.0;
        
        // Simplified implementation
        // Full Johansen test requires:
        // 1. VAR model estimation
        // 2. Eigenvalue decomposition
        // 3. Trace and max eigenvalue statistics
        
        // For now, return placeholder
        // In production, would use Eigen library for matrix operations
        
        return result;
    }
    
    // Calculate optimal hedge ratio using rolling window
    std::vector<double> calculateRollingHedgeRatio(const std::vector<double>& prices1,
                                                   const std::vector<double>& prices2,
                                                   size_t window_size) {
        std::vector<double> hedge_ratios;
        
        if (prices1.size() != prices2.size() || prices1.size() < window_size) {
            return hedge_ratios;
        }
        
        for (size_t i = window_size; i <= prices1.size(); ++i) {
            // Extract window
            std::vector<double> window1(prices1.begin() + i - window_size, prices1.begin() + i);
            std::vector<double> window2(prices2.begin() + i - window_size, prices2.begin() + i);
            
            // Calculate hedge ratio for this window
            double mean1 = std::accumulate(window1.begin(), window1.end(), 0.0) / window_size;
            double mean2 = std::accumulate(window2.begin(), window2.end(), 0.0) / window_size;
            
            double covariance = 0.0;
            double variance2 = 0.0;
            
            for (size_t j = 0; j < window_size; ++j) {
                double diff1 = window1[j] - mean1;
                double diff2 = window2[j] - mean2;
                covariance += diff1 * diff2;
                variance2 += diff2 * diff2;
            }
            
            double hedge_ratio = (variance2 > 1e-10) ? (covariance / variance2) : 1.0;
            hedge_ratios.push_back(hedge_ratio);
        }
        
        return hedge_ratios;
    }
};

} // namespace backtesting