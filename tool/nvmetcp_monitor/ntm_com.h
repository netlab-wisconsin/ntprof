struct blk_stat {
    /** mutex */
    // spinlock_t lock;

    /** every io */
    unsigned long long read_count;
    unsigned long long write_count;
    
    /** sampled io */
    unsigned long long read_io[9];
    unsigned long long write_io[9];

    unsigned long long pending_rq;
};

static void clean_blk_stat(struct blk_stat *tr) {
    tr->read_count = 0;
    tr->write_count = 0;
    int i;
    for (i = 0; i < 9; i++) {
        tr->read_io[i] = 0;
        tr->write_io[i] = 0;
    }
    tr->pending_rq = 0;
}