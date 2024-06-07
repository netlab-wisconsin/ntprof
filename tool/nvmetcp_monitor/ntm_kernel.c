#include "blk_layer.h"

static int __init init_ntm_module(void) {
  return _init_ntm_blk_layer();
}

static void __exit exit_ntm_module(void) {
  if (update_routine_thread) {
    kthread_stop(update_routine_thread);
  }

  _exit_ntm_blk_layer();
}

module_init(init_ntm_module);
module_exit(exit_ntm_module);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("yuyuan");
MODULE_DESCRIPTION("Lightweight NVMeTCP Monitoring Module");
