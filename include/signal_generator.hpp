#pragma once
#include <vector>

class SignalGenerator {
public:
    SignalGenerator(double entry_z, double exit_z, int window);

    void update(double priceA, double priceB);
    int get_signal() const; // +1 long, -1 short, 0 neutral

private:
    double entry_threshold_;
    double exit_threshold_;
    int lookback_window_;

    std::vector<double> spread_history_;
    double compute_zscore() const;
};
