#include <iostream>
#include <vector>
#include <cmath>
#include <random>
#include <cassert>
#include "../include/strategies/rolling_statistics.hpp"
#include "../include/strategies/cointegration_analyzer.hpp"

using namespace backtesting;

// Generate an OU-like spread and check half-life > 0
int main() {
    std::vector<double> spread;
    double x = 0.0;
    double theta = 0.5; // mean reversion speed
    double mu = 0.0;
    double sigma = 1.0;
    std::mt19937 rng(42);
    std::normal_distribution<> n(0.0, 1.0);

    for (int i = 0; i < 200; ++i) {
        x += theta * (mu - x) + sigma * n(rng);
        spread.push_back(x);
    }

    CointegrationAnalyzer c; // has calculateHalfLife
    double hl = c.calculateHalfLife(spread);

    if (!(hl > 0.0 && std::isfinite(hl))) {
        std::cerr << "Half-life test failed, hl=" << hl << std::endl;
        return 2;
    }

    std::cout << "test_half_life: OK (hl=" << hl << ")\n";
    return 0;
}
