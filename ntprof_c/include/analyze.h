#ifndef ANALYZE_H
#define ANALYZE_H

#include "config.h"

struct profile_result {
    unsigned long long total_io;
    // unsigned long long start_time;
    // unsigned long long end_time;
    // unsigned long long sampled_io;
    // unsigned long long total_end2end_lat;

    // block layer
    // unsigned long long
};

struct analyze_arg {
    struct ntprof_config config;
    struct profile_result result;
};

#endif //ANALYZE_H
