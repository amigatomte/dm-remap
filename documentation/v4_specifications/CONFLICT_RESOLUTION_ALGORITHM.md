# dm-remap v4.0 Conflict Resolution Algorithm Specification

**Document Version**: 1.0  
**Date**: October 12, 2025  
**Phase**: 4.0 Week 1-2 Deliverable  
**Purpose**: Sequence number and timestamp-based conflict resolution for metadata consistency

---

## 🎯 Overview

The v4.0 conflict resolution system uses **monotonic sequence numbers** with **timestamp fallbacks** to automatically resolve metadata conflicts when multiple copies exist with different versions, ensuring consistent and reliable metadata state across all spare devices.

## ⚡ Conflict Resolution Hierarchy

### **Three-Tier Resolution Strategy**
```
Priority 1: Sequence Number (Primary)
    ↓ (if equal)
Priority 2: Timestamp (Secondary)  
    ↓ (if equal)
Priority 3: Copy Index (Tiebreaker)
```

### **Resolution Algorithm Logic**
```c
/**
 * Resolution decision tree:
 * 
 * 1. Compare sequence numbers → Higher wins
 * 2. If sequence equal → Compare timestamps → Newer wins  
 * 3. If timestamps equal → Compare copy indices → Lower index wins
 * 4. If all equal → Copies are identical (no conflict)
 */
```

## 🔢 Sequence Number Management

### **Monotonic Sequence Counter**
```c
/**
 * Global sequence number management
 */
static atomic64_t dm_remap_global_sequence = ATOMIC64_INIT(1);

/**
 * get_next_sequence_number() - Generate next sequence number
 * 
 * Guarantees:
 * - Monotonically increasing
 * - Thread-safe atomic operations
 * - Never returns 0 (reserved for uninitialized)
 */
static uint64_t get_next_sequence_number(void)
{
    return atomic64_inc_return(&dm_remap_global_sequence);
}

/**
 * initialize_sequence_from_metadata() - Bootstrap sequence counter
 * 
 * Called during module initialization to set sequence counter
 * to highest value found in existing metadata
 */
static void initialize_sequence_from_metadata(void)
{
    uint64_t max_sequence = 0;
    
    // Scan all spare devices for highest sequence number
    // (Implementation details in discovery module)
    max_sequence = scan_all_metadata_for_max_sequence();
    
    // Set global counter to be higher than any existing metadata
    atomic64_set(&dm_remap_global_sequence, max_sequence + 1);
    
    DMR_DEBUG(1, "Initialized global sequence counter to %llu", max_sequence + 1);
}
```

### **Sequence Number Validation**
```c
/**
 * validate_sequence_number() - Ensure sequence number is valid
 */
static bool validate_sequence_number(uint64_t sequence)
{
    // Sequence number must be non-zero and not too large
    return (sequence > 0 && sequence < UINT64_MAX - 1000);
}
```

## ⏰ Timestamp Management

### **High-Resolution Timestamps**
```c
#include <linux/ktime.h>

/**
 * get_current_timestamp() - Get current Unix timestamp
 * 
 * Uses kernel's real-time clock for consistent timestamps
 * across reboots and system time changes
 */
static uint64_t get_current_timestamp(void)
{
    return ktime_get_real_seconds();
}

/**
 * validate_timestamp() - Ensure timestamp is reasonable
 */
static bool validate_timestamp(uint64_t timestamp)
{
    uint64_t current_time = get_current_timestamp();
    uint64_t min_valid_time = 1640995200; // Jan 1, 2022 UTC
    uint64_t max_future_time = current_time + 86400; // 1 day in future
    
    return (timestamp >= min_valid_time && timestamp <= max_future_time);
}
```

## 🔄 Conflict Resolution Implementation

### **Primary Resolution Function**
```c
/**
 * resolve_metadata_conflict() - Determine which metadata copy to use
 * 
 * @copies: Array of metadata copies to compare
 * @count: Number of copies in array
 * @valid_mask: Bitmask of which copies passed checksum validation
 * 
 * Returns: Index of winning copy, or -1 if no valid copies
 */
static int resolve_metadata_conflict(struct dm_remap_metadata_v4 *copies, 
                                   int count, uint8_t valid_mask)
{
    int winner = -1;
    uint64_t best_sequence = 0;
    uint64_t best_timestamp = 0;
    int best_copy_index = INT_MAX;
    int i;
    
    DMR_DEBUG(2, "Resolving metadata conflict among %d copies (valid_mask=0x%02x)", 
              count, valid_mask);
    
    // Examine all valid copies
    for (i = 0; i < count; i++) {
        if (!(valid_mask & (1 << i))) {
            continue; // Skip invalid copies
        }
        
        struct dm_remap_metadata_v4 *copy = &copies[i];
        bool is_better = false;
        
        // Primary criterion: Sequence number (higher is better)
        if (copy->header.sequence_number > best_sequence) {
            is_better = true;
        } else if (copy->header.sequence_number == best_sequence) {
            // Secondary criterion: Timestamp (newer is better)
            if (copy->header.timestamp > best_timestamp) {
                is_better = true;
            } else if (copy->header.timestamp == best_timestamp) {
                // Tertiary criterion: Copy index (lower is better - prefer Copy 0)
                if (copy->header.copy_index < best_copy_index) {
                    is_better = true;
                }
            }
        }
        
        if (is_better) {
            winner = i;
            best_sequence = copy->header.sequence_number;
            best_timestamp = copy->header.timestamp;
            best_copy_index = copy->header.copy_index;
        }
    }
    
    if (winner >= 0) {
        DMR_DEBUG(1, "Conflict resolved: Copy %d wins (seq=%llu, ts=%llu, idx=%d)",
                  winner, best_sequence, best_timestamp, best_copy_index);
    } else {
        DMR_DEBUG(0, "Conflict resolution failed: no valid copies found");
    }
    
    return winner;
}
```

### **Advanced Conflict Analysis**
```c
/**
 * analyze_metadata_conflicts() - Detailed conflict analysis and reporting
 */
struct conflict_analysis {
    int total_copies;
    int valid_copies;
    int sequence_conflicts;
    int timestamp_conflicts;
    uint64_t sequence_range_min;
    uint64_t sequence_range_max;
    uint64_t timestamp_range_min;
    uint64_t timestamp_range_max;
    bool requires_repair;
};

static struct conflict_analysis 
analyze_metadata_conflicts(struct dm_remap_metadata_v4 *copies, 
                          int count, uint8_t valid_mask)
{
    struct conflict_analysis analysis = {0};
    uint64_t sequences[5] = {0};
    uint64_t timestamps[5] = {0};
    int valid_count = 0;
    int i, j;
    
    analysis.total_copies = count;
    analysis.sequence_range_min = UINT64_MAX;
    analysis.timestamp_range_min = UINT64_MAX;
    
    // Collect data from valid copies
    for (i = 0; i < count; i++) {
        if (!(valid_mask & (1 << i))) {
            continue;
        }
        
        sequences[valid_count] = copies[i].header.sequence_number;
        timestamps[valid_count] = copies[i].header.timestamp;
        
        // Update ranges
        if (sequences[valid_count] < analysis.sequence_range_min)
            analysis.sequence_range_min = sequences[valid_count];
        if (sequences[valid_count] > analysis.sequence_range_max)
            analysis.sequence_range_max = sequences[valid_count];
            
        if (timestamps[valid_count] < analysis.timestamp_range_min)
            analysis.timestamp_range_min = timestamps[valid_count];
        if (timestamps[valid_count] > analysis.timestamp_range_max)
            analysis.timestamp_range_max = timestamps[valid_count];
        
        valid_count++;
    }
    
    analysis.valid_copies = valid_count;
    
    // Detect conflicts
    for (i = 0; i < valid_count - 1; i++) {
        for (j = i + 1; j < valid_count; j++) {
            if (sequences[i] != sequences[j]) {
                analysis.sequence_conflicts++;
            }
            if (timestamps[i] != timestamps[j]) {
                analysis.timestamp_conflicts++;
            }
        }
    }
    
    // Determine if repair is needed
    analysis.requires_repair = (analysis.sequence_conflicts > 0 || 
                               analysis.timestamp_conflicts > 0 ||
                               analysis.valid_copies < count);
    
    return analysis;
}
```

## 🛠️ Conflict Prevention Strategies

### **Atomic Update Protocol**
```c
/**
 * atomic_metadata_update() - Prevent conflicts during updates
 * 
 * Strategy:
 * 1. Read current best metadata
 * 2. Increment sequence number atomically
 * 3. Update timestamp
 * 4. Write all copies with identical sequence/timestamp
 * 5. Verify all writes succeeded
 */
static int atomic_metadata_update(struct block_device *spare_bdev,
                                 struct dm_remap_metadata_v4 *new_metadata)
{
    struct dm_remap_metadata_v4 current_metadata;
    int ret;
    
    // Read current best copy for sequence number
    ret = dm_remap_metadata_v4_read_best_copy(spare_bdev, &current_metadata);
    if (ret && ret != -ENODATA) {
        return ret; // Error reading current metadata
    }
    
    // Set up new metadata with incremented sequence
    if (ret == 0) {
        // Update existing metadata
        new_metadata->header.sequence_number = current_metadata.header.sequence_number + 1;
    } else {
        // First-time metadata creation
        new_metadata->header.sequence_number = 1;
    }
    
    new_metadata->header.timestamp = get_current_timestamp();
    new_metadata->header.magic = DM_REMAP_METADATA_V4_MAGIC;
    
    // Write all copies atomically
    ret = dm_remap_metadata_v4_write_all_copies(new_metadata, spare_bdev);
    if (ret) {
        DMR_DEBUG(0, "Atomic metadata update failed: %d", ret);
        return ret;
    }
    
    DMR_DEBUG(1, "Atomic update completed: seq=%llu, ts=%llu",
              new_metadata->header.sequence_number, new_metadata->header.timestamp);
    
    return 0;
}
```

### **Concurrency Protection**
```c
/**
 * Mutex protection for metadata operations to prevent race conditions
 */
static DEFINE_MUTEX(dm_remap_metadata_mutex);

/**
 * dm_remap_metadata_update_locked() - Thread-safe metadata update
 */
static int dm_remap_metadata_update_locked(struct block_device *spare_bdev,
                                         struct dm_remap_metadata_v4 *metadata)
{
    int ret;
    
    mutex_lock(&dm_remap_metadata_mutex);
    ret = atomic_metadata_update(spare_bdev, metadata);
    mutex_unlock(&dm_remap_metadata_mutex);
    
    return ret;
}
```

## 🔍 Conflict Detection and Reporting

### **Conflict Detection During Discovery**
```c
/**
 * detect_metadata_conflicts() - Scan for conflicts during device discovery
 */
static bool detect_metadata_conflicts(struct dm_remap_metadata_v4 *copies,
                                     int count, uint8_t valid_mask)
{
    uint64_t first_sequence = 0;
    uint64_t first_timestamp = 0;
    bool first_found = false;
    int i;
    
    for (i = 0; i < count; i++) {
        if (!(valid_mask & (1 << i))) {
            continue;
        }
        
        if (!first_found) {
            first_sequence = copies[i].header.sequence_number;
            first_timestamp = copies[i].header.timestamp;
            first_found = true;
            continue;
        }
        
        // Check for conflicts
        if (copies[i].header.sequence_number != first_sequence ||
            copies[i].header.timestamp != first_timestamp) {
            return true; // Conflict detected
        }
    }
    
    return false; // No conflicts found
}
```

### **Sysfs Interface for Conflict Monitoring**
```c
/**
 * /sys/kernel/dm_remap/conflict_status - Monitor conflict resolution
 */
static ssize_t conflict_status_show(struct kobject *kobj,
                                   struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf,
        "Conflict Resolution Statistics:\n"
        "  Total conflicts resolved: %llu\n"
        "  Sequence number conflicts: %llu\n"
        "  Timestamp conflicts: %llu\n"
        "  Copy index tiebreakers: %llu\n"
        "  Average resolution time: %llu ns\n"
        "  Failed resolutions: %llu\n",
        atomic64_read(&conflict_stats.total_resolved),
        atomic64_read(&conflict_stats.sequence_conflicts),
        atomic64_read(&conflict_stats.timestamp_conflicts),
        atomic64_read(&conflict_stats.index_tiebreakers),
        atomic64_read(&conflict_stats.resolution_time_ns) / 
            max(1ULL, atomic64_read(&conflict_stats.total_resolved)),
        atomic64_read(&conflict_stats.failed_resolutions)
    );
}
```

## 📊 Performance Analysis

### **Resolution Performance Characteristics**
```c
/**
 * Typical conflict resolution performance:
 * 
 * Simple case (sequence differs):     ~100ns
 * Complex case (timestamp fallback):  ~200ns  
 * Worst case (full analysis):        ~500ns
 * 
 * Memory usage: O(1) - fixed size structures
 * CPU complexity: O(n) where n = number of copies (max 5)
 */
```

### **Performance Monitoring**
```c
/**
 * Track conflict resolution performance
 */
struct dm_remap_conflict_stats {
    atomic64_t total_resolved;
    atomic64_t sequence_conflicts;
    atomic64_t timestamp_conflicts;
    atomic64_t index_tiebreakers;
    atomic64_t resolution_time_ns;
    atomic64_t failed_resolutions;
};

static struct dm_remap_conflict_stats conflict_stats;
```

## 📋 Implementation Checklist

### Week 1-2 Deliverables
- [ ] Implement monotonic sequence number management
- [ ] Create timestamp validation and management functions
- [ ] Build three-tier conflict resolution algorithm
- [ ] Add atomic metadata update protocol with mutex protection
- [ ] Implement conflict detection and analysis functions
- [ ] Create sysfs interface for conflict monitoring
- [ ] Add comprehensive logging and debugging support

### Integration Requirements
- [ ] Integrate with redundant storage system
- [ ] Add to discovery and repair algorithms
- [ ] Include in metadata read/write operations
- [ ] Add performance monitoring and statistics

## 🎯 Success Criteria

### **Reliability Targets**
- [ ] 100% conflict resolution success rate for valid metadata
- [ ] Zero data loss during conflict resolution
- [ ] Deterministic resolution (same inputs → same output)

### **Performance Targets**
- [ ] <1μs average conflict resolution time
- [ ] <0.01% CPU overhead for conflict resolution
- [ ] O(1) memory usage regardless of conflict complexity

### **Enterprise Features**
- [ ] Real-time conflict monitoring via sysfs
- [ ] Comprehensive conflict analysis and reporting
- [ ] Atomic updates with concurrency protection

---

## 📚 Technical References

- **Linux Atomic Operations**: `include/linux/atomic.h`
- **Kernel Time Management**: `include/linux/ktime.h`
- **Mutex Implementation**: `include/linux/mutex.h`
- **Debugging Infrastructure**: `include/linux/printk.h`

This conflict resolution system provides deterministic, fast, and reliable metadata consistency for dm-remap v4.0, ensuring that conflicting metadata copies are automatically resolved using well-defined priority rules.