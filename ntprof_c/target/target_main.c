#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

static int __init ntprof_host_module_init(void) {
    pr_info("ntprof_target: Module loaded\n");
    return 0;
}

static void __exit ntprof_host_module_exit(void) {
    pr_info("ntprof_target: Module unloaded\n");
}

module_init(ntprof_host_module_init);
module_exit(ntprof_host_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("yuyuan");
MODULE_DESCRIPTION("The kernel module that collects I/O statistics on the target side");
MODULE_VERSION("1.0");
