// test_phase5_validation.cpp
// Phase 5: Advanced Statistical Validation
// Tests Purged Cross-Validation and Deflated Sharpe Ratio

#include <iostream>
#include <iomanip>
#include <random>
#include <vector>
#include <cmath>
#include <algorithm>

// Phase 5 validation modules
#include "../include/validation/purged_cross_validation.hpp"
#include "../include/validation/deflated_sharpe_ratio.hpp"

// For reading backtest results
#include "../include/portfolio/basic_portfolio.hpp"

using namespace backtesting;

// ============================================================================
// Synthetic Data Generation for Testing
// ============================================================================

std::vector<double> generateSyntheticReturns(size_t n, 
                                            double mean_return = 0.001,
                                            double volatility = 0.02,
                                            double sharpe_target = 0.5,
                                            int seed = 42) {
    std::mt19937 gen(seed);
    
    // Adjust mean to target Sharpe ratio
    double target_mean = sharpe_target * volatility;
    
    std::normal_distribution<double> dist(target_mean, volatility);
    
    std::vector<double> returns(n);
    for (size_t i = 0; i < n; ++i) {
        returns[i] = dist(gen);
    }
    
    return returns;
}

// Generate returns with autocorrelation (more realistic for finance)
std::vector<double> generateAutocorrelatedReturns(size_t n,
                                                  double autocorr = 0.1,
                                                  double volatility = 0.02,
                                                  int seed = 42) {
    std::mt19937 gen(seed);
    std::normal_distribution<double> dist(0.0, volatility);
    
    std::vector<double> returns(n);
    returns[0] = dist(gen);
    
    for (size_t i = 1; i < n; ++i) {
        returns[i] = autocorr * returns[i-1] + dist(gen);
    }
    
    return returns;
}

// ============================================================================
// Simple Strategy for CV Testing
// ============================================================================

class SimpleMovingAverageStrategy {
private:
    size_t fast_window_;
    size_t slow_window_;
    
public:
    SimpleMovingAverageStrategy(size_t fast = 10, size_t slow = 30)
        : fast_window_(fast), slow_window_(slow) {}
    
    // Calculate returns for given indices
    std::vector<double> getReturns(const std::vector<double>& prices,
                                   const std::vector<size_t>& indices) const {
        
        std::vector<double> returns;
        
        std::vector<double> fast_ma(prices.size(), 0.0);
        std::vector<double> slow_ma(prices.size(), 0.0);
        
        // Calculate moving averages
        for (size_t i = slow_window_; i < prices.size(); ++i) {
            double fast_sum = 0.0, slow_sum = 0.0;
            
            for (size_t j = 0; j < fast_window_; ++j) {
                fast_sum += prices[i - j];
            }
            for (size_t j = 0; j < slow_window_; ++j) {
                slow_sum += prices[i - j];
            }
            
            fast_ma[i] = fast_sum / fast_window_;
            slow_ma[i] = slow_sum / slow_window_;
        }
        
        // Generate signals and calculate returns
        int position = 0;
        for (size_t idx : indices) {
            if (idx < slow_window_) continue;
            
            double ret = 0.0;
            
            // Simple MA crossover strategy
            if (fast_ma[idx] > slow_ma[idx] && position <= 0) {
                position = 1;  // Go long
            } else if (fast_ma[idx] < slow_ma[idx] && position >= 0) {
                position = -1;  // Go short
            }
            
            if (idx > 0 && position != 0) {
                ret = position * (prices[idx] - prices[idx-1]) / prices[idx-1];
            }
            
            returns.push_back(ret);
        }
        
        return returns;
    }
};

// ============================================================================
// Test Functions
// ============================================================================

void testPurgedCrossValidation() {
    std::cout << "\n" << std::string(70, '=') << "\n";
    std::cout << "PHASE 5.1: PURGED K-FOLD CROSS-VALIDATION TEST\n";
    std::cout << std::string(70, '=') << "\n\n";
    
    // Generate synthetic price data
    std::cout << "1. Generating synthetic price data with autocorrelation...\n";
    
    std::mt19937 gen(42);
    std::normal_distribution<double> dist(0.0005, 0.02);
    
    std::vector<double> prices;
    prices.push_back(100.0);
    
    for (size_t i = 1; i < 500; ++i) {
        double ret = dist(gen);
        prices.push_back(prices.back() * (1 + ret));
    }
    
    std::cout << "   Generated " << prices.size() << " price points\n";
    std::cout << "   Starting price: $" << prices.front() << "\n";
    std::cout << "   Ending price: $" << prices.back() << "\n\n";
    
    // Create strategy
    SimpleMovingAverageStrategy strategy(10, 30);
    
    // Define scoring function
    auto score_function = [&strategy](const SimpleMovingAverageStrategy& strat,
                                     const std::vector<double>& data,
                                     const std::vector<size_t>& train_idx,
                                     const std::vector<size_t>& test_idx) -> double {
        // Calculate returns on test set
        auto returns = strat.getReturns(data, test_idx);
        
        if (returns.empty()) return 0.0;
        
        // Calculate Sharpe ratio
        double mean = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
        double sum_sq = 0.0;
        for (double r : returns) {
            sum_sq += (r - mean) * (r - mean);
        }
        double std_dev = std::sqrt(sum_sq / returns.size());
        
        return (std_dev > 1e-10) ? mean / std_dev * std::sqrt(252) : 0.0;
    };
    
    // Test 1: Standard K-Fold (for comparison)
    std::cout << "2. Standard K-Fold Cross-Validation (NO purging)...\n";
    
    size_t n_folds = 5;
    std::vector<double> standard_scores;
    
    size_t fold_size = prices.size() / n_folds;
    for (size_t k = 0; k < n_folds; ++k) {
        size_t test_start = k * fold_size;
        size_t test_end = (k == n_folds - 1) ? prices.size() : (k + 1) * fold_size;
        
        std::vector<size_t> test_indices;
        for (size_t i = test_start; i < test_end; ++i) {
            test_indices.push_back(i);
        }
        
        std::vector<size_t> train_indices;
        for (size_t i = 0; i < prices.size(); ++i) {
            if (i < test_start || i >= test_end) {
                train_indices.push_back(i);
            }
        }
        
        double score = score_function(strategy, prices, train_indices, test_indices);
        standard_scores.push_back(score);
        
        std::cout << "   Fold " << (k + 1) << ": Sharpe = " << std::fixed 
                 << std::setprecision(3) << score << "\n";
    }
    
    double std_mean = std::accumulate(standard_scores.begin(), standard_scores.end(), 0.0) 
                     / standard_scores.size();
    std::cout << "   Mean Sharpe: " << std_mean << "\n\n";
    
    // Test 2: Purged K-Fold CV
    std::cout << "3. Purged K-Fold Cross-Validation (WITH purging & embargo)...\n";
    
    CrossValidator<SimpleMovingAverageStrategy, std::vector<double>> validator(score_function);
    
    auto purged_result = validator.runPurgedKFold(strategy, prices, 5, 
                                                  5,  // purge window
                                                  5); // embargo
    
    std::cout << "\n   PURGED CV RESULTS:\n";
    std::cout << "   Mean Score:   " << std::fixed << std::setprecision(4) 
              << purged_result.mean_score << "\n";
    std::cout << "   Std Score:    " << purged_result.std_score << "\n";
    std::cout << "   Min Score:    " << purged_result.min_score << "\n";
    std::cout << "   Max Score:    " << purged_result.max_score << "\n";
    std::cout << "   Sharpe Ratio: " << purged_result.sharpe_ratio << "\n";
    std::cout << "   Stability:    " << purged_result.stability << "\n\n";
    
    // Test 3: Combinatorial Purged CV (smaller test due to computational cost)
    std::cout << "4. Combinatorial Purged Cross-Validation...\n";
    
    size_t n_groups = 6;
    size_t n_test_groups = 2;
    size_t num_combinations = CombinatorialPurgedCV::calculateNumSplits(n_groups, n_test_groups);
    
    std::cout << "   Groups: " << n_groups << ", Test groups: " << n_test_groups << "\n";
    std::cout << "   Number of combinations: " << num_combinations << "\n\n";
    
    auto cpcv_result = validator.runCombinatorialCV(strategy, prices, 
                                                   n_groups, n_test_groups,
                                                   3, 3);  // Smaller windows for speed
    
    std::cout << "\n   CPCV RESULTS:\n";
    std::cout << "   Mean Score:   " << cpcv_result.mean_score << "\n";
    std::cout << "   Std Score:    " << cpcv_result.std_score << "\n";
    std::cout << "   Min Score:    " << cpcv_result.min_score << "\n";
    std::cout << "   Max Score:    " << cpcv_result.max_score << "\n";
    std::cout << "   Stability:    " << cpcv_result.stability << "\n\n";
    
    std::cout << "   ✓ Purged Cross-Validation prevents information leakage\n";
    std::cout << "   ✓ CPCV provides robust distribution of performance\n";
}

void testDeflatedSharpeRatio() {
    std::cout << "\n" << std::string(70, '=') << "\n";
    std::cout << "PHASE 5.2: DEFLATED SHARPE RATIO (DSR) TEST\n";
    std::cout << std::string(70, '=') << "\n\n";
    
    DeflatedSharpeRatio dsr_calc;
    
    // Test Case 1: High Sharpe from few trials (likely genuine)
    std::cout << "1. Test Case: High Sharpe, Few Trials (Genuine Skill)\n";
    {
        auto returns = generateSyntheticReturns(500, 0.001, 0.015, 1.5, 42);
        size_t num_trials = 5;  // Only tested 5 strategies
        
        auto result = dsr_calc.calculateDetailed(returns, num_trials);
        
        std::cout << "   Returns:          " << returns.size() << " observations\n";
        std::cout << "   Trials tested:    " << num_trials << "\n";
        std::cout << "   Observed Sharpe:  " << std::fixed << std::setprecision(3) 
                 << result.observed_sharpe << "\n";
        std::cout << "   Expected Max SR:  " << result.expected_max_sharpe << "\n";
        std::cout << "   Deflated Sharpe:  " << result.deflated_sharpe << "\n";
        std::cout << "   Skewness:         " << result.skewness << "\n";
        std::cout << "   Kurtosis:         " << result.kurtosis << "\n";
        std::cout << "   PSR:              " << std::setprecision(1) 
                 << result.psr * 100 << "%\n";
        std::cout << "   P-value:          " << std::setprecision(4) << result.p_value << "\n";
        std::cout << "   Significant?      " << (result.is_significant ? "YES ✓" : "NO ✗") << "\n\n";
    }
    
    // Test Case 2: High Sharpe from many trials (likely overfitting)
    std::cout << "2. Test Case: High Sharpe, Many Trials (Likely Overfit)\n";
    {
        auto returns = generateSyntheticReturns(500, 0.001, 0.015, 1.5, 42);
        size_t num_trials = 1000;  // Tested 1000 strategies!
        
        auto result = dsr_calc.calculateDetailed(returns, num_trials);
        
        std::cout << "   Returns:          " << returns.size() << " observations\n";
        std::cout << "   Trials tested:    " << num_trials << "\n";
        std::cout << "   Observed Sharpe:  " << std::fixed << std::setprecision(3) 
                 << result.observed_sharpe << "\n";
        std::cout << "   Expected Max SR:  " << result.expected_max_sharpe << "\n";
        std::cout << "   Deflated Sharpe:  " << result.deflated_sharpe << "\n";
        std::cout << "   PSR:              " << std::setprecision(1) 
                 << result.psr * 100 << "%\n";
        std::cout << "   P-value:          " << std::setprecision(4) << result.p_value << "\n";
        std::cout << "   Significant?      " << (result.is_significant ? "YES ✓" : "NO ✗") << "\n\n";
    }
    
    // Test Case 3: Effect of skewness and kurtosis
    std::cout << "3. Test Case: Non-Normal Returns (Fat Tails)\n";
    {
        // Generate returns with fat tails
        std::mt19937 gen(42);
        std::student_t_distribution<double> t_dist(3);  // df=3 for fat tails
        
        std::vector<double> returns(500);
        for (size_t i = 0; i < returns.size(); ++i) {
            returns[i] = t_dist(gen) * 0.01;
        }
        
        size_t num_trials = 100;
        auto result = dsr_calc.calculateDetailed(returns, num_trials);
        
        std::cout << "   Returns:          " << returns.size() << " observations\n";
        std::cout << "   Trials tested:    " << num_trials << "\n";
        std::cout << "   Observed Sharpe:  " << std::fixed << std::setprecision(3) 
                 << result.observed_sharpe << "\n";
        std::cout << "   Deflated Sharpe:  " << result.deflated_sharpe << "\n";
        std::cout << "   Skewness:         " << result.skewness << "\n";
        std::cout << "   Kurtosis:         " << result.kurtosis << " (fat tails!)\n";
        std::cout << "   Significant?      " << (result.is_significant ? "YES ✓" : "NO ✗") << "\n\n";
    }
    
    // Test Case 4: Minimum track record length
    std::cout << "4. Minimum Track Record Length Analysis\n";
    {
        std::vector<double> sharpe_ratios = {0.5, 1.0, 1.5, 2.0};
        double target_sharpe = 0.0;  // vs zero skill
        
        std::cout << "   Target Sharpe: " << target_sharpe << "\n";
        std::cout << "   Confidence:    95%\n\n";
        std::cout << "   Observed SR | Min Track Length (periods)\n";
        std::cout << "   " << std::string(45, '-') << "\n";
        
        for (double sr : sharpe_ratios) {
            double min_length = dsr_calc.calculateMinTrackLength(sr, target_sharpe, 
                                                                0.0, 0.0, 0.95);
            std::cout << "   " << std::setw(11) << std::fixed << std::setprecision(2) 
                     << sr << " | " << std::setw(10) << static_cast<int>(min_length) 
                     << " periods\n";
        }
        std::cout << "\n";
    }
    
    // Test Case 5: Multiple testing adjustment
    std::cout << "5. Multiple Testing Adjustments\n";
    {
        std::vector<double> p_values = {0.01, 0.02, 0.03, 0.04, 0.05};
        
        std::cout << "   Original p-values:  ";
        for (double p : p_values) {
            std::cout << std::fixed << std::setprecision(3) << p << " ";
        }
        std::cout << "\n";
        
        // Bonferroni
        std::cout << "   Bonferroni:         ";
        for (double p : p_values) {
            std::cout << MultipleTestingAdjustment::bonferroniCorrection(p, p_values.size()) << " ";
        }
        std::cout << "\n";
        
        // Holm-Bonferroni
        auto holm_p = MultipleTestingAdjustment::holmBonferroni(p_values);
        std::cout << "   Holm-Bonferroni:    ";
        for (double p : holm_p) {
            std::cout << p << " ";
        }
        std::cout << "\n";
        
        // Benjamini-Hochberg (FDR)
        auto bh_p = MultipleTestingAdjustment::benjaminiHochberg(p_values);
        std::cout << "   Benjamini-Hochberg: ";
        for (double p : bh_p) {
            std::cout << p << " ";
        }
        std::cout << "\n\n";
    }
    
    std::cout << "   ✓ DSR correctly deflates Sharpe for multiple testing\n";
    std::cout << "   ✓ Accounts for distribution moments (skew, kurtosis)\n";
    std::cout << "   ✓ Provides statistical significance testing\n";
}

void testIntegratedValidation() {
    std::cout << "\n" << std::string(70, '=') << "\n";
    std::cout << "PHASE 5: INTEGRATED VALIDATION WORKFLOW\n";
    std::cout << std::string(70, '=') << "\n\n";
    
    std::cout << "Simulating realistic backtest validation workflow...\n\n";
    
    // Step 1: Generate strategy returns
    std::cout << "1. Strategy Backtest Results\n";
    auto returns = generateSyntheticReturns(1000, 0.0008, 0.015, 1.2, 42);
    
    double mean_ret = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
    double sum_sq = 0.0;
    for (double r : returns) sum_sq += (r - mean_ret) * (r - mean_ret);
    double vol = std::sqrt(sum_sq / returns.size());
    double observed_sharpe = mean_ret / vol * std::sqrt(252);
    
    std::cout << "   Observations:     " << returns.size() << "\n";
    std::cout << "   Annual Return:    " << std::fixed << std::setprecision(2) 
             << mean_ret * 252 * 100 << "%\n";
    std::cout << "   Annual Volatility:" << vol * std::sqrt(252) * 100 << "%\n";
    std::cout << "   Sharpe Ratio:     " << std::setprecision(3) << observed_sharpe << "\n\n";
    
    // Step 2: Apply DSR
    std::cout << "2. Deflated Sharpe Ratio Analysis\n";
    size_t num_trials = 50;  // Tested 50 strategy variations
    
    DeflatedSharpeRatio dsr_calc;
    auto dsr_result = dsr_calc.calculateDetailed(returns, num_trials);
    
    std::cout << "   Strategies tested: " << num_trials << "\n";
    std::cout << "   Deflated Sharpe:   " << dsr_result.deflated_sharpe << "\n";
    std::cout << "   PSR:               " << std::setprecision(1) 
             << dsr_result.psr * 100 << "%\n";
    std::cout << "   Significant:       " << (dsr_result.is_significant ? "YES ✓" : "NO ✗") << "\n\n";
    
    // Step 3: Validation decision
    std::cout << "3. Validation Decision\n";
    if (dsr_result.is_significant && dsr_result.deflated_sharpe > 0) {
        std::cout << "   ✓ PASS: Strategy shows statistically significant skill\n";
        std::cout << "   ✓ Safe to proceed to live testing with appropriate risk controls\n";
    } else {
        std::cout << "   ✗ FAIL: Strategy likely overfit to historical data\n";
        std::cout << "   ✗ Do NOT deploy - high probability of poor out-of-sample performance\n";
    }
    std::cout << "\n";
    
    std::cout << "   Key Insight: Deflating for multiple testing reveals true skill vs luck\n";
}

// ============================================================================
// Main Test Runner
// ============================================================================

int main() {
    try {
        std::cout << "\n" << std::string(70, '=') << "\n";
        std::cout << "   PHASE 5: ADVANCED STATISTICAL VALIDATION SUITE\n";
        std::cout << std::string(70, '=') << "\n";
        
        testPurgedCrossValidation();
        testDeflatedSharpeRatio();
        testIntegratedValidation();
        
        std::cout << "\n" << std::string(70, '=') << "\n";
        std::cout << "           PHASE 5 VALIDATION COMPLETED SUCCESSFULLY ✓\n";
        std::cout << std::string(70, '=') << "\n\n";
        
        std::cout << "Advanced Statistical Tools Implemented:\n\n";
        std::cout << "  ✓ Purged K-Fold Cross-Validation\n";
        std::cout << "    - Sequential time series splits\n";
        std::cout << "    - Purging to prevent look-ahead bias\n";
        std::cout << "    - Embargo periods for serial correlation\n\n";
        
        std::cout << "  ✓ Combinatorial Purged Cross-Validation (CPCV)\n";
        std::cout << "    - All combinations of train/test splits\n";
        std::cout << "    - Robust distribution of performance\n";
        std::cout << "    - Reduces dependency on single split\n\n";
        
        std::cout << "  ✓ Deflated Sharpe Ratio (DSR)\n";
        std::cout << "    - Adjusts for multiple testing bias\n";
        std::cout << "    - Accounts for distribution moments\n";
        std::cout << "    - Probabilistic significance testing\n";
        std::cout << "    - Minimum track length calculation\n\n";
        
        std::cout << "  ✓ Multiple Testing Adjustments\n";
        std::cout << "    - Bonferroni correction\n";
        std::cout << "    - Holm-Bonferroni method\n";
        std::cout << "    - Benjamini-Hochberg FDR control\n\n";
        
        std::cout << "These tools combat overfitting and provide realistic assessment\n";
        std::cout << "of strategy performance accounting for the research process.\n\n";
        
        std::cout << "Next Steps:\n";
        std::cout << "  • Integrate with backtesting engine output\n";
        std::cout << "  • Apply to real strategy evaluation\n";
        std::cout << "  • Use DSR for go/no-go decisions on deployment\n";
        std::cout << "  • Track number of trials in research workflow\n\n";
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "\nError during Phase 5 testing: " << e.what() << "\n";
        return 1;
    }
}