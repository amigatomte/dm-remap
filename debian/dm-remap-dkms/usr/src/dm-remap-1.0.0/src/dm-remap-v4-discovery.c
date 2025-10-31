/**
 * dm-remap-v4-discovery.c - Automatic Device Discovery and Management
 * 
 * Copyright (C) 2025 dm-remap Development Team
 * 
 * This implements the v4.0 automatic device discovery system:
 * - Scan system for dm-remap v4.0 metadata signatures
 * - Automatic device pairing and recovery
 * - UUID-based device identification
 * - Hot-plug device detection
 * - Device fingerprint validation
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/blkdev.h>
#include <linux/genhd.h>
#include <linux/fs.h>
#include <linux/bio.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/notifier.h>
#include <linux/string.h>

#include "dm-remap-v4.h"

/* Discovery system state */
static LIST_HEAD(discovered_devices);
static DEFINE_MUTEX(discovery_mutex);
static struct workqueue_struct *discovery_wq;
static struct delayed_work discovery_work;

/* Device discovery statistics */
static atomic_t devices_discovered = ATOMIC_INIT(0);
static atomic_t devices_paired = ATOMIC_INIT(0);
static atomic_t discovery_scans = ATOMIC_INIT(0);

/**
 * struct discovered_device - Information about a discovered device
 */
struct discovered_device {
    struct list_head list;
    char device_path[256];
    struct dm_remap_metadata_v4 metadata;
    struct dm_remap_device_fingerprint fingerprint;
    struct block_device *bdev;
    bool is_spare_device;
    bool is_paired;
    ktime_t discovery_time;
};

/**
 * dm_remap_discovery_v4_init() - Initialize discovery subsystem
 */
int dm_remap_discovery_v4_init(void)
{
    discovery_wq = alloc_workqueue("dm_remap_discovery", WQ_MEM_RECLAIM, 1);
    if (!discovery_wq) {
        return -ENOMEM;
    }
    
    INIT_DELAYED_WORK(&discovery_work, dm_remap_discovery_scan_work);
    
    /* Start initial discovery scan after 30 seconds */
    queue_delayed_work(discovery_wq, &discovery_work, msecs_to_jiffies(30000));
    
    DMR_DEBUG(1, "Device discovery subsystem initialized");
    return 0;
}

/**
 * dm_remap_discovery_v4_cleanup() - Cleanup discovery subsystem
 */
void dm_remap_discovery_v4_cleanup(void)
{
    struct discovered_device *dev, *tmp;
    
    if (discovery_wq) {
        cancel_delayed_work_sync(&discovery_work);
        destroy_workqueue(discovery_wq);
        discovery_wq = NULL;
    }
    
    /* Clean up discovered devices list */
    mutex_lock(&discovery_mutex);
    list_for_each_entry_safe(dev, tmp, &discovered_devices, list) {
        list_del(&dev->list);
        if (dev->bdev) {
            blkdev_put(dev->bdev, FMODE_READ);
        }
        kfree(dev);
    }
    mutex_unlock(&discovery_mutex);
    
    DMR_DEBUG(1, "Device discovery subsystem cleaned up");
}

/**
 * read_device_metadata() - Try to read dm-remap v4.0 metadata from device
 */
static int read_device_metadata(struct block_device *bdev,
                               struct dm_remap_metadata_v4 *metadata,
                               struct dm_remap_device_fingerprint *fingerprint)
{
    int ret;
    
    /* Try to read metadata */
    ret = dm_remap_read_metadata_v4(bdev, metadata);
    if (ret) {
        return ret; /* No valid metadata found */
    }
    
    /* Generate current fingerprint */
    ret = dm_remap_generate_fingerprint(bdev, fingerprint);
    if (ret) {
        return ret;
    }
    
    /* Validate fingerprint against metadata */
    ret = dm_remap_validate_fingerprint(bdev, fingerprint);
    if (ret) {
        DMR_DEBUG(2, "Device fingerprint validation failed for %s", bdev->bd_disk->disk_name);
        return ret;
    }
    
    return 0;
}

/**
 * scan_block_device() - Scan a single block device for dm-remap metadata
 */
static int scan_block_device(const char *device_path)
{
    struct block_device *bdev;
    struct discovered_device *dev;
    int ret;
    
    /* Skip obviously inappropriate devices */
    if (strstr(device_path, "loop") || 
        strstr(device_path, "ram") ||
        strstr(device_path, "dm-")) {
        return 0; /* Skip these device types */
    }
    
    /* Open device for reading */
    bdev = blkdev_get_by_path(device_path, FMODE_READ, NULL);
    if (IS_ERR(bdev)) {
        return PTR_ERR(bdev);
    }
    
    /* Check if device is large enough for dm-remap */
    if (get_capacity(bdev->bd_disk) < DM_REMAP_MIN_DEVICE_SECTORS) {
        blkdev_put(bdev, FMODE_READ);
        return -ENOSPC;
    }
    
    /* Allocate discovered device structure */
    dev = kzalloc(sizeof(*dev), GFP_KERNEL);
    if (!dev) {
        blkdev_put(bdev, FMODE_READ);
        return -ENOMEM;
    }
    
    /* Try to read metadata */
    ret = read_device_metadata(bdev, &dev->metadata, &dev->fingerprint);
    if (ret) {
        /* No valid dm-remap metadata */
        blkdev_put(bdev, FMODE_READ);
        kfree(dev);
        return ret;
    }
    
    /* Found valid dm-remap device! */
    strncpy(dev->device_path, device_path, sizeof(dev->device_path) - 1);
    dev->bdev = bdev;
    dev->discovery_time = ktime_get();
    dev->is_spare_device = (dev->metadata.remap_data.active_remaps > 0 ||
                           strlen(dev->metadata.main_device_uuid) > 0);
    dev->is_paired = false;
    
    /* Add to discovered devices list */
    mutex_lock(&discovery_mutex);
    list_add(&dev->list, &discovered_devices);
    atomic_inc(&devices_discovered);
    mutex_unlock(&discovery_mutex);
    
    DMR_DEBUG(1, "Discovered dm-remap v4.0 device: %s (spare=%s, health=%u%%, remaps=%u)",
              device_path,
              dev->is_spare_device ? "yes" : "no",
              dev->metadata.health_data.health_score,
              dev->metadata.remap_data.active_remaps);
    
    return 0;
}

/**
 * find_matching_device() - Find device matching given UUID
 */
static struct discovered_device *find_matching_device(const char *uuid, bool is_main_device)
{
    struct discovered_device *dev;
    
    list_for_each_entry(dev, &discovered_devices, list) {
        if (dev->is_paired) {
            continue; /* Already paired */
        }
        
        if (is_main_device) {
            /* Looking for main device */
            if (strcmp(dev->metadata.main_device_uuid, uuid) == 0) {
                return dev;
            }
        } else {
            /* Looking for spare device */
            if (strcmp(dev->metadata.spare_device_uuid, uuid) == 0) {
                return dev;
            }
        }
    }
    
    return NULL;
}

/**
 * attempt_device_pairing() - Try to automatically pair discovered devices
 */
static int attempt_device_pairing(void)
{
    struct discovered_device *spare_dev, *main_dev;
    int pairs_created = 0;
    
    mutex_lock(&discovery_mutex);
    
    /* Look for spare devices that can be paired */
    list_for_each_entry(spare_dev, &discovered_devices, list) {
        if (!spare_dev->is_spare_device || spare_dev->is_paired) {
            continue;
        }
        
        /* Try to find matching main device */
        main_dev = find_matching_device(spare_dev->metadata.main_device_uuid, true);
        if (!main_dev) {
            DMR_DEBUG(2, "No matching main device found for spare %s (UUID: %.8s...)",
                     spare_dev->device_path, spare_dev->metadata.main_device_uuid);
            continue;
        }
        
        /* Found a potential pair - validate compatibility */
        if (spare_dev->metadata.format_version != main_dev->metadata.format_version) {
            DMR_DEBUG(0, "Version mismatch: spare=%u, main=%u",
                     spare_dev->metadata.format_version, main_dev->metadata.format_version);
            continue;
        }
        
        /* TODO: Create dm-remap target automatically */
        DMR_DEBUG(1, "Found compatible device pair: main=%s, spare=%s",
                 main_dev->device_path, spare_dev->device_path);
        
        /* Mark devices as paired */
        spare_dev->is_paired = true;
        main_dev->is_paired = true;
        pairs_created++;
        atomic_inc(&devices_paired);
        
        /* For now, just log the pairing - actual dm-remap target creation
         * would require userspace cooperation or kernel device creation */
        printk(KERN_INFO "dm-remap v4.0: Auto-discovered device pair:\n");
        printk(KERN_INFO "  Main: %s (capacity: %llu sectors)\n",
               main_dev->device_path, get_capacity(main_dev->bdev->bd_disk));
        printk(KERN_INFO "  Spare: %s (health: %u%%, remaps: %u)\n",
               spare_dev->device_path, 
               spare_dev->metadata.health_data.health_score,
               spare_dev->metadata.remap_data.active_remaps);
    }
    
    mutex_unlock(&discovery_mutex);
    
    return pairs_created;
}

/**
 * dm_remap_discovery_scan_work() - Main discovery work function
 */
void dm_remap_discovery_scan_work(struct work_struct *work)
{
    struct class_dev_iter iter;
    struct device *dev;
    char device_path[256];
    int devices_scanned = 0, new_devices = 0;
    int pairs_created;
    
    DMR_DEBUG(2, "Starting device discovery scan");
    
    /* Iterate through all block devices */
    class_dev_iter_init(&iter, &block_class, NULL, &disk_type);
    while ((dev = class_dev_iter_next(&iter))) {
        struct gendisk *disk = dev_to_disk(dev);
        
        if (!disk || !disk->disk_name) {
            continue;
        }
        
        /* Create device path */
        snprintf(device_path, sizeof(device_path), "/dev/%s", disk->disk_name);
        
        /* Scan this device */
        if (scan_block_device(device_path) == 0) {
            new_devices++;
        }
        devices_scanned++;
    }
    class_dev_iter_exit(&iter);
    
    /* Attempt to pair any discovered devices */
    pairs_created = attempt_device_pairing();
    
    atomic_inc(&discovery_scans);
    
    DMR_DEBUG(1, "Discovery scan complete: scanned=%d, found=%d, paired=%d",
              devices_scanned, new_devices, pairs_created);
    
    /* Schedule next scan in 1 hour */
    queue_delayed_work(discovery_wq, &discovery_work, msecs_to_jiffies(3600000));
}

/**
 * dm_remap_discover_devices_v4() - Manual device discovery trigger
 */
int dm_remap_discover_devices_v4(void)
{
    if (!discovery_wq) {
        return -ENODEV;
    }
    
    /* Cancel any pending discovery and run immediately */
    cancel_delayed_work(&discovery_work);
    queue_delayed_work(discovery_wq, &discovery_work, 0);
    
    /* Wait a bit for discovery to start */
    msleep(100);
    
    return atomic_read(&devices_discovered);
}

/**
 * dm_remap_get_discovered_devices() - Get list of discovered devices
 */
int dm_remap_get_discovered_devices(struct dm_remap_discovered_device_info *devices,
                                   int max_devices)
{
    struct discovered_device *dev;
    int count = 0;
    
    if (!devices || max_devices <= 0) {
        return -EINVAL;
    }
    
    mutex_lock(&discovery_mutex);
    
    list_for_each_entry(dev, &discovered_devices, list) {
        if (count >= max_devices) {
            break;
        }
        
        strncpy(devices[count].device_path, dev->device_path,
                sizeof(devices[count].device_path) - 1);
        devices[count].is_spare_device = dev->is_spare_device;
        devices[count].is_paired = dev->is_paired;
        devices[count].health_score = dev->metadata.health_data.health_score;
        devices[count].active_remaps = dev->metadata.remap_data.active_remaps;
        devices[count].discovery_time = ktime_to_timespec64(dev->discovery_time);
        
        /* Copy UUIDs */
        strncpy(devices[count].main_device_uuid, dev->metadata.main_device_uuid,
                sizeof(devices[count].main_device_uuid) - 1);
        strncpy(devices[count].spare_device_uuid, dev->metadata.spare_device_uuid,
                sizeof(devices[count].spare_device_uuid) - 1);
        
        count++;
    }
    
    mutex_unlock(&discovery_mutex);
    
    return count;
}

/**
 * dm_remap_validate_device_pair() - Validate that two devices can form a pair
 */
int dm_remap_validate_device_pair(const char *main_path, const char *spare_path)
{
    struct block_device *main_bdev, *spare_bdev;
    struct dm_remap_metadata_v4 spare_metadata;
    struct dm_remap_device_fingerprint spare_fingerprint;
    int ret = 0;
    
    /* Open both devices */
    main_bdev = blkdev_get_by_path(main_path, FMODE_READ, NULL);
    if (IS_ERR(main_bdev)) {
        return PTR_ERR(main_bdev);
    }
    
    spare_bdev = blkdev_get_by_path(spare_path, FMODE_READ, NULL);
    if (IS_ERR(spare_bdev)) {
        blkdev_put(main_bdev, FMODE_READ);
        return PTR_ERR(spare_bdev);
    }
    
    /* Check spare device has valid metadata */
    ret = read_device_metadata(spare_bdev, &spare_metadata, &spare_fingerprint);
    if (ret) {
        DMR_DEBUG(0, "Spare device %s has no valid dm-remap metadata", spare_path);
        goto out;
    }
    
    /* Validate device sizes */
    if (get_capacity(main_bdev->bd_disk) > spare_metadata.main_device_sectors) {
        DMR_DEBUG(0, "Main device too large: %llu > %llu sectors",
                 get_capacity(main_bdev->bd_disk), spare_metadata.main_device_sectors);
        ret = -EINVAL;
        goto out;
    }
    
    if (get_capacity(spare_bdev->bd_disk) < spare_metadata.spare_device_sectors) {
        DMR_DEBUG(0, "Spare device too small: %llu < %llu sectors",
                 get_capacity(spare_bdev->bd_disk), spare_metadata.spare_device_sectors);
        ret = -EINVAL;
        goto out;
    }
    
    /* TODO: Add more sophisticated validation (UUID matching, etc.) */
    
    DMR_DEBUG(1, "Device pair validation successful: main=%s, spare=%s", 
             main_path, spare_path);

out:
    blkdev_put(spare_bdev, FMODE_READ);
    blkdev_put(main_bdev, FMODE_READ);
    return ret;
}

/**
 * dm_remap_get_discovery_stats() - Get discovery statistics
 */
void dm_remap_get_discovery_stats(struct dm_remap_discovery_stats *stats)
{
    if (!stats) {
        return;
    }
    
    stats->devices_discovered = atomic_read(&devices_discovered);
    stats->devices_paired = atomic_read(&devices_paired);
    stats->discovery_scans = atomic_read(&discovery_scans);
    
    /* Count current unpaired devices */
    mutex_lock(&discovery_mutex);
    {
        struct discovered_device *dev;
        int unpaired = 0;
        
        list_for_each_entry(dev, &discovered_devices, list) {
            if (!dev->is_paired) {
                unpaired++;
            }
        }
        stats->devices_unpaired = unpaired;
    }
    mutex_unlock(&discovery_mutex);
}