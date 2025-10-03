### Why Validation Matters

**The Problem:** Testing thousands of strategy variations leads to **overfitting**. The best-performing strategy is likely just lucky, not skilled.

**The Solution:** Phase 5 provides tools to determine if observed performance is **statistically significant** or just **random chance**.

### Key Concepts

#### Deflated Sharpe Ratio (DSR)

Adjusts observed Sharpe ratio for multiple testing bias:

```
DSR = (SR_observed - E[max(SR₀)]) / σ[SR]
```

Where:
- `SR_observed` = Your strategy's Sharpe ratio
- `E[max(SR₀)]` = Expected maximum SR under null hypothesis (zero skill)
- `σ[SR]` = Standard error of Sharpe ratio estimator

**Example:**
```cpp
DeflatedSharpeRatio dsr;
auto result = dsr.calculateDetailed(returns, num_trials);

if (result.is_significant && result.deflated_sharpe > 0.5) {
    std::cout << "✓ Strategy validated - safe to deploy\n";
} else {
    std::cout << "✗ Strategy overfit - do NOT deploy\n";
}
```

#### Purged K-Fold Cross-Validation

Standard CV **fails for time series** because it causes information leakage. Our implementation:
- Respects temporal ordering
- Purges training samples influenced by test set
- Adds embargo periods to prevent serial correlation

**Example:**
```cpp
PurgedKFoldCV cv(5, 10, 5);  // 5 folds, 10-bar purge, 5-bar embargo
auto splits = cv.split(returns.size());

for (const auto& split : splits) {
    // Train on split.train_indices
    // Test on split.test_indices
    // No information leakage!
}
```

