#ifndef DM_REMAP_DEBUG_H
#define DM_REMAP_DEBUG_H

#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include "dm-remap-core.h"

/* Debug interface for testing remap entries */
extern struct dentry *dmr_debug_dir;

/* Function prototypes */
int dmr_debug_init(void);
void dmr_debug_exit(void);
int dmr_debug_add_target(struct remap_c *rc, const char *name);
void dmr_debug_remove_target(struct remap_c *rc);

/* Manual remap control for testing */
ssize_t dmr_debug_remap_write(struct file *file, const char __user *buf, 
                             size_t count, loff_t *ppos);

#endif /* DM_REMAP_DEBUG_H */