# Phase 5: Advanced Statistical Validation

## Overview

Phase 5 implements advanced statistical validation tools that are **critical for preventing overfitting** and providing realistic assessment of backtested strategy performance. These tools account for the research process itself - specifically the number of trials conducted to arrive at a final strategy.

## Why Phase 5 Matters

**The Problem:** A researcher who tests thousands of strategy variations is effectively cherry-picking the best result from a large distribution of outcomes. The maximum Sharpe ratio observed in such a process is a biased and overly optimistic estimate of future performance.

**The Solution:** Phase 5 provides the statistical machinery to:
1. Validate strategies without information leakage (Purged CV)
2. Adjust performance metrics for multiple testing bias (Deflated Sharpe Ratio)
3. Determine if observed performance is likely genuine skill vs. random luck

## Implemented Components

### 5.1 Purged K-Fold Cross-Validation

Standard machine learning cross-validation **fails for financial time series** because data points are not independent. Randomly splitting time series data causes information from the "future" (test set) to leak into the "past" (training set).

**Our Implementation:**
- **Sequential Splits:** Respects temporal ordering
- **Purging:** Removes training samples whose labels are influenced by test set observations
- **Embargo Periods:** Adds buffer after test set to prevent serial correlation

**Key Classes:**
```cpp
PurgedKFoldCV cv(n_splits, purge_window, embargo_periods);
auto splits = cv.split(n_samples);
```

### 5.2 Combinatorial Purged Cross-Validation (CPCV)

An even more robust extension of Purged K-Fold CV. Instead of a single walk-forward split, CPCV evaluates **all possible combinations** of training/testing splits.

**Benefits:**
- Provides a **distribution** of out-of-sample performance metrics
- Makes results less dependent on a single, potentially "lucky" data split
- More computationally expensive but dramatically more robust

**Key Classes:**
```cpp
CombinatorialPurgedCV cv(n_test_groups, purge_window, embargo);
auto splits = cv.split(n_samples, n_groups);
```

### 5.3 Deflated Sharpe Ratio (DSR)

The DSR is the **most important tool** for determining if an observed Sharpe ratio is statistically significant or likely the result of multiple testing.

**What it does:**
- Adjusts observed Sharpe ratio **downward** by considering:
  - Number of independent trials conducted
  - Length of the backtest
  - Skewness and kurtosis of returns
- A high Sharpe ratio might become **statistically insignificant** after deflation

**Key Features:**
- **Deflated Sharpe Ratio:** Adjusted for multiple testing
- **Probabilistic Sharpe Ratio (PSR):** Probability that SR > benchmark
- **Statistical Significance:** P-value testing
- **Minimum Track Length:** Required observations for significance

**Key Classes:**
```cpp
DeflatedSharpeRatio dsr_calc;
auto result = dsr_calc.calculateDetailed(returns, num_trials);

// result contains:
// - deflated_sharpe
// - observed_sharpe
// - expected_max_sharpe (under null hypothesis)
// - psr (probabilistic sharpe ratio)
// - p_value
// - is_significant
```

### 5.4 Multiple Testing Adjustments

When testing multiple strategies, standard p-values become unreliable. We implement three correction methods:

1. **Bonferroni:** Conservative, controls family-wise error rate
2. **Holm-Bonferroni:** Sequential method, less conservative
3. **Benjamini-Hochberg:** Controls false discovery rate (FDR)

## File Structure

```
include/validation/
├── purged_cross_validation.hpp    # Purged CV implementations
└── deflated_sharpe_ratio.hpp      # DSR and statistical utilities

test/
└── test_phase5_validation.cpp     # Comprehensive test suite

Makefile.phase5                     # Build configuration
build_phase5.sh                     # Automated build script
```

## Building and Running

### Quick Start

```bash
# Make build script executable
chmod +x build_phase5.sh

# Build and run tests
./build_phase5.sh run
```

### Using Makefile

```bash
# Build Phase 5
make -f Makefile.phase5

# Run tests
make -f Makefile.phase5 test

# Clean build
make -f Makefile.phase5 clean
```

### Manual Build

```bash
# Create directories
mkdir -p include/validation bin

# Compile
clang++ -std=c++17 -O3 -I./include \
    -o bin/test_phase5_validation \
    test/test_phase5_validation.cpp

# Run
./bin/test_phase5_validation
```

## Usage Examples

### Example 1: Purged Cross-Validation

```cpp
// Define scoring function
auto score_func = [](const Strategy& strat,
                    const Data& data,
                    const std::vector<size_t>& train_idx,
                    const std::vector<size_t>& test_idx) -> double {
    // Calculate and return performance metric
    return calculateSharpeRatio(test_returns);
};

// Create validator
CrossValidator<Strategy, Data> validator(score_func);

// Run purged K-fold CV
auto result = validator.runPurgedKFold(
    strategy, data, 
    5,     // n_splits
    5,     // purge_window
    5      // embargo_periods
);

std::cout << "Mean Score: " << result.mean_score << "\n";
std::cout << "Stability: " << result.stability << "\n";
```

### Example 2: Deflated Sharpe Ratio

```cpp
// Your strategy returns
std::vector<double> returns = getStrategyReturns();

// Number of strategies tested to find this one
size_t num_trials = 100;

// Calculate DSR
DeflatedSharpeRatio dsr_calc;
auto result = dsr_calc.calculateDetailed(returns, num_trials);

std::cout << "Observed Sharpe: " << result.observed_sharpe << "\n";
std::cout << "Deflated Sharpe: " << result.deflated_sharpe << "\n";
std::cout << "Is Significant: " << (result.is_significant ? "YES" : "NO") << "\n";

// Decision logic
if (result.is_significant && result.deflated_sharpe > 0) {
    std::cout << "PASS: Strategy shows genuine skill\n";
    // Safe to deploy with risk controls
} else {
    std::cout << "FAIL: Strategy likely overfit\n";
    // Do NOT deploy
}
```

### Example 3: Minimum Track Length

```cpp
DeflatedSharpeRatio dsr_calc;

// How many observations needed?
double min_length = dsr_calc.calculateMinTrackLength(
    1.5,    // observed_sharpe
    0.0,    // target_sharpe (benchmark)
    0.0,    // skewness
    0.0,    // kurtosis
    0.95    // confidence level
);

std::cout << "Need " << min_length << " periods for 95% confidence\n";
```

## Mathematical Background

### Purged Cross-Validation

For each fold k:
1. Define test set T_k
2. Define training set as all samples not in T_k
3. **Purge:** Remove samples from training where labels overlap with test observations
4. **Embargo:** Remove samples immediately after test set (prevents serial correlation)

### Deflated Sharpe Ratio

The DSR formula:

```
DSR = (SR_observed - E[max(SR₀)]) / σ[SR]

where:
- SR_observed = your observed Sharpe ratio
- E[max(SR₀)] = expected maximum SR under null hypothesis (zero skill)
- σ[SR] = standard error of Sharpe ratio estimator
```

The variance of Sharpe ratio estimator (Bailey & López de Prado):

```
Var[SR] ≈ (1 + SR²/2 - SR·γ₁ + (3+γ₂-γ₁)·SR²/4) / (n-1)

where:
- γ₁ = skewness
- γ₂ = excess kurtosis
- n = number of observations
```

### Expected Maximum Sharpe

Under the null hypothesis of zero skill, if you test N strategies:

```
E[max(SR₁, SR₂, ..., SRₙ)] ≈ Φ⁻¹(1 - 1/(N+1)) · σ[SR]

where Φ⁻¹ is the inverse normal CDF
```

## Test Suite

The test suite demonstrates:

1. **Purged CV vs Standard CV:** Shows how purging affects results
2. **DSR with Few vs Many Trials:** Demonstrates multiple testing adjustment
3. **Fat-Tailed Returns:** Effect of non-normal distributions
4. **Minimum Track Length:** Required observations for significance
5. **Multiple Testing Corrections:** Bonferroni, Holm, Benjamini-Hochberg
6. **Integrated Workflow:** Complete validation pipeline

## Performance Characteristics

- **Purged K-Fold:** O(n) per split, total O(k·n) for k folds
- **CPCV:** O(C(n,k)) combinations where C is binomial coefficient
- **DSR Calculation:** O(n) for single calculation
- **Memory:** O(n) for storing indices and intermediate results

## Integration with Backtesting Engine

Phase 5 is designed as a **separate analysis module** that consumes output from the backtester:

```cpp
// 1. Run backtest
auto backtest_results = cerebro.run();

// 2. Extract returns
std::vector<double> returns = extractReturns(backtest_results);

// 3. Apply validation
DeflatedSharpeRatio dsr;
auto validation = dsr.calculateDetailed(returns, num_trials_conducted);

// 4. Make deployment decision
bool deploy = validation.is_significant && validation.deflated_sharpe > threshold;
```

## Critical Reminders

1. **Track Your Trials:** The number of strategies tested is CRITICAL for DSR
2. **Don't Cheat:** If you test 1000 variations, DSR needs to know about all 1000
3. **Respect the Process:** Use purged CV to prevent information leakage
4. **Understand the Math:** These aren't just formulas - they encode fundamental statistical truths about overfitting

## References

- Bailey, D. H., & López de Prado, M. (2014). "The Deflated Sharpe Ratio: Correcting for Selection Bias, Backtest Overfitting, and Non-Normality"
- López de Prado, M. (2018). "Advances in Financial Machine Learning"
- Bailey, D. H., Borwein, J., López de Prado, M., & Zhu, Q. J. (2017). "The Probability of Backtest Overfitting"

## Next Steps

After Phase 5, your backtesting framework is complete with:
- ✅ Event-driven architecture (Phase 1)
- ✅ End-to-end simulation (Phase 2)  
- ✅ Statistical arbitrage features (Phase 3)
- ✅ Performance optimization (Phase 4)
- ✅ Advanced validation (Phase 5)

**You now have a professional-grade backtesting system that:**
- Simulates strategies realistically
- Optimizes for performance
- Validates results rigorously
- Accounts for the research process
- Prevents deployment of overfit strategies

The system is ready for serious quantitative research and strategy development!