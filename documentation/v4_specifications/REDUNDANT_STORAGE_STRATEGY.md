# dm-remap v4.0 Redundant Storage Strategy

**Document Version**: 1.0  
**Date**: October 12, 2025  
**Phase**: 4.0 Week 1-2 Deliverable  
**Purpose**: Define 5-copy redundant metadata storage architecture for reliability and discovery

---

## 🎯 Overview

The v4.0 redundant storage strategy implements a **5-copy metadata distribution system** across strategic spare device sectors to ensure maximum reliability, fast discovery, and automatic recovery from corruption.

## 📍 Storage Location Strategy

### **Strategic Sector Placement**
```
Spare Device Layout (example 1TB device):
┌─────────────────────────────────────────────────────────────┐
│ Sector 0        │ Primary metadata copy (Copy 0)            │
│ Sector 1024     │ Secondary metadata copy (Copy 1)          │
│ Sector 2048     │ Tertiary metadata copy (Copy 2)           │
│ Sector 4096     │ Quaternary metadata copy (Copy 3)         │
│ Sector 8192     │ Backup metadata copy (Copy 4)             │
│ Sectors 16384+  │ Available for remap data storage          │
└─────────────────────────────────────────────────────────────┘
```

### **Sector Selection Rationale**
1. **Sector 0**: Traditional metadata location - fastest access
2. **Sector 1024**: Early location - quick discovery scan
3. **Sector 2048**: Powers-of-2 alignment - filesystem-friendly
4. **Sector 4096**: Page boundary alignment - memory-efficient
5. **Sector 8192**: Sufficient separation - wear distribution

## 🔄 Copy Management Algorithm

### **Write Strategy: All-or-Nothing Updates**
```c
/**
 * dm_remap_metadata_v4_write_all_copies() - Write metadata to all 5 copies
 * 
 * Algorithm:
 * 1. Increment sequence number
 * 2. Update timestamp
 * 3. Calculate checksums
 * 4. Write to all 5 locations atomically
 * 5. Verify all writes succeeded
 * 6. Rollback on any failure
 */
int dm_remap_metadata_v4_write_all_copies(struct dm_remap_metadata_v4 *metadata,
                                          struct block_device *spare_bdev)
{
    const sector_t copy_sectors[] = {0, 1024, 2048, 4096, 8192};
    int i, ret;
    
    // Prepare metadata for writing
    metadata->header.sequence_number++;
    metadata->header.timestamp = ktime_get_real_seconds();
    
    // Write to all copies
    for (i = 0; i < 5; i++) {
        metadata->header.copy_index = i;
        metadata->header.header_checksum = calculate_header_crc32(metadata);
        metadata->header.data_checksum = calculate_data_crc32(metadata);
        
        ret = write_metadata_to_sector(spare_bdev, copy_sectors[i], metadata);
        if (ret) {
            // Rollback previous writes on failure
            rollback_metadata_writes(spare_bdev, copy_sectors, i);
            return ret;
        }
    }
    
    return 0;
}
```

### **Read Strategy: Best-Copy Selection**
```c
/**
 * dm_remap_metadata_v4_read_best_copy() - Find and return the best metadata copy
 * 
 * Selection Algorithm:
 * 1. Read all 5 copies
 * 2. Validate checksums for each copy
 * 3. Select copy with highest sequence number
 * 4. Fall back to timestamp if sequence numbers equal
 * 5. Return error only if no valid copies found
 */
int dm_remap_metadata_v4_read_best_copy(struct block_device *spare_bdev,
                                        struct dm_remap_metadata_v4 *metadata)
{
    const sector_t copy_sectors[] = {0, 1024, 2048, 4096, 8192};
    struct dm_remap_metadata_v4 copies[5];
    bool valid[5] = {false};
    int best_copy = -1;
    uint64_t best_sequence = 0;
    uint64_t best_timestamp = 0;
    int i, valid_count = 0;
    
    // Read all copies
    for (i = 0; i < 5; i++) {
        if (read_metadata_from_sector(spare_bdev, copy_sectors[i], &copies[i]) == 0) {
            if (validate_metadata_checksums(&copies[i])) {
                valid[i] = true;
                valid_count++;
                
                // Track best copy based on sequence number, then timestamp
                if (copies[i].header.sequence_number > best_sequence ||
                    (copies[i].header.sequence_number == best_sequence &&
                     copies[i].header.timestamp > best_timestamp)) {
                    best_copy = i;
                    best_sequence = copies[i].header.sequence_number;
                    best_timestamp = copies[i].header.timestamp;
                }
            }
        }
    }
    
    if (best_copy >= 0) {
        memcpy(metadata, &copies[best_copy], sizeof(*metadata));
        DMR_DEBUG(1, "Selected metadata copy %d (seq=%llu, valid_copies=%d/5)",
                  best_copy, best_sequence, valid_count);
        return 0;
    }
    
    return -ENODATA; // No valid copies found
}
```

## 🛠️ Automatic Repair Mechanism

### **Repair Trigger Conditions**
- Any copy fails checksum validation
- Sequence number mismatches detected
- Missing copies during discovery
- Corruption detected during normal operation

### **Self-Healing Algorithm**
```c
/**
 * dm_remap_metadata_v4_repair() - Repair corrupted metadata copies
 * 
 * Repair Strategy:
 * 1. Identify best valid copy (highest sequence number)
 * 2. Overwrite corrupted copies with best copy
 * 3. Update copy indices appropriately
 * 4. Log repair actions for monitoring
 */
int dm_remap_metadata_v4_repair(struct block_device *spare_bdev)
{
    struct dm_remap_metadata_v4 best_metadata;
    const sector_t copy_sectors[] = {0, 1024, 2048, 4096, 8192};
    int ret, i, repairs_made = 0;
    
    // Find best copy
    ret = dm_remap_metadata_v4_read_best_copy(spare_bdev, &best_metadata);
    if (ret) {
        DMR_DEBUG(0, "Cannot repair: no valid metadata copy found");
        return ret;
    }
    
    // Repair corrupted copies
    for (i = 0; i < 5; i++) {
        struct dm_remap_metadata_v4 copy;
        
        if (read_metadata_from_sector(spare_bdev, copy_sectors[i], &copy) != 0 ||
            !validate_metadata_checksums(&copy) ||
            copy.header.sequence_number != best_metadata.header.sequence_number) {
            
            // Repair this copy
            best_metadata.header.copy_index = i;
            best_metadata.header.header_checksum = calculate_header_crc32(&best_metadata);
            
            ret = write_metadata_to_sector(spare_bdev, copy_sectors[i], &best_metadata);
            if (ret == 0) {
                repairs_made++;
                DMR_DEBUG(1, "Repaired metadata copy %d at sector %llu", i, copy_sectors[i]);
            }
        }
    }
    
    if (repairs_made > 0) {
        DMR_DEBUG(1, "Metadata repair completed: %d copies repaired", repairs_made);
    }
    
    return 0;
}
```

## 🔍 Discovery Optimization

### **Fast Discovery Scan**
```c
/**
 * Discovery scans sectors in order of access speed:
 * 1. Sector 0 (fastest - traditional location)
 * 2. Sector 1024 (quick discovery)
 * 3. Sector 2048, 4096, 8192 (additional copies)
 */
static const sector_t discovery_order[] = {0, 1024, 2048, 4096, 8192};
```

### **Discovery Performance Targets**
- **Single device scan**: <100ms for valid metadata detection
- **Multiple device scan**: <500ms for complete spare device inventory
- **Validation overhead**: <10ms additional per device

## 📊 Reliability Analysis

### **Failure Probability Calculation**
```
Single copy failure rate: P(fail) = 0.01 (1% sector failure rate)
All 5 copies fail probability: P(all_fail) = (0.01)^5 = 0.0000000001 (0.00000001%)

Reliability improvement: 10^10 times more reliable than single copy
```

### **Recovery Scenarios**
1. **1 copy corrupted**: Automatic repair from 4 valid copies
2. **2 copies corrupted**: Automatic repair from 3 valid copies  
3. **3 copies corrupted**: Automatic repair from 2 valid copies
4. **4 copies corrupted**: Emergency repair from 1 valid copy
5. **5 copies corrupted**: Metadata loss (extremely unlikely: 0.00000001%)

## ⚡ Performance Considerations

### **Write Performance Impact**
- **Additional write operations**: 4 extra sector writes per update
- **Sequential write optimization**: Batch writes when possible
- **Expected overhead**: <5ms additional latency per metadata update

### **Read Performance Optimization**
- **Fast path**: Single sector read when Copy 0 is valid
- **Fallback path**: Progressive scanning of remaining copies
- **Cache optimization**: In-memory caching of valid metadata

### **Storage Overhead**
- **Per spare device**: 5 * 4KB = 20KB metadata storage
- **Percentage overhead**: <0.001% on typical spare devices
- **Wear distribution**: Even wear across 5 different sectors

## 🛡️ Security Considerations

### **Integrity Protection**
- **Multi-level checksums**: Header and data section CRC32
- **Sequence number protection**: Prevents replay attacks
- **Timestamp validation**: Detects time-based inconsistencies

### **Discovery Safety**
- **Magic number validation**: Prevents false positive detection
- **Multi-copy consensus**: Requires majority agreement for critical decisions
- **Fingerprint verification**: Ensures device identity consistency

## 📋 Implementation Checklist

### Week 1-2 Implementation Tasks
- [ ] Define sector layout constants and data structures
- [ ] Implement 5-copy write algorithm with rollback
- [ ] Implement best-copy selection algorithm
- [ ] Implement automatic repair mechanism
- [ ] Add discovery optimization with ordered scanning
- [ ] Add performance monitoring and metrics
- [ ] Create comprehensive test cases for all failure scenarios

### Integration Requirements
- [ ] Integrate with existing dm-remap metadata functions
- [ ] Maintain backward compatibility with v3.0 single-copy format
- [ ] Add sysfs interfaces for repair status and statistics
- [ ] Document upgrade path from v3.0 to v4.0 redundant format

## 🎯 Success Metrics

### **Reliability Targets**
- [ ] 99.99999% metadata availability (6-nines reliability)
- [ ] <1 second automatic repair time
- [ ] 100% compatibility with existing v3.0 metadata

### **Performance Targets**  
- [ ] <5ms additional write latency
- [ ] <100ms discovery time per device
- [ ] <0.001% storage overhead

### **Enterprise Features**
- [ ] Automatic corruption detection and repair
- [ ] Comprehensive logging and monitoring
- [ ] Zero-downtime metadata format upgrades

---

## 📚 References

- **Linux Kernel Block Layer**: Documentation/block/
- **Device Mapper Infrastructure**: drivers/md/dm.c
- **CRC32 Implementation**: lib/crc32.c
- **Sector I/O Operations**: fs/block_dev.c

This redundant storage strategy provides enterprise-grade metadata reliability while maintaining the performance and simplicity that dm-remap users expect.