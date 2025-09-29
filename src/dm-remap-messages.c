/*
 * dm-remap-messages.c - Message handling for dm-remap target
 * 
 * This file implements the message interface for the dm-remap device mapper target.
 * Users can send commands via "dmsetup message" to control remapping behavior.
 * 
 * Supported commands:
 * - ping: Test if the target is responding
 * - remap <sector>: Mark a sector as bad and remap it to spare area
 * - verify <sector>: Check if a sector is remapped
 * - clear: Remove all remap entries
 * 
 * Author: Christian (with AI assistance)
 * License: GPL v2
 */

#include <linux/kernel.h>         /* For printk, kstrtoull */
#include <linux/string.h>         /* For strcmp, scnprintf */
#include <linux/device-mapper.h>  /* For struct dm_target */

#include "dm-remap-core.h"        /* Our core data structures */
#include "dm-remap-messages.h"    /* Our message function declarations */

/*
 * remap_message() - Handle dmsetup message commands
 * 
 * This function is called when a user runs "dmsetup message <target> <sector> <command>".
 * It parses the command and arguments, then performs the requested operation.
 * 
 * IMPORTANT: This function can be called concurrently with I/O operations,
 * so it must use proper locking when accessing the remap table.
 * 
 * @ti: Device mapper target instance (contains our remap_c structure)
 * @argc: Number of command arguments
 * @argv: Array of command argument strings
 * @result: Buffer to write response message (can be NULL)
 * @maxlen: Size of result buffer
 * 
 * Returns: 0 on success, negative error code on failure
 */
int remap_message(struct dm_target *ti, unsigned argc, char **argv, 
                  char *result, unsigned maxlen)
{
    /* Extract our target context */
    struct remap_c *rc = ti->private;
    
    /* Variables for parsing sector numbers */
    sector_t bad_sector, spare_sector;
    
    /* Loop counter for table operations */
    int i;

    /*
     * DEBUG LOGGING: Log all message calls
     * 
     * This helps with debugging message handling issues.
     * We log both the arguments and the result buffer details.
     */
    printk(KERN_INFO "dm-remap: message handler called, argc=%u, maxlen=%u\n", 
           argc, maxlen);
    
    for (i = 0; i < argc; i++) {
        printk(KERN_INFO "dm-remap: argv[%d] = '%s'\n", i, argv[i]);
    }

    /*
     * RESULT BUFFER INITIALIZATION
     * 
     * The result buffer is used to send responses back to userspace.
     * We initialize it to be safe, even though some commands don't use it.
     */
    if (maxlen) {
        result[0] = '\0';              /* Start with empty string */
        result[maxlen - 1] = '\0';     /* Ensure null termination */
    }

    /*
     * ARGUMENT VALIDATION
     * 
     * All commands need at least one argument (the command name).
     */
    if (argc < 1) {
        if (maxlen)
            scnprintf(result, maxlen, "error: missing command");
        return 0;  /* Return 0 for handled but invalid commands */
    }

    /*
     * COMMAND: "remap <sector>"
     * 
     * This command marks a sector as bad and remaps it to the spare area.
     * It's the core functionality of the dm-remap target.
     * 
     * Usage: dmsetup message target-name 0 remap 12345
     */
    if (argc == 2 && strcmp(argv[0], "remap") == 0) {
        
        /* Parse the sector number from the command line */
        if (kstrtoull(argv[1], 10, &bad_sector)) {
            if (maxlen)
                scnprintf(result, maxlen, "error: invalid sector '%s'", argv[1]);
            return 0;
        }

        /*
         * CRITICAL SECTION: Modify the remap table
         * 
         * We need to hold the lock while modifying the table because
         * I/O operations might be reading it concurrently.
         */
        spin_lock(&rc->lock);
        
        /*
         * Check if this sector is already remapped
         * 
         * We search through all used entries to see if this sector
         * is already in our remap table.
         */
        for (i = 0; i < rc->spare_used; i++) {
            if (rc->table[i].main_lba == bad_sector) {
                spin_unlock(&rc->lock);
                if (maxlen)
                    scnprintf(result, maxlen, "error: sector %llu already remapped", 
                             (unsigned long long)bad_sector);
                return 0;
            }
        }
        
        /*
         * Check if we have spare sectors available
         * 
         * spare_used counts how many spare sectors we've allocated.
         * spare_len is the total number of spare sectors available.
         */
        if (rc->spare_used >= rc->spare_len) {
            spin_unlock(&rc->lock);
            if (maxlen)
                scnprintf(result, maxlen, "error: no spare sectors available");
            return 0;
        }

        /*
         * Add the new remap entry
         * 
         * We use the next available slot in our table.
         * The spare sector number is calculated as spare_start + spare_used.
         */
        rc->table[rc->spare_used].main_lba = bad_sector;
        spare_sector = rc->spare_start + rc->spare_used;
        rc->table[rc->spare_used].spare_lba = spare_sector;
        
        /* Increment the count of used spare sectors */
        rc->spare_used++;
        
        /* Release the lock - we're done modifying the table */
        spin_unlock(&rc->lock);

        /* Send success response back to userspace */
        if (maxlen) {
            scnprintf(result, maxlen, "remapped sector %llu -> spare sector %llu",
                     (unsigned long long)bad_sector,
                     (unsigned long long)spare_sector);
        }
        
        /* Log the successful remap for debugging */
        printk(KERN_INFO "dm-remap: Added remap: sector %llu -> spare %llu\n",
               (unsigned long long)bad_sector, (unsigned long long)spare_sector);
        
        return 0;
    }

    /*
     * COMMAND: "ping"
     * 
     * This is a simple test command to verify the target is responding.
     * It's useful for debugging and health checks.
     * 
     * Usage: dmsetup message target-name 0 ping
     */
    if (argc == 1 && strcmp(argv[0], "ping") == 0) {
        /*
         * DEBUG: Detailed ping handling
         * 
         * This helps debug message handling issues by showing
         * exactly how the ping command is processed.
         */
        printk(KERN_INFO "dm-remap: handling ping, argc=%u, maxlen=%u\n", argc, maxlen);
        printk(KERN_INFO "dm-remap: argv[0]='%s' at %p, result at %p\n", 
               argv[0], argv[0], result);
        
        /*
         * EXPERIMENTAL: Modify the input argument
         * 
         * This is an attempt to send data back to userspace through
         * the argv array. This doesn't actually work due to how the
         * device mapper framework handles messages, but we keep it
         * for educational purposes.
         */
        strcpy(argv[0], "pong");
        printk(KERN_INFO "dm-remap: overwrote argv[0] with 'pong'\n");
        
        /* Send the proper response through the result buffer */
        if (maxlen > 4) {
            strcpy(result, "pong");
            printk(KERN_INFO "dm-remap: wrote 'pong' to result buffer\n");
        }
        
        /* Also send to kernel log for debugging */
        printk(KERN_INFO "dm-remap: pong\n");
        
        return 0;
    }

    /*
     * COMMAND: "verify <sector>"
     * 
     * This command checks if a sector is currently remapped.
     * It's useful for debugging and status checking.
     * 
     * Usage: dmsetup message target-name 0 verify 12345
     */
    if (argc == 2 && strcmp(argv[0], "verify") == 0) {
        
        /* Parse the sector number */
        if (kstrtoull(argv[1], 10, &bad_sector)) {
            if (maxlen)
                scnprintf(result, maxlen, "error: invalid sector '%s'", argv[1]);
            return 0;
        }

        /*
         * CRITICAL SECTION: Read the remap table
         * 
         * We need the lock even for reading because another thread
         * might be modifying the table structure.
         */
        spin_lock(&rc->lock);
        
        /* Search for the sector in our remap table */
        for (i = 0; i < rc->spare_used; i++) {
            if (rc->table[i].main_lba == bad_sector && 
                rc->table[i].main_lba != (sector_t)-1) {
                
                spare_sector = rc->table[i].spare_lba;
                spin_unlock(&rc->lock);
                
                /* Found it - report the mapping */
                if (maxlen) {
                    scnprintf(result, maxlen, "sector %llu -> spare %llu",
                             (unsigned long long)bad_sector,
                             (unsigned long long)spare_sector);
                }
                return 0;
            }
        }
        
        spin_unlock(&rc->lock);
        
        /* Not found - sector is not remapped */
        if (maxlen)
            scnprintf(result, maxlen, "sector %llu not remapped", 
                     (unsigned long long)bad_sector);
        
        return 0;
    }

    /*
     * COMMAND: "clear"
     * 
     * This command removes all remap entries, effectively resetting
     * the target to its initial state.
     * 
     * Usage: dmsetup message target-name 0 clear
     */
    if (argc == 1 && strcmp(argv[0], "clear") == 0) {
        
        /*
         * CRITICAL SECTION: Clear the remap table
         * 
         * We reset all entries to unused state and reset the counter.
         */
        spin_lock(&rc->lock);
        
        /* Mark all entries as unused */
        for (i = 0; i < rc->spare_len; i++) {
            rc->table[i].main_lba = (sector_t)-1;  /* -1 means unused */
            /* spare_lba stays the same - it's pre-calculated */
        }
        
        /* Reset the used counter */
        rc->spare_used = 0;
        
        spin_unlock(&rc->lock);
        
        /* Send confirmation to userspace */
        if (maxlen)
            scnprintf(result, maxlen, "cleared all remap entries");
        
        printk(KERN_INFO "dm-remap: Cleared all remap entries\n");
        
        return 0;
    }

    /*
     * UNKNOWN COMMAND
     * 
     * If we get here, the user sent a command we don't recognize.
     */
    if (maxlen)
        scnprintf(result, maxlen, "error: unknown command '%s'", argv[0]);
    
    printk(KERN_WARNING "dm-remap: Unknown message command: %s\n", argv[0]);
    
    return 0;
}

/*
 * DESIGN NOTES:
 * 
 * 1. RETURN VALUES:
 *    We always return 0 from this function, even for errors.
 *    The device mapper framework interprets non-zero returns as
 *    system errors, not command errors. Command errors are reported
 *    through the result buffer.
 * 
 * 2. LOCKING STRATEGY:
 *    We use the same lock for both reading and writing the remap table.
 *    This is simple and safe, though it could theoretically cause
 *    contention. In practice, message commands are rare compared to I/O.
 * 
 * 3. ERROR HANDLING:
 *    We validate all inputs and provide detailed error messages.
 *    This makes the interface easier to use and debug.
 * 
 * 4. USERSPACE INTERFACE:
 *    The result buffer is the primary way to send data back to userspace.
 *    The kernel log messages are for debugging and monitoring.
 * 
 * 5. THREAD SAFETY:
 *    This function can be called concurrently with I/O operations
 *    and other message operations. All shared data access is protected
 *    by the remap_c->lock spinlock.
 */