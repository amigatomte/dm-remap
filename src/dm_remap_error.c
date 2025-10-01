/*
 * dm-remap-error.c - Error handling and recovery logic for dm-remap v2.0
 * 
 * This file implements the intelligent error handling features that make
 * dm-remap v2.0 capable of automatically detecting and remapping bad sectors.
 * 
 * KEY v2.0 FEATURES:
 * - Automatic bad sector detection from I/O errors
 * - Intelligent retry logic with exponential backoff
 * - Proactive remapping based on error patterns
 * - Health assessment and monitoring
 * 
 * Author: Christian (with AI assistance)
 * License: GPL v2
 */

#include <linux/module.h>         /* Core kernel module support */
#include <linux/bio.h>           /* Bio structures and functions */
#include <linux/blkdev.h>        /* Block device interfaces */
#include <linux/device-mapper.h> /* Device mapper framework */
#include <linux/delay.h>         /* msleep for retry delays */
#include <linux/jiffies.h>       /* Time handling */

#include "dm-remap-core.h"       /* Core data structures */
#include "dm-remap-error.h"      /* Error handling interfaces */

/*
 * dmr_should_retry_io() - Determine if an I/O operation should be retried
 * 
 * This function analyzes an I/O error and decides whether the operation
 * should be retried, and if so, where (original location vs spare).
 * 
 * @ctx: I/O context containing error information
 * @error: The error code that occurred
 * 
 * Returns: true if the I/O should be retried, false otherwise
 */
bool dmr_should_retry_io(struct remap_io_ctx *ctx, int error)
{
    /* Don't retry if we've already exceeded maximum attempts */
    if (ctx->retry_count >= DMR_MAX_RETRIES) {
        DMR_DEBUG(1, "Max retries exceeded for sector %llu", 
                  (unsigned long long)ctx->original_lba);
        return false;
    }
    
    /* Analyze the type of error to determine if retry makes sense */
    switch (error) {
    case -EIO:           /* I/O error - possibly recoverable */
    case -ETIMEDOUT:     /* Timeout - might work on retry */
    case -EREMOTEIO:     /* Remote I/O error - retry might help */
        DMR_DEBUG(2, "Retryable error %d on sector %llu (attempt %u)", 
                  error, (unsigned long long)ctx->original_lba, ctx->retry_count);
        return true;
        
    case -ENOMEM:        /* Out of memory - unlikely to help immediately */
    case -EINVAL:        /* Invalid argument - won't fix with retry */
    case -ENODEV:        /* No device - permanent failure */
        DMR_DEBUG(1, "Non-retryable error %d on sector %llu", 
                  error, (unsigned long long)ctx->original_lba);
        return false;
        
    default:
        /* For unknown errors, try once more but be conservative */
        DMR_DEBUG(1, "Unknown error %d on sector %llu, trying once more", 
                  error, (unsigned long long)ctx->original_lba);
        return (ctx->retry_count == 0);
    }
}

/*
 * dmr_calculate_retry_delay() - Calculate delay before retry attempt
 * 
 * Implements exponential backoff to avoid overwhelming a struggling device.
 * 
 * @retry_count: Number of retries already attempted
 * 
 * Returns: delay in milliseconds
 */
unsigned int dmr_calculate_retry_delay(u32 retry_count)
{
    /* Exponential backoff: 10ms, 50ms, 250ms */
    static const unsigned int delays[] = {10, 50, 250};
    
    if (retry_count < ARRAY_SIZE(delays))
        return delays[retry_count];
    else
        return delays[ARRAY_SIZE(delays) - 1];  /* Cap at maximum delay */
}

/*
 * dmr_update_sector_health() - Update health statistics for a sector
 * 
 * This function tracks I/O operations and errors to assess sector health.
 * It's called after every I/O operation to maintain accurate statistics.
 * 
 * @rc: Target context
 * @lba: Logical block address
 * @was_error: True if this I/O resulted in an error
 * @error_code: The specific error code (if was_error is true)
 */
void dmr_update_sector_health(struct remap_c *rc, sector_t lba, 
                             bool was_error, int error_code)
{
    struct remap_entry *entry;
    unsigned long flags;
    int i;
    
    /* Find the entry for this sector (if it exists in our tracking) */
    spin_lock_irqsave(&rc->lock, flags);
    
    /* Look for existing entry - search all health tracking entries */
    entry = NULL;
    for (i = 0; i < rc->health_entries; i++) {
        if (rc->table[i].main_lba == lba) {
            entry = &rc->table[i];
            break;
        }
    }
    
    /* If no entry exists and this is an error, we may need to create one */
    if (!entry && was_error && rc->health_entries < rc->spare_len) {
        /* Create new tracking entry for this problematic sector */
        entry = &rc->table[rc->health_entries];
        entry->main_lba = lba;
        entry->spare_lba = rc->spare_start + rc->health_entries;  /* Reserved spare slot */
        entry->error_count = 0;
        entry->access_count = 0;
        entry->last_error_time = 0;
        entry->remap_reason = 0;  /* Not remapped yet */
        entry->health_status = DMR_HEALTH_UNKNOWN;
        rc->health_entries++;  /* Track this new health entry */
        
        DMR_DEBUG(1, "Created health tracking entry for sector %llu (entry %llu)", 
                  (unsigned long long)lba, (unsigned long long)(rc->health_entries - 1));
    }
    
    /* Update statistics if we have an entry */
    if (entry) {
        entry->access_count++;
        
        if (was_error) {
            entry->error_count++;
            entry->last_error_time = jiffies;
            rc->write_errors += (error_code == -EIO) ? 1 : 0;  /* Approximate */
            
            /* Update health status based on error pattern */
            if (entry->error_count == 1) {
                entry->health_status = DMR_HEALTH_SUSPECT;
            } else if (entry->error_count >= rc->error_threshold) {
                entry->health_status = DMR_HEALTH_BAD;
            }
            
            DMR_DEBUG(1, "Sector %llu health update: %u errors in %u accesses", 
                      (unsigned long long)lba, entry->error_count, entry->access_count);
        } else {
            /* Successful I/O - improve health assessment if it was suspect */
            if (entry->health_status == DMR_HEALTH_SUSPECT && 
                entry->access_count > 10 && 
                entry->error_count * 10 < entry->access_count) {
                entry->health_status = DMR_HEALTH_GOOD;
                DMR_DEBUG(2, "Sector %llu health improved to GOOD", 
                          (unsigned long long)lba);
            }
        }
    }
    
    spin_unlock_irqrestore(&rc->lock, flags);
}

/*
 * dmr_should_auto_remap() - Determine if a sector should be automatically remapped
 * 
 * This function analyzes a sector's error history and decides if it should
 * be automatically remapped to a spare sector.
 * 
 * @rc: Target context
 * @lba: Logical block address to evaluate
 * 
 * Returns: true if the sector should be auto-remapped, false otherwise
 */
bool dmr_should_auto_remap(struct remap_c *rc, sector_t lba)
{
    struct remap_entry *entry;
    unsigned long flags;
    bool should_remap = false;
    int i;
    
    if (!rc->auto_remap_enabled)
        return false;
    
    spin_lock_irqsave(&rc->lock, flags);
    
    /* Find the health tracking entry for this sector */
    for (i = 0; i < rc->health_entries; i++) {
        entry = &rc->table[i];
        if (entry->main_lba == lba) {
            /* Check if this sector meets auto-remap criteria */
            should_remap = (entry->error_count >= rc->error_threshold) &&
                          (entry->health_status == DMR_HEALTH_BAD) &&
                          (entry->remap_reason == 0);  /* Not already remapped */
            
            if (should_remap) {
                DMR_DEBUG(0, "Auto-remap triggered for sector %llu (%u errors)", 
                          (unsigned long long)lba, entry->error_count);
            }
            break;
        }
    }
    
    spin_unlock_irqrestore(&rc->lock, flags);
    return should_remap;
}

/*
 * dmr_perform_auto_remap() - Automatically remap a bad sector
 * 
 * This function performs the actual remapping of a sector that has been
 * identified as needing automatic remapping.
 * 
 * @rc: Target context  
 * @lba: Logical block address to remap
 * 
 * Returns: 0 on success, negative error code on failure
 */
int dmr_perform_auto_remap(struct remap_c *rc, sector_t lba)
{
    struct remap_entry *entry;
    unsigned long flags;
    int ret = -ENOSPC;  /* Default: no space for remap */
    int i;
    
    spin_lock_irqsave(&rc->lock, flags);
    
    /* Check if we have spare sectors available */
    if (rc->spare_used >= rc->spare_len) {
        DMR_DEBUG(0, "Cannot auto-remap sector %llu: spare area full", 
                  (unsigned long long)lba);
        goto unlock;
    }
    
    /* Find existing health tracking entry or create new remap entry */
    entry = NULL;
    for (i = 0; i < rc->spare_len; i++) {
        if (rc->table[i].main_lba == lba) {
            entry = &rc->table[i];
            break;
        }
    }
    
    /* If no existing entry, use the next available slot */
    if (!entry) {
        entry = &rc->table[rc->spare_used];
        entry->main_lba = lba;
        entry->spare_lba = rc->spare_start + rc->spare_used;
        entry->error_count = rc->error_threshold;  /* Triggered auto-remap */
        entry->access_count = rc->error_threshold;
        entry->last_error_time = jiffies;
    }
    
    /* Mark as remapped */
    entry->remap_reason = DMR_REMAP_WRITE_ERR;  /* Assume write error triggered it */
    entry->health_status = DMR_HEALTH_REMAPPED;
    
    /* Update counters */
    if (rc->table[rc->spare_used].main_lba == lba) {
        rc->spare_used++;  /* Only increment if this is a new entry */
    }
    rc->auto_remaps++;
    global_auto_remaps++;  /* Global counter for testing */
    
    DMR_DEBUG(0, "Auto-remapped sector %llu to spare %llu (reason: %s)", 
              (unsigned long long)lba, 
              (unsigned long long)entry->spare_lba,
              "write_error");
    
    ret = 0;  /* Success */
    
unlock:
    spin_unlock_irqrestore(&rc->lock, flags);
    return ret;
}

/*
 * dmr_assess_overall_health() - Assess overall device health
 * 
 * This function analyzes the current state of all tracked sectors
 * to determine the overall health of the device.
 * 
 * @rc: Target context
 * 
 * Returns: Overall health status (DMR_DEVICE_HEALTH_*)
 */
u8 dmr_assess_overall_health(struct remap_c *rc)
{
    unsigned long flags;
    u32 total_errors = 0;
    u32 bad_sectors = 0;
    u32 remapped_sectors = rc->spare_used;
    u8 health;
    int i;
    
    spin_lock_irqsave(&rc->lock, flags);
    
    /* Count errors and bad sectors */
    for (i = 0; i < rc->spare_len; i++) {
        struct remap_entry *entry = &rc->table[i];
        if (DMR_IS_REMAPPED_ENTRY(entry)) {
            total_errors += entry->error_count;
            if (entry->health_status == DMR_HEALTH_BAD)
                bad_sectors++;
        }
    }
    
    spin_unlock_irqrestore(&rc->lock, flags);
    
    /* Determine overall health based on statistics - no floating point in kernel */
    if (remapped_sectors * 10 >= rc->spare_len * 9) {  /* >= 90% */
        health = DMR_DEVICE_HEALTH_CRITICAL;  /* Spare area nearly full */
    } else if (bad_sectors > 100 || remapped_sectors * 2 > rc->spare_len) {  /* > 50% */
        health = DMR_DEVICE_HEALTH_POOR;      /* Many problems */
    } else if (bad_sectors > 10 || remapped_sectors * 10 > rc->spare_len) {  /* > 10% */
        health = DMR_DEVICE_HEALTH_FAIR;      /* Some problems */
    } else if (total_errors > 0 || remapped_sectors > 0) {
        health = DMR_DEVICE_HEALTH_GOOD;      /* Minor issues */
    } else {
        health = DMR_DEVICE_HEALTH_EXCELLENT; /* No problems detected */
    }
    
    rc->overall_health = health;
    return health;
}

/*
 * dmr_get_health_string() - Convert health status to human-readable string
 * 
 * @health: Health status code
 * 
 * Returns: String representation of health status
 */
const char *dmr_get_health_string(u8 health)
{
    switch (health) {
    case DMR_DEVICE_HEALTH_EXCELLENT: return "excellent";
    case DMR_DEVICE_HEALTH_GOOD:      return "good";
    case DMR_DEVICE_HEALTH_FAIR:      return "fair";
    case DMR_DEVICE_HEALTH_POOR:      return "poor";
    case DMR_DEVICE_HEALTH_CRITICAL:  return "critical";
    default:                          return "unknown";
    }
}