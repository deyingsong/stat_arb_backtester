### Run All Tests

```bash
# Phase 1: Core infrastructure
./bin/test_core_infrastructure

# Phase 2: End-to-end system
./bin/test_end_to_end_system

# Phase 3: Statistical arbitrage
./bin/test_stat_arb_system

# Phase 4: Performance benchmarks
./bin/test_phase4_performance

# Phase 5: Statistical validation
./bin/test_phase5_validation

# Integration test
./bin/test_validation_integration
```

### Test Coverage

- ✅ Event system and queue operations
- ✅ Data loading and synchronization
- ✅ Strategy signal generation
- ✅ Portfolio management and P&L
- ✅ Order execution with costs
- ✅ Cointegration testing
- ✅ Rolling statistics accuracy
- ✅ SIMD operations correctness
- ✅ Memory pool thread-safety
- ✅ Cross-validation procedures
- ✅ Deflated Sharpe Ratio calculations


