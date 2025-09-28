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

/* Find the first non-zero byte starting at p, up to end */
static const char* skip_zeros(const char *p, const char *end) {
    while (p < end && *p == '\0') p++;
    return p;
}

/* Print bytes from p up to the first NUL (or end), return 1 if something was printed */
static int print_cstr_region(const char *p, const char *end) {
    if (p >= end) return 0;
    const char *nul = memchr(p, '\0', (size_t)(end - p));
    size_t len = nul ? (size_t)(nul - p) : (size_t)(end - p);
    if (len == 0) return 0;
    fwrite(p, 1, len, stdout);
    return 1;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr,
            "Usage:\n  %s <target-sector> \"message string\"\n\n"
            "Examples:\n  %s 0 \"verify 123456\"\n"
            "  %s 2048 \"verify 123456\"  # if your table starts at 2048\n",
            argv[0], argv[0], argv[0]);
        return 1;
    }

    /* Parse target-sector */
    char *endp = NULL;
    uint64_t sector = strtoull(argv[1], &endp, 10);
    if (!argv[1][0] || (endp && *endp != '\0')) {
        fprintf(stderr, "Invalid sector: %s\n", argv[1]);
        return 1;
    }

    const char *user_msg = argv[2];

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
    io->flags      = DM_DATA_OUT_FLAG;   /* expect output from kernel */
    snprintf(io->name, sizeof(io->name), DM_DEV_NAME);
    io->target_count = 1;

    /* Build input payload: dm_target_msg + message */
    struct dm_target_msg *tmsg = (struct dm_target_msg *)(buffer + io->data_start);
    tmsg->sector = sector;

    char *in = (char *)tmsg->message;
    int max_in = DM_BUFFER_SIZE - (int)io->data_start - (int)sizeof(*tmsg);
    int in_len = snprintf(in, max_in, "%s", user_msg);
    if (in_len < 0 || in_len + 1 > max_in) {
        fprintf(stderr, "Message too long (max %d)\n", max_in - 1);
        close(fd);
        return 1;
    }

    /* Send DM_TARGET_MSG */
    if (ioctl(fd, DM_TARGET_MSG, io) < 0) {
        perror("ioctl(DM_TARGET_MSG)");
        close(fd);
        return 1;
    }
    close(fd);

    /* Read reply area */
    buffer[DM_BUFFER_SIZE - 1] = '\0';
    const char *endbuf = buffer + DM_BUFFER_SIZE;
    const char *base   = buffer + io->data_start;                 /* start of region */
    const char *post   = base + sizeof(struct dm_target_msg);     /* after header */

    /* Debug: metadata + hex dump */
    fprintf(stderr, "data_start=%u data_size=%u\n", io->data_start, io->data_size);
    fprintf(stderr, "Raw @data_start (first 64):\n");
    for (int i = 0; i < 64 && (io->data_start + i) < DM_BUFFER_SIZE; i++)
        fprintf(stderr, "%02x ", (unsigned char)base[i]);
    fprintf(stderr, "\n");

    /* 1) Try from base (skip leading zeros) */
    const char *p = skip_zeros(base, endbuf);
    if (print_cstr_region(p, endbuf)) return 0;

    /* 2) Try right after dm_target_msg header */
    p = skip_zeros(post, endbuf);
    if (print_cstr_region(p, endbuf)) return 0;

    /* 3) Fallback: scan next 1 KiB for the first NUL-terminated string */
    for (int off = 0; off < 1024 && (io->data_start + off) < DM_BUFFER_SIZE; off++) {
        const char *s = base + off;
        const char *q = skip_zeros(s, endbuf);
        if (print_cstr_region(q, endbuf)) return 0;
    }

    fprintf(stderr, "(empty reply)\n");
    return 0;
}
