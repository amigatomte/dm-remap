/* compat.h - API shims for dm-remap across kernel versions */

#ifndef DM_REMAP_COMPAT_H
#define DM_REMAP_COMPAT_H

#include <linux/version.h>
#include <linux/device-mapper.h>
#include <linux/bio.h>

/* Usage counters for runtime reporting */
extern unsigned long dmr_clone_shallow_count;
extern unsigned long dmr_clone_deep_count;

/*
 * Per-bio data shim:
 * - Newer kernels (>= 6.14): dm_per_bio_data() takes (bio, size)
 * - Older kernels: dm_per_bio_data() takes only (bio)
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,14,0)
#define dmr_per_bio_data(bio, type) \
    ((type *)dm_per_bio_data((bio), sizeof(type)))
#else
#define dmr_per_bio_data(bio, type) \
    ((type *)dm_per_bio_data((bio)))
#endif

/*
 * Bio clone shims:
 * - 6.14+: bio_alloc_clone(bdev, bio_src, gfp, bioset)
 * - 6.12–6.13: bio_dup() / bio_alloc_clone()
 * - Older: bio_clone_fast() / bio_clone_bioset()
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,14,0)

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

#else /* very old kernels */

extern struct bio_set dm_remap_bioset;

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

#endif /* kernel version check */

#endif /* DM_REMAP_COMPAT_H */
