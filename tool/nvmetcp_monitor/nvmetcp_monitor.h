#ifndef _NVMETCP_TR_H
#define _NVMETCP_TR_H

#ifdef __KERNEL__
#include <linux/atomic.h>
#else
#include <stdatomic.h>
#endif

struct nvmetcp_tr {
#ifdef __KERNEL__
    atomic64_t read_count;
    atomic64_t write_count;
    // use an array to represent the slots
    // 0: <4k, 1: 4k, 2: 8k, 3: 16k, 4: 32k, 5: 64k, 6: 128k, 7: >128k, 8: others
    atomic64_t read_io[9];
    atomic64_t write_io[9];
    atomic64_t queue_length;
#else
    atomic_ullong read_count;
    atomic_ullong write_count;
    // use an array to represend the slots
    // 0: <4k, 1: 4k, 2: 8k, 3: 16k, 4: 32k, 5: 64k, 6: 128k, 7: >128k, 8: others
    atomic_ullong read_io[9];
    atomic_ullong write_io[9];
    atomic_ullong queue_length;
#endif
};

#ifdef __KERNEL__
#define INC_READ_LT_4K(tr) atomic64_inc(&tr->read_io[0])
#define INC_READ_4K(tr) atomic64_inc(&tr->read_io[1])
#define INC_READ_8K(tr) atomic64_inc(&tr->read_io[2])
#define INC_READ_16K(tr) atomic64_inc(&tr->read_io[3])
#define INC_READ_32K(tr) atomic64_inc(&tr->read_io[4])
#define INC_READ_64K(tr) atomic64_inc(&tr->read_io[5])
#define INC_READ_128K(tr) atomic64_inc(&tr->read_io[6])
#define INC_READ_GT_128K(tr) atomic64_inc(&tr->read_io[7])
#define INC_READ_OTHERS(tr) atomic64_inc(&tr->read_io[8])
#define INC_WRITE_LT_4K(tr) atomic64_inc(&tr->write_io[0])
#define INC_WRITE_4K(tr) atomic64_inc(&tr->write_io[1])
#define INC_WRITE_8K(tr) atomic64_inc(&tr->write_io[2])
#define INC_WRITE_16K(tr) atomic64_inc(&tr->write_io[3])
#define INC_WRITE_32K(tr) atomic64_inc(&tr->write_io[4])
#define INC_WRITE_64K(tr) atomic64_inc(&tr->write_io[5])
#define INC_WRITE_128K(tr) atomic64_inc(&tr->write_io[6])
#define INC_WRITE_GT_128K(tr) atomic64_inc(&tr->write_io[7])
#define INC_WRITE_OTHERS(tr) atomic64_inc(&tr->write_io[8])
#define SET_QUEUE_LENGTH(tr, len) atomic64_set(&tr->queue_length, len)

static void init_nvmetcp_tr(struct nvmetcp_tr *tr) {
    atomic64_set(&tr->read_count, 0);
    atomic64_set(&tr->write_count, 0);
    int i = 0;
    for(i = 0; i < 9; i++) {
        atomic64_set(&tr->read_io[i], 0);
        atomic64_set(&tr->write_io[i], 0);
    }
}
#else
#define GET_READ_LT_4K(tr) atomic_load(&tr->read_io[0])
#define GET_READ_4K(tr) atomic_load(&tr->read_io[1])
#define GET_READ_8K(tr) atomic_load(&tr->read_io[2])
#define GET_READ_16K(tr) atomic_load(&tr->read_io[3])
#define GET_READ_32K(tr) atomic_load(&tr->read_io[4])
#define GET_READ_64K(tr) atomic_load(&tr->read_io[5])
#define GET_READ_128K(tr) atomic_load(&tr->read_io[6])
#define GET_READ_GT_128K(tr) atomic_load(&tr->read_io[7])
#define GET_READ_OTHERS(tr) atomic_load(&tr->read_io[8])
#define GET_WRITE_LT_4K(tr) atomic_load(&tr->write_io[0])
#define GET_WRITE_4K(tr) atomic_load(&tr->write_io[1])
#define GET_WRITE_8K(tr) atomic_load(&tr->write_io[2])
#define GET_WRITE_16K(tr) atomic_load(&tr->write_io[3])
#define GET_WRITE_32K(tr) atomic_load(&tr->write_io[4])
#define GET_WRITE_64K(tr) atomic_load(&tr->write_io[5])
#define GET_WRITE_128K(tr) atomic_load(&tr->write_io[6])
#define GET_WRITE_GT_128K(tr) atomic_load(&tr->write_io[7])
#define GET_WRITE_OTHERS(tr) atomic_load(&tr->write_io[8])

static void print_tr(struct nvmetcp_tr* tr){
    printf("Read count: %llu\n", atomic_load(&tr->read_count));
    // print the distribution, ratio of different size
    printf("read distr: [<4K: %.2f], [4K: %.2f], [8K: %.2f], [16K: %.2f], [32K: %.2f], [64K: %.2f], [128K: %.2f], [>128K: %.2f], [others: %.2f]\n",
            (float)atomic_load(&tr->read_io[0]) / atomic_load(&tr->read_count),
            (float)atomic_load(&tr->read_io[1]) / atomic_load(&tr->read_count),
            (float)atomic_load(&tr->read_io[2]) / atomic_load(&tr->read_count),
            (float)atomic_load(&tr->read_io[3]) / atomic_load(&tr->read_count),
            (float)atomic_load(&tr->read_io[4]) / atomic_load(&tr->read_count),
            (float)atomic_load(&tr->read_io[5]) / atomic_load(&tr->read_count),
            (float)atomic_load(&tr->read_io[6]) / atomic_load(&tr->read_count),
            (float)atomic_load(&tr->read_io[7]) / atomic_load(&tr->read_count),
            (float)atomic_load(&tr->read_io[8]) / atomic_load(&tr->read_count)
            );
    printf("read cnt: [<4K: %llu], [4K: %llu], [8K: %llu], [16K: %llu], [32K: %llu], [64K: %llu], [128K: %llu], [>128K: %llu], [others: %llu]\n",
            atomic_load(&tr->read_io[0]),
            atomic_load(&tr->read_io[1]),
            atomic_load(&tr->read_io[2]),
            atomic_load(&tr->read_io[3]),
            atomic_load(&tr->read_io[4]),
            atomic_load(&tr->read_io[5]),
            atomic_load(&tr->read_io[6]),
            atomic_load(&tr->read_io[7]),
            atomic_load(&tr->read_io[8])
            );

    printf("Write count: %llu\n", atomic_load(&tr->write_count));
    printf("write distr: [4K: %.2f], [8K: %.2f], [16K: %.2f], [32K: %.2f], [64K: %.2f], [128K: %.2f], [>128K: %.2f], [others: %.2f]\n",
            (float)atomic_load(&tr->write_io[1]) / atomic_load(&tr->write_count),
            (float)atomic_load(&tr->write_io[2]) / atomic_load(&tr->write_count),
            (float)atomic_load(&tr->write_io[3]) / atomic_load(&tr->write_count),
            (float)atomic_load(&tr->write_io[4]) / atomic_load(&tr->write_count),
            (float)atomic_load(&tr->write_io[5]) / atomic_load(&tr->write_count),
            (float)atomic_load(&tr->write_io[6]) / atomic_load(&tr->write_count),
            (float)atomic_load(&tr->write_io[7]) / atomic_load(&tr->write_count),
            (float)atomic_load(&tr->write_io[8]) / atomic_load(&tr->write_count)
            );
    printf("write cnt: [4K: %llu], [8K: %llu], [16K: %llu], [32K: %llu], [64K: %llu], [128K: %llu], [>128K: %llu], [others: %llu]\n",
            atomic_load(&tr->write_io[1]),
            atomic_load(&tr->write_io[2]),
            atomic_load(&tr->write_io[3]),
            atomic_load(&tr->write_io[4]),
            atomic_load(&tr->write_io[5]),
            atomic_load(&tr->write_io[6]),
            atomic_load(&tr->write_io[7]),
            atomic_load(&tr->write_io[8])
            );

    printf("Queue length: %llu\n", atomic_load(&tr->queue_length));

}

#endif





#endif // _NVMETCP_TR_H
