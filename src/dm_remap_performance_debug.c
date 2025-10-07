/* Enhanced performance optimization with debugging and fixes
 * This is a temporary debug version to identify the performance bottleneck
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/bitmap.h>
#include <linux/slab.h>
#include <linux/atomic.h>

#include "dm-remap-core.h"
#include "dm_remap_reservation.h"
#include "dm_remap_performance.h"

/* Debug version constants */
#define DMR_ALLOCATION_CACHE_SIZE 64
#define DMR_SEARCH_BATCH_SIZE 32
#define DMR_MAX_SEARCH_ITERATIONS 1000

/* Full cache structure */
struct dmr_allocation_cache {
    sector_t cached_sectors[DMR_ALLOCATION_CACHE_SIZE];
    int cache_head;
    int cache_tail;
    int cache_count;
    spinlock_t cache_lock;
    atomic_t cache_hits;
    atomic_t cache_misses;
};

/**
 * dmr_init_allocation_cache_debug - Initialize allocation cache with debugging
 */
int dmr_init_allocation_cache_debug(struct remap_c *rc)
{
    struct dmr_allocation_cache *cache;
    
    printk(KERN_INFO "dm-remap: [DEBUG] Starting cache initialization\n");
    
    cache = kzalloc(sizeof(struct dmr_allocation_cache), GFP_KERNEL);
    if (!cache) {
        printk(KERN_ERR "dm-remap: [DEBUG] Cache allocation failed\n");
        return -ENOMEM;
    }
    
    cache->cache_head = 0;
    cache->cache_tail = 0;
    cache->cache_count = 0;
    spin_lock_init(&cache->cache_lock);
    atomic_set(&cache->cache_hits, 0);
    atomic_set(&cache->cache_misses, 0);
    
    rc->allocation_cache = cache;
    
    printk(KERN_INFO "dm-remap: [DEBUG] Cache structure allocated and initialized\n");
    printk(KERN_INFO "dm-remap: [DEBUG] Cache size: %lu bytes\n", sizeof(struct dmr_allocation_cache));
    printk(KERN_INFO "dm-remap: [DEBUG] Spare length: %llu sectors\n", (unsigned long long)rc->spare_len);
    
    return 0;
}

/**
 * dmr_refill_allocation_cache_debug - Debug version of cache refill
 */
static void dmr_refill_allocation_cache_debug(struct remap_c *rc)
{
    struct dmr_allocation_cache *cache = rc->allocation_cache;
    sector_t candidate = rc->next_spare_sector;
    sector_t max_search = rc->spare_len;
    int found_count = 0;
    int iterations = 0;
    
    printk(KERN_INFO "dm-remap: [DEBUG] Starting cache refill\n");
    printk(KERN_INFO "dm-remap: [DEBUG] Current cache count: %d\n", cache->cache_count);
    printk(KERN_INFO "dm-remap: [DEBUG] Search starting at sector: %llu\n", (unsigned long long)candidate);
    printk(KERN_INFO "dm-remap: [DEBUG] Max search sectors: %llu\n", (unsigned long long)max_search);
    
    if (!cache || cache->cache_count >= DMR_ALLOCATION_CACHE_SIZE / 2) {
        printk(KERN_INFO "dm-remap: [DEBUG] Cache refill skipped (sufficient sectors or no cache)\n");
        return;
    }
    
    /* Simple linear search for available sectors */
    while (found_count < (DMR_ALLOCATION_CACHE_SIZE - cache->cache_count) && 
           iterations < DMR_MAX_SEARCH_ITERATIONS && candidate < max_search) {
        
        if (!test_bit(candidate, rc->reserved_sectors)) {
            /* Found available sector */
            int cache_idx = (cache->cache_tail + found_count) % DMR_ALLOCATION_CACHE_SIZE;
            cache->cached_sectors[cache_idx] = candidate;
            found_count++;
            
            if (found_count <= 5) {  /* Log first few sectors found */
                printk(KERN_INFO "dm-remap: [DEBUG] Found free sector %llu (cache slot %d)\n", 
                       (unsigned long long)candidate, cache_idx);
            }
        }
        
        candidate++;
        iterations++;
        
        if (iterations % 100 == 0) {
            printk(KERN_INFO "dm-remap: [DEBUG] Search progress: %d iterations, %d found\n", 
                   iterations, found_count);
        }
    }
    
    /* Update cache metadata */
    cache->cache_tail = (cache->cache_tail + found_count) % DMR_ALLOCATION_CACHE_SIZE;
    cache->cache_count += found_count;
    
    if (found_count > 0) {
        rc->next_spare_sector = candidate;
    }
    
    printk(KERN_INFO "dm-remap: [DEBUG] Cache refill complete: %d sectors added, total cache: %d\n", 
           found_count, cache->cache_count);
}

/**
 * dmr_allocate_spare_sector_optimized_debug - Debug version of optimized allocation
 */
sector_t dmr_allocate_spare_sector_optimized_debug(struct remap_c *rc)
{
    struct dmr_allocation_cache *cache;
    sector_t allocated_sector;
    unsigned long flags;
    ktime_t start_time, end_time;
    s64 elapsed_ns;
    
    start_time = ktime_get();
    
    printk(KERN_INFO "dm-remap: [DEBUG] Starting optimized allocation\n");
    
    if (!rc || !rc->reserved_sectors) {
        printk(KERN_ERR "dm-remap: [DEBUG] Invalid parameters (rc=%p, reserved_sectors=%p)\n", 
               rc, rc ? rc->reserved_sectors : NULL);
        return SECTOR_MAX;
    }
    
    cache = rc->allocation_cache;
    if (!cache) {
        printk(KERN_WARNING "dm-remap: [DEBUG] No cache available, falling back to original algorithm\n");
        return dmr_allocate_spare_sector(rc);
    }
    
    printk(KERN_INFO "dm-remap: [DEBUG] Cache available, checking for cached sectors\n");
    
    /* Try to get sector from cache first */
    spin_lock_irqsave(&cache->cache_lock, flags);
    
    printk(KERN_INFO "dm-remap: [DEBUG] Cache status: count=%d, head=%d, tail=%d\n", 
           cache->cache_count, cache->cache_head, cache->cache_tail);
    
    if (cache->cache_count > 0) {
        /* Cache hit */
        allocated_sector = cache->cached_sectors[cache->cache_head];
        cache->cache_head = (cache->cache_head + 1) % DMR_ALLOCATION_CACHE_SIZE;
        cache->cache_count--;
        
        atomic_inc(&cache->cache_hits);
        spin_unlock_irqrestore(&cache->cache_lock, flags);
        
        printk(KERN_INFO "dm-remap: [DEBUG] Cache hit! Allocated sector %llu\n", 
               (unsigned long long)allocated_sector);
        
        /* Mark sector as allocated */
        set_bit(allocated_sector, rc->reserved_sectors);
        
        end_time = ktime_get();
        elapsed_ns = ktime_to_ns(ktime_sub(end_time, start_time));
        printk(KERN_INFO "dm-remap: [DEBUG] Fast allocation completed in %lld ns\n", elapsed_ns);
        
        return rc->spare_start + allocated_sector;
    }
    
    spin_unlock_irqrestore(&cache->cache_lock, flags);
    
    /* Cache miss - refill and try again */
    printk(KERN_INFO "dm-remap: [DEBUG] Cache miss, refilling cache\n");
    atomic_inc(&cache->cache_misses);
    
    dmr_refill_allocation_cache_debug(rc);
    
    /* Try cache again after refill */
    spin_lock_irqsave(&cache->cache_lock, flags);
    
    if (cache->cache_count > 0) {
        allocated_sector = cache->cached_sectors[cache->cache_head];
        cache->cache_head = (cache->cache_head + 1) % DMR_ALLOCATION_CACHE_SIZE;
        cache->cache_count--;
        
        spin_unlock_irqrestore(&cache->cache_lock, flags);
        
        printk(KERN_INFO "dm-remap: [DEBUG] Cache refill successful! Allocated sector %llu\n", 
               (unsigned long long)allocated_sector);
        
        set_bit(allocated_sector, rc->reserved_sectors);
        
        end_time = ktime_get();
        elapsed_ns = ktime_to_ns(ktime_sub(end_time, start_time));
        printk(KERN_INFO "dm-remap: [DEBUG] Allocation completed in %lld ns (with refill)\n", elapsed_ns);
        
        return rc->spare_start + allocated_sector;
    }
    
    spin_unlock_irqrestore(&cache->cache_lock, flags);
    
    /* No sectors available */
    printk(KERN_WARNING "dm-remap: [DEBUG] No free sectors found, all reserved or exhausted\n");
    return SECTOR_MAX;
}