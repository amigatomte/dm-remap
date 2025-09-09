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

#include <linux/device-mapper.h> // Device Mapper API for block device targets
#include <linux/spinlock.h>      // For concurrency protection
#include <linux/list.h>          // For multi-instance sysfs support
#include <linux/kobject.h>       // For sysfs integration

// remap_entry: describes a single remapped sector
struct remap_entry {
    sector_t orig_sector;                // Logical sector marked bad (to be remapped)
    struct dm_dev *spare_dev;            // Spare device used for remap (can be NULL for default)
    sector_t spare_sector;               // Physical sector on spare device
    bool valid;                          // Data validity flag (false = lost, true = valid)
};

// remap_c: per-target context for dm-remap
// Contains all runtime state, remap table, and sysfs/debugfs integration
struct remap_c {
    bool auto_remap_enabled;
    struct dm_dev *dev;                  // Main block device (user data)
    struct dm_dev *spare_dev;            // Spare block device (for remapping)
    sector_t start;                      // Start offset for usable sectors on main device
    sector_t spare_start;                // Start offset for spare sector pool
    int remap_count;                     // Number of sectors currently remapped
    int spare_used;                      // Number of spare sectors assigned
    sector_t spare_total;                // Total number of spare sectors available
    struct remap_entry *remaps;          // Remap table (dynamically allocated)
    spinlock_t lock;                     // Protects remap table and counters
    struct kobject *kobj;                // Sysfs kobject for per-target stats
    struct list_head list;               // Linked list node for global summary and multi-instance sysfs
    char last_reset_time[32];            // Human-readable timestamp of last reset
    atomic_t auto_remap_count;           // Number of sectors auto-remapped
    sector_t last_bad_sector;            // Last sector auto-remapped
};

#endif // _DM_REMAP_H
