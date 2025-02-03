#ifndef ANALYZER_H
#define ANALYZER_H

#include "../include/config.h"
#include "host.h"

void analyze_statistics(struct per_core_statistics *stat, struct ntprof_config *config);

#endif // ANALYZER_H
