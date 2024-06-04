#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/spinlock.h>
#else
#include <stdio.h>
#endif

#ifdef __KERNEL__
struct _blk_tr {
    /** every io */
    atomic64_t read_count;
    atomic64_t write_count;
    
    /** sampled io */
    atomic64_t read_io[9];
    atomic64_t write_io[9];

    /** just once */
    atomic64_t queue_length;
};
#define _LT_4K 0
#define _4K 1
#define _8K 2
#define _16K 3
#define _32K 4
#define _64K 5
#define _128K 6
#define _GT_128K 7
#define _OTHERS 8

static void _init_blk_tr(struct _blk_tr *tr) {
    atomic64_set(&tr->read_count, 0);
    atomic64_set(&tr->write_count, 0);
    int i;
    for (i = 0; i < 9; i++) {
        atomic64_set(&tr->read_io[i], 0);
        atomic64_set(&tr->write_io[i], 0);
    }
    atomic64_set(&tr->queue_length, 0);
}

#endif

struct blk_tr {
    /** mutex */
    // spinlock_t lock;

    /** every io */
    unsigned long long read_count;
    unsigned long long write_count;
    
    /** sampled io */
    unsigned long long read_io[9];
    unsigned long long write_io[9];

    /** just once */
    unsigned long long queue_length;
};

#ifdef __KERNEL__

static void copy_blk_tr(struct blk_tr *dst, struct _blk_tr *src) {
    // spin_lock(&dst->lock);
    // read atomic data
    dst->read_count = atomic64_read(&src->read_count);
    dst->write_count = atomic64_read(&src->write_count);
    int i;
    for (i = 0; i < 9; i++) {
        dst->read_io[i] = atomic64_read(&src->read_io[i]);
        dst->write_io[i] = atomic64_read(&src->write_io[i]);
    }
    dst->queue_length = atomic64_read(&src->queue_length);
    // spin_unlock(&dst->lock);
}

static void init_blk_tr(struct blk_tr *tr) {
    // spin_lock_init(&tr->lock);
    tr->read_count = 0;
    tr->write_count = 0;
    int i;
    for (i = 0; i < 9; i++) {
        tr->read_io[i] = 0;
        tr->write_io[i] = 0;
    }
    tr->queue_length = 0;
}

#else

static void pr_blk_tr(struct blk_tr *tr) {
    char *dis_header[9] = {"<4KB", "4KB", "8KB", "16KB", "32KB", "64KB", "128KB", ">128KB", "others"};
    // spin_lock(&tr->lock);
    printf("blk_tr report>>>>>>>>>>>>>>>>>\n");
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
    printf("\n");
    // spin_unlock(&tr->lock);
}

static void serialize_blk_tr(struct blk_tr *tr, FILE *file) {
    // serialize the struct to a binary file
    size_t f = fwrite(tr, sizeof(struct blk_tr), 1, file);
    if (f != 1) {
        perror("fwrite");
    }
}

static void deserialize_blk_tr(struct blk_tr *tr, FILE *file) {
    // deserialize the struct from a binary file
    size_t t = fread(tr, sizeof(struct blk_tr), 1, file);
    if (t != 1) {
        perror("fread");
    }
}

#endif









