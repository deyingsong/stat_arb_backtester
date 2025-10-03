### Data Format

The backtester expects CSV files with the following format:

```csv
date,symbol,open,high,low,close,volume
2023-01-01,AAPL,150.0,152.0,149.5,151.5,1000000
2023-01-01,GOOGL,100.0,101.0,99.5,100.5,500000
2023-01-02,AAPL,151.5,153.0,151.0,152.5,1100000
2023-01-02,GOOGL,100.5,101.5,100.0,101.0,550000
...
```

**Requirements:**
- Header row with column names
- Date in `YYYY-MM-DD` format
- Multiple symbols can be in same file or separate files
- Timestamps must be synchronized across symbols

