#include <stdio.h>
#include "nvmetcp_monitor_com.h"

static void pr_blk_tr(struct blk_stat *tr) {
    char *dis_header[9] = {"<4KB", "4KB", "8KB", "16KB", "32KB", "64KB", "128KB", ">128KB", "others"};
    // spin_lock(&tr->lock);
    printf("read total: %llu\n", tr->read_count);
    printf("read cnt");
    int i;
    for (i = 0; i < 9; i++) {
        printf(", [%s: %llu]", dis_header[i], tr->read_io[i]);
    }
    printf("\nread dis");
    for (i = 0; i < 9; i++) {
        printf(", [%s: %.2f]", dis_header[i], (float)tr->read_io[i] / tr->read_count);
    }
    printf("\nwrite total: %llu\n", tr->write_count);
    printf("write cnt");
    for (i = 0; i < 9; i++) {
        printf(", [%s: %llu]", dis_header[i], tr->write_io[i]);
    }
    printf("\nwrite dis");
    for (i = 0; i < 9; i++) {
        printf(", [%s: %.2f]", dis_header[i], (float)tr->write_io[i] / tr->write_count);
    }
    printf("\n\n");
    // printf("pending_rq: %llu\n", tr->pending_rq);
    // spin_unlock(&tr->lock);
}

static void serialize_blk_tr(struct blk_stat *tr, FILE *file) {
    // serialize the struct to a binary file
    size_t f = fwrite(tr, sizeof(struct blk_stat), 1, file);
    if (f != 1) {
        perror("fwrite");
    }
}

static void deserialize_blk_tr(struct blk_stat *tr, FILE *file) {
    // deserialize the struct from a binary file
    size_t t = fread(tr, sizeof(struct blk_stat), 1, file);
    if (t != 1) {
        perror("fread");
    }
}