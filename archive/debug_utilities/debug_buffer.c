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
        perror("open /dev/mapper/control");
        return 1;
    }

    char buffer[DM_BUFFER_SIZE];
    memset(buffer, 0, sizeof(buffer));

    struct dm_ioctl *io = (struct dm_ioctl *)buffer;
    io->version[0] = DM_VERSION_MAJOR;
    io->version[1] = DM_VERSION_MINOR;
    io->version[2] = DM_VERSION_PATCHLEVEL;
    io->data_start = sizeof(struct dm_ioctl);
    io->data_size  = DM_BUFFER_SIZE;
    io->flags      = DM_DATA_OUT_FLAG;
    snprintf(io->name, sizeof(io->name), DM_DEV_NAME);
    io->target_count = 1;

    struct dm_target_msg *tmsg = (struct dm_target_msg *)(buffer + io->data_start);
    tmsg->sector = 0;
    strcpy((char *)tmsg->message, "ping");

    printf("BEFORE ioctl - entire buffer:\n");
    for (int i = 0; i < 256; i++) {
        printf("%02x ", (unsigned char)buffer[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }

    if (ioctl(fd, DM_TARGET_MSG, io) < 0) {
        perror("ioctl(DM_TARGET_MSG)");
        close(fd);
        return 1;
    }

    printf("\nAFTER ioctl - entire buffer:\n");
    for (int i = 0; i < 256; i++) {
        printf("%02x ", (unsigned char)buffer[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }
    
    printf("\nData area (starting at offset %u):\n", io->data_start);
    for (int i = io->data_start; i < io->data_start + 64 && i < DM_BUFFER_SIZE; i++) {
        printf("%02x ", (unsigned char)buffer[i]);
        if ((i - io->data_start + 1) % 16 == 0) printf("\n");
    }
    
    printf("\nLooking for 'pong' in entire buffer:\n");
    for (int i = 0; i < DM_BUFFER_SIZE - 4; i++) {
        if (buffer[i] == 'p' && buffer[i+1] == 'o' && buffer[i+2] == 'n' && buffer[i+3] == 'g') {
            printf("Found 'pong' at offset %d!\n", i);
        }
    }

    close(fd);
    return 0;
}