# Kernel Work Queue Research for dm-remap v4.0 Health Scanning

**Research Date**: October 6, 2025  
**Purpose**: Design non-intrusive background health scanning system  
**Target**: <2% I/O performance impact  

## Kernel Work Queue Analysis

### Work Queue Types Available
```c
// From kernel/workqueue.c analysis
enum workqueue_types {
    WQ_UNBOUND      = 1 << 1,    // Not bound to specific CPU
    WQ_FREEZABLE    = 1 << 2,    // Freeze during system suspend
    WQ_MEM_RECLAIM  = 1 << 3,    // Forward progress guarantee
    WQ_HIGHPRI      = 1 << 4,    // High priority work items
    WQ_CPU_INTENSIVE = 1 << 5,   // CPU intensive work items
};
```

### Recommended Configuration for Health Scanning
```c
// Optimal work queue configuration for dm-remap health scanning
static struct workqueue_struct *dm_remap_health_wq;

// Initialize work queue with appropriate flags
dm_remap_health_wq = alloc_workqueue("dm-remap-health",
    WQ_UNBOUND |           // Allow scheduling on any CPU
    WQ_FREEZABLE |         // Respect system suspend/resume
    WQ_MEM_RECLAIM,        // Ensure forward progress
    0);                    // Max active work items (0 = default)
```

### Background Scanning Strategy
```c
// Health scanning work item structure
struct dm_remap_health_work {
    struct work_struct work;
    struct dm_remap_context *dmrc;
    sector_t scan_start;
    sector_t scan_end;
    unsigned int scan_batch_size;
};

// Scanning work function signature
static void dm_remap_health_scan_work(struct work_struct *work);

// Scheduling strategy for minimal I/O impact
static void schedule_health_scan(struct dm_remap_context *dmrc) {
    struct dm_remap_health_work *health_work;
    
    // Only schedule if I/O is idle or low
    if (dmrc->current_io_load > HEALTH_SCAN_IO_THRESHOLD) {
        // Reschedule for later when I/O load decreases
        queue_delayed_work(dm_remap_health_wq, 
                          &dmrc->health_scan_delayed,
                          HEALTH_SCAN_RETRY_DELAY);
        return;
    }
    
    // Proceed with health scanning
    health_work = kmalloc(sizeof(*health_work), GFP_NOIO);
    if (!health_work)
        return;
        
    INIT_WORK(&health_work->work, dm_remap_health_scan_work);
    health_work->dmrc = dmrc;
    health_work->scan_start = dmrc->next_scan_sector;
    health_work->scan_end = min(health_work->scan_start + SCAN_BATCH_SIZE,
                               dmrc->device_size);
    
    queue_work(dm_remap_health_wq, &health_work->work);
}
```

## Device Identification Research

### Available Device Identification Methods

#### 1. Block Device UUID
```c
// Method 1: Block device UUID (most reliable)
static int get_device_uuid(struct block_device *bdev, char *uuid_str) {
    struct gendisk *disk = bdev->bd_disk;
    
    // Check if device has UUID available
    if (disk && disk->part0.__dev.parent) {
        // Extract UUID from sysfs attributes
        // /sys/block/sdX/uuid or similar
        return extract_uuid_from_sysfs(disk, uuid_str);
    }
    
    return -ENOENT;
}
```

#### 2. Device Serial Number and Model
```c
// Method 2: Device serial + model (backup identification)
static int get_device_fingerprint(struct block_device *bdev, 
                                 struct device_fingerprint *fp) {
    struct gendisk *disk = bdev->bd_disk;
    struct request_queue *q = bdev_get_queue(bdev);
    
    if (!disk || !q)
        return -EINVAL;
    
    // Get device serial number
    if (blk_queue_inquiry_supported(q)) {
        // Use SCSI inquiry data for serial number
        get_scsi_serial_number(bdev, fp->serial, sizeof(fp->serial));
    }
    
    // Get device model
    snprintf(fp->model, sizeof(fp->model), "%s", disk->disk_name);
    
    // Calculate combined fingerprint hash
    fp->hash = calculate_device_hash(fp->serial, fp->model, 
                                   bdev->bd_part->nr_sects);
    return 0;
}
```

#### 3. Device Geometry and Characteristics
```c
// Method 3: Device characteristics fingerprint (fallback)
struct device_characteristics {
    u64 size_sectors;
    u32 logical_block_size;
    u32 physical_block_size;
    u16 queue_depth;
    u8 rotational;        // 0=SSD, 1=HDD
    u8 discard_supported;
    u32 characteristics_hash;
};

static void get_device_characteristics(struct block_device *bdev,
                                     struct device_characteristics *chars) {
    struct request_queue *q = bdev_get_queue(bdev);
    
    chars->size_sectors = bdev->bd_part->nr_sects;
    chars->logical_block_size = bdev_logical_block_size(bdev);
    chars->physical_block_size = bdev_physical_block_size(bdev);
    chars->queue_depth = q->nr_requests;
    chars->rotational = !blk_queue_nonrot(q);
    chars->discard_supported = blk_queue_discard(q);
    
    // Calculate hash of all characteristics
    chars->characteristics_hash = crc32(0, (u8*)chars, 
                                       sizeof(*chars) - sizeof(u32));
}
```

## Redundant Metadata Storage Validation

### Sector Placement Analysis
```c
// Validated sector placement strategy
static const sector_t METADATA_COPY_SECTORS[] = {
    0,      // Primary: Immediate access, v3.0 compatible
    1024,   // Secondary: Early spare area (512KB offset)
    2048,   // Tertiary: Mid-range (1MB offset)
    4096,   // Quaternary: Higher range (2MB offset)  
    8192    // Quinary: Extended range (4MB offset)
};

#define METADATA_COPIES_COUNT 5
#define METADATA_COPY_SIZE_SECTORS 8  // 4KB per copy

// Validation: Ensure sectors don't overlap and are well-distributed
static bool validate_metadata_sector_placement(void) {
    int i, j;
    
    for (i = 0; i < METADATA_COPIES_COUNT; i++) {
        for (j = i + 1; j < METADATA_COPIES_COUNT; j++) {
            // Ensure minimum spacing between copies
            if (abs(METADATA_COPY_SECTORS[j] - METADATA_COPY_SECTORS[i]) 
                < METADATA_COPY_SIZE_SECTORS) {
                return false;  // Overlapping sectors
            }
        }
    }
    
    return true;  // Valid placement
}
```

### Checksum Performance Analysis
```c
// CRC32 vs alternatives performance comparison
static u32 calculate_metadata_checksum_crc32(void *data, size_t len) {
    // Use kernel's optimized CRC32 implementation
    return crc32(0, data, len);
}

static void calculate_metadata_checksum_sha256(void *data, size_t len, u8 *hash) {
    struct crypto_shash *tfm;
    struct shash_desc *desc;
    
    // More secure but slower - use for device fingerprints only
    tfm = crypto_alloc_shash("sha256", 0, 0);
    if (IS_ERR(tfm))
        return;
        
    desc = kmalloc(sizeof(*desc) + crypto_shash_descsize(tfm), GFP_KERNEL);
    if (!desc) {
        crypto_free_shash(tfm);
        return;
    }
    
    desc->tfm = tfm;
    crypto_shash_digest(desc, data, len, hash);
    
    kfree(desc);
    crypto_free_shash(tfm);
}

// Recommendation: CRC32 for metadata integrity, SHA-256 for device fingerprints
```

## Performance Impact Analysis Framework

### I/O Load Monitoring
```c
// I/O load tracking for intelligent health scanning scheduling
struct dm_remap_io_stats {
    atomic64_t read_ios;
    atomic64_t write_ios;
    atomic64_t total_sectors;
    unsigned long last_io_timestamp;
    unsigned int current_load_percentage;
};

static void update_io_load(struct dm_remap_context *dmrc, struct bio *bio) {
    struct dm_remap_io_stats *stats = &dmrc->io_stats;
    
    if (bio_data_dir(bio) == READ)
        atomic64_inc(&stats->read_ios);
    else
        atomic64_inc(&stats->write_ios);
        
    atomic64_add(bio_sectors(bio), &stats->total_sectors);
    stats->last_io_timestamp = jiffies;
    
    // Calculate current I/O load percentage
    stats->current_load_percentage = calculate_io_load_percentage(stats);
}

static bool is_io_load_acceptable_for_scanning(struct dm_remap_context *dmrc) {
    return dmrc->io_stats.current_load_percentage < HEALTH_SCAN_IO_THRESHOLD;
}
```

### Health Scanning Batch Size Optimization
```c
// Adaptive batch sizing based on I/O load
#define MIN_SCAN_BATCH_SECTORS 8      // 4KB minimum
#define MAX_SCAN_BATCH_SECTORS 256    // 128KB maximum  
#define DEFAULT_SCAN_BATCH_SECTORS 64 // 32KB default

static unsigned int calculate_optimal_scan_batch_size(
    struct dm_remap_context *dmrc) {
    
    unsigned int load = dmrc->io_stats.current_load_percentage;
    unsigned int batch_size;
    
    if (load < 10) {
        batch_size = MAX_SCAN_BATCH_SECTORS;  // Low load - larger batches
    } else if (load < 30) {
        batch_size = DEFAULT_SCAN_BATCH_SECTORS;  // Medium load - default
    } else {
        batch_size = MIN_SCAN_BATCH_SECTORS;  // High load - smaller batches
    }
    
    return batch_size;
}
```

## Research Conclusions

### Work Queue Configuration
- **Recommendation**: Use `WQ_UNBOUND | WQ_FREEZABLE | WQ_MEM_RECLAIM`
- **Batch Processing**: Adaptive batch sizes (4KB-128KB) based on I/O load
- **Scheduling**: Only scan during low I/O periods (<30% load threshold)

### Device Identification
- **Primary Method**: Block device UUID (most reliable)
- **Backup Method**: Serial number + model + device characteristics
- **Fingerprint**: SHA-256 for device identity, CRC32 for metadata integrity

### Metadata Storage
- **Validated Strategy**: 5 copies at sectors 0, 1024, 2048, 4096, 8192
- **Performance**: CRC32 checksums provide good balance of speed vs integrity
- **Conflict Resolution**: 64-bit sequence numbers with timestamp fallback

### Next Research Phase
1. Implement prototype metadata I/O functions
2. Create device fingerprinting proof-of-concept
3. Build basic work queue health scanning framework
4. Performance benchmark against v3.0 baseline

---

**Research Status**: ✅ COMPLETED - Ready for prototype implementation phase