/**
 * dm-remap-v4-core.c - Pure v4.0 Core Implementation
 * 
 * Copyright (C) 2025 dm-remap Development Team
 * 
 * This is the main dm-remap v4.0 implementation with:
 * - Clean slate architecture (no v3.0 compatibility)
 * - Enterprise features built-in (health monitoring, discovery)
 * - Optimal performance (<1% overhead target)
 * - Modern kernel patterns (work queues, atomic operations)
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

#include "dm-remap-v4.h"

/* Module metadata */
MODULE_DESCRIPTION("Device Mapper Remapping Target v4.0 - Enterprise Edition");
MODULE_AUTHOR("dm-remap Development Team");
MODULE_LICENSE("GPL");
MODULE_VERSION("4.0.0");

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

/* Global device list and protection */
static LIST_HEAD(dm_remap_devices);
static DEFINE_MUTEX(dm_remap_devices_mutex);
static atomic_t dm_remap_device_count = ATOMIC_INIT(0);

/* Global statistics */
struct dm_remap_global_stats {
    atomic64_t total_reads;
    atomic64_t total_writes;
    atomic64_t total_remaps;
    atomic64_t total_errors;
    atomic64_t devices_created;
    atomic64_t background_scans_completed;
};

static struct dm_remap_global_stats global_stats;

/**
 * dm_remap_create_device_v4() - Create new v4.0 device instance
 */
struct dm_remap_device_v4 *dm_remap_create_device_v4(struct block_device *main_dev,
                                                     struct block_device *spare_dev)
{
    struct dm_remap_device_v4 *device;
    int ret;
    
    device = kzalloc(sizeof(*device), GFP_KERNEL);
    if (!device) {
        return ERR_PTR(-ENOMEM);
    }
    
    /* Initialize device structure */
    device->main_dev = main_dev;
    device->spare_dev = spare_dev;
    mutex_init(&device->metadata_mutex);
    atomic_set(&device->device_active, 1);
    INIT_LIST_HEAD(&device->device_list);
    
    /* Generate device fingerprint */
    ret = dm_remap_generate_fingerprint(spare_dev, &device->fingerprint);
    if (ret) {
        DMR_DEBUG(0, "Failed to generate device fingerprint: %d", ret);
        kfree(device);
        return ERR_PTR(ret);
    }
    
    /* Try to read existing metadata */
    ret = dm_remap_read_metadata_v4(spare_dev, &device->metadata);
    if (ret == -ENODATA) {
        /* No existing metadata - initialize new */
        char main_uuid[37] = "unknown";
        char spare_uuid[37] = "unknown";
        
        /* TODO: Extract actual device UUIDs */
        
        dm_remap_init_metadata_v4(&device->metadata,
                                 main_uuid, spare_uuid,
                                 get_capacity(main_dev->bd_disk),
                                 get_capacity(spare_dev->bd_disk));
        
        /* Write initial metadata */
        ret = dm_remap_write_metadata_v4(spare_dev, &device->metadata);
        if (ret) {
            DMR_DEBUG(0, "Failed to write initial metadata: %d", ret);
            kfree(device);
            return ERR_PTR(ret);
        }
        
        DMR_DEBUG(1, "Created new v4.0 device with fresh metadata");
    } else if (ret) {
        DMR_DEBUG(0, "Failed to read metadata: %d", ret);
        kfree(device);
        return ERR_PTR(ret);
    } else {
        /* Validate existing metadata against device fingerprint */
        ret = dm_remap_validate_fingerprint(spare_dev, &device->fingerprint);
        if (ret) {
            DMR_DEBUG(0, "Device fingerprint validation failed: %d", ret);
            kfree(device);
            return ERR_PTR(ret);
        }
        
        DMR_DEBUG(1, "Loaded existing v4.0 device: health=%u%%, remaps=%u",
                  device->metadata.health_data.health_score,
                  device->metadata.remap_data.active_remaps);
    }
    
    /* Initialize background scanner if enabled */
    if (enable_background_scanning) {
        ret = dm_remap_scanner_init(&device->scanner, device);
        if (ret) {
            DMR_DEBUG(0, "Failed to initialize background scanner: %d", ret);
            /* Continue without scanner - not fatal */
        } else {
            ret = dm_remap_scanner_start(&device->scanner);
            if (ret) {
                DMR_DEBUG(0, "Failed to start background scanner: %d", ret);
            }
        }
    }
    
    /* Add to global device list */
    mutex_lock(&dm_remap_devices_mutex);
    list_add(&device->device_list, &dm_remap_devices);
    atomic_inc(&dm_remap_device_count);
    mutex_unlock(&dm_remap_devices_mutex);
    
    atomic64_inc(&global_stats.devices_created);
    
    DMR_DEBUG(1, "Created v4.0 device: main=%s, spare=%s",
              main_dev->bd_disk->disk_name, spare_dev->bd_disk->disk_name);
    
    return device;
}

/**
 * dm_remap_destroy_device_v4() - Clean up v4.0 device instance
 */
void dm_remap_destroy_device_v4(struct dm_remap_device_v4 *device)
{
    if (!device) {
        return;
    }
    
    DMR_DEBUG(1, "Destroying v4.0 device: main=%s, spare=%s",
              device->main_dev->bd_disk->disk_name, device->spare_dev->bd_disk->disk_name);
    
    /* Mark device as inactive */
    atomic_set(&device->device_active, 0);
    
    /* Stop background scanner */
    dm_remap_scanner_stop(&device->scanner);
    dm_remap_scanner_cleanup(&device->scanner);
    
    /* Remove from global list */
    mutex_lock(&dm_remap_devices_mutex);
    list_del(&device->device_list);
    atomic_dec(&dm_remap_device_count);
    mutex_unlock(&dm_remap_devices_mutex);
    
    /* Write final metadata update */
    if (device->metadata_dirty) {
        int ret = dm_remap_write_metadata_v4(device->spare_dev, &device->metadata);
        if (ret) {
            DMR_DEBUG(0, "Failed to write final metadata: %d", ret);
        }
    }
    
    kfree(device);
}

/**
 * dm_remap_add_remap_v4() - Add new sector remap
 */
int dm_remap_add_remap_v4(struct dm_remap_device_v4 *device,
                         uint64_t original_sector, uint64_t spare_sector,
                         uint16_t reason)
{
    struct dm_remap_metadata_v4 *metadata = &device->metadata;
    uint32_t remap_index;
    int ret = 0;
    
    mutex_lock(&device->metadata_mutex);
    
    /* Check if we have space for new remap */
    if (metadata->remap_data.active_remaps >= metadata->remap_data.max_remaps) {
        DMR_DEBUG(0, "No space for new remap: %u/%u used",
                  metadata->remap_data.active_remaps, metadata->remap_data.max_remaps);
        ret = -ENOSPC;
        goto out;
    }
    
    /* Find next available remap slot */
    remap_index = metadata->remap_data.active_remaps;
    
    /* Initialize remap entry */
    metadata->remap_data.remaps[remap_index].original_sector = original_sector;
    metadata->remap_data.remaps[remap_index].spare_sector = spare_sector;
    metadata->remap_data.remaps[remap_index].remap_timestamp = ktime_get_real_seconds();
    metadata->remap_data.remaps[remap_index].access_count = 0;
    metadata->remap_data.remaps[remap_index].error_count = 1; /* Initial error that caused remap */
    metadata->remap_data.remaps[remap_index].remap_reason = reason;
    metadata->remap_data.remaps[remap_index].flags = 0;
    
    /* Update counters */
    metadata->remap_data.active_remaps++;
    metadata->remap_data.next_spare_sector = spare_sector + 1;
    
    /* Mark metadata dirty for eventual write */
    device->metadata_dirty = true;
    
    atomic64_inc(&global_stats.total_remaps);
    atomic64_inc(&device->stats.remap_count);
    
    DMR_DEBUG(1, "Added remap %u: sector %llu -> %llu (reason=%u)",
              remap_index, original_sector, spare_sector, reason);

out:
    mutex_unlock(&device->metadata_mutex);
    return ret;
}

/**
 * dm_remap_lookup_v4() - Look up remap for sector
 */
static uint64_t dm_remap_lookup_v4(struct dm_remap_device_v4 *device, uint64_t sector)
{
    struct dm_remap_metadata_v4 *metadata = &device->metadata;
    uint32_t i;
    
    /* Simple linear search - TODO: optimize with hash table for large remap counts */
    for (i = 0; i < metadata->remap_data.active_remaps; i++) {
        if (metadata->remap_data.remaps[i].original_sector == sector) {
            /* Update access count */
            metadata->remap_data.remaps[i].access_count++;
            return metadata->remap_data.remaps[i].spare_sector;
        }
    }
    
    return sector; /* No remap found, return original sector */
}

/**
 * dm_remap_map_v4() - Main I/O mapping function
 */
static int dm_remap_map_v4(struct dm_target *ti, struct bio *bio)
{
    struct dm_remap_device_v4 *device = ti->private;
    uint64_t original_sector = bio->bi_iter.bi_sector;
    uint64_t mapped_sector;
    ktime_t start_time, end_time;
    bool is_read = bio_data_dir(bio) == READ;
    
    start_time = ktime_get();
    
    /* Check if device is active */
    if (!atomic_read(&device->device_active)) {
        return DM_MAPIO_KILL;
    }
    
    /* Look up any existing remap */
    mapped_sector = dm_remap_lookup_v4(device, original_sector);
    
    /* Update bio to point to mapped sector */
    bio_set_dev(bio, device->main_dev);
    bio->bi_iter.bi_sector = mapped_sector;
    
    /* Update statistics */
    if (is_read) {
        atomic64_inc(&device->stats.read_count);
        atomic64_inc(&global_stats.total_reads);
    } else {
        atomic64_inc(&device->stats.write_count);
        atomic64_inc(&global_stats.total_writes);
    }
    
    /* Track performance */
    end_time = ktime_get();
    atomic64_add(ktime_to_ns(ktime_sub(end_time, start_time)),
                 &device->stats.total_latency_ns);
    
    if (mapped_sector != original_sector) {
        DMR_DEBUG(3, "Remapped I/O: %llu -> %llu", original_sector, mapped_sector);
    }
    
    return DM_MAPIO_REMAPPED;
}

/**
 * dm_remap_ctr_v4() - Constructor for v4.0 target
 */
static int dm_remap_ctr_v4(struct dm_target *ti, unsigned int argc, char **argv)
{
    struct dm_remap_device_v4 *device;
    struct block_device *main_dev, *spare_dev;
    char *main_path, *spare_path;
    int ret;
    
    if (argc != 2) {
        ti->error = "Invalid argument count: dm-remap-v4 <main_device> <spare_device>";
        return -EINVAL;
    }
    
    main_path = argv[0];
    spare_path = argv[1];
    
    DMR_DEBUG(1, "Creating v4.0 target: main=%s, spare=%s", main_path, spare_path);
    
    /* Open main device */
    main_dev = lookup_bdev(main_path);
    if (IS_ERR(main_dev)) {
        ti->error = "Cannot open main device";
        return PTR_ERR(main_dev);
    }
    
    /* Open spare device */
    spare_dev = lookup_bdev(spare_path);
    if (IS_ERR(spare_dev)) {
        ti->error = "Cannot open spare device";
        return PTR_ERR(spare_dev);
    }
    
    /* Create device instance */
    device = dm_remap_create_device_v4(main_dev, spare_dev);
    if (IS_ERR(device)) {
        ti->error = "Failed to create v4.0 device instance";
        ret = PTR_ERR(device);
        blkdev_put(spare_dev, FMODE_READ | FMODE_WRITE);
        blkdev_put(main_dev, FMODE_READ | FMODE_WRITE);
        return ret;
    }
    
    /* Set target private data */
    ti->private = device;
    
    DMR_DEBUG(1, "v4.0 target created successfully");
    return 0;
}

/**
 * dm_remap_dtr_v4() - Destructor for v4.0 target
 */
static void dm_remap_dtr_v4(struct dm_target *ti)
{
    struct dm_remap_device_v4 *device = ti->private;
    
    if (device) {
        struct block_device *main_dev = device->main_dev;
        struct block_device *spare_dev = device->spare_dev;
        
        dm_remap_destroy_device_v4(device);
        
        DMR_DEBUG(1, "v4.0 target destroyed");
    }
}

/**
 * dm_remap_status_v4() - Status reporting for v4.0 target
 */
static void dm_remap_status_v4(struct dm_target *ti, status_type_t type,
                              unsigned status_flags, char *result, unsigned maxlen)
{
    struct dm_remap_device_v4 *device = ti->private;
    
    switch (type) {
    case STATUSTYPE_INFO:
        scnprintf(result, maxlen,
                 "v4.0 health:%u%% remaps:%u/%u scanned:%u%% errors:%llu",
                 device->metadata.health_data.health_score,
                 device->metadata.remap_data.active_remaps,
                 device->metadata.remap_data.max_remaps,
                 device->metadata.health_data.scan_progress_percent,
                 atomic64_read(&device->stats.error_count));
        break;
        
    case STATUSTYPE_TABLE:
        scnprintf(result, maxlen, "%s %s",
                 device->main_dev->bd_disk->disk_name,
                 device->spare_dev->bd_disk->disk_name);
        break;
        
    case STATUSTYPE_IMA:
        /* IMA status not supported in v4.0 */
        break;
    }
}

/* Device mapper target structure */
static struct target_type dm_remap_target_v4 = {
    .name    = "remap-v4",
    .version = {4, 0, 0},
    .module  = THIS_MODULE,
    .ctr     = dm_remap_ctr_v4,
    .dtr     = dm_remap_dtr_v4,
    .map     = dm_remap_map_v4,
    .status  = dm_remap_status_v4,
};

/**
 * Module initialization
 */
static int __init dm_remap_v4_init(void)
{
    int ret;
    
    printk(KERN_INFO "dm-remap v4.0: Enterprise Storage Remapping Target\n");
    printk(KERN_INFO "dm-remap v4.0: Clean slate architecture - no legacy overhead\n");
    
    /* Initialize global statistics */
    memset(&global_stats, 0, sizeof(global_stats));
    
    /* Initialize subsystems */
    ret = dm_remap_metadata_v4_init();
    if (ret) {
        printk(KERN_ERR "dm-remap v4.0: Failed to initialize metadata system: %d\n", ret);
        return ret;
    }
    
    ret = dm_remap_health_v4_init();
    if (ret) {
        printk(KERN_ERR "dm-remap v4.0: Failed to initialize health system: %d\n", ret);
        dm_remap_metadata_v4_cleanup();
        return ret;
    }
    
    ret = dm_remap_discovery_v4_init();
    if (ret) {
        printk(KERN_ERR "dm-remap v4.0: Failed to initialize discovery system: %d\n", ret);
        dm_remap_health_v4_cleanup();
        dm_remap_metadata_v4_cleanup();
        return ret;
    }
    
    /* Register device mapper target */
    ret = dm_register_target(&dm_remap_target_v4);
    if (ret) {
        printk(KERN_ERR "dm-remap v4.0: Failed to register target: %d\n", ret);
        dm_remap_discovery_v4_cleanup();
        dm_remap_health_v4_cleanup();
        dm_remap_metadata_v4_cleanup();
        return ret;
    }
    
    /* Perform automatic device discovery if enabled */
    if (enable_background_scanning) {
        int devices_found = dm_remap_discover_devices_v4();
        if (devices_found > 0) {
            printk(KERN_INFO "dm-remap v4.0: Discovered %d existing devices\n", devices_found);
        }
    }
    
    printk(KERN_INFO "dm-remap v4.0: Module loaded successfully\n");
    return 0;
}

/**
 * Module cleanup
 */
static void __exit dm_remap_v4_exit(void)
{
    struct dm_remap_device_v4 *device, *tmp;
    
    printk(KERN_INFO "dm-remap v4.0: Module unloading...\n");
    
    /* Unregister target */
    dm_unregister_target(&dm_remap_target_v4);
    
    /* Clean up all devices */
    mutex_lock(&dm_remap_devices_mutex);
    list_for_each_entry_safe(device, tmp, &dm_remap_devices, device_list) {
        dm_remap_destroy_device_v4(device);
    }
    mutex_unlock(&dm_remap_devices_mutex);
    
    /* Clean up subsystems */
    dm_remap_discovery_v4_cleanup();
    dm_remap_health_v4_cleanup();
    dm_remap_metadata_v4_cleanup();
    
    printk(KERN_INFO "dm-remap v4.0: Module unloaded. Stats: reads=%llu, writes=%llu, remaps=%llu\n",
           atomic64_read(&global_stats.total_reads),
           atomic64_read(&global_stats.total_writes),
           atomic64_read(&global_stats.total_remaps));
}

module_init(dm_remap_v4_init);
module_exit(dm_remap_v4_exit);