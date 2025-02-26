#include <linux/kernel.h>

#include "../include/config.h"
#include "host_logging.h"

void print_config(struct ntprof_config* config) {
  pr_info("Session Name: %s\n", config->session_name);
  pr_info("IO Type: %d\n", config->io_type);
  pr_info("IO Size: %d\n", config->io_size[0]);
  pr_info("Min IO Size: %d\n", config->min_io_size);
  pr_info("Max IO Size: %d\n", config->max_io_size);
  pr_info("Network Bandwidth Limit: %d\n", config->network_bandwidth_limit);
  pr_info("NVMe Drive Bandwidth Limit: %d\n",
          config->nvme_drive_bandwidth_limit);
  pr_info("Is Online: %d\n", config->is_online);
  pr_info("Time Interval: %d\n", config->time_interval);
  pr_info("Frequency: %d\n", config->frequency);
  pr_info("Buffer Size: %d\n", config->buffer_size);
  pr_info("Aggregation: %d\n", config->aggr);
  pr_info("Enable Latency Distribution: %d\n",
          config->enable_latency_distribution);
  pr_info("Enable Throughput: %d\n", config->enable_throughput);
  pr_info("Enable Latency Breakdown: %d\n", config->enable_latency_breakdown);
  pr_info("Enable Queue Length: %d\n", config->enable_queue_length);
  pr_info("Enable Group By Size: %d\n", config->enable_group_by_size);
  pr_info("Enable Group By Type: %d\n", config->enable_group_by_type);
  pr_info("Enable Group By Session: %d\n", config->enable_group_by_session);
}