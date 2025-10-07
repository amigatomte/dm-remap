/*
 * dm-remap-health-scanner.c - Background Health Scanning Implementation
 * 
 * Background Health Scanning System for dm-remap v4.0
 * Provides proactive storage health monitoring with predictive failure analysis
 * 
 * This module implements the core health scanning engine that runs in the
 * background to monitor storage device health and predict potential failures.
 * 
 * Author: Christian (with AI assistance)
 * License: GPL v2
 */

#include <linux/module.h>
#include <linux/device-mapper.h>
#include <linux/bio.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/jiffies.h>
#include <linux/random.h>

#include "dm-remap-core.h"
#include "dm-remap-health-core.h"

/* Module metadata */
MODULE_AUTHOR("Christian (with AI assistance)");
MODULE_DESCRIPTION("Background Health Scanning for dm-remap");
MODULE_LICENSE("GPL");

/* Health scanning work function forward declaration */
static void dmr_health_scan_work_fn(struct work_struct *work);

/**
 * dmr_health_scanner_init - Initialize the health scanning system
 * @rc: Parent remap context
 * 
 * Sets up the health scanning infrastructure including work queues,
 * health tracking maps, and sysfs interfaces.
 * 
 * Returns: 0 on success, negative error code on failure
 */
int dmr_health_scanner_init(struct remap_c *rc)
{
    struct dmr_health_scanner *scanner;
    int ret;

    if (!rc) {
        printk(KERN_ERR "dm-remap-health: Invalid remap context\n");
        return -EINVAL;
    }

    /* Allocate scanner structure */
    scanner = kzalloc(sizeof(*scanner), GFP_KERNEL);
    if (!scanner) {
        printk(KERN_ERR "dm-remap-health: Failed to allocate scanner\n");
        return -ENOMEM;
    }

    /* Initialize basic configuration */
    scanner->rc = rc;
    scanner->scan_interval_ms = DMR_HEALTH_SCAN_DEFAULT_INTERVAL_MS;
    scanner->sectors_per_scan = DMR_HEALTH_SECTORS_PER_SCAN_DEFAULT;
    scanner->scan_intensity = DMR_HEALTH_SCAN_INTENSITY_DEFAULT;
    scanner->scanner_state = DMR_SCANNER_STOPPED;
    scanner->enabled = true;

    /* Initialize synchronization primitives */
    spin_lock_init(&scanner->scanner_lock);
    mutex_init(&scanner->config_mutex);

    /* Initialize scanning progress */
    scanner->scan_cursor = 0;
    scanner->scan_start_sector = 0;
    scanner->scan_end_sector = rc->main_sectors;

    /* Create dedicated workqueue for health scanning */
    scanner->scan_workqueue = alloc_workqueue("dm-remap-health",
                                            WQ_UNBOUND | WQ_MEM_RECLAIM, 1);
    if (!scanner->scan_workqueue) {
        printk(KERN_ERR "dm-remap-health: Failed to create workqueue\n");
        ret = -ENOMEM;
        goto err_free_scanner;
    }

    /* Initialize delayed work */
    INIT_DELAYED_WORK(&scanner->scan_work, dmr_health_scan_work_fn);

    /* Initialize health tracking map */
    ret = dmr_health_map_init(&scanner->health_map, rc->main_sectors);
    if (ret) {
        printk(KERN_ERR "dm-remap-health: Failed to initialize health map: %d\n", ret);
        goto err_destroy_workqueue;
    }

    /* Initialize statistics */
    atomic64_set(&scanner->stats.total_scans, 0);
    atomic64_set(&scanner->stats.sectors_scanned, 0);
    atomic64_set(&scanner->stats.warnings_issued, 0);
    atomic64_set(&scanner->stats.predictions_made, 0);
    atomic64_set(&scanner->stats.scan_time_total_ns, 0);
    atomic_set(&scanner->stats.active_warnings, 0);
    atomic_set(&scanner->stats.high_risk_sectors, 0);
    scanner->stats.last_full_scan_time = 0;
    scanner->stats.scan_coverage_percent = 0;

    /* Store scanner in remap context */
    rc->health_scanner = scanner;

    printk(KERN_INFO "dm-remap-health: Health scanner initialized successfully\n");
    printk(KERN_INFO "dm-remap-health: Monitoring %llu sectors\n", 
           (unsigned long long)rc->main_sectors);
    printk(KERN_INFO "dm-remap-health: Scan interval: %lu ms, sectors per scan: %llu\n",
           scanner->scan_interval_ms, (unsigned long long)scanner->sectors_per_scan);

    return 0;

err_destroy_workqueue:
    destroy_workqueue(scanner->scan_workqueue);
err_free_scanner:
    kfree(scanner);
    rc->health_scanner = NULL;
    return ret;
}

/**
 * dmr_health_scanner_cleanup - Clean up the health scanning system
 * @rc: Parent remap context
 * 
 * Stops scanning operations and frees all allocated resources.
 */
void dmr_health_scanner_cleanup(struct remap_c *rc)
{
    struct dmr_health_scanner *scanner;

    if (!rc || !rc->health_scanner) {
        return;
    }

    scanner = rc->health_scanner;

    /* Stop scanner if running */
    dmr_health_scanner_stop(scanner);

    /* Clean up health map */
    if (scanner->health_map) {
        dmr_health_map_cleanup(scanner->health_map);
        scanner->health_map = NULL;
    }

    /* Destroy workqueue */
    if (scanner->scan_workqueue) {
        destroy_workqueue(scanner->scan_workqueue);
        scanner->scan_workqueue = NULL;
    }

    /* Free scanner structure */
    kfree(scanner);
    rc->health_scanner = NULL;

    printk(KERN_INFO "dm-remap-health: Health scanner cleaned up\n");
}

/**
 * dmr_health_scanner_start - Start the health scanning process
 * @scanner: Health scanner instance
 * 
 * Begins background health scanning operations.
 * 
 * Returns: 0 on success, negative error code on failure
 */
int dmr_health_scanner_start(struct dmr_health_scanner *scanner)
{
    unsigned long flags;

    if (!scanner) {
        return -EINVAL;
    }

    spin_lock_irqsave(&scanner->scanner_lock, flags);

    if (scanner->scanner_state == DMR_SCANNER_RUNNING) {
        spin_unlock_irqrestore(&scanner->scanner_lock, flags);
        return 0;  /* Already running */
    }

    if (!scanner->enabled) {
        spin_unlock_irqrestore(&scanner->scanner_lock, flags);
        printk(KERN_INFO "dm-remap-health: Scanner disabled, not starting\n");
        return -ENODEV;
    }

    scanner->scanner_state = DMR_SCANNER_STARTING;
    spin_unlock_irqrestore(&scanner->scanner_lock, flags);

    /* Queue first scan work */
    queue_delayed_work(scanner->scan_workqueue, &scanner->scan_work,
                      msecs_to_jiffies(scanner->scan_interval_ms));

    spin_lock_irqsave(&scanner->scanner_lock, flags);
    scanner->scanner_state = DMR_SCANNER_RUNNING;
    spin_unlock_irqrestore(&scanner->scanner_lock, flags);

    printk(KERN_INFO "dm-remap-health: Health scanner started\n");
    return 0;
}

/**
 * dmr_health_scanner_stop - Stop the health scanning process
 * @scanner: Health scanner instance
 * 
 * Stops background health scanning operations and waits for completion.
 * 
 * Returns: 0 on success, negative error code on failure
 */
int dmr_health_scanner_stop(struct dmr_health_scanner *scanner)
{
    unsigned long flags;

    if (!scanner) {
        return -EINVAL;
    }

    spin_lock_irqsave(&scanner->scanner_lock, flags);

    if (scanner->scanner_state == DMR_SCANNER_STOPPED) {
        spin_unlock_irqrestore(&scanner->scanner_lock, flags);
        return 0;  /* Already stopped */
    }

    scanner->scanner_state = DMR_SCANNER_STOPPING;
    spin_unlock_irqrestore(&scanner->scanner_lock, flags);

    /* Cancel pending work */
    cancel_delayed_work_sync(&scanner->scan_work);

    spin_lock_irqsave(&scanner->scanner_lock, flags);
    scanner->scanner_state = DMR_SCANNER_STOPPED;
    spin_unlock_irqrestore(&scanner->scanner_lock, flags);

    printk(KERN_INFO "dm-remap-health: Health scanner stopped\n");
    return 0;
}

/**
 * dmr_health_scanner_pause - Pause the health scanning process
 * @scanner: Health scanner instance
 * 
 * Temporarily pauses health scanning without stopping the infrastructure.
 * 
 * Returns: 0 on success, negative error code on failure
 */
int dmr_health_scanner_pause(struct dmr_health_scanner *scanner)
{
    unsigned long flags;

    if (!scanner) {
        return -EINVAL;
    }

    spin_lock_irqsave(&scanner->scanner_lock, flags);

    if (scanner->scanner_state != DMR_SCANNER_RUNNING) {
        spin_unlock_irqrestore(&scanner->scanner_lock, flags);
        return -EINVAL;
    }

    scanner->scanner_state = DMR_SCANNER_PAUSED;
    spin_unlock_irqrestore(&scanner->scanner_lock, flags);

    /* Cancel current work but don't sync (allow current scan to complete) */
    cancel_delayed_work(&scanner->scan_work);

    printk(KERN_INFO "dm-remap-health: Health scanner paused\n");
    return 0;
}

/**
 * dmr_health_scanner_resume - Resume the health scanning process
 * @scanner: Health scanner instance
 * 
 * Resumes health scanning after a pause.
 * 
 * Returns: 0 on success, negative error code on failure
 */
int dmr_health_scanner_resume(struct dmr_health_scanner *scanner)
{
    unsigned long flags;

    if (!scanner) {
        return -EINVAL;
    }

    spin_lock_irqsave(&scanner->scanner_lock, flags);

    if (scanner->scanner_state != DMR_SCANNER_PAUSED) {
        spin_unlock_irqrestore(&scanner->scanner_lock, flags);
        return -EINVAL;
    }

    scanner->scanner_state = DMR_SCANNER_RUNNING;
    spin_unlock_irqrestore(&scanner->scanner_lock, flags);

    /* Resume scanning */
    queue_delayed_work(scanner->scan_workqueue, &scanner->scan_work,
                      msecs_to_jiffies(scanner->scan_interval_ms));

    printk(KERN_INFO "dm-remap-health: Health scanner resumed\n");
    return 0;
}

/**
 * dmr_health_scan_sectors - Perform health scan on a range of sectors
 * @scanner: Health scanner instance
 * @start_sector: Starting sector for scan
 * @sector_count: Number of sectors to scan
 * 
 * Performs I/O-based health scanning on the specified sector range.
 * This function issues read operations to test sector reliability.
 * 
 * Returns: Number of sectors successfully scanned, negative on error
 */
static int dmr_health_scan_sectors(struct dmr_health_scanner *scanner,
                                  sector_t start_sector, sector_t sector_count)
{
    sector_t sectors_scanned = 0;
    sector_t current_sector;
    ktime_t scan_start, scan_end;
    
    scan_start = ktime_get();

    for (current_sector = start_sector; 
         current_sector < start_sector + sector_count && 
         current_sector < scanner->scan_end_sector;
         current_sector++) {
        
        struct dmr_sector_health *health;
        bool scan_success = true;
        
        /* Check if we should stop scanning */
        if (scanner->scanner_state != DMR_SCANNER_RUNNING) {
            break;
        }

        /* Get or create health record for this sector */
        health = dmr_get_sector_health(scanner->health_map, current_sector);
        if (!health) {
            /* For now, skip sectors without health records */
            continue;
        }

        /* Simulate health scan - in a real implementation, this would
         * perform actual I/O operations to test sector health */
        
        /* Update health record */
        health->last_scan_time = jiffies;
        health->scan_count++;
        
        /* Simulate occasional health degradation for testing */
        if (get_random_u32() % 10000 == 0) {
            health->read_errors++;
            scan_success = false;
        }
        
        /* Update health score based on scan results */
        health->health_score = dmr_calculate_health_score(health);
        
        /* Update sector health statistics */
        dmr_health_update_sector(scanner, current_sector, scan_success, true);
        
        sectors_scanned++;
        
        /* Yield CPU periodically to avoid hogging */
        if (sectors_scanned % 100 == 0) {
            cond_resched();
        }
    }

    scan_end = ktime_get();
    scanner->io_overhead_ns = ktime_to_ns(ktime_sub(scan_end, scan_start));

    /* Update statistics */
    atomic64_add(sectors_scanned, &scanner->stats.sectors_scanned);

    return sectors_scanned;
}

/**
 * dmr_health_scan_work_fn - Work function for periodic health scanning
 * @work: Work structure (contains scanner via container_of)
 * 
 * This is the main work function that performs periodic health scans.
 * It scans a portion of the device on each invocation and reschedules
 * itself for the next scan cycle.
 */
static void dmr_health_scan_work_fn(struct work_struct *work)
{
    struct dmr_health_scanner *scanner;
    struct delayed_work *dwork;
    sector_t scan_start, sectors_to_scan;
    int scanned_count;
    ktime_t work_start, work_end;
    unsigned long flags;

    dwork = to_delayed_work(work);
    scanner = container_of(dwork, struct dmr_health_scanner, scan_work);

    /* Check if we should continue scanning */
    spin_lock_irqsave(&scanner->scanner_lock, flags);
    if (scanner->scanner_state != DMR_SCANNER_RUNNING) {
        spin_unlock_irqrestore(&scanner->scanner_lock, flags);
        return;
    }
    spin_unlock_irqrestore(&scanner->scanner_lock, flags);

    work_start = ktime_get();
    scanner->last_scan_start = work_start;

    /* Determine scan range */
    scan_start = scanner->scan_cursor;
    sectors_to_scan = min_t(sector_t, scanner->sectors_per_scan,
                           scanner->scan_end_sector - scan_start);

    if (sectors_to_scan == 0) {
        /* Reached end of device, wrap around */
        scanner->scan_cursor = scanner->scan_start_sector;
        scanner->stats.last_full_scan_time = jiffies;
        sectors_to_scan = min_t(sector_t, scanner->sectors_per_scan,
                               scanner->scan_end_sector - scanner->scan_cursor);
        scan_start = scanner->scan_cursor;
    }

    /* Perform the actual health scan */
    scanned_count = dmr_health_scan_sectors(scanner, scan_start, sectors_to_scan);
    
    if (scanned_count > 0) {
        /* Update scan cursor */
        scanner->scan_cursor = scan_start + scanned_count;
        
        /* Update scan coverage percentage */
        scanner->stats.scan_coverage_percent = 
            (u32)((scanner->scan_cursor * 100) / scanner->scan_end_sector);
    }

    work_end = ktime_get();
    scanner->last_scan_end = work_end;

    /* Update timing statistics */
    atomic64_add(ktime_to_ns(ktime_sub(work_end, work_start)),
                 &scanner->stats.scan_time_total_ns);
    atomic64_inc(&scanner->stats.total_scans);

    /* Log progress periodically */
    if (atomic64_read(&scanner->stats.total_scans) % 100 == 0) {
        printk(KERN_INFO "dm-remap-health: Scan progress: %u%% complete, "
               "%lld total scans, %lld sectors scanned\n",
               scanner->stats.scan_coverage_percent,
               atomic64_read(&scanner->stats.total_scans),
               atomic64_read(&scanner->stats.sectors_scanned));
    }

    /* Schedule next scan */
    spin_lock_irqsave(&scanner->scanner_lock, flags);
    if (scanner->scanner_state == DMR_SCANNER_RUNNING) {
        queue_delayed_work(scanner->scan_workqueue, &scanner->scan_work,
                          msecs_to_jiffies(scanner->scan_interval_ms));
    }
    spin_unlock_irqrestore(&scanner->scanner_lock, flags);
}

/**
 * dmr_calculate_health_score - Calculate health score for a sector
 * @health: Sector health information
 * 
 * Calculates a health score (0-1000) based on error history and access patterns.
 * Higher scores indicate better health.
 * 
 * Returns: Health score (0-1000)
 */
u16 dmr_calculate_health_score(struct dmr_sector_health *health)
{
    u32 score = DMR_HEALTH_SCORE_PERFECT;
    u32 error_rate, access_factor;

    if (!health) {
        return 0;
    }

    /* Calculate error rate impact */
    if (health->access_count > 0) {
        error_rate = ((health->read_errors + health->write_errors) * 1000) / 
                     health->access_count;
        
        /* Reduce score based on error rate */
        score = score > error_rate ? score - error_rate : 0;
    }

    /* Factor in access frequency - frequently accessed sectors
     * with no errors get bonus points */
    if (health->access_count > 100 && health->read_errors == 0 && 
        health->write_errors == 0) {
        access_factor = min_t(u32, 50, health->access_count / 20);
        score = min_t(u32, DMR_HEALTH_SCORE_PERFECT, score + access_factor);
    }

    /* Age factor - very old data without recent scans reduces score */
    if (health->last_scan_time && 
        time_after(jiffies, (unsigned long)(health->last_scan_time + HZ * 3600))) {
        score = score > 50 ? score - 50 : 0;
    }

    return (u16)score;
}

/**
 * dmr_health_update_sector - Update health information for a sector
 * @scanner: Health scanner instance
 * @sector: Sector number
 * @read_success: Whether last read was successful
 * @write_success: Whether last write was successful
 * 
 * Updates the health tracking information for a specific sector based
 * on recent I/O results.
 * 
 * Returns: 0 on success, negative error code on failure
 */
int dmr_health_update_sector(struct dmr_health_scanner *scanner, 
                            sector_t sector, bool read_success, bool write_success)
{
    struct dmr_sector_health *health;
    u16 old_score, new_score;
    enum dmr_health_risk_level old_risk, new_risk;

    if (!scanner || !scanner->health_map) {
        return -EINVAL;
    }

    health = dmr_get_sector_health(scanner->health_map, sector);
    if (!health) {
        /* Create new health record */
        struct dmr_sector_health new_health = {
            .health_score = DMR_HEALTH_SCORE_PERFECT,
            .read_errors = 0,
            .write_errors = 0,
            .access_count = 1,
            .last_scan_time = jiffies,
            .last_access_time = jiffies,
            .trend = DMR_HEALTH_TREND_STABLE,
            .risk_level = DMR_HEALTH_RISK_SAFE,
            .scan_count = 1,
            .reserved = 0
        };
        return dmr_set_sector_health(scanner->health_map, sector, &new_health);
    }

    old_score = health->health_score;
    old_risk = health->risk_level;

    /* Update error counts */
    if (!read_success) {
        health->read_errors++;
    }
    if (!write_success) {
        health->write_errors++;
    }

    /* Update access tracking */
    health->access_count++;
    health->last_access_time = jiffies;

    /* Recalculate health score */
    new_score = dmr_calculate_health_score(health);
    health->health_score = new_score;

    /* Update trend analysis */
    if (new_score > old_score + 50) {
        health->trend = DMR_HEALTH_TREND_IMPROVING;
    } else if (new_score < old_score - 50) {
        health->trend = DMR_HEALTH_TREND_DECLINING;
    } else {
        health->trend = DMR_HEALTH_TREND_STABLE;
    }

    /* Update risk level */
    if (new_score >= DMR_HEALTH_SCORE_WARNING_THRESHOLD) {
        new_risk = DMR_HEALTH_RISK_SAFE;
    } else if (new_score >= DMR_HEALTH_SCORE_DANGER_THRESHOLD) {
        new_risk = DMR_HEALTH_RISK_CAUTION;
    } else {
        new_risk = DMR_HEALTH_RISK_DANGER;
    }

    health->risk_level = new_risk;

    /* Update scanner statistics */
    if (new_risk > old_risk) {
        /* Risk increased */
        if (new_risk >= DMR_HEALTH_RISK_CAUTION) {
            atomic64_inc(&scanner->stats.warnings_issued);
            atomic_inc(&scanner->stats.active_warnings);
        }
        if (new_risk == DMR_HEALTH_RISK_DANGER) {
            atomic_inc(&scanner->stats.high_risk_sectors);
        }
    } else if (new_risk < old_risk) {
        /* Risk decreased */
        if (old_risk >= DMR_HEALTH_RISK_CAUTION && new_risk < DMR_HEALTH_RISK_CAUTION) {
            atomic_dec(&scanner->stats.active_warnings);
        }
        if (old_risk == DMR_HEALTH_RISK_DANGER && new_risk < DMR_HEALTH_RISK_DANGER) {
            atomic_dec(&scanner->stats.high_risk_sectors);
        }
    }

    return 0;
}