#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "dmremap_status.h"

#define DMREMAP_STATUS_VERSION "1.0.0"

static void print_usage(const char *prog)
{
    fprintf(stderr, "dmremap-status v%s - dm-remap status formatter\n", DMREMAP_STATUS_VERSION);
    fprintf(stderr, "Usage: %s [OPTIONS] <device_name>\n\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -f, --format FORMAT    Output format: human|json|csv|raw|compact (default: human)\n");
    fprintf(stderr, "  -i, --input FILE       Read from file instead of dmsetup\n");
    fprintf(stderr, "  -w, --watch INTERVAL   Watch mode: refresh every N seconds\n");
    fprintf(stderr, "  -n, --no-color         Disable colored output\n");
    fprintf(stderr, "  -h, --help             Show this help message\n");
    fprintf(stderr, "  -v, --version          Show version\n");
    fprintf(stderr, "\nExamples:\n");
    fprintf(stderr, "  # Show pretty status\n");
    fprintf(stderr, "  sudo dmremap-status dm-test-remap\n");
    fprintf(stderr, "  # JSON output for scripting\n");
    fprintf(stderr, "  sudo dmremap-status dm-test-remap --format json\n");
    fprintf(stderr, "  # Watch mode (refresh every 2 seconds)\n");
    fprintf(stderr, "  sudo dmremap-status dm-test-remap --watch 2\n");
    fprintf(stderr, "  # Read from file\n");
    fprintf(stderr, "  dmremap-status --input status.txt --format json\n");
}

static void print_version(void)
{
    printf("dmremap-status v%s\n", DMREMAP_STATUS_VERSION);
}

/**
 * Execute dmsetup status and capture output
 */
static int get_dmsetup_status(const char *device_name, char *buffer, size_t buffer_size)
{
    if (!device_name || !buffer || buffer_size == 0) {
        fprintf(stderr, "Error: Invalid parameters to get_dmsetup_status\n");
        return -1;
    }

    /* Build command */
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "dmsetup status %s", device_name);

    /* Execute command and capture output */
    FILE *pipe = popen(cmd, "r");
    if (!pipe) {
        perror("popen");
        return -1;
    }

    size_t bytes_read = fread(buffer, 1, buffer_size - 1, pipe);
    buffer[bytes_read] = '\0';

    int ret = pclose(pipe);
    if (ret != 0) {
        fprintf(stderr, "Error: dmsetup command failed\n");
        return -1;
    }

    return 0;
}

/**
 * Clear screen for watch mode
 */
static void clear_screen(void)
{
    printf("\033[2J\033[H");
    fflush(stdout);
}

/**
 * Main entry point
 */
int main(int argc, char *argv[])
{
    if (argc < 2) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    /* Parse arguments */
    output_format_t format = OUTPUT_HUMAN;
    const char *device_name = NULL;
    const char *input_file = NULL;
    int watch_interval = 0;
    int use_color = 1;

    static struct option long_options[] = {
        {"format",    required_argument, 0, 'f'},
        {"input",     required_argument, 0, 'i'},
        {"watch",     required_argument, 0, 'w'},
        {"no-color",  no_argument,       0, 'n'},
        {"help",      no_argument,       0, 'h'},
        {"version",   no_argument,       0, 'v'},
        {0, 0, 0, 0}
    };

    int option_index = 0;
    int c;

    while ((c = getopt_long(argc, argv, "f:i:w:nhv", long_options, &option_index)) != -1) {
        switch (c) {
        case 'f': {
            if (strcmp(optarg, "human") == 0) {
                format = OUTPUT_HUMAN;
            } else if (strcmp(optarg, "json") == 0) {
                format = OUTPUT_JSON;
            } else if (strcmp(optarg, "csv") == 0) {
                format = OUTPUT_CSV;
            } else if (strcmp(optarg, "raw") == 0) {
                format = OUTPUT_RAW;
            } else if (strcmp(optarg, "compact") == 0) {
                format = OUTPUT_COMPACT;
            } else {
                fprintf(stderr, "Error: Unknown format '%s'\n", optarg);
                return EXIT_FAILURE;
            }
            break;
        }
        case 'i':
            input_file = optarg;
            break;
        case 'w':
            watch_interval = atoi(optarg);
            if (watch_interval <= 0) {
                fprintf(stderr, "Error: Watch interval must be positive\n");
                return EXIT_FAILURE;
            }
            break;
        case 'n':
            use_color = 0;
            break;
        case 'h':
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        case 'v':
            print_version();
            return EXIT_SUCCESS;
        default:
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    /* Get device name from positional argument or error */
    if (optind < argc && !input_file) {
        device_name = argv[optind];
    } else if (!input_file) {
        fprintf(stderr, "Error: Device name required\n");
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    /* Allocate status structure */
    dm_remap_status_t *status = malloc(sizeof(dm_remap_status_t));
    if (!status) {
        perror("malloc");
        return EXIT_FAILURE;
    }

    /* Main loop */
    int first_iteration = 1;

    while (1) {
        if (watch_interval > 0 && !first_iteration) {
            sleep(watch_interval);
            clear_screen();
        }
        first_iteration = 0;

        char status_buffer[4096];

        /* Get status */
        if (input_file) {
            if (parse_dmremap_status_file(input_file, status) != 0) {
                fprintf(stderr, "Error: Failed to parse status from file '%s'\n", input_file);
                free(status);
                return EXIT_FAILURE;
            }
        } else {
            if (get_dmsetup_status(device_name, status_buffer, sizeof(status_buffer)) != 0) {
                fprintf(stderr, "Error: Failed to get status for device '%s'\n", device_name);
                free(status);
                return EXIT_FAILURE;
            }

            if (parse_dmremap_status(status_buffer, status) != 0) {
                fprintf(stderr, "Error: Failed to parse status output\n");
                free(status);
                return EXIT_FAILURE;
            }
        }

        /* Output in requested format */
        int ret = 0;
        switch (format) {
        case OUTPUT_HUMAN:
            ret = print_human_readable(status, stdout);
            break;
        case OUTPUT_JSON:
            ret = print_json(status, stdout);
            break;
        case OUTPUT_CSV:
            ret = print_csv(status, stdout);
            break;
        case OUTPUT_COMPACT:
            ret = print_compact(status, stdout);
            break;
        case OUTPUT_RAW:
            if (input_file) {
                fprintf(stderr, "Error: Cannot output raw format from file input\n");
                ret = -1;
            } else {
                fprintf(stdout, "%s", status_buffer);
                ret = 0;
            }
            break;
        }

        if (ret != 0) {
            fprintf(stderr, "Error: Failed to format output\n");
            free(status);
            return EXIT_FAILURE;
        }

        /* Break loop if not watching */
        if (watch_interval == 0) {
            break;
        }
    }

    free(status);
    return EXIT_SUCCESS;
}
