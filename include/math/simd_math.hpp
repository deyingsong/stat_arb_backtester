// simd_math.hpp
// SIMD-Optimized Mathematical Operations
// ARM NEON support for Apple Silicon with fallback implementations

#pragma once

#include <cmath>
#include <vector>
#include <algorithm>
#include <numeric>

// ARM NEON support for Apple Silicon
#if defined(__aarch64__) || defined(__ARM_NEON)
    #define HAS_NEON 1
    #include <arm_neon.h>
#else
    #define HAS_NEON 0
#endif

namespace backtesting {
namespace simd {

// ============================================================================
// SIMD Vector Operations
// ============================================================================

class VectorOps {
public:
    // Vector addition: result[i] = a[i] + b[i]
    static void add(const double* a, const double* b, double* result, size_t n) {
#if HAS_NEON
        size_t i = 0;
        // Process 2 doubles at a time with NEON
        for (; i + 1 < n; i += 2) {
            float64x2_t va = vld1q_f64(a + i);
            float64x2_t vb = vld1q_f64(b + i);
            float64x2_t vr = vaddq_f64(va, vb);
            vst1q_f64(result + i, vr);
        }
        // Handle remaining elements
        for (; i < n; ++i) {
            result[i] = a[i] + b[i];
        }
#else
        for (size_t i = 0; i < n; ++i) {
            result[i] = a[i] + b[i];
        }
#endif
    }
    
    // Vector subtraction: result[i] = a[i] - b[i]
    static void subtract(const double* a, const double* b, double* result, size_t n) {
#if HAS_NEON
        size_t i = 0;
        for (; i + 1 < n; i += 2) {
            float64x2_t va = vld1q_f64(a + i);
            float64x2_t vb = vld1q_f64(b + i);
            float64x2_t vr = vsubq_f64(va, vb);
            vst1q_f64(result + i, vr);
        }
        for (; i < n; ++i) {
            result[i] = a[i] - b[i];
        }
#else
        for (size_t i = 0; i < n; ++i) {
            result[i] = a[i] - b[i];
        }
#endif
    }
    
    // Vector multiplication: result[i] = a[i] * b[i]
    static void multiply(const double* a, const double* b, double* result, size_t n) {
#if HAS_NEON
        size_t i = 0;
        for (; i + 1 < n; i += 2) {
            float64x2_t va = vld1q_f64(a + i);
            float64x2_t vb = vld1q_f64(b + i);
            float64x2_t vr = vmulq_f64(va, vb);
            vst1q_f64(result + i, vr);
        }
        for (; i < n; ++i) {
            result[i] = a[i] * b[i];
        }
#else
        for (size_t i = 0; i < n; ++i) {
            result[i] = a[i] * b[i];
        }
#endif
    }
    
    // Scalar multiplication: result[i] = a[i] * scalar
    static void multiply_scalar(const double* a, double scalar, double* result, size_t n) {
#if HAS_NEON
        size_t i = 0;
        float64x2_t vs = vdupq_n_f64(scalar);
        for (; i + 1 < n; i += 2) {
            float64x2_t va = vld1q_f64(a + i);
            float64x2_t vr = vmulq_f64(va, vs);
            vst1q_f64(result + i, vr);
        }
        for (; i < n; ++i) {
            result[i] = a[i] * scalar;
        }
#else
        for (size_t i = 0; i < n; ++i) {
            result[i] = a[i] * scalar;
        }
#endif
    }
    
    // Sum of vector elements
    static double sum(const double* data, size_t n) {
#if HAS_NEON
        size_t i = 0;
        float64x2_t vsum = vdupq_n_f64(0.0);
        
        // Process 2 elements at a time
        for (; i + 1 < n; i += 2) {
            float64x2_t v = vld1q_f64(data + i);
            vsum = vaddq_f64(vsum, v);
        }
        
        // Horizontal add
        double result = vgetq_lane_f64(vsum, 0) + vgetq_lane_f64(vsum, 1);
        
        // Handle remaining elements
        for (; i < n; ++i) {
            result += data[i];
        }
        
        return result;
#else
        return std::accumulate(data, data + n, 0.0);
#endif
    }
    
    // Dot product: sum(a[i] * b[i])
    static double dot_product(const double* a, const double* b, size_t n) {
#if HAS_NEON
        size_t i = 0;
        float64x2_t vsum = vdupq_n_f64(0.0);
        
        for (; i + 1 < n; i += 2) {
            float64x2_t va = vld1q_f64(a + i);
            float64x2_t vb = vld1q_f64(b + i);
            vsum = vfmaq_f64(vsum, va, vb);  // Fused multiply-add
        }
        
        double result = vgetq_lane_f64(vsum, 0) + vgetq_lane_f64(vsum, 1);
        
        for (; i < n; ++i) {
            result += a[i] * b[i];
        }
        
        return result;
#else
        double result = 0.0;
        for (size_t i = 0; i < n; ++i) {
            result += a[i] * b[i];
        }
        return result;
#endif
    }
    
    // Mean of vector
    static double mean(const double* data, size_t n) {
        if (n == 0) return 0.0;
        return sum(data, n) / static_cast<double>(n);
    }
    
    // Variance (biased estimator)
    static double variance(const double* data, size_t n, double mean_val) {
        if (n == 0) return 0.0;
        
#if HAS_NEON
        size_t i = 0;
        float64x2_t vmean = vdupq_n_f64(mean_val);
        float64x2_t vsum = vdupq_n_f64(0.0);
        
        for (; i + 1 < n; i += 2) {
            float64x2_t v = vld1q_f64(data + i);
            float64x2_t diff = vsubq_f64(v, vmean);
            vsum = vfmaq_f64(vsum, diff, diff);  // sum += diff * diff
        }
        
        double result = vgetq_lane_f64(vsum, 0) + vgetq_lane_f64(vsum, 1);
        
        for (; i < n; ++i) {
            double diff = data[i] - mean_val;
            result += diff * diff;
        }
        
        return result / static_cast<double>(n);
#else
        double sum_sq_diff = 0.0;
        for (size_t i = 0; i < n; ++i) {
            double diff = data[i] - mean_val;
            sum_sq_diff += diff * diff;
        }
        return sum_sq_diff / static_cast<double>(n);
#endif
    }
    
    // Standard deviation
    static double std_dev(const double* data, size_t n, double mean_val) {
        return std::sqrt(variance(data, n, mean_val));
    }
};

// ============================================================================
// SIMD-Optimized Statistical Functions
// ============================================================================

class StatisticalOps {
public:
    // Calculate mean and variance in one pass (more efficient)
    struct MeanVariance {
        double mean;
        double variance;
        double std_dev;
    };
    
    static MeanVariance mean_variance(const double* data, size_t n) {
        if (n == 0) return {0.0, 0.0, 0.0};
        
        double mean = VectorOps::mean(data, n);
        double var = VectorOps::variance(data, n, mean);
        
        return {mean, var, std::sqrt(var)};
    }
    
    // Z-score normalization: (x - mean) / std_dev
    static void z_score_normalize(const double* data, double* result, size_t n) {
        if (n == 0) return;
        
        auto [mean, var, std_dev] = mean_variance(data, n);
        
        if (std_dev < 1e-10) {
            // Avoid division by zero
            std::fill(result, result + n, 0.0);
            return;
        }
        
#if HAS_NEON
        size_t i = 0;
        float64x2_t vmean = vdupq_n_f64(mean);
        float64x2_t vinv_std = vdupq_n_f64(1.0 / std_dev);
        
        for (; i + 1 < n; i += 2) {
            float64x2_t v = vld1q_f64(data + i);
            float64x2_t diff = vsubq_f64(v, vmean);
            float64x2_t z = vmulq_f64(diff, vinv_std);
            vst1q_f64(result + i, z);
        }
        
        for (; i < n; ++i) {
            result[i] = (data[i] - mean) / std_dev;
        }
#else
        for (size_t i = 0; i < n; ++i) {
            result[i] = (data[i] - mean) / std_dev;
        }
#endif
    }
    
    // Exponential moving average (EMA)
    static void ema(const double* data, double* result, size_t n, double alpha) {
        if (n == 0) return;
        
        double beta = 1.0 - alpha;
        result[0] = data[0];
        
        for (size_t i = 1; i < n; ++i) {
            result[i] = alpha * data[i] + beta * result[i - 1];
        }
    }
    
    // Correlation coefficient
    static double correlation(const double* x, const double* y, size_t n) {
        if (n < 2) return 0.0;
        
        double mean_x = VectorOps::mean(x, n);
        double mean_y = VectorOps::mean(y, n);
        
        double sum_xy = 0.0, sum_xx = 0.0, sum_yy = 0.0;
        
#if HAS_NEON
        size_t i = 0;
        float64x2_t vmean_x = vdupq_n_f64(mean_x);
        float64x2_t vmean_y = vdupq_n_f64(mean_y);
        float64x2_t vsum_xy = vdupq_n_f64(0.0);
        float64x2_t vsum_xx = vdupq_n_f64(0.0);
        float64x2_t vsum_yy = vdupq_n_f64(0.0);
        
        for (; i + 1 < n; i += 2) {
            float64x2_t vx = vld1q_f64(x + i);
            float64x2_t vy = vld1q_f64(y + i);
            
            float64x2_t dx = vsubq_f64(vx, vmean_x);
            float64x2_t dy = vsubq_f64(vy, vmean_y);
            
            vsum_xy = vfmaq_f64(vsum_xy, dx, dy);
            vsum_xx = vfmaq_f64(vsum_xx, dx, dx);
            vsum_yy = vfmaq_f64(vsum_yy, dy, dy);
        }
        
        sum_xy = vgetq_lane_f64(vsum_xy, 0) + vgetq_lane_f64(vsum_xy, 1);
        sum_xx = vgetq_lane_f64(vsum_xx, 0) + vgetq_lane_f64(vsum_xx, 1);
        sum_yy = vgetq_lane_f64(vsum_yy, 0) + vgetq_lane_f64(vsum_yy, 1);
        
        for (; i < n; ++i) {
            double dx = x[i] - mean_x;
            double dy = y[i] - mean_y;
            sum_xy += dx * dy;
            sum_xx += dx * dx;
            sum_yy += dy * dy;
        }
#else
        for (size_t i = 0; i < n; ++i) {
            double dx = x[i] - mean_x;
            double dy = y[i] - mean_y;
            sum_xy += dx * dy;
            sum_xx += dx * dx;
            sum_yy += dy * dy;
        }
#endif
        
        double denominator = std::sqrt(sum_xx * sum_yy);
        return (denominator > 1e-10) ? (sum_xy / denominator) : 0.0;
    }
};

} // namespace simd
} // namespace backtesting