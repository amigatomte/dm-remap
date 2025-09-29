/*
 * dm-remap-io.h - I/O handling function declarations
 * 
 * This header declares the I/O processing functions used by dm-remap.
 * 
 * Author: Christian (with AI assistance)
 * License: GPL v2
 */

#ifndef DM_REMAP_IO_H
#define DM_REMAP_IO_H

#include <linux/device-mapper.h>  /* For struct dm_target */
#include <linux/bio.h>            /* For struct bio */

/*
 * remap_map() - Main I/O processing function
 * 
 * This is the .map function for our device mapper target.
 * It processes every I/O request and decides whether to remap it.
 * 
 * @ti: Device mapper target instance
 * @bio: Block I/O request to process
 * 
 * Returns: DM_MAPIO_* value indicating how the bio was handled
 */
int remap_map(struct dm_target *ti, struct bio *bio);

#endif /* DM_REMAP_IO_H */