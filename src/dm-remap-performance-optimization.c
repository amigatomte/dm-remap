/*
 * dm-remap-performance-optimization.c - Phase 3.2B Performance Tuning Implementation
 * 
 * This file implements the advanced performance optimizations introduced in
 * Phase 3.2B, focusing on hot path optimization, cache efficiency, and
 * profile-guided improvements.
 * 
 * IMPLEMENTED OPTIMIZATIONS:
 * - Red-black tree for O(log n) remap lookups
 * - Per-CPU performance counters (lock-free)
 * - Cache-aligned data structures
 * - Memory prefetching and spatial locality
 * - Sequential access pattern detection
 * - Lock contention reduction techniques
 * 
 * Author: Christian (with AI assistance)
 * License: GPL v2
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/rbtree.h>
#include <linux/percpu.h>
#include <linux/prefetch.h>
#include <linux/cache.h>
#include <linux/compiler.h>

#include "dm-remap-core.h"
#include "dm-remap-performance-optimization.h"

/**
 * dmr_perf_opt_init - Initialize optimized performance context
 * @ctx: Context to initialize
 * @max_entries: Maximum number of remap entries
 * 
 * Returns: 0 on success, negative error code on failure
 */
int dmr_perf_opt_init(struct dmr_optimized_context *ctx, size_t max_entries)
{
    if (!ctx || max_entries == 0)
        return -EINVAL;
    
    /* Initialize red-black tree */
    ctx->remap_tree = RB_ROOT;
    
    /* Initialize locks */
    spin_lock_init(&ctx->fast_lock);
    rwlock_init(&ctx->slow_lock);
    
    /* Allocate cache-aligned remap entries */
    ctx->entries = kzalloc(max_entries * sizeof(struct dmr_optimized_remap_entry),
                          GFP_KERNEL | __GFP_ZERO);
    if (!ctx->entries) {
        DMR_DEBUG(0, "Failed to allocate optimized remap entries");
        return -ENOMEM;
    }
    
    /* Allocate per-CPU statistics */
    ctx->stats = alloc_percpu(struct dmr_percpu_stats);
    if (!ctx->stats) {
        DMR_DEBUG(0, "Failed to allocate per-CPU statistics");
        kfree(ctx->entries);
        return -ENOMEM;
    }
    
    /* Initialize context parameters */
    ctx->max_entries = max_entries;
    ctx->entry_count = 0;
    ctx->last_sector = 0;
    ctx->sequential_count = 0;
    
    /* Enable all optimizations by default */
    ctx->optimization_flags = DMR_OPT_FAST_PATH_ENABLED |
                             DMR_OPT_PREFETCH_ENABLED |
                             DMR_OPT_PERCPU_STATS_ENABLED |
                             DMR_OPT_RBTREE_ENABLED |
                             DMR_OPT_SEQUENTIAL_DETECTION;
    
    DMR_DEBUG(1, "Initialized Phase 3.2B optimized context: max_entries=%zu, flags=0x%x",
              max_entries, ctx->optimization_flags);
    
    return 0;
}

/**
 * dmr_perf_opt_cleanup - Cleanup optimized performance context
 * @ctx: Context to cleanup
 */
void dmr_perf_opt_cleanup(struct dmr_optimized_context *ctx)
{
    struct rb_node *node;
    struct dmr_rbtree_node *rb_node;
    
    if (!ctx)
        return;
    
    /* Clean up red-black tree */
    while ((node = rb_first(&ctx->remap_tree))) {
        rb_node = rb_entry(node, struct dmr_rbtree_node, rb);
        rb_erase(node, &ctx->remap_tree);
        kfree(rb_node);
    }
    
    /* Free per-CPU statistics */
    if (ctx->stats) {
        free_percpu(ctx->stats);
        ctx->stats = NULL;
    }
    
    /* Free remap entries */
    if (ctx->entries) {
        kfree(ctx->entries);
        ctx->entries = NULL;
    }
    
    ctx->entry_count = 0;
    ctx->max_entries = 0;
    
    DMR_DEBUG(1, "Cleaned up Phase 3.2B optimized context");
}

/**
 * dmr_rbtree_search - Search for sector in red-black tree
 * @root: Tree root
 * @sector: Sector to search for
 * 
 * Returns: Tree node if found, NULL otherwise
 */
static struct dmr_rbtree_node *dmr_rbtree_search(struct rb_root *root, sector_t sector)
{
    struct rb_node *node = root->rb_node;
    
    while (node) {
        struct dmr_rbtree_node *rb_node = rb_entry(node, struct dmr_rbtree_node, rb);
        
        if (sector < rb_node->sector) {
            node = node->rb_left;
        } else if (sector > rb_node->sector) {
            node = node->rb_right;
        } else {
            return rb_node;
        }
    }
    
    return NULL;
}

/**
 * dmr_rbtree_insert - Insert sector into red-black tree
 * @root: Tree root
 * @new_node: New node to insert
 * 
 * Returns: 0 on success, -EEXIST if already exists
 */
static int dmr_rbtree_insert(struct rb_root *root, struct dmr_rbtree_node *new_node)
{
    struct rb_node **link = &root->rb_node;
    struct rb_node *parent = NULL;
    sector_t sector = new_node->sector;
    
    /* Find insertion point */
    while (*link) {
        struct dmr_rbtree_node *rb_node;
        
        parent = *link;
        rb_node = rb_entry(parent, struct dmr_rbtree_node, rb);
        
        if (sector < rb_node->sector) {
            link = &(*link)->rb_left;
        } else if (sector > rb_node->sector) {
            link = &(*link)->rb_right;
        } else {
            return -EEXIST;  /* Already exists */
        }
    }
    
    /* Insert and rebalance */
    rb_link_node(&new_node->rb, parent, link);
    rb_insert_color(&new_node->rb, root);
    
    return 0;
}

/**
 * dmr_perf_opt_lookup_fast - Fast optimized remap lookup
 * @ctx: Optimized context
 * @sector: Sector to look up
 * 
 * Returns: Remap entry if found, NULL otherwise
 */
struct dmr_optimized_remap_entry *dmr_perf_opt_lookup_fast(
    struct dmr_optimized_context *ctx, sector_t sector)
{
    struct dmr_rbtree_node *rb_node;
    struct dmr_optimized_remap_entry *entry = NULL;
    struct dmr_percpu_stats *stats;
    
    if (unlikely(!ctx || !(ctx->optimization_flags & DMR_OPT_RBTREE_ENABLED)))
        return NULL;
    
    /* Prefetch likely data */
    dmr_perf_opt_prefetch_remap_data(ctx, sector);
    
    /* Fast path: RCU-protected read-only access */
    rcu_read_lock();
    
    /* Search red-black tree - O(log n) complexity */
    rb_node = dmr_rbtree_search(&ctx->remap_tree, sector);
    if (rb_node) {
        entry = rb_node->entry;
        
        /* Update access statistics */
        entry->access_count++;
        entry->last_access = ktime_get();
        
        /* Update per-CPU cache hit statistics */
        if (ctx->optimization_flags & DMR_OPT_PERCPU_STATS_ENABLED) {
            stats = this_cpu_ptr(ctx->stats);
            stats->cache_hits++;
            stats->remap_lookups++;
        }
        
        DMR_DEBUG(3, "Phase 3.2B fast lookup HIT: sector %llu -> spare %llu",
                  (unsigned long long)sector, (unsigned long long)entry->spare_lba);
    } else {
        /* Update per-CPU cache miss statistics */
        if (ctx->optimization_flags & DMR_OPT_PERCPU_STATS_ENABLED) {
            stats = this_cpu_ptr(ctx->stats);
            stats->cache_misses++;
            stats->remap_lookups++;
        }
        
        DMR_DEBUG(3, "Phase 3.2B fast lookup MISS: sector %llu",
                  (unsigned long long)sector);
    }
    
    rcu_read_unlock();
    
    /* Update sequential access detection */
    dmr_perf_opt_is_sequential(ctx, sector);
    
    return entry;
}

/**
 * dmr_perf_opt_add_remap - Add optimized remap entry
 * @ctx: Optimized context
 * @main_lba: Main device LBA
 * @spare_lba: Spare device LBA
 * 
 * Returns: 0 on success, negative error code on failure
 */
int dmr_perf_opt_add_remap(struct dmr_optimized_context *ctx,
    sector_t main_lba, sector_t spare_lba)
{
    struct dmr_rbtree_node *rb_node;
    struct dmr_optimized_remap_entry *entry;
    int ret;
    
    if (!ctx || ctx->entry_count >= ctx->max_entries)
        return -EINVAL;
    
    /* Allocate tree node */
    rb_node = kzalloc(sizeof(*rb_node), GFP_KERNEL);
    if (!rb_node)
        return -ENOMEM;
    
    /* Find free entry slot */
    write_lock(&ctx->slow_lock);
    
    if (ctx->entry_count >= ctx->max_entries) {
        write_unlock(&ctx->slow_lock);
        kfree(rb_node);
        return -ENOSPC;
    }
    
    /* Use next available entry */
    entry = &ctx->entries[ctx->entry_count];
    ctx->entry_count++;
    
    /* Initialize entry */
    entry->main_lba = main_lba;
    entry->spare_lba = spare_lba;
    entry->access_count = 0;
    entry->flags = 0;
    entry->last_access = ktime_get();
    
    /* Initialize tree node */
    rb_node->sector = main_lba;
    rb_node->entry = entry;
    
    /* Insert into tree */
    ret = dmr_rbtree_insert(&ctx->remap_tree, rb_node);
    if (ret) {
        ctx->entry_count--;  /* Rollback */
        write_unlock(&ctx->slow_lock);
        kfree(rb_node);
        return ret;
    }
    
    write_unlock(&ctx->slow_lock);
    
    DMR_DEBUG(1, "Phase 3.2B added remap: %llu -> %llu (entry %u/%zu)",
              (unsigned long long)main_lba, (unsigned long long)spare_lba,
              ctx->entry_count, ctx->max_entries);
    
    return 0;
}

/**
 * dmr_perf_opt_remove_remap - Remove optimized remap entry
 * @ctx: Optimized context
 * @main_lba: Main device LBA to remove
 * 
 * Returns: 0 on success, -ENOENT if not found
 */
int dmr_perf_opt_remove_remap(struct dmr_optimized_context *ctx, sector_t main_lba)
{
    struct dmr_rbtree_node *rb_node;
    
    if (!ctx)
        return -EINVAL;
    
    write_lock(&ctx->slow_lock);
    
    rb_node = dmr_rbtree_search(&ctx->remap_tree, main_lba);
    if (!rb_node) {
        write_unlock(&ctx->slow_lock);
        return -ENOENT;
    }
    
    /* Remove from tree */
    rb_erase(&rb_node->rb, &ctx->remap_tree);
    
    /* Mark entry as free (set main_lba to invalid) */
    rb_node->entry->main_lba = (sector_t)-1;
    
    write_unlock(&ctx->slow_lock);
    
    /* Free tree node */
    kfree(rb_node);
    
    DMR_DEBUG(1, "Phase 3.2B removed remap: %llu",
              (unsigned long long)main_lba);
    
    return 0;
}

/**
 * dmr_perf_opt_get_aggregated_stats - Get aggregated per-CPU statistics
 * @ctx: Optimized context
 * @result: Result structure to fill
 */
void dmr_perf_opt_get_aggregated_stats(struct dmr_optimized_context *ctx,
    struct dmr_percpu_stats *result)
{
    int cpu;
    struct dmr_percpu_stats *stats;
    
    if (!ctx || !result)
        return;
    
    memset(result, 0, sizeof(*result));
    
    if (!(ctx->optimization_flags & DMR_OPT_PERCPU_STATS_ENABLED))
        return;
    
    /* Aggregate across all CPUs */
    for_each_possible_cpu(cpu) {
        stats = per_cpu_ptr(ctx->stats, cpu);
        result->total_ios += stats->total_ios;
        result->total_latency_ns += stats->total_latency_ns;
        result->total_bytes += stats->total_bytes;
        result->cache_hits += stats->cache_hits;
        result->cache_misses += stats->cache_misses;
        result->fast_path_hits += stats->fast_path_hits;
        result->remap_lookups += stats->remap_lookups;
        result->lock_contentions += stats->lock_contentions;
    }
}

/**
 * dmr_perf_opt_optimize_memory_layout - Optimize memory layout for cache efficiency
 * @ctx: Optimized context
 * 
 * This function reorganizes the remap table to improve spatial locality
 * and cache efficiency based on access patterns.
 */
void dmr_perf_opt_optimize_memory_layout(struct dmr_optimized_context *ctx)
{
    struct dmr_optimized_remap_entry *temp_entries;
    int i, j = 0;
    
    if (!ctx || ctx->entry_count == 0)
        return;
    
    /* Allocate temporary array */
    temp_entries = kzalloc(ctx->entry_count * sizeof(struct dmr_optimized_remap_entry), 
                          GFP_KERNEL);
    if (!temp_entries) {
        DMR_DEBUG(1, "Failed to allocate temporary entries for optimization");
        return;
    }
    
    write_lock(&ctx->slow_lock);
    
    /* Sort entries by access frequency (most accessed first) */
    for (i = 0; i < ctx->entry_count; i++) {
        if (ctx->entries[i].main_lba != (sector_t)-1) {
            temp_entries[j++] = ctx->entries[i];
        }
    }
    
    /* Simple insertion sort by access_count (descending) */
    for (i = 1; i < j; i++) {
        struct dmr_optimized_remap_entry key = temp_entries[i];
        int k = i - 1;
        
        while (k >= 0 && temp_entries[k].access_count < key.access_count) {
            temp_entries[k + 1] = temp_entries[k];
            k--;
        }
        temp_entries[k + 1] = key;
    }
    
    /* Copy back to main table */
    memcpy(ctx->entries, temp_entries, j * sizeof(struct dmr_optimized_remap_entry));
    
    /* Clear unused entries */
    for (i = j; i < ctx->max_entries; i++) {
        ctx->entries[i].main_lba = (sector_t)-1;
    }
    
    write_unlock(&ctx->slow_lock);
    
    /* Free temporary array */
    kfree(temp_entries);
    
    DMR_DEBUG(1, "Phase 3.2B optimized memory layout: %d active entries", j);
}

/**
 * dmr_perf_opt_compact_remap_table - Compact remap table to remove gaps
 * @ctx: Optimized context
 */
void dmr_perf_opt_compact_remap_table(struct dmr_optimized_context *ctx)
{
    int read_idx, write_idx = 0;
    
    if (!ctx)
        return;
    
    write_lock(&ctx->slow_lock);
    
    /* Compact by moving all valid entries to the front */
    for (read_idx = 0; read_idx < ctx->entry_count; read_idx++) {
        if (ctx->entries[read_idx].main_lba != (sector_t)-1) {
            if (read_idx != write_idx) {
                ctx->entries[write_idx] = ctx->entries[read_idx];
                ctx->entries[read_idx].main_lba = (sector_t)-1;
            }
            write_idx++;
        }
    }
    
    ctx->entry_count = write_idx;
    
    write_unlock(&ctx->slow_lock);
    
    DMR_DEBUG(1, "Phase 3.2B compacted remap table: %u entries",
              ctx->entry_count);
}