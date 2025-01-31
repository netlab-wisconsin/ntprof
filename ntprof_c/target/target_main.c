#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

#include "trace_nvmet_tcp.h"

void register_tracepoints(void) {
    register_nvmet_tcp_tracepoints();
}

void unregister_tracepoints(void) {
    unregister_nvmet_tcp_tracepoints();
}

void clean_up(void) {
    unregister_tracepoints();
}

static int __init ntprof_host_module_init(void) {
    pr_info("ntprof_target: Module loaded\n");
    register_tracepoints();
    return 0;
}

static void __exit ntprof_host_module_exit(void) {
    pr_info("ntprof_target: Module unloaded\n");
    clean_up();
}

module_init(ntprof_host_module_init);
module_exit(ntprof_host_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("yuyuan");
MODULE_DESCRIPTION("The kernel module that collects I/O statistics on the target side");
MODULE_VERSION("1.0");
