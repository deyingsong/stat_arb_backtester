// purged_cross_validation.hpp
// Phase 5.1: Purged K-Fold Cross-Validation for Time Series
// Implements Purged K-Fold CV and Combinatorial Purged CV (CPCV)
// Prevents information leakage in time series backtesting

#pragma once

#include <vector>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <memory>
#include <functional>
#include <iostream>
#include "../core/branch_hints.hpp"

namespace backtesting {

// ============================================================================
// Data Structures for Cross-Validation
// ============================================================================

struct TimeSeriesSplit {
    std::vector<size_t> train_indices;
    std::vector<size_t> test_indices;
    
    size_t train_size() const { return train_indices.size(); }
    size_t test_size() const { return test_indices.size(); }
};

struct CVResult {
    double mean_score;
    double std_score;
    double min_score;
    double max_score;
    std::vector<double> fold_scores;
    size_t num_folds;
    
    // Additional statistics
    double sharpe_ratio;
    double information_ratio;
    double stability;  // Consistency across folds
};

// ============================================================================
// Purged K-Fold Cross-Validation
// ============================================================================

class PurgedKFoldCV {
private:
    size_t n_splits_;
    size_t purge_window_;  // Number of periods to purge
    size_t embargo_periods_;  // Embargo after test set
    
    // Calculate which indices to purge based on test set
    std::vector<size_t> getPurgeIndices(const std::vector<size_t>& test_indices,
                                       size_t total_samples) const {
        if (test_indices.empty()) return {};
        
        std::vector<size_t> purge_indices;
        
        // Find min and max test indices
        size_t min_test = *std::min_element(test_indices.begin(), test_indices.end());
        size_t max_test = *std::max_element(test_indices.begin(), test_indices.end());
        
        // Purge before test set (to prevent look-ahead bias)
        size_t purge_start = (min_test > purge_window_) ? 
                            min_test - purge_window_ : 0;
        for (size_t i = purge_start; i < min_test; ++i) {
            purge_indices.push_back(i);
        }
        
        // Embargo after test set (to prevent serial correlation)
        size_t embargo_end = std::min(max_test + embargo_periods_ + 1, total_samples);
        for (size_t i = max_test + 1; i < embargo_end; ++i) {
            purge_indices.push_back(i);
        }
        
        return purge_indices;
    }
    
    // Remove purged indices from training set
    std::vector<size_t> applyPurge(const std::vector<size_t>& train_indices,
                                  const std::vector<size_t>& purge_indices) const {
        std::vector<size_t> purged_train;
        purged_train.reserve(train_indices.size());
        
        for (size_t idx : train_indices) {
            if (std::find(purge_indices.begin(), purge_indices.end(), idx) 
                == purge_indices.end()) {
                purged_train.push_back(idx);
            }
        }
        
        return purged_train;
    }
    
public:
    // Public wrappers to allow other classes in header to reuse purge logic
    std::vector<size_t> publicGetPurgeIndices(const std::vector<size_t>& test_indices,
                                               size_t total_samples) const {
        return getPurgeIndices(test_indices, total_samples);
    }

    std::vector<size_t> publicApplyPurge(const std::vector<size_t>& train_indices,
                                         const std::vector<size_t>& purge_indices) const {
        return applyPurge(train_indices, purge_indices);
    }
    
public:
    PurgedKFoldCV(size_t n_splits, 
                  size_t purge_window = 5,
                  size_t embargo_periods = 5)
        : n_splits_(n_splits)
        , purge_window_(purge_window)
        , embargo_periods_(embargo_periods) {
        
        if (n_splits < 2) {
            throw std::invalid_argument("n_splits must be at least 2");
        }
    }
    
    // Generate all purged splits
    std::vector<TimeSeriesSplit> split(size_t n_samples) const {
        std::vector<TimeSeriesSplit> splits;
        splits.reserve(n_splits_);
        
        size_t fold_size = n_samples / n_splits_;
        
        for (size_t k = 0; k < n_splits_; ++k) {
            TimeSeriesSplit split;
            
            // Define test set for this fold
            size_t test_start = k * fold_size;
            size_t test_end = (k == n_splits_ - 1) ? n_samples : (k + 1) * fold_size;
            
            split.test_indices.reserve(test_end - test_start);
            for (size_t i = test_start; i < test_end; ++i) {
                split.test_indices.push_back(i);
            }
            
            // Define training set (all except test)
            std::vector<size_t> train_indices;
            train_indices.reserve(n_samples - split.test_indices.size());
            
            for (size_t i = 0; i < n_samples; ++i) {
                if (i < test_start || i >= test_end) {
                    train_indices.push_back(i);
                }
            }
            
            // Apply purging and embargo
            auto purge_indices = getPurgeIndices(split.test_indices, n_samples);
            split.train_indices = applyPurge(train_indices, purge_indices);
            
            splits.push_back(std::move(split));
        }
        
        return splits;
    }
    
    // Validate configuration
    bool validateConfig(size_t n_samples) const {
        size_t min_fold_size = n_samples / n_splits_;
        size_t required_buffer = purge_window_ + embargo_periods_;
        
        return (min_fold_size > required_buffer * 2);
    }
};

// ============================================================================
// Combinatorial Purged Cross-Validation (CPCV)
// ============================================================================

class CombinatorialPurgedCV {
private:
    size_t n_test_groups_;
    size_t purge_window_;
    size_t embargo_periods_;
    
    // Generate all combinations of k items from n
    void generateCombinations(std::vector<std::vector<size_t>>& result,
                            std::vector<size_t>& current,
                            size_t start, size_t n, size_t k) const {
        if (current.size() == k) {
            result.push_back(current);
            return;
        }
        
        for (size_t i = start; i <= n - (k - current.size()); ++i) {
            current.push_back(i);
            generateCombinations(result, current, i + 1, n, k);
            current.pop_back();
        }
    }
    
public:
    CombinatorialPurgedCV(size_t n_test_groups,
                         size_t purge_window = 5,
                         size_t embargo_periods = 5)
        : n_test_groups_(n_test_groups)
        , purge_window_(purge_window)
        , embargo_periods_(embargo_periods) {}
    
    // Generate all combinatorial splits
    std::vector<TimeSeriesSplit> split(size_t n_samples, size_t n_groups) const {
        if (n_test_groups_ >= n_groups) {
            throw std::invalid_argument("n_test_groups must be less than n_groups");
        }
        
        // Generate group indices
        std::vector<std::vector<size_t>> test_combinations;
        std::vector<size_t> current;
        generateCombinations(test_combinations, current, 0, n_groups - 1, n_test_groups_);
        
        std::vector<TimeSeriesSplit> splits;
        splits.reserve(test_combinations.size());
        
        size_t group_size = n_samples / n_groups;
        PurgedKFoldCV purger(2, purge_window_, embargo_periods_);
        
        for (const auto& test_group_indices : test_combinations) {
            TimeSeriesSplit split;
            
            // Create test indices from selected groups
            for (size_t group_idx : test_group_indices) {
                size_t start = group_idx * group_size;
                size_t end = (group_idx == n_groups - 1) ? n_samples : (group_idx + 1) * group_size;
                
                for (size_t i = start; i < end; ++i) {
                    split.test_indices.push_back(i);
                }
            }
            
            // Training set is all other indices
            std::vector<size_t> train_indices;
            for (size_t i = 0; i < n_samples; ++i) {
                if (std::find(split.test_indices.begin(), split.test_indices.end(), i) 
                    == split.test_indices.end()) {
                    train_indices.push_back(i);
                }
            }
            
            // Apply purging using public wrappers
            auto purge_indices = purger.publicGetPurgeIndices(split.test_indices, n_samples);
            split.train_indices = purger.publicApplyPurge(train_indices, purge_indices);
            
            splits.push_back(std::move(split));
        }
        
        return splits;
    }
    
    // Calculate number of combinations
    static size_t calculateNumSplits(size_t n_groups, size_t n_test_groups) {
        // C(n, k) = n! / (k! * (n-k)!)
        size_t result = 1;
        for (size_t i = 0; i < n_test_groups; ++i) {
            result *= (n_groups - i);
            result /= (i + 1);
        }
        return result;
    }
};

// ============================================================================
// Cross-Validation Executor
// ============================================================================

template<typename Strategy, typename Data>
class CrossValidator {
private:
    using ScoreFunction = std::function<double(const Strategy&, 
                                              const Data&, 
                                              const std::vector<size_t>&,
                                              const std::vector<size_t>&)>;
    
    ScoreFunction score_func_;
    
    // Calculate statistics from fold scores
    CVResult calculateStatistics(const std::vector<double>& scores) const {
        CVResult result;
        result.fold_scores = scores;
        result.num_folds = scores.size();
        
        if (scores.empty()) {
            result.mean_score = 0.0;
            result.std_score = 0.0;
            result.min_score = 0.0;
            result.max_score = 0.0;
            result.sharpe_ratio = 0.0;
            result.stability = 0.0;
            return result;
        }
        
        // Mean
        result.mean_score = std::accumulate(scores.begin(), scores.end(), 0.0) / scores.size();
        
        // Standard deviation
        double sum_sq_diff = 0.0;
        for (double score : scores) {
            double diff = score - result.mean_score;
            sum_sq_diff += diff * diff;
        }
        result.std_score = std::sqrt(sum_sq_diff / scores.size());
        
        // Min/Max
        result.min_score = *std::min_element(scores.begin(), scores.end());
        result.max_score = *std::max_element(scores.begin(), scores.end());
        
        // Sharpe-like ratio (mean/std)
        result.sharpe_ratio = (result.std_score > 1e-10) ? 
                             result.mean_score / result.std_score : 0.0;
        
        // Stability: inverse of coefficient of variation
        result.stability = (std::abs(result.mean_score) > 1e-10) ?
                          1.0 / (result.std_score / std::abs(result.mean_score)) : 0.0;
        
        return result;
    }
    
public:
    explicit CrossValidator(ScoreFunction score_func)
        : score_func_(score_func) {}
    
    // Run purged K-fold CV
    CVResult runPurgedKFold(const Strategy& strategy,
                           const Data& data,
                           size_t n_splits,
                           size_t purge_window = 5,
                           size_t embargo = 5) {
        
        PurgedKFoldCV cv(n_splits, purge_window, embargo);
        auto splits = cv.split(data.size());
        
        std::vector<double> scores;
        scores.reserve(splits.size());
        
        std::cout << "Running Purged " << n_splits << "-Fold Cross-Validation...\n";
        
        for (size_t i = 0; i < splits.size(); ++i) {
            const auto& split = splits[i];
            
            double score = score_func_(strategy, data, 
                                      split.train_indices, 
                                      split.test_indices);
            scores.push_back(score);
            
            std::cout << "  Fold " << (i + 1) << "/" << splits.size() 
                     << ": Score = " << score 
                     << " (Train: " << split.train_size() 
                     << ", Test: " << split.test_size() << ")\n";
        }
        
        return calculateStatistics(scores);
    }
    
    // Run combinatorial purged CV
    CVResult runCombinatorialCV(const Strategy& strategy,
                               const Data& data,
                               size_t n_groups,
                               size_t n_test_groups,
                               size_t purge_window = 5,
                               size_t embargo = 5) {
        
        CombinatorialPurgedCV cv(n_test_groups, purge_window, embargo);
        auto splits = cv.split(data.size(), n_groups);
        
        std::vector<double> scores;
        scores.reserve(splits.size());
        
        std::cout << "Running Combinatorial Purged CV (" << splits.size() << " combinations)...\n";
        
        size_t count = 0;
        for (const auto& split : splits) {
            double score = score_func_(strategy, data,
                                      split.train_indices,
                                      split.test_indices);
            scores.push_back(score);
            
            if (++count % 10 == 0 || count == splits.size()) {
                std::cout << "  Completed " << count << "/" << splits.size() 
                         << " combinations...\n";
            }
        }
        
        return calculateStatistics(scores);
    }
};

} // namespace backtesting