/*
 * dm-remap-debug.c - Debug interface for dm-remap testing
 *
 * This provides a simple debugfs interface for testing remap functionality.
 */

#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/string.h>
#include "dm-remap-debug.h"
#include "dm-remap-core.h"
#include "dm-remap-messages.h"

struct dentry *dmr_debug_dir = NULL;
static struct remap_c *debug_target = NULL;

/*
 * dmr_debug_remap_write() - Handle remap control commands
 * 
 * Commands:
 *   "add <main_sector> <spare_sector>" - Add a remap entry
 *   "remove <main_sector>" - Remove a remap entry
 *   "list" - List all remap entries
 */
ssize_t dmr_debug_remap_write(struct file *file, const char __user *buf, 
                             size_t count, loff_t *ppos)
{
    char *kbuf, *kbuf_orig, *cmd, *arg1, *arg2;
    sector_t main_sector, spare_sector;
    int i;
    
    if (!debug_target) {
        DMR_DEBUG(0, "No debug target set");
        return -ENODEV;
    }
    
    /* Allocate kernel buffer */
    kbuf_orig = kzalloc(count + 1, GFP_KERNEL);
    if (!kbuf_orig)
        return -ENOMEM;
    
    /* Copy from user space */
    if (copy_from_user(kbuf_orig, buf, count)) {
        kfree(kbuf_orig);
        return -EFAULT;
    }
    
    /* Create working copy for parsing */
    kbuf = kbuf_orig;
    
    /* Parse command */
    cmd = strsep(&kbuf, " \n");
    if (!cmd) {
        kfree(kbuf_orig);
        return -EINVAL;
    }
    
    if (strcmp(cmd, "add") == 0) {
        arg1 = strsep(&kbuf, " \n");
        arg2 = strsep(&kbuf, " \n");
        
        if (!arg1 || !arg2) {
            DMR_DEBUG(0, "Usage: add <main_sector> <spare_sector>");
            kfree(kbuf_orig);
            return -EINVAL;
        }
        
        if (kstrtoull(arg1, 10, &main_sector) || kstrtoull(arg2, 10, &spare_sector)) {
            DMR_DEBUG(0, "Invalid sector numbers");
            kfree(kbuf_orig);
            return -EINVAL;
        }
        
        /* Add remap entry manually */
        spin_lock(&debug_target->lock);
        
        if (debug_target->spare_used >= debug_target->spare_len) {
            spin_unlock(&debug_target->lock);
            DMR_DEBUG(0, "Remap table full");
            kfree(kbuf_orig);
            return -ENOSPC;
        }
        
        /* Add to table */
        i = debug_target->spare_used;
        debug_target->table[i].main_lba = main_sector;
        debug_target->table[i].spare_lba = spare_sector;
        debug_target->table[i].error_count = 0;
        debug_target->table[i].last_error_time = jiffies;
        debug_target->spare_used++;
        
        spin_unlock(&debug_target->lock);
        
        DMR_DEBUG(0, "DEBUG: Added remap %llu -> %llu", 
                  (unsigned long long)main_sector, (unsigned long long)spare_sector);
        
    } else if (strcmp(cmd, "list") == 0) {
        spin_lock(&debug_target->lock);
        DMR_DEBUG(0, "DEBUG: Remap table (%llu entries):", (unsigned long long)debug_target->spare_used);
        for (i = 0; i < debug_target->spare_used; i++) {
            DMR_DEBUG(0, "  [%d] %llu -> %llu", i,
                      (unsigned long long)debug_target->table[i].main_lba,
                      (unsigned long long)debug_target->table[i].spare_lba);
        }
        spin_unlock(&debug_target->lock);
        
    } else {
        DMR_DEBUG(0, "Unknown command: %s", cmd);
        kfree(kbuf_orig);
        return -EINVAL;
    }
    
    kfree(kbuf_orig);
    return count;
}

static const struct file_operations dmr_debug_remap_fops = {
    .write = dmr_debug_remap_write,
};

/*
 * dmr_debug_add_target() - Add target to debug interface
 */
int dmr_debug_add_target(struct remap_c *rc, const char *name)
{
    struct dentry *remap_file;
    
    if (!dmr_debug_dir)
        return -ENODEV;
    
    /* For simplicity, support only one target at a time */
    debug_target = rc;
    
    /* Create remap control file */
    remap_file = debugfs_create_file("remap_control", 0200, dmr_debug_dir, 
                                    NULL, &dmr_debug_remap_fops);
    if (!remap_file) {
        DMR_DEBUG(0, "Failed to create remap_control file");
        return -ENOMEM;
    }
    
    DMR_DEBUG(1, "Created debug interface for target %s", name);
    return 0;
}

/*
 * dmr_debug_remove_target() - Remove target from debug interface
 */
void dmr_debug_remove_target(struct remap_c *rc)
{
    if (debug_target == rc) {
        debug_target = NULL;
    }
}

/*
 * dmr_debug_init() - Initialize debug interface
 */
int dmr_debug_init(void)
{
    dmr_debug_dir = debugfs_create_dir("dm-remap", NULL);
    if (!dmr_debug_dir) {
        DMR_DEBUG(0, "Failed to create debug directory");
        return -ENOMEM;
    }
    
    DMR_DEBUG(1, "Initialized debug interface");
    return 0;
}

/*
 * dmr_debug_exit() - Cleanup debug interface
 */
void dmr_debug_exit(void)
{
    if (dmr_debug_dir) {
        debugfs_remove_recursive(dmr_debug_dir);
        dmr_debug_dir = NULL;
    }
    
    debug_target = NULL;
    DMR_DEBUG(1, "Cleaned up debug interface");
}