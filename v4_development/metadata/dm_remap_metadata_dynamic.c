/* dm-remap v4.0 Dynamic Metadata Placement Implementation
 * 
 * Adaptive metadata placement for spare devices of any size
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/bio.h>
#include <linux/blkdev.h>

#include "dm_remap_metadata_v4.h"

/* ============================================================================
 * DYNAMIC PLACEMENT ALGORITHM IMPLEMENTATIONS
 * ============================================================================ */

/**
 * try_geometric_distribution - Attempt geometric spacing pattern
 * @spare_size_sectors: Available spare device size in sectors
 * @sectors_out: Output array for sector positions
 * @copies_count: Input/output for number of copies
 * Returns: true if geometric distribution succeeded, false otherwise
 */
static bool try_geometric_distribution(sector_t spare_size_sectors,
                                     sector_t *sectors_out,
                                     int *copies_count)
{
    const sector_t geometric_pattern[] = {0, 1024, 2048, 4096, 8192};
    int max_fit = 0;
    int i;
    
    /* Find how many geometric positions fit */
    for (i = 0; i < 5; i++) {
        if (geometric_pattern[i] + DM_REMAP_METADATA_SECTORS <= spare_size_sectors) {
            sectors_out[max_fit] = geometric_pattern[i];
            max_fit++;
        } else {
            break;
        }
    }
    
    if (max_fit >= DM_REMAP_METADATA_COPIES_MIN) {
        *copies_count = max_fit;
        return true;
    }
    
    return false;
}

/**
 * try_linear_distribution - Distribute copies evenly across spare device
 * @spare_size_sectors: Available spare device size in sectors
 * @sectors_out: Output array for sector positions
 * @copies_count: Input/output for number of copies
 * Returns: true if linear distribution succeeded, false otherwise
 */
static bool try_linear_distribution(sector_t spare_size_sectors,
                                  sector_t *sectors_out,
                                  int *copies_count)
{
    int desired_copies = *copies_count;
    sector_t min_spacing = DM_REMAP_METADATA_SECTORS;
    sector_t total_metadata_space = desired_copies * min_spacing;
    sector_t spacing;
    int actual_copies;
    int i;
    
    /* Check if desired copies fit with minimum spacing */
    if (total_metadata_space > spare_size_sectors) {
        /* Reduce copies to fit */
        actual_copies = spare_size_sectors / min_spacing;
        if (actual_copies < DM_REMAP_METADATA_COPIES_MIN) {
            return false;
        }
    } else {
        actual_copies = desired_copies;
    }
    
    /* Calculate even spacing */
    if (actual_copies > 1) {
        spacing = (spare_size_sectors - min_spacing) / (actual_copies - 1);
        if (spacing < min_spacing) {
            spacing = min_spacing;
        }
    } else {
        spacing = 0;
    }
    
    /* Place copies with calculated spacing */
    sectors_out[0] = 0;  /* Always start at sector 0 */
    for (i = 1; i < actual_copies; i++) {
        sectors_out[i] = i * spacing;
        
        /* Ensure we don't exceed spare device size */
        if (sectors_out[i] + min_spacing > spare_size_sectors) {
            actual_copies = i;  /* Truncate to what fits */
            break;
        }
    }
    
    *copies_count = actual_copies;
    return actual_copies >= DM_REMAP_METADATA_COPIES_MIN;
}

/**
 * try_minimal_distribution - Pack copies as tightly as possible
 * @spare_size_sectors: Available spare device size in sectors
 * @sectors_out: Output array for sector positions
 * @actual_copies: Output for actual number of copies placed
 * @max_copies: Input maximum desired copies
 * Returns: 0 on success, negative error code on failure
 */
static int try_minimal_distribution(sector_t spare_size_sectors,
                                  sector_t *sectors_out,
                                  int *actual_copies,
                                  int max_copies)
{
    sector_t min_spacing = DM_REMAP_METADATA_SECTORS;
    int copies_that_fit = spare_size_sectors / min_spacing;
    int i;
    
    if (copies_that_fit < 1) {
        return -ENOSPC;
    }
    
    /* Limit to reasonable maximum */
    if (copies_that_fit > max_copies) {
        copies_that_fit = max_copies;
    }
    
    /* Place copies with minimal spacing */
    for (i = 0; i < copies_that_fit; i++) {
        sectors_out[i] = i * min_spacing;
    }
    
    *actual_copies = copies_that_fit;
    return 0;
}

/**
 * calculate_dynamic_metadata_sectors - Calculate optimal metadata placement
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
    int actual_copies = desired_copies;
    sector_t min_spacing = DM_REMAP_METADATA_SECTORS;
    
    /* Ensure minimum spare size for metadata + actual spare sectors */
    /* Need: metadata space + minimum spare sectors for remapping */
    sector_t min_viable_size = min_spacing + 64;  /* 4KB metadata + 32KB spare minimum */
    if (spare_size_sectors < min_viable_size) {
        return -ENOSPC;  /* Too small for practical use */
    }
    
    /* Strategy 1: Try ideal geometric distribution */
    if (try_geometric_distribution(spare_size_sectors, sectors_out, 
                                  &actual_copies)) {
        *max_copies = actual_copies;
        return 0;
    }
    
    /* Strategy 2: Linear distribution with maximum spacing */
    actual_copies = desired_copies;
    if (try_linear_distribution(spare_size_sectors, sectors_out, 
                               &actual_copies)) {
        *max_copies = actual_copies;
        return 0;
    }
    
    /* Strategy 3: Minimal distribution (as many as fit) */
    return try_minimal_distribution(spare_size_sectors, sectors_out, 
                                   &actual_copies, desired_copies);
}

/* ============================================================================
 * STRATEGY DETECTION AND COMPATIBILITY
 * ============================================================================ */

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
    case PLACEMENT_STRATEGY_CUSTOM:
        return "custom";
    case PLACEMENT_STRATEGY_AUTO:
        return "auto";
    default:
        return "unknown";
    }
}

/**
 * detect_metadata_placement_strategy - Detect existing placement
 * @spare_bdev: Spare block device
 * @strategy_out: Detected strategy
 * @sectors_out: Detected sector locations
 * @copies_out: Number of copies found
 * Returns: 0 on success, negative error code on failure
 */
int detect_metadata_placement_strategy(struct block_device *spare_bdev,
                                     u32 *strategy_out,
                                     sector_t *sectors_out,
                                     int *copies_out)
{
    struct dm_remap_metadata_v4 *meta;
    const sector_t fixed_sectors[] = {0, 1024, 2048, 4096, 8192};
    int valid_copies = 0;
    int i;
    
    /* Try reading from fixed sector locations first (v4.0 compatibility) */
    for (i = 0; i < 5; i++) {
        if (read_single_metadata_copy(spare_bdev, fixed_sectors[i], &meta) == 0) {
            if (validate_metadata_copy(meta) == METADATA_VALID) {
                sectors_out[valid_copies] = fixed_sectors[i];
                valid_copies++;
                
                /* If this copy has dynamic placement info, use it */
                if (meta->header.total_copies > 0 && 
                    meta->header.placement_strategy != 0) {
                    *strategy_out = meta->header.placement_strategy;
                    *copies_out = meta->header.total_copies;
                    memcpy(sectors_out, meta->header.copy_sectors,
                           sizeof(sector_t) * meta->header.total_copies);
                    kfree(meta);
                    return 0;
                }
            }
            kfree(meta);
        }
    }
    
    /* Fallback: scan spare device for metadata signatures */
    if (valid_copies == 0) {
        return scan_for_metadata_copies(spare_bdev, sectors_out, copies_out);
    }
    
    /* Detected fixed placement strategy */
    *strategy_out = PLACEMENT_STRATEGY_GEOMETRIC;
    *copies_out = valid_copies;
    return 0;
}

/* ============================================================================
 * ENHANCED METADATA I/O WITH DYNAMIC PLACEMENT
 * ============================================================================ */

/**
 * get_device_size_sectors - Get device size in sectors
 * @bdev: Block device
 * Returns: Device size in sectors
 */
sector_t get_device_size_sectors(struct block_device *bdev)
{
    return bdev->bd_part->nr_sects;
}

/**
 * determine_placement_strategy - Determine best strategy for given placement
 * @copy_sectors: Array of sector locations
 * @copies_count: Number of copies
 * Returns: Strategy constant
 */
static u32 determine_placement_strategy(sector_t *copy_sectors, int copies_count)
{
    const sector_t geometric_pattern[] = {0, 1024, 2048, 4096, 8192};
    bool is_geometric = true;
    bool is_linear = true;
    sector_t spacing = 0;
    int i;
    
    if (copies_count < 2) {
        return PLACEMENT_STRATEGY_MINIMAL;
    }
    
    /* Check if it matches geometric pattern */
    for (i = 0; i < copies_count && i < 5; i++) {
        if (copy_sectors[i] != geometric_pattern[i]) {
            is_geometric = false;
            break;
        }
    }
    
    if (is_geometric) {
        return PLACEMENT_STRATEGY_GEOMETRIC;
    }
    
    /* Check if it's linear (even spacing) */
    if (copies_count >= 2) {
        spacing = copy_sectors[1] - copy_sectors[0];
        for (i = 2; i < copies_count; i++) {
            if (copy_sectors[i] - copy_sectors[i-1] != spacing) {
                is_linear = false;
                break;
            }
        }
    }
    
    if (is_linear) {
        return PLACEMENT_STRATEGY_LINEAR;
    }
    
    return PLACEMENT_STRATEGY_CUSTOM;
}

/**
 * write_redundant_metadata_v4_dynamic - Write with dynamic placement
 * @spare_bdev: Spare block device
 * @meta: Metadata to write
 * Returns: 0 on success, negative error code on failure
 */
int write_redundant_metadata_v4_dynamic(struct block_device *spare_bdev,
                                       struct dm_remap_metadata_v4 *meta)
{
    sector_t spare_size = get_device_size_sectors(spare_bdev);
    sector_t copy_sectors[DM_REMAP_METADATA_COPIES_MAX];
    int max_copies = DM_REMAP_METADATA_COPIES_DEFAULT;
    int actual_copies;
    int ret, i;
    
    /* Calculate optimal placement for this spare device size */
    ret = calculate_dynamic_metadata_sectors(spare_size, copy_sectors, 
                                           &max_copies);
    if (ret < 0) {
        printk(KERN_ERR "dm-remap: Cannot fit metadata on %llu-sector spare device\n",
               spare_size);
        return ret;
    }
    
    actual_copies = max_copies;
    
    /* Update metadata header with placement information */
    meta->header.total_copies = actual_copies;
    meta->header.spare_device_size = spare_size;
    meta->header.placement_strategy = determine_placement_strategy(copy_sectors, 
                                                                 actual_copies);
    
    /* Store copy sector locations in metadata */
    memset(meta->header.copy_sectors, 0, sizeof(meta->header.copy_sectors));
    memcpy(meta->header.copy_sectors, copy_sectors, 
           sizeof(sector_t) * actual_copies);
    
    /* Write all copies */
    for (i = 0; i < actual_copies; i++) {
        ret = write_single_metadata_copy(spare_bdev, copy_sectors[i], 
                                       meta, i);
        if (ret < 0) {
            printk(KERN_WARNING "dm-remap: Failed to write metadata copy %d "
                   "at sector %llu: %d\n", i, copy_sectors[i], ret);
        }
    }
    
    printk(KERN_INFO "dm-remap: Wrote %d metadata copies using %s strategy "
           "for %llu-sector spare device\n", actual_copies,
           get_placement_strategy_name(meta->header.placement_strategy), 
           spare_size);
    
    return 0;
}

/* ============================================================================
 * METADATA SCANNING AND RECOVERY
 * ============================================================================ */

/**
 * read_single_metadata_copy - Read one metadata copy from specific sector
 * @spare_bdev: Spare block device
 * @sector: Sector to read from
 * @meta_out: Output metadata structure
 * Returns: 0 on success, negative error code on failure
 */
int read_single_metadata_copy(struct block_device *spare_bdev,
                            sector_t sector,
                            struct dm_remap_metadata_v4 **meta_out)
{
    struct bio *bio;
    struct page *page;
    void *page_data;
    struct dm_remap_metadata_v4 *meta;
    int ret;
    
    /* Allocate metadata structure */
    meta = kmalloc(sizeof(*meta), GFP_KERNEL);
    if (!meta) {
        return -ENOMEM;
    }
    
    /* Allocate page for reading */
    page = alloc_page(GFP_KERNEL);
    if (!page) {
        kfree(meta);
        return -ENOMEM;
    }
    
    page_data = page_address(page);
    
    /* Create bio for read */
    bio = bio_alloc(GFP_KERNEL, 1);
    if (!bio) {
        __free_page(page);
        kfree(meta);
        return -ENOMEM;
    }
    
    bio_set_dev(bio, spare_bdev);
    bio->bi_iter.bi_sector = sector;
    bio_set_op_attrs(bio, REQ_OP_READ, 0);
    bio_add_page(bio, page, DM_REMAP_METADATA_SIZE, 0);
    
    /* Submit bio and wait for completion */
    ret = submit_bio_wait(bio);
    
    if (ret == 0) {
        /* Copy data to metadata structure */
        memcpy(meta, page_data, sizeof(*meta));
        *meta_out = meta;
    } else {
        kfree(meta);
    }
    
    bio_put(bio);
    __free_page(page);
    
    return ret;
}

/**
 * scan_for_metadata_copies - Scan spare device for metadata signatures
 * @spare_bdev: Spare block device
 * @sectors_out: Output array for found sector locations
 * @copies_out: Output for number of copies found
 * Returns: 0 on success, negative error code on failure
 */
int scan_for_metadata_copies(struct block_device *spare_bdev,
                           sector_t *sectors_out,
                           int *copies_out)
{
    sector_t spare_size = get_device_size_sectors(spare_bdev);
    sector_t sector;
    struct dm_remap_metadata_v4 *meta;
    int found_copies = 0;
    int ret;
    
    /* Scan every 8 sectors (4KB boundaries) looking for metadata */
    for (sector = 0; sector < spare_size && found_copies < DM_REMAP_METADATA_COPIES_MAX; 
         sector += DM_REMAP_METADATA_SECTORS) {
        
        ret = read_single_metadata_copy(spare_bdev, sector, &meta);
        if (ret == 0) {
            if (validate_metadata_copy(meta) == METADATA_VALID) {
                sectors_out[found_copies] = sector;
                found_copies++;
            }
            kfree(meta);
        }
        
        /* Yield CPU periodically during scan */
        if (sector % 1024 == 0) {
            cond_resched();
        }
    }
    
    *copies_out = found_copies;
    
    if (found_copies == 0) {
        return -ENOENT;
    }
    
    return 0;
}

/* Export new functions */
EXPORT_SYMBOL(calculate_dynamic_metadata_sectors);
EXPORT_SYMBOL(detect_metadata_placement_strategy);
EXPORT_SYMBOL(write_redundant_metadata_v4_dynamic);
EXPORT_SYMBOL(get_placement_strategy_name);
EXPORT_SYMBOL(get_device_size_sectors);
EXPORT_SYMBOL(read_single_metadata_copy);
EXPORT_SYMBOL(scan_for_metadata_copies);