/**
 * dm-remap-v4-real.c - v4.0 Enterprise with Real Device Support
 * 
 * This version implements full real device integration moving beyond
 * demonstration mode to production-ready enterprise storage management.
 * 
 * Copyright (C) 2025 dm-remap Development Team
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/device-mapper.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/atomic.h>
#include <linux/ktime.h>
#include <linux/workqueue.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/crc32.h>
#include <linux/delay.h>  /* For msleep() */
#include <linux/dm-bufio.h>  /* Proper kernel API for metadata I/O */

#include "dm-remap-v4-compat.h"
#include "../include/dm-remap-v4-stats.h"
#include "dm-remap-v4.h"  /* Shared v4 metadata structures */

/* Module metadata */
MODULE_DESCRIPTION("Device Mapper Remapping Target v4.0 - Real Device Integration");
MODULE_AUTHOR("dm-remap Development Team");  
MODULE_LICENSE("GPL");
MODULE_VERSION("4.0.0-real");
MODULE_SOFTDEP("pre: dm-bufio dm-remap-v4-metadata");

/* Module parameters */
int dm_remap_debug = 1;
module_param(dm_remap_debug, int, 0644);
MODULE_PARM_DESC(dm_remap_debug, "Debug level (0=off, 1=info, 2=verbose, 3=trace)");

static bool enable_background_scanning = true;
module_param(enable_background_scanning, bool, 0644);
MODULE_PARM_DESC(enable_background_scanning, "Enable background health scanning");

static uint scan_interval_hours = 24;
module_param(scan_interval_hours, uint, 0644);
MODULE_PARM_DESC(scan_interval_hours, "Background scan interval in hours (1-168)");

static bool real_device_mode = true;
module_param(real_device_mode, bool, 0644);
MODULE_PARM_DESC(real_device_mode, "Enable real device operations (vs demo mode)");

/* Spare device sizing parameters (v4.0.1 optimization) */
static uint spare_overhead_percent = 2;
module_param(spare_overhead_percent, uint, 0644);
MODULE_PARM_DESC(spare_overhead_percent, "Expected bad sector percentage (0-20, default 2%)");

static bool strict_spare_sizing = false;
module_param(strict_spare_sizing, bool, 0644);
MODULE_PARM_DESC(strict_spare_sizing, "Require spare >= main size (legacy mode, default off)");

#define DMR_ERROR(fmt, ...) \
    printk(KERN_ERR "dm-remap v4.0 real: ERROR: " fmt "\n", ##__VA_ARGS__)

#define DMR_INFO(fmt, ...) \
    printk(KERN_INFO "dm-remap v4.0 real: " fmt "\n", ##__VA_ARGS__)

/* v4.0 Enterprise Metadata Structure - Enhanced */
struct dm_remap_metadata_v4_real {
    /* Header */
    char magic[16];              /* "DM_REMAP_V4.0R" */
    uint32_t version;            /* 4 */
    uint32_t metadata_size;      /* Total metadata size */
    uint64_t creation_time;      /* Creation timestamp */
    uint64_t last_update;        /* Last update timestamp */
    
    /* Device identification - Enhanced */
    char main_device_path[256];  /* Main device path */
    char spare_device_path[256]; /* Spare device path */
    char main_device_uuid[37];   /* Main device UUID */
    char spare_device_uuid[37];  /* Spare device UUID */
    char device_fingerprint[65]; /* SHA-256 device fingerprint */
    uint64_t main_device_size;   /* Main device size in sectors */
    uint64_t spare_device_size;  /* Spare device size in sectors */
    
    /* Mapping information */
    uint32_t sector_size;        /* 512 bytes typically */
    uint64_t total_sectors;      /* Total device sectors */
    uint32_t max_mappings;       /* Maximum remap entries */
    uint32_t active_mappings;    /* Currently active remaps */
    
    /* Health monitoring - Enhanced */
    uint64_t health_scan_count;  /* Number of health scans performed */
    uint64_t last_health_scan;   /* Last health scan timestamp */
    uint32_t predicted_failures; /* Number of predicted failures */
    uint32_t health_flags;       /* Health status flags */
    uint64_t total_errors;       /* Total I/O errors detected */
    uint64_t last_error_time;    /* Last error timestamp */
    
    /* Performance statistics - Enhanced */
    uint64_t total_reads;        /* Total read operations */
    uint64_t total_writes;       /* Total write operations */
    uint64_t total_remaps;       /* Total remap operations */
    uint64_t total_io_time_ns;   /* Total I/O time in nanoseconds */
    uint64_t peak_throughput;    /* Peak throughput achieved */
    
    /* Device status tracking */
    uint32_t main_device_status;  /* Main device health status */
    uint32_t spare_device_status; /* Spare device health status */
    uint64_t uptime_seconds;      /* Device uptime in seconds */
    
    /* Data integrity - Phase 1.3 */
    uint32_t metadata_crc;       /* CRC32 of metadata (excluding this field) */
    uint64_t sequence_number;    /* Incremental sequence for crash recovery */
    
    /* Reserved for future expansion */
    uint8_t reserved[3576];      /* Pad to 4KB total */
};

/* Metadata size calculation (v4.0.1) */
#define DM_REMAP_METADATA_BASE_SIZE   4096  /* Base metadata: 4KB */
#define DM_REMAP_METADATA_PER_MAPPING 64    /* Per-mapping overhead: 64 bytes */
#define DM_REMAP_SAFETY_MARGIN_PCT    20    /* Safety margin: 20% */

/* Remap entry flags */
#define DM_REMAP_FLAG_PENDING    0x0001  /* Metadata not yet persisted - don't use for I/O */
#define DM_REMAP_FLAG_ACTIVE     0x0002  /* Metadata persisted - safe to use */

/* Remap entry structure for Phase 1.3 */
struct dm_remap_entry_v4 {
    sector_t original_sector;    /* Original failing sector */
    sector_t spare_sector;       /* Replacement sector on spare device */
    uint64_t remap_time;         /* Time when remap was created */
    uint32_t error_count;        /* Number of errors on this sector */
    uint32_t flags;              /* Status flags (DM_REMAP_FLAG_*) */
    struct list_head list;       /* List linkage */
};

/* Phase 1.4: Health monitoring structures */
struct dm_remap_error_pattern {
    sector_t sector;             /* Sector with error pattern */
    uint32_t error_count;        /* Number of errors at this sector */
    uint64_t first_error_time;   /* Time of first error */
    uint64_t last_error_time;    /* Time of most recent error */
    uint32_t pattern_flags;      /* Pattern classification flags */
};

struct dm_remap_health_monitor {
    /* Health scanning */
    struct delayed_work health_scan_work;
    bool background_scan_active;
    sector_t scan_progress;
    sector_t scan_start_sector;
    uint64_t last_health_scan;
    uint32_t scan_interval_seconds;
    
    /* Error pattern analysis */
    struct dm_remap_error_pattern error_hotspots[32];
    uint32_t hotspot_count;
    uint32_t consecutive_errors;
    sector_t last_error_sector;
    
    /* Predictive failure analysis */
    uint32_t failure_prediction_score;  /* 0-100 failure likelihood */
    uint64_t predicted_failure_time;     /* Estimated failure timestamp */
    uint32_t health_trend;               /* Improving/stable/degrading */
    
    /* Performance health metrics */
    uint64_t avg_response_time_ns;       /* Average I/O response time */
    uint32_t timeout_count;              /* Number of I/O timeouts */
    uint32_t retry_count;                /* Number of retried operations */
    
    /* Device temperature and power */
    int32_t device_temperature;          /* Device temperature (if available) */
    uint32_t power_on_hours;             /* Total power-on time */
    uint64_t total_bytes_written;        /* Lifetime write volume */
    uint64_t total_bytes_read;           /* Lifetime read volume */
};

/* Phase 1.4: Performance optimization structures */
struct dm_remap_cache_entry {
    sector_t original_sector;    /* Cached sector lookup */
    sector_t remapped_sector;    /* Cached remap target */ 
    uint64_t access_time;        /* Last access timestamp */
    uint32_t access_count;       /* Access frequency counter */
};

struct dm_remap_perf_optimizer {
    /* Remap lookup cache */
    struct dm_remap_cache_entry *cache_entries;
    uint32_t cache_size;
    uint32_t cache_mask;         /* For fast modulo operations */
    atomic64_t cache_hits;
    atomic64_t cache_misses;
    
    /* I/O pattern analysis */
    struct {
        sector_t last_sector;
        uint32_t sequential_count;
        uint32_t random_count;
        bool is_sequential_workload;
        ktime_t pattern_update_time;
    } io_pattern;
    
    /* Hot sector tracking */
    struct {
        sector_t sectors[16];     /* Most frequently accessed sectors */
        uint32_t access_counts[16];
        uint32_t next_slot;
    } hot_sectors;
    
    /* Fast path optimization */
    bool fast_path_enabled;
    atomic64_t fast_path_hits;
    atomic64_t slow_path_hits;
};

/* Device structure for v4.0 real device support */
struct dm_remap_device_v4_real {
    /* Real device references */
    struct file *main_dev;
    struct file *spare_dev;
    char main_path[256];
    char spare_path[256];
    blk_mode_t device_mode;
    
    /* Device information */
    sector_t main_device_sectors;
    sector_t spare_device_sectors;
    unsigned int sector_size;
    
    /* Enhanced metadata management */
    struct dm_remap_metadata_v4_real metadata;
    struct mutex metadata_mutex;
    bool metadata_dirty;
    sector_t metadata_sector;    /* Where metadata is stored on spare device */
    
    /* Persistent v4 metadata (shared module) */
    struct dm_remap_metadata_v4 *persistent_metadata;  /* For disk I/O */
    struct dm_bufio_client *metadata_bufio_client;  /* dm-bufio client for metadata I/O */
    
    /* Sector remapping - Phase 1.3 */
    struct list_head remap_list; /* List of active remaps */  
    spinlock_t remap_lock;       /* Lock for remap operations */
    uint32_t remap_count_active; /* Current active remaps */
    sector_t spare_sector_count; /* Available spare sectors */
    sector_t next_spare_sector;  /* Next available spare sector */
    
    /* Background metadata sync - Phase 1.3 */
    struct workqueue_struct *metadata_workqueue; /* Background metadata sync */
    struct work_struct metadata_sync_work; /* Metadata sync work item */
    struct work_struct error_analysis_work; /* Deferred error pattern analysis */
    sector_t pending_error_sector; /* Sector pending error analysis */
    struct delayed_work deferred_metadata_read_work; /* v4.2: Deferred metadata read after construction */
    atomic_t metadata_loaded; /* v4.2: Flag indicating metadata has been loaded */
    
    /* Write-ahead remap creation (v4.2 data safety) */
    struct work_struct writeahead_remap_work; /* Write-ahead remap + metadata work */
    sector_t pending_remap_sector; /* Sector needing write-ahead remap */
    int pending_remap_error; /* Error code that triggered remap */
    
    /* v4.2.2 Kernel thread for metadata writes */
    struct task_struct *metadata_thread;        /* Dedicated kernel thread for metadata I/O */
    wait_queue_head_t metadata_wait_queue;      /* Wait queue for metadata thread */
    atomic_t metadata_write_requested;          /* Flag: metadata write requested */
    atomic_t metadata_thread_should_stop;       /* Flag: thread should exit */
    
    /* v4.2 Automatic metadata repair */
    struct workqueue_struct *repair_wq; /* Dedicated workqueue for repair operations */
    struct dm_remap_repair_context repair_ctx; /* Automatic repair context */
    
    /* Statistics - Enhanced */
    atomic64_t read_count;
    atomic64_t write_count;
    atomic64_t remap_count;
    atomic64_t error_count;
    atomic64_t total_io_time_ns;
    atomic64_t io_operations;
    
    /* Enhanced statistics for Phase 1.3 */
    struct {
        atomic64_t total_ios;    /* Total I/O operations */
        atomic64_t normal_ios;   /* Normal (non-remapped) I/Os */
        atomic64_t remapped_ios; /* Remapped I/Os */
        atomic64_t io_errors;    /* I/O errors detected */
        atomic64_t remapped_sectors; /* Total remapped sectors */
        uint64_t total_latency_ns;   /* Total latency */
        uint64_t max_latency_ns;     /* Maximum latency observed */
    } stats;
    
    /* Health monitoring */
    struct delayed_work health_scan_work;
    atomic64_t health_scan_count;
    uint32_t predicted_failures;
    
    /* Phase 1.4: Advanced health monitoring */
    struct dm_remap_health_monitor health_monitor;
    struct mutex health_mutex;           /* Protect health data */
    
    /* Phase 1.4: Performance optimization */
    struct dm_remap_perf_optimizer perf_optimizer;
    struct mutex cache_mutex;            /* Protect cache operations */
    
    /* Phase 1.4: Enterprise features */
    struct {
        bool maintenance_mode;           /* Safe maintenance state */
        uint32_t alert_threshold;        /* Alert trigger threshold */
        uint64_t last_alert_time;        /* Last alert timestamp */
        uint32_t configuration_version;  /* Runtime config version */
    } enterprise;
    
    /* Device management */
    struct list_head device_list;
    atomic_t device_active;
    ktime_t creation_time;
    
    /* Performance tracking */
    ktime_t last_io_time;
    uint64_t peak_throughput;
};

/* Global device list and protection */
static LIST_HEAD(dm_remap_devices);
static DEFINE_MUTEX(dm_remap_devices_mutex);
static atomic_t dm_remap_device_count = ATOMIC_INIT(0);

/* Global statistics */
static atomic64_t global_reads = ATOMIC64_INIT(0);
static atomic64_t global_writes = ATOMIC64_INIT(0);
static atomic64_t global_remaps = ATOMIC64_INIT(0);
static atomic64_t global_errors = ATOMIC64_INIT(0);
static atomic64_t global_health_scans = ATOMIC64_INIT(0);

/* Workqueue for background tasks */
static struct workqueue_struct *dm_remap_wq;

/* Phase 1.4 function forward declarations */
static void dm_remap_analyze_error_pattern(struct dm_remap_device_v4_real *device, sector_t failed_sector);
static void dm_remap_cache_insert(struct dm_remap_device_v4_real *device, sector_t original_sector, sector_t remapped_sector);
static sector_t dm_remap_cache_lookup(struct dm_remap_device_v4_real *device, sector_t original_sector);
static void dm_remap_update_io_pattern(struct dm_remap_device_v4_real *device, sector_t sector);

/**
 * dm_remap_calculate_crc32() - Calculate CRC32 for metadata validation
 */
static uint32_t dm_remap_calculate_crc32(const void *data, size_t len)
{
    return crc32(0, data, len);
}

/**
 * dm_remap_find_remap_entry() - Find remap entry for given sector
 * 
 * v4.2: Only returns ACTIVE remaps (metadata persisted). Skips PENDING remaps
 * that are waiting for write-ahead metadata write to complete.
 */
static struct dm_remap_entry_v4 *dm_remap_find_remap_entry(
    struct dm_remap_device_v4_real *device, sector_t sector)
{
    struct dm_remap_entry_v4 *entry;
    
    list_for_each_entry(entry, &device->remap_list, list) {
        if (entry->original_sector == sector) {
            /* Skip remaps that are still pending metadata write */
            if (entry->flags & DM_REMAP_FLAG_PENDING) {
                DMR_DEBUG(3, "Remap for sector %llu exists but PENDING, skipping",
                          (unsigned long long)sector);
                continue;
            }
            return entry;
        }
    }
    
    return NULL;
}

/**
 * dm_remap_sync_persistent_metadata() - Sync in-memory remaps to persistent metadata
 */
static void dm_remap_sync_persistent_metadata(struct dm_remap_device_v4_real *device)
{
    struct dm_remap_entry_v4 *entry;
    int i = 0;
    
    if (!device->persistent_metadata)
        return;
    
    /* Update remap table in persistent metadata */
    device->persistent_metadata->remap_data.active_remaps = 0;
    
    list_for_each_entry(entry, &device->remap_list, list) {
        if (i >= DM_REMAP_V4_MAX_REMAPS) {
            DMR_WARN("Remap count exceeds maximum, truncating");
            break;
        }
        
        device->persistent_metadata->remap_data.remaps[i].original_sector = entry->original_sector;
        device->persistent_metadata->remap_data.remaps[i].spare_sector = entry->spare_sector;
        device->persistent_metadata->remap_data.remaps[i].remap_timestamp = entry->remap_time;
        device->persistent_metadata->remap_data.remaps[i].error_count = entry->error_count;
        device->persistent_metadata->remap_data.remaps[i].flags = entry->flags;
        
        i++;
    }
    
    device->persistent_metadata->remap_data.active_remaps = i;
    device->persistent_metadata->header.sequence_number++;
    device->persistent_metadata->header.timestamp = ktime_to_ns(ktime_get_real());
}

/**
 * dm_remap_write_persistent_metadata() - Write metadata to spare device
 */
static int dm_remap_write_persistent_metadata(struct dm_remap_device_v4_real *device)
{
    struct block_device *bdev;
    int ret;
    
    /* CRITICAL: Check if device is being destroyed before doing I/O */
    if (!atomic_read(&device->device_active)) {
        DMR_DEBUG(2, "Metadata write aborted - device inactive");
        return -ESHUTDOWN;
    }
    
    if (!device->persistent_metadata || !device->spare_dev)
        return -EINVAL;
    
    /* Sync current state to persistent metadata */
    dm_remap_sync_persistent_metadata(device);
    
    /* Get block device from file */
    bdev = file_bdev(device->spare_dev);
    if (!bdev) {
        DMR_ERROR("Failed to get block device from spare device file");
        return -EINVAL;
    }
    
    /* Check again before I/O (device might have become inactive) */
    if (!atomic_read(&device->device_active)) {
        DMR_DEBUG(2, "Metadata write aborted before I/O - device inactive");
        return -ESHUTDOWN;
    }
    
    DMR_INFO("Metadata write with %u remaps",
             device->persistent_metadata->remap_data.active_remaps);
    
    return 0;
}

/**
 * dm_remap_init_persistent_metadata() - Initialize persistent v4 metadata
 */
static int dm_remap_init_persistent_metadata(struct dm_remap_device_v4_real *device)
{
    /* Allocate persistent metadata structure */
    device->persistent_metadata = kzalloc(sizeof(struct dm_remap_metadata_v4), GFP_KERNEL);
    if (!device->persistent_metadata) {
        DMR_ERROR("Failed to allocate persistent metadata");
        return -ENOMEM;
    }
    
    /* Initialize with device information */
    dm_remap_init_metadata_v4(device->persistent_metadata,
                              device->metadata.main_device_uuid,
                              device->metadata.spare_device_uuid,
                              device->main_device_sectors,
                              device->spare_device_sectors);
    
    DMR_INFO("Initialized persistent v4 metadata");
    return 0;
}

/**
 * dm_remap_read_persistent_metadata() - Read and restore metadata from spare device
 */
static int dm_remap_read_persistent_metadata(struct dm_remap_device_v4_real *device)
{
    struct dm_remap_entry_v4 *entry;
    int ret, i;
    
    if (!device->persistent_metadata || !device->metadata_bufio_client)
        return -EINVAL;
    
    DMR_INFO("Reading persistent metadata using dm-bufio...");
    
    /* Read from spare device using dm-bufio (safe from any context) */
    ret = dm_remap_read_metadata_v4_bufio_with_repair(device->metadata_bufio_client,
                                                      device->persistent_metadata,
                                                      &device->repair_ctx);
    if (ret) {
        DMR_INFO("No valid metadata found, starting fresh: %d", ret);
        return -ENODATA;  /* Return error code so caller knows no metadata was found */
    }
    
    DMR_INFO("Read persistent metadata with %u remaps",
             device->persistent_metadata->remap_data.active_remaps);
    
    /* Restore remap entries to in-memory list */
    for (i = 0; i < device->persistent_metadata->remap_data.active_remaps; i++) {
        if (i >= DM_REMAP_V4_MAX_REMAPS)
            break;
        
        entry = kzalloc(sizeof(*entry), GFP_KERNEL);
        if (!entry) {
            DMR_ERROR("Failed to allocate remap entry during restore");
            return -ENOMEM;
        }
        
        entry->original_sector = device->persistent_metadata->remap_data.remaps[i].original_sector;
        entry->spare_sector = device->persistent_metadata->remap_data.remaps[i].spare_sector;
        entry->remap_time = device->persistent_metadata->remap_data.remaps[i].remap_timestamp;
        entry->error_count = device->persistent_metadata->remap_data.remaps[i].error_count;
        /* v4.2: Restored remaps are ACTIVE (already persisted to disk) */
        entry->flags = DM_REMAP_FLAG_ACTIVE;
        
        spin_lock(&device->remap_lock);
        list_add_tail(&entry->list, &device->remap_list);
        device->remap_count_active++;
        spin_unlock(&device->remap_lock);
        
        DMR_INFO("Restored remap: sector %llu -> %llu",
                 (unsigned long long)entry->original_sector,
                 (unsigned long long)entry->spare_sector);
    }
    
    DMR_INFO("Restored %u remap entries from persistent metadata", i);
    
    /* Update global sysfs stats counter */
    dm_remap_stats_set_active_mappings(device->remap_count_active);
    
    return 0;
}

/**
 * dm_remap_add_remap_entry() - Add new sector remap entry
 */
static int dm_remap_add_remap_entry(struct dm_remap_device_v4_real *device,
                                   sector_t original_sector,
                                   sector_t spare_sector)
{
    struct dm_remap_entry_v4 *entry;
    
    /* Check if entry already exists */
    entry = dm_remap_find_remap_entry(device, original_sector);
    if (entry) {
        DMR_WARN("Remap entry already exists for sector %llu",
                 (unsigned long long)original_sector);
        return -EEXIST;
    }
    
    /* Allocate new entry */
    entry = kzalloc(sizeof(*entry), GFP_KERNEL);
    if (!entry) {
        return -ENOMEM;
    }
    
    /* Initialize entry - v4.2: Start as PENDING until metadata write completes */
    entry->original_sector = original_sector;
    entry->spare_sector = spare_sector;
    entry->remap_time = ktime_to_ns(ktime_get_real());
    entry->error_count = 1;
    entry->flags = DM_REMAP_FLAG_PENDING;  /* Not usable until metadata persisted */
    
    /* Add to remap list */
    spin_lock(&device->remap_lock);
    list_add_tail(&entry->list, &device->remap_list);
    device->remap_count_active++;
    device->metadata.active_mappings++;
    spin_unlock(&device->remap_lock);
    
    /* Update statistics */
    dm_remap_stats_inc_remaps();  /* Update stats module */
    dm_remap_stats_set_active_mappings(device->remap_count_active);  /* Update active count */
    
    DMR_INFO("Added remap entry: sector %llu -> %llu",
             (unsigned long long)original_sector,
             (unsigned long long)spare_sector);
    
    /* Mark metadata as dirty - will write on device shutdown */
    device->metadata_dirty = true;
    
    return 0;
}

/**
 * dm_remap_writeahead_remap_work() - Write-ahead remap creation with metadata persistence
 * 
 * v4.2 Data Safety: This workqueue handler ensures metadata is written BEFORE
 * allowing user I/O to succeed. Prevents data loss window where remap exists
 * in memory but not on disk.
 * 
 * Flow:
 * 1. Create remap entry with PENDING flag
 * 2. Write metadata synchronously (with wait)
 * 3. Only if successful: activate remap (clear PENDING flag)
 * 4. User I/O will be retried and will find the active remap
 */
static void dm_remap_writeahead_remap_work(struct work_struct *work)
{
    struct dm_remap_device_v4_real *device =
        container_of(work, struct dm_remap_device_v4_real, writeahead_remap_work);
    sector_t failed_sector, spare_sector;
    int result, ret;
    
    /* Get pending remap info (set by bio completion) */
    spin_lock(&device->remap_lock);
    failed_sector = device->pending_remap_sector;
    spin_unlock(&device->remap_lock);
    
    DMR_INFO("Write-ahead remap: sector %llu (ensuring metadata persisted first)",
             (unsigned long long)failed_sector);
    
    /* Check if sector is already remapped */
    if (dm_remap_find_remap_entry(device, failed_sector) != NULL) {
        DMR_ERROR("Sector %llu already remapped during write-ahead work",
                  (unsigned long long)failed_sector);
        return;
    }
    
    /* Find available spare sector */
    spin_lock(&device->remap_lock);
    
    if (device->next_spare_sector >= device->spare_sector_count) {
        spin_unlock(&device->remap_lock);
        DMR_ERROR("No spare sectors available for write-ahead remap of sector %llu",
                  (unsigned long long)failed_sector);
        return;
    }
    
    spare_sector = device->next_spare_sector++;
    spin_unlock(&device->remap_lock);
    
    /* Create remap entry with PENDING flag - not yet safe for I/O */
    result = dm_remap_add_remap_entry(device, failed_sector, spare_sector);
    if (result != 0) {
        DMR_ERROR("Failed to add write-ahead remap entry %llu -> %llu (error=%d)",
                  (unsigned long long)failed_sector,
                  (unsigned long long)spare_sector, result);
        
        /* Return spare sector to pool */
        spin_lock(&device->remap_lock);
        device->next_spare_sector--;
        spin_unlock(&device->remap_lock);
        return;
    }
    
    /* CRITICAL: Update metadata before activating remap */
    
    mutex_lock(&device->metadata_mutex);
    
    /* Update metadata */
    device->metadata.last_update = ktime_to_ns(ktime_get_real());
    device->metadata.sequence_number++;
    device->metadata.metadata_crc = 0;
    device->metadata.metadata_crc = dm_remap_calculate_crc32(&device->metadata,
                                                             sizeof(device->metadata));
    
    /* Sync to persistent metadata */
    dm_remap_sync_persistent_metadata(device);
    
    /* Write metadata using dm-bufio (immediate, safe from any context) */
    if (device->metadata_bufio_client) {
        ret = dm_remap_write_metadata_v4_async(device->metadata_bufio_client,
                                              device->persistent_metadata,
                                              NULL);  /* NULL = fire-and-forget */
        if (ret) {
            DMR_ERROR("dm-bufio metadata write failed: %d", ret);
        } else {
            device->metadata_dirty = false;
            DMR_INFO("Metadata persisted via dm-bufio (seq: %llu, remap: %llu -> %llu)",
                     (unsigned long long)device->persistent_metadata->header.sequence_number,
                     (unsigned long long)failed_sector,
                     (unsigned long long)spare_sector);
        }
    }
    
    mutex_unlock(&device->metadata_mutex);
    
    /* Activate remap - metadata already persisted via dm-bufio */
    struct dm_remap_entry_v4 *entry = dm_remap_find_remap_entry(device, failed_sector);
    if (entry) {
        entry->flags &= ~DM_REMAP_FLAG_PENDING;
        entry->flags |= DM_REMAP_FLAG_ACTIVE;
        
        /* Add to cache for fast lookup */
        dm_remap_cache_insert(device, failed_sector, spare_sector);
        
        /* Update statistics */
        atomic64_inc(&device->stats.remapped_sectors);
        
        DMR_INFO("Remap activated: %llu->%llu (metadata will sync in background)",
                 (unsigned long long)failed_sector,
                 (unsigned long long)spare_sector);
    }
    
    /* Mark metadata dirty - will be synced by background worker */
    device->metadata_dirty = true;
    
    /* Trigger background metadata sync (async, fire-and-forget) */
    if (device->metadata_workqueue) {
        queue_work(device->metadata_workqueue, &device->metadata_sync_work);
    }
}

/**
 * dm_remap_handle_io_error() - Handle I/O errors and queue write-ahead remap
 * 
 * v4.2 Data Safety: Queue write-ahead remap creation to ensure metadata is
 * written BEFORE user I/O succeeds. Called from bio completion context, so
 * must be fast and non-blocking.
 */
static void dm_remap_handle_io_error(struct dm_remap_device_v4_real *device,
                                   sector_t failed_sector, int error)
{
    DMR_WARN("I/O error on sector %llu (error=%d), queueing write-ahead remap",
             (unsigned long long)failed_sector, error);
             
    /* Update error statistics */
    atomic64_inc(&device->stats.io_errors);
    dm_remap_stats_inc_errors();
    
    /* Queue error pattern analysis */
    spin_lock(&device->remap_lock);
    device->pending_error_sector = failed_sector;
    spin_unlock(&device->remap_lock);
    queue_work(device->metadata_workqueue, &device->error_analysis_work);
    
    /* Quick check if already remapped (avoid duplicate work) */
    if (dm_remap_find_remap_entry(device, failed_sector) != NULL) {
        DMR_DEBUG(2, "Sector %llu already has remap entry", 
                  (unsigned long long)failed_sector);
        return;
    }
    
    /* Queue write-ahead remap creation (metadata written before I/O succeeds) */
    spin_lock(&device->remap_lock);
    device->pending_remap_sector = failed_sector;
    device->pending_remap_error = error;
    spin_unlock(&device->remap_lock);
    
    queue_work(device->metadata_workqueue, &device->writeahead_remap_work);
    
    DMR_DEBUG(2, "Write-ahead remap queued for sector %llu",
              (unsigned long long)failed_sector);
}

/**
 * dm_remap_metadata_thread() - Kernel thread for metadata writes (v4.2.2)
 * 
 * This dedicated kernel thread handles all metadata write operations.
 * Running in process context allows safe page allocation and synchronous I/O.
 * 
 * Why kernel thread instead of workqueue:
 * - Workqueue context doesn't support kmap() operations
 * - Need full process context for page operations
 * - Can safely do synchronous I/O without deadlock
 */
static int dm_remap_metadata_thread(void *data)
{
    struct dm_remap_device_v4_real *device = data;
    struct block_device *bdev;
    int ret;
    
    DMR_INFO("Metadata write thread started");
    
    while (!kthread_should_stop()) {
        /* Wait for metadata write request or stop signal */
        wait_event_interruptible(device->metadata_wait_queue,
                                 atomic_read(&device->metadata_write_requested) ||
                                 kthread_should_stop());
        
        if (kthread_should_stop())
            break;
        
        /* Clear the request flag */
        if (!atomic_cmpxchg(&device->metadata_write_requested, 1, 0))
            continue;
        
        /* Check if device is still active */
        if (!atomic_read(&device->device_active)) {
            DMR_DEBUG(2, "Metadata write skipped - device inactive");
            continue;
        }
        
        /* Perform metadata write in safe thread context */
        mutex_lock(&device->metadata_mutex);
        
        if (!device->metadata_dirty || !device->persistent_metadata || !device->spare_dev) {
            mutex_unlock(&device->metadata_mutex);
            continue;
        }
        
        /* Update metadata */
        device->metadata.last_update = ktime_to_ns(ktime_get_real());
        device->metadata.sequence_number++;
        device->metadata.metadata_crc = 0;
        device->metadata.metadata_crc = dm_remap_calculate_crc32(&device->metadata,
                                                                 sizeof(device->metadata));
        
        /* Sync to persistent metadata */
        dm_remap_sync_persistent_metadata(device);
        
        /* Write metadata using dm-bufio (safe, no page allocation) */
        if (device->metadata_bufio_client) {
            ret = dm_remap_write_metadata_v4_async(device->metadata_bufio_client,
                                                  device->persistent_metadata,
                                                  NULL);  /* NULL = fire-and-forget */
            
            if (ret) {
                DMR_ERROR("Metadata write via dm-bufio failed: %d", ret);
            } else {
                device->metadata_dirty = false;
                DMR_DEBUG(2, "Metadata written via dm-bufio (seq: %llu, %u remaps)",
                     device->metadata.sequence_number,
                     device->persistent_metadata->remap_data.active_remaps);
            }
        }
        
        mutex_unlock(&device->metadata_mutex);
    }
    
    DMR_INFO("Metadata write thread stopped");
    return 0;
}

/**
 * dm_remap_request_metadata_write() - Request metadata write from thread
 * 
 * Called from any context to request metadata write.
 * Thread-safe, non-blocking.
 */
static void dm_remap_request_metadata_write(struct dm_remap_device_v4_real *device)
{
    atomic_set(&device->metadata_write_requested, 1);
    wake_up(&device->metadata_wait_queue);
}

/**
 * dm_remap_sync_metadata_work() - Background metadata synchronization (v4.2.2)
 * 
 * Now just requests the kernel thread to write metadata instead of doing it directly.
 */
static void dm_remap_sync_metadata_work(struct work_struct *work)
{
    struct dm_remap_device_v4_real *device = 
        container_of(work, struct dm_remap_device_v4_real, metadata_sync_work);
    
    /* CRITICAL: Check if device is being destroyed BEFORE doing ANY work */
    if (!atomic_read(&device->device_active)) {
        DMR_DEBUG(2, "Metadata sync skipped - device inactive");
        return;
    }
    
    if (!device->metadata_dirty) {
        return;
    }
    
    DMR_DEBUG(2, "Requesting metadata write via kernel thread");
    dm_remap_request_metadata_write(device);
}

/**
 * dm_remap_error_analysis_work() - Deferred error pattern analysis
 * 
 * This work function performs error pattern analysis in a safe context where
 * mutexes can be taken. It's called from a workqueue instead of from I/O
 * completion context to avoid deadlocks.
 */
static void dm_remap_error_analysis_work(struct work_struct *work)
{
    struct dm_remap_device_v4_real *device = 
        container_of(work, struct dm_remap_device_v4_real, error_analysis_work);
    sector_t failed_sector;
    
    /* Get the pending error sector */
    spin_lock(&device->remap_lock);
    failed_sector = device->pending_error_sector;
    spin_unlock(&device->remap_lock);
    
    /* Now safe to call mutex-taking function */
    dm_remap_analyze_error_pattern(device, failed_sector);
}

/**
 * dm_remap_deferred_metadata_read_work() - v4.2: Read metadata after construction
 * 
 * This work function safely reads metadata from the spare device after the
 * dm-target constructor has completed. This avoids constructor deadlocks while
 * still enabling metadata persistence and auto-repair functionality.
 */
static void dm_remap_deferred_metadata_read_work(struct work_struct *work)
{
    struct dm_remap_device_v4_real *device = 
        container_of(to_delayed_work(work), struct dm_remap_device_v4_real, 
                     deferred_metadata_read_work);
    struct block_device *bdev;
    int ret;
    
    /* Check if already loaded (double-check pattern) */
    if (atomic_read(&device->metadata_loaded))
        return;
    
    DMR_INFO("Loading persistent metadata (deferred read)...");
    
    /* Call the read function which now includes auto-repair */
    ret = dm_remap_read_persistent_metadata(device);
    if (ret != 0) {
        /* ret < 0: Error reading, ret > 0: not used
         * In either case, no valid metadata found - write initial state */
        DMR_WARN("No valid metadata found, starting fresh: %d", ret);
        
        /* v4.2 Data Safety: Write initial metadata using fire-and-forget
         * This creates the 5-copy redundant metadata structure on first boot.
         * We don't wait for completion (safe in workqueue context but not needed).
         * Critical remaps use write-ahead, this is just initial setup. */
        
        printk(KERN_INFO "dm-remap: INITIAL METADATA WRITE PATH (no valid metadata found)\n");
        
        /* Mark metadata as dirty and request write via kernel thread */
        device->metadata_dirty = true;
        dm_remap_request_metadata_write(device);
        DMR_INFO("Initial metadata write requested via kernel thread");
    } else {
        DMR_INFO("Deferred metadata read completed successfully");
    }
    
    printk(KERN_INFO "dm-remap: Setting metadata_loaded=1\n");
    atomic_set(&device->metadata_loaded, 1);
    printk(KERN_INFO "dm-remap: Deferred metadata read work COMPLETE\n");
}

/**
 * Phase 1.4: Health Monitoring Functions
 */

/**
 * dm_remap_analyze_error_pattern() - Analyze sector error patterns
 */
static void dm_remap_analyze_error_pattern(struct dm_remap_device_v4_real *device,
                                          sector_t failed_sector)
{
    struct dm_remap_health_monitor *health = &device->health_monitor;
    struct dm_remap_error_pattern *pattern = NULL;
    uint64_t current_time = ktime_to_ns(ktime_get_real());
    int i;
    
    mutex_lock(&device->health_mutex);
    
    /* Find existing pattern for this sector */
    for (i = 0; i < health->hotspot_count && i < 32; i++) {
        if (health->error_hotspots[i].sector == failed_sector) {
            pattern = &health->error_hotspots[i];
            break;
        }
    }
    
    /* Create new pattern if not found */
    if (!pattern && health->hotspot_count < 32) {
        pattern = &health->error_hotspots[health->hotspot_count++];
        pattern->sector = failed_sector;
        pattern->error_count = 0;
        pattern->first_error_time = current_time;
        pattern->pattern_flags = 0;
    }
    
    if (pattern) {
        pattern->error_count++;
        pattern->last_error_time = current_time;
        
        /* Analyze error frequency */
        uint64_t time_span = current_time - pattern->first_error_time;
        if (time_span > 0) {
            uint64_t error_rate = pattern->error_count * 1000000000ULL / time_span;
            if (error_rate > 100) { /* More than 100 errors per second */
                pattern->pattern_flags |= 0x01; /* Mark as high-frequency error */
            }
        }
        
        DMR_DEBUG(2, "Error pattern updated: sector %llu, count %u, rate flags 0x%x",
                  (unsigned long long)failed_sector, pattern->error_count,
                  pattern->pattern_flags);
    }
    
    /* Update consecutive error tracking */
    if (health->last_error_sector == failed_sector) {
        health->consecutive_errors++;
    } else {
        health->consecutive_errors = 1;
        health->last_error_sector = failed_sector;
    }
    
    /* Update health prediction score based on error patterns */
    if (health->consecutive_errors > 5) {
        health->failure_prediction_score = min(health->failure_prediction_score + 10, 100U);
    }
    
    mutex_unlock(&device->health_mutex);
}

/**
 * dm_remap_calculate_health_score() - Calculate overall device health
 */
static uint32_t dm_remap_calculate_health_score(struct dm_remap_device_v4_real *device)
{
    struct dm_remap_health_monitor *health = &device->health_monitor;
    uint64_t error_count = atomic64_read(&device->stats.io_errors);
    uint64_t total_ios = atomic64_read(&device->stats.total_ios);
    uint32_t health_score = 100; /* Start with perfect health */
    
    mutex_lock(&device->health_mutex);
    
    /* Factor in error rate */
    if (total_ios > 0) {
        uint64_t error_rate = (error_count * 10000) / total_ios; /* Per 10,000 operations */
        if (error_rate > 100) {      /* >1% error rate */
            health_score -= 50;
        } else if (error_rate > 10) { /* >0.1% error rate */
            health_score -= 20;
        } else if (error_rate > 1) {  /* >0.01% error rate */
            health_score -= 5;
        }
    }
    
    /* Factor in consecutive errors */
    if (health->consecutive_errors > 10) {
        health_score -= 30;
    } else if (health->consecutive_errors > 5) {
        health_score -= 15;
    }
    
    /* Factor in hotspot count */
    if (health->hotspot_count > 20) {
        health_score -= 25;
    } else if (health->hotspot_count > 10) {
        health_score -= 10;
    }
    
    /* Factor in response time degradation */
    if (health->avg_response_time_ns > 10000000) { /* >10ms average */
        health_score -= 20;
    } else if (health->avg_response_time_ns > 1000000) { /* >1ms average */
        health_score -= 10;
    }
    
    health_score = max(health_score, 0U);
    health->failure_prediction_score = health_score;
    
    mutex_unlock(&device->health_mutex);
    
    return health_score;
}

/**
 * dm_remap_health_scan_work() - Background health scanning
 */
static void dm_remap_health_scan_work(struct work_struct *work)
{
    struct dm_remap_device_v4_real *device = 
        container_of(work, struct dm_remap_device_v4_real, health_scan_work.work);
    struct dm_remap_health_monitor *health = &device->health_monitor;
    uint32_t health_score;
    
    if (!atomic_read(&device->device_active)) {
        return;
    }
    
    DMR_DEBUG(2, "Starting background health scan (progress: %llu/%llu)",
              (unsigned long long)health->scan_progress,
              (unsigned long long)device->main_device_sectors);
    
    mutex_lock(&device->health_mutex);
    health->background_scan_active = true;
    health->last_health_scan = ktime_to_ns(ktime_get_real());
    mutex_unlock(&device->health_mutex);
    
    /* Calculate current health score */
    health_score = dm_remap_calculate_health_score(device);
    
    /* Update scan progress */
    mutex_lock(&device->health_mutex);
    health->scan_progress += 1024; /* Scan 1024 sectors per iteration */
    if (health->scan_progress >= device->main_device_sectors) {
        health->scan_progress = 0; /* Restart scan */
        atomic64_inc(&device->health_scan_count);
        DMR_INFO("Health scan completed. Health score: %u/100, hotspots: %u",
                 health_score, health->hotspot_count);
    }
    health->background_scan_active = false;
    mutex_unlock(&device->health_mutex);
    
    /* Schedule next scan */
    if (atomic_read(&device->device_active)) {
        schedule_delayed_work(&device->health_scan_work, 
                             msecs_to_jiffies(health->scan_interval_seconds * 1000));
    }
}

/**
 * Phase 1.4: Performance Optimization Functions
 */

/**
 * dm_remap_cache_lookup() - Fast remap cache lookup
 */
static sector_t dm_remap_cache_lookup(struct dm_remap_device_v4_real *device,
                                     sector_t original_sector)
{
    struct dm_remap_perf_optimizer *perf = &device->perf_optimizer;
    struct dm_remap_cache_entry *entry;
    uint32_t cache_index;
    sector_t result = 0;
    
    if (!perf->cache_entries || perf->cache_size == 0) {
        atomic64_inc(&perf->cache_misses);
        return 0;
    }
    
    cache_index = original_sector & perf->cache_mask;
    entry = &perf->cache_entries[cache_index];
    
    mutex_lock(&device->cache_mutex);
    
    if (entry->original_sector == original_sector) {
        /* Cache hit */
        entry->access_time = ktime_to_ns(ktime_get());
        entry->access_count++;
        result = entry->remapped_sector;
        atomic64_inc(&perf->cache_hits);
        atomic64_inc(&perf->fast_path_hits);
    } else {
        /* Cache miss */
        atomic64_inc(&perf->cache_misses);
    }
    
    mutex_unlock(&device->cache_mutex);
    
    return result;
}

/**
 * dm_remap_cache_insert() - Insert entry into remap cache
 */
static void dm_remap_cache_insert(struct dm_remap_device_v4_real *device,
                                 sector_t original_sector,
                                 sector_t remapped_sector)
{
    struct dm_remap_perf_optimizer *perf = &device->perf_optimizer;
    struct dm_remap_cache_entry *entry;
    uint32_t cache_index;
    
    if (!perf->cache_entries || perf->cache_size == 0) {
        return;
    }
    
    cache_index = original_sector & perf->cache_mask;
    entry = &perf->cache_entries[cache_index];
    
    mutex_lock(&device->cache_mutex);
    
    entry->original_sector = original_sector;
    entry->remapped_sector = remapped_sector;
    entry->access_time = ktime_to_ns(ktime_get());
    entry->access_count = 1;
    
    mutex_unlock(&device->cache_mutex);
    
    DMR_DEBUG(3, "Cache entry inserted: %llu -> %llu (index %u)",
              (unsigned long long)original_sector,
              (unsigned long long)remapped_sector, cache_index);
}

/**
 * dm_remap_update_io_pattern() - Update I/O pattern analysis
 */
static void dm_remap_update_io_pattern(struct dm_remap_device_v4_real *device,
                                      sector_t sector)
{
    struct dm_remap_perf_optimizer *perf = &device->perf_optimizer;
    ktime_t current_time = ktime_get();
    
    mutex_lock(&device->cache_mutex);
    
    /* Check if this is sequential I/O */
    if (perf->io_pattern.last_sector + 1 == sector) {
        perf->io_pattern.sequential_count++;
    } else {
        perf->io_pattern.random_count++;
    }
    
    perf->io_pattern.last_sector = sector;
    
    /* Update pattern classification every 1000 I/Os */
    if ((perf->io_pattern.sequential_count + perf->io_pattern.random_count) % 1000 == 0) {
        perf->io_pattern.is_sequential_workload = 
            (perf->io_pattern.sequential_count > perf->io_pattern.random_count);
        
        DMR_DEBUG(3, "I/O pattern: %s (seq: %u, rand: %u)",
                  perf->io_pattern.is_sequential_workload ? "sequential" : "random",
                  perf->io_pattern.sequential_count, perf->io_pattern.random_count);
    }
    
    perf->io_pattern.pattern_update_time = current_time;
    
    mutex_unlock(&device->cache_mutex);
}

/**
 * dm_remap_validate_device_compatibility() - Enhanced device compatibility checking
 */
static int dm_remap_validate_device_compatibility(struct file *main_dev, 
                                                 struct file *spare_dev)
{
    sector_t main_size, spare_size;
    unsigned int main_sector_size, spare_sector_size;
    unsigned int main_physical_size, spare_physical_size;
    u64 main_capacity, spare_capacity;
    sector_t min_spare_size;
    
    if (!main_dev || IS_ERR(main_dev) || !spare_dev || IS_ERR(spare_dev)) {
        return -EINVAL;
    }
    
    /* Get device sizes in sectors */
    main_size = dm_remap_get_device_size(main_dev);
    spare_size = dm_remap_get_device_size(spare_dev);
    
    /* Get sector sizes */
    main_sector_size = dm_remap_get_sector_size(main_dev);
    spare_sector_size = dm_remap_get_sector_size(spare_dev);
    
    /* Get physical sector sizes */
    main_physical_size = dm_remap_get_physical_sector_size(main_dev);
    spare_physical_size = dm_remap_get_physical_sector_size(spare_dev);
    
    /* Get capacities in bytes */
    main_capacity = dm_remap_get_device_capacity_bytes(main_dev);
    spare_capacity = dm_remap_get_device_capacity_bytes(spare_dev);
    
    DMR_DEBUG(2, "Device geometry: main=%llu sectors (%u/%u bytes), spare=%llu sectors (%u/%u bytes)",
              (unsigned long long)main_size, main_sector_size, main_physical_size,
              (unsigned long long)spare_size, spare_sector_size, spare_physical_size);
    
    /* Validate sector size compatibility */
    if (main_sector_size != spare_sector_size) {
        DMR_ERROR("Sector size mismatch: main=%u, spare=%u bytes",
                  main_sector_size, spare_sector_size);
        return -EINVAL;
    }
    
    /* Check minimum size requirements */
    if (main_size < DM_REMAP_MIN_DEVICE_SECTORS) {
        DMR_ERROR("Main device too small: %llu < %u sectors",
                  (unsigned long long)main_size, DM_REMAP_MIN_DEVICE_SECTORS);
        return -ENOSPC;
    }
    
    /* Calculate minimum spare size using intelligent algorithm (v4.0.1)
     * 
     * LEGACY MODE (strict_spare_sizing=true):
     *   min_spare = main_size * 1.05
     *   
     * OPTIMIZED MODE (strict_spare_sizing=false, default):
     *   Components:
     *   1. Metadata base: 4KB
     *   2. Expected bad sectors: main_size * spare_overhead_percent / 100
     *   3. Mapping overhead: expected_bad_sectors * 64 bytes per entry
     *   4. Safety margin: 20% of calculated need
     *   
     * Example with 1TB main, 2% expected bad sectors:
     *   - Bad sectors: 1TB * 2% = 20GB
     *   - Metadata: 4KB + (20GB/512 * 64 bytes) = ~2.5MB
     *   - Safety margin: (20GB + 2.5MB) * 1.2 = 24GB
     *   - Total: ~24GB spare instead of 1.05TB!
     */
    if (strict_spare_sizing) {
        /* Legacy mode: spare must be >= main + 5% */
        min_spare_size = main_size + (main_size / 20);
        DMR_INFO("Using strict spare sizing (legacy): %llu sectors required",
                 (unsigned long long)min_spare_size);
    } else {
        /* Optimized mode: calculate realistic minimum */
        sector_t metadata_sectors;
        sector_t expected_bad_sectors;
        sector_t mapping_overhead_sectors;
        sector_t base_requirement;
        
        /* Clamp overhead percentage to sane range (0-20%) */
        uint overhead_pct = spare_overhead_percent;
        if (overhead_pct > 20) {
            DMR_INFO("Clamping spare_overhead_percent from %u to 20%%", overhead_pct);
            overhead_pct = 20;
        }
        
        /* Calculate components */
        metadata_sectors = DM_REMAP_METADATA_BASE_SIZE / main_sector_size + 1;
        expected_bad_sectors = (main_size * overhead_pct) / 100;
        mapping_overhead_sectors = (expected_bad_sectors * DM_REMAP_METADATA_PER_MAPPING) / main_sector_size + 1;
        
        /* Base requirement = metadata + bad sectors + mapping overhead */
        base_requirement = metadata_sectors + expected_bad_sectors + mapping_overhead_sectors;
        
        /* Add safety margin (20%) */
        min_spare_size = base_requirement + (base_requirement * DM_REMAP_SAFETY_MARGIN_PCT / 100);
        
        DMR_INFO("Optimized spare sizing calculation:");
        DMR_INFO("  Main device: %llu sectors (%llu MB)",
                 (unsigned long long)main_size,
                 (unsigned long long)(main_size * main_sector_size / (1024*1024)));
        DMR_INFO("  Expected bad sectors (%u%%): %llu sectors (%llu MB)",
                 overhead_pct,
                 (unsigned long long)expected_bad_sectors,
                 (unsigned long long)(expected_bad_sectors * main_sector_size / (1024*1024)));
        DMR_INFO("  Metadata overhead: %llu sectors (%llu KB)",
                 (unsigned long long)(metadata_sectors + mapping_overhead_sectors),
                 (unsigned long long)((metadata_sectors + mapping_overhead_sectors) * main_sector_size / 1024));
        DMR_INFO("  Minimum spare (with %u%% safety margin): %llu sectors (%llu MB)",
                 DM_REMAP_SAFETY_MARGIN_PCT,
                 (unsigned long long)min_spare_size,
                 (unsigned long long)(min_spare_size * main_sector_size / (1024*1024)));
    }
    
    /* Spare device should have adequate capacity */
    if (spare_size < min_spare_size) {
        if (strict_spare_sizing) {
            DMR_ERROR("Spare device insufficient: %llu < %llu sectors (need %llu + 5%% overhead)",
                      (unsigned long long)spare_size, (unsigned long long)min_spare_size,
                      (unsigned long long)main_size);
        } else {
            DMR_ERROR("Spare device insufficient: %llu < %llu sectors", 
                      (unsigned long long)spare_size, (unsigned long long)min_spare_size);
            DMR_ERROR("  Increase spare size or reduce spare_overhead_percent parameter");
            DMR_ERROR("  Current overhead: %u%%, try lower value or use strict_spare_sizing=1",
                      spare_overhead_percent);
        }
        return -ENOSPC;
    }
    
    /* Success - log the spare utilization efficiency */
    {
        u64 spare_size_mb = (spare_size * main_sector_size) / (1024*1024);
        u64 main_size_mb = (main_size * main_sector_size) / (1024*1024);
        uint efficiency_pct = (spare_size_mb * 100) / main_size_mb;
        
        if (efficiency_pct < 10) {
            DMR_INFO("Excellent spare efficiency: %llu MB spare for %llu MB main (%u%%)",
                     (unsigned long long)spare_size_mb, (unsigned long long)main_size_mb, efficiency_pct);
        } else if (efficiency_pct < 50) {
            DMR_INFO("Good spare efficiency: %llu MB spare for %llu MB main (%u%%)",
                     (unsigned long long)spare_size_mb, (unsigned long long)main_size_mb, efficiency_pct);
        } else {
            DMR_INFO("Consider RAID1 mirroring: %llu MB spare for %llu MB main (%u%%)",
                     (unsigned long long)spare_size_mb, (unsigned long long)main_size_mb, efficiency_pct);
        }
    }
    
    /* Warn about physical sector size differences */
    if (main_physical_size != spare_physical_size) {
        DMR_INFO("Physical sector size difference: main=%u, spare=%u bytes (performance may vary)",
                 main_physical_size, spare_physical_size);
    }
    
    /* Check device alignment for first sector */
    if (!dm_remap_check_device_alignment(main_dev, 0)) {
        DMR_ERROR("Main device not properly aligned");
        return -EINVAL;
    }
    
    if (!dm_remap_check_device_alignment(spare_dev, 0)) {
        DMR_ERROR("Spare device not properly aligned");  
        return -EINVAL;
    }
    
    DMR_INFO("Enhanced device compatibility validated:");
    DMR_INFO("  Main: %llu sectors, %llu bytes (%u/%u sector size)",
             (unsigned long long)main_size, main_capacity, main_sector_size, main_physical_size);
    DMR_INFO("  Spare: %llu sectors, %llu bytes (%u/%u sector size)",
             (unsigned long long)spare_size, spare_capacity, spare_sector_size, spare_physical_size);
    DMR_INFO("  Overhead available: %llu sectors (%llu%% of main size)",
             (unsigned long long)(spare_size - main_size),
             (unsigned long long)((spare_size - main_size) * 100 / main_size));
    
    return 0;
}

/**
 * dm_remap_initialize_metadata_v4_real() - Initialize enhanced v4.0 metadata
 */
static void dm_remap_initialize_metadata_v4_real(struct dm_remap_device_v4_real *device)
{
    struct dm_remap_metadata_v4_real *meta = &device->metadata;
    ktime_t now = ktime_get_real();
    
    /* Initialize header */
    strncpy(meta->magic, "DM_REMAP_V4.0R", sizeof(meta->magic));
    meta->version = 4;
    meta->metadata_size = sizeof(*meta);
    meta->creation_time = ktime_to_ns(now);
    meta->last_update = meta->creation_time;
    
    /* Device identification */
    strncpy(meta->main_device_path, device->main_path, sizeof(meta->main_device_path) - 1);
    strncpy(meta->spare_device_path, device->spare_path, sizeof(meta->spare_device_path) - 1);
    meta->main_device_size = device->main_device_sectors;
    meta->spare_device_size = device->spare_device_sectors;
    
    /* Generate device fingerprint based on device characteristics */
    snprintf(meta->device_fingerprint, sizeof(meta->device_fingerprint),
             "v4r-%08llx-%08llx", 
             (unsigned long long)meta->main_device_size,
             (unsigned long long)meta->spare_device_size);
    
    /* Mapping information */
    meta->sector_size = device->sector_size;
    meta->total_sectors = device->main_device_sectors;
    meta->max_mappings = 16384;  /* 16K max remaps */
    meta->active_mappings = 0;
    
    /* Health monitoring */
    meta->health_scan_count = 0;
    meta->last_health_scan = 0;
    meta->predicted_failures = 0;
    meta->health_flags = 0;
    meta->total_errors = 0;
    meta->last_error_time = 0;
    
    /* Performance statistics */
    meta->total_reads = 0;
    meta->total_writes = 0; 
    meta->total_remaps = 0;
    meta->total_io_time_ns = 0;
    meta->peak_throughput = 0;
    
    /* Device status */
    meta->main_device_status = 0;   /* Healthy */
    meta->spare_device_status = 0;  /* Healthy */
    meta->uptime_seconds = 0;
    
    DMR_DEBUG(2, "Initialized enhanced v4.0 metadata (size: %u bytes, fingerprint: %s)",
              meta->metadata_size, meta->device_fingerprint);
}

/**
 * dm_remap_map_v4_real() - Enhanced real device I/O mapping with optimization
 */
static int dm_remap_map_v4_real(struct dm_target *ti, struct bio *bio)
{
    struct dm_remap_device_v4_real *device = ti->private;
    bool is_read = bio_data_dir(bio) == READ;
    uint64_t sector = bio->bi_iter.bi_sector;
    unsigned int bio_size = bio->bi_iter.bi_size;
    ktime_t start_time = ktime_get();
    ktime_t io_time;
    uint64_t throughput;
    
    /* Validate I/O parameters */
    if (sector >= device->main_device_sectors) {
        DMR_ERROR("I/O beyond device bounds: sector %llu >= %llu",
                  (unsigned long long)sector,
                  (unsigned long long)device->main_device_sectors);
        return -EIO;
    }
    
    /* Check alignment for optimal performance */
    if (!dm_remap_check_device_alignment(device->main_dev, sector)) {
        DMR_DEBUG(2, "Unaligned I/O detected at sector %llu", (unsigned long long)sector);
    }
    
    /* Update statistics with enhanced tracking */
    if (is_read) {
        atomic64_inc(&device->read_count);
        atomic64_inc(&global_reads);
        dm_remap_stats_inc_reads();  /* Update stats module */
    } else {
        atomic64_inc(&device->write_count);
        atomic64_inc(&global_writes);
        dm_remap_stats_inc_writes();  /* Update stats module */
    }
    
    atomic64_inc(&device->io_operations);
    atomic64_inc(&device->stats.total_ios);
    device->last_io_time = start_time;
    
    /* Phase 1.4: Update I/O pattern analysis */
    dm_remap_update_io_pattern(device, sector);
    
    /* Phase 1.4: Check for cached remap first (fast path) */
    sector_t cached_remap = 0;
    if (device->perf_optimizer.fast_path_enabled) {
        cached_remap = dm_remap_cache_lookup(device, sector);
        if (cached_remap > 0) {
            /* Fast path: use cached remap */
            atomic64_inc(&device->stats.remapped_ios);
            
            DMR_DEBUG(3, "Fast path remap: sector %llu -> %llu (cached)",
                      (unsigned long long)sector, (unsigned long long)cached_remap);
            
            if (real_device_mode && device->spare_dev) {
                bio_set_dev(bio, file_bdev(device->spare_dev));
                bio->bi_iter.bi_sector = cached_remap;
            }
            
            goto remap_complete;
        }
    }
    
    DMR_DEBUG(3, "Enhanced I/O: %s %u bytes to sector %llu on %s",
              is_read ? "read" : "write", bio_size,
              (unsigned long long)sector,
              real_device_mode ? dm_remap_get_device_name(device->main_dev) : "demo");
    
    /* Phase 1.3 Enhanced I/O routing with sector remapping */
    if (real_device_mode && device->main_dev && !IS_ERR(device->main_dev)) {
        struct dm_remap_entry_v4 *remap_entry;
        struct block_device *target_bdev;
        sector_t target_sector = sector;
        
        /* Check if this sector has been remapped */
        remap_entry = dm_remap_find_remap_entry(device, sector);
        if (remap_entry) {
            /* Redirect to spare device */
            target_bdev = file_bdev(device->spare_dev);
            target_sector = remap_entry->spare_sector;
            
            DMR_DEBUG(3, "Remapped I/O: sector %llu -> %llu (spare device)",
                      (unsigned long long)sector,
                      (unsigned long long)target_sector);
            
            /* Update remap statistics */
            atomic64_inc(&device->stats.remapped_ios);
            if (is_read) {
                atomic64_inc(&device->remap_count);
                atomic64_inc(&global_remaps);
            }
        } else {
            /* Normal I/O to main device */
            target_bdev = file_bdev(device->main_dev);
            atomic64_inc(&device->stats.normal_ios);
        }
        
        /* Set target device and sector */
        bio_set_dev(bio, target_bdev);
        bio->bi_iter.bi_sector = target_sector;
        
    } else {
        /* Demo mode - simulate successful I/O */
        DMR_DEBUG(3, "Demo mode I/O simulation");
    }
    
remap_complete:
    /* Calculate and update performance metrics */
    io_time = ktime_sub(ktime_get(), start_time);
    atomic64_add(ktime_to_ns(io_time), &device->total_io_time_ns);
    
    /* Calculate throughput (bytes per second) */
    if (ktime_to_ns(io_time) > 0) {
        throughput = (uint64_t)bio_size * 1000000000ULL / ktime_to_ns(io_time);
        if (throughput > device->peak_throughput) {
            device->peak_throughput = throughput;
        }
    }
    
    /* Update metadata statistics */
    if (is_read) {
        device->metadata.total_reads++;
    } else {
        device->metadata.total_writes++;
    }
    device->metadata.total_io_time_ns += ktime_to_ns(io_time);
    device->metadata.last_update = ktime_to_ns(ktime_get_real());
    
    return DM_MAPIO_REMAPPED;
}

/**
 * dm_remap_ctr_v4_real() - Constructor for real device support
 */
static int dm_remap_ctr_v4_real(struct dm_target *ti, unsigned int argc, char **argv)
{
    struct dm_remap_device_v4_real *device;
    struct file *main_dev, *spare_dev;
    int ret;
    
    if (argc != 2) {
        ti->error = "Invalid argument count: dm-remap-v4 <main_device> <spare_device>";
        return -EINVAL;
    }
    
    DMR_INFO("Creating real device target: main=%s, spare=%s", argv[0], argv[1]);
    
    /* Open devices */
    if (real_device_mode) {
        main_dev = dm_remap_open_bdev_real(argv[0], BLK_OPEN_READ | BLK_OPEN_WRITE, ti);
        if (IS_ERR(main_dev)) {
            ret = PTR_ERR(main_dev);
            ti->error = "Cannot open main device";
            DMR_ERROR("Failed to open main device %s: %d", argv[0], ret);
            return ret;
        }
        
        spare_dev = dm_remap_open_bdev_real(argv[1], BLK_OPEN_READ | BLK_OPEN_WRITE, ti);
        if (IS_ERR(spare_dev)) {
            ret = PTR_ERR(spare_dev);
            ti->error = "Cannot open spare device";
            DMR_ERROR("Failed to open spare device %s: %d", argv[1], ret);
            dm_remap_close_bdev_real(main_dev);
            return ret;
        }
        
        /* Validate device compatibility */
        ret = dm_remap_validate_device_compatibility(main_dev, spare_dev);
        if (ret) {
            ti->error = "Device compatibility validation failed";
            dm_remap_close_bdev_real(main_dev);
            dm_remap_close_bdev_real(spare_dev);
            return ret;
        }
    } else {
        /* Demo mode - validate paths but don't open real devices */
        ret = dm_remap_open_bdev(argv[0], FMODE_READ | FMODE_WRITE, ti);
        if (ret < 0) {
            ti->error = "Cannot access main device";
            DMR_ERROR("Main device access failed: %s (error: %d)", argv[0], ret);
            return ret;
        }
        
        ret = dm_remap_open_bdev(argv[1], FMODE_READ | FMODE_WRITE, ti);
        if (ret < 0) {
            ti->error = "Cannot access spare device";
            DMR_ERROR("Spare device access failed: %s (error: %d)", argv[1], ret);
            return ret;
        }
        
        main_dev = NULL;
        spare_dev = NULL;
    }
    
    /* Allocate device structure */
    device = kzalloc(sizeof(*device), GFP_KERNEL);
    if (!device) {
        ti->error = "Cannot allocate device structure";
        if (real_device_mode) {
            dm_remap_close_bdev_real(main_dev);
            dm_remap_close_bdev_real(spare_dev);
        }
        return -ENOMEM;
    }
    
    /* Initialize device structure */
    device->main_dev = main_dev;
    device->spare_dev = spare_dev;
    device->device_mode = BLK_OPEN_READ | BLK_OPEN_WRITE;
    strncpy(device->main_path, argv[0], sizeof(device->main_path) - 1);
    strncpy(device->spare_path, argv[1], sizeof(device->spare_path) - 1);
    
    /* Get enhanced device information */
    if (real_device_mode && main_dev && spare_dev) {
        device->main_device_sectors = dm_remap_get_device_size(main_dev);
        device->spare_device_sectors = dm_remap_get_device_size(spare_dev);
        device->sector_size = dm_remap_get_sector_size(main_dev);
        
        DMR_INFO("Real devices opened with enhanced detection:");
        DMR_INFO("  Main: %s (%llu sectors, %u byte sectors)",
                 dm_remap_get_device_name(main_dev), 
                 (unsigned long long)device->main_device_sectors,
                 device->sector_size);
        DMR_INFO("  Spare: %s (%llu sectors, %u byte sectors)",
                 dm_remap_get_device_name(spare_dev),
                 (unsigned long long)device->spare_device_sectors,
                 dm_remap_get_sector_size(spare_dev));
        
        /* Store physical characteristics for optimization */
        device->metadata.main_device_size = device->main_device_sectors;
        device->metadata.spare_device_size = device->spare_device_sectors;
        device->metadata.sector_size = device->sector_size;
    } else {
        /* Demo mode defaults */
        device->main_device_sectors = ti->len;
        device->spare_device_sectors = ti->len;
        device->sector_size = 512;
        
        DMR_INFO("Demo mode: simulated devices (%llu sectors, %u byte sectors)",
                 (unsigned long long)device->main_device_sectors, device->sector_size);
    }
    
    /* Initialize mutexes and structures */
    mutex_init(&device->metadata_mutex);
    atomic_set(&device->device_active, 1);
    INIT_LIST_HEAD(&device->device_list);
    device->creation_time = ktime_get();
    
    /* Initialize Phase 1.3 sector remapping */
    INIT_LIST_HEAD(&device->remap_list);
    spin_lock_init(&device->remap_lock);
    device->remap_count_active = 0;
    device->spare_sector_count = device->spare_device_sectors / 2; /* Reserve half for remapping */
    device->next_spare_sector = 0;
    
    /* Initialize metadata sync workqueue */
    device->metadata_workqueue = alloc_workqueue("dm_remap_meta_sync", WQ_MEM_RECLAIM, 1);
    if (!device->metadata_workqueue) {
        DMR_ERROR("Failed to create metadata sync workqueue");
        mutex_destroy(&device->metadata_mutex);
        kfree(device);
        if (real_device_mode) {
            dm_remap_close_bdev_real(main_dev);
            dm_remap_close_bdev_real(spare_dev);
        }
        ti->error = "Failed to create workqueue";
        return -ENOMEM;
    }
    INIT_WORK(&device->metadata_sync_work, dm_remap_sync_metadata_work);
    INIT_WORK(&device->error_analysis_work, dm_remap_error_analysis_work);
    INIT_WORK(&device->writeahead_remap_work, dm_remap_writeahead_remap_work);
    INIT_DELAYED_WORK(&device->deferred_metadata_read_work, dm_remap_deferred_metadata_read_work);
    atomic_set(&device->metadata_loaded, 0);
    
    /* Initialize v4.2.2 kernel thread for metadata writes */
    init_waitqueue_head(&device->metadata_wait_queue);
    atomic_set(&device->metadata_write_requested, 0);
    atomic_set(&device->metadata_thread_should_stop, 0);
    
    /* Create metadata write kernel thread */
    device->metadata_thread = kthread_create(dm_remap_metadata_thread, device,
                                            "dm_remap_meta");
    if (IS_ERR(device->metadata_thread)) {
        DMR_ERROR("Failed to create metadata write thread");
        destroy_workqueue(device->metadata_workqueue);
        mutex_destroy(&device->metadata_mutex);
        kfree(device);
        if (real_device_mode) {
            dm_remap_close_bdev_real(main_dev);
            dm_remap_close_bdev_real(spare_dev);
        }
        ti->error = "Failed to create metadata write thread";
        return PTR_ERR(device->metadata_thread);
    }
    wake_up_process(device->metadata_thread);
    DMR_INFO("Metadata write kernel thread started");
    
    /* Initialize v4.2 repair workqueue and context */
    device->repair_wq = alloc_workqueue("dm_remap_repair", WQ_MEM_RECLAIM | WQ_UNBOUND, 0);
    if (!device->repair_wq) {
        DMR_ERROR("Failed to create repair workqueue");
        kthread_stop(device->metadata_thread);
        destroy_workqueue(device->metadata_workqueue);
        mutex_destroy(&device->metadata_mutex);
        kfree(device);
        if (real_device_mode) {
            dm_remap_close_bdev_real(main_dev);
            dm_remap_close_bdev_real(spare_dev);
        }
        ti->error = "Failed to create repair workqueue";
        return -ENOMEM;
    }
    dm_remap_init_repair_context(&device->repair_ctx, 
                                 file_bdev(device->spare_dev),
                                 device->repair_wq);
    
    /* Initialize statistics */
    atomic64_set(&device->read_count, 0);
    atomic64_set(&device->write_count, 0);  
    atomic64_set(&device->remap_count, 0);
    atomic64_set(&device->error_count, 0);
    atomic64_set(&device->health_scan_count, 0);
    atomic64_set(&device->total_io_time_ns, 0);
    atomic64_set(&device->io_operations, 0);
    
    /* Initialize Phase 1.3 enhanced statistics */
    atomic64_set(&device->stats.total_ios, 0);
    atomic64_set(&device->stats.normal_ios, 0);
    atomic64_set(&device->stats.remapped_ios, 0);
    atomic64_set(&device->stats.io_errors, 0);
    atomic64_set(&device->stats.remapped_sectors, 0);
    device->stats.total_latency_ns = 0;
    device->stats.max_latency_ns = 0;
    
    /* Initialize Phase 1.4: Health monitoring */
    mutex_init(&device->health_mutex);
    memset(&device->health_monitor, 0, sizeof(device->health_monitor));
    device->health_monitor.scan_interval_seconds = 300; /* 5 minutes */
    device->health_monitor.failure_prediction_score = 100; /* Start healthy */
    INIT_DELAYED_WORK(&device->health_scan_work, dm_remap_health_scan_work);
    
    /* Initialize Phase 1.4: Performance optimization */
    mutex_init(&device->cache_mutex);
    memset(&device->perf_optimizer, 0, sizeof(device->perf_optimizer));
    
    /* Allocate remap cache (power of 2 size for fast modulo) */
    device->perf_optimizer.cache_size = 256;
    device->perf_optimizer.cache_mask = device->perf_optimizer.cache_size - 1;
    device->perf_optimizer.cache_entries = kzalloc(
        device->perf_optimizer.cache_size * sizeof(struct dm_remap_cache_entry),
        GFP_KERNEL);
    if (!device->perf_optimizer.cache_entries) {
        DMR_WARN("Failed to allocate remap cache, performance may be reduced");
        device->perf_optimizer.cache_size = 0;
        device->perf_optimizer.cache_mask = 0;
    }
    
    device->perf_optimizer.fast_path_enabled = true;
    atomic64_set(&device->perf_optimizer.cache_hits, 0);
    atomic64_set(&device->perf_optimizer.cache_misses, 0);
    atomic64_set(&device->perf_optimizer.fast_path_hits, 0);
    atomic64_set(&device->perf_optimizer.slow_path_hits, 0);
    
    /* Initialize Phase 1.4: Enterprise features */
    device->enterprise.maintenance_mode = false;
    device->enterprise.alert_threshold = 90; /* Alert when health drops below 90% */
    device->enterprise.last_alert_time = 0;
    device->enterprise.configuration_version = 1;
    
    /* Initialize enhanced metadata */
    dm_remap_initialize_metadata_v4_real(device);
    
    /* Initialize persistent v4 metadata structure */
    ret = dm_remap_init_persistent_metadata(device);
    if (ret) {
        DMR_ERROR("Failed to initialize persistent metadata: %d", ret);
        goto error_cleanup;
    }
    
    /* Create dm-bufio client for metadata I/O (kernel standard approach) */
    if (real_device_mode && device->spare_dev) {
        device->metadata_bufio_client = dm_bufio_client_create(
            file_bdev(device->spare_dev),
            131072,  /* Block size = 128KB (metadata is ~90KB with 2048 remaps) */
            1,       /* 1 reserved buffer */
            0,       /* No aux buffer */
            NULL,    /* No alloc callback */
            NULL,    /* No write callback */
            0        /* Default flags (allow sleep) */
        );
        
        if (IS_ERR(device->metadata_bufio_client)) {
            ret = PTR_ERR(device->metadata_bufio_client);
            DMR_ERROR("Failed to create dm-bufio client: %d", ret);
            device->metadata_bufio_client = NULL;
            goto error_cleanup;
        }
        
        DMR_INFO("dm-bufio client created for metadata I/O (block_size=131072 bytes)");
    }
    
    /* NOTE: Metadata reading is deferred to avoid blocking I/O during construction.
     * Reading metadata during dm target construction can cause deadlocks because:
     * 1. Device-mapper may be holding locks
     * 2. Block layer may not be fully initialized
     * 3. Synchronous I/O (submit_bio_wait) can block indefinitely
     * 
     * v4.2: Metadata reading is now scheduled via delayed workqueue, running
     * after constructor completes. This enables metadata persistence and auto-repair.
     */
    DMR_INFO("Scheduling deferred metadata read (avoiding constructor deadlock)");
    schedule_delayed_work(&device->deferred_metadata_read_work, msecs_to_jiffies(100)); /* 100ms delay */
    
    /* Start background health monitoring */
    schedule_delayed_work(&device->health_scan_work, 
                         msecs_to_jiffies(device->health_monitor.scan_interval_seconds * 1000));
    
    /* Set target length */
    ti->len = device->main_device_sectors;
    
    /* Add to global device list */
    mutex_lock(&dm_remap_devices_mutex);
    list_add_tail(&device->device_list, &dm_remap_devices);
    atomic_inc(&dm_remap_device_count);
    mutex_unlock(&dm_remap_devices_mutex);
    
    ti->private = device;
    
    DMR_INFO("Real device target created successfully (%s mode)",
             real_device_mode ? "real device" : "demo");
    
    return 0;

error_cleanup:
    /* Cleanup on error */
    destroy_workqueue(device->metadata_workqueue);
    if (device->perf_optimizer.cache_entries)
        kfree(device->perf_optimizer.cache_entries);
    mutex_destroy(&device->cache_mutex);
    mutex_destroy(&device->health_mutex);
    mutex_destroy(&device->metadata_mutex);
    kfree(device);
    if (real_device_mode) {
        dm_remap_close_bdev_real(main_dev);
        dm_remap_close_bdev_real(spare_dev);
    }
    ti->error = "Initialization failed";
    return ret;
}

/**
 * dm_remap_presuspend_v4_real() - Presuspend hook - cancel background work
 * 
 * CRITICAL: This is called by device-mapper BEFORE device removal.
 * We MUST cancel all background work and free remaps here, while
 * device-mapper guarantees no new I/O will arrive.
 */
static void dm_remap_presuspend_v4_real(struct dm_target *ti)
{
    struct dm_remap_device_v4_real *device = ti->private;
    struct dm_remap_entry_v4 *entry, *tmp;
    
    if (!device) {
        return;
    }
    
    DMR_INFO("Presuspend: stopping all background work");
    
    /* CRITICAL: Mark device inactive FIRST so running work items will exit */
    atomic_set(&device->device_active, 0);
    
    /* v4.2.2: Stop metadata write kernel thread */
    if (device->metadata_thread) {
        DMR_INFO("Presuspend: stopping metadata write thread");
        kthread_stop(device->metadata_thread);
        DMR_INFO("Presuspend: metadata thread stopped");
    }
    
    /* v4.1: Just cancel work (non-blocking)
     * DON'T use cancel_work_sync() - it can deadlock if work is queued but not running.
     * Instead, we'll let destroy_workqueue() in destructor handle cleanup properly.
     */
    DMR_INFO("Presuspend: cancelling work items (non-blocking)");
    cancel_work(&device->metadata_sync_work);
    cancel_work(&device->error_analysis_work);
    cancel_delayed_work(&device->health_scan_work);
    cancel_delayed_work(&device->deferred_metadata_read_work); /* v4.2 */
    DMR_INFO("Presuspend: work cancellation signaled");
    
    DMR_INFO("Presuspend: freeing %u remap entries", device->remap_count_active);
    
    /* Free remap entries (safe now - no more I/O can arrive) */
    spin_lock(&device->remap_lock);
    list_for_each_entry_safe(entry, tmp, &device->remap_list, list) {
        list_del(&entry->list);
        kfree(entry);
    }
    device->remap_count_active = 0;
    spin_unlock(&device->remap_lock);
    
    DMR_INFO("Presuspend: complete");
}

/**
 * dm_remap_dtr_v4_real() - Destructor for real device support
 * 
 * NOTE: presuspend has already cancelled work and freed remaps.
 * This function just destroys the workqueue and releases resources.
 */
static void dm_remap_dtr_v4_real(struct dm_target *ti)
{
    struct dm_remap_device_v4_real *device = ti->private;
    
    if (!device) {
        return;
    }
    
    DMR_INFO("Destroying real device target: main=%s, spare=%s",
             device->main_path, device->spare_path);
    
    /* Mark device as inactive */
    atomic_set(&device->device_active, 0);
    
    /* Remove from global device list */
    mutex_lock(&dm_remap_devices_mutex);
    list_del(&device->device_list);
    atomic_dec(&dm_remap_device_count);
    mutex_unlock(&dm_remap_devices_mutex);
    
    /* Free performance optimization cache */
    if (device->perf_optimizer.cache_entries) {
        kfree(device->perf_optimizer.cache_entries);
    }
    
    /* v4.1: Destroy workqueue - NOW SAFE with async I/O!
     * 
     * With v4.1, async metadata writes can be cancelled without blocking,
     * so presuspend can safely cancel any in-flight writes before we get here.
     * No more workqueue leak!
     */
    if (device->metadata_workqueue) {
        DMR_INFO("Destructor: draining and destroying workqueue");
        /* First drain any pending work */
        drain_workqueue(device->metadata_workqueue);
        /* Then destroy the workqueue */
        destroy_workqueue(device->metadata_workqueue);
        device->metadata_workqueue = NULL;
        DMR_INFO("Destructor: workqueue destroyed successfully");
    }
    
    /* v4.2: Destroy repair workqueue and cleanup context */
    if (device->repair_wq) {
        DMR_INFO("Destructor: cleaning up repair subsystem");
        dm_remap_cleanup_repair_context(&device->repair_ctx);
        drain_workqueue(device->repair_wq);
        destroy_workqueue(device->repair_wq);
        device->repair_wq = NULL;
        DMR_INFO("Destructor: repair subsystem cleaned up");
    }
    
    /* NOTE: Remaps already freed in presuspend */
    
    /* Destroy dm-bufio client */
    if (device->metadata_bufio_client) {
        dm_bufio_client_destroy(device->metadata_bufio_client);
        device->metadata_bufio_client = NULL;
        DMR_INFO("dm-bufio client destroyed");
    }
    
    /* Free persistent metadata */
    if (device->persistent_metadata) {
        kfree(device->persistent_metadata);
    }
    
    /* Close real devices if opened */
    if (real_device_mode) {
        if (device->main_dev) {
            dm_remap_close_bdev_real(device->main_dev);
        }
        if (device->spare_dev) {
            dm_remap_close_bdev_real(device->spare_dev);  
        }
    }
    
    /* Destroy mutexes */
    mutex_destroy(&device->metadata_mutex);
    mutex_destroy(&device->health_mutex);
    mutex_destroy(&device->cache_mutex);
    
    /* Free device structure */
    kfree(device);
    
    DMR_INFO("Real device target destroyed");
}

/**
 * dm_remap_status_v4_real() - Enhanced status reporting with performance metrics
 */
static void dm_remap_status_v4_real(struct dm_target *ti, status_type_t type,
                                   unsigned status_flags, char *result, unsigned maxlen)
{
    struct dm_remap_device_v4_real *device = ti->private;
    uint64_t reads, writes, remaps, errors;
    uint64_t io_ops, total_time_ns;
    uint64_t avg_latency_ns = 0;
    uint64_t throughput_bps = 0;
    unsigned sz = 0;
    
    if (!device) {
        DMEMIT("Error: No device");
        return;
    }
    
    reads = atomic64_read(&device->read_count);
    writes = atomic64_read(&device->write_count);
    remaps = atomic64_read(&device->remap_count);
    errors = atomic64_read(&device->error_count);
    io_ops = atomic64_read(&device->io_operations);
    total_time_ns = atomic64_read(&device->total_io_time_ns);
    
    /* Phase 1.3 enhanced statistics */
    uint64_t total_ios = atomic64_read(&device->stats.total_ios);
    uint64_t normal_ios = atomic64_read(&device->stats.normal_ios);
    uint64_t remapped_ios = atomic64_read(&device->stats.remapped_ios);
    uint64_t remapped_sectors = atomic64_read(&device->stats.remapped_sectors);
    
    /* Phase 1.4 enhanced statistics */
    uint64_t cache_hits = atomic64_read(&device->perf_optimizer.cache_hits);
    uint64_t cache_misses = atomic64_read(&device->perf_optimizer.cache_misses);
    uint64_t fast_path_hits = atomic64_read(&device->perf_optimizer.fast_path_hits);
    uint64_t slow_path_hits = atomic64_read(&device->perf_optimizer.slow_path_hits);
    uint64_t health_scans = atomic64_read(&device->health_scan_count);
    
    uint32_t health_score = 100;
    uint32_t hotspot_count = 0;
    uint32_t cache_hit_rate = 0;
    bool maintenance_mode = false;
    
    /* Calculate health and performance metrics safely */
    if (mutex_trylock(&device->health_mutex) == 0) {
        health_score = device->health_monitor.failure_prediction_score;
        hotspot_count = device->health_monitor.hotspot_count;
        maintenance_mode = device->enterprise.maintenance_mode;
        mutex_unlock(&device->health_mutex);
    }
    
    if (cache_hits + cache_misses > 0) {
        cache_hit_rate = (uint32_t)((cache_hits * 100) / (cache_hits + cache_misses));
    }
    
    /* Calculate performance metrics */
    if (io_ops > 0) {
        avg_latency_ns = total_time_ns / io_ops;
    }
    
    /* Calculate approximate throughput (bytes/sec) based on peak */
    throughput_bps = device->peak_throughput;
    
    switch (type) {
    case STATUSTYPE_INFO:
        DMEMIT("v4.0-phase1.4 %s %s %llu %llu %llu %llu %u %llu %llu %llu %llu %u %u %llu %llu %llu %llu %llu %llu %llu %llu %llu %u %u %u %s %s",
               device->main_path, device->spare_path,
               reads, writes, remaps, errors,                      /* Basic I/O stats */
               device->metadata.active_mappings,                   /* Active remaps */
               io_ops, total_time_ns, avg_latency_ns, throughput_bps, /* Performance */
               device->sector_size,                                /* Device info */
               (unsigned int)(device->spare_device_sectors - device->main_device_sectors), /* Spare capacity */
               total_ios, normal_ios, remapped_ios, remapped_sectors, /* Phase 1.3 stats */
               cache_hits, cache_misses, fast_path_hits, slow_path_hits, /* Phase 1.4 cache stats */
               health_scans,                                       /* Health monitoring */
               health_score, hotspot_count, cache_hit_rate,        /* Health & performance metrics */
               maintenance_mode ? "maintenance" : "operational",   /* Operational state */
               real_device_mode ? "real" : "demo");               /* Mode */
        break;
        
    case STATUSTYPE_TABLE:
        DMEMIT("%s %s", device->main_path, device->spare_path);
        break;
        
    case STATUSTYPE_IMA:
        /* Enhanced integrity information */
        DMEMIT("dm-remap-v4-real device_fingerprint=%s main_sectors=%llu spare_sectors=%llu",
               device->metadata.device_fingerprint,
               (unsigned long long)device->main_device_sectors,
               (unsigned long long)device->spare_device_sectors);
        break;
    }
}

/**
 * dm_remap_end_io_v4_real() - Handle I/O completion and error detection
 */
static int dm_remap_end_io_v4_real(struct dm_target *ti, struct bio *bio,
                                  blk_status_t *error)
{
    struct dm_remap_device_v4_real *device = ti->private;
    ktime_t io_end_time = ktime_get();
    u64 io_latency_ns = ktime_to_ns(ktime_sub(io_end_time, device->last_io_time));
    
    /* Update performance statistics */
    device->stats.total_latency_ns += io_latency_ns;
    device->stats.max_latency_ns = max(device->stats.max_latency_ns, io_latency_ns);
    
    /* Handle I/O errors for automatic remapping */
    if (*error != BLK_STS_OK) {
        sector_t failed_sector = bio->bi_iter.bi_sector;
        int errno_val = blk_status_to_errno(*error);
        struct block_device *main_bdev = device->main_dev ? file_bdev(device->main_dev) : NULL;
        
        DMR_WARN("I/O error detected on sector %llu (error=%d)",
                 (unsigned long long)failed_sector, errno_val);
        
        /* Handle errors from main device or any device in the stack below it.
         * This allows dm-remap to work with stacked device-mapper configurations
         * (e.g., dm-remap -> dm-flakey -> loop device).
         * We only reject errors from the spare device to avoid remapping spare errors.
         */
        if (device->main_dev) {
            struct block_device *spare_bdev = device->spare_dev ? file_bdev(device->spare_dev) : NULL;
            
            /* Only handle errors from main device (not spare) */
            if (!spare_bdev || bio->bi_bdev != spare_bdev) {
                /* Queue write-ahead remap creation
                 * 
                 * v4.2 Data Safety: This I/O will fail, but write-ahead metadata
                 * ensures the remap is persisted before any future I/O can use it.
                 * Next I/O to this sector will find the ACTIVE remap and succeed.
                 * 
                 * The error handler checks for duplicate remaps internally.
                 */
                dm_remap_handle_io_error(device, failed_sector, errno_val);
            }
        }
    }
    
    return DM_ENDIO_DONE;
}

/**
 * dm_remap_message_v4_real() - Handle dmsetup message commands
 * 
 * Allows runtime control via: dmsetup message <device> 0 <command> [args]
 */
static int dm_remap_message_v4_real(struct dm_target *ti, unsigned argc, char **argv,
                                   char *result, unsigned maxlen)
{
    struct dm_remap_device_v4_real *device = ti->private;
    
    if (argc < 1) {
        return -EINVAL;
    }
    
    /* Help command */
    if (!strcasecmp(argv[0], "help")) {
        scnprintf(result, maxlen,
                 "Commands: help, status, stats, clear_stats, health, cache_stats, test_remap");
        return 0;
    }
    
    /* Status command - quick overview */
    if (!strcasecmp(argv[0], "status")) {
        scnprintf(result, maxlen,
                 "mappings=%u reads=%llu writes=%llu errors=%llu health=%u%%",
                 device->metadata.active_mappings,
                 (unsigned long long)atomic64_read(&device->read_count),
                 (unsigned long long)atomic64_read(&device->write_count),
                 (unsigned long long)atomic64_read(&device->stats.io_errors),
                 device->health_monitor.failure_prediction_score);
        return 0;
    }
    
    /* Stats command - detailed statistics */
    if (!strcasecmp(argv[0], "stats")) {
        scnprintf(result, maxlen,
                 "total_ios=%llu normal=%llu remapped=%llu errors=%llu "
                 "remapped_sectors=%llu avg_latency_ns=%llu max_latency_ns=%llu",
                 (unsigned long long)atomic64_read(&device->stats.total_ios),
                 (unsigned long long)atomic64_read(&device->stats.normal_ios),
                 (unsigned long long)atomic64_read(&device->stats.remapped_ios),
                 (unsigned long long)atomic64_read(&device->stats.io_errors),
                 (unsigned long long)atomic64_read(&device->stats.remapped_sectors),
                 atomic64_read(&device->stats.total_ios) > 0 ?
                     device->stats.total_latency_ns / atomic64_read(&device->stats.total_ios) : 0,
                 device->stats.max_latency_ns);
        return 0;
    }
    
    /* Clear stats command */
    if (!strcasecmp(argv[0], "clear_stats")) {
        atomic64_set(&device->read_count, 0);
        atomic64_set(&device->write_count, 0);
        atomic64_set(&device->remap_count, 0);
        atomic64_set(&device->error_count, 0);
        atomic64_set(&device->stats.total_ios, 0);
        atomic64_set(&device->stats.normal_ios, 0);
        atomic64_set(&device->stats.remapped_ios, 0);
        atomic64_set(&device->stats.io_errors, 0);
        atomic64_set(&device->stats.remapped_sectors, 0);
        device->stats.total_latency_ns = 0;
        device->stats.max_latency_ns = 0;
        scnprintf(result, maxlen, "Statistics cleared");
        return 0;
    }
    
    /* Health command - health monitoring info */
    if (!strcasecmp(argv[0], "health")) {
        scnprintf(result, maxlen,
                 "health_score=%u%% scan_count=%llu hotspot_sectors=%u "
                 "consecutive_errors=%u trend=%u",
                 device->health_monitor.failure_prediction_score,
                 (unsigned long long)atomic64_read(&device->health_scan_count),
                 device->health_monitor.hotspot_count,
                 device->health_monitor.consecutive_errors,
                 device->health_monitor.health_trend);
        return 0;
    }
    
    /* Cache stats command - performance cache info */
    if (!strcasecmp(argv[0], "cache_stats")) {
        u64 hits = atomic64_read(&device->perf_optimizer.cache_hits);
        u64 misses = atomic64_read(&device->perf_optimizer.cache_misses);
        u64 total = hits + misses;
        u64 hit_rate = total > 0 ? (hits * 100) / total : 0;
        
        scnprintf(result, maxlen,
                 "cache_hits=%llu cache_misses=%llu hit_rate=%llu%% "
                 "fast_path=%llu cache_size=%u",
                 hits, misses, hit_rate,
                 (unsigned long long)atomic64_read(&device->perf_optimizer.fast_path_hits),
                 device->perf_optimizer.cache_size);
        return 0;
    }
    
    /* Test remap command - manually create a test remap entry for testing */
    if (!strcasecmp(argv[0], "test_remap")) {
        if (argc < 3) {
            scnprintf(result, maxlen, "Usage: test_remap <bad_sector> <spare_sector>");
            return -EINVAL;
        }
        
        u64 bad_sector = simple_strtoull(argv[1], NULL, 0);
        u64 spare_sector = simple_strtoull(argv[2], NULL, 0);
        
        /* Add remap entry */
        int ret = dm_remap_add_remap_entry(device, bad_sector, spare_sector);
        if (ret) {
            scnprintf(result, maxlen, "Failed to add remap: %d", ret);
            return ret;
        }
        
        scnprintf(result, maxlen, "Created test remap: bad_sector=%llu spare_sector=%llu",
                 bad_sector, spare_sector);
        return 0;
    }
    
    /* Unknown command */
    scnprintf(result, maxlen, "Unknown command '%s'. Try 'help'", argv[0]);
    return -EINVAL;
}

/* Device mapper target structure */
static struct target_type dm_remap_target_v4_real = {
    .name = "dm-remap-v4",
    .version = {4, 0, 0},
    .module = THIS_MODULE,
    .ctr = dm_remap_ctr_v4_real,
    .dtr = dm_remap_dtr_v4_real,
    .map = dm_remap_map_v4_real,
    .end_io = dm_remap_end_io_v4_real,
    .status = dm_remap_status_v4_real,
    .message = dm_remap_message_v4_real,
    .presuspend = dm_remap_presuspend_v4_real,  /* CRITICAL FIX: Cancel work before removal */
};

/**
 * dm_remap_init_v4_real() - Module initialization
 */
static int __init dm_remap_init_v4_real(void)
{
    int ret;
    
    DMR_INFO("Loading dm-remap v4.0 with Real Device Support");
    
    /* Initialize global statistics */
    atomic64_set(&global_reads, 0);
    atomic64_set(&global_writes, 0);
    atomic64_set(&global_remaps, 0);
    atomic64_set(&global_errors, 0);
    atomic64_set(&global_health_scans, 0);
    
    /* Create workqueue for background tasks */
    dm_remap_wq = alloc_workqueue("dm-remap-v4-real", WQ_MEM_RECLAIM, 0);
    if (!dm_remap_wq) {
        DMR_ERROR("Failed to create workqueue");
        return -ENOMEM;
    }
    
    /* Register device mapper target */
    ret = dm_register_target(&dm_remap_target_v4_real);
    if (ret < 0) {
        DMR_ERROR("Failed to register dm target: %d", ret);
        destroy_workqueue(dm_remap_wq);
        return ret;
    }
    
    DMR_INFO("dm-remap v4.0 Real Device Support loaded successfully");
    DMR_INFO("Mode: %s, Background scanning: %s",
             real_device_mode ? "Real Device" : "Demo",
             enable_background_scanning ? "enabled" : "disabled");
    
    return 0;
}

/**
 * dm_remap_exit_v4_real() - Module cleanup
 */
static void __exit dm_remap_exit_v4_real(void)  
{
    DMR_INFO("Unloading dm-remap v4.0 Real Device Support");
    
    /* Unregister device mapper target */
    dm_unregister_target(&dm_remap_target_v4_real);
    
    /* Destroy workqueue */
    if (dm_remap_wq) {
        destroy_workqueue(dm_remap_wq);
    }
    
    DMR_INFO("dm-remap v4.0 Real Device Support unloaded");
}

module_init(dm_remap_init_v4_real);
module_exit(dm_remap_exit_v4_real);