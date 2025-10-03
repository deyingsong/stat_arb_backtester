### API Reference

#### Strategy Interface

```cpp
class IStrategy {
public:
    virtual ~IStrategy() = default;
    virtual void onMarketEvent(const MarketEvent& event) = 0;
    virtual std::string getName() const = 0;
};
```

#### Portfolio Interface

```cpp
class IPortfolio {
public:
    virtual ~IPortfolio() = default;
    virtual void updateFromFill(const FillEvent& fill) = 0;
    virtual double getEquity() const = 0;
    virtual Position getPosition(const std::string& symbol) const = 0;
};
```

#### Execution Interface

```cpp
class IExecutionHandler {
public:
    virtual ~IExecutionHandler() = default;
    virtual FillEvent executeOrder(const OrderEvent& order) = 0;
};
```
