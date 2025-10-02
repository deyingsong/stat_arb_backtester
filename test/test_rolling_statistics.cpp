#include <iostream>
#include <cassert>
#include <random>
#include "../include/strategies/rolling_statistics.hpp"

using namespace backtesting;

int main() {
    RollingStatistics stats(20);

    // deterministic seed
    std::mt19937 rng(12345);
    std::normal_distribution<> dist(100.0, 10.0);

    // warm up
    for (int i = 0; i < 50; ++i) stats.update(dist(rng));

    double m1 = stats.getMean();
    double s1 = stats.getStdDev();
    assert(s1 >= 0.0);

    // push a sequence of identical values to reduce stddev
    for (int i = 0; i < 100; ++i) stats.update(100.0);
    double m2 = stats.getMean();
    double s2 = stats.getStdDev();

    // mean should be close to 100, stddev should be small
    if (!(std::abs(m2 - 100.0) < 1.0)) {
        std::cerr << "RollingStatistics mean drift: " << m2 << std::endl;
        return 2;
    }
    if (!(s2 >= 0.0 && s2 < 5.0)) {
        std::cerr << "RollingStatistics stddev out of expected range: " << s2 << std::endl;
        return 3;
    }

    std::cout << "test_rolling_statistics: OK\n";
    return 0;
}
