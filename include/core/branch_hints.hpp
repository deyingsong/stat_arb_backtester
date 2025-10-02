// branch_hints.hpp
// Branch Prediction Hints and Optimization Utilities
// Compiler-specific hints for hot path optimization

#pragma once

#include <cstdint>

namespace backtesting {

// ============================================================================
// Branch Prediction Hints
// ============================================================================

// Likely/Unlikely hints for branch prediction
#if defined(__GNUC__) || defined(__clang__)
    #define LIKELY(x)   __builtin_expect(!!(x), 1)
    #define UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
    #define LIKELY(x)   (x)
    #define UNLIKELY(x) (x)
#endif

// Force inline for hot path functions
#if defined(__GNUC__) || defined(__clang__)
    #define FORCE_INLINE __attribute__((always_inline)) inline
#elif defined(_MSC_VER)
    #define FORCE_INLINE __forceinline
#else
    #define FORCE_INLINE inline
#endif

// Prevent inlining for cold path functions
#if defined(__GNUC__) || defined(__clang__)
    #define NO_INLINE __attribute__((noinline))
#elif defined(_MSC_VER)
    #define NO_INLINE __declspec(noinline)
#else
    #define NO_INLINE
#endif

// Prefetch hints for memory access
#if defined(__GNUC__) || defined(__clang__)
    #define PREFETCH_READ(addr)  __builtin_prefetch((addr), 0, 3)
    #define PREFETCH_WRITE(addr) __builtin_prefetch((addr), 1, 3)
#else
    #define PREFETCH_READ(addr)  ((void)0)
    #define PREFETCH_WRITE(addr) ((void)0)
#endif

// ============================================================================
// Branchless Operations
// ============================================================================

class BranchlessOps {
public:
    // Branchless min
    static FORCE_INLINE int min(int a, int b) {
        return b + ((a - b) & ((a - b) >> 31));
    }
    
    static FORCE_INLINE double min(double a, double b) {
        return (a < b) ? a : b;  // Modern compilers optimize this
    }
    
    // Branchless max
    static FORCE_INLINE int max(int a, int b) {
        return a - ((a - b) & ((a - b) >> 31));
    }
    
    static FORCE_INLINE double max(double a, double b) {
        return (a > b) ? a : b;
    }
    
    // Branchless abs
    static FORCE_INLINE int abs(int x) {
        int mask = x >> 31;
        return (x + mask) ^ mask;
    }
    
    // Branchless sign (-1, 0, or 1)
    static FORCE_INLINE int sign(int x) {
        return (x > 0) - (x < 0);
    }
    
    // Branchless clamp
    static FORCE_INLINE int clamp(int x, int low, int high) {
        return max(low, min(x, high));
    }
    
    static FORCE_INLINE double clamp(double x, double low, double high) {
        return max(low, min(x, high));
    }
    
    // Conditional move (cmov) - select without branching
    template<typename T>
    static FORCE_INLINE T select(bool condition, T if_true, T if_false) {
        // Modern compilers will generate cmov instruction
        return condition ? if_true : if_false;
    }
};

// ============================================================================
// Hot Path Validation (Optimized for Common Case)
// ============================================================================

class FastValidation {
public:
    // Fast range check optimized for values typically in range
    static FORCE_INLINE bool is_in_range(double value, double low, double high) {
        // Typically true case - optimize for this
        if (LIKELY(value >= low && value <= high)) {
            return true;
        }
        return false;
    }
    
    // Fast positive check (common in financial data)
    static FORCE_INLINE bool is_positive(double value) {
        return LIKELY(value > 0.0);
    }
    
    // Fast finite check (usually true)
    static FORCE_INLINE bool is_finite(double value) {
        // Check for NaN or Inf - uncommon case
        if (UNLIKELY(!std::isfinite(value))) {
            return false;
        }
        return true;
    }
    
    // Fast OHLC validation (typically valid)
    static FORCE_INLINE bool validate_ohlc(double open, double high, double low, double close) {
        // Common case: valid OHLC
        if (LIKELY(high >= low && high >= open && high >= close && 
                   low <= open && low <= close)) {
            return true;
        }
        return false;
    }
};

// ============================================================================
// Cache-Line Padding Utility
// ============================================================================

template<typename T>
struct CacheLinePadded {
    static constexpr size_t CACHE_LINE = 64;
    static constexpr size_t PADDING = CACHE_LINE - (sizeof(T) % CACHE_LINE);
    
    T value;
    char padding[PADDING];
    
    CacheLinePadded() : value{} {}
    explicit CacheLinePadded(const T& v) : value{v} {}
    
    operator T&() { return value; }
    operator const T&() const { return value; }
    
    T& get() { return value; }
    const T& get() const { return value; }
};

// ============================================================================
// Performance Hint Macros
// ============================================================================

// Mark hot functions for optimization
#if defined(__GNUC__) || defined(__clang__)
    #define HOT_FUNCTION __attribute__((hot))
    #define COLD_FUNCTION __attribute__((cold))
#else
    #define HOT_FUNCTION
    #define COLD_FUNCTION
#endif

// Alignment hints
#if defined(__GNUC__) || defined(__clang__)
    #define ALIGN_CACHE __attribute__((aligned(64)))
    #define ALIGN_16    __attribute__((aligned(16)))
#elif defined(_MSC_VER)
    #define ALIGN_CACHE __declspec(align(64))
    #define ALIGN_16    __declspec(align(16))
#else
    #define ALIGN_CACHE
    #define ALIGN_16
#endif

// Restrict pointer (no aliasing)
#if defined(__GNUC__) || defined(__clang__)
    #define RESTRICT __restrict__
#elif defined(_MSC_VER)
    #define RESTRICT __restrict
#else
    #define RESTRICT
#endif

} // namespace backtesting