// deflated_sharpe_ratio.hpp
// Phase 5.2: Deflated Sharpe Ratio (DSR) Calculation
// Adjusts Sharpe ratio for multiple testing bias
// Based on Bailey & López de Prado methodology

#pragma once

#include <vector>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <stdexcept>

namespace backtesting {

// ============================================================================
// Statistical Helper Functions
// ============================================================================

class StatisticalUtils {
public:
    // Calculate skewness of a distribution
    static double calculateSkewness(const std::vector<double>& returns) {
        if (returns.size() < 3) return 0.0;
        
        double mean = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
        
        double m2 = 0.0, m3 = 0.0;
        for (double r : returns) {
            double diff = r - mean;
            m2 += diff * diff;
            m3 += diff * diff * diff;
        }
        
        m2 /= returns.size();
        m3 /= returns.size();
        
        if (m2 < 1e-10) return 0.0;
        
        return m3 / std::pow(m2, 1.5);
    }
    
    // Calculate excess kurtosis of a distribution
    static double calculateKurtosis(const std::vector<double>& returns) {
        if (returns.size() < 4) return 0.0;
        
        double mean = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
        
        double m2 = 0.0, m4 = 0.0;
        for (double r : returns) {
            double diff = r - mean;
            double diff2 = diff * diff;
            m2 += diff2;
            m4 += diff2 * diff2;
        }
        
        m2 /= returns.size();
        m4 /= returns.size();
        
        if (m2 < 1e-10) return 0.0;
        
        return (m4 / (m2 * m2)) - 3.0;  // Excess kurtosis
    }
    
    // Standard normal CDF approximation
    static double normalCDF(double x) {
        // Using error function approximation
        return 0.5 * (1.0 + std::erf(x / std::sqrt(2.0)));
    }
    
    // Inverse normal CDF (quantile function)
    static double normalQuantile(double p) {
        if (p <= 0.0 || p >= 1.0) {
            throw std::invalid_argument("Probability must be in (0, 1)");
        }
        
        // Rational approximation for inverse normal CDF
        // Beasley-Springer-Moro algorithm
        const double a[] = {2.50662823884, -18.61500062529, 41.39119773534, -25.44106049637};
        const double b[] = {-8.47351093090, 23.08336743743, -21.06224101826, 3.13082909833};
        const double c[] = {0.3374754822726147, 0.9761690190917186, 0.1607979714918209,
                          0.0276438810333863, 0.0038405729373609, 0.0003951896511919,
                          0.0000321767881768, 0.0000002888167364, 0.0000003960315187};
        
        double x = p - 0.5;
        
        if (std::abs(x) < 0.42) {
            double r = x * x;
            double q = x * (((a[3] * r + a[2]) * r + a[1]) * r + a[0]) /
                          ((((b[3] * r + b[2]) * r + b[1]) * r + b[0]) * r + 1.0);
            return q;
        }
        
        double r = (x < 0) ? p : 1 - p;
        r = std::sqrt(-std::log(r));
        
        double q = (((((((c[8] * r + c[7]) * r + c[6]) * r + c[5]) * r + c[4]) * r + c[3]) * r + c[2]) * r + c[1]) * r + c[0];
        q = q / r;
        
        return (x < 0) ? -q : q;
    }
};

// ============================================================================
// Deflated Sharpe Ratio Calculator
// ============================================================================

class DeflatedSharpeRatio {
private:
    // Parameters for DSR calculation
    struct DSRParams {
        size_t num_trials;           // Number of independent trials/strategies tested
        size_t num_observations;     // Number of return observations
        double skewness;             // Skewness of returns
        double kurtosis;             // Excess kurtosis of returns
        double sharpe_ratio;         // Observed Sharpe ratio
        double variance_sharpe;      // Variance of Sharpe ratio estimator
    };
    
    // Calculate variance of Sharpe ratio estimator
    double calculateSharpeVariance(double sharpe, double skew, double kurt, size_t n) const {
        if (n <= 1) return 0.0;
        
        // Bailey & López de Prado formula
        // Var[SR] ≈ (1 + SR²/2 - SR*γ₁ + (3 - 2γ₁)*SR²/4) / (n - 1)
        // where γ₁ is skewness and γ₂ is excess kurtosis
        
        double sr2 = sharpe * sharpe;
        double term1 = 1.0;
        double term2 = sr2 / 2.0;
        double term3 = -sharpe * skew;
        double term4 = ((3.0 + kurt) - skew) * sr2 / 4.0;
        
        return (term1 + term2 + term3 + term4) / (n - 1.0);
    }
    
    // Calculate expected maximum Sharpe ratio under null hypothesis
    double calculateExpectedMaxSharpe(size_t num_trials, double var_sharpe) const {
        if (num_trials == 0 || var_sharpe <= 0.0) return 0.0;
        
        // Expected maximum of N standard normal variables
        // E[max(Z₁,...,Zₙ)] ≈ Φ⁻¹(1 - 1/(N+1))
        // where Φ⁻¹ is the inverse normal CDF
        
        double prob = 1.0 - 1.0 / (num_trials + 1.0);
        double z_max = StatisticalUtils::normalQuantile(prob);
        
        // Scale by standard deviation of Sharpe estimator
        return z_max * std::sqrt(var_sharpe);
    }
    
public:
    // Calculate Deflated Sharpe Ratio
    double calculate(const std::vector<double>& returns,
                    double observed_sharpe,
                    size_t num_trials,
                    double risk_free_rate = 0.0) const {
        
        if (returns.empty() || num_trials == 0) {
            throw std::invalid_argument("Invalid inputs for DSR calculation");
        }
        
        // Calculate distribution moments
        double skewness = StatisticalUtils::calculateSkewness(returns);
        double kurtosis = StatisticalUtils::calculateKurtosis(returns);
        
        // Calculate variance of Sharpe ratio estimator
        double var_sharpe = calculateSharpeVariance(observed_sharpe, skewness, 
                                                    kurtosis, returns.size());
        
        if (var_sharpe <= 0.0) return 0.0;
        
        // Expected maximum Sharpe under null (zero skill)
        double expected_max_sharpe = calculateExpectedMaxSharpe(num_trials, var_sharpe);
        
        // Deflated Sharpe Ratio
        // DSR = (SR - E[max SR₀]) / σ[SR]
        double std_sharpe = std::sqrt(var_sharpe);
        
        if (std_sharpe < 1e-10) return 0.0;
        
        double dsr = (observed_sharpe - expected_max_sharpe) / std_sharpe;
        
        return dsr;
    }
    
    // Calculate DSR with detailed breakdown
    struct DSRResult {
        double deflated_sharpe;
        double observed_sharpe;
        double expected_max_sharpe;
        double sharpe_std_error;
        double skewness;
        double kurtosis;
        double psr;  // Probabilistic Sharpe Ratio
        double p_value;
        bool is_significant;
    };
    
    DSRResult calculateDetailed(const std::vector<double>& returns,
                               size_t num_trials,
                               double risk_free_rate = 0.0,
                               double significance_level = 0.05) const {
        
        DSRResult result;
        
        if (returns.empty()) {
            result = {0, 0, 0, 0, 0, 0, 0, 0, false};
            return result;
        }
        
        // Calculate observed Sharpe ratio
        double mean_return = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
        
        double sum_sq_diff = 0.0;
        for (double r : returns) {
            double diff = r - mean_return;
            sum_sq_diff += diff * diff;
        }
        double std_dev = std::sqrt(sum_sq_diff / returns.size());
        
        result.observed_sharpe = (std_dev > 1e-10) ? 
                                (mean_return - risk_free_rate) / std_dev : 0.0;
        
        // Calculate moments
        result.skewness = StatisticalUtils::calculateSkewness(returns);
        result.kurtosis = StatisticalUtils::calculateKurtosis(returns);
        
        // Variance of Sharpe estimator
        double var_sharpe = calculateSharpeVariance(result.observed_sharpe, 
                                                    result.skewness,
                                                    result.kurtosis, 
                                                    returns.size());
        
        result.sharpe_std_error = std::sqrt(var_sharpe);
        
        // Expected maximum Sharpe under null
        result.expected_max_sharpe = calculateExpectedMaxSharpe(num_trials, var_sharpe);
        
        // Deflated Sharpe Ratio
        if (result.sharpe_std_error > 1e-10) {
            result.deflated_sharpe = (result.observed_sharpe - result.expected_max_sharpe) / 
                                    result.sharpe_std_error;
        } else {
            result.deflated_sharpe = 0.0;
        }
        
        // Probabilistic Sharpe Ratio (PSR)
        // PSR = Φ((SR - SR*) / σ[SR])
        // where SR* is a benchmark (often 0)
        double benchmark_sharpe = 0.0;
        if (result.sharpe_std_error > 1e-10) {
            double z_stat = (result.observed_sharpe - benchmark_sharpe) / result.sharpe_std_error;
            result.psr = StatisticalUtils::normalCDF(z_stat);
        } else {
            result.psr = 0.5;
        }
        
        // P-value for DSR (two-tailed test)
        result.p_value = 2.0 * (1.0 - StatisticalUtils::normalCDF(std::abs(result.deflated_sharpe)));
        result.is_significant = (result.p_value < significance_level) && (result.deflated_sharpe > 0);
        
        return result;
    }
    
    // Calculate minimum track record length for significance
    double calculateMinTrackLength(double sharpe_ratio,
                                   double target_sharpe,
                                   double skewness = 0.0,
                                   double kurtosis = 0.0,
                                   double confidence = 0.95) const {
        
        // Minimum track length formula
        // Based on Bailey & López de Prado
        
        if (sharpe_ratio <= target_sharpe) {
            return std::numeric_limits<double>::infinity();
        }
        
        double z_score = StatisticalUtils::normalQuantile(confidence);
        double sr_diff = sharpe_ratio - target_sharpe;
        
        if (sr_diff < 1e-10) {
            return std::numeric_limits<double>::infinity();
        }
        
        // Simplified formula
        double n = std::pow(z_score / sr_diff, 2.0) * 
                  (1.0 + 0.5 * sharpe_ratio * sharpe_ratio - 
                   sharpe_ratio * skewness + 
                   0.25 * (3.0 + kurtosis) * sharpe_ratio * sharpe_ratio);
        
        return std::max(1.0, n);
    }
};

// ============================================================================
// Multiple Testing Adjustment
// ============================================================================

class MultipleTestingAdjustment {
public:
    // Bonferroni correction
    static double bonferroniCorrection(double p_value, size_t num_tests) {
        return std::min(1.0, p_value * num_tests);
    }
    
    // Holm-Bonferroni correction
    static std::vector<double> holmBonferroni(const std::vector<double>& p_values) {
        std::vector<double> adjusted = p_values;
        std::vector<size_t> indices(p_values.size());
        std::iota(indices.begin(), indices.end(), 0);
        
        // Sort by p-value
        std::sort(indices.begin(), indices.end(),
                 [&p_values](size_t a, size_t b) { 
                     return p_values[a] < p_values[b]; 
                 });
        
        size_t n = p_values.size();
        for (size_t i = 0; i < n; ++i) {
            size_t idx = indices[i];
            adjusted[idx] = std::min(1.0, p_values[idx] * (n - i));
        }
        
        return adjusted;
    }
    
    // False Discovery Rate (Benjamini-Hochberg)
    static std::vector<double> benjaminiHochberg(const std::vector<double>& p_values) {
        std::vector<double> adjusted = p_values;
        std::vector<size_t> indices(p_values.size());
        std::iota(indices.begin(), indices.end(), 0);
        
        // Sort by p-value (descending)
        std::sort(indices.begin(), indices.end(),
                 [&p_values](size_t a, size_t b) { 
                     return p_values[a] > p_values[b]; 
                 });
        
        size_t n = p_values.size();
        double min_adjusted = 1.0;
        
        for (size_t i = 0; i < n; ++i) {
            size_t idx = indices[i];
            double rank = n - i;  // Descending order
            adjusted[idx] = std::min(min_adjusted, p_values[idx] * n / rank);
            min_adjusted = adjusted[idx];
        }
        
        return adjusted;
    }
};

} // namespace backtesting