/*
 * dm-remap-io.c - I/O handling for dm-remap target
 * 
 * This file contains all the I/O path logic for the dm-remap device mapper target.
 * It handles bio processing, sector remapping, and I/O redirection.
 * 
 * The I/O path is the most performance-critical part of the module, so extensive
 * comments explain the logic and design decisions.
 * 
 * Author: Christian (with AI assistance)
 * License: GPL v2
 */

#include <linux/bio.h>            /* For struct bio, bio manipulation functions */
#include <linux/blk_types.h>      /* For bio operation types, status codes */
#include <linux/device-mapper.h>  /* For DM_MAPIO_* constants */

#include "dm-remap-core.h"        /* Our core data structures */
#include "dm-remap-io.h"          /* Our I/O function declarations */

/*
 * remap_map() - Main I/O processing function
 * 
 * This function is called by the device mapper framework for every I/O request
 * directed to our target. It's the heart of the remapping logic.
 * 
 * PERFORMANCE CRITICAL: This function is called for every single I/O operation,
 * so it must be as fast as possible. We minimize memory allocations, lock
 * contention, and expensive operations.
 * 
 * @ti: Device mapper target instance (contains our remap_c structure)
 * @bio: Block I/O request to process
 * 
 * Returns: DM_MAPIO_REMAPPED if we redirected the bio
 *          DM_MAPIO_SUBMITTED if we handled it ourselves (not used currently)
 */
int remap_map(struct dm_target *ti, struct bio *bio)
{
    /* Extract our target context - this contains all our state */
    struct remap_c *rc = ti->private;
    
    /* Get the logical sector number from the bio */
    sector_t sector = bio->bi_iter.bi_sector;
    
    /* Loop counter for searching the remap table */
    int i;
    
    /* Default: assume we'll send I/O to main device */
    struct dm_dev *target_dev = rc->main_dev;
    sector_t target_sector = rc->main_start + sector;
    
    /* Get our per-I/O context structure from the bio's private data area */
    struct remap_io_ctx *ctx = dmr_per_bio_data(bio, struct remap_io_ctx);
    
    /*
     * Initialize per-I/O context on first access
     * 
     * The device mapper framework reuses this memory, so we need to
     * initialize it. We use ctx->lba == 0 as a "not initialized" marker.
     */
    if (ctx->lba == 0) {
        ctx->lba = sector;
        ctx->was_write = (bio_data_dir(bio) == WRITE);
        ctx->retry_to_spare = false;
    }

    /*
     * DEBUG LOGGING: Log every I/O operation if debug level is high enough
     * 
     * This is placed at the very beginning so we capture ALL I/Os regardless
     * of which path they take through the function.
     */
    if (debug_level >= 2) {
        printk(KERN_INFO "dm-remap: I/O: sector=%llu, size=%u, %s\n", 
               (unsigned long long)sector, bio->bi_iter.bi_size, 
               bio_data_dir(bio) == WRITE ? "WRITE" : "READ");
    }

    /*
     * MULTI-SECTOR I/O HANDLING
     * 
     * Our remapping table only handles single-sector (512-byte) operations.
     * Multi-sector I/Os are passed through unchanged to the main device.
     * 
     * This is a design decision that simplifies the remapping logic:
     * - Most bad sectors affect only single sectors
     * - Multi-sector remapping would require complex splitting logic
     * - Filesystems typically handle multi-sector failures gracefully
     */
    if (bio->bi_iter.bi_size != 512) {
        if (debug_level >= 2) {
            printk(KERN_INFO "dm-remap: Multi-sector passthrough: %u bytes\n", 
                   bio->bi_iter.bi_size);
        }
        
        /*
         * Redirect the bio to the main device at the correct offset
         * 
         * bio_set_dev() changes which block device this bio targets
         * bi_sector adjustment accounts for any offset in our target mapping
         */
        bio_set_dev(bio, rc->main_dev->bdev);
        bio->bi_iter.bi_sector = rc->main_start + bio->bi_iter.bi_sector;
        
        /* Tell device mapper we modified the bio and it should submit it */
        return DM_MAPIO_REMAPPED;
    }

    /*
     * SPECIAL OPERATION HANDLING
     * 
     * Some bio operations (flush, discard, write-zeroes) don't contain normal
     * data and need special handling. We pass them through to the main device.
     * 
     * These operations typically affect multiple sectors and don't make sense
     * to remap on a per-sector basis.
     */
    if (bio_op(bio) == REQ_OP_FLUSH || 
        bio_op(bio) == REQ_OP_DISCARD || 
        bio_op(bio) == REQ_OP_WRITE_ZEROES) {
        
        /* Redirect to main device - same logic as multi-sector case */
        bio_set_dev(bio, rc->main_dev->bdev);
        bio->bi_iter.bi_sector = rc->main_start + bio->bi_iter.bi_sector;
        return DM_MAPIO_REMAPPED;
    }

    /*
     * SINGLE-SECTOR REMAPPING LOGIC
     * 
     * This is where the actual bad sector remapping happens.
     * We search our remap table to see if this sector has been remapped.
     */

    /*
     * CRITICAL SECTION: Search the remap table
     * 
     * We need to hold the lock while searching because another thread
     * might be modifying the table (adding new remaps via messages).
     * 
     * This lock is held for a very short time - just a table lookup.
     */
    spin_lock(&rc->lock);
    
    /* Linear search through the remap table */
    for (i = 0; i < rc->spare_used; i++) {
        /*
         * Check if this table entry matches our sector
         * 
         * We check both conditions:
         * 1. sector matches the main_lba in this entry
         * 2. main_lba is not -1 (which marks unused entries)
         */
        if (sector == rc->table[i].main_lba && 
            rc->table[i].main_lba != (sector_t)-1) {
            
            /*
             * FOUND A REMAP!
             * 
             * This sector has been remapped to the spare device.
             * Update our target device and sector before releasing the lock.
             */
            target_dev = rc->spare_dev;
            target_sector = rc->table[i].spare_lba;
            
            /* Release the lock BEFORE doing I/O operations */
            spin_unlock(&rc->lock);
            
            /* Log the redirection for debugging */
            if (debug_level >= 1) {
                printk(KERN_INFO "dm-remap: REMAP: sector %llu -> spare sector %llu\n", 
                       (unsigned long long)sector, 
                       (unsigned long long)target_sector);
            }
            
            /*
             * Redirect the bio to the spare device
             * 
             * Note: target_sector is the absolute sector number on the spare
             * device, not relative to spare_start. This was pre-calculated
             * when the remap entry was created.
             */
            bio_set_dev(bio, target_dev->bdev);
            bio->bi_iter.bi_sector = target_sector;
            
            return DM_MAPIO_REMAPPED;
        }
    }
    
    /* No remap found - release the lock */
    spin_unlock(&rc->lock);

    /*
     * NORMAL CASE: No remapping needed
     * 
     * This sector is not in our remap table, so send it to the main device.
     * This is the most common case for a healthy storage system.
     */
    if (debug_level >= 2) {
        printk(KERN_INFO "dm-remap: Passthrough: sector %llu to main device\n", 
               (unsigned long long)sector);
    }
    
    /* Redirect to main device at the correct offset */
    bio_set_dev(bio, rc->main_dev->bdev);
    bio->bi_iter.bi_sector = rc->main_start + sector;
    
    return DM_MAPIO_REMAPPED;
}

/*
 * DESIGN NOTES:
 * 
 * 1. LINEAR SEARCH vs HASH TABLE:
 *    We use linear search because:
 *    - Most systems have very few bad sectors (< 100)
 *    - Linear search has better cache locality
 *    - Hash tables add complexity and memory overhead
 *    - The search is done under a spinlock (very short time)
 * 
 * 2. DIRECT BIO REMAPPING vs BIO CLONING:
 *    We modify the original bio instead of cloning because:
 *    - Cloning requires memory allocation (can fail)
 *    - Cloning adds CPU overhead
 *    - Direct remapping is simpler and more reliable
 *    - The device mapper framework handles submission for us
 * 
 * 3. LOCK GRANULARITY:
 *    We use a single spinlock for the entire remap table because:
 *    - Remap operations are rare (only when adding new bad sectors)
 *    - Fine-grained locking would add complexity
 *    - The critical section is very short (just a table lookup)
 * 
 * 4. ERROR HANDLING:
 *    Currently, we don't handle I/O errors in this function.
 *    Error handling will be added in v2 with bio endio callbacks.
 */