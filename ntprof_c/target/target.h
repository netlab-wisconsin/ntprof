#ifndef TARGET_H
#define TARGET_H

#define MAX_QUEUE_NUM 64

#include <linux/spinlock.h>
#include <linux/nvme-tcp.h>

#include "../include/statistics.h"

#define SPINELOCK_IRQSAVE(x, name) \
unsigned long __flags; \
if (spin_is_locked(x) && raw_smp_processor_id() == smp_processor_id()) { \
pr_err("WARNING: CPU %d already holds lock %s!, %s\n", smp_processor_id(), name, check_irq()); \
} else { \
spin_lock_irqsave(x, __flags); \
}

#define SPINUNLOCK_IRQRESTORE(x, name) \
if(spin_is_locked(x)){ \
spin_unlock_irqrestore(x, __flags); \
} else { \
pr_err("WARNING: CPU %d does not hold lock %s!, %s\n", smp_processor_id(), name, check_irq()); \
}

#define SPINLOCK_IRQSAVE_DISABLEPREEMPT(x, name) \
    unsigned long __flags; \
    if (spin_is_locked(x) && raw_smp_processor_id() == smp_processor_id()) { \
        pr_err("WARNING: CPU %d already holds lock %s!, %s\n", smp_processor_id(), name, check_irq()); \
    } else { \
        preempt_disable(); \
        spin_lock_irqsave(x, __flags); \
    }

#define SPINUNLOCK_IRQRESTORE_ENABLEPREEMPT(x, name) \
    if(spin_is_locked(x)){ \
        spin_unlock_irqrestore(x, __flags); \
        preempt_enable();\
    } else { \
        pr_err("WARNING: CPU %d does not hold lock %s!, %s\n", smp_processor_id(), name, check_irq()); \
    }

#define SPINLOCK_IRQSAVE_DISABLEPREEMPT_DISABLEBH(x, name) \
unsigned long __flags; \
if (spin_is_locked(x) && raw_smp_processor_id() == smp_processor_id()) { \
pr_err("WARNING: CPU %d already holds lock %s!, %s\n", smp_processor_id(), name, check_irq()); \
} else { \
local_bh_disable(); \
preempt_disable(); \
spin_lock_irqsave(x, __flags); \
}

#define SPINUNLOCK_IRQRESTORE_ENABLEPREEMPT_ENABLEBH(x, name) \
if(spin_is_locked(x)){ \
spin_unlock_irqrestore(x, __flags); \
preempt_enable();\
local_bh_enable();\
} else { \
pr_err("WARNING: CPU %d does not hold lock %s!, %s\n", smp_processor_id(), name, check_irq()); \
}

static inline char * check_irq(void) {
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

struct per_queue_statistics {
    struct list_head records;
    struct spinlock lock;
};

typedef void (*fn)(struct profile_record *record, unsigned long long timestamp, enum EEvent event,
                  struct ntprof_stat *s);

void init_per_queue_statistics(struct per_queue_statistics *pqs);

void free_per_queue_statistics(struct per_queue_statistics *pqs);

void append_record(struct per_queue_statistics *pqs, struct profile_record *r);

struct profile_record *get_profile_record(struct per_queue_statistics *pqs, int cmdid);

int try_remove_record(struct per_queue_statistics *pqs, int cmdid, void *op, unsigned long long timestamp,
                       enum EEvent event, struct ntprof_stat *s);


extern struct per_queue_statistics stat[MAX_QUEUE_NUM];


#endif //TARGET_H
