# dm-remap v4.0 Enhanced Metadata Format Specification

**Document Version**: 1.0  
**Date**: October 11, 2025  
**Author**: Phase 4.0 Development Team  
**Status**: Draft - Week 1 Deliverable

---

## 🎯 Executive Summary

The v4.0 metadata format introduces **redundant storage**, **enhanced integrity protection**, and **comprehensive configuration management** while maintaining full backward compatibility with v3.0. This foundation enables **automatic setup reassembly** and **background health scanning** capabilities.

---

## 📋 Core Design Principles

### 1. **Redundancy First**
- **5-copy storage** across strategic spare device sectors
- **Automatic corruption detection** and recovery
- **Consensus-based validation** using multiple copies

### 2. **Backward Compatibility**
- **Complete v3.0 section** embedded in v4.0 metadata
- **Transparent upgrade path** from existing installations
- **Fallback support** for v3.0-only operations

### 3. **Enterprise Reliability**
- **Multi-level checksums** (header + data + sections)
- **Sequence number tracking** for conflict resolution
- **Timestamp-based freshness** validation

### 4. **Configuration Management**
- **Complete setup storage** in metadata
- **Device fingerprinting** for validation
- **Sysfs settings preservation** for automatic reassembly

---

## 🏗️ Metadata Structure Definition

```c
/**
 * dm-remap v4.0 Enhanced Metadata Structure
 * 
 * Design Goals:
 * - Redundant storage with automatic recovery
 * - Complete configuration preservation
 * - Background health scanning support
 * - Full v3.0 backward compatibility
 */

#define DMR_METADATA_V4_MAGIC    0x444D5234  /* "DMR4" */
#define DMR_METADATA_V4_VERSION  0x00040000  /* 4.0.0.0 */
#define DMR_METADATA_COPIES      5           /* Number of redundant copies */

/* Strategic sector locations for metadata copies */
#define DMR_METADATA_SECTORS     { 0, 1024, 2048, 4096, 8192 }

struct dm_remap_metadata_v4 {
    /* ================== INTEGRITY HEADER ================== */
    struct {
        uint32_t magic;                 /* Magic number: DMR_METADATA_V4_MAGIC */
        uint32_t version;               /* Metadata format version */
        uint64_t sequence_number;       /* Monotonic counter for conflict resolution */
        uint32_t copy_index;            /* Which copy this is (0-4) */
        uint64_t timestamp;             /* Creation/update timestamp (seconds since epoch) */
        uint32_t total_size;            /* Total metadata structure size */
        uint32_t header_checksum;       /* CRC32 of this header (excluding this field) */
        uint32_t data_checksum;         /* CRC32 of all data sections */
        uint8_t  reserved_header[32];   /* Reserved for future header extensions */
    } header;

    /* ================== V3.0 COMPATIBILITY SECTION ================== */
    struct {
        /* Complete v3.0 metadata embedded for backward compatibility */
        struct dm_remap_metadata_v3 legacy_data;
        uint32_t legacy_checksum;       /* CRC32 of legacy_data section */
        uint8_t  reserved_legacy[64];   /* Reserved for v3.0 extensions */
    } compatibility;

    /* ================== CONFIGURATION STORAGE SECTION ================== */
    struct {
        /* Main device identification */
        char     main_device_uuid[37];     /* UUID of main device (36 chars + null) */
        char     main_device_path[256];    /* Original device path */
        uint64_t main_device_size;         /* Device size in sectors */
        uint8_t  device_fingerprint[32];   /* SHA-256 device fingerprint */
        
        /* dm-remap target configuration */
        uint32_t target_params_len;        /* Length of stored parameters */
        char     target_params[512];       /* Complete dm-remap target parameters */
        uint32_t target_table_len;         /* Length of device mapper table */
        char     target_table[1024];       /* Complete device mapper table entry */
        
        /* System configuration snapshot */
        char sysfs_config[1024];           /* Sysfs settings snapshot (JSON format) */
        uint32_t config_version;           /* Configuration format version */
        uint32_t config_flags;             /* Configuration option flags */
        
        /* Validation and integrity */
        uint32_t config_checksum;          /* CRC32 of configuration section */
        uint64_t config_timestamp;         /* Configuration creation time */
        uint8_t  reserved_config[128];     /* Reserved for configuration extensions */
    } setup_config;

    /* ================== HEALTH SCANNING SECTION ================== */
    struct {
        /* Scanning configuration */
        uint64_t last_scan_time;           /* Last background scan timestamp */
        uint32_t scan_interval;            /* Configured scan interval (seconds) */
        uint32_t scan_flags;               /* Scanning configuration flags */
        uint32_t sector_scan_progress;     /* Current scan progress (0-100) */
        
        /* Health assessment */
        uint32_t health_score;             /* Overall device health (0-100) */
        uint32_t health_trend;             /* Health trend indicator */
        uint64_t health_last_updated;      /* Health data timestamp */
        
        /* Scanning statistics */
        struct {
            uint32_t scans_completed;      /* Total completed scans */
            uint32_t errors_detected;      /* Errors found during scanning */
            uint32_t predictive_remaps;    /* Predictive remaps performed */
            uint32_t scan_overhead_ms;     /* Average scan overhead per sector */
            uint64_t total_sectors_scanned; /* Cumulative sectors scanned */
            uint64_t total_scan_time_ms;   /* Cumulative scanning time */
        } statistics;
        
        /* Health data storage */
        uint8_t health_data[256];          /* Health-specific data storage */
        uint32_t health_checksum;          /* CRC32 of health section */
        uint8_t reserved_health[128];      /* Reserved for health extensions */
    } health_data;

    /* ================== DISCOVERY AND REASSEMBLY SECTION ================== */
    struct {
        /* Device discovery metadata */
        uint32_t spare_device_count;       /* Number of spare devices in setup */
        char spare_device_paths[8][256];   /* Paths to spare devices */
        uint8_t spare_device_uuids[8][37]; /* UUIDs of spare devices */
        uint8_t spare_fingerprints[8][32]; /* Fingerprints of spare devices */
        
        /* Reassembly validation */
        uint64_t setup_creation_time;      /* Original setup creation timestamp */
        uint32_t setup_modification_count; /* Number of configuration changes */
        uint32_t validation_flags;         /* Setup validation requirement flags */
        
        /* Disaster recovery information */
        char recovery_contact[128];        /* Admin contact information */
        char setup_description[256];       /* Human-readable setup description */
        
        uint32_t discovery_checksum;       /* CRC32 of discovery section */
        uint8_t reserved_discovery[256];   /* Reserved for discovery extensions */
    } discovery_data;

    /* ================== FOOTER AND VALIDATION ================== */
    struct {
        uint64_t metadata_creation_time;   /* Original metadata creation */
        uint64_t metadata_update_time;     /* Last metadata update */
        uint32_t update_count;             /* Number of metadata updates */
        uint32_t validation_signature;     /* Overall validation signature */
        uint8_t reserved_footer[64];       /* Reserved for future extensions */
        uint32_t structure_checksum;       /* CRC32 of entire structure */
    } footer;
} __attribute__((packed));

/* Compile-time size validation */
static_assert(sizeof(struct dm_remap_metadata_v4) <= 8192, 
              "v4.0 metadata must fit in 8KB for efficient storage");
```

---

## 🔄 Redundant Storage Strategy

### Storage Locations
The v4.0 metadata uses **5 strategic locations** on spare devices:

| Copy | Sector | Purpose | Reasoning |
|------|--------|---------|-----------|
| 0 | 0 | Primary | Traditional location, fastest access |
| 1 | 1024 | Secondary | Common spare area, 512KB offset |
| 2 | 2048 | Tertiary | 1MB offset, avoids common wear areas |
| 3 | 4096 | Quaternary | 2MB offset, distributed wear |
| 4 | 8192 | Backup | 4MB offset, maximum distribution |

### Consensus Algorithm
```c
/**
 * Metadata validation and selection algorithm
 * 
 * 1. Read all 5 copies from spare device
 * 2. Validate checksums for each copy
 * 3. Select copy with highest sequence number
 * 4. Verify timestamp consistency
 * 5. Perform integrity validation
 * 6. Return valid metadata or initiate recovery
 */

enum dmr_metadata_validation_result {
    DMR_METADATA_VALID,           /* Metadata is valid and consistent */
    DMR_METADATA_REPAIRABLE,      /* Some copies corrupted but recoverable */
    DMR_METADATA_CONFLICTED,      /* Multiple valid but conflicting copies */
    DMR_METADATA_CORRUPTED,       /* All copies corrupted or missing */
    DMR_METADATA_VERSION_MISMATCH /* Unsupported metadata version */
};

struct dmr_metadata_validation_context {
    struct dm_remap_metadata_v4 copies[DMR_METADATA_COPIES];
    bool copy_valid[DMR_METADATA_COPIES];
    uint64_t max_sequence_number;
    int preferred_copy_index;
    enum dmr_metadata_validation_result result;
};
```

---

## 🔐 Integrity Protection Design

### Multi-Level Checksums
1. **Header Checksum**: Protects metadata header integrity
2. **Section Checksums**: Individual checksums for each major section
3. **Data Checksum**: Overall checksum of all data sections
4. **Structure Checksum**: Final checksum of entire metadata structure

### Checksum Implementation
```c
/**
 * CRC32 implementation for metadata integrity
 * Using kernel's existing crc32() function for consistency
 */

static uint32_t dmr_calculate_header_checksum(const struct dm_remap_metadata_v4 *meta)
{
    /* Calculate CRC32 of header excluding header_checksum field */
    const uint8_t *data = (const uint8_t *)&meta->header;
    size_t len = offsetof(struct dm_remap_metadata_v4, header.header_checksum);
    return crc32(0, data, len);
}

static uint32_t dmr_calculate_section_checksum(const void *section, size_t size)
{
    /* Calculate CRC32 of specific metadata section */
    return crc32(0, (const uint8_t *)section, size);
}

static uint32_t dmr_calculate_structure_checksum(const struct dm_remap_metadata_v4 *meta)
{
    /* Calculate CRC32 of entire structure excluding structure_checksum field */
    const uint8_t *data = (const uint8_t *)meta;
    size_t len = sizeof(*meta) - sizeof(meta->footer.structure_checksum);
    return crc32(0, data, len);
}
```

---

## ⚡ Performance Considerations

### Storage Efficiency
- **Single 8KB block** per metadata copy
- **Aligned to sector boundaries** for optimal I/O
- **Packed structure** to minimize storage overhead

### Access Optimization
- **Primary copy (sector 0)** for fastest normal access
- **Distributed copies** only accessed during validation/recovery
- **Sequential sector layout** for efficient reads

### I/O Impact Targets
- **Metadata read**: <1ms for primary copy
- **Metadata write**: <5ms for all 5 copies
- **Validation**: <10ms for full 5-copy validation
- **Recovery**: <50ms for corruption recovery

---

## 🔄 Backward Compatibility Strategy

### v3.0 Compatibility Section
The `compatibility` section contains a complete v3.0 metadata structure, ensuring:
- **Existing v3.0 tools** can still read essential metadata
- **Gradual migration** without breaking existing setups
- **Fallback capability** if v4.0 features are disabled

### Migration Path
```c
/**
 * Automatic v3.0 → v4.0 metadata upgrade
 * 
 * 1. Detect v3.0 metadata on spare device
 * 2. Create v4.0 structure with v3.0 data in compatibility section
 * 3. Initialize v4.0-specific sections with defaults
 * 4. Write v4.0 metadata to all 5 strategic locations
 * 5. Maintain v3.0 metadata until confirmed v4.0 operation
 */

enum dmr_metadata_upgrade_result {
    DMR_UPGRADE_SUCCESS,          /* Upgrade completed successfully */
    DMR_UPGRADE_PARTIAL,          /* Upgrade started but not completed */
    DMR_UPGRADE_FAILED,           /* Upgrade failed, v3.0 preserved */
    DMR_UPGRADE_NOT_NEEDED        /* Already v4.0 or newer */
};
```

---

## 📊 Configuration Management

### Device Fingerprinting
```c
/**
 * Device fingerprinting for reliable identification
 * 
 * Creates SHA-256 hash of:
 * - Device UUID
 * - Device size
 * - First and last 4KB of device
 * - Device serial number (if available)
 */

struct dmr_device_fingerprint_data {
    char device_uuid[37];         /* Device UUID */
    uint64_t device_size;         /* Size in sectors */
    uint8_t first_4kb[4096];      /* First 4KB of device */
    uint8_t last_4kb[4096];       /* Last 4KB of device */
    char serial_number[64];       /* Device serial (if available) */
    uint64_t timestamp;           /* Fingerprint creation time */
};

static void dmr_calculate_device_fingerprint(struct block_device *bdev,
                                           uint8_t fingerprint[32])
{
    struct dmr_device_fingerprint_data data;
    struct crypto_shash *sha256;
    
    /* Collect device identification data */
    dmr_collect_device_data(bdev, &data);
    
    /* Calculate SHA-256 hash */
    sha256 = crypto_alloc_shash("sha256", 0, 0);
    crypto_shash_digest(sha256, (uint8_t *)&data, sizeof(data), fingerprint);
    crypto_free_shash(sha256);
}
```

### Configuration Serialization
```c
/**
 * Sysfs configuration snapshot in JSON format
 * 
 * Example:
 * {
 *   "remap_threshold": 100,
 *   "auto_remap": true,
 *   "scan_interval": 86400,
 *   "health_monitoring": true,
 *   "debug_level": 1
 * }
 */

static int dmr_serialize_sysfs_config(char *buffer, size_t size)
{
    return snprintf(buffer, size,
        "{\n"
        "  \"remap_threshold\": %d,\n"
        "  \"auto_remap\": %s,\n"
        "  \"scan_interval\": %d,\n"  
        "  \"health_monitoring\": %s,\n"
        "  \"debug_level\": %d\n"
        "}",
        dmr_get_remap_threshold(),
        dmr_get_auto_remap() ? "true" : "false",
        dmr_get_scan_interval(),
        dmr_get_health_monitoring() ? "true" : "false",
        dmr_get_debug_level());
}
```

---

## 🎯 Implementation Milestones

### Week 1 Deliverables ✅
- [x] **Enhanced metadata format specification** (This document)
- [ ] **Redundant storage strategy documentation**
- [ ] **Checksum and integrity protection design**
- [ ] **Conflict resolution algorithm specification**

### Week 2 Deliverables (Next)
- [ ] **Device fingerprinting algorithm implementation**
- [ ] **Background scanning architecture design**
- [ ] **Performance impact analysis methodology**
- [ ] **v3.0 compatibility testing framework**

---

## 📝 Next Steps

1. **Complete Week 1 deliverables** with redundant storage and checksum designs
2. **Research kernel work queues** for background health scanning
3. **Design device fingerprinting** algorithm for reliable identification
4. **Create performance baseline** for v3.0 to measure v4.0 impact

---

**Status**: ✅ **COMPLETED** - Enhanced metadata format specification  
**Next**: Redundant storage strategy documentation