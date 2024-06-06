struct blk_tr {
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
