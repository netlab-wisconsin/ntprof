#ifndef _UTIL_H_
#define _UTIL_H_

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/string.h>
#include <stdbool.h>
#include <linux/slab.h>

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
static inline void init_sliding_window(struct sliding_window *sw) {
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
static inline void add_to_sliding_window(struct sliding_window *sw, struct sw_node *node) {
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
static inline int remove_from_sliding_window(struct sliding_window *sw, u64 expire) {
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

static inline void clear_sliding_window(struct sliding_window *sw) {
  struct list_head *pos, *q;
  struct sw_node *node;
  spin_lock(&sw->lock);
  list_for_each_safe(pos, q, &sw->list) {
    node = list_entry(pos, struct sw_node, list);
    list_del(pos);
    atomic64_dec(&sw->count);
    kfree(node->data);
    kfree(node);
  }
  spin_unlock(&sw->lock);
}



static inline bool parse_nvme_name(const char *name, int *ctrl_id, int *ns_id) {
    int path_id;

    // check if starts with "nvme"
    if (strstr(name, "nvme") != name)
        return false;

    // try to parse nvme<ctrl_id>c<path_id>n<ns_id>
    if (sscanf(name, "nvme%dc%dn%d", ctrl_id, &path_id, ns_id) == 3)
        return true;

    // try to parse nvme<ctrl_id>n<ns_id>
    if (sscanf(name, "nvme%dn%d", ctrl_id, ns_id) == 2)
        return true;

    return false;
}

/** todo this method can be time consuming */
static inline bool is_same_dev_name(char *name1, char *name2) {
    int ctrl_id1, ns_id1;
    int ctrl_id2, ns_id2;

    // if identical, return true
    if (strcmp(name1, name2) == 0)
        return true;

    // parse the first device name
    if (!parse_nvme_name(name1, &ctrl_id1, &ns_id1))
        return false;

    // parse the second device name
    if (!parse_nvme_name(name2, &ctrl_id2, &ns_id2))
        return false;

    // compare the parsed values
    return (ctrl_id1 == ctrl_id2) && (ns_id1 == ns_id2);
}

#endif  // _UTIL_H_
