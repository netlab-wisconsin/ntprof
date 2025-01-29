#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <unistd.h>
#include "../include/config.h"
#include "../include/ntprof_ctl.h"

#define DEVICE_FILE "/dev/ntprof"
#define DEFAULT_CONFIG_FILE "ntprof_config.ini"

enum ECmdType {
    CMD_QUERY,
    CMD_START,
    CMD_STOP,
    CMD_INVALID
};

void print_usage() {
    printf("Usage: ntprof_cli <command> [config_file]\n");
    printf("Commands:\n");
    printf("  query <config_file> - Generate probing IO to target device, default ntprof_config.ini\n");
    printf("  start <config_file> - Start profiling, default ntprof_config.ini\n");
    printf("  stop                - Stop profiling\n");
}

enum ECmdType parse_command(const char *cmd) {
    if (strcmp(cmd, "query") == 0) return CMD_QUERY;
    if (strcmp(cmd, "start") == 0) return CMD_START;
    if (strcmp(cmd, "stop") == 0) return CMD_STOP;
    return CMD_INVALID;
}

int open_device() {
    int fd = open(DEVICE_FILE, O_RDWR);
    if (fd < 0) {
        perror("Failed to open device file");
        return -1;
    }
    return fd;
}

/**
 * Send the message to the ntprof kernel modules.
 * This is one-time message, so we close the file descriptor after sending the message.
 */
int send_ioctl(int fd, unsigned long request, void *arg) {
    if (ioctl(fd, request, arg) < 0) {
        perror("ioctl failed");
        close(fd);
        return EXIT_FAILURE;
    }
    close(fd);
    return EXIT_SUCCESS;
}

// TODO: implement this function
void run_query() {
    printf("Query command not implemented yet.\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage();
        return EXIT_FAILURE;
    }

    enum ECmdType cmd = parse_command(argv[1]);

    switch (cmd) {
        case CMD_QUERY:
        case CMD_START: {
            if (argc < 3) {
                printf("No config file provided, using default: %s\n", DEFAULT_CONFIG_FILE);
            }
            const char *config_file = (argc == 3) ? argv[2] : DEFAULT_CONFIG_FILE;
            // check if config file exists
            if (access(config_file, F_OK) != 0) {
                fprintf(stderr, "Error: Config file '%s' does not exist!\n", config_file);
                return EXIT_FAILURE;
            }

            struct ntprof_config config;
            if (parse_config(config_file, &config) != 0) {
                fprintf(stderr, "Error: Failed to parse config file: %s\n", config_file);
                return EXIT_FAILURE;
            }

            if (cmd == CMD_QUERY) {
                run_query();
                return EXIT_SUCCESS;
            }

            // Start profiling
            int fd = open_device();
            if (fd < 0) return EXIT_FAILURE;
            return send_ioctl(fd, NTPROF_IOCTL_START, &config);
        }

        case CMD_STOP: {
            int fd = open_device();
            if (fd < 0) return EXIT_FAILURE;
            return send_ioctl(fd, NTPROF_IOCTL_STOP, NULL);
        }

        default:
            fprintf(stderr, "Invalid command: %s\n", argv[1]);
            print_usage();
            return EXIT_FAILURE;
    }
}
