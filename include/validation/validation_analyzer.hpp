// validation_analyzer.hpp
// Integration module for Phase 5 validation with backtesting engine
// Provides unified interface for analyzing backtest results

#pragma once

#include <vector>
#include <string>
#include <memory>
#include <fstream>
#include <sstream>
#include <iomanip>

#include "purged_cross_validation.hpp"
#include "deflated_sharpe_ratio.hpp"
#include "../portfolio/basic_portfolio.hpp"

namespace backtesting {

// ============================================================================
// Backtest Result Extractor
// ============================================================================

class BacktestResultExtractor {
public:
    // Extract returns from equity curve
    static std::vector<double> extractReturns(const std::vector<Portfolio::EquityPoint>& equity_curve) {
        std::vector<double> returns;
        
        if (equity_curve.size() < 2) return returns;
        
        returns.reserve(equity_curve.size() - 1);
        
        for (size_t i = 1; i < equity_curve.size(); ++i) {
            double prev = equity_curve[i-1].equity;
            double cur = equity_curve[i].equity;
            if (prev == 0.0) {
                returns.push_back(0.0);
            } else {
                returns.push_back((cur - prev) / prev);
            }
        }
        
        return returns;
    }

    // Templated overload: accept any equity-point-like type (e.g., PortfolioSnapshot)
    template<typename T>
    static std::vector<double> extractReturns(const std::vector<T>& equity_curve) {
        std::vector<double> returns;
        if (equity_curve.size() < 2) return returns;
        returns.reserve(equity_curve.size() - 1);

        for (size_t i = 1; i < equity_curve.size(); ++i) {
            double prev = static_cast<double>(equity_curve[i-1].equity);
            double cur = static_cast<double>(equity_curve[i].equity);
            if (prev == 0.0) {
                returns.push_back(0.0);
            } else {
                returns.push_back((cur - prev) / prev);
            }
        }

        return returns;
    }
    
    // Calculate basic statistics from returns
    struct ReturnStats {
        double mean;
        double std_dev;
        double sharpe_ratio;
        double sortino_ratio;
        double max_drawdown;
        size_t num_observations;
        
        // Annualized metrics
        double annual_return;
        double annual_volatility;
        double annual_sharpe;
    };
    
    static ReturnStats calculateStats(const std::vector<double>& returns, 
                                      double risk_free_rate = 0.0,
                                      size_t periods_per_year = 252) {
        ReturnStats stats = {0, 0, 0, 0, 0, 0, 0, 0, 0};
        
        if (returns.empty()) return stats;
        
        stats.num_observations = returns.size();
        
        // Mean return
        stats.mean = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
        
        // Standard deviation
        double sum_sq_diff = 0.0;
        for (double r : returns) {
            sum_sq_diff += (r - stats.mean) * (r - stats.mean);
        }
        stats.std_dev = std::sqrt(sum_sq_diff / returns.size());
        
        // Sharpe ratio
        if (stats.std_dev > 1e-10) {
            stats.sharpe_ratio = (stats.mean - risk_free_rate) / stats.std_dev;
        }
        
        // Sortino ratio (downside deviation)
        double downside_sum = 0.0;
        int downside_count = 0;
        for (double r : returns) {
            if (r < 0) {
                downside_sum += r * r;
                downside_count++;
            }
        }
        double downside_dev = (downside_count > 0) ? 
                             std::sqrt(downside_sum / downside_count) : stats.std_dev;
        
        if (downside_dev > 1e-10) {
            stats.sortino_ratio = (stats.mean - risk_free_rate) / downside_dev;
        }
        
        // Annualized metrics
        stats.annual_return = stats.mean * periods_per_year;
        stats.annual_volatility = stats.std_dev * std::sqrt(periods_per_year);
        stats.annual_sharpe = (stats.std_dev > 1e-10) ? 
                             stats.annual_return / stats.annual_volatility : 0.0;
        
        return stats;
    }
};

// ============================================================================
// Validation Report Generator
// ============================================================================

class ValidationReport {
private:
    std::ostringstream report_;
    
    void addSection(const std::string& title) {
        report_ << "\n" << std::string(70, '=') << "\n";
        report_ << title << "\n";
        report_ << std::string(70, '=') << "\n\n";
    }
    
    void addSubsection(const std::string& title) {
        report_ << "\n" << title << "\n";
        report_ << std::string(title.length(), '-') << "\n\n";
    }
    
public:
    void addBasicStats(const BacktestResultExtractor::ReturnStats& stats) {
        addSection("BASIC PERFORMANCE METRICS");
        
        report_ << std::fixed << std::setprecision(4);
        report_ << "Observations:       " << stats.num_observations << "\n";
        report_ << "Mean Return:        " << stats.mean * 100 << "%\n";
        report_ << "Volatility:         " << stats.std_dev * 100 << "%\n";
        report_ << "Sharpe Ratio:       " << stats.sharpe_ratio << "\n";
        report_ << "Sortino Ratio:      " << stats.sortino_ratio << "\n\n";
        
        report_ << "Annualized Metrics:\n";
        report_ << "  Return:           " << stats.annual_return * 100 << "%\n";
        report_ << "  Volatility:       " << stats.annual_volatility * 100 << "%\n";
        report_ << "  Sharpe Ratio:     " << stats.annual_sharpe << "\n";
    }
    
    void addDSRAnalysis(const DeflatedSharpeRatio::DSRResult& dsr, size_t num_trials) {
        addSection("DEFLATED SHARPE RATIO ANALYSIS");
        
        report_ << std::fixed << std::setprecision(4);
        report_ << "Number of Trials:   " << num_trials << "\n";
        report_ << "Observed Sharpe:    " << dsr.observed_sharpe << "\n";
        report_ << "Expected Max SR₀:   " << dsr.expected_max_sharpe << "\n";
        report_ << "Sharpe Std Error:   " << dsr.sharpe_std_error << "\n\n";
        
        report_ << "Deflated Sharpe:    " << dsr.deflated_sharpe << "\n";
        report_ << "Probabilistic SR:   " << std::setprecision(1) 
                << dsr.psr * 100 << "%\n";
        report_ << "P-value:            " << std::setprecision(4) << dsr.p_value << "\n";
        report_ << "Significant (α=5%): " << (dsr.is_significant ? "YES ✓" : "NO ✗") << "\n\n";
        
        report_ << "Distribution Moments:\n";
        report_ << "  Skewness:         " << dsr.skewness << "\n";
        report_ << "  Kurtosis:         " << dsr.kurtosis << "\n";
    }
    
    void addCVAnalysis(const CVResult& cv_result, const std::string& cv_type) {
        addSection(cv_type + " CROSS-VALIDATION RESULTS");
        
        report_ << std::fixed << std::setprecision(4);
        report_ << "Number of Folds:    " << cv_result.num_folds << "\n\n";
        
        report_ << "Performance Distribution:\n";
        report_ << "  Mean Score:       " << cv_result.mean_score << "\n";
        report_ << "  Std Deviation:    " << cv_result.std_score << "\n";
        report_ << "  Min Score:        " << cv_result.min_score << "\n";
        report_ << "  Max Score:        " << cv_result.max_score << "\n\n";
        
        report_ << "Stability Metrics:\n";
        report_ << "  Sharpe Ratio:     " << cv_result.sharpe_ratio << "\n";
        report_ << "  Stability Index:  " << cv_result.stability << "\n\n";
        
        report_ << "Individual Fold Scores:\n";
        for (size_t i = 0; i < cv_result.fold_scores.size(); ++i) {
            report_ << "  Fold " << std::setw(2) << (i + 1) << ": " 
                   << cv_result.fold_scores[i] << "\n";
        }
    }
    
    void addDeploymentDecision(bool deploy, const std::string& reason) {
        addSection("DEPLOYMENT DECISION");
        
        if (deploy) {
            report_ << "RECOMMENDATION: DEPLOY ✓\n\n";
            report_ << "The strategy shows statistically significant skill and has\n";
            report_ << "passed rigorous validation. Proceed to live testing with\n";
            report_ << "appropriate risk controls.\n\n";
        } else {
            report_ << "RECOMMENDATION: DO NOT DEPLOY ✗\n\n";
            report_ << "The strategy likely suffers from overfitting to historical data.\n";
            report_ << "There is high probability of poor out-of-sample performance.\n\n";
        }
        
        report_ << "Reason: " << reason << "\n";
    }
    
    std::string getReport() const {
        return report_.str();
    }
    
    void saveToFile(const std::string& filename) const {
        std::ofstream file(filename);
        if (file.is_open()) {
            file << getReport();
            file.close();
        }
    }
    
    void print() const {
        std::cout << getReport();
    }
};

// ============================================================================
// Complete Validation Analyzer
// ============================================================================

class ValidationAnalyzer {
private:
    DeflatedSharpeRatio dsr_calculator_;
    
public:
    struct ValidationConfig {
        size_t num_trials;           // Number of strategies tested
        bool run_purged_cv;          // Run purged cross-validation
        bool run_cpcv;               // Run combinatorial purged CV
        size_t cv_splits;            // Number of CV splits
        size_t purge_window;         // Purge window size
        size_t embargo_periods;      // Embargo period size
        double significance_level;   // For hypothesis testing (default 0.05)
        double dsr_threshold;        // Minimum acceptable DSR
        
        ValidationConfig() 
            : num_trials(1)
            , run_purged_cv(true)
            , run_cpcv(false)
            , cv_splits(5)
            , purge_window(5)
            , embargo_periods(5)
            , significance_level(0.05)
            , dsr_threshold(0.0) {}
    };
    
    struct ValidationResult {
        BacktestResultExtractor::ReturnStats basic_stats;
        DeflatedSharpeRatio::DSRResult dsr_result;
        CVResult purged_cv_result;
        CVResult cpcv_result;
        bool deploy_recommended;
        std::string decision_reason;
    };
    
    // Analyze backtest results with full validation suite
    ValidationResult analyze(const std::vector<double>& returns,
                           const ValidationConfig& config) {
        
        ValidationResult result;
        
        // 1. Calculate basic statistics
        result.basic_stats = BacktestResultExtractor::calculateStats(returns);
        
        // 2. Calculate Deflated Sharpe Ratio
        result.dsr_result = dsr_calculator_.calculateDetailed(
            returns, 
            config.num_trials,
            0.0,  // risk-free rate
            config.significance_level
        );
        
        // 3. Decision logic
        result.deploy_recommended = false;
        
        if (result.dsr_result.is_significant && 
            result.dsr_result.deflated_sharpe > config.dsr_threshold) {
            result.deploy_recommended = true;
            result.decision_reason = "Deflated Sharpe ratio is statistically significant "
                                    "and exceeds threshold after adjusting for " + 
                                    std::to_string(config.num_trials) + " trials";
        } else if (!result.dsr_result.is_significant) {
            result.decision_reason = "Deflated Sharpe ratio is not statistically significant "
                                    "(p-value = " + 
                                    std::to_string(result.dsr_result.p_value) + ")";
        } else {
            result.decision_reason = "Deflated Sharpe ratio below threshold after "
                                    "adjusting for multiple testing bias";
        }
        
        return result;
    }
    
    // Generate comprehensive report
    ValidationReport generateReport(const ValidationResult& result,
                                   const ValidationConfig& config) {
        
        ValidationReport report;
        
        report.addBasicStats(result.basic_stats);
        report.addDSRAnalysis(result.dsr_result, config.num_trials);
        
        if (config.run_purged_cv && result.purged_cv_result.num_folds > 0) {
            report.addCVAnalysis(result.purged_cv_result, "PURGED K-FOLD");
        }
        
        if (config.run_cpcv && result.cpcv_result.num_folds > 0) {
            report.addCVAnalysis(result.cpcv_result, "COMBINATORIAL PURGED");
        }
        
        report.addDeploymentDecision(result.deploy_recommended, result.decision_reason);
        
        return report;
    }
    
    // Convenience method: analyze from Portfolio equity curve
    ValidationResult analyzePortfolio(const BasicPortfolio& portfolio,
                                     const ValidationConfig& config) {
        
        auto equity_curve = portfolio.getEquityCurve();
        auto returns = BacktestResultExtractor::extractReturns(equity_curve);
        
        return analyze(returns, config);
    }
};

} // namespace backtesting