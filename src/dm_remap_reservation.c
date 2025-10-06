/* dm-remap v4.0 Reservation System Implementation
 * 
 * Manages sector reservations to prevent metadata corruption
 * by spare sector allocation conflicts
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/bitmap.h>
#include <linux/slab.h>

#include "dm-remap-core.h"
#include "../v4_development/metadata/dm_remap_metadata_v4.h"

/* ============================================================================
 * RESERVATION BITMAP MANAGEMENT
 * ============================================================================ */

/**
 * dmr_init_reservation_system - Initialize reservation bitmap for target
 * @rc: remap context
 * Returns: 0 on success, negative error code on failure
 */
int dmr_init_reservation_system(struct remap_c *rc)
{
    size_t bitmap_size;
    
    if (!rc || rc->spare_len == 0) {
        return -EINVAL;
    }
    
    /* Calculate bitmap size needed */
    bitmap_size = BITS_TO_LONGS(rc->spare_len) * sizeof(unsigned long);
    
    /* Allocate reservation bitmap */
    rc->reserved_sectors = kzalloc(bitmap_size, GFP_KERNEL);
    if (!rc->reserved_sectors) {
        printk(KERN_ERR "dm-remap: Failed to allocate reservation bitmap\n");
        return -ENOMEM;
    }
    
    /* Initialize spare allocation cursor */
    rc->next_spare_sector = 0;
    rc->metadata_copies_count = 0;
    rc->placement_strategy = 0;
    
    /* Clear metadata sector array */
    memset(rc->metadata_sectors, 0, sizeof(rc->metadata_sectors));
    
    printk(KERN_INFO "dm-remap: Initialized reservation system for %llu sectors\n",
           (unsigned long long)rc->spare_len);
    
    return 0;
}

/**
 * dmr_cleanup_reservation_system - Clean up reservation system
 * @rc: remap context
 */
void dmr_cleanup_reservation_system(struct remap_c *rc)
{
    if (rc && rc->reserved_sectors) {
        kfree(rc->reserved_sectors);
        rc->reserved_sectors = NULL;
    }
}

/**
 * dmr_reserve_metadata_sectors - Reserve sectors for metadata copies
 * @rc: remap context
 * @metadata_sectors: array of sector positions to reserve
 * @count: number of sectors in array
 * @sectors_per_copy: number of sectors per metadata copy (usually 8)
 * Returns: 0 on success, negative error code on failure
 */
int dmr_reserve_metadata_sectors(struct remap_c *rc, 
                                sector_t *metadata_sectors,
                                int count, 
                                int sectors_per_copy)
{
    int i, j;
    sector_t relative_sector;
    
    if (!rc || !rc->reserved_sectors || !metadata_sectors || count <= 0) {
        return -EINVAL;
    }
    
    if (count > ARRAY_SIZE(rc->metadata_sectors)) {
        printk(KERN_ERR "dm-remap: Too many metadata copies (%d > %zu)\n",
               count, ARRAY_SIZE(rc->metadata_sectors));
        return -E2BIG;
    }
    
    printk(KERN_INFO "dm-remap: Reserving %d metadata locations, %d sectors each\n",
           count, sectors_per_copy);
    
    /* Reserve sectors for each metadata copy */
    for (i = 0; i < count; i++) {
        /* Convert absolute sector to relative offset in spare device */
        if (metadata_sectors[i] < rc->spare_start) {
            printk(KERN_ERR "dm-remap: Metadata sector %llu before spare start %llu\n",
                   metadata_sectors[i], rc->spare_start);
            return -EINVAL;
        }
        
        relative_sector = metadata_sectors[i] - rc->spare_start;
        
        if (relative_sector >= rc->spare_len) {
            printk(KERN_ERR "dm-remap: Metadata sector %llu beyond spare end\n",
                   metadata_sectors[i]);
            return -EINVAL;
        }
        
        /* Reserve all sectors for this metadata copy */
        for (j = 0; j < sectors_per_copy; j++) {
            if (relative_sector + j < rc->spare_len) {
                set_bit(relative_sector + j, rc->reserved_sectors);
            }
        }
        
        /* Store metadata sector location */
        rc->metadata_sectors[i] = metadata_sectors[i];
        
        printk(KERN_DEBUG "dm-remap: Reserved metadata copy %d at sector %llu (%d sectors)\n",
               i, metadata_sectors[i], sectors_per_copy);
    }
    
    rc->metadata_copies_count = count;
    
    return 0;
}

/**
 * dmr_allocate_spare_sector - Allocate next available spare sector
 * @rc: remap context
 * Returns: absolute sector number, or SECTOR_MAX if no sectors available
 */
sector_t dmr_allocate_spare_sector(struct remap_c *rc)
{
    sector_t candidate;
    sector_t max_search;
    
    if (!rc || !rc->reserved_sectors) {
        return SECTOR_MAX;
    }
    
    candidate = rc->next_spare_sector;
    max_search = rc->spare_len;
    
    /* Search for next unreserved sector */
    while (candidate < max_search) {
        if (!test_bit(candidate, rc->reserved_sectors)) {
            /* Found unreserved sector */
            rc->next_spare_sector = candidate + 1;
            return rc->spare_start + candidate;
        }
        candidate++;
    }
    
    /* No unreserved sectors found, try wrapping around */
    candidate = 0;
    max_search = rc->next_spare_sector;
    
    while (candidate < max_search) {
        if (!test_bit(candidate, rc->reserved_sectors)) {
            /* Found unreserved sector */
            rc->next_spare_sector = candidate + 1;
            return rc->spare_start + candidate;
        }
        candidate++;
    }
    
    /* No spare sectors available */
    printk(KERN_WARNING "dm-remap: No spare sectors available (all reserved or used)\n");
    return SECTOR_MAX;
}

/**
 * dmr_check_sector_reserved - Check if a sector is reserved
 * @rc: remap context
 * @sector: absolute sector number to check
 * Returns: true if reserved, false if available
 */
bool dmr_check_sector_reserved(struct remap_c *rc, sector_t sector)
{
    sector_t relative_sector;
    
    if (!rc || !rc->reserved_sectors) {
        return false;
    }
    
    if (sector < rc->spare_start || sector >= rc->spare_start + rc->spare_len) {
        return false; /* Outside spare area */
    }
    
    relative_sector = sector - rc->spare_start;
    return test_bit(relative_sector, rc->reserved_sectors);
}

/* ============================================================================
 * DYNAMIC METADATA INTEGRATION
 * ============================================================================ */

/**
 * dmr_setup_dynamic_metadata_reservations - Set up reservations for dynamic metadata
 * @rc: remap context
 * Returns: 0 on success, negative error code on failure
 */
int dmr_setup_dynamic_metadata_reservations(struct remap_c *rc)
{
    sector_t metadata_sectors[8];
    int max_copies = 5;
    int actual_copies;
    int ret;
    u8 strategy;
    
    if (!rc) {
        return -EINVAL;
    }
    
    /* Calculate optimal metadata placement for this spare device */
    ret = calculate_dynamic_metadata_sectors(rc->spare_len, metadata_sectors, &max_copies);
    if (ret < 0) {
        if (ret == -ENOSPC) {
            printk(KERN_WARNING "dm-remap: Spare device too small for metadata (%llu sectors)\n",
                   rc->spare_len);
        }
        return ret;
    }
    
    actual_copies = max_copies;
    
    /* Determine placement strategy */
    if (rc->spare_len >= 8192) {  /* 4MB+ */
        strategy = PLACEMENT_STRATEGY_GEOMETRIC;
    } else if (rc->spare_len >= 1024) {  /* 512KB+ */
        strategy = PLACEMENT_STRATEGY_LINEAR;
    } else if (rc->spare_len >= 72) {  /* 36KB+ */
        strategy = PLACEMENT_STRATEGY_MINIMAL;
    } else {
        strategy = PLACEMENT_STRATEGY_IMPOSSIBLE;
        return -ENOSPC;
    }
    
    rc->placement_strategy = strategy;
    
    /* Convert relative sectors to absolute sectors */
    for (int i = 0; i < actual_copies; i++) {
        metadata_sectors[i] += rc->spare_start;
    }
    
    /* Reserve sectors for metadata */
    ret = dmr_reserve_metadata_sectors(rc, metadata_sectors, actual_copies, 
                                     DM_REMAP_METADATA_SECTORS);
    if (ret < 0) {
        printk(KERN_ERR "dm-remap: Failed to reserve metadata sectors: %d\n", ret);
        return ret;
    }
    
    printk(KERN_INFO "dm-remap: Set up %s metadata strategy with %d copies\n",
           get_placement_strategy_name(strategy), actual_copies);
    
    return 0;
}

/* ============================================================================
 * STATISTICS AND DEBUGGING
 * ============================================================================ */

/**
 * dmr_get_reservation_stats - Get reservation system statistics
 * @rc: remap context
 * @total_sectors: output for total spare sectors
 * @reserved_sectors: output for reserved sector count
 * @available_sectors: output for available sector count
 */
void dmr_get_reservation_stats(struct remap_c *rc, 
                              sector_t *total_sectors,
                              sector_t *reserved_sectors, 
                              sector_t *available_sectors)
{
    sector_t reserved_count = 0;
    sector_t i;
    
    if (!rc || !rc->reserved_sectors) {
        if (total_sectors) *total_sectors = 0;
        if (reserved_sectors) *reserved_sectors = 0;
        if (available_sectors) *available_sectors = 0;
        return;
    }
    
    /* Count reserved sectors */
    for (i = 0; i < rc->spare_len; i++) {
        if (test_bit(i, rc->reserved_sectors)) {
            reserved_count++;
        }
    }
    
    if (total_sectors) *total_sectors = rc->spare_len;
    if (reserved_sectors) *reserved_sectors = reserved_count;
    if (available_sectors) *available_sectors = rc->spare_len - reserved_count - rc->spare_used;
}

/**
 * dmr_print_reservation_map - Print reservation bitmap for debugging
 * @rc: remap context  
 * @max_sectors: maximum sectors to print (0 = print all)
 */
void dmr_print_reservation_map(struct remap_c *rc, sector_t max_sectors)
{
    sector_t i, limit;
    
    if (!rc || !rc->reserved_sectors) {
        printk(KERN_INFO "dm-remap: No reservation system initialized\n");
        return;
    }
    
    limit = max_sectors ? min(max_sectors, rc->spare_len) : rc->spare_len;
    
    printk(KERN_INFO "dm-remap: Reservation map (R=reserved, A=available, U=used):\n");
    
    for (i = 0; i < limit; i += 64) {
        char line[80];
        sector_t j, end = min(i + 64, limit);
        int pos = 0;
        
        pos += snprintf(line + pos, sizeof(line) - pos, "%8llu: ", i);
        
        for (j = i; j < end && pos < sizeof(line) - 2; j++) {
            if (test_bit(j, rc->reserved_sectors)) {
                line[pos++] = 'R';
            } else if (j < rc->spare_used) {
                line[pos++] = 'U';
            } else {
                line[pos++] = 'A';
            }
        }
        
        line[pos] = '\0';
        printk(KERN_INFO "%s\n", line);
    }
}

/* ============================================================================
 * STUB IMPLEMENTATIONS FOR DYNAMIC METADATA FUNCTIONS
 * ============================================================================ */

/**
 * calculate_dynamic_metadata_sectors - Stub implementation for dynamic placement
 * @spare_size_sectors: Available spare device size in sectors
 * @sectors_out: Output array for calculated sector positions
 * @max_copies: Maximum desired copies (input), actual copies (output)
 * Returns: 0 on success, negative error code on failure
 */
int calculate_dynamic_metadata_sectors(sector_t spare_size_sectors,
                                     sector_t *sectors_out,
                                     int *max_copies)
{
    int desired_copies = *max_copies;
    int actual_copies;
    sector_t min_spacing = DM_REMAP_METADATA_SECTORS;
    
    /* Ensure minimum spare size for metadata + actual spare sectors */
    sector_t min_viable_size = min_spacing + 64;  /* 4KB metadata + 32KB spare minimum */
    if (spare_size_sectors < min_viable_size) {
        return -ENOSPC;  /* Too small for practical use */
    }
    
    /* Strategy selection based on spare device size */
    if (spare_size_sectors >= 8192) {  /* 4MB+ - geometric strategy */
        const sector_t geometric_pattern[] = {0, 1024, 2048, 4096, 8192};
        actual_copies = 0;
        
        for (int i = 0; i < 5 && i < desired_copies; i++) {
            if (geometric_pattern[i] + min_spacing <= spare_size_sectors) {
                sectors_out[actual_copies] = geometric_pattern[i];
                actual_copies++;
            }
        }
    } else if (spare_size_sectors >= 1024) {  /* 512KB+ - linear strategy */
        actual_copies = min(desired_copies, (int)(spare_size_sectors / min_spacing));
        if (actual_copies > 4) actual_copies = 4;  /* Reasonable limit */
        
        for (int i = 0; i < actual_copies; i++) {
            sectors_out[i] = i * (spare_size_sectors / actual_copies);
        }
    } else {  /* < 512KB - minimal strategy */
        actual_copies = min(desired_copies, (int)(spare_size_sectors / min_spacing));
        if (actual_copies > 8) actual_copies = 8;  /* Reasonable limit */
        
        for (int i = 0; i < actual_copies; i++) {
            sectors_out[i] = i * min_spacing;
        }
    }
    
    if (actual_copies < 1) {
        return -ENOSPC;
    }
    
    *max_copies = actual_copies;
    return 0;
}

/**
 * get_placement_strategy_name - Get human-readable strategy name
 * @strategy: Strategy constant
 * Returns: String name of strategy
 */
const char *get_placement_strategy_name(u32 strategy)
{
    switch (strategy) {
    case PLACEMENT_STRATEGY_GEOMETRIC:
        return "geometric";
    case PLACEMENT_STRATEGY_LINEAR:
        return "linear";
    case PLACEMENT_STRATEGY_MINIMAL:
        return "minimal";
    case PLACEMENT_STRATEGY_IMPOSSIBLE:
        return "impossible";
    default:
        return "unknown";
    }
}