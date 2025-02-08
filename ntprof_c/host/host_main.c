#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/mutex.h>

#include "../include/config.h"
#include "../include/ntprof_ctl.h"
#include "trace_nvme_tcp.h"
#include "trace_blk.h"
#include "host.h"
#include "host_logging.h"
#include "analyzer.h"

#define DEVICE_NAME "ntprof"
#define CLASS_NAME "ntprof_class"

static struct class *ntprof_class;
static struct cdev ntprof_cdev;
static dev_t dev_num;
static DEFINE_MUTEX(ntprof_mutex);

struct ntprof_config global_config;
struct per_core_statistics stat[MAX_CORE_NUM];

static int is_profiling = 0;

// Function declarations
void register_tracepoints(void);
void unregister_tracepoints(void);
void init_variables(void);
void clear_up(void);

void register_tracepoints(void) {
    register_blk_tracepoints();
    register_nvme_tcp_tracepoints();
    pr_info("ntprof: Tracepoints registered successfully\n");
}

void unregister_tracepoints(void) {
    unregister_blk_tracepoints();
    unregister_nvme_tcp_tracepoints();
    pr_info("ntprof: Tracepoints unregistered successfully\n");
}

void init_variables(void) {
    int i;
    pr_info("ntprof: Initializing per-core statistics\n");
    for (i = 0; i < MAX_CORE_NUM; i++) {
        init_per_core_statistics(&stat[i]);
    }
}

void clear_up(void) {
    pr_info("ntprof: Clearing up resources\n");
    if (is_profiling) {
        unregister_tracepoints();
    }
    int i;
    for (i = 0; i < MAX_CORE_NUM; i++) {
        free_per_core_statistics(&stat[i]);
    }
}

static long ntprof_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
    int previous_stat;
    struct analyze_arg aarg;
    struct analyze_arg __user *uarg = (struct analyze_arg __user *) arg;

    switch (cmd) {
        case NTPROF_IOCTL_START:
            if (copy_from_user(&global_config, (struct ntprof_config __user *) arg, sizeof(struct ntprof_config))) {
                pr_err("ntprof: Failed to copy config from user\n");
                return -EFAULT;
            }
            is_profiling = 1;
            register_tracepoints();
            pr_info("ntprof: Profiling started: %s\n", global_config.session_name);
            print_config(&global_config);
            break;

        case NTPROF_IOCTL_STOP:
            if (!is_profiling) {
                pr_warn("ntprof: Profiling is not active\n");
                return -EINVAL;
            }
            is_profiling = 0;
            unregister_tracepoints();
            pr_info("ntprof: Profiling stopped\n");
            break;

        case NTPROF_IOCTL_ANALYZE:
            previous_stat = is_profiling;
            if (is_profiling) {
                is_profiling = 0;
                unregister_tracepoints();
                pr_info("ntprof: Profiling temporarily stopped for analysis\n");
            }


            struct profile_result ret = {
                .total_io = 0
            };

        // call the analyze function to assign value to ret
            analyze(&global_config, &ret);

            memset(&aarg, 0, sizeof(aarg)); // Initialize structure
            aarg.result.total_io = ret.total_io; // TODO: Implement analysis

            if (copy_to_user(uarg, &aarg, sizeof(aarg))) {
                pr_err("ntprof: Failed to copy analysis result to user\n");
                return -EFAULT;
            }

            if (previous_stat) {
                is_profiling = 1;
                register_tracepoints();
                pr_info("ntprof: Profiling resumed: %s\n", global_config.session_name);
            }
            break;

        default:
            pr_err("ntprof: Invalid IOCTL command\n");
            return -EINVAL;
    }
    return 0;
}

static int ntprof_open(struct inode *inode, struct file *file) {
    if (!mutex_trylock(&ntprof_mutex)) {
        pr_alert("ntprof: Device is busy\n");
        return -EBUSY;
    }
    pr_info("ntprof: Device opened\n");
    return 0;
}

static int ntprof_release(struct inode *inode, struct file *file) {
    mutex_unlock(&ntprof_mutex);
    pr_info("ntprof: Device released\n");
    return 0;
}

static struct file_operations fops = {
    .open = ntprof_open,
    .release = ntprof_release,
    .unlocked_ioctl = ntprof_ioctl,
};

static int __init ntprof_host_module_init(void) {
    pr_info("ntprof_host: Module loading...\n");
    
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
    pr_info("ntprof_host: Module unloading...\n");
    clear_up();

    if (ntprof_class) {
        device_destroy(ntprof_class, dev_num);
        class_destroy(ntprof_class);
    }

    cdev_del(&ntprof_cdev);
    unregister_chrdev_region(dev_num, 1);
    pr_info("ntprof_host: Module unloaded successfully\n");
}

module_init(ntprof_host_module_init);
module_exit(ntprof_host_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("yuyuan");
MODULE_DESCRIPTION("Kernel module that collects I/O statistics on the host side");
MODULE_VERSION("1.0");
