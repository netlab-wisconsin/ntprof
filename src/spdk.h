/* SPDX-License-Identifier: GPL-2.0 */
/*
 * SPDK applications built with ntprof's USDT probes (spdk/).
 *
 * The probes only fire for commands SPDK samples, and SPDK samples nothing
 * until ntprof writes its sampling period into the process: the variable
 * g_ntprof_sample_every (lib/sock/sock.c of the patched SPDK). ntprof finds
 * the processes that map the given binaries, writes the period through
 * /proc/PID/mem once its programs are attached, and writes 0 back when it
 * stops.
 */
#ifndef NTPROF_SPDK_H
#define NTPROF_SPDK_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

struct ntp_spdk_proc {
  pid_t pid;
  uint64_t gate; /* address of g_ntprof_sample_every in the process */
};

/*
 * Processes (other than this one) that map the first of the comma-separated
 * @paths, with the address of their sampling variable. Returns how many were
 * found (at most @max), or -1 after printing why.
 */
int ntp_spdk_find(const char* paths, struct ntp_spdk_proc* procs, int max);

/* Write @every (0 = off) into the process's sampling variable. */
int ntp_spdk_set_gate(const struct ntp_spdk_proc* p, uint32_t every);

/*
 * Print the ntprof USDT probes in the ELF file @path (one line per probe
 * name, with its number of sites). Returns the number of sites, or -1.
 */
int ntp_spdk_list_probes(const char* path, FILE* f);

#endif /* NTPROF_SPDK_H */
