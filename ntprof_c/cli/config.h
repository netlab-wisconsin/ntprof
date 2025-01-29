#ifndef CONFIG_H
#define CONFIG_H

#include <stdio.h>

enum EIoType { READ, WRITE, BOTH } ;

struct ntprof_config {
  // Workload Specification
  char session_name[128];              // Name of the profiling session
  enum EIoType io_type;                // Type of IO to collect
  int io_size[32];                     // Fixed size of IO to collect, in bytes
  int min_io_size;                     // Minimum size of IO to collect
  int max_io_size;                     // Maximum size of IO to collect

  // Execution Specification
  int network_bandwidth_limit;         // Network bandwidth limit in MB/s
  int nvme_drive_bandwidth_limit;      // NVMe drive bandwidth limit in MB/s

  // Profiling Specification
  int is_online;                       // Profiling mode: online or offline
  int time_interval;                   // Time interval in seconds
  int frequency;                       // Sampling rate
  int buffer_size;                     // Buffer size in MB
  char aggregation[32];                // Aggregation function: min, max, avg, dist

  // Report Specification
  int enable_latency_distribution;    // Enable latency distribution reporting
  int enable_throughput;              // Enable throughput reporting
  int enable_latency_breakdown;       // Enable latency breakdown reporting
  int enable_queue_length;            // Enable queue length reporting
  int enable_group_by_size;           // Enable grouping by IO size in reports
  int enable_group_by_type;           // Enable grouping by IO type in reports
  int enable_group_by_session;        // Enable grouping by session in reports
};


// Helper: Parse size with units (e.g., 4K, 1M)
int parse_size_with_unit(const char *size_str) {
  int value = atoi(size_str);
  if (strchr(size_str, 'K') || strchr(size_str, 'k')) value *= 1024;
  if (strchr(size_str, 'M') || strchr(size_str, 'm')) value *= 1024 * 1024;
  return value;
}

// Helper: Parse IO_TYPE from string
enum EIoType parse_io_type(const char *str) {
  if (strcasecmp(str, "read") == 0) return READ;
  if (strcasecmp(str, "write") == 0) return WRITE;
  return BOTH; // Default to BOTH
}

// Helper: Parse boolean from string
int parse_boolean(const char *str) {
  return (strcasecmp(str, "true") == 0 || strcmp(str, "1") == 0);
}

// Function to parse config file into ntprof_config structure
int parse_config(const char *filename, struct ntprof_config *config) {
    dictionary *ini = iniparser_load(filename);
    if (ini == NULL) {
        fprintf(stderr, "Failed to load configuration file.\n");
        return -1;
    }

    // Initialize default values
    memset(config, 0, sizeof(struct ntprof_config));
    strncpy(config->session_name, "default_session", sizeof(config->session_name) - 1);
    config->io_type = BOTH;
    config->min_io_size = 0;
    config->max_io_size = 0;
    config->network_bandwidth_limit = -1;
    config->nvme_drive_bandwidth_limit = -1;
    config->is_online = 1;
    config->time_interval = 1;
    config->frequency = 1000;
    config->buffer_size = 1024;
    strncpy(config->aggregation, "avg", sizeof(config->aggregation) - 1);
    config->enable_latency_distribution = 1;
    config->enable_throughput = 1;
    config->enable_latency_breakdown = 1;
    config->enable_queue_length = 1;
    config->enable_group_by_size = 1;
    config->enable_group_by_type = 1;
    config->enable_group_by_session = 1;

    // Parse Workload section
    const char *session_name = iniparser_getstring(ini, "Workload:SESSION_NAME", "default_session");
    const char *io_type = iniparser_getstring(ini, "Workload:IO_TYPE", "both");
    const char *io_size = iniparser_getstring(ini, "Workload:IO_SIZE", NULL);
    const char *min_io_size = iniparser_getstring(ini, "Workload:MIN_IO_SIZE", NULL);
    const char *max_io_size = iniparser_getstring(ini, "Workload:MAX_IO_SIZE", NULL);

    strncpy(config->session_name, session_name, sizeof(config->session_name) - 1);
    config->io_type = parse_io_type(io_type);
    if (io_size) {
        config->io_size[0] = parse_size_with_unit(io_size);
    }
    if (min_io_size) {
        config->min_io_size = parse_size_with_unit(min_io_size);
    }
    if (max_io_size) {
        config->max_io_size = parse_size_with_unit(max_io_size);
    }

    // Parse Execution section
    config->network_bandwidth_limit = iniparser_getint(ini, "Execution:NETWORK_BANDWIDTH_LIMIT", -1);
    config->nvme_drive_bandwidth_limit = iniparser_getint(ini, "Execution:NVME_DRIVE_BANDWIDTH_LIMIT", -1);

    // Parse Profiling section
    config->is_online = parse_boolean(iniparser_getstring(ini, "Profiling:IS_ONLINE", "true"));
    config->time_interval = iniparser_getint(ini, "Profiling:TIME_INTERVAL", 1);
    config->frequency = iniparser_getint(ini, "Profiling:FREQUENCY", 1000);
    config->buffer_size = iniparser_getint(ini, "Profiling:BUFFER_SIZE", 1024);
    const char *aggregation = iniparser_getstring(ini, "Profiling:AGGREGATION", "avg");
    strncpy(config->aggregation, aggregation, sizeof(config->aggregation) - 1);

    // Parse Report section
    config->enable_latency_distribution = parse_boolean(iniparser_getstring(ini, "Report:ENABLE_LATENCY_DISTRIBUTION", "true"));
    config->enable_throughput = parse_boolean(iniparser_getstring(ini, "Report:ENABLE_THROUGHPUT", "true"));
    config->enable_latency_breakdown = parse_boolean(iniparser_getstring(ini, "Report:ENABLE_LATENCY_BREAKDOWN", "true"));
    config->enable_queue_length = parse_boolean(iniparser_getstring(ini, "Report:ENABLE_QUEUE_LENGTH", "true"));
    config->enable_group_by_size = parse_boolean(iniparser_getstring(ini, "Report:ENABLE_GROUP_BY_SIZE", "true"));
    config->enable_group_by_type = parse_boolean(iniparser_getstring(ini, "Report:ENABLE_GROUP_BY_TYPE", "true"));
    config->enable_group_by_session = parse_boolean(iniparser_getstring(ini, "Report:ENABLE_GROUP_BY_SESSION", "true"));

    iniparser_freedict(ini);
    return 0;
}


#endif //CONFIG_H
