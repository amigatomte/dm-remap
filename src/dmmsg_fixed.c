#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <linux/dm-ioctl.h>

#define DM_DEV_NAME     "test-remap"
#define DM_BUFFER_SIZE  4096

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <target-sector> \"message\"\n", argv[0]);
        return 1;
    }

    uint64_t sector = strtoull(argv[1], NULL, 10);
    const char *message = argv[2];

    int fd = open("/dev/mapper/control", O_RDWR);
    if (fd < 0) {
        perror("open /dev/mapper/control");
        return 1;
    }

    char buffer[DM_BUFFER_SIZE];
    memset(buffer, 0, sizeof(buffer));

    /* Set up dm_ioctl structure */
    struct dm_ioctl *io = (struct dm_ioctl *)buffer;
    io->version[0] = DM_VERSION_MAJOR;
    io->version[1] = DM_VERSION_MINOR;
    io->version[2] = DM_VERSION_PATCHLEVEL;
    io->data_start = sizeof(struct dm_ioctl);
    io->data_size = DM_BUFFER_SIZE;
    io->flags = DM_DATA_OUT_FLAG;  /* We expect output from kernel */
    strncpy(io->name, DM_DEV_NAME, sizeof(io->name) - 1);
    io->target_count = 1;

    /* Set up dm_target_msg structure */
    struct dm_target_msg *tmsg = (struct dm_target_msg *)(buffer + io->data_start);
    tmsg->sector = sector;
    strncpy((char *)tmsg->message, message, 
            DM_BUFFER_SIZE - io->data_start - sizeof(struct dm_target_msg) - 1);

    /* Send the message */
    if (ioctl(fd, DM_TARGET_MSG, io) < 0) {
        perror("ioctl(DM_TARGET_MSG)");
        close(fd);
        return 1;
    }

    close(fd);

    /* After successful ioctl, the response should be in the message area */
    /* According to device-mapper protocol, the kernel overwrites the message */
    /* area with the response */
    
    char *response = (char *)tmsg->message;
    
    /* The response should now be in the same location as the original message */
    if (response[0] != '\0') {
        printf("%s\n", response);
    } else {
        /* No response - this is normal for many commands */
        /* Don't print anything, just like dmsetup message */
    }

    return 0;
}