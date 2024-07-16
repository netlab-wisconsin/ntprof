#ifndef _K_TCP_LAYER_H_
#define _K_TCP_LAYER_H_

#include <linux/types.h>
#include <linux/spinlock.h>

#include "config.h"

struct atomic_tcp_stat_of_one_queue {
  atomic_t pkt_in_flight;
  atomic_t cwnd;
  char last_event[64];
  atomic64_t skb_num;
  atomic64_t skb_size;
  spinlock_t lock;
};

static void inline init_atomic_tcp_stat_of_one_queue(struct atomic_tcp_stat_of_one_queue *stat) {
  atomic_set(&stat->pkt_in_flight, 0);
  atomic_set(&stat->cwnd, 0);
  atomic64_set(&stat->skb_num, 0);
  atomic64_set(&stat->skb_size, 0);
  spin_lock_init(&stat->lock);
}

struct atomic_tcp_stat {
  struct atomic_tcp_stat_of_one_queue sks[MAX_QID];
};

static void inline init_atomic_tcp_stat(struct atomic_tcp_stat *stat) {
  int i;
  for (i = 0; i < MAX_QID; i++) {
    init_atomic_tcp_stat_of_one_queue(&stat->sks[i]);
  }
}

void tcp_stat_update(void);

int init_tcp_layer(void);

void exit_tcp_layer(void);



#endif // _K_TCP_LAYER_H_