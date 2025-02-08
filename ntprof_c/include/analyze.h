#ifndef ANALYZE_H
#define ANALYZE_H

#include "config.h"

struct profile_result {
    unsigned long long total_io;
};

struct analyze_arg {
    struct ntprof_config config;
    struct profile_result result;
};

#endif //ANALYZE_H
