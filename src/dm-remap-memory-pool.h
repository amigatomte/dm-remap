/*
 * dm-remap-memory-pool.h - Optimized Memory Management for dm-remap v4.0
 * 
 * Week 9 Optimization: Memory Pool System
 * Provides efficient memory allocation pools for frequently allocated objects
 * to reduce memory fragmentation and improve performance.
 * 
 * Author: Christian (with AI assistance)  
 * License: GPL v2
 */

#ifndef DM_REMAP_MEMORY_POOL_H
#define DM_REMAP_MEMORY_POOL_H

#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/slab.h>

/* Forward declarations */
struct remap_c;
struct dmr_sector_health;
struct dmr_bio_context;

/*
 * MEMORY POOL CONFIGURATION
 */
#define DMR_POOL_MIN_OBJECTS        32      /* Minimum objects per pool */
#define DMR_POOL_MAX_OBJECTS        512     /* Maximum objects per pool */
#define DMR_POOL_GROWTH_BATCH       16      /* Objects to allocate per batch */

/*
 * Memory pool types for different object sizes
 */
enum dmr_pool_type {
    DMR_POOL_HEALTH_RECORD = 0,     /* Health record structures */
    DMR_POOL_BIO_CONTEXT,           /* Bio tracking contexts */
    DMR_POOL_WORK_ITEMS,            /* Work queue items */
    DMR_POOL_SMALL_BUFFERS,         /* Small temporary buffers */
    DMR_POOL_TYPE_MAX
};

/*
 * Memory pool statistics for monitoring
 */
struct dmr_pool_stats {
    atomic_t allocations;           /* Total allocations */
    atomic_t deallocations;         /* Total deallocations */
    atomic_t pool_hits;             /* Successful pool allocations */
    atomic_t pool_misses;           /* Fallback to kmalloc */
    atomic_t pool_grows;            /* Pool expansion events */
    atomic_t pool_shrinks;          /* Pool contraction events */
};

/*
 * Memory pool object header (internal use)
 */
struct dmr_pool_object {
    struct list_head list;          /* Free list linkage */
    u32 magic;                      /* Corruption detection */
    u32 pool_type;                  /* Pool type identifier */
};

/*
 * Memory pool structure
 */
struct dmr_memory_pool {
    spinlock_t lock;                /* Pool access lock */
    struct list_head free_list;     /* Free objects list */
    size_t object_size;             /* Size of each object */
    int current_objects;            /* Current object count */
    int min_objects;                /* Minimum pool size */
    int max_objects;                /* Maximum pool size */
    enum dmr_pool_type type;        /* Pool type */
    struct dmr_pool_stats stats;    /* Pool statistics */
    struct kmem_cache *cache;       /* Slab cache for objects */
};

/*
 * Memory pool manager
 */
struct dmr_pool_manager {
    struct dmr_memory_pool pools[DMR_POOL_TYPE_MAX];
    atomic_t total_memory;          /* Total pool memory usage */
    bool emergency_mode;            /* Emergency low-memory mode */
    struct work_struct cleanup_work; /* Periodic cleanup work */
};

/* Memory pool management functions */
int dmr_pool_manager_init(struct remap_c *rc);
void dmr_pool_manager_cleanup(struct remap_c *rc);

/* Object allocation/deallocation */
void *dmr_pool_alloc(struct remap_c *rc, enum dmr_pool_type type, gfp_t flags);
void dmr_pool_free(struct remap_c *rc, enum dmr_pool_type type, void *object);

/* Pool management */
int dmr_pool_grow(struct remap_c *rc, enum dmr_pool_type type, int count);
int dmr_pool_shrink(struct remap_c *rc, enum dmr_pool_type type, int count);
void dmr_pool_emergency_mode(struct remap_c *rc, bool enable);

/* Statistics and monitoring */
void dmr_pool_get_stats(struct remap_c *rc, enum dmr_pool_type type, 
                       struct dmr_pool_stats *stats);
size_t dmr_pool_get_memory_usage(struct remap_c *rc);

/* Utility macros for type-safe allocation */
#define dmr_alloc_health_record(rc) \
    ((struct dmr_sector_health *)dmr_pool_alloc(rc, DMR_POOL_HEALTH_RECORD, GFP_KERNEL))

#define dmr_free_health_record(rc, record) \
    dmr_pool_free(rc, DMR_POOL_HEALTH_RECORD, record)

#define dmr_alloc_bio_context(rc) \
    ((struct dmr_bio_context *)dmr_pool_alloc(rc, DMR_POOL_BIO_CONTEXT, GFP_NOIO))

#define dmr_free_bio_context(rc, ctx) \
    dmr_pool_free(rc, DMR_POOL_BIO_CONTEXT, ctx)

#endif /* DM_REMAP_MEMORY_POOL_H */