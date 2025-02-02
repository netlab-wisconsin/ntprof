#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include "../include/config.h"
#include "../include/ntprof_ctl.h"
#include "../include/analyze.h"

#define DEVICE_FILE "/dev/ntprof"
#define DEFAULT_CONFIG_FILE "ntprof_config.ini"

enum ECmdType {
    CMD_ANALYZE,
    CMD_START,
    CMD_STOP,
    CMD_INVALID
};

volatile sig_atomic_t stop = 0;

void handle_sigint(int sig) {
    stop = 1;
}

void print_usage() {
    printf("Usage: ntprof_cli <command> [config_file]\n");
    printf("Commands:\n");
    printf("  analyze <config_file> - Generate probing IO to target device, default ntprof_config.ini\n");
    printf("  start <config_file> - Start profiling, default ntprof_config.ini\n");
    printf("  stop                - Stop profiling\n");
}

enum ECmdType parse_command(const char *cmd) {
    if (strcmp(cmd, "analyze") == 0) return CMD_ANALYZE;
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

int send_ioctl(int fd, unsigned long request, void *arg) {
    if (ioctl(fd, request, arg) < 0) {
        perror("ioctl failed");
        close(fd);
        return EXIT_FAILURE;
    }
    close(fd);
    return EXIT_SUCCESS;
}

int analyze(struct ntprof_config *config) {
    int fd = open_device();
    if (fd < 0) return EXIT_FAILURE;

    struct analyze_arg arg = { .config = *config };
    int ret = send_ioctl(fd, NTPROF_IOCTL_ANALYZE, &arg);
    if (ret != EXIT_SUCCESS) return EXIT_FAILURE;

    printf("Analyze result:\n");
    printf("  - Total number of probes: %llu\n", arg.result.total_io);

    return EXIT_SUCCESS;
}

int run_analyze(struct ntprof_config *config) {
    if (config->is_online) {
        while (!stop) {
            if (analyze(config) != EXIT_SUCCESS) {
                fprintf(stderr, "Error: Analysis failed during online monitoring.\n");
                return EXIT_FAILURE;
            }
            sleep(config->time_interval);
        }
        printf("\nAnalysis interrupted by user.\n");
    } else {
        if (analyze(config) != EXIT_SUCCESS) {
            fprintf(stderr, "Error: Analysis failed.\n");
            return EXIT_FAILURE;
        }
    }
    return EXIT_SUCCESS;
}

int execute_command(unsigned long ioctl_cmd, void *arg) {
    int fd = open_device();
    if (fd < 0) return EXIT_FAILURE;
    return send_ioctl(fd, ioctl_cmd, arg);
}

int main(int argc, char *argv[]) {
    signal(SIGINT, handle_sigint);  // register SIGINT processing function

    if (argc < 2) {
        print_usage();
        return EXIT_FAILURE;
    }

    enum ECmdType cmd = parse_command(argv[1]);
    if (cmd == CMD_INVALID) {
        fprintf(stderr, "Invalid command: %s\n", argv[1]);
        print_usage();
        return EXIT_FAILURE;
    }

    if (cmd == CMD_STOP) {
        return execute_command(NTPROF_IOCTL_STOP, NULL);
    }

    const char *config_file = (argc == 3) ? argv[2] : DEFAULT_CONFIG_FILE;
    if (access(config_file, F_OK) != 0) {
        fprintf(stderr, "Error: Config file '%s' does not exist!\n", config_file);
        return EXIT_FAILURE;
    }

    struct ntprof_config config;
    if (parse_config(config_file, &config) != 0) {
        fprintf(stderr, "Error: Failed to parse config file: %s\n", config_file);
        return EXIT_FAILURE;
    }

    if (cmd == CMD_ANALYZE) {
        return run_analyze(&config);
    } else if (cmd == CMD_START) {
        return execute_command(NTPROF_IOCTL_START, &config);
    }

    return EXIT_SUCCESS;
}
