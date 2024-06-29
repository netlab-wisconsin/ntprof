#ifndef _K_TCP_LAYER_H_
#define _K_TCP_LAYER_H_

#include <linux/types.h>
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


void tcp_stat_update(void);

int init_tcp_layer(void);

void exit_tcp_layer(void);



#endif // _K_TCP_LAYER_H_