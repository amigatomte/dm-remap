/*
 * dm-remap v2.0 - Device Mapper target for bad sector remapping
 * 
 * This module remaps bad sectors from a primary device to spare sectors 
 * on a separate block device. v2.0 adds intelligent error detection,
 * automatic remapping, and comprehensive health monitoring.
 *
 * Key v2.0 features:
 * - Automatic bad sector detection from I/O errors
 * - Intelligent retry logic with exponential backoff
 * - Proactive remapping based on error patterns
 * - Health assessment and monitoring
 * - Enhanced statistics and reporting
 * 
 * Author: Christian (with AI assistance)
 * License: GPL v2
 */

#include <linux/module.h>        // Core kernel module support
#include <linux/moduleparam.h>   // Module parameter support
#include <linux/types.h>         // Basic kernel types
#include "dm-remap-core.h"       // Core data structures and debug macros
#include "dm-remap-messages.h"   // Debug macros and messaging support
#include "dm-remap-sysfs.h"      // Sysfs interface declarations
#include "dm_remap_reservation.h" // v4.0 Reservation system
#include "dm-remap-performance.h" // v4.0 Performance optimizations
#include "dm-remap-memory-pool.h" // Week 9-10 Memory optimization
#include "dm-remap-hotpath-optimization.h" // Week 9-10 Hotpath optimization
#include "dm-remap-hotpath-sysfs.h" // Week 9-10 Hotpath sysfs interface
#include <linux/device-mapper.h> // Device mapper framework
#include <linux/bio.h>           // Block I/O structures
#include <linux/init.h>          // Module initialization
#include "dm-remap-production.h" // Production hardening features
#include <linux/kernel.h>        // Kernel utilities  
#include <linux/vmalloc.h>       // Virtual memory allocation
#include <linux/string.h>        // String functions
#include <linux/time64.h>        // Time functions for v2.0 timestamping
#include <linux/delay.h>         // Sleep functions (msleep)
#include "dm-remap-io.h"         // I/O processing functions
#include "dm-remap-error.h"      // Error handling functions
#include "dm-remap-debug.h"      // Debug interface for testing
#include "dm-remap-metadata.h"   // v3.0 metadata persistence system
#include "dm-remap-health-core.h" // Week 7-8: Background health scanning
#include "dm-remap-performance-profiler.h" // Phase 3: Advanced performance profiling
#include "dm-remap-performance-sysfs.h" // Phase 3: Performance profiler sysfs interface
#include "dm-remap-performance-optimization.h" // Phase 3.2B: Performance optimization
#include "dm-remap-io-optimized.h" // Phase 3.2B: Optimized I/O processing
#include "dm-remap-optimization-sysfs.h" // Phase 3.2B: Optimization monitoring

/*
 * Module parameters - configurable via modprobe or /sys/module/
 */
int debug_level = 1;             // Debug verbosity: 0=quiet, 1=info, 2=debug
int max_remaps = 1000;           // Maximum remappable sectors per target
int error_threshold = 3;         // Default error threshold for auto-remap
int auto_remap_enabled = 0;      // Enable automatic remapping (disabled by default)
unsigned int global_write_errors = 0;     // Global write error counter for testing
unsigned int global_read_errors = 0;      // Global read error counter for testing  
unsigned int global_auto_remaps = 0;      // Global auto-remap counter for testing

/* Module parameter registration */
module_param(debug_level, int, 0644);
MODULE_PARM_DESC(debug_level, "Debug verbosity level (0=quiet, 1=info, 2=debug)");

// Maximum number of sectors that can be remapped per target instance
module_param(max_remaps, int, 0644);
MODULE_PARM_DESC(max_remaps, "Maximum number of remappable sectors per target");

// Error detection and auto-remap parameters
module_param(error_threshold, int, 0644);
MODULE_PARM_DESC(error_threshold, "Number of errors before auto-remap is triggered");

module_param(auto_remap_enabled, int, 0644);
MODULE_PARM_DESC(auto_remap_enabled, "Enable automatic remapping on errors (0=disabled, 1=enabled)");

module_param(global_write_errors, uint, 0444);
MODULE_PARM_DESC(global_write_errors, "Total write errors detected (read-only)");

module_param(global_read_errors, uint, 0444);
MODULE_PARM_DESC(global_read_errors, "Total read errors detected (read-only)");

module_param(global_auto_remaps, uint, 0444);
MODULE_PARM_DESC(global_auto_remaps, "Total automatic remaps performed (read-only)");

/* Phase 3.2A: Performance Dashboard Parameters */
static unsigned int perf_total_ios = 0;
static unsigned int perf_avg_latency_ns = 0;
static unsigned int perf_total_mb = 0;
static unsigned int perf_cache_hits = 0;
static unsigned int perf_cache_misses = 0;

module_param(perf_total_ios, uint, 0444);
MODULE_PARM_DESC(perf_total_ios, "Total I/O operations processed (read-only)");

module_param(perf_avg_latency_ns, uint, 0444);
MODULE_PARM_DESC(perf_avg_latency_ns, "Average I/O latency in nanoseconds (read-only)");

module_param(perf_total_mb, uint, 0444);
MODULE_PARM_DESC(perf_total_mb, "Total megabytes processed (read-only)");

module_param(perf_cache_hits, uint, 0444);
MODULE_PARM_DESC(perf_cache_hits, "Performance cache hits (read-only)");

module_param(perf_cache_misses, uint, 0444);
MODULE_PARM_DESC(perf_cache_misses, "Performance cache misses (read-only)");

/* Phase 3.2A: Performance Update Functions */
void dmr_perf_update_stats(unsigned int ios, unsigned int latency_ns, unsigned int bytes, 
                          unsigned int cache_hit, unsigned int cache_miss)
{
    perf_total_ios += ios;
    if (latency_ns > 0)
        perf_avg_latency_ns = (perf_avg_latency_ns + latency_ns) / 2;  /* Simple rolling average */
    perf_total_mb += bytes / (1024 * 1024);
    perf_cache_hits += cache_hit;
    perf_cache_misses += cache_miss;
}

/* 
 * EXPORTED VARIABLES 
 * These are accessible from other source files in the dm-remap module
 */
// DMR_DEBUG is now defined in dm-remap-core.h

// DEVICE MAPPER TARGET FUNCTIONS
// These are called by the device mapper framework to manage our target.
// Each function handles a specific aspect of target lifecycle and operation.

// remap_map is now in dm-remap-io.c as dmr_enhanced_map()
// Message handling is now in dm-remap-messages.c

// Reports status via dmsetup status
static void remap_status(struct dm_target *ti, status_type_t type,
                         // remap_status: Reports status via dmsetup status.
                         // Shows number of remapped sectors, lost sectors, and spare usage.
                         // This function is used by dmsetup to report target status.
                         unsigned status_flags, char *result, unsigned maxlen)
{
    struct remap_c *rc = ti->private;
    int lost = 0, i, remapped = 0;

    // Count remapped and lost sectors
    for (i = 0; i < rc->spare_used; i++) {
        if (rc->table[i].main_lba != (sector_t)-1) {
            remapped++;
        } else {
            lost++;
        }
    }

    switch (type) {
    case STATUSTYPE_INFO:
        // v3.0 Enhanced status with metadata information
        if (rc->metadata) {
            u64 successful, failed;
            bool active;
            dm_remap_recovery_get_stats(rc, &successful, &failed, &active);
            
            scnprintf(result, maxlen, 
                     "v3.0 %d/%llu %d/%llu %llu/%llu health=%u errors=W%u:R%u auto_remaps=%u manual_remaps=%u scan=%u%% metadata=enabled autosave=%s saves=%llu/%llu",
                     remapped, (unsigned long long)rc->spare_len,
                     lost, (unsigned long long)rc->spare_len, 
                     (unsigned long long)rc->spare_used, (unsigned long long)rc->spare_len,
                     rc->overall_health,
                     rc->write_errors, rc->read_errors,
                     rc->auto_remaps, rc->manual_remaps,
                     rc->scan_progress,
                     active ? "active" : "inactive",
                     successful, failed);
        } else {
            // Fallback for targets without metadata
            scnprintf(result, maxlen, 
                     "v3.0 %d/%llu %d/%llu %llu/%llu health=%u errors=W%u:R%u auto_remaps=%u manual_remaps=%u scan=%u%% metadata=disabled",
                     remapped, (unsigned long long)rc->spare_len,
                     lost, (unsigned long long)rc->spare_len, 
                     (unsigned long long)rc->spare_used, (unsigned long long)rc->spare_len,
                     rc->overall_health,
                     rc->write_errors, rc->read_errors,
                     rc->auto_remaps, rc->manual_remaps,
                     rc->scan_progress);
        }
        break;
        
    case STATUSTYPE_TABLE:
        // Format: <main_dev> <spare_dev> <spare_start> <spare_len>
        scnprintf(result, maxlen, "%s %s %llu %llu", 
                 rc->main_dev->name, rc->spare_dev->name,
                 (unsigned long long)rc->spare_start, (unsigned long long)rc->spare_len);
        break;
        
    default:
        scnprintf(result, maxlen, "unknown status type %u", type);
        break;
    }
}

// Parses target construction arguments and initializes the target
static int remap_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
    struct remap_c *rc;
    unsigned long long tmp;
    int ret;
    sector_t dev_size;
    char target_name[256];

    pr_info("dm-remap: v2.0 Constructor called with %u args\n", argc);

    /* Validate argument count */
    if (argc != 4) {
        ti->error = "Invalid argument count, need: <main_dev> <spare_dev> <spare_start> <spare_len>";
        return -EINVAL;
    }

    /* Allocate context structure */
    rc = kzalloc(sizeof(*rc), GFP_KERNEL);
    if (!rc) {
        ti->error = "Cannot allocate remap context";
        return -ENOMEM;
    }

    /* Get main device */
    ret = dm_get_device(ti, argv[0], dm_table_get_mode(ti->table), &rc->main_dev);
    if (ret) {
        ti->error = "Main device lookup failed";
        goto bad;
    }

    /* Get spare device */  
    ret = dm_get_device(ti, argv[1], dm_table_get_mode(ti->table), &rc->spare_dev);
    if (ret) {
        ti->error = "Spare device lookup failed";
        goto bad;
    }

    /* Parse spare start sector */
    ret = kstrtoull(argv[2], 10, &tmp);
    if (ret) {
        ti->error = "Invalid spare start sector";
        goto bad;
    }
    rc->spare_start = tmp;

    /* Parse spare length */
    ret = kstrtoull(argv[3], 10, &tmp);
    if (ret) {
        ti->error = "Invalid spare length";
        goto bad;
    }
    rc->spare_len = tmp;

    /* Initialize spare usage tracking */
    rc->spare_used = 0;
    rc->health_entries = 0;
    
    /* Initialize sysfs tracking fields */
    rc->sysfs_created = false;
    rc->hotpath_sysfs_created = false;
    
    /* Initialize kobject for sysfs interface - will be set up later */
    memset(&rc->kobj, 0, sizeof(rc->kobj));
    
    /* Initialize auto-save tracking field */
    rc->autosave_started = false;
    
    /* Initialize health scanner tracking field */
    rc->health_scanner_started = false;
    
    /* Initialize I/O optimization tracking fields */
    rc->memory_pool_started = false;
    rc->hotpath_optimization_started = false;
    
    /* Initialize production hardening context */
    rc->prod_ctx = kzalloc(sizeof(struct dmr_production_context), GFP_KERNEL);
    if (rc->prod_ctx) {
        ret = dmr_production_init(rc->prod_ctx);
        if (ret != 0) {
            DMR_DEBUG(0, "Production hardening initialization failed: %d", ret);
            kfree(rc->prod_ctx);
            rc->prod_ctx = NULL;
        }
    } else {
        DMR_DEBUG(0, "Failed to allocate production context");
    }

    /* Initialize v2.0 fields */
    rc->write_errors = 0;
    rc->read_errors = 0;
    rc->auto_remaps = 0;
    rc->manual_remaps = 0;
    rc->scan_progress = 0;
    rc->last_scan_time = 0;
    rc->overall_health = DMR_HEALTH_GOOD;
    rc->auto_remap_enabled = auto_remap_enabled;  /* Use global parameter */
    rc->background_scan = false;
    rc->error_threshold = error_threshold;  /* Use global parameter */
    
    /* Initialize v3.0 metadata system */
    rc->metadata = NULL;  /* Will be initialized after device validation */
    
    /* Initialize v4.0 reservation system */
    ret = dmr_init_reservation_system(rc);
    if (ret) {
        ti->error = "Failed to initialize reservation system";
        goto bad;
    }
    
    /* Initialize v4.0 performance optimization cache */
    ret = dmr_init_allocation_cache(rc);
    if (ret) {
        ti->error = "Failed to initialize allocation cache";
        goto bad;
    }
    
    /* Set up dynamic metadata reservations */
    ret = dmr_setup_dynamic_metadata_reservations(rc);
    if (ret && ret != -ENOSPC) {
        ti->error = "Failed to set up metadata reservations";
        goto bad;
    }
    if (ret == -ENOSPC) {
        printk(KERN_WARNING "dm-remap: Spare device too small for optimal metadata placement\n");
    }

    /* Enforce module parameter limits */
    if (rc->spare_len > max_remaps) {
        DMR_DEBUG(0, "Limiting spare_len from %llu to %d (max_remaps parameter)", 
                  (unsigned long long)rc->spare_len, max_remaps);
        rc->spare_len = max_remaps;
    }

    /* Validate spare area fits in spare device */
    dev_size = bdev_nr_sectors(rc->spare_dev->bdev);
    if (rc->spare_start + rc->spare_len > dev_size) {
        ti->error = "Spare area exceeds device size";
        goto bad;
    }

    DMR_DEBUG(0, "Constructor: main_dev=%s, spare_dev=%s, spare_start=%llu, spare_len=%llu", 
              rc->main_dev->name, rc->spare_dev->name,
              (unsigned long long)rc->spare_start, (unsigned long long)rc->spare_len);

    /* Allocate remap table */
    rc->table = kcalloc(rc->spare_len, sizeof(struct remap_entry), GFP_KERNEL);
    if (!rc->table) {
        ti->error = "Cannot allocate remap table";
        goto bad;
    }

    /* Set up target */
    ti->private = rc;
    ti->num_flush_bios = 1;      /* Forward flush requests */
    ti->num_discard_bios = 1;    /* Forward discard requests */

    /* Initialize v2.0 I/O processing subsystem */
    ret = dmr_io_init();
    if (ret) {
        ti->error = "Failed to initialize I/O subsystem";
        goto bad;
    }

    /* Create sysfs interface for this target with enhanced error handling */
    snprintf(target_name, sizeof(target_name), "%s", dm_table_device_name(ti->table));
    ret = dmr_sysfs_create_target(rc, target_name);
    if (ret) {
        DMR_DEBUG(0, "Failed to create sysfs interface for target: %d", ret);
        /* Continue without sysfs - not a fatal error, but mark for cleanup */
        rc->sysfs_created = false;
    } else {
        rc->sysfs_created = true;
        DMR_DEBUG(1, "Sysfs interface created successfully for target: %s", target_name);
    }

    /* Create hotpath sysfs interface if hotpath optimization is enabled */
    if (rc->hotpath_manager) {
        ret = dmr_hotpath_sysfs_create(rc);
        if (ret) {
            DMR_DEBUG(0, "Failed to create hotpath sysfs interface: %d", ret);
            /* Continue without hotpath sysfs - not a fatal error */
            rc->hotpath_sysfs_created = false;
        } else {
            rc->hotpath_sysfs_created = true;
            DMR_DEBUG(1, "Hotpath sysfs interface created successfully");
        }
    } else {
        rc->hotpath_sysfs_created = false;
    }

    /* Create performance sysfs interface (Phase 3) */
    ret = dmr_perf_sysfs_create(rc);
    if (ret) {
        DMR_DEBUG(0, "Failed to create performance sysfs interface: %d", ret);
        /* Continue without performance sysfs - not a fatal error */
    }

    /* Create debug interface for testing */
    ret = dmr_debug_add_target(rc, target_name);
    if (ret) {
        DMR_DEBUG(0, "Failed to create debug interface for target: %d", ret);
        /* Continue without debug - not a fatal error */
    }
    
    /* Initialize main device sector count for health scanning */
    rc->main_sectors = bdev_nr_sectors(rc->main_dev->bdev);
    
    /* Initialize v3.0 metadata system */
    rc->metadata = dm_remap_metadata_create(rc->spare_dev->bdev, 
                                           rc->main_sectors,
                                           bdev_nr_sectors(rc->spare_dev->bdev));
    if (!rc->metadata) {
        DMR_DEBUG(0, "Failed to create metadata context - continuing without persistence");
        /* Continue without metadata - not a fatal error in v3.0 development */
    } else {
        /* Try to read existing metadata */
        enum dm_remap_metadata_result result = dm_remap_metadata_read(rc->metadata);
        if (result == DM_REMAP_META_SUCCESS) {
            DMR_DEBUG(0, "Successfully restored metadata from spare device");
            /* Restore remap table from metadata */
            int restored = dm_remap_recovery_restore_table(rc);
            if (restored > 0) {
                DMR_DEBUG(0, "Restored %d remap entries from persistent storage", restored);
            }
        } else if (result == DM_REMAP_META_ERROR_MAGIC) {
            DMR_DEBUG(0, "No existing metadata found - starting with clean state");
        } else {
            DMR_DEBUG(0, "Metadata read failed (%d) - starting with clean state", result);
        }
        
        /* Start auto-save system with enhanced safety measures */
        if (rc->metadata) {
            dm_remap_autosave_start(rc->metadata);
            rc->autosave_started = true;
            DMR_DEBUG(1, "Auto-save system started successfully with enhanced safety");
        } else {
            rc->autosave_started = false;
            DMR_DEBUG(0, "Auto-save not started - no metadata context available");
        }
    }

    /* Initialize Week 9-10: Memory Pool System for Optimization */
    ret = dmr_pool_manager_init(rc);
    if (ret) {
        DMR_DEBUG(0, "Failed to initialize memory pool system: %d", ret);
        rc->pool_manager = NULL;
    } else {
        /* v4.0 Enhanced Safety: Track successful memory pool startup */
        rc->memory_pool_started = true;
        DMR_DEBUG(0, "Memory pool system initialized successfully with enhanced safety");
    }

    /* Initialize Week 9-10: Hotpath Performance Optimization */
    ret = dmr_hotpath_init(rc);
    if (ret) {
        DMR_DEBUG(0, "Failed to initialize hotpath optimization: %d", ret);
        rc->hotpath_manager = NULL;
    } else {
        /* v4.0 Enhanced Safety: Allow stabilization before full activation */
        msleep(500);  /* 0.5-second delay for optimization stabilization */
        
        /* Track successful hotpath optimization startup */
        rc->hotpath_optimization_started = true;
        DMR_DEBUG(0, "Hotpath optimization initialized successfully with enhanced safety measures");
    }

    /* Initialize Week 7-8: Background Health Scanning System */
    ret = dmr_health_scanner_init(rc);
    if (ret) {
        DMR_DEBUG(0, "Failed to initialize health scanner: %d", ret);
        /* Continue without health scanning - not a fatal error */
        rc->health_scanner = NULL; 
    } else {
        DMR_DEBUG(0, "Background health scanner initialized successfully");
        
        /* v4.0 Enhanced Auto-start with Safety Measures */
        if (rc->health_scanner) {
            /* Allow system stabilization before scanner startup */
            msleep(1000);  /* 1-second delay for device stabilization */
            
            ret = dmr_health_scanner_start(rc->health_scanner);
            if (ret == 0) {
                rc->health_scanner_started = true;
                DMR_DEBUG(0, "Background health scanning started successfully with safety measures");
            } else {
                DMR_DEBUG(0, "Failed to start health scanner: %d (continuing without health scanning)", ret);
                /* Continue without health scanning - device is still functional */
            }
        }
    }

    /* Initialize Phase 3: Advanced Performance Profiler */
    ret = dmr_perf_profiler_init(&rc->perf_profiler);
    if (ret) {
        DMR_DEBUG(0, "Failed to initialize performance profiler: %d", ret);
        rc->perf_profiler = NULL;
    } else {
        DMR_DEBUG(0, "Advanced performance profiler initialized successfully");
    }

    pr_info("dm-remap: v4.0 target created successfully (metadata: %s, health: %s, I/O-opt: %s, profiler: %s)\n",
            rc->metadata ? "enabled" : "disabled",
            rc->health_scanner_started ? "enabled" : "disabled",
            (rc->memory_pool_started && rc->hotpath_optimization_started) ? "enabled" : "partial/disabled",
            rc->perf_profiler ? "enabled" : "disabled");
    return 0;

bad:
    if (rc->table)
        kfree(rc->table);
    if (rc->spare_dev)
        dm_put_device(ti, rc->spare_dev);
    if (rc->main_dev)
        dm_put_device(ti, rc->main_dev);
    kfree(rc);
    return -EINVAL;
}

// Destructor - cleans up resources when target is removed
static void remap_dtr(struct dm_target *ti)
{
    struct remap_c *rc = ti->private;

    pr_info("dm-remap: v2.0 Destructor called\n");

    /* Remove sysfs interface if it was successfully created */
    if (rc->sysfs_created) {
        dmr_sysfs_remove_target(rc);
        DMR_DEBUG(1, "Sysfs interface removed successfully");
    }

    /* Remove hotpath sysfs interface if it was successfully created */
    if (rc->hotpath_sysfs_created) {
        dmr_hotpath_sysfs_remove(rc);
        DMR_DEBUG(1, "Hotpath sysfs interface removed successfully");
    }

    /* Remove performance sysfs interface (Phase 3) */
    dmr_perf_sysfs_remove(rc);
    DMR_DEBUG(1, "Performance sysfs interface removed successfully");

    /* Remove debug interface */
    dmr_debug_remove_target(rc);
    
    /* Cleanup v4.0 performance optimizations */
    dmr_cleanup_allocation_cache(rc);
    
    /* Cleanup v4.0 reservation system */
    dmr_cleanup_reservation_system(rc);
    
    /* Cleanup Week 7-8: Background Health Scanning System */
    if (rc->health_scanner) {
        /* Stop health scanner if it was successfully started */
        if (rc->health_scanner_started) {
            dmr_health_scanner_stop(rc->health_scanner);
            DMR_DEBUG(1, "Health scanner stopped successfully");
        }
        
        /* Clean up health scanner resources */
        dmr_health_scanner_cleanup(rc);
        pr_info("dm-remap: cleaned up health scanning system\n");
    }
    
    /* Cleanup Week 9-10: Hotpath Optimization */
    if (rc->hotpath_manager) {
        /* v4.0 Enhanced Safety: Stop hotpath optimization if it was successfully started */
        if (rc->hotpath_optimization_started) {
            /* Allow graceful shutdown of optimization processes */
            msleep(100);  /* Brief delay for optimization cleanup */
            DMR_DEBUG(1, "Hotpath optimization stopped successfully");
        }
        
        dmr_hotpath_cleanup(rc);
        pr_info("dm-remap: cleaned up hotpath optimization\n");
    }
    
    /* Cleanup Week 9-10: Memory Pool System */
    if (rc->pool_manager) {
        /* v4.0 Enhanced Safety: Clean up memory pools if they were successfully started */
        if (rc->memory_pool_started) {
            DMR_DEBUG(1, "Memory pool system stopped successfully");
        }
        
        dmr_pool_manager_cleanup(rc);
        pr_info("dm-remap: cleaned up memory pool system\n");
    }
    
    /* Cleanup Phase 3: Advanced Performance Profiler */
    if (rc->perf_profiler) {
        dmr_perf_profiler_cleanup(rc->perf_profiler);
        pr_info("dm-remap: cleaned up performance profiler\n");
    }
    
    /* Cleanup v3.0 metadata system with enhanced auto-save handling */
    if (rc->metadata) {
        /* Force final save before shutdown if auto-save was started */
        if (rc->autosave_started) {
            dm_remap_autosave_force(rc->metadata);
            DMR_DEBUG(1, "Final auto-save completed before shutdown");
        }
        /* Clean up metadata context */
        dm_remap_metadata_destroy(rc->metadata);
        pr_info("dm-remap: cleaned up metadata system\n");
    }

    /* Free remap table */
    if (rc->table) {
        kfree(rc->table);
        pr_info("dm-remap: freed remap table\n");
    }

    /* Release devices */
    if (rc->spare_dev) {
        dm_put_device(ti, rc->spare_dev);
        pr_info("dm-remap: released spare device\n");
    }
    
    if (rc->main_dev) {
        dm_put_device(ti, rc->main_dev);
        pr_info("dm-remap: released main device\n");
    }

    /* Cleanup v2.0 I/O processing subsystem */
    dmr_io_exit();
    
    /* Cleanup production hardening */
    if (rc->prod_ctx) {
        dmr_production_cleanup(rc->prod_ctx);
        kfree(rc->prod_ctx);
        pr_info("dm-remap: cleaned up production context\n");
    }
    
    /* Free context structure */
    kfree(rc);
    pr_info("dm-remap: freed remap_c struct\n");
}

// Device mapper target structure - defines our target interface
static struct target_type remap_target = {
    .name            = "remap",
    .version         = {3, 0, 0},
    .features        = DM_TARGET_PASSES_INTEGRITY,
    .module          = THIS_MODULE,
    .ctr             = remap_ctr,
    .dtr             = remap_dtr,
    .map             = remap_map,           /* Main I/O mapping function - Phase 3.2B optimized */
    .status          = remap_status,
    .message         = remap_message,       /* From dm-remap-messages.c */
};

static int __init dm_remap_init(void)
{
    int result;
    
    DMR_DEBUG(1, "Initializing dm-remap module with Phase 3.2B optimizations");
    
    /* Initialize sysfs interface first */
    result = dmr_sysfs_init();
    if (result) {
        DMR_ERROR("Failed to initialize sysfs interface: %d", result);
        return result;
    }

    /* Phase 3.2B: Initialize optimization sysfs interface */
    result = dmr_optimization_sysfs_init();
    if (result) {
        DMR_ERROR("Failed to initialize Phase 3.2B optimization sysfs interface: %d", result);
        dmr_sysfs_exit();
        return result;
    }

    /* Phase 3.2B: Initialize optimized I/O processing */
    result = dmr_io_optimized_init(max_remaps);
    if (result) {
        DMR_ERROR("Failed to initialize Phase 3.2B optimized I/O processing: %d", result);
        dmr_optimization_sysfs_cleanup();
        dmr_sysfs_exit();
        return result;
    }

    /* Initialize debug interface */
    result = dmr_debug_init();
    if (result) {
        DMR_DEBUG(0, "Failed to initialize debug interface: %d", result);
        /* Continue without debug - not fatal */
    }
    
    result = dm_register_target(&remap_target);
    if (result < 0) {
        DMR_ERROR("register failed %d", result);
        dmr_io_optimized_cleanup();
        dmr_optimization_sysfs_cleanup();
        dmr_sysfs_exit();
        return result;
    }
    
    DMR_DEBUG(1, "dm-remap module initialized successfully with Phase 3.2B optimizations");
    return result;
}

static void __exit dm_remap_exit(void)
{
    DMR_DEBUG(1, "Exiting dm-remap module with Phase 3.2B optimizations");
    
    dm_unregister_target(&remap_target);
    
    /* Cleanup global I/O subsystem (destroys auto_remap_wq workqueue) */
    dmr_io_exit();
    
    /* Phase 3.2B: Cleanup optimized I/O processing */
    dmr_io_optimized_cleanup();
    
    /* Phase 3.2B: Cleanup optimization sysfs interface */
    dmr_optimization_sysfs_cleanup();
    
    /* Cleanup global interfaces */
    dmr_sysfs_exit();
    dmr_debug_exit();
    
    /* Force cleanup any remaining global workqueues */
    /* Note: Individual device workqueues should be cleaned up in device destructors,
     * but we add safety cleanup here for any missed global workqueues */
    
    DMR_DEBUG(1, "dm-remap module exited successfully with Phase 3.2B optimizations");
}

module_init(dm_remap_init);
module_exit(dm_remap_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Christian");
MODULE_DESCRIPTION("Device Mapper target for dynamic bad sector remapping v2.0 with intelligent bad sector detection and sysfs interface");