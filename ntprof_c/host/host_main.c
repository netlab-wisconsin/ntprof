#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>

#include "../include/config.h"
#include "../include/ntprof_ctl.h"
#include "trace_nvme_tcp.h"
#include "trace_blk.h"
#include "host.h"

#define DEVICE_NAME "ntprof"
#define CLASS_NAME "ntprof_class"
#define MAX_CORE_NUM 128

static struct class *ntprof_class;
static struct cdev ntprof_cdev;
static dev_t dev_num;
struct ntprof_config global_config;
static int is_profiling = 0;


struct per_core_statistics stat[MAX_CORE_NUM];

struct ntprof_config config;

void register_tracepoints(void) {
    register_blk_tracepoints();
    register_nvme_tcp_tracepoints();
}

void unregister_tracepoints(void) {
    unregister_blk_tracepoints();
    unregister_nvme_tcp_tracepoints();
}

static long ntprof_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
    switch (cmd) {
        case NTPROF_IOCTL_START:
            if (copy_from_user(&global_config, (struct ntprof_config __user *) arg, sizeof(struct ntprof_config))) {
                return -EFAULT;
            }
            is_profiling = 1;
            register_tracepoints();
            printk(KERN_INFO "[ntprof] Profiling started: %s\n", global_config.session_name);
            print_config(&global_config);
            break;

        case NTPROF_IOCTL_STOP:
            is_profiling = 0;
            unregister_tracepoints();
            printk(KERN_INFO "[ntprof] Profiling stopped\n");
            break;

        default:
            return -EINVAL;
    }
    return 0;
}

static struct file_operations fops = {
    .unlocked_ioctl = ntprof_ioctl,
};

static int __init ntprof_host_module_init(void) {
    pr_info("ntprof_host: Module loaded\n");
    if (alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME) < 0) {
        printk(KERN_ALERT "Failed to allocate char device region\n");
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
    device_create(ntprof_class, NULL, dev_num, NULL, DEVICE_NAME);
    return 0;
}

static void __exit ntprof_host_module_exit(void) {
    device_destroy(ntprof_class, dev_num);
    class_destroy(ntprof_class);
    cdev_del(&ntprof_cdev);
    unregister_chrdev_region(dev_num, 1);
    printk(KERN_INFO "[ntprof] Module unloaded\n");
}

module_init(ntprof_host_module_init);
module_exit(ntprof_host_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("yuyuan");
MODULE_DESCRIPTION("The kernel module that collects I/O statistics on the host side");
MODULE_VERSION("1.0");
