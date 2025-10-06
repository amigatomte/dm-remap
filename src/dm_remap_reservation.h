/* dm-remap v4.0 Reservation System Header
 * 
 * Function declarations for metadata sector reservation system
 */

#ifndef _DM_REMAP_RESERVATION_H
#define _DM_REMAP_RESERVATION_H

#include <linux/types.h>
#include "dm-remap-core.h"

/* Constants for dynamic metadata placement */
#define DM_REMAP_METADATA_SECTORS         8    /* Sectors per metadata copy */
#define PLACEMENT_STRATEGY_IMPOSSIBLE     0    /* Too small for metadata */
#define PLACEMENT_STRATEGY_MINIMAL        1    /* Minimal copies, tight packing */
#define PLACEMENT_STRATEGY_LINEAR         2    /* Even distribution */
#define PLACEMENT_STRATEGY_GEOMETRIC      3    /* Geometric spacing */
#define SECTOR_MAX                        ((sector_t)-1)  /* Invalid sector marker */

/* Reservation system functions */
int dmr_init_reservation_system(struct remap_c *rc);
void dmr_cleanup_reservation_system(struct remap_c *rc);

int dmr_reserve_metadata_sectors(struct remap_c *rc, 
                                sector_t *metadata_sectors,
                                int count, 
                                int sectors_per_copy);

sector_t dmr_allocate_spare_sector(struct remap_c *rc);
bool dmr_check_sector_reserved(struct remap_c *rc, sector_t sector);

/* Dynamic metadata integration */
int dmr_setup_dynamic_metadata_reservations(struct remap_c *rc);

/* Statistics and debugging */
void dmr_get_reservation_stats(struct remap_c *rc, 
                              sector_t *total_sectors,
                              sector_t *reserved_sectors, 
                              sector_t *available_sectors);

void dmr_print_reservation_map(struct remap_c *rc, sector_t max_sectors);

/* External functions from dynamic metadata implementation */
extern int calculate_dynamic_metadata_sectors(sector_t spare_size_sectors,
                                            sector_t *sectors_out,
                                            int *max_copies);

extern const char *get_placement_strategy_name(u32 strategy);

#endif /* _DM_REMAP_RESERVATION_H */