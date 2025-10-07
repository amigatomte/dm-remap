/*
 * dm-remap-health-map.c - Health Tracking Map Implementation
 * 
 * Health Map Management for dm-remap Background Health Scanning
 * Provides efficient storage and retrieval of per-sector health information
 * 
 * This module implements a sparse health tracking system that efficiently
 * manages health data for large storage devices without consuming excessive
 * memory for unused sectors.
 * 
 * Author: Christian (with AI assistance)
 * License: GPL v2
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/bitmap.h>
#include <linux/spinlock.h>
#include <linux/atomic.h>
#include <linux/delay.h>

#include "dm-remap-health-core.h"

/* Module metadata */
MODULE_AUTHOR("Christian (with AI assistance)");
MODULE_DESCRIPTION("Health Map Management for dm-remap");
MODULE_LICENSE("GPL");

/* Health map constants */
#define DMR_HEALTH_MAP_INITIAL_SIZE     1024        /* Initial health entries */
#define DMR_HEALTH_MAP_GROWTH_FACTOR    2           /* Growth multiplier */
#define DMR_HEALTH_MAP_MAX_LOAD_FACTOR  75          /* Max load percentage */

/**
 * dmr_health_map_init - Initialize a health tracking map
 * @health_map: Pointer to health map pointer (output)
 * @total_sectors: Total number of sectors to potentially track
 * 
 * Creates and initializes a new health tracking map for the specified
 * number of sectors. Uses sparse representation to minimize memory usage.
 * 
 * Returns: 0 on success, negative error code on failure
 */
int dmr_health_map_init(struct dmr_health_map **health_map, sector_t total_sectors)
{
    struct dmr_health_map *map;
    size_t bitmap_size;

    if (!health_map || total_sectors == 0) {
        printk(KERN_ERR "dm-remap-health-map: Invalid parameters\n");
        return -EINVAL;
    }

    /* Allocate main health map structure */
    map = kzalloc(sizeof(*map), GFP_KERNEL);
    if (!map) {
        printk(KERN_ERR "dm-remap-health-map: Failed to allocate health map\n");
        return -ENOMEM;
    }

    /* Initialize map parameters */
    map->total_sectors = total_sectors;
    map->tracked_sectors = 0;

    /* Calculate bitmap size (one bit per sector) */
    bitmap_size = BITS_TO_LONGS(total_sectors) * sizeof(unsigned long);

    /* Allocate tracking bitmap */
    map->tracking_bitmap = vzalloc(bitmap_size);
    if (!map->tracking_bitmap) {
        printk(KERN_ERR "dm-remap-health-map: Failed to allocate tracking bitmap\n");
        kfree(map);
        return -ENOMEM;
    }

    /* Allocate initial health data array */
    map->health_data = vzalloc(DMR_HEALTH_MAP_INITIAL_SIZE * sizeof(struct dmr_sector_health));
    if (!map->health_data) {
        printk(KERN_ERR "dm-remap-health-map: Failed to allocate health data\n");
        vfree(map->tracking_bitmap);
        kfree(map);
        return -ENOMEM;
    }

    /* Initialize synchronization */
    spin_lock_init(&map->health_lock);
    atomic_set(&map->updates_pending, 0);

    *health_map = map;

    printk(KERN_INFO "dm-remap-health-map: Health map initialized for %llu sectors\n",
           (unsigned long long)total_sectors);
    printk(KERN_INFO "dm-remap-health-map: Bitmap size: %zu bytes, "
           "initial health data: %zu bytes\n",
           bitmap_size, DMR_HEALTH_MAP_INITIAL_SIZE * sizeof(struct dmr_sector_health));

    return 0;
}

/**
 * dmr_health_map_cleanup - Clean up and free a health tracking map
 * @health_map: Health map to clean up
 * 
 * Frees all resources associated with a health tracking map.
 */
void dmr_health_map_cleanup(struct dmr_health_map *health_map)
{
    if (!health_map) {
        return;
    }

    /* Wait for pending updates to complete */
    while (atomic_read(&health_map->updates_pending) > 0) {
        msleep(1);
    }

    /* Free allocated memory */
    if (health_map->tracking_bitmap) {
        vfree(health_map->tracking_bitmap);
        health_map->tracking_bitmap = NULL;
    }

    if (health_map->health_data) {
        vfree(health_map->health_data);
        health_map->health_data = NULL;
    }

    /* Free main structure */
    kfree(health_map);

    printk(KERN_INFO "dm-remap-health-map: Health map cleaned up\n");
}

/**
 * dmr_find_health_slot - Find or allocate a slot for sector health data
 * @health_map: Health map instance
 * @sector: Sector number to find slot for
 * @create: Whether to create a new slot if not found
 * 
 * Finds the health data slot for a given sector. If create is true and
 * no slot exists, allocates a new one.
 * 
 * Returns: Index of health data slot, or negative error code
 */
static int dmr_find_health_slot(struct dmr_health_map *health_map, 
                               sector_t sector, bool create)
{
    unsigned long flags;
    sector_t slot_index;
    
    if (!health_map || sector >= health_map->total_sectors) {
        return -EINVAL;
    }

    spin_lock_irqsave(&health_map->health_lock, flags);

    /* Check if sector is already tracked */
    if (test_bit(sector, health_map->tracking_bitmap)) {
        /* Find the slot index for this sector */
        slot_index = bitmap_weight(health_map->tracking_bitmap, sector);
        spin_unlock_irqrestore(&health_map->health_lock, flags);
        return (int)slot_index;
    }

    if (!create) {
        spin_unlock_irqrestore(&health_map->health_lock, flags);
        return -ENOENT;
    }

    /* Check if we need to expand the health data array */
    if (health_map->tracked_sectors >= DMR_HEALTH_MAP_INITIAL_SIZE) {
        /* For now, limit to initial size - could implement expansion later */
        spin_unlock_irqrestore(&health_map->health_lock, flags);
        printk(KERN_WARNING "dm-remap-health-map: Health map full, "
               "cannot track sector %llu\n", (unsigned long long)sector);
        return -ENOSPC;
    }

    /* Mark sector as tracked */
    set_bit(sector, health_map->tracking_bitmap);
    slot_index = health_map->tracked_sectors;
    health_map->tracked_sectors++;

    spin_unlock_irqrestore(&health_map->health_lock, flags);

    return (int)slot_index;
}

/**
 * dmr_get_sector_health - Get health information for a sector
 * @health_map: Health map instance
 * @sector: Sector number
 * 
 * Retrieves the health information structure for the specified sector.
 * Returns NULL if the sector is not being tracked.
 * 
 * Returns: Pointer to sector health data, or NULL if not found
 */
struct dmr_sector_health *dmr_get_sector_health(struct dmr_health_map *health_map, 
                                               sector_t sector)
{
    int slot_index;

    if (!health_map) {
        return NULL;
    }

    slot_index = dmr_find_health_slot(health_map, sector, false);
    if (slot_index < 0) {
        return NULL;
    }

    return &health_map->health_data[slot_index];
}

/**
 * dmr_set_sector_health - Set health information for a sector
 * @health_map: Health map instance
 * @sector: Sector number
 * @health: Health data to set
 * 
 * Sets the health information for the specified sector. Creates a new
 * tracking entry if one doesn't exist.
 * 
 * Returns: 0 on success, negative error code on failure
 */
int dmr_set_sector_health(struct dmr_health_map *health_map, sector_t sector,
                         struct dmr_sector_health *health)
{
    int slot_index;
    unsigned long flags;

    if (!health_map || !health) {
        return -EINVAL;
    }

    /* Find or create slot for this sector */
    slot_index = dmr_find_health_slot(health_map, sector, true);
    if (slot_index < 0) {
        return slot_index;
    }

    /* Update health data atomically */
    atomic_inc(&health_map->updates_pending);

    spin_lock_irqsave(&health_map->health_lock, flags);
    memcpy(&health_map->health_data[slot_index], health, sizeof(*health));
    spin_unlock_irqrestore(&health_map->health_lock, flags);

    atomic_dec(&health_map->updates_pending);

    return 0;
}

/**
 * dmr_health_map_get_stats - Get statistics about the health map
 * @health_map: Health map instance
 * @total_tracked: Output for total tracked sectors
 * @memory_used: Output for memory usage in bytes
 * 
 * Retrieves statistics about the health map usage and memory consumption.
 * 
 * Returns: 0 on success, negative error code on failure
 */
int dmr_health_map_get_stats(struct dmr_health_map *health_map,
                            sector_t *total_tracked, size_t *memory_used)
{
    unsigned long flags;
    size_t bitmap_size, health_data_size;

    if (!health_map) {
        return -EINVAL;
    }

    spin_lock_irqsave(&health_map->health_lock, flags);

    if (total_tracked) {
        *total_tracked = health_map->tracked_sectors;
    }

    if (memory_used) {
        bitmap_size = BITS_TO_LONGS(health_map->total_sectors) * sizeof(unsigned long);
        health_data_size = DMR_HEALTH_MAP_INITIAL_SIZE * sizeof(struct dmr_sector_health);
        *memory_used = sizeof(*health_map) + bitmap_size + health_data_size;
    }

    spin_unlock_irqrestore(&health_map->health_lock, flags);

    return 0;
}

/**
 * dmr_health_map_iterate - Iterate over all tracked sectors
 * @health_map: Health map instance
 * @callback: Callback function to call for each sector
 * @context: Context pointer passed to callback
 * 
 * Iterates over all sectors with health tracking data, calling the
 * provided callback function for each one.
 * 
 * Returns: 0 on success, negative error code on failure
 */
int dmr_health_map_iterate(struct dmr_health_map *health_map,
                          int (*callback)(sector_t sector, 
                                        struct dmr_sector_health *health,
                                        void *context),
                          void *context)
{
    unsigned long flags;
    sector_t sector, slot_index = 0;
    int ret = 0;

    if (!health_map || !callback) {
        return -EINVAL;
    }

    spin_lock_irqsave(&health_map->health_lock, flags);

    /* Iterate through all set bits in the tracking bitmap */
    for_each_set_bit(sector, health_map->tracking_bitmap, health_map->total_sectors) {
        struct dmr_sector_health *health;

        if (slot_index >= health_map->tracked_sectors) {
            printk(KERN_ERR "dm-remap-health-map: Bitmap/data inconsistency detected\n");
            ret = -ENODATA;
            break;
        }

        health = &health_map->health_data[slot_index];
        
        /* Call callback with current sector and health data */
        ret = callback(sector, health, context);
        if (ret != 0) {
            break;
        }

        slot_index++;
    }

    spin_unlock_irqrestore(&health_map->health_lock, flags);

    return ret;
}

/**
 * dmr_health_map_clear_sector - Remove health tracking for a sector
 * @health_map: Health map instance
 * @sector: Sector to stop tracking
 * 
 * Removes health tracking data for the specified sector and compacts
 * the health data array.
 * 
 * Returns: 0 on success, negative error code on failure
 */
int dmr_health_map_clear_sector(struct dmr_health_map *health_map, sector_t sector)
{
    unsigned long flags;
    int slot_index;
    sector_t i;

    if (!health_map || sector >= health_map->total_sectors) {
        return -EINVAL;
    }

    spin_lock_irqsave(&health_map->health_lock, flags);

    /* Check if sector is tracked */
    if (!test_bit(sector, health_map->tracking_bitmap)) {
        spin_unlock_irqrestore(&health_map->health_lock, flags);
        return -ENOENT;
    }

    /* Find slot index */
    slot_index = bitmap_weight(health_map->tracking_bitmap, sector);

    /* Clear tracking bit */
    clear_bit(sector, health_map->tracking_bitmap);

    /* Compact health data array by shifting remaining entries */
    for (i = slot_index; i < health_map->tracked_sectors - 1; i++) {
        memcpy(&health_map->health_data[i], &health_map->health_data[i + 1],
               sizeof(struct dmr_sector_health));
    }

    /* Clear last entry */
    memset(&health_map->health_data[health_map->tracked_sectors - 1], 0,
           sizeof(struct dmr_sector_health));

    health_map->tracked_sectors--;

    spin_unlock_irqrestore(&health_map->health_lock, flags);

    return 0;
}

/**
 * dmr_health_map_compact - Compact the health map to reduce memory usage
 * @health_map: Health map instance
 * 
 * Compacts the health map by removing gaps and optimizing memory layout.
 * This is useful after many sectors have been cleared.
 * 
 * Returns: 0 on success, negative error code on failure
 */
int dmr_health_map_compact(struct dmr_health_map *health_map)
{
    unsigned long flags;
    sector_t sector, new_slot = 0;
    struct dmr_sector_health *new_data;

    if (!health_map) {
        return -EINVAL;
    }

    spin_lock_irqsave(&health_map->health_lock, flags);

    if (health_map->tracked_sectors == 0) {
        spin_unlock_irqrestore(&health_map->health_lock, flags);
        return 0;
    }

    /* Allocate new compact data array */
    new_data = vzalloc(health_map->tracked_sectors * sizeof(struct dmr_sector_health));
    if (!new_data) {
        spin_unlock_irqrestore(&health_map->health_lock, flags);
        return -ENOMEM;
    }

    /* Copy health data in order of tracked sectors */
    for_each_set_bit(sector, health_map->tracking_bitmap, health_map->total_sectors) {
        if (new_slot >= health_map->tracked_sectors) {
            printk(KERN_ERR "dm-remap-health-map: Compaction inconsistency\n");
            vfree(new_data);
            spin_unlock_irqrestore(&health_map->health_lock, flags);
            return -ENODATA;
        }

        /* Find current slot */
        sector_t current_slot = bitmap_weight(health_map->tracking_bitmap, sector);
        memcpy(&new_data[new_slot], &health_map->health_data[current_slot],
               sizeof(struct dmr_sector_health));
        new_slot++;
    }

    /* Replace old data with compacted data */
    vfree(health_map->health_data);
    health_map->health_data = new_data;

    spin_unlock_irqrestore(&health_map->health_lock, flags);

    printk(KERN_INFO "dm-remap-health-map: Health map compacted, "
           "%llu sectors tracked\n", (unsigned long long)health_map->tracked_sectors);

    return 0;
}

/* Debug and diagnostic functions */

/**
 * dmr_health_map_debug_dump - Dump health map state for debugging
 * @health_map: Health map instance
 * @max_entries: Maximum number of entries to dump
 * 
 * Dumps the current state of the health map for debugging purposes.
 * Limited to max_entries to avoid excessive log output.
 */
void dmr_health_map_debug_dump(struct dmr_health_map *health_map, int max_entries)
{
    unsigned long flags;
    sector_t sector, slot_index = 0;
    int dumped = 0;

    if (!health_map) {
        printk(KERN_INFO "dm-remap-health-map: Health map is NULL\n");
        return;
    }

    spin_lock_irqsave(&health_map->health_lock, flags);

    printk(KERN_INFO "dm-remap-health-map: Health Map Debug Dump\n");
    printk(KERN_INFO "  Total sectors: %llu\n", 
           (unsigned long long)health_map->total_sectors);
    printk(KERN_INFO "  Tracked sectors: %llu\n", 
           (unsigned long long)health_map->tracked_sectors);
    printk(KERN_INFO "  Updates pending: %d\n", 
           atomic_read(&health_map->updates_pending));

    printk(KERN_INFO "  Tracked sector details (showing up to %d):\n", max_entries);

    for_each_set_bit(sector, health_map->tracking_bitmap, health_map->total_sectors) {
        struct dmr_sector_health *health;

        if (dumped >= max_entries) {
            printk(KERN_INFO "    ... (truncated, %llu more sectors)\n",
                   (unsigned long long)(health_map->tracked_sectors - dumped));
            break;
        }

        if (slot_index >= health_map->tracked_sectors) {
            printk(KERN_ERR "    ERROR: Bitmap/data inconsistency at slot %llu\n",
                   (unsigned long long)slot_index);
            break;
        }

        health = &health_map->health_data[slot_index];
        
        printk(KERN_INFO "    Sector %llu: score=%u, errors=R%u/W%u, "
               "accesses=%u, risk=%u\n",
               (unsigned long long)sector, health->health_score,
               health->read_errors, health->write_errors,
               health->access_count, health->risk_level);

        slot_index++;
        dumped++;
    }

    spin_unlock_irqrestore(&health_map->health_lock, flags);
}