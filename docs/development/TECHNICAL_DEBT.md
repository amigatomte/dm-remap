# Technical Debt Tracking - dm-remap v4.0
**Status:** Active  
**Last Updated:** October 14, 2025

## Overview

This document tracks technical debt items that need resolution before v4.0 release. Items are categorized by priority and estimated effort.

## 🔴 Critical (Blocks Release)

### 1. Floating-Point Math in Kernel Code
**Module:** `dm-remap-v4-health-monitoring-utils.c`  
**Severity:** CRITICAL  
**Effort:** 3-4 hours

**Issue:**
Kernel code uses floating-point math operations (`fabsf`, `logf`, `expf`, `sinf`, `sqrtf`) which are not allowed in kernel space. Causes compilation errors with SSE disabled.

**Affected Functions:**
- `dm_remap_v4_health_update_model()` - Lines 258, 454
- `dm_remap_v4_health_predict_failure()` - Line 346
- `dm_remap_v4_health_validate_model()` - Lines 435, 445
- `dm_remap_v4_health_get_statistics()` - Line 634

**Solution:**
Replace all floating-point operations with fixed-point arithmetic:
- Use integer math with scaling factors (e.g., multiply by 1000000)
- Implement integer approximations of exponential/logarithmic functions
- Use lookup tables for trigonometric functions
- Document precision trade-offs

**Example Fix:**
```c
// Before (float):
float prediction = model->coefficients[0] * expf(-model->coefficients[1] * days);

// After (fixed-point):
int64_t prediction = (model->coefficients[0] * int_exp(-model->coefficients[1] * days)) / 1000000;
```

**Impact:** Blocks health monitoring integration tests

---

### 2. Validation Module Struct Incompatibility
**Module:** `dm-remap-v4-validation.c`  
**Severity:** CRITICAL  
**Effort:** 2-3 hours

**Issue:**
Validation module expects metadata structures that don't match current design. Multiple undefined struct types.

**Missing Includes:**
- `dm-remap-v4-setup-reassembly.h` (defines `dm_remap_v4_device_fingerprint`)

**Undefined Structs:**
- `dm_remap_v4_metadata_header` - not defined anywhere
- `dm_remap_v4_target_config` - defined in setup-reassembly.h
- `dm_remap_v4_spare_device_info` - name mismatch
- `dm_remap_v4_reassembly_instructions` - defined in setup-reassembly.h

**Solution:**
1. Add missing includes in proper order
2. Update function signatures to match current struct names
3. Verify all struct member accesses are valid
4. Update validation logic for current metadata format

**Impact:** Blocks metadata validation and integration tests

---

## 🟡 High Priority (Should Fix Before Release)

### 3. Version Control Header Conflicts
**Modules:** `dm-remap-v4-version-control.c`, `dm-remap-v4-metadata.h`  
**Severity:** HIGH  
**Effort:** 1-2 hours

**Issue:**
Struct `dm_remap_v4_version_header` is defined in both:
- `include/dm-remap-v4-metadata.h` (line 71)
- `include/dm-remap-v4-version-control.h` (line 55)

This causes redefinition errors and incompatible pointer types.

**Solution:**
1. Choose single authoritative location (recommend: metadata.h)
2. Remove duplicate definition from version-control.h
3. Update all includes to reference correct header
4. Verify all member access is compatible

**Impact:** Blocks version control testing

---

### 4. Missing DM_MSG_PREFIX Definitions
**Modules:** Multiple  
**Severity:** HIGH  
**Effort:** 30 minutes

**Issue:**
Several files include `linux/device-mapper.h` without defining `DM_MSG_PREFIX` first, causing compilation errors when using DMERR/DMWARN/DMINFO macros.

**Affected Files:**
- ✅ `dm-remap-v4-version-control.c` - FIXED
- ✅ `dm-remap-v4-version-control-utils.c` - FIXED  
- ❌ `dm-remap-v4-health-monitoring.c` - needs fix
- ❌ `dm-remap-v4-health-monitoring-utils.c` - needs fix

**Solution:**
Add before all includes:
```c
#define DM_MSG_PREFIX "dm-remap-v4-<module-name>"
```

**Impact:** Prevents compilation

---

## 🟢 Medium Priority (Quality Improvements)

### 5. Circular Header Dependencies
**Severity:** MEDIUM  
**Effort:** 1-2 hours

**Issue:**
Complex include chains make it difficult to determine correct header order. Forward declarations needed but not consistently used.

**Example:**
```
validation.h includes setup-reassembly.h
setup-reassembly.h includes metadata.h  
metadata.h includes version-control.h
version-control.h includes validation.h (circular!)
```

**Solution:**
1. Map all header dependencies
2. Establish clear header hierarchy
3. Use forward declarations where possible
4. Document include order requirements

**Impact:** Makes maintenance difficult

---

### 6. Inconsistent Error Handling
**Severity:** MEDIUM  
**Effort:** 2-3 hours

**Issue:**
Mix of error handling patterns across modules:
- Some use -ERRNO codes
- Some use custom error codes
- Some return bool
- Inconsistent cleanup on error paths

**Solution:**
1. Standardize on kernel -ERRNO codes
2. Document error code meanings
3. Add consistent cleanup patterns
4. Use goto-based error handling

**Impact:** API consistency, debugging difficulty

---

## 🔵 Low Priority (Nice to Have)

### 7. Code Duplication
**Severity:** LOW  
**Effort:** 3-4 hours

**Issue:**
Similar code patterns repeated across modules:
- CRC32 calculation
- Timestamp handling
- Logging macros
- Structure initialization

**Solution:**
Extract common utilities to shared header/module.

**Impact:** Maintainability

---

### 8. Missing const Correctness
**Severity:** LOW  
**Effort:** 1-2 hours

**Issue:**
Many pointers that should be const are not marked as such.

**Impact:** Code clarity, compiler optimization

---

## Cleanup Task List

### Phase 1: Critical Blockers (Required for Integration Tests)
- [ ] Fix floating-point math in health-monitoring (3-4h)
- [ ] Fix validation module struct compatibility (2-3h)
- [ ] Fix version-control header conflicts (1-2h)
- [ ] Add missing DM_MSG_PREFIX definitions (30min)

**Total Effort:** 7-10 hours  
**Outcome:** Integration tests can run

### Phase 2: Quality Improvements (Before v4.0 Release)
- [ ] Resolve circular header dependencies (1-2h)
- [ ] Standardize error handling (2-3h)
- [ ] Extract common utilities (3-4h)
- [ ] Add const correctness (1-2h)

**Total Effort:** 7-11 hours  
**Outcome:** Clean, maintainable codebase

### Phase 3: Polish (Post-Release)
- [ ] Performance profiling and optimization
- [ ] Memory leak detection
- [ ] Static analysis cleanup
- [ ] Documentation improvements

## Priority Recommendation

**Immediate Action:** Complete Phase 1 cleanup (7-10h) to enable integration testing

**Before Release:** Complete Phase 2 (7-11h) for code quality

**Post-Release:** Address Phase 3 as ongoing maintenance

---

**Total Technical Debt:** ~20-30 hours  
**Critical Path:** 7-10 hours (Phase 1)
