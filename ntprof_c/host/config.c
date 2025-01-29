#include <linux/kernel.h>

#include "../include/config.h"

void print_config(struct ntprof_config* config) {
    printk(KERN_INFO "[ntprof] Session Name: %s\n", config->session_name);
    printk(KERN_INFO "[ntprof] IO Type: %d\n", config->io_type);
    printk(KERN_INFO "[ntprof] IO Size: %d\n", config->io_size[0]);
    printk(KERN_INFO "[ntprof] Min IO Size: %d\n", config->min_io_size);
    printk(KERN_INFO "[ntprof] Max IO Size: %d\n", config->max_io_size);
    printk(KERN_INFO "[ntprof] Network Bandwidth Limit: %d\n", config->network_bandwidth_limit);
    printk(KERN_INFO "[ntprof] NVMe Drive Bandwidth Limit: %d\n", config->nvme_drive_bandwidth_limit);
    printk(KERN_INFO "[ntprof] Is Online: %d\n", config->is_online);
    printk(KERN_INFO "[ntprof] Time Interval: %d\n", config->time_interval);
    printk(KERN_INFO "[ntprof] Frequency: %d\n", config->frequency);
    printk(KERN_INFO "[ntprof] Buffer Size: %d\n", config->buffer_size);
    printk(KERN_INFO "[ntprof] Aggregation: %d\n", config->aggr);
    printk(KERN_INFO "[ntprof] Enable Latency Distribution: %d\n", config->enable_latency_distribution);
    printk(KERN_INFO "[ntprof] Enable Throughput: %d\n", config->enable_throughput);
    printk(KERN_INFO "[ntprof] Enable Latency Breakdown: %d\n", config->enable_latency_breakdown);
    printk(KERN_INFO "[ntprof] Enable Queue Length: %d\n", config->enable_queue_length);
    printk(KERN_INFO "[ntprof] Enable Group By Size: %d\n", config->enable_group_by_size);
    printk(KERN_INFO "[ntprof] Enable Group By Type: %d\n", config->enable_group_by_type);
    printk(KERN_INFO "[ntprof] Enable Group By Session: %d\n", config->enable_group_by_session);
}
