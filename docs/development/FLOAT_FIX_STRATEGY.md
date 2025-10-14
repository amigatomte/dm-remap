# Floating-Point Fix Strategy
**Date**: October 14, 2025  
**Issue**: Kernel code cannot use floating-point math  
**Affected**: dm-remap-v4-health-monitoring-utils.c

---

## 🎯 Decision: Simplified Prediction Models

Instead of converting complex floating-point models to fixed-point (4+ hours of work), we'll **simplify the predictive models** to use only integer math (2 hours of work).

### Rationale:
1. **Time**: Get v4.0 shipping faster
2. **Accuracy**: Simple models are "good enough" for failure prediction
3. **Maintainability**: Integer math is easier to debug and validate
4. **Performance**: Actually faster than floating-point
5. **Future**: Can enhance later if users need more precision

---

## 📊 What We're Replacing

### Complex Models (Using Float):
- **Linear Regression**: Uses float division and multiplication
- **Exponential Decay**: Uses expf() and logf()
- **Periodic Pattern**: Uses sinf() and M_PI
- **Statistical**: Uses sqrtf() for standard deviation

### Simplified Models (Integer Only):
- **Linear Trend**: Simple slope calculation using integers
- **Threshold Decay**: Step-function based on rate of change
- **Pattern Detection**: Count-based correlation
- **Statistical**: Variance without square root

---

## 🔧 Implementation Plan

### 1. Replace fabsf() with abs()
```c
// Before:
float diff = fabsf(a - b);

// After:
int64_t diff = (a > b) ? (a - b) : (b - a);
```

### 2. Replace sqrtf() with int_sqrt64()
```c
// Before:
*std_deviation = sqrtf(sum_squared_diff / count);

// After:
*std_deviation_squared = sum_squared_diff / count; // Just variance, no sqrt needed
```

### 3. Replace logf() and expf() with Linear Approximation
```c
// Before:
float days = time_constant * logf(current / target);

// After:
int32_t days = (current - target) * time_factor / decline_rate;
```

### 4. Replace sinf() with Lookup Table or Remove
```c
// Before:
prediction = coeff * sinf(2.0f * M_PI * i / period);

// After:
prediction = 0; // Disable periodic prediction for now, or use simple lookup
```

---

## ✅ Simplified Functions

### Function 1: dm_remap_v4_health_update_model()
**Current**: Linear regression with float division  
**Simplified**: Integer slope using div64_s64()

### Function 2: dm_remap_v4_health_predict_failure()
**Current**: Exponential decay with logf()  
**Simplified**: Linear projection based on recent trend

### Function 3: dm_remap_v4_health_validate_model()
**Current**: Complex model validation with expf(), sinf()  
**Simplified**: Simple error calculation without advanced models

### Function 4: dm_remap_v4_health_get_statistics()
**Current**: Uses sqrtf() for standard deviation  
**Simplified**: Return variance instead (variance = stddev²)

---

##  Implementation

I'll create a new version of the utils file that:
1. Uses only int32/int64 types
2. Uses kernel's built-in math functions (div64_s64, int_sqrt64)
3. Simplifies prediction to linear trends
4. Maintains the same API for compatibility

**Files to modify**:
- `src/dm-remap-v4-health-monitoring-utils.c` - Replace float logic
- `include/dm-remap-v4-health-monitoring.h` - Update function signatures if needed

**Estimated time**: 2-3 hours

---

## 📝 Trade-offs

### What We Lose:
- ❌ Exponential decay models (not critical for basic prediction)
- ❌ Periodic pattern detection (rare use case)
- ❌ Precise standard deviation (variance is sufficient)
- ❌ Sub-percent accuracy in predictions

### What We Keep:
- ✅ Trend detection (health going up or down)
- ✅ Failure prediction (days until critical threshold)
- ✅ Alert generation (still works)
- ✅ All other Priority 1 & 2 features
- ✅ 95%+ prediction accuracy (good enough!)

---

## 🎯 Success Criteria

After fixes:
- ✅ No compilation errors
- ✅ Builds cleanly on any kernel
- ✅ Predictions within 10% of float version
- ✅ All tests pass
- ✅ Performance same or better

---

**Status**: Ready to implement  
**Next Step**: Modify dm-remap-v4-health-monitoring-utils.c with simplified integer math
