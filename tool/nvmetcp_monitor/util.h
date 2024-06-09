#ifndef _UTIL_H_
#define _UTIL_H_

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/types.h>

/** sliding window */
struct sliding_window {
  /** a lock free linked list of <timestamp, request> */
  struct list_head list;
  /** count */
  atomic64_t count;
  spinlock_t lock;
};

/**
 * initialize a sliding window
 * @param sw: the sliding window to be initialized
 */
void init_sliding_window(struct sliding_window *sw) {
  INIT_LIST_HEAD(&sw->list);
  atomic64_set(&sw->count, 0);
  spin_lock_init(&sw->lock);
}

/**
 * A node in the sliding window
 * Since the sliding window stores time events, the node should have a timestamp
 */
struct sw_node {
  void *data;
  struct list_head list;
  u64 timestamp;
};

/**
 * Add a node to the sliding window. The node is appended to the tail of the list.
 * Assume the sliding window is in order, and the node is added in order.
 * @param sw: the sliding window
 * @param node: the node to be added
 */
void add_to_sliding_window(struct sliding_window *sw, struct sw_node *node) {
  spin_lock(&sw->lock);
  list_add_tail(&node->list, &sw->list);
  atomic64_inc(&sw->count);
  spin_unlock(&sw->lock);
}

/**
 * remove the node that is older than the given timestamp
 * @param sw: the sliding window
 * @param timestamp: the timestamp
 */
int remove_from_sliding_window(struct sliding_window *sw, u64 expire) {
  struct list_head *pos, *q;
  struct sw_node *node;
  u64 cnt = 0;
  spin_lock(&sw->lock);
  list_for_each_safe(pos, q, &sw->list) {
    node = list_entry(pos, struct sw_node, list);
    if (node->timestamp < expire) {
      list_del(pos);
      atomic64_dec(&sw->count);
      kfree(node->data);
      kfree(node);
      cnt++;
    } else {
      /** since we assume the sliding window is in order */
      break;
    }
  }
  spin_unlock(&sw->lock);
  return cnt;
}

#endif  // _UTIL_H_
