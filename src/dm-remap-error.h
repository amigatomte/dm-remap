/*
 * dm-remap-error.h - Error handling interface definitions for dm-remap v2.0
 * 
 * This header defines the interfaces and constants for the intelligent
 * error handling system in dm-remap v2.0.
 * 
 * Author: Christian (with AI assistance)
 * License: GPL v2
 */

#ifndef DM_REMAP_ERROR_H
#define DM_REMAP_ERROR_H

#include <linux/types.h>         /* Basic type definitions */
#include <linux/blkdev.h>        /* sector_t definition */

/* Forward declarations */
struct remap_c;
struct remap_io_ctx;

/* Constants for retry logic */
#define DMR_MAX_RETRIES         3    /* Maximum retry attempts per I/O */
#define DMR_RETRY_DELAY_BASE    10   /* Base retry delay in milliseconds */

/* Device health assessment levels */
#define DMR_DEVICE_HEALTH_EXCELLENT  0  /* No issues detected */
#define DMR_DEVICE_HEALTH_GOOD       1  /* Minor issues, well managed */
#define DMR_DEVICE_HEALTH_FAIR       2  /* Some problems, monitoring needed */
#define DMR_DEVICE_HEALTH_POOR       3  /* Significant issues, action needed */
#define DMR_DEVICE_HEALTH_CRITICAL   4  /* Critical state, immediate attention */

/*
 * Function prototypes for error handling operations
 */

/* Retry decision logic */
bool dmr_should_retry_io(struct remap_io_ctx *ctx, int error);
unsigned int dmr_calculate_retry_delay(u32 retry_count);

/* Health tracking and assessment */
void dmr_update_sector_health(struct remap_c *rc, sector_t lba, 
                             bool was_error, int error_code);
u8 dmr_assess_overall_health(struct remap_c *rc);
const char *dmr_get_health_string(u8 health);

/* Automatic remapping logic */
bool dmr_should_auto_remap(struct remap_c *rc, sector_t lba);
int dmr_perform_auto_remap(struct remap_c *rc, sector_t lba);

/*
 * Inline helper functions for common error handling tasks
 */

/**
 * dmr_is_retryable_error() - Quick check if an error code is retryable
 * @error: Error code to check
 * 
 * Returns: true if the error might be resolved by retrying
 */
static inline bool dmr_is_retryable_error(int error)
{
    return (error == -EIO || error == -ETIMEDOUT || error == -EREMOTEIO);
}

/**
 * dmr_increment_error_stats() - Update error statistics
 * @rc: Target context
 * @is_write: True if this was a write operation
 * 
 * Updates the appropriate error counter in the target context.
 */
static inline void dmr_increment_error_stats(struct remap_c *rc, bool is_write)
{
    if (is_write)
        rc->write_errors++;
    else
        rc->read_errors++;
}

/**
 * dmr_get_error_rate() - Calculate error rate for a sector
 * @error_count: Number of errors
 * @access_count: Total number of accesses
 * 
 * Returns: Error rate as a percentage (0-100)
 */
static inline u32 dmr_get_error_rate(u32 error_count, u32 access_count)
{
    if (access_count == 0)
        return 0;
    return (error_count * 100) / access_count;
}

#endif /* DM_REMAP_ERROR_H */