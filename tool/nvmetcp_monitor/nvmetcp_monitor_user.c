#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/user.h>  // 包含 PAGE_SIZE 定义

#define BUFFER_SIZE PAGE_SIZE

static volatile int keep_running = 1;

void int_handler(int dummy) {
    keep_running = 0;
}

int main(int argc, char **argv) {
    int control_fd, mmap_fd;
    char *buffer;

    signal(SIGINT, int_handler);

    // 打开并映射缓冲区
    mmap_fd = open("/proc/nvmetcp_monitor", O_RDONLY);
    if (mmap_fd == -1) {
        perror("open");
        exit(EXIT_FAILURE);
    }

    buffer = mmap(NULL, BUFFER_SIZE, PROT_READ, MAP_SHARED, mmap_fd, 0);
    if (buffer == MAP_FAILED) {
        perror("mmap");
        close(mmap_fd);
        exit(EXIT_FAILURE);
    }

    // 启用记录
    control_fd = open("/proc/nvmetcp_monitor", O_WRONLY);
    if (control_fd == -1) {
        perror("open");
        munmap(buffer, BUFFER_SIZE);
        close(mmap_fd);
        exit(EXIT_FAILURE);
    }
    write(control_fd, "1", 1);
    close(control_fd);

    while (keep_running) {
        printf("%s", buffer);
        sleep(1);
    }

    // 禁用记录
    control_fd = open("/proc/nvmetcp_monitor", O_WRONLY);
    if (control_fd == -1) {
        perror("open");
        munmap(buffer, BUFFER_SIZE);
        close(mmap_fd);
        exit(EXIT_FAILURE);
    }
    write(control_fd, "0", 1);
    close(control_fd);

    FILE *data_file = fopen("nvmetcp_monitor.data", "w");
    if (!data_file) {
        perror("fopen");
        munmap(buffer, BUFFER_SIZE);
        close(mmap_fd);
        exit(EXIT_FAILURE);
    }

    fprintf(data_file, "Final I/O Requests Count:\n%s", buffer);
    fclose(data_file);

    printf("Data saved to nvmetcp_monitor.data\n");

    munmap(buffer, BUFFER_SIZE);
    close(mmap_fd);

    return 0;
}
