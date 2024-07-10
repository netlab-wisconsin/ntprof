#ifndef _K_TCP_LAYER_H_
#define _K_TCP_LAYER_H_

#include <linux/atomic.h>
#include <linux/spinlock.h>

#include "config.h"

struct atomic_tcp_stat_of_one_queue {
  atomic_t pkt_in_flight;
  atomic_t cwnd;
  char last_event[64];
  spinlock_t lock;
};

struct atomic_tcp_stat {
  struct atomic_tcp_stat_of_one_queue sks[MAX_QID];
};

static inline void init_atomic_tcp_stat(struct atomic_tcp_stat *stat) {
  int i;
  for (i = 0; i < MAX_QID; i++) {
    atomic_set(&stat->sks[i].pkt_in_flight, 0);
    atomic_set(&stat->sks[i].cwnd, 0);
    spin_lock_init(&stat->sks[i].lock);
    stat->sks[i].last_event[0] = '\0';
  }
}


void tcp_stat_update(void);

int init_tcp_layer(void);

void exit_tcp_layer(void);

#endif  // _K_TCP_LAYER_H_