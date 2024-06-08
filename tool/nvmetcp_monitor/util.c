#include <linux/module.h>
#include "util.h"

void print_hello(void) {
    pr_info("Hello, world\n");
}

MODULE_LICENSE("GPL");
