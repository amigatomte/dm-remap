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

int main() {
    int fd = open("/dev/mapper/control", O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    char buffer[DM_BUFFER_SIZE];
    memset(buffer, 0, sizeof(buffer));

    struct dm_ioctl *io = (struct dm_ioctl *)buffer;
    io->version[0] = DM_VERSION_MAJOR;
    io->version[1] = DM_VERSION_MINOR;
    io->version[2] = DM_VERSION_PATCHLEVEL;
    io->data_start = sizeof(struct dm_ioctl);
    io->data_size = DM_BUFFER_SIZE;
    io->flags = DM_DATA_OUT_FLAG;
    strncpy(io->name, DM_DEV_NAME, sizeof(io->name) - 1);
    io->target_count = 1;

    struct dm_target_msg *tmsg = (struct dm_target_msg *)(buffer + io->data_start);
    tmsg->sector = 0;
    strcpy((char *)tmsg->message, "ping");

    printf("BEFORE ioctl:\n");
    printf("data_start: %u, data_size: %u\n", io->data_start, io->data_size);
    printf("Message: '%s'\n", (char *)tmsg->message);

    if (ioctl(fd, DM_TARGET_MSG, io) < 0) {
        perror("ioctl");
        close(fd);
        return 1;
    }

    printf("\nAFTER ioctl:\n");
    printf("data_start: %u, data_size: %u\n", io->data_start, io->data_size);
    printf("Message area: '%s'\n", (char *)tmsg->message);
    
    /* Check if response is at start of data area */
    char *data_start_ptr = buffer + io->data_start;
    printf("Start of data area: '%s'\n", data_start_ptr);
    
    /* Maybe the response is at the very beginning of the buffer */
    printf("Start of entire buffer: '%.32s'\n", buffer);
    
    /* Search the ENTIRE buffer for "pong" */
    printf("Searching entire buffer for 'pong':\n");
    for (int i = 0; i < DM_BUFFER_SIZE - 4; i++) {
        if (strncmp(buffer + i, "pong", 4) == 0) {
            printf("  Found 'pong' at offset %d\n", i);
        }
    }
    
    /* The data_size changed to 305, maybe the response is in first 305 bytes */
    printf("First %u bytes after ioctl:\n", io->data_size);
    for (int i = 0; i < (int)io->data_size && i < 128; i++) {
        if (buffer[i] >= 32 && buffer[i] <= 126) {
            printf("%c", buffer[i]);
        } else {
            printf(".");
        }
        if ((i + 1) % 32 == 0) printf("\n");
    }
    printf("\n");

    close(fd);
    return 0;
}