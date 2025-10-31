/**
 * dm-remap-v4-health.c - Background Health Scanning and Monitoring
 * 
 * Copyright (C) 2025 dm-remap Development Team
 * 
 * This implements the v4.0 background health scanning system:
 * - Work queue-based intelligent scheduling
 * - Predictive failure detection using ML-inspired heuristics
 * - <1% performance overhead target
 * - Adaptive scanning frequency based on device health
 * - Proactive sector remapping before failures occur
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/random.h>
#include <linux/crc32.h>
#include <linux/ktime.h>
#include <linux/atomic.h>

#include "dm-remap-v4.h"

/* Global health scanning statistics */
static atomic64_t total_scans_completed = ATOMIC64_INIT(0);
static atomic64_t total_sectors_scanned = ATOMIC64_INIT(0);
static atomic64_t total_errors_detected = ATOMIC64_INIT(0);
static atomic64_t total_preventive_remaps = ATOMIC64_INIT(0);

/* Health scanning workqueue */
static struct workqueue_struct *dm_remap_health_wq;

/**
 * dm_remap_health_v4_init() - Initialize health scanning subsystem
 */
int dm_remap_health_v4_init(void)
{
    dm_remap_health_wq = alloc_workqueue("dm_remap_health", 
                                        WQ_MEM_RECLAIM | WQ_UNBOUND, 0);
    if (!dm_remap_health_wq) {
        return -ENOMEM;
    }
    
    DMR_DEBUG(1, "Health scanning subsystem initialized");
    return 0;
}

/**
 * dm_remap_health_v4_cleanup() - Cleanup health scanning subsystem
 */
void dm_remap_health_v4_cleanup(void)
{
    if (dm_remap_health_wq) {
        destroy_workqueue(dm_remap_health_wq);
        dm_remap_health_wq = NULL;
    }
    
    DMR_DEBUG(1, "Health scanning subsystem cleaned up");
}

/**
 * calculate_health_score() - Calculate device health score (0-100)
 */
static uint8_t calculate_health_score(struct dm_remap_health_data_v4 *health_data)
{
    uint64_t total_sectors = health_data->total_sectors_scanned;
    uint64_t error_sectors = health_data->error_sectors_found;
    uint64_t warning_sectors = health_data->warning_sectors_found;
    uint32_t base_score = 100;
    
    if (total_sectors == 0) {
        return 100; /* No data yet - assume healthy */
    }
    
    /* Error rate impact (major) */
    if (error_sectors > 0) {
        uint64_t error_rate = (error_sectors * 10000) / total_sectors; /* Per 10K sectors */
        if (error_rate > 100) {
            base_score = 0; /* Critical: >1% error rate */
        } else if (error_rate > 10) {
            base_score = 30; /* Poor: >0.1% error rate */
        } else if (error_rate > 1) {
            base_score = 70; /* Fair: >0.01% error rate */
        } else {
            base_score = 85; /* Good: <0.01% error rate */
        }
    }
    
    /* Warning rate impact (minor) */
    if (warning_sectors > 0) {
        uint64_t warning_rate = (warning_sectors * 10000) / total_sectors;
        if (warning_rate > 50) {
            base_score -= 15; /* Many warnings */
        } else if (warning_rate > 10) {
            base_score -= 10; /* Some warnings */
        } else {
            base_score -= 5; /* Few warnings */
        }
    }
    
    /* Age factor (very minor) */
    uint64_t days_since_scan = (ktime_get_real_seconds() - health_data->last_full_scan) / 86400;
    if (days_since_scan > 30) {
        base_score -= min(days_since_scan / 30, 5ULL); /* Reduce score for stale data */
    }
    
    return max(base_score, 0U);
}

/**
 * adaptive_scan_interval() - Calculate next scan interval based on health
 */
static unsigned long adaptive_scan_interval(struct dm_remap_device_v4 *device)
{
    uint8_t health_score = device->metadata.health_data.health_score;
    unsigned long base_interval = scan_interval_hours * 3600; /* Convert to seconds */
    
    /* Adjust scan frequency based on health */
    if (health_score < 30) {
        return base_interval / 8; /* Critical: scan 8x more frequently */
    } else if (health_score < 50) {
        return base_interval / 4; /* Poor: scan 4x more frequently */
    } else if (health_score < 70) {
        return base_interval / 2; /* Fair: scan 2x more frequently */
    } else if (health_score < 85) {
        return base_interval; /* Good: normal frequency */
    } else {
        return base_interval * 2; /* Excellent: scan half as often */
    }
}

/**
 * sector_health_test() - Test individual sector health
 */
static int sector_health_test(struct dm_remap_device_v4 *device, uint64_t sector,
                             uint8_t *health_score)
{
    struct bio *bio;
    struct block_device *bdev = device->main_dev;
    void *data;
    int ret = 0;
    ktime_t start_time, end_time;
    uint64_t latency_ns;
    
    /* Allocate test page */
    data = (void *)__get_free_page(GFP_KERNEL);
    if (!data) {
        return -ENOMEM;
    }
    
    /* Create read bio */
    bio = bio_alloc(bdev, 1, REQ_OP_READ | REQ_SYNC, GFP_KERNEL);
    if (!bio) {
        free_page((unsigned long)data);
        return -ENOMEM;
    }
    
    bio->bi_iter.bi_sector = sector;
    bio_add_page(bio, virt_to_page(data), PAGE_SIZE, 0);
    
    /* Time the read operation */
    start_time = ktime_get();
    ret = submit_bio_wait(bio);
    end_time = ktime_get();
    
    latency_ns = ktime_to_ns(ktime_sub(end_time, start_time));
    
    if (ret) {
        /* Read error - definite problem */
        *health_score = 0;
        DMR_DEBUG(2, "Sector %llu read error: %d", sector, ret);
    } else {
        /* Analyze read latency for early warning signs */
        uint64_t latency_us = latency_ns / 1000;
        
        if (latency_us > 100000) { /* >100ms - very slow */
            *health_score = 10;
            DMR_DEBUG(2, "Sector %llu very slow: %llu us", sector, latency_us);
        } else if (latency_us > 50000) { /* >50ms - slow */
            *health_score = 30;
            DMR_DEBUG(3, "Sector %llu slow: %llu us", sector, latency_us);
        } else if (latency_us > 20000) { /* >20ms - warning */
            *health_score = 60;
            DMR_DEBUG(3, "Sector %llu warning latency: %llu us", sector, latency_us);
        } else {
            *health_score = 100; /* Good performance */
        }
        
        /* Additional heuristic: check for data corruption patterns */
        uint32_t crc = crc32(0, data, PAGE_SIZE);
        if (crc == 0x00000000 || crc == 0xFFFFFFFF) {
            /* Suspicious - all zeros or all ones might indicate problem */
            *health_score = min(*health_score, (uint8_t)70);
            DMR_DEBUG(3, "Sector %llu suspicious data pattern: CRC=0x%08x", sector, crc);
        }
    }
    
    bio_put(bio);
    free_page((unsigned long)data);
    
    return ret;
}

/**
 * should_preemptive_remap() - Decide if sector should be preemptively remapped
 */
static bool should_preemptive_remap(uint8_t health_score, uint64_t sector)
{
    /* Remap sectors with health score below threshold */
    if (health_score < 20) {
        return true; /* Definitely remap very unhealthy sectors */
    }
    
    /* Use probabilistic remapping for marginal sectors to avoid cluster failures */
    if (health_score < 40) {
        /* 50% chance for marginal sectors */
        return (get_random_u32() % 100) < 50;
    }
    
    return false;
}

/**
 * scan_sector_range() - Scan a range of sectors for health issues
 */
static int scan_sector_range(struct dm_remap_device_v4 *device,
                           uint64_t start_sector, uint64_t end_sector)
{
    uint64_t sector;
    uint8_t health_score;
    int ret;
    int sectors_processed = 0, errors_found = 0, warnings_found = 0;
    int preemptive_remaps = 0;
    
    for (sector = start_sector; sector < end_sector; sector += 8) { /* Scan every 8th sector */
        /* Check if scanning should continue */
        if (!atomic_read(&device->device_active)) {
            break;
        }
        
        ret = sector_health_test(device, sector, &health_score);
        sectors_processed++;
        
        if (ret < 0) {
            /* Hard error - immediate remap needed */
            errors_found++;
            
            uint64_t spare_sector = device->metadata.remap_data.next_spare_sector;
            ret = dm_remap_add_remap_v4(device, sector, spare_sector,
                                      DM_REMAP_REASON_READ_ERROR);
            if (ret == 0) {
                DMR_DEBUG(1, "Emergency remap: sector %llu (read error)", sector);
                atomic64_inc(&total_preventive_remaps);
                preemptive_remaps++;
            }
        } else if (health_score < 50) {
            /* Potential problem - consider preemptive remap */
            warnings_found++;
            
            if (should_preemptive_remap(health_score, sector)) {
                uint64_t spare_sector = device->metadata.remap_data.next_spare_sector;
                ret = dm_remap_add_remap_v4(device, sector, spare_sector,
                                          DM_REMAP_REASON_PREVENTIVE);
                if (ret == 0) {
                    DMR_DEBUG(1, "Preemptive remap: sector %llu (health=%u)", 
                             sector, health_score);
                    atomic64_inc(&total_preventive_remaps);
                    preemptive_remaps++;
                }
            }
        }
        
        /* Yield CPU periodically to maintain <1% overhead */
        if (sectors_processed % 100 == 0) {
            cond_resched();
            usleep_range(100, 200); /* Brief pause */
        }
    }
    
    /* Update statistics */
    atomic64_add(sectors_processed, &device->metadata.health_data.total_sectors_scanned);
    atomic64_add(errors_found, &device->metadata.health_data.error_sectors_found);
    atomic64_add(warnings_found, &device->metadata.health_data.warning_sectors_found);
    atomic64_add(sectors_processed, &total_sectors_scanned);
    atomic64_add(errors_found, &total_errors_detected);
    
    DMR_DEBUG(2, "Scanned sectors %llu-%llu: processed=%d, errors=%d, warnings=%d, remaps=%d",
              start_sector, end_sector, sectors_processed, errors_found, 
              warnings_found, preemptive_remaps);
    
    return sectors_processed;
}

/**
 * background_scanner_work() - Main background scanning work function
 */
static void background_scanner_work(struct work_struct *work)
{
    struct dm_remap_scanner_v4 *scanner = container_of(work, 
                                                      struct dm_remap_scanner_v4,
                                                      scan_work.work);
    struct dm_remap_device_v4 *device = scanner->device;
    uint64_t device_sectors = get_capacity(device->main_dev->bd_disk);
    uint64_t scan_chunk_size = 1024; /* Scan 1024 sectors at a time */
    uint64_t start_sector = scanner->next_scan_sector;
    uint64_t end_sector = min(start_sector + scan_chunk_size, device_sectors);
    int sectors_scanned;
    unsigned long next_delay;
    
    if (!atomic_read(&device->device_active)) {
        return; /* Device is being destroyed */
    }
    
    DMR_DEBUG(3, "Background scan chunk: sectors %llu-%llu", start_sector, end_sector);
    
    /* Perform the scan */
    sectors_scanned = scan_sector_range(device, start_sector, end_sector);
    
    /* Update scan progress */
    scanner->next_scan_sector = end_sector;
    if (scanner->next_scan_sector >= device_sectors) {
        /* Full scan completed */
        scanner->next_scan_sector = 0;
        device->metadata.health_data.last_full_scan = ktime_get_real_seconds();
        device->metadata.health_data.scan_progress_percent = 100;
        atomic64_inc(&total_scans_completed);
        
        /* Recalculate health score */
        device->metadata.health_data.health_score = 
            calculate_health_score(&device->metadata.health_data);
        
        DMR_DEBUG(1, "Completed full scan: health=%u%%, sectors=%llu, errors=%llu, warnings=%llu",
                  device->metadata.health_data.health_score,
                  device->metadata.health_data.total_sectors_scanned,
                  device->metadata.health_data.error_sectors_found,
                  device->metadata.health_data.warning_sectors_found);
        
        /* Mark metadata for writing */
        device->metadata_dirty = true;
    } else {
        /* Update progress percentage */
        device->metadata.health_data.scan_progress_percent = 
            (scanner->next_scan_sector * 100) / device_sectors;
    }
    
    /* Schedule next scan chunk with adaptive interval */
    next_delay = adaptive_scan_interval(device) / (device_sectors / scan_chunk_size);
    next_delay = max(next_delay, 1UL); /* At least 1 second between chunks */
    
    if (atomic_read(&device->device_active)) {
        queue_delayed_work(dm_remap_health_wq, &scanner->scan_work, 
                          msecs_to_jiffies(next_delay * 1000));
    }
}

/**
 * dm_remap_scanner_init() - Initialize background scanner for device
 */
int dm_remap_scanner_init(struct dm_remap_scanner_v4 *scanner,
                         struct dm_remap_device_v4 *device)
{
    if (!scanner || !device) {
        return -EINVAL;
    }
    
    scanner->device = device;
    scanner->next_scan_sector = 0;
    atomic_set(&scanner->scanner_active, 0);
    INIT_DELAYED_WORK(&scanner->scan_work, background_scanner_work);
    
    DMR_DEBUG(2, "Initialized background scanner");
    return 0;
}

/**
 * dm_remap_scanner_start() - Start background scanning
 */
int dm_remap_scanner_start(struct dm_remap_scanner_v4 *scanner)
{
    if (!scanner || !dm_remap_health_wq) {
        return -EINVAL;
    }
    
    if (atomic_cmpxchg(&scanner->scanner_active, 0, 1) == 0) {
        unsigned long initial_delay = adaptive_scan_interval(scanner->device);
        
        queue_delayed_work(dm_remap_health_wq, &scanner->scan_work,
                          msecs_to_jiffies(initial_delay * 1000));
        
        DMR_DEBUG(1, "Started background scanner (delay=%lu sec)", initial_delay);
        return 0;
    }
    
    return -EALREADY; /* Already running */
}

/**
 * dm_remap_scanner_stop() - Stop background scanning
 */
void dm_remap_scanner_stop(struct dm_remap_scanner_v4 *scanner)
{
    if (!scanner) {
        return;
    }
    
    if (atomic_cmpxchg(&scanner->scanner_active, 1, 0) == 1) {
        cancel_delayed_work_sync(&scanner->scan_work);
        DMR_DEBUG(1, "Stopped background scanner");
    }
}

/**
 * dm_remap_scanner_cleanup() - Cleanup scanner resources
 */
void dm_remap_scanner_cleanup(struct dm_remap_scanner_v4 *scanner)
{
    if (!scanner) {
        return;
    }
    
    dm_remap_scanner_stop(scanner);
    /* scanner struct is embedded in device, no additional cleanup needed */
}

/**
 * dm_remap_trigger_immediate_scan() - Trigger immediate health scan
 */
int dm_remap_trigger_immediate_scan(struct dm_remap_device_v4 *device)
{
    if (!device || !atomic_read(&device->device_active)) {
        return -EINVAL;
    }
    
    /* Cancel existing delayed work and queue immediate scan */
    cancel_delayed_work(&device->scanner.scan_work);
    queue_delayed_work(dm_remap_health_wq, &device->scanner.scan_work, 0);
    
    DMR_DEBUG(1, "Triggered immediate health scan");
    return 0;
}

/**
 * dm_remap_get_health_stats() - Get health scanning statistics
 */
void dm_remap_get_health_stats(struct dm_remap_health_stats *stats)
{
    if (!stats) {
        return;
    }
    
    stats->total_scans_completed = atomic64_read(&total_scans_completed);
    stats->total_sectors_scanned = atomic64_read(&total_sectors_scanned);
    stats->total_errors_detected = atomic64_read(&total_errors_detected);
    stats->total_preventive_remaps = atomic64_read(&total_preventive_remaps);
}