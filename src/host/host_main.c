#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/mutex.h>
#include <linux/delay.h>

#include "../include/config.h"
#include "../include/ntprof_ctl.h"
#include "trace_nvme_tcp.h"
#include "trace_blk.h"
#include "host.h"
#include "host_logging.h"
#include "analyzer.h"
#include "serialize.h"

#define DEVICE_NAME "ntprof"
#define CLASS_NAME "ntprof_class"

static struct class* ntprof_class;
static struct cdev ntprof_cdev;
static dev_t dev_num;
static DEFINE_MUTEX(ntprof_mutex);

struct ntprof_config global_config;
struct per_core_statistics stat[MAX_CORE_NUM];

// atomic counter
atomic_t trace_on = ATOMIC_INIT(0);

static int is_profiling = 0;

// Function declarations
void register_tracepoints(void);

void unregister_tracepoints(void);

void init_variables(void);

void clear_up(void);

void register_tracepoints(void) {
  register_blk_tracepoints();
  register_nvme_tcp_tracepoints();
  // set trace on to 1
  atomic_set(&trace_on, 1);
  pr_debug("ntprof: Tracepoints registered successfully\n");
}

void unregister_tracepoints(void) {
  unregister_blk_tracepoints();
  unregister_nvme_tcp_tracepoints();
  // set trace on to 0
  atomic_set(&trace_on, 0);
  pr_debug("ntprof: Tracepoints unregistered successfully\n");
}

void init_variables(void) {
  int i;
  pr_debug("ntprof: Initializing per-core statistics\n");
  for (i = 0; i < MAX_CORE_NUM; i++) {
    init_per_core_statistics(&stat[i]);
  }
}

void clear_up(void) {
  pr_debug("ntprof: Clearing up resources\n");
  if (is_profiling) {
    unregister_tracepoints();
  }
  int i;
  for (i = 0; i < MAX_CORE_NUM; i++) {
    SPINLOCK_IRQSAVE_DISABLEPREEMPT_Q(&stat[i].lock, "clear_up", i);
    free_per_core_statistics(&stat[i]);
    SPINUNLOCK_IRQRESTORE_ENABLEPREEMPT_Q(&stat[i].lock, "clear_up", i);
  }
}

char* print_cmd(unsigned int cmd) {
  switch (cmd) {
    case NTPROF_IOCTL_START:
      return "NTPROF_IOCTL_START";
    case NTPROF_IOCTL_STOP:
      return "NTPROF_IOCTL_STOP";
    case NTPROF_IOCTL_ANALYZE:
      return "NTPROF_IOCTL_ANALYZE";
    default:
      return "UNKNOWN";
  }
}

static long ntprof_ioctl(struct file* file, unsigned int cmd,
                         unsigned long arg) {
  pr_debug("!ntprof: IOCTL command received: %s\n", print_cmd(cmd));
  int previous_stat;
  struct analyze_arg aarg;
  struct analyze_arg __user * uarg = (struct analyze_arg __user *)arg;

  switch (cmd) {
    case NTPROF_IOCTL_START:
      pr_debug("NTPROF_IOCTL_START: try to copy from user, size is %d\n",
               sizeof(struct ntprof_config));
      if (copy_from_user(&global_config, (struct ntprof_config __user *)arg,
                         sizeof(struct ntprof_config))) {
        pr_err("ntprof: Failed to copy config from user\n");
        return -EFAULT;
      }
      is_profiling = 1;
      register_tracepoints();
      break;

    case NTPROF_IOCTL_STOP:
      if (!is_profiling) {
        pr_warn("ntprof: Profiling is not active\n");
        return 0;
      }
      is_profiling = 0;
      unregister_tracepoints();


    // if is offline mode, flush the profile records to the files
      if (!global_config.is_online) {
        msleep(200);
        pr_debug("ntprof: Flushing profile records to files, datadir=%s\n", global_config.data_dir);
        serialize_all_cores(global_config.data_dir, stat);
      }
      break;

    case NTPROF_IOCTL_ANALYZE:
      if (global_config.is_online) {
        previous_stat = is_profiling;
        if (is_profiling) {
          is_profiling = 0;
          unregister_tracepoints();
          pr_debug("ntprof: Profiling temporarily stopped for analysis\n");
        }
        msleep(200);
        // call the analyze function to assign value to ret
        memset(&aarg, 0, sizeof(aarg));
        analyze(&global_config, &aarg.rpt);

        if (copy_to_user(uarg, &aarg, sizeof(aarg))) {
          pr_err("ntprof: Failed to copy analysis result to user\n");
          return -EFAULT;
        }

        if (previous_stat) {
          is_profiling = 1;
          register_tracepoints();
          pr_debug("ntprof: Profiling resumed: %s\n",
                   global_config.session_name);
        }
      } else {
        // clear stat
        int i;
        for (i = 0; i < MAX_CORE_NUM; i++) {
          free_per_core_statistics(&stat[i]);
          init_per_core_statistics(&stat[i]);
        }
        deserialize_all_cores(global_config.data_dir, stat);

        memset(&aarg, 0, sizeof(aarg));
        analyze(&global_config, &aarg.rpt);
        if (copy_to_user(uarg, &aarg, sizeof(aarg))) {
          pr_err("ntprof: Failed to copy analysis result to user\n");
          return -EFAULT;
        }
      }

      break;

    default:
      pr_err("ntprof: Invalid IOCTL command\n");
      return -EINVAL;
  }
  return 0;
}

static int ntprof_open(struct inode* inode, struct file* file) {
  if (!mutex_trylock(&ntprof_mutex)) {
    pr_alert("ntprof: Device is busy\n");
    return -EBUSY;
  }
  pr_debug("ntprof: Device opened\n");
  return 0;
}

static int ntprof_release(struct inode* inode, struct file* file) {
  mutex_unlock(&ntprof_mutex);
  pr_debug("ntprof: Device released\n");
  return 0;
}

static struct file_operations fops = {
    .open = ntprof_open,
    .release = ntprof_release,
    .unlocked_ioctl = ntprof_ioctl,
};

static int __init ntprof_host_module_init(void) {
  pr_debug("ntprof_host: Module loading...\n");

  if (alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME) < 0) {
    pr_alert("ntprof_host: Failed to allocate char device region\n");
    return -1;
  }

  ntprof_class = class_create(THIS_MODULE, CLASS_NAME);
  if (IS_ERR(ntprof_class)) {
    unregister_chrdev_region(dev_num, 1);
    return PTR_ERR(ntprof_class);
  }

  cdev_init(&ntprof_cdev, &fops);
  ntprof_cdev.owner = THIS_MODULE;
  if (cdev_add(&ntprof_cdev, dev_num, 1) < 0) {
    class_destroy(ntprof_class);
    unregister_chrdev_region(dev_num, 1);
    return -1;
  }

  if (device_create(ntprof_class, NULL, dev_num, NULL, DEVICE_NAME) == NULL) {
    cdev_del(&ntprof_cdev);
    class_destroy(ntprof_class);
    unregister_chrdev_region(dev_num, 1);
    return -1;
  }

  init_variables();
  pr_info("ntprof_host: Module loaded successfully\n");
  return 0;
}

static void __exit ntprof_host_module_exit(void) {
  pr_debug("ntprof_host: Module unloading...\n");
  clear_up();

  if (ntprof_class) {
    device_destroy(ntprof_class, dev_num);
    class_destroy(ntprof_class);
  }

  cdev_del(&ntprof_cdev);
  unregister_chrdev_region(dev_num, 1);
  pr_debug("ntprof_host: Module unloaded successfully\n");
}

module_init(ntprof_host_module_init);
module_exit(ntprof_host_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("yuyuan");
MODULE_DESCRIPTION(
    "Kernel module that collects I/O statistics on the host side");
MODULE_VERSION("1.0");