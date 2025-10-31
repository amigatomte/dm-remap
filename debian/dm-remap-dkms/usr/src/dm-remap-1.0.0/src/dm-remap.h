// dm-remap.h
// Header for dm-remap kernel module
// Defines core data structures for remapping bad sectors to spare sectors
// Used by dm-remap.c and any user-space tools interacting with the target
//
// Key concepts:
// - remap_entry: describes a mapping from a bad sector to a spare sector
// - remap_c: per-target context, tracks all remaps and runtime state
// - All fields are documented for clarity

#ifndef _DM_REMAP_H
#define _DM_REMAP_H

#include <linux/types.h>
#include <linux/device-mapper.h> // Device Mapper API for block device targets
#include <linux/spinlock.h>      // For concurrency protection
#include <linux/list.h>          // For multi-instance sysfs support
#include <linux/kobject.h>       // For sysfs integration

// Per-IO context for remap operations
struct remap_io_ctx {
    sector_t lba;
    bool was_write;
    bool retry_to_spare;
};


// v1 remap table entry
struct remap_entry {
    sector_t main_lba;
    sector_t spare_lba;
};

// v1 context
struct remap_c {
    struct dm_dev *main_dev;
    struct dm_dev *spare_dev;
    sector_t main_start;
    sector_t spare_start;
    sector_t spare_len;
    sector_t spare_used;
    struct remap_entry *table;
    spinlock_t lock;
};

#endif // _DM_REMAP_H
