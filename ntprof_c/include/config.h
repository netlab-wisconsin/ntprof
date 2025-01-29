#ifndef CONFIG_H
#define CONFIG_H

enum EIoType { IO_READ, IO_WRITE, BOTH };

enum EAggregation { MIN, MAX, AVG, DIST };

struct ntprof_config {
    // Workload Specification
    char session_name[128]; // Name of the profiling session
    enum EIoType io_type; // Type of IO to collect
    int io_size[32]; // Fixed size of IO to collect, in bytes
    int min_io_size; // Minimum size of IO to collect
    int max_io_size; // Maximum size of IO to collect

    // Execution Specification
    int network_bandwidth_limit; // Network bandwidth limit in MB/s
    int nvme_drive_bandwidth_limit; // NVMe drive bandwidth limit in MB/s

    // Profiling Specification
    int is_online; // Profiling mode: online or offline
    int time_interval; // Time interval in seconds
    int frequency; // Sampling rate
    int buffer_size; // Buffer size in MB
    enum EAggregation aggr; // Aggregation function: min, max, avg, dist

    // Report Specification
    int enable_latency_distribution; // Enable latency distribution reporting
    int enable_throughput; // Enable throughput reporting
    int enable_latency_breakdown; // Enable latency breakdown reporting
    int enable_queue_length; // Enable queue length reporting
    int enable_group_by_size; // Enable grouping by IO size in reports
    int enable_group_by_type; // Enable grouping by IO type in reports
    int enable_group_by_session; // Enable grouping by session in reports
};

int parse_config(const char *filename, struct ntprof_config *config);

void print_config(struct ntprof_config *config);

#endif //CONFIG_H
