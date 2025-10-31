/*
 * dm-remap-messages.h - Message handling function declarations
 * 
 * This header declares the message processing functions for dm-remap.
 * 
 * Author: Christian (with AI assistance)
 * License: GPL v2
 */

#ifndef DM_REMAP_MESSAGES_H
#define DM_REMAP_MESSAGES_H

#include <linux/device-mapper.h>  /* For struct dm_target */

/*
 * remap_message() - Handle dmsetup message commands
 * 
 * This is the .message function for our device mapper target.
 * It processes commands sent via "dmsetup message".
 * 
 * @ti: Device mapper target instance
 * @argc: Number of command arguments
 * @argv: Array of command argument strings
 * @result: Buffer to write response (may be NULL)
 * @maxlen: Size of result buffer
 * 
 * Returns: 0 on success (even for command errors)
 */
int remap_message(struct dm_target *ti, unsigned argc, char **argv,
                  char *result, unsigned maxlen);

#endif /* DM_REMAP_MESSAGES_H */