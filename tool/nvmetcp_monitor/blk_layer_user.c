#include "blk_layer_user.h"

void print_blk_stat(struct blk_stat *b_stat, char *header) {
  printf("header: %s \t", header);
  printf("device_name: %s\n", blk_dev_name);
  char *dis_header[9] = {"<4KB", "4KB",   "8KB",    "16KB",  "32KB",
                         "64KB", "128KB", ">128KB", "others"};
  // spin_lock(&tr->lock);
  printf("read total: %llu\n", b_stat->read_count);
  printf("read cnt");
  int i;
  for (i = 0; i < 9; i++) {
    printf(", \t[%s: %llu]", dis_header[i], b_stat->read_io[i]);
  }
  printf("\nread dis");
  for (i = 0; i < 9; i++) {
    printf(", \t[%s: %.2f]", dis_header[i],
           (float)b_stat->read_io[i] / b_stat->read_count);
  }
  printf("\nwrite total: %llu\n", b_stat->write_count);
  printf("write cnt");
  for (i = 0; i < 9; i++) {
    printf(", \t[%s: %llu]", dis_header[i], b_stat->write_io[i]);
  }
  printf("\nwrite dis");
  for (i = 0; i < 9; i++) {
    printf(", \t[%s: %.2f]", dis_header[i],
           (float)b_stat->write_io[i] / b_stat->write_count);
  }
  printf("\n\n");
}

void print_blk_stat_set(struct blk_stat_set *bs, bool clear) {
  if (clear) printf("\033[H\033[J");
  print_blk_stat(bs->raw_blk_stat, "RAW BLK STAT");
  print_blk_stat(bs->blk_stat_10s, "LAST 10s");
  print_blk_stat(bs->blk_stat_2s, "LAST 2s");
}

void serialize_blk_tr(struct blk_stat *tr, FILE *file) {
  // serialize the struct to a binary file
  size_t f = fwrite(tr, sizeof(struct blk_stat), 1, file);
  if (f != 1) {
    perror("fwrite");
  }
}

void deserialize_blk_tr(struct blk_stat *tr, FILE *file) {
  // deserialize the struct from a binary file
  size_t t = fread(tr, sizeof(struct blk_stat), 1, file);
  if (t != 1) {
    perror("fread");
  }
}

void init_ntm_blk(char *dev_name, struct blk_stat_set *bs) {
  blk_set = bs;
  blk_dev_name = dev_name;
}



void start_ntm_blk() {
  if (!blk_set) {
    printf("blk_set is not initialized\n");
    return;
  }
  int control_fd, raw_blk_stat_fd, param_fd, sample_10s_fd, sample_2s_fd;

  /** map the raw_blk_stat*/
  raw_blk_stat_fd = open("/proc/ntm/ntm_raw_blk_stat", O_RDONLY);
  if (raw_blk_stat_fd == -1) {
    perror("open");
    exit(EXIT_FAILURE);
  }
  blk_set->raw_blk_stat = mmap(NULL, sizeof(struct blk_stat), PROT_READ,
                               MAP_SHARED, raw_blk_stat_fd, 0);
  if (blk_set->raw_blk_stat == MAP_FAILED) {
    perror("mmap");
    close(raw_blk_stat_fd);
    exit(EXIT_FAILURE);
  }
  close(raw_blk_stat_fd);

  /** map the blk_stat_10s and blk_stat_2s */
  sample_10s_fd = open("/proc/ntm/ntm_sample_10s", O_RDONLY);
  if (sample_10s_fd == -1) {
    perror("open");
    munmap(blk_set->raw_blk_stat, sizeof(struct blk_stat));
    close(sample_10s_fd);
    exit(EXIT_FAILURE);
  }
  blk_set->blk_stat_10s = mmap(NULL, sizeof(struct blk_stat), PROT_READ,
                               MAP_SHARED, sample_10s_fd, 0);
  if (blk_set->blk_stat_10s == MAP_FAILED) {
    perror("mmap");
    munmap(blk_set->raw_blk_stat, sizeof(struct blk_stat));
    close(sample_10s_fd);
    exit(EXIT_FAILURE);
  }
  close(sample_10s_fd);

  sample_2s_fd = open("/proc/ntm/ntm_sample_2s", O_RDONLY);
  if (sample_2s_fd == -1) {
    perror("open");
    munmap(blk_set->raw_blk_stat, sizeof(struct blk_stat));
    munmap(blk_set->blk_stat_10s, sizeof(struct blk_stat));
    close(sample_2s_fd);
    exit(EXIT_FAILURE);
  }
  blk_set->blk_stat_2s = mmap(NULL, sizeof(struct blk_stat), PROT_READ,
                              MAP_SHARED, sample_2s_fd, 0);
  if (blk_set->blk_stat_2s == MAP_FAILED) {
    perror("mmap");
    munmap(blk_set->raw_blk_stat, sizeof(struct blk_stat));
    munmap(blk_set->blk_stat_10s, sizeof(struct blk_stat));
    close(sample_2s_fd);
    exit(EXIT_FAILURE);
  }
  close(sample_2s_fd);
}

void exit_ntm_blk() {
  munmap(blk_set->raw_blk_stat, sizeof(struct blk_stat));
  munmap(blk_set->blk_stat_10s, sizeof(struct blk_stat));
  munmap(blk_set->blk_stat_2s, sizeof(struct blk_stat));
}