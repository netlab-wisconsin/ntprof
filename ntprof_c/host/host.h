#ifndef HOST_H
#define HOST_H

#include "../include/config.h"
#include "../include/statistics.h"
#include <linux/types.h>
#include <linux/blkdev.h>
#include <linux/spinlock.h>

// #define SPINLOCK_IRQSAVE_DISABLEPREEMPT(x, name) \
//     unsigned long __flags; \
//     preempt_disable(); \
//     spin_lock_irqsave(x, __flags);
//
// #define SPINUNLOCK_IRQRESTORE_ENABLEPREEMPT(x, name) \
//     spin_unlock_irqrestore(x, __flags); \
//     preempt_enable(); \
//     pr_info("cpuid: %d, %s, unlock\n", smp_processor_id(), name);

// #define SPINLOCK_IRQSAVE_DISABLEPREEMPT(x, name) \
//     unsigned long __flags; \
//     if (spin_is_locked(x) && raw_smp_processor_id() == smp_processor_id()) { \
//     pr_err("WARNING: CPU %d already holds lock %s!, %s\n", smp_processor_id(), name, check_irq()); \
//     } else { \
//     local_bh_disable();\
//     preempt_disable(); \
//     spin_lock_irqsave(x, __flags); \
//     pr_info("cpuid: %d, %s, lock\n", smp_processor_id(), name); \
//     }
//
// #define SPINUNLOCK_IRQRESTORE_ENABLEPREEMPT(x, name) \
//     if(spin_is_locked(x)){ \
//     spin_unlock_irqrestore(x, __flags); \
//     local_bh_enable();\
//     preempt_enable();\
//     pr_info("cpuid: %d, %s, unlock\n", smp_processor_id(), name); \
//     } else { \
//     pr_err("WARNING: CPU %d does not hold lock %s!, %s\n", smp_processor_id(), name, check_irq()); \
//     }

// #define SPINLOCK_IRQSAVE_DISABLEPREEMPT_Q(x, name, qid) \
// unsigned long __flags; \
// if (spin_is_locked(x) && raw_smp_processor_id() == smp_processor_id()) { \
// pr_err("WARNING: queue lock %d is not available, CPU %d failed to get lock for %s, %s\n", qid, smp_processor_id(), name, check_irq()); \
// } else { \
// pr_info("cpuid: %d get queue lock: %d for %s, lock\n", smp_processor_id(), qid, name); \
// local_bh_disable();\
// preempt_disable(); \
// spin_lock_irqsave(x, __flags); \
// }
//
// #define SPINUNLOCK_IRQRESTORE_ENABLEPREEMPT_Q(x, name, qid) \
// if(spin_is_locked(x)){ \
// spin_unlock_irqrestore(x, __flags); \
// local_bh_enable();\
// preempt_enable();\
// pr_info("cpuid: %d release queue lock %d for %s, unlock\n", smp_processor_id(), qid, name); \
// } else { \
// pr_err("WARNING: CPU %d does not hold queue lock %d, failed to release for %s, %s\n", smp_processor_id(), qid, name, check_irq()); \
// }

// #define SPINLOCK_IRQSAVE_DISABLEPREEMPT_Q(x, name, qid) \
// unsigned long __flags; \
// pr_info("cpuid: %d get queue lock: %d for %s, lock\n", smp_processor_id(), qid, name); \
// local_bh_disable();\
// preempt_disable(); \
// spin_lock_irqsave(x, __flags);
//
// #define SPINUNLOCK_IRQRESTORE_ENABLEPREEMPT_Q(x, name, qid) \
// spin_unlock_irqrestore(x, __flags); \
// local_bh_enable();\
// preempt_enable();\
// pr_info("cpuid: %d release queue lock %d for %s, unlock\n", smp_processor_id(), qid, name);

#define SPINLOCK_IRQSAVE_DISABLEPREEMPT_Q(x, name, qid) \
  unsigned long __flags; \
  local_bh_disable();\
  preempt_disable(); \
  spin_lock_irqsave(x, __flags);

#define SPINUNLOCK_IRQRESTORE_ENABLEPREEMPT_Q(x, name, qid) \
  spin_unlock_irqrestore(x, __flags); \
  local_bh_enable();\
  preempt_enable();

#define LOCKQ(qid) \
  SPINLOCK_IRQSAVE_DISABLEPREEMPT_Q(&stat[qid].lock, __func__, qid)

#define UNLOCKQ(qid) \
  SPINUNLOCK_IRQRESTORE_ENABLEPREEMPT_Q(&stat[qid].lock, __func__, qid)


static inline char* check_irq(void) {
  if (in_interrupt()) {
    if (in_softirq()) {
      return "softirq";
    }
    if (in_irq()) {
      return "irq";
    }
    return "unknown";
  }
  return "process";
}

#define MAX_CORE_NUM 33

struct per_core_statistics {
  // mutex to protect
  // (1) working thread that is appending record
  // (2) analyzation thread that is reading record

  spinlock_t lock;

  unsigned long long sampler;
  int is_cleared;
  struct list_head incomplete_records;
  struct list_head completed_records;
  // struct list_head records;
};

void init_per_core_statistics(struct per_core_statistics* stats);

void free_per_core_statistics(struct per_core_statistics* stats);

void append_record(struct per_core_statistics* stats,
                   struct profile_record* record);

void complete_record(struct per_core_statistics* stats,
                     struct profile_record* record);

struct profile_record* get_profile_record(struct per_core_statistics* stats,
                                          struct request* req_tag);

// struct profile_record* get_profile_record_global(
//     struct request* req, struct per_core_statistics* stats);

bool match_config(struct request* req, struct ntprof_config* config);

extern struct per_core_statistics stat[MAX_CORE_NUM];

extern struct ntprof_config global_config;


// TODO: to remove
int get_list_len(struct per_core_statistics* stats);

// TODO: to remove
int print_incomplete_queue(struct per_core_statistics* stats);

extern atomic_t trace_on;

// void update_op_cnt(bool inc);

#endif //HOST_H