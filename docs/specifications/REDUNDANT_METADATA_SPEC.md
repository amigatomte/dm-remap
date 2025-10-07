# dm-remap v4.0 Redundant Metadata Specification

**Document Version**: 1.0  
**Date**: October 6, 2025  
**Author**: dm-remap Development Team  

---

## 🎯 Overview

This specification defines the redundant metadata storage system for dm-remap v4.0, providing enterprise-grade reliability through multiple metadata copies, integrity protection, and automatic conflict resolution.

## 📋 Metadata Storage Strategy

### **Multiple Copy Locations**
```
Spare Device Metadata Layout:
┌─────────────────────────────────────────────────────────────┐
│ Sector 0:    Primary metadata copy (4KB)                   │
│ Sector 1024: Secondary metadata copy (4KB)                 │
│ Sector 2048: Tertiary metadata copy (4KB)                  │
│ Sector 4096: Quaternary metadata copy (4KB)                │
│ Sector 8192: Quinary metadata copy (4KB)                   │
└─────────────────────────────────────────────────────────────┘

Total: 5 redundant copies across strategic spare device locations
Strategy: Geometric progression (powers of 2) for diverse failure protection
```

### **Metadata Copy Distribution Logic**
- **Sector 0**: Immediate access, compatible with existing v3.0 location
- **Sector 1024**: Early spare area, survives filesystem corruption
- **Sector 2048**: Mid-range location, survives partial device failures
- **Sector 4096**: Higher sector range, survives bad sector clusters
- **Sector 8192**: Extended range, survives large corruption events

## 🔧 Enhanced Metadata Structure

### **v4.0 Metadata Format with Redundancy**
```c
#define DM_REMAP_METADATA_V4_MAGIC 0x444D5234  // "DMR4"
#define DM_REMAP_METADATA_COPIES   5
#define DM_REMAP_METADATA_SIZE     4096

struct dm_remap_metadata_v4 {
    // Header with integrity protection
    struct {
        uint32_t magic;              // Magic number: 0x444D5234
        uint32_t version;            // Metadata format version
        uint64_t sequence_number;    // Monotonic counter for conflict resolution
        uint32_t total_size;         // Total metadata size in bytes
        uint32_t header_checksum;    // CRC32 of this header
        uint32_t data_checksum;      // CRC32 of data sections
        uint32_t copy_index;         // Which copy this is (0-4)
        uint64_t timestamp;          // Creation/update timestamp (nanoseconds)
        uint8_t  reserved[32];       // Reserved for future extensions
    } header;
    
    // v3.0 compatibility section (unchanged)
    struct dm_remap_metadata_v3 legacy;
    
    // v4.0 Configuration Storage Section
    struct {
        char main_device_uuid[37];      // UUID of main device
        char main_device_path[256];     // Original device path
        uint64_t main_device_size;      // Device size in sectors
        uint32_t target_params_len;     // Length of stored parameters
        char target_params[512];        // dm-remap target parameters
        char sysfs_config[1024];       // sysfs settings snapshot
        uint32_t config_version;       // Configuration format version
        uint32_t config_checksum;      // Configuration section CRC32
        uint8_t device_fingerprint[32]; // SHA-256 device fingerprint
        uint8_t spare_device_uuid[37];  // UUID of this spare device
    } setup_config;
    
    // v4.0 Health Scanning Section
    struct {
        uint64_t last_scan_time;       // Last background scan timestamp
        uint32_t scan_interval;        // Configured scan interval (seconds)
        uint32_t health_score;         // Overall device health (0-100)
        uint32_t sector_scan_progress; // Current scan progress (0-100)
        uint32_t scan_flags;           // Scanning configuration flags
        struct {
            uint32_t scans_completed;
            uint32_t errors_detected;
            uint32_t predictive_remaps;
            uint32_t scan_overhead_ms;
        } health_stats;
        uint32_t health_checksum;      // Health section CRC32
    } health_data;
    
    // Padding to ensure 4KB total size
    uint8_t padding[METADATA_PADDING_SIZE];
    
    // Footer validation
    struct {
        uint32_t footer_magic;         // Footer magic: 0x34524D44
        uint32_t overall_checksum;     // CRC32 of entire metadata
    } footer;
};
```

## 🔒 Integrity Protection System

### **Multi-Level Checksum Strategy**
```c
// Checksum calculation functions
uint32_t calculate_header_checksum(struct dm_remap_metadata_v4 *meta);
uint32_t calculate_config_checksum(struct setup_config *config);
uint32_t calculate_health_checksum(struct health_data *health);
uint32_t calculate_overall_checksum(struct dm_remap_metadata_v4 *meta);

// Validation function
enum metadata_validation_result {
    METADATA_VALID,
    METADATA_HEADER_CORRUPT,
    METADATA_CONFIG_CORRUPT,
    METADATA_HEALTH_CORRUPT,
    METADATA_OVERALL_CORRUPT,
    METADATA_MAGIC_INVALID,
    METADATA_VERSION_UNSUPPORTED
};

enum metadata_validation_result validate_metadata_copy(
    struct dm_remap_metadata_v4 *meta
);
```

### **Integrity Protection Levels**
1. **Header Checksum**: Protects critical metadata structure information
2. **Section Checksums**: Individual protection for config and health data
3. **Overall Checksum**: Complete metadata integrity verification
4. **Magic Number Validation**: Ensures proper metadata identification
5. **Size Validation**: Prevents buffer overflow attacks

## 🔄 Version Control & Conflict Resolution

### **Sequence Number Management**
```c
// Sequence number operations
uint64_t get_next_sequence_number(void);
bool is_newer_metadata(uint64_t seq1, uint64_t seq2);
struct dm_remap_metadata_v4 *select_newest_valid_copy(
    struct dm_remap_metadata_v4 copies[DM_REMAP_METADATA_COPIES]
);

// Conflict resolution algorithm
enum conflict_resolution_result {
    CONFLICT_RESOLVED_SUCCESS,
    CONFLICT_ALL_COPIES_CORRUPT,
    CONFLICT_VERSION_MISMATCH,
    CONFLICT_DEVICE_MISMATCH
};

enum conflict_resolution_result resolve_metadata_conflicts(
    struct dm_remap_metadata_v4 copies[DM_REMAP_METADATA_COPIES],
    struct dm_remap_metadata_v4 **selected_copy
);
```

### **Conflict Resolution Algorithm**
1. **Read all 5 metadata copies** from spare device
2. **Validate checksums** for each copy
3. **Filter out corrupted copies** (failed checksum validation)
4. **Compare sequence numbers** among valid copies
5. **Select copy with highest sequence number** (newest)
6. **Verify device compatibility** (UUID matching)
7. **Return selected metadata** or error if all copies invalid

## 🛠️ Metadata Operations API

### **Core Metadata Functions**
```c
// High-level metadata operations
int write_redundant_metadata_v4(
    struct block_device *spare_bdev,
    struct dm_remap_metadata_v4 *meta
);

int read_redundant_metadata_v4(
    struct block_device *spare_bdev,
    struct dm_remap_metadata_v4 **meta_out
);

int repair_corrupted_metadata_copies(
    struct block_device *spare_bdev,
    struct dm_remap_metadata_v4 *valid_meta
);

int upgrade_metadata_v3_to_v4(
    struct block_device *spare_bdev,
    struct dm_remap_metadata_v3 *old_meta
);
```

### **Metadata Write Strategy**
```c
// Atomic metadata update process
int atomic_metadata_update(struct block_device *spare_bdev,
                          struct dm_remap_metadata_v4 *new_meta) {
    // 1. Increment sequence number
    new_meta->header.sequence_number = get_next_sequence_number();
    
    // 2. Update timestamp
    new_meta->header.timestamp = ktime_get_ns();
    
    // 3. Calculate all checksums
    new_meta->header.data_checksum = calculate_data_checksum(new_meta);
    new_meta->header.header_checksum = calculate_header_checksum(new_meta);
    new_meta->footer.overall_checksum = calculate_overall_checksum(new_meta);
    
    // 4. Write all copies atomically
    for (int i = 0; i < DM_REMAP_METADATA_COPIES; i++) {
        new_meta->header.copy_index = i;
        int sector = metadata_copy_sectors[i];
        if (write_metadata_copy(spare_bdev, sector, new_meta) != 0) {
            return -EIO;
        }
    }
    
    // 5. Verify all writes succeeded
    return verify_all_copies_written(spare_bdev, new_meta);
}
```

## 📊 Performance Considerations

### **Read Performance Optimization**
- **Fast path**: Try sector 0 first (primary copy)
- **Fallback strategy**: Only read additional copies if primary fails validation
- **Parallel validation**: Validate checksums while reading additional copies
- **Early termination**: Stop reading once valid copy is found

### **Write Performance Strategy**
- **Batch writes**: Write all 5 copies in single I/O batch when possible
- **Asynchronous completion**: Use bio completion callbacks for efficiency
- **Write ordering**: Ensure primary copy (sector 0) written last for atomicity

### **Recovery Performance**
- **Lazy repair**: Only repair corrupted copies during normal operations
- **Background repair**: Use work queues for non-critical repairs
- **Selective repair**: Only repair copies that fail validation

## 🧪 Testing Strategy

### **Metadata Corruption Testing**
```bash
# Test scenarios for metadata corruption
tests/v4_metadata/
├── single_copy_corruption_test.sh    # Corrupt one metadata copy
├── multiple_copy_corruption_test.sh  # Corrupt 2-3 metadata copies
├── checksum_corruption_test.sh       # Specific checksum corruption
├── sequence_conflict_test.sh         # Multiple valid copies with different sequences
├── power_failure_simulation_test.sh  # Interrupt during metadata write
└── metadata_repair_test.sh          # Automatic repair functionality
```

### **Validation Test Cases**
1. **Single copy corruption**: Verify recovery from 4 remaining copies
2. **Majority corruption**: Verify recovery when only 1-2 copies remain valid
3. **Sequence conflicts**: Verify newest valid copy selection
4. **Checksum validation**: Verify all checksum levels detect corruption
5. **Write interruption**: Verify metadata consistency after power failure
6. **Automatic repair**: Verify corrupted copies are restored from valid sources

## 🎯 Implementation Timeline

### **Week 1-2: Core Infrastructure**
- [ ] Define metadata structure and constants
- [ ] Implement checksum calculation functions
- [ ] Create metadata validation framework
- [ ] Basic read/write operations for single copy

### **Week 3-4: Redundancy System**
- [ ] Implement multi-copy write operations
- [ ] Create conflict resolution algorithms
- [ ] Add automatic repair functionality
- [ ] Integration with existing v3.0 metadata

### **Week 5-6: Testing & Optimization**
- [ ] Comprehensive corruption testing
- [ ] Performance optimization and benchmarking
- [ ] Integration with discovery system
- [ ] Documentation and examples

## 🔒 Security Considerations

### **Metadata Integrity Protection**
- **Cryptographic hashing**: Consider SHA-256 for device fingerprints
- **Tamper detection**: Checksums detect accidental and malicious corruption
- **Version replay protection**: Sequence numbers prevent downgrade attacks
- **Device binding**: UUID validation prevents metadata transplant attacks

### **Discovery Security**
- **Device authentication**: Verify spare device legitimacy before reassembly
- **Configuration validation**: Ensure metadata matches expected device characteristics
- **Safe defaults**: Fail secure when metadata validation fails
- **Audit logging**: Log all metadata operations for security analysis

---

This redundant metadata system provides **enterprise-grade reliability** with protection against corruption, automatic conflict resolution, and robust recovery capabilities while maintaining backward compatibility with v3.0 systems.