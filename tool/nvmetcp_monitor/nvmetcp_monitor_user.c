#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/user.h>  // 包含 PAGE_SIZE 定义

#include "nvmetcp_monitor.h"

#define BUFFER_SIZE PAGE_SIZE

static volatile int keep_running = 1;

void int_handler(int dummy) {
    keep_running = 0;
}

int main(int argc, char **argv) {
    int control_fd, mmap_fd;
    struct nvmetcp_tr *tr_data;

    signal(SIGINT, int_handler);

    // 打开并映射缓冲区
    mmap_fd = open("/proc/nvmetcp_monitor", O_RDONLY);
    if (mmap_fd == -1) {
        perror("open");
        exit(EXIT_FAILURE);
    }

    tr_data = mmap(NULL, sizeof(struct nvmetcp_tr), PROT_READ, MAP_SHARED, mmap_fd, 0);
    if (tr_data == MAP_FAILED) {
        perror("mmap");
        close(mmap_fd);
        exit(EXIT_FAILURE);
    }

    // 启用记录
    control_fd = open("/proc/nvmetcp_monitor", O_WRONLY);
    if (control_fd == -1) {
        perror("open");
        munmap(tr_data, sizeof(struct nvmetcp_tr));
        close(mmap_fd);
        exit(EXIT_FAILURE);
    }
    write(control_fd, "1", 1);
    close(control_fd);

    while (keep_running) {
        printf("I/O request count: %llu\n", tr_data->io_request_count);
        sleep(1);
    }

    // 禁用记录
    control_fd = open("/proc/nvmetcp_monitor", O_WRONLY);
    if (control_fd == -1) {
        perror("open");
        munmap(tr_data, sizeof(struct nvmetcp_tr));
        close(mmap_fd);
        exit(EXIT_FAILURE);
    }
    write(control_fd, "0", 1);
    close(control_fd);

    FILE *data_file = fopen("nvmetcp_monitor.data", "w");
    if (!data_file) {
        perror("fopen");
        munmap(tr_data, sizeof(struct nvmetcp_tr));
        close(mmap_fd);
        exit(EXIT_FAILURE);
    }

    fprintf(data_file, "Final I/O Requests Count: %llu\n", tr_data->io_request_count);
    fclose(data_file);

    printf("Data saved to nvmetcp_monitor.data\n");

    munmap(tr_data, sizeof(struct nvmetcp_tr));
    close(mmap_fd);

    return 0;
}
