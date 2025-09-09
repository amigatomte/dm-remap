#ifndef DM_REMAP_COMPAT_H
#define DM_REMAP_COMPAT_H

// Kernel compatibility shims for dm-remap

#include <linux/bio.h>

// Bio clone shims (fallback to generic APIs)
extern struct bio_set dm_remap_bioset;
static inline struct bio *dmr_bio_clone_shallow(struct bio *bio, gfp_t gfp)
{
    // Use bio_clone if bio_clone_fast is unavailable
    return bio_clone(bio, gfp);
}

static inline struct bio *dmr_bio_clone_deep(struct bio *bio, gfp_t gfp)
{
    // Use bio_alloc_bioset if bio_clone_bioset is unavailable
    return bio_alloc_bioset(bio->bi_bdev, bio->bi_iter.bi_size >> 9, bio->bi_opf, gfp, &dm_remap_bioset);
}

// Per-bio data accessor shim
#define dmr_per_bio_data(bio) dm_per_bio_data((bio))

// Per-bio data accessor shim
#define dmr_per_bio_data(bio, type) dm_per_bio_data((bio), type)

#endif // DM_REMAP_COMPAT_H
