// test_phase4_performance.cpp
// Comprehensive Performance Benchmarking Suite for Phase 4 Optimizations
// Tests memory pools, SIMD operations, and optimized rolling statistics

#include <iostream>
#include <iomanip>
#include <chrono>
#include <vector>
#include <random>
#include <string>
#include <functional>

#include "../include/concurrent/memory_pool.hpp"
#include "../include/math/simd_math.hpp"
#include "../include/core/branch_hints.hpp"
#include "../include/strategies/simd_rolling_statistics.hpp"
#include "../include/strategies/rolling_statistics.hpp"

using namespace backtesting;
using namespace std::chrono;

// ============================================================================
// Performance Benchmark Utilities
// ============================================================================

class BenchmarkTimer {
private:
    std::string name_;
    high_resolution_clock::time_point start_;
    
public:
    explicit BenchmarkTimer(const std::string& name) : name_(name) {
        start_ = high_resolution_clock::now();
    }
    
    ~BenchmarkTimer() {
        auto end = high_resolution_clock::now();
        auto duration = duration_cast<microseconds>(end - start_);
        std::cout << "  " << std::left << std::setw(40) << name_ 
                  << std::right << std::setw(12) << duration.count() << " μs\n";
    }
};

template<typename Func>
double benchmark(const std::string& name, Func&& func, int iterations = 1) {
    auto start = high_resolution_clock::now();
    
    for (int i = 0; i < iterations; ++i) {
        func();
    }
    
    auto end = high_resolution_clock::now();
    auto duration = duration_cast<microseconds>(end - start).count();
    double avg = static_cast<double>(duration) / iterations;
    
    std::cout << "  " << std::left << std::setw(40) << name 
              << std::right << std::setw(12) << std::fixed << std::setprecision(2) 
              << avg << " μs (avg over " << iterations << " runs)\n";
    
    return avg;
}

// ============================================================================
// Test Data Generators
// ============================================================================

std::vector<double> generate_random_data(size_t size, double mean = 100.0, double stddev = 10.0) {
    std::vector<double> data(size);
    std::mt19937 gen(42);
    std::normal_distribution<> dist(mean, stddev);
    
    for (auto& val : data) {
        val = dist(gen);
    }
    
    return data;
}

std::vector<double> generate_correlated_data(const std::vector<double>& base, double correlation) {
    std::vector<double> data(base.size());
    std::mt19937 gen(123);
    std::normal_distribution<> dist(0, 1);
    
    double uncorr_weight = std::sqrt(1 - correlation * correlation);
    
    for (size_t i = 0; i < base.size(); ++i) {
        data[i] = correlation * base[i] + uncorr_weight * dist(gen);
    }
    
    return data;
}

// ============================================================================
// Phase 4.1: Memory Pool Benchmarks
// ============================================================================

struct TestEvent {
    double price;
    double volume;
    int64_t timestamp;
    char symbol[8];
};

void benchmark_memory_pool() {
    std::cout << "\n" << std::string(70, '=') << "\n";
    std::cout << "PHASE 4.1: MEMORY POOL PERFORMANCE\n";
    std::cout << std::string(70, '=') << "\n\n";
    
    const int num_allocations = 100000;
    
    // Baseline: raw new/delete
    {
        BenchmarkTimer timer("Raw new/delete (baseline)");
        for (int i = 0; i < num_allocations; ++i) {
            auto* obj = new TestEvent();
            obj->price = 100.0 + i;
            delete obj;
        }
    }
    
    // Enhanced memory pool
    {
        EnhancedMemoryPool<TestEvent, 4096> pool;
        BenchmarkTimer timer("Enhanced Memory Pool");
        
        for (int i = 0; i < num_allocations; ++i) {
            auto* obj = pool.acquire();
            obj->price = 100.0 + i;
            pool.release(obj);
        }
        
        auto stats = pool.getStats();
        std::cout << "\n  Pool Statistics:\n";
        std::cout << "    Allocations: " << stats.allocations << "\n";
        std::cout << "    Hit Rate: " << std::fixed << std::setprecision(2) 
                  << stats.hit_rate_pct << "%\n";
        std::cout << "    Peak Usage: " << stats.peak_usage << " / " << pool.capacity() 
                  << " (" << stats.utilization_pct << "%)\n\n";
    }
    
    // Batch allocation test
    {
        EnhancedMemoryPool<TestEvent, 4096> pool;
        BenchmarkTimer timer("Batch Allocation (1000 at a time)");
        
        for (int batch = 0; batch < num_allocations / 1000; ++batch) {
            auto objects = pool.acquire_batch(1000);
            pool.release_batch(objects);
        }
    }
}

// ============================================================================
// Phase 4.2: SIMD Operations Benchmarks
// ============================================================================

void benchmark_simd_operations() {
    std::cout << "\n" << std::string(70, '=') << "\n";
    std::cout << "PHASE 4.2: SIMD OPERATIONS PERFORMANCE\n";
    std::cout << std::string(70, '=') << "\n\n";
    
    const size_t data_size = 10000;
    auto data1 = generate_random_data(data_size);
    auto data2 = generate_random_data(data_size);
    std::vector<double> result(data_size);
    
    std::cout << "Data Size: " << data_size << " elements\n";
    std::cout << "NEON Support: " << (HAS_NEON ? "YES" : "NO") << "\n\n";
    
    // Vector addition
    {
        BenchmarkTimer timer("SIMD Vector Addition");
        for (int i = 0; i < 100; ++i) {
            simd::VectorOps::add(data1.data(), data2.data(), result.data(), data_size);
        }
    }
    
    // Vector multiplication
    {
        BenchmarkTimer timer("SIMD Vector Multiplication");
        for (int i = 0; i < 100; ++i) {
            simd::VectorOps::multiply(data1.data(), data2.data(), result.data(), data_size);
        }
    }
    
    // Sum operation
    {
        double sum = 0.0;
        BenchmarkTimer timer("SIMD Sum");
        for (int i = 0; i < 100; ++i) {
            sum = simd::VectorOps::sum(data1.data(), data_size);
        }
        std::cout << "    Final sum: " << sum << "\n";
    }
    
    // Dot product
    {
        double dot = 0.0;
        BenchmarkTimer timer("SIMD Dot Product");
        for (int i = 0; i < 100; ++i) {
            dot = simd::VectorOps::dot_product(data1.data(), data2.data(), data_size);
        }
        std::cout << "    Final dot product: " << dot << "\n";
    }
    
    // Mean and variance
    {
        BenchmarkTimer timer("SIMD Mean & Variance");
        for (int i = 0; i < 100; ++i) {
            auto [mean, var, std_dev] = simd::StatisticalOps::mean_variance(data1.data(), data_size);
        }
    }
    
    // Z-score normalization
    {
        BenchmarkTimer timer("SIMD Z-Score Normalization");
        for (int i = 0; i < 100; ++i) {
            simd::StatisticalOps::z_score_normalize(data1.data(), result.data(), data_size);
        }
    }
    
    // Correlation
    {
        double corr = 0.0;
        BenchmarkTimer timer("SIMD Correlation");
        for (int i = 0; i < 100; ++i) {
            corr = simd::StatisticalOps::correlation(data1.data(), data2.data(), data_size);
        }
        std::cout << "    Correlation: " << std::fixed << std::setprecision(4) << corr << "\n";
    }
}

// ============================================================================
// Phase 4.3: Branch Prediction Benchmarks
// ============================================================================

void benchmark_branch_prediction() {
    std::cout << "\n" << std::string(70, '=') << "\n";
    std::cout << "PHASE 4.3: BRANCH PREDICTION OPTIMIZATION\n";
    std::cout << std::string(70, '=') << "\n\n";
    
    const int iterations = 1000000;
    std::vector<int> data(iterations);
    
    // Generate mostly positive data (90% positive)
    std::mt19937 gen(42);
    std::uniform_real_distribution<> dist(0, 1);
    for (auto& val : data) {
        val = (dist(gen) < 0.9) ? 1 : -1;
    }
    
    // Without branch hints
    {
        int count = 0;
        BenchmarkTimer timer("Without LIKELY hint");
        for (int val : data) {
            if (val > 0) {
                count++;
            }
        }
        std::cout << "    Positive count: " << count << "\n";
    }
    
    // With branch hints
    {
        int count = 0;
        BenchmarkTimer timer("With LIKELY hint");
        for (int val : data) {
            if (LIKELY(val > 0)) {
                count++;
            }
        }
        std::cout << "    Positive count: " << count << "\n";
    }
    
    // Branchless min/max
    {
        int min_val = data[0];
        int max_val = data[0];
        BenchmarkTimer timer("Branchless Min/Max");
        for (int val : data) {
            min_val = BranchlessOps::min(min_val, val);
            max_val = BranchlessOps::max(max_val, val);
        }
        std::cout << "    Min: " << min_val << ", Max: " << max_val << "\n";
    }
}

// ============================================================================
// Phase 4.4: Rolling Statistics Comparison
// ============================================================================

void benchmark_rolling_statistics() {
    std::cout << "\n" << std::string(70, '=') << "\n";
    std::cout << "PHASE 4.4: ROLLING STATISTICS COMPARISON\n";
    std::cout << std::string(70, '=') << "\n\n";
    
    const size_t window = 60;
    const int num_updates = 10000;
    auto test_data = generate_random_data(num_updates);
    
    std::cout << "Window Size: " << window << "\n";
    std::cout << "Updates: " << num_updates << "\n\n";
    
    // Original implementation
    {
        RollingStatistics stats(window);
        BenchmarkTimer timer("Original RollingStatistics");
        
        for (double val : test_data) {
            stats.update(val);
        }
        
        std::cout << "    Final mean: " << stats.getMean() << "\n";
        std::cout << "    Final std dev: " << stats.getStdDev() << "\n";
    }
    
    // SIMD-optimized implementation
    {
        SIMDRollingStatistics stats(window);
        BenchmarkTimer timer("SIMD RollingStatistics");
        
        for (double val : test_data) {
            stats.update(val);
        }
        
        std::cout << "    Final mean: " << stats.getMean() << "\n";
        std::cout << "    Final std dev: " << stats.getStdDev() << "\n";
    }
    
    // Rolling correlation comparison
    auto data2 = generate_correlated_data(test_data, 0.7);
    
    {
        RollingCorrelation corr(window);
        BenchmarkTimer timer("Original RollingCorrelation");
        
        for (size_t i = 0; i < test_data.size(); ++i) {
            corr.update(test_data[i], data2[i]);
        }
        
        std::cout << "    Final correlation: " << corr.getCorrelation() << "\n";
    }
    
    {
        SIMDRollingCorrelation corr(window);
        BenchmarkTimer timer("SIMD RollingCorrelation");
        
        for (size_t i = 0; i < test_data.size(); ++i) {
            corr.update(test_data[i], data2[i]);
        }
        
        std::cout << "    Final correlation: " << corr.getCorrelation() << "\n";
    }
}

// ============================================================================
// Comprehensive Performance Report
// ============================================================================

void generate_performance_report() {
    std::cout << "\n" << std::string(70, '=') << "\n";
    std::cout << "PHASE 4 OPTIMIZATION SUMMARY\n";
    std::cout << std::string(70, '=') << "\n\n";
    
    std::cout << "System Information:\n";
    std::cout << "  Compiler: " << 
#if defined(__clang__)
        "Clang " << __clang_major__ << "." << __clang_minor__ << "\n";
#elif defined(__GNUC__)
        "GCC " << __GNUC__ << "." << __GNUC_MINOR__ << "\n";
#else
        "Unknown\n";
#endif
    
    std::cout << "  CPU Architecture: ";
#if defined(__aarch64__)
    std::cout << "ARM64 (Apple Silicon)\n";
#elif defined(__x86_64__)
    std::cout << "x86-64\n";
#else
    std::cout << "Unknown\n";
#endif
    
    std::cout << "  SIMD Support: " << (HAS_NEON ? "ARM NEON" : "Scalar only") << "\n";
    std::cout << "  Cache Line Size: 64 bytes\n\n";
    
    std::cout << "Optimization Features Implemented:\n";
    std::cout << "  ✓ Lock-free memory pool with thread-local caching\n";
    std::cout << "  ✓ SIMD-vectorized mathematical operations\n";
    std::cout << "  ✓ Branch prediction hints (LIKELY/UNLIKELY)\n";
    std::cout << "  ✓ Cache-aligned data structures\n";
    std::cout << "  ✓ Hot path optimization with FORCE_INLINE\n";
    std::cout << "  ✓ Branchless conditional operations\n\n";
}

// ============================================================================
// Main Benchmark Runner
// ============================================================================

int main() {
    std::cout << "\n" << std::string(70, '=') << "\n";
    std::cout << "   PHASE 4: PERFORMANCE OPTIMIZATION BENCHMARK SUITE\n";
    std::cout << std::string(70, '=') << "\n";
    
    generate_performance_report();
    
    try {
        benchmark_memory_pool();
        benchmark_simd_operations();
        benchmark_branch_prediction();
        benchmark_rolling_statistics();
        
        std::cout << "\n" << std::string(70, '=') << "\n";
        std::cout << "ALL BENCHMARKS COMPLETED SUCCESSFULLY ✓\n";
        std::cout << std::string(70, '=') << "\n\n";
        
        std::cout << "Next Steps:\n";
        std::cout << "  • Profile your specific use case with real market data\n";
        std::cout << "  • Measure end-to-end system latency\n";
        std::cout << "  • Compare with Phase 3 baseline performance\n";
        std::cout << "  • Fine-tune parameters based on your workload\n\n";
        
    } catch (const std::exception& e) {
        std::cerr << "\nError during benchmarking: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}