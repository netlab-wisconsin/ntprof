#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <stdatomic.h>
#include "nvmetcp_monitor.h"

#define BUFFER_SIZE PAGE_SIZE

static volatile int keep_running = 1;

void int_handler(int dummy) {
    keep_running = 0;
}


int main(int argc, char **argv) {
    int control_fd, mmap_fd;
    struct blk_tr *tr_data;

    signal(SIGINT, int_handler);

    mmap_fd = open("/proc/nvmetcp_monitor", O_RDONLY);
    if (mmap_fd == -1) {
        perror("open");
        exit(EXIT_FAILURE);
    }

    tr_data = mmap(NULL, sizeof(struct blk_tr), PROT_READ, MAP_SHARED, mmap_fd, 0);
    if (tr_data == MAP_FAILED) {
        perror("mmap");
        close(mmap_fd);
        exit(EXIT_FAILURE);
    }

    control_fd = open("/proc/nvmetcp_monitor", O_WRONLY);
    if (control_fd == -1) {
        perror("open");
        munmap(tr_data, sizeof(struct blk_tr));
        close(mmap_fd);
        exit(EXIT_FAILURE);
    }
    write(control_fd, "1", 1);
    close(control_fd);

    while (keep_running) {
        pr_blk_tr(tr_data);
        sleep(1);
    }

    control_fd = open("/proc/nvmetcp_monitor", O_WRONLY);
    if (control_fd == -1) {
        perror("open");
        munmap(tr_data, sizeof(struct blk_tr));
        close(mmap_fd);
        exit(EXIT_FAILURE);
    }
    write(control_fd, "0", 1);
    close(control_fd);

    FILE *data_file = fopen("nvmetcp_monitor.data", "w");
    if (!data_file) {
        perror("fopen");
        munmap(tr_data, sizeof(struct blk_tr));
        close(mmap_fd);
        exit(EXIT_FAILURE);
    }

    fclose(data_file);

    printf("Data saved to nvmetcp_monitor.data\n");

    munmap(tr_data, sizeof(struct blk_tr));
    close(mmap_fd);

    return 0;
}