/**
 * dm-remap-logging.h - Unified Debug/Info/Error Logging for dm-remap
 * 
 * This header provides centralized logging macros to eliminate
 * macro redefinition warnings across compilation units.
 */

#ifndef DM_REMAP_LOGGING_H
#define DM_REMAP_LOGGING_H

#include <linux/kernel.h>
#include <linux/printk.h>

/* Global debug level */
extern int dm_remap_debug;

/**
 * DMR_DEBUG - Debug logging with level control
 * @level: Debug level (0=off, 1=info, 2=debug, 3=trace)
 * @fmt: Format string
 */
#define DMR_DEBUG(level, fmt, args...) \
    do { \
        if (dm_remap_debug >= (level)) \
            printk(KERN_DEBUG "dm-remap: DEBUG: " fmt "\n", ##args); \
    } while (0)

/**
 * DMR_INFO - Informational messages
 * @fmt: Format string
 */
#define DMR_INFO(fmt, args...) \
    printk(KERN_INFO "dm-remap: " fmt "\n", ##args)

/**
 * DMR_ERROR - Error messages
 * @fmt: Format string
 */
#define DMR_ERROR(fmt, args...) \
    printk(KERN_ERR "dm-remap: ERROR: " fmt "\n", ##args)

/**
 * DMR_WARN - Warning messages
 * @fmt: Format string
 */
#define DMR_WARN(fmt, args...) \
    printk(KERN_WARNING "dm-remap: WARNING: " fmt "\n", ##args)

#endif /* DM_REMAP_LOGGING_H */
