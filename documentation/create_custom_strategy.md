### Creating Custom Strategies

```cpp
#include "interfaces/strategy.hpp"

class MyCustomStrategy : public IStrategy {
private:
    std::string symbol_;
    double threshold_;
    
public:
    MyCustomStrategy(const std::string& symbol, double threshold)
        : symbol_(symbol), threshold_(threshold) {}
    
    void onMarketEvent(const MarketEvent& event) override {
        // Your strategy logic here
        if (event.symbol == symbol_) {
            double signal = calculateSignal(event);
            if (signal > threshold_) {
                // Generate buy signal
                generateSignal(SignalType::BUY, symbol_, 100);
            }
        }
    }
    
    std::string getName() const override {
        return "MyCustomStrategy";
    }
    
private:
    double calculateSignal(const MarketEvent& event) {
        // Your calculation logic
        return 0.0;
    }
};
```


