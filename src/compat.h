/*
 * compat.h - Compatibility layer for dm-remap across different kernel versions
 * 
 * This header provides compatibility definitions and workarounds for differences
 * between kernel versions. The goal is to support a reasonable range of kernel
 * versions without requiring separate code paths in the main implementation.
 * 
 * KERNEL VERSION SUPPORT:
 * - Primary target: Linux 5.15+ (LTS kernels)
 * - Tested on: Linux 6.1+ (modern kernels)  
 * - May work on older versions with minor modifications
 * 
 * COMPATIBILITY AREAS:
 * 1. Block device interface changes (blk_mode_t vs fmode_t)
 * 2. Device mapper per-bio data interface evolution
 * 3. Bio cloning API changes (not used in v1, here for future reference)
 * 4. Bio completion interface changes
 * 
 * Author: Christian (with AI assistance)
 * License: GPL v2
 */

#ifndef DM_REMAP_COMPAT_H
#define DM_REMAP_COMPAT_H

#include <linux/version.h>        /* KERNEL_VERSION macros */
#include <linux/device-mapper.h>  /* Device mapper API */
#include <linux/bio.h>           /* Bio structures and functions */
#include <linux/blkdev.h>        /* Block device interfaces */

/*
 * BLOCK DEVICE MODE TYPE COMPATIBILITY
 * 
 * Starting with kernel 6.5, the block layer introduced blk_mode_t to replace
 * fmode_t for block device access modes. This provides better type safety.
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 5, 0)
typedef fmode_t blk_mode_t;
#endif

/*
 * BIO ENDIO FUNCTION TYPE
 * 
 * For function pointer declarations and compatibility across kernel versions.
 * Different kernels may have slightly different bio completion signatures.
 */
typedef void (*dm_remap_endio_fn)(struct bio *);

/*
 * GLOBAL STATISTICS COUNTERS
 * 
 * These track bio cloning operations across all dm-remap targets.
 * Used for debugging and performance monitoring.
 * 
 * Note: In v1, we don't use bio cloning to avoid kernel crashes,
 * but these counters are kept for future versions and compatibility.
 */
extern unsigned long dmr_clone_shallow_count;  /* Shallow bio clones */
extern unsigned long dmr_clone_deep_count;    /* Deep bio clones */

/*
 * PER-BIO DATA COMPATIBILITY
 * 
 * The dm_per_bio_data() interface has evolved:
 * - Newer kernels (>= 6.14): Takes (bio, size) parameters for safety
 * - Older kernels: Takes only (bio) parameter
 * 
 * Our macro provides a consistent interface that works on both.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,14,0)
#define dmr_per_bio_data(bio, type) \
    ((type *)dm_per_bio_data((bio), sizeof(type)))
#else
#define dmr_per_bio_data(bio, type) \
    ((type *)dm_per_bio_data((bio)))
#endif

/*
 * BIO CLONING COMPATIBILITY SHIMS
 * 
 * Bio cloning interfaces have evolved significantly across kernel versions:
 * - 6.14+: bio_alloc_clone(bdev, bio_src, gfp, bioset)
 * - 6.12-6.13: bio_dup() for shallow, bio_alloc_clone() for deep
 * - Older: bio_clone_fast() for shallow, bio_clone_bioset() for deep
 * 
 * NOTE: In dm-remap v1, we don't use bio cloning because it causes
 * kernel crashes in device mapper contexts. These functions are provided
 * for completeness and potential future use.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,14,0)

/* Modern kernel (6.14+) bio cloning */
static inline struct bio *dmr_bio_clone_shallow(struct bio *bio, gfp_t gfp)
{
    dmr_clone_shallow_count++;
    return bio_alloc_clone(bio->bi_bdev, bio, gfp, NULL);
}

static inline struct bio *dmr_bio_clone_deep(struct bio *bio, gfp_t gfp)
{
    dmr_clone_deep_count++;
    return bio_alloc_clone(bio->bi_bdev, bio, gfp, NULL);
}

#elif LINUX_VERSION_CODE >= KERNEL_VERSION(6,12,0)

/* Transitional kernel (6.12-6.13) bio cloning */
static inline struct bio *dmr_bio_clone_shallow(struct bio *bio, gfp_t gfp)
{
    dmr_clone_shallow_count++;
    return bio_dup(bio, gfp);
}

static inline struct bio *dmr_bio_clone_deep(struct bio *bio, gfp_t gfp)
{
    dmr_clone_deep_count++;
    return bio_alloc_clone(bio->bi_bdev, bio, gfp, NULL);
}

#else

/* Older kernel bio cloning */
extern struct bio_set dm_remap_bioset;  /* Would need to be allocated */

static inline struct bio *dmr_bio_clone_shallow(struct bio *bio, gfp_t gfp)
{
    dmr_clone_shallow_count++;
    return bio_clone_fast(bio, gfp, &dm_remap_bioset);
}

static inline struct bio *dmr_bio_clone_deep(struct bio *bio, gfp_t gfp)
{
    dmr_clone_deep_count++;
    return bio_clone_bioset(bio, gfp, &dm_remap_bioset);
}

#endif /* kernel version bio cloning */

/*
 * BIO COMPLETION COMPATIBILITY
 * 
 * Bio completion interfaces have also evolved:
 * - Some kernels have dm_endio() for device mapper specific completion
 * - Others use bio_endio() directly
 * - Parameter counts and meanings may vary
 * 
 * Our macro provides a unified interface for bio completion.
 */
#if defined(dm_endio)
#define dmr_endio(bio, status) dm_endio((bio), (status))
#elif defined(bio_endio)
#define dmr_endio(bio, status) bio_endio((bio), (status))  
#else
#define dmr_endio(bio, status) bio_endio((bio))
#endif

/*
 * KERNEL VERSION DETECTION UTILITIES
 * 
 * Useful macros for version-specific code paths if needed.
 */
#define DMR_KERNEL_VERSION LINUX_VERSION_CODE

#define DMR_KERNEL_AT_LEAST(major, minor, patch) \
    (LINUX_VERSION_CODE >= KERNEL_VERSION(major, minor, patch))

#define DMR_KERNEL_OLDER_THAN(major, minor, patch) \
    (LINUX_VERSION_CODE < KERNEL_VERSION(major, minor, patch))

/*
 * COMPILE-TIME COMPATIBILITY VALIDATION
 * 
 * These warnings help catch potential compatibility issues during compilation.
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
#warning "dm-remap may not work properly on kernels older than 5.10"
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(7, 0, 0)
#warning "dm-remap has not been tested on kernel versions 7.0 or higher"
#endif

/*
 * FUTURE COMPATIBILITY NOTES
 * 
 * Areas that may need attention in future kernel versions:
 * 
 * 1. Device Mapper API Evolution:
 *    - New target operation callbacks
 *    - Changed function signatures for existing operations
 *    - Modified target structure fields
 * 
 * 2. Block Layer Changes:
 *    - New bio fields or changed field meanings
 *    - Different I/O completion patterns
 *    - Modified error code conventions
 * 
 * 3. Memory Management:
 *    - New GFP flags or changed flag meanings
 *    - Different allocation/deallocation patterns
 *    - NUMA-aware allocation interfaces
 * 
 * 4. Synchronization Primitives:
 *    - New locking mechanisms
 *    - Changed spinlock/mutex interfaces
 *    - RCU (Read-Copy-Update) integration
 * 
 * When adding support for new kernel versions:
 * - Test all basic functionality thoroughly
 * - Verify module loading/unloading works
 * - Check target creation/destruction
 * - Validate I/O path performance
 * - Test error conditions and recovery
 */

/*
 * DEBUGGING AND DIAGNOSTICS
 * 
 * Compatibility helpers for debugging across kernel versions.
 * These ensure consistent behavior regardless of kernel version.
 */

/* Standard error codes used across all kernel versions */
#define DMR_SUCCESS        0        /* Operation succeeded */
#define DMR_EINVAL        -EINVAL   /* Invalid argument */
#define DMR_ENOMEM        -ENOMEM   /* Out of memory */
#define DMR_EIO           -EIO      /* I/O error */
#define DMR_ENOTTY        -ENOTTY   /* Invalid ioctl/message */
#define DMR_ENOSPC        -ENOSPC   /* No space left (spare area full) */

/* Memory barrier compatibility (usually not needed for our use case) */
#define DMR_MEMORY_BARRIER() smp_mb()

#endif /* DM_REMAP_COMPAT_H */
