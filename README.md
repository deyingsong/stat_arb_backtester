# Statistical Arbitrage Backtesting Engine


[![C++17](https://img.shields.io/badge/C++-17-blue.svg)](https://isocpp.org/)
[![Platform](https://img.shields.io/badge/platform-macOS%20%7C%20Linux-lightgrey.svg)](https://github.com/)

[Installation](#installation) • [Usage](#usage) • [Architecture](#architecture) • [Documentation](#documentation)




## Overview

A high-performance backtesting engine designed for statistical arbitrage and quantitative trading strategies. Built with modern C++17, this framework implements industry best practices from algorithmic trading and provides institutional-grade features including:

- **Event-driven architecture** - No look-ahead bias, realistic order simulation
- **Low-latency performance** - SIMD optimizations, lock-free data structures
- **Statistical validation** - Deflated Sharpe Ratio, Purged Cross-Validation
- **Production-ready** - Comprehensive error handling, extensive testing




## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                   BACKTESTING FRAMEWORK                         │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐           │
│  │ Data Handler │→ │   Strategy   │→ │  Portfolio   │           │
│  │ (CSV/Live)   │  │  (Stat Arb)  │  │ (Positions)  │           │
│  └──────────────┘  └──────────────┘  └──────────────┘           │
│         ↓                  ↓                  ↓                 │
│  ┌───────────────────────────────────────────────────────┐      │
│  │   Event Queue (Lock-Free Disruptor) - 65536 slots     │      │
│  │   [Market Event] → [Signal Event] → [Order Event]     │      │
│  └───────────────────────────────────────────────────────┘      │
│         ↓                  ↓                  ↓                 │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐           │
│  │  Execution   │  │     Risk     │  │   Cerebro    │           │
│  │   Handler    │  │  Management  │  │   Engine     │           │
│  └──────────────┘  └──────────────┘  └──────────────┘           │
│                                                                 │
├─────────────────────────────────────────────────────────────────┤
│                VALIDATION MODULE                                │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌──────────────────┐  ┌────────────────────────────┐           │
│  │ Purged K-Fold CV │  │  Deflated Sharpe Ratio     │           │
│  │ (No Info Leak)   │  │  (Multiple Testing Adj.)   │           │
│  └──────────────────┘  └────────────────────────────┘           │
│           ↓                        ↓                            │
│  ┌───────────────────────────────────────────────────┐          │
│  │    Deployment Decision: Deploy vs. Reject          │         │
│  └───────────────────────────────────────────────────┘          │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### Event Flow

1. **Market Data** → Data handler publishes `MarketEvent`
2. **Strategy Logic** → Consumes market events, generates `SignalEvent`
3. **Portfolio** → Converts signals to `OrderEvent` based on risk management
4. **Execution** → Simulates order execution, updates portfolio with `FillEvent`
5. **Loop** → Process continues until all data consumed

### Key Design Patterns

- **Strategy Pattern**: Pluggable strategies via `IStrategy` interface
- **Observer Pattern**: Event-driven communication between components
- **Factory Pattern**: Event creation via builders
- **Object Pool**: Memory pool for high-frequency allocations
- **RAII**: Automatic resource management



## Installation

### Prerequisites

- **Compiler**: Clang 11+ or GCC 9+ with C++17 support
- **OS**: macOS or Linux (tested on macOS 12+, Ubuntu 20.04+)
- **Optional**: Eigen library for advanced matrix operations

### Quick Start

```bash
# Clone the repository
git clone https://github.com/yourusername/stat_arb_backtester.git
cd stat_arb_backtester

# Run the main backtester
./bin/stat_arb_backtest --help
```

### Manual Build

```bash
# Compile main program
clang++ -std=c++17 -O3 -march=native -flto \
    -I./include \
    -o bin/stat_arb_backtest \
    src/main.cpp

# Run with default configuration
./bin/stat_arb_backtest
```

### [Build Options](documentation/build_options.md)

## Usage

### Basic Example

```bash
# Run with default configuration (AAPL vs GOOGL pairs trading)
./bin/stat_arb_backtest

# Specify custom data file
./bin/stat_arb_backtest --data data/my_prices.csv --symbols AAPL,MSFT

# Run with validation (Phase 5)
./bin/stat_arb_backtest --validate --trials 100
```

### [Command-Line Options](documentation/command_line_options.md)

### [Data Format](documentation/data_format.md)

## [Tests](documentation/testing.md)


## Documentation

- **[API Reference](documentation/api_reference.md)** - API reference: strategy, portfolio, and execution interface
- **[Create Custom Strategy](documentation/create_custom_strategy.md)** - Create custom strategy
- **[Final Summary](documentation/final_completion_summary.md)** - Complete project overview
- **[Validation Report](documentation/validation_report.txt)** - Sample validation output


## [Statistical Validation](documentation/stats_validation.md) 


## Reference


- Chan, E. P. (2021). Quantitative Trading: How to Build Your Own Algorithmic Trading Business. United Kingdom: Wiley.
- Hanson, D. (2024). Learning Modern C++ for Finance. (n.p.): O'Reilly Media.
- Lopez de Prado, M. (2018). Advances in Financial Machine Learning. United Kingdom: Wiley.
- Harris, L. (2023). Trading and Exchanges: Market Microstructure for Practitioners. United States: Oxford University Press.
- [awesome-quant](https://github.com/wilsonfreitas/awesome-quant): A curated list of insanely awesome libraries, packages and resources for Quants (Quantitative Finance)

