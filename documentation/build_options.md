### Build Options

```bash
# Debug build with symbols
clang++ -std=c++17 -g -I./include -o bin/backtest_debug src/main.cpp

# Profile-guided optimization (2 steps)
# Step 1: Generate profile
clang++ -std=c++17 -O3 -fprofile-generate -I./include -o bin/backtest src/main.cpp
./bin/backtest  # Run with representative data
# Step 2: Use profile
clang++ -std=c++17 -O3 -fprofile-use -I./include -o bin/backtest_opt src/main.cpp

# Enable SIMD on Apple Silicon
clang++ -std=c++17 -O3 -march=armv8-a+simd -I./include -o bin/backtest src/main.cpp

# Enable SIMD on x86
g++ -std=c++17 -O3 -march=native -mavx2 -I./include -o bin/backtest src/main.cpp
```


