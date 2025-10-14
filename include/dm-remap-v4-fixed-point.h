/*
 * dm-remap v4.0 Health Monitoring - Fixed-Point Math Helpers
 * 
 * Integer-only math functions to replace floating-point operations
 * in kernel code. Uses 64-bit integers with scaling for precision.
 * 
 * Author: dm-remap development team
 * Date: October 14, 2025
 */

#ifndef DM_REMAP_V4_FIXED_POINT_H
#define DM_REMAP_V4_FIXED_POINT_H

#include <linux/types.h>
#include <linux/math64.h>

/* Fixed-point scaling factor (1 million for 6 decimal places of precision) */
#define FP_SCALE 1000000LL
#define FP_ONE (1LL * FP_SCALE)

/* Convert integer to fixed-point */
static inline int64_t int_to_fp(int32_t x) {
    return (int64_t)x * FP_SCALE;
}

/* Convert fixed-point to integer (with rounding) */
static inline int32_t fp_to_int(int64_t x) {
    if (x >= 0)
        return (int32_t)((x + FP_SCALE/2) / FP_SCALE);
    else
        return (int32_t)((x - FP_SCALE/2) / FP_SCALE);
}

/* Fixed-point multiply */
static inline int64_t fp_mul(int64_t a, int64_t b) {
    return div64_s64(a * b, FP_SCALE);
}

/* Fixed-point divide */
static inline int64_t fp_div(int64_t a, int64_t b) {
    if (b == 0)
        return 0;
    return div64_s64(a * FP_SCALE, b);
}

/* Absolute value for 64-bit integers */
static inline int64_t abs64(int64_t x) {
    return (x < 0) ? -x : x;
}

/* Integer square root (already in kernel as int_sqrt64) */
static inline uint32_t isqrt64(uint64_t x) {
    return int_sqrt64(x);
}

/*
 * Simple linear interpolation for predictions
 * Returns: predicted_value = current + (slope * steps)
 */
static inline int32_t predict_linear(int32_t current, int32_t slope, int32_t steps) {
    int64_t prediction = (int64_t)current + ((int64_t)slope * steps);
    
    /* Clamp to valid health score range [0, 100] */
    if (prediction < 0) return 0;
    if (prediction > 100) return 100;
    
    return (int32_t)prediction;
}

/*
 * Calculate simple moving average
 */
static inline int32_t moving_average(const int32_t *values, uint32_t count) {
    int64_t sum = 0;
    uint32_t i;
    
    if (count == 0)
        return 0;
    
    for (i = 0; i < count; i++) {
        sum += values[i];
    }
    
    return (int32_t)(sum / count);
}

/*
 * Calculate variance (without square root for standard deviation)
 * This avoids needing sqrtf() - variance alone is sufficient for most uses
 */
static inline uint64_t calculate_variance(const int32_t *values, uint32_t count, int32_t mean) {
    uint64_t sum_squared_diff = 0;
    uint32_t i;
    int64_t diff;
    
    if (count <= 1)
        return 0;
    
    for (i = 0; i < count; i++) {
        diff = values[i] - mean;
        sum_squared_diff += (uint64_t)(diff * diff);
    }
    
    return sum_squared_diff / (count - 1);
}

#endif /* DM_REMAP_V4_FIXED_POINT_H */
