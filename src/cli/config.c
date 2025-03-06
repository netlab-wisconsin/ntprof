#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "../include/config.h"
#include <iniparser/iniparser.h>

#define DEFAULT_SESSION "all"
#define DEFAULT_IO_TYPE "both"
#define DEFAULT_DATA_DIR "./data"

// Helper: Parse size with units (e.g., 4K, 1M)
int parse_size_with_unit(const char* size_str) {
  int value = atoi(size_str);
  if (strchr(size_str, 'K') || strchr(size_str, 'k')) value *= 1024;
  if (strchr(size_str, 'M') || strchr(size_str, 'm')) value *= 1024 * 1024;
  return value;
}

// Helper: Parse IO_TYPE from string
enum EIoType parse_io_type(const char* str) {
  if (strcasecmp(str, "read") == 0) return IO_READ;
  if (strcasecmp(str, "write") == 0) return IO_WRITE;
  return BOTH; // Default to BOTH
}

enum EAggregation parse_aggr(const char* str) {
  if (strcasecmp(str, "avg") == 0) return AVG;
  if (strcasecmp(str, "min") == 0) return MIN;
  if (strcasecmp(str, "max") == 0) return MAX;
  if (strcasecmp(str, "dist") == 0) return DIST;
  return AVG; // Default to AVG
}

// Helper: Parse boolean from string
int parse_boolean(const char* str) {
  return (strcasecmp(str, "true") == 0 || strcmp(str, "1") == 0);
}

void parse_io_sizes(const char* io_size_str, int* io_size_array,
                    int* io_size_count) {
  char* token;
  // copy the string to avoid modifying the original string
  char* input_copy = strdup(io_size_str);
  if (!input_copy) {
    perror("strdup failed");
    return;
  }
  *io_size_count = 0;
  token = strtok(input_copy, ",");
  while (token != NULL && *io_size_count < MAX_IO_SIZE_FILTERS_NUM) {
    io_size_array[*io_size_count] = parse_size_with_unit(token);
    (*io_size_count)++;
    token = strtok(NULL, ",");
  }
  free(input_copy);
}

// Function to parse config file into ntprof_config structure
int parse_config(const char* filename, struct ntprof_config* config) {
  dictionary* ini = iniparser_load(filename);
  if (ini == NULL) {
    fprintf(stderr, "Failed to load configuration file.\n");
    return -1;
  }

  // Initialize default values
  memset(config, 0, sizeof(struct ntprof_config));
  strncpy(config->session_name, DEFAULT_SESSION,
          sizeof(config->session_name) - 1);
  config->io_type = BOTH;
  config->io_size_num = 0;
  config->min_io_size = -1;
  config->max_io_size = -1;
  config->network_bandwidth_limit = -1;
  config->nvme_drive_bandwidth_limit = -1;
  config->is_online = 1;
  strncpy(config->data_dir, DEFAULT_DATA_DIR, sizeof(config->data_dir) - 1);
  config->time_interval = 1;
  config->frequency = 1000;
  config->buffer_size = 1024;
  config->aggr = AVG;
  config->enable_latency_distribution = 1;
  config->enable_throughput = 1;
  config->enable_latency_breakdown = 1;
  config->enable_queue_length = 1;
  config->enable_group_by_size = 1;
  config->enable_group_by_type = 1;
  config->enable_group_by_session = 1;

  // Parse Workload section
  const char* session_name = iniparser_getstring(
      ini, "Workload:SESSION_NAME", DEFAULT_SESSION);
  const char* io_type = iniparser_getstring(ini, "Workload:IO_TYPE",
                                            DEFAULT_IO_TYPE);
  const char* io_size = iniparser_getstring(ini, "Workload:IO_SIZE", NULL);
  const char* min_io_size = iniparser_getstring(ini, "Workload:MIN_IO_SIZE",
                                                NULL);
  const char* max_io_size = iniparser_getstring(ini, "Workload:MAX_IO_SIZE",
                                                NULL);

  strncpy(config->session_name, session_name, sizeof(config->session_name) - 1);
  config->io_type = parse_io_type(io_type);
  if (io_size) {
    parse_io_sizes(io_size, config->io_size, &config->io_size_num);
  }
  if (min_io_size) {
    config->min_io_size = parse_size_with_unit(min_io_size);
  }
  if (max_io_size) {
    config->max_io_size = parse_size_with_unit(max_io_size);
  }

  // Parse Execution section
  config->network_bandwidth_limit = iniparser_getint(
      ini, "Execution:NETWORK_BANDWIDTH_LIMIT", -1);
  config->nvme_drive_bandwidth_limit = iniparser_getint(
      ini, "Execution:NVME_DRIVE_BANDWIDTH_LIMIT", -1);

  // Parse Profiling section
  config->is_online = parse_boolean(
      iniparser_getstring(ini, "Profiling:IS_ONLINE", "true"));
  const char* data_dir = iniparser_getstring(ini, "Profiling:DATA_DIR",
                                       DEFAULT_DATA_DIR);
  strncpy(config->data_dir, data_dir, sizeof(config->data_dir) - 1);
  config->time_interval = iniparser_getint(ini, "Profiling:TIME_INTERVAL", 1);
  config->frequency = iniparser_getint(ini, "Profiling:FREQUENCY", 1000);
  config->buffer_size = iniparser_getint(ini, "Profiling:BUFFER_SIZE", 1024);
  const char* aggregation = iniparser_getstring(ini, "Profiling:AGGREGATION",
                                                "avg");
  config->aggr = parse_aggr(aggregation);

  // Parse Report section
  config->enable_latency_distribution = parse_boolean(
      iniparser_getstring(ini, "Report:ENABLE_LATENCY_DISTRIBUTION", "true"));
  config->enable_throughput = parse_boolean(
      iniparser_getstring(ini, "Report:ENABLE_THROUGHPUT", "true"));
  config->enable_latency_breakdown = parse_boolean(
      iniparser_getstring(ini, "Report:ENABLE_LATENCY_BREAKDOWN", "true"));
  config->enable_queue_length = parse_boolean(
      iniparser_getstring(ini, "Report:ENABLE_QUEUE_LENGTH", "true"));
  config->enable_group_by_size = parse_boolean(
      iniparser_getstring(ini, "Report:ENABLE_GROUP_BY_SIZE", "true"));
  config->enable_group_by_type = parse_boolean(
      iniparser_getstring(ini, "Report:ENABLE_GROUP_BY_TYPE", "true"));
  config->enable_group_by_session = parse_boolean(
      iniparser_getstring(ini, "Report:ENABLE_GROUP_BY_SESSION", "true"));

  iniparser_freedict(ini);
  return 0;
}

void print_config(struct ntprof_config* config) {
  printf("Session Name: %s\n", config->session_name);
  printf("IO Type: %d\n", config->io_type);
  printf("IO Size: %d\n", config->io_size[0]);
  printf("Min IO Size: %d\n", config->min_io_size);
  printf("Max IO Size: %d\n", config->max_io_size);
  printf("Network Bandwidth Limit: %d\n", config->network_bandwidth_limit);
  printf("NVMe Drive Bandwidth Limit: %d\n",
         config->nvme_drive_bandwidth_limit);
  printf("Is Online: %d\n", config->is_online);
  printf("Time Interval: %d\n", config->time_interval);
  printf("Frequency: %d\n", config->frequency);
  printf("Buffer Size: %d\n", config->buffer_size);
  printf("Aggregation: %d\n", config->aggr);
  printf("Enable Latency Distribution: %d\n",
         config->enable_latency_distribution);
  printf("Enable Throughput: %d\n", config->enable_throughput);
  printf("Enable Latency Breakdown: %d\n", config->enable_latency_breakdown);
  printf("Enable Queue Length: %d\n", config->enable_queue_length);
  printf("Enable Group By Size: %d\n", config->enable_group_by_size);
  printf("Enable Group By Type: %d\n", config->enable_group_by_type);
  printf("Enable Group By Session: %d\n", config->enable_group_by_session);
}

void print_world() {
  printf("Hello, World!\n");
}