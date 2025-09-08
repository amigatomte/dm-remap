// dm-remap.h

#ifndef _DM_REMAP_H
#define _DM_REMAP_H

#include <linux/device-mapper.h>


#define MAX_BADBLOCKS 1024

struct remap_entry {
    sector_t orig_sector;                // Original bad sector
    struct dm_dev *spare_dev;            // Spare device for remap
    sector_t spare_sector;               // Sector on spare device
    bool valid;                          // Data validity flag
};

struct remap_c {
    struct dm_dev *dev;                  // Main block device
    struct dm_dev *spare_dev;            // Spare block device
    sector_t start;                      // Start offset for usable sectors
    sector_t spare_start;                // Start offset for spare sector pool
    int remap_count;                     // Number of remapped sectors
    int spare_used;                      // Number of spare sectors assigned
    struct remap_entry remaps[MAX_BADBLOCKS]; // Remap table
    spinlock_t lock;                     // Concurrency protection
};

#endif
