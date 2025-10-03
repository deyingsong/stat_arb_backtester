### Command-Line Options

```
Usage: stat_arb_backtest [OPTIONS]

Options:
  -d, --data FILE          Data file path (default: data/prices.csv)
  -s, --symbols SYM1,SYM2  Trading symbols (default: AAPL,GOOGL)
  -t, --strategy TYPE      Strategy type: stat_arb or simple_ma (default: stat_arb)
  -e, --entry THRESHOLD    Entry z-score threshold (default: 2.0)
  -x, --exit THRESHOLD     Exit z-score threshold (default: 0.5)
  -w, --window SIZE        Lookback window size (default: 60)
  -c, --capital AMOUNT     Initial capital (default: 100000)
  -a, --advanced           Use advanced execution model
  -v, --validate           Run statistical validation (Phase 5)
  -n, --trials NUM         Number of trials for validation (default: 1)
  -o, --output FILE        Output file path (default: backtest_results.txt)
  --verbose                Enable verbose output
  --show-trades            Show individual trades
  -h, --help               Show this help message

Examples:
  # Statistical arbitrage with validation
  ./bin/stat_arb_backtest --data data/AAPL.csv --symbols AAPL,GOOGL --validate --trials 100
  
  # Simple moving average strategy
  ./bin/stat_arb_backtest -t simple_ma -w 20 -c 50000 --verbose
  
  # Custom thresholds with advanced execution
  ./bin/stat_arb_backtest --strategy stat_arb -e 2.5 -x 0.3 --advanced
```
