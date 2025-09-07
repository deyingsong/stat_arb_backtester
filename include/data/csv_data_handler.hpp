// csv_data_handler.hpp
// CSV Data Handler Implementation for Statistical Arbitrage Backtesting Engine
// Provides historical market data loading and event generation from CSV files

#pragma once

#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <unordered_map>
#include <optional>
#include <chrono>
#include <algorithm>
#include <queue>
#include <memory>
#include "../interfaces/data_handler.hpp"
#include "../core/event_types.hpp"
#include "../core/exceptions.hpp"
#include "../concurrent/disruptor_queue.hpp"

namespace backtesting {

// ============================================================================
// CSV Data Handler for Historical Market Data
// ============================================================================

class CsvDataHandler : public IDataHandler {
public:
    struct CsvConfig {
        bool has_header;
        char delimiter;
        std::string date_format;  // strptime format
        std::string time_format;  // Optional time column
        bool adjust_for_splits;
        bool check_data_integrity;
        
        // Default constructor with explicit initialization
        CsvConfig() 
            : has_header(true)
            , delimiter(',')
            , date_format("%Y-%m-%d")
            , time_format("%H:%M:%S")
            , adjust_for_splits(false)
            , check_data_integrity(true) {}
        
        // Static method to get default config
        static CsvConfig getDefault() {
            return CsvConfig();
        }
    };
    
private:
    // Data structure for a single bar
    struct Bar {
        std::chrono::nanoseconds timestamp;
        double open, high, low, close, volume;
        double adj_close;  // Adjusted close for splits/dividends
        double bid, ask;   // Optional bid/ask for spread modeling
        
        bool validate() const {
            return high >= low && 
                   high >= open && high >= close &&
                   low <= open && low <= close &&
                   volume >= 0;
        }
    };
    
    // Store bars for each symbol
    std::unordered_map<std::string, std::vector<Bar>> symbol_data_;
    std::unordered_map<std::string, size_t> current_indices_;
    std::unordered_map<std::string, Bar> latest_bars_;
    
    // Priority queue for synchronized multi-asset iteration
    struct TimePoint {
        std::chrono::nanoseconds timestamp;
        std::string symbol;
        size_t index;
        
        // Min heap based on timestamp
        bool operator>(const TimePoint& other) const {
            return timestamp > other.timestamp;
        }
    };
    std::priority_queue<TimePoint, std::vector<TimePoint>, std::greater<TimePoint>> time_queue_;
    
    // Event queue reference
    DisruptorQueue<EventVariant, 65536>* event_queue_ = nullptr;
    
    // Configuration
    CsvConfig config_;
    bool initialized_ = false;
    size_t total_bars_processed_ = 0;
    
    // CSV parsing helpers
    std::vector<std::string> splitLine(const std::string& line, char delimiter) {
        std::vector<std::string> tokens;
        std::stringstream ss(line);
        std::string token;
        while (std::getline(ss, token, delimiter)) {
            // Trim whitespace
            token.erase(0, token.find_first_not_of(" \t\r\n"));
            token.erase(token.find_last_not_of(" \t\r\n") + 1);
            tokens.push_back(token);
        }
        return tokens;
    }
    
    std::chrono::nanoseconds parseTimestamp(const std::string& date_str, 
                                           const std::string& time_str = "") {
        // Simplified timestamp parsing - in production use date library
        // For now, parse YYYY-MM-DD format
        std::tm tm = {};
        std::istringstream ss(date_str);
        ss >> std::get_time(&tm, config_.date_format.c_str());
        
        if (!time_str.empty()) {
            std::istringstream time_ss(time_str);
            time_ss >> std::get_time(&tm, config_.time_format.c_str());
        }
        
        auto time_point = std::chrono::system_clock::from_time_t(std::mktime(&tm));
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            time_point.time_since_epoch());
    }
    
public:
    // Default constructor using default config
    CsvDataHandler() : config_(CsvConfig::getDefault()) {}
    
    // Constructor with custom config
    explicit CsvDataHandler(const CsvConfig& config) 
        : config_(config) {}
    
    // Load CSV data for a symbol
    void loadCsv(const std::string& symbol, const std::string& filepath) {
        if (initialized_) {
            throw DataException("Cannot load data after initialization");
        }
        
        std::ifstream file(filepath);
        if (!file.is_open()) {
            throw DataException("Failed to open CSV file: " + filepath);
        }
        
        std::vector<Bar> bars;
        std::string line;
        
        // Skip header if present
        if (config_.has_header && !std::getline(file, line)) {
            throw DataException("Empty CSV file: " + filepath);
        }
        
        // Parse each line
        size_t line_num = config_.has_header ? 2 : 1;
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            
            auto tokens = splitLine(line, config_.delimiter);
            if (tokens.size() < 6) {  // Minimum: Date,O,H,L,C,V
                throw DataException("Invalid CSV format at line " + 
                                  std::to_string(line_num));
            }
            
            try {
                Bar bar;
                bar.timestamp = parseTimestamp(tokens[0]);
                bar.open = std::stod(tokens[1]);
                bar.high = std::stod(tokens[2]);
                bar.low = std::stod(tokens[3]);
                bar.close = std::stod(tokens[4]);
                bar.volume = std::stod(tokens[5]);
                
                // Optional adjusted close
                bar.adj_close = (tokens.size() > 6) ? std::stod(tokens[6]) : bar.close;
                
                // Optional bid/ask
                bar.bid = (tokens.size() > 7) ? std::stod(tokens[7]) : bar.close - 0.01;
                bar.ask = (tokens.size() > 8) ? std::stod(tokens[8]) : bar.close + 0.01;
                
                // Validate bar
                if (config_.check_data_integrity && !bar.validate()) {
                    throw DataException("Invalid bar data at line " + 
                                      std::to_string(line_num));
                }
                
                bars.push_back(bar);
            } catch (const std::exception& e) {
                throw DataException("Error parsing line " + std::to_string(line_num) + 
                                  ": " + e.what());
            }
            
            line_num++;
        }
        
        if (bars.empty()) {
            throw DataException("No valid bars loaded from: " + filepath);
        }
        
        // Sort bars by timestamp (ensure chronological order)
        std::sort(bars.begin(), bars.end(), 
                  [](const Bar& a, const Bar& b) { 
                      return a.timestamp < b.timestamp; 
                  });
        
        // Store the data
        symbol_data_[symbol] = std::move(bars);
        current_indices_[symbol] = 0;
    }
    
    // Set the event queue for publishing MarketEvents
    void setEventQueue(DisruptorQueue<EventVariant, 65536>* queue) {
        event_queue_ = queue;
    }
    
    // IDataHandler interface implementation
    void initialize() override {
        if (initialized_) return;
        
        if (symbol_data_.empty()) {
            throw DataException("No data loaded before initialization");
        }
        
        // Initialize the priority queue with first bar from each symbol
        for (const auto& [symbol, bars] : symbol_data_) {
            if (!bars.empty()) {
                time_queue_.push({bars[0].timestamp, symbol, 0});
            }
        }
        
        initialized_ = true;
        total_bars_processed_ = 0;
    }
    
    bool hasMoreData() const override {
        return !time_queue_.empty();
    }
    
    void updateBars() override {
        if (!initialized_) {
            throw DataException("Data handler not initialized");
        }
        
        if (time_queue_.empty()) {
            return;
        }
        
        // Get the next bar in chronological order
        auto time_point = time_queue_.top();
        time_queue_.pop();
        
        const auto& bars = symbol_data_.at(time_point.symbol);
        const auto& bar = bars[time_point.index];
        
        // Update latest bar for this symbol
        latest_bars_[time_point.symbol] = bar;
        
        // Create and publish MarketEvent
        if (event_queue_) {
            MarketEvent event;
            event.symbol = time_point.symbol;
            event.timestamp = bar.timestamp;
            event.sequence_id = ++total_bars_processed_;
            event.open = bar.open;
            event.high = bar.high;
            event.low = bar.low;
            event.close = bar.close;
            event.volume = bar.volume;
            event.bid = bar.bid;
            event.ask = bar.ask;
            event.bid_size = 100;  // Default size
            event.ask_size = 100;
            
            if (!event.validate()) {
                throw DataException("Invalid MarketEvent generated");
            }
            
            event_queue_->publish(event);
        }
        
        // Queue next bar for this symbol if available
        size_t next_index = time_point.index + 1;
        if (next_index < bars.size()) {
            time_queue_.push({bars[next_index].timestamp, time_point.symbol, next_index});
        }
        
        current_indices_[time_point.symbol] = next_index;
    }
    
    std::optional<MarketEvent> getLatestBar(const std::string& symbol) const override {
        auto it = latest_bars_.find(symbol);
        if (it == latest_bars_.end()) {
            return std::nullopt;
        }
        
        const auto& bar = it->second;
        MarketEvent event;
        event.symbol = symbol;
        event.timestamp = bar.timestamp;
        event.open = bar.open;
        event.high = bar.high;
        event.low = bar.low;
        event.close = bar.close;
        event.volume = bar.volume;
        event.bid = bar.bid;
        event.ask = bar.ask;
        event.bid_size = 100;
        event.ask_size = 100;
        
        return event;
    }
    
    std::vector<std::string> getSymbols() const override {
        std::vector<std::string> symbols;
        symbols.reserve(symbol_data_.size());
        for (const auto& [symbol, _] : symbol_data_) {
            symbols.push_back(symbol);
        }
        return symbols;
    }
    
    void shutdown() override {
        // Clean up if needed
        initialized_ = false;
    }
    
    void reset() override {
        if (!initialized_) return;
        
        // Reset all indices to beginning
        for (auto& [symbol, index] : current_indices_) {
            index = 0;
        }
        
        // Clear and rebuild priority queue
        while (!time_queue_.empty()) {
            time_queue_.pop();
        }
        
        for (const auto& [symbol, bars] : symbol_data_) {
            if (!bars.empty()) {
                time_queue_.push({bars[0].timestamp, symbol, 0});
            }
        }
        
        latest_bars_.clear();
        total_bars_processed_ = 0;
    }
    
    // Additional utility methods
    size_t getTotalBarsLoaded() const {
        size_t total = 0;
        for (const auto& [_, bars] : symbol_data_) {
            total += bars.size();
        }
        return total;
    }
    
    size_t getBarsProcessed() const {
        return total_bars_processed_;
    }
    
    std::pair<std::chrono::nanoseconds, std::chrono::nanoseconds> 
    getDateRange(const std::string& symbol) const {
        auto it = symbol_data_.find(symbol);
        if (it == symbol_data_.end() || it->second.empty()) {
            return {std::chrono::nanoseconds(0), std::chrono::nanoseconds(0)};
        }
        
        const auto& bars = it->second;
        return {bars.front().timestamp, bars.back().timestamp};
    }
};

} // namespace backtesting