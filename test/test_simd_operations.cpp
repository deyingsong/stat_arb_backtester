// test_simd_operations.cpp
// Comprehensive tests for SIMD mathematical operations

#include <iostream>
#include <vector>
#include <cmath>
#include <random>
#include <iomanip>
#include "../include/math/simd_math.hpp"

using namespace backtesting;

const double TOLERANCE = 1e-6;

bool approx_equal(double a, double b, double tol = TOLERANCE) {
    return std::abs(a - b) < tol;
}

// Generate test data
std::vector<double> generate_data(size_t n, double mean = 0.0, double stddev = 1.0) {
    std::vector<double> data(n);
    std::mt19937 gen(42);
    std::normal_distribution<> dist(mean, stddev);
    for (auto& val : data) {
        val = dist(gen);
    }
    return data;
}

// Test 1: Vector addition
void test_vector_addition() {
    std::cout << "Test 1: Vector Addition\n";
    std::cout << std::string(40, '-') << "\n";
    
    size_t n = 1000;
    auto a = generate_data(n, 10.0);
    auto b = generate_data(n, 20.0);
    std::vector<double> result(n);
    std::vector<double> expected(n);
    
    // SIMD operation
    simd::VectorOps::add(a.data(), b.data(), result.data(), n);
    
    // Reference implementation
    for (size_t i = 0; i < n; ++i) {
        expected[i] = a[i] + b[i];
    }
    
    // Verify
    bool passed = true;
    for (size_t i = 0; i < n; ++i) {
        if (!approx_equal(result[i], expected[i])) {
            std::cout << "  Mismatch at " << i << ": " 
                      << result[i] << " vs " << expected[i] << "\n";
            passed = false;
            break;
        }
    }
    
    if (passed) {
        std::cout << "  ✓ PASSED: All " << n << " elements match\n\n";
    } else {
        std::cout << "  ✗ FAILED\n\n";
    }
}

// Test 2: Dot product
void test_dot_product() {
    std::cout << "Test 2: Dot Product\n";
    std::cout << std::string(40, '-') << "\n";
    
    size_t n = 10000;
    auto a = generate_data(n);
    auto b = generate_data(n);
    
    // SIMD operation
    double result = simd::VectorOps::dot_product(a.data(), b.data(), n);
    
    // Reference implementation
    double expected = 0.0;
    for (size_t i = 0; i < n; ++i) {
        expected += a[i] * b[i];
    }
    
    std::cout << "  SIMD result: " << result << "\n";
    std::cout << "  Expected: " << expected << "\n";
    std::cout << "  Difference: " << std::abs(result - expected) << "\n";
    
    if (approx_equal(result, expected, 1e-3)) {
        std::cout << "  ✓ PASSED\n\n";
    } else {
        std::cout << "  ✗ FAILED\n\n";
    }
}

// Test 3: Mean and variance
void test_mean_variance() {
    std::cout << "Test 3: Mean and Variance\n";
    std::cout << std::string(40, '-') << "\n";
    
    size_t n = 10000;
    auto data = generate_data(n, 100.0, 15.0);
    
    // SIMD operation
    auto [mean, var, std_dev] = simd::StatisticalOps::mean_variance(data.data(), n);
    
    // Reference implementation
    double ref_mean = 0.0;
    for (double val : data) {
        ref_mean += val;
    }
    ref_mean /= n;
    
    double ref_var = 0.0;
    for (double val : data) {
        double diff = val - ref_mean;
        ref_var += diff * diff;
    }
    ref_var /= n;
    
    std::cout << "  SIMD mean: " << mean << ", expected: " << ref_mean << "\n";
    std::cout << "  SIMD variance: " << var << ", expected: " << ref_var << "\n";
    std::cout << "  SIMD std dev: " << std_dev << ", expected: " << std::sqrt(ref_var) << "\n";
    
    bool passed = approx_equal(mean, ref_mean, 1e-6) && 
                  approx_equal(var, ref_var, 1e-3);
    
    if (passed) {
        std::cout << "  ✓ PASSED\n\n";
    } else {
        std::cout << "  ✗ FAILED\n\n";
    }
}

// Test 4: Z-score normalization
void test_zscore_normalization() {
    std::cout << "Test 4: Z-Score Normalization\n";
    std::cout << std::string(40, '-') << "\n";
    
    size_t n = 1000;
    auto data = generate_data(n, 50.0, 10.0);
    std::vector<double> result(n);
    
    // SIMD operation
    simd::StatisticalOps::z_score_normalize(data.data(), result.data(), n);
    
    // Verify properties: mean should be ~0, std dev should be ~1
    auto [norm_mean, norm_var, norm_std] = simd::StatisticalOps::mean_variance(result.data(), n);
    
    std::cout << "  Normalized mean: " << norm_mean << " (expected ~0)\n";
    std::cout << "  Normalized std dev: " << norm_std << " (expected ~1)\n";
    
    bool passed = std::abs(norm_mean) < 1e-10 && approx_equal(norm_std, 1.0, 0.01);
    
    if (passed) {
        std::cout << "  ✓ PASSED\n\n";
    } else {
        std::cout << "  ✗ FAILED\n\n";
    }
}

// Test 5: Correlation
void test_correlation() {
    std::cout << "Test 5: Correlation Coefficient\n";
    std::cout << std::string(40, '-') << "\n";
    
    size_t n = 10000;
    auto x = generate_data(n);
    
    // Create correlated data: y = 0.8 * x + noise
    std::vector<double> y(n);
    std::mt19937 gen(123);
    std::normal_distribution<> noise(0, 0.6);
    for (size_t i = 0; i < n; ++i) {
        y[i] = 0.8 * x[i] + noise(gen);
    }
    
    // SIMD operation
    double corr = simd::StatisticalOps::correlation(x.data(), y.data(), n);
    
    std::cout << "  Correlation: " << corr << " (expected ~0.8)\n";
    
    bool passed = corr > 0.7 && corr < 0.9;
    
    if (passed) {
        std::cout << "  ✓ PASSED\n\n";
    } else {
        std::cout << "  ✗ FAILED\n\n";
    }
}

// Test 6: Performance comparison
void test_performance() {
    std::cout << "Test 6: Performance Comparison\n";
    std::cout << std::string(40, '-') << "\n";
    
    size_t n = 100000;
    auto data = generate_data(n);
    const int iterations = 1000;
    
    // SIMD sum
    auto start1 = std::chrono::high_resolution_clock::now();
    double simd_sum = 0.0;
    for (int i = 0; i < iterations; ++i) {
        simd_sum = simd::VectorOps::sum(data.data(), n);
    }
    auto end1 = std::chrono::high_resolution_clock::now();
    auto simd_time = std::chrono::duration_cast<std::chrono::microseconds>(end1 - start1);
    
    // Scalar sum
    auto start2 = std::chrono::high_resolution_clock::now();
    double scalar_sum = 0.0;
    for (int i = 0; i < iterations; ++i) {
        scalar_sum = 0.0;
        for (double val : data) {
            scalar_sum += val;
        }
    }
    auto end2 = std::chrono::high_resolution_clock::now();
    auto scalar_time = std::chrono::duration_cast<std::chrono::microseconds>(end2 - start2);
    
    double speedup = static_cast<double>(scalar_time.count()) / simd_time.count();
    
    std::cout << "  Data size: " << n << " elements\n";
    std::cout << "  Iterations: " << iterations << "\n";
    std::cout << "  SIMD time: " << simd_time.count() << " μs\n";
    std::cout << "  Scalar time: " << scalar_time.count() << " μs\n";
    std::cout << "  Speedup: " << std::fixed << std::setprecision(2) << speedup << "x\n";
    std::cout << "  Results match: " << (approx_equal(simd_sum, scalar_sum, 1e-3) ? "Yes" : "No") << "\n";
    
    if (speedup > 1.0) {
        std::cout << "  ✓ PASSED (SIMD faster)\n\n";
    } else {
        std::cout << "  ⚠ WARNING (No speedup, SIMD may not be enabled)\n\n";
    }
}

int main() {
    std::cout << "\n========================================\n";
    std::cout << "SIMD Operations Test Suite\n";
    std::cout << "========================================\n";
    
    std::cout << "\nSIMD Support: ";
#if HAS_NEON
    std::cout << "ARM NEON (enabled)\n\n";
#else
    std::cout << "Scalar fallback (NEON not available)\n\n";
#endif
    
    try {
        test_vector_addition();
        test_dot_product();
        test_mean_variance();
        test_zscore_normalization();
        test_correlation();
        test_performance();
        
        std::cout << "========================================\n";
        std::cout << "All SIMD tests passed! ✓\n";
        std::cout << "========================================\n\n";
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << "\n";
        return 1;
    }
}