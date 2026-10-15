/* SPDX-License-Identifier: GPL-2.0 */
/* The ntprof subcommands. Each returns a process exit status. */
#ifndef NTPROF_COMMANDS_H
#define NTPROF_COMMANDS_H

#include "config.h"

/* Attach, collect, report online and/or write a record file. */
int cmd_record(const struct ntp_config* c);

/* Analyse record files: a host file and optionally the target's file. */
int cmd_report(const struct ntp_config* c, char* const* files, int nfiles);

/*
 * LAD between two runs. Each run is a comma-separated list of record files
 * (host[,target]).
 */
int cmd_compare(const struct ntp_config* c, const char* base,
                const char* loaded);

/* Print the records of record files as CSV. */
int cmd_dump(char* const* files, int nfiles);

/* List the hooks this kernel offers. */
int cmd_probe(const struct ntp_config* c);

#endif /* NTPROF_COMMANDS_H */
