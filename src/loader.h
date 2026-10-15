/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Open, configure, load and attach the BPF objects for one role and
 * transport. Each program is attached on its own: a hook whose function was
 * inlined on this kernel only costs the events it would have produced, and
 * `ntprof probe` lists what is available.
 */
#ifndef NTPROF_LOADER_H
#define NTPROF_LOADER_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "config.h"
#include "ntprof.h"

struct ring_buffer;
struct ntp_loader;

/* Called for every record the BPF programs emit. */
typedef int (*ntp_record_cb)(void* ctx, const void* data, size_t size);

/* Returns NULL after printing why. */
struct ntp_loader* ntp_loader_open(const struct ntp_config* c, ntp_record_cb cb,
                                   void* cb_ctx);
void ntp_loader_close(struct ntp_loader* l);

/*
 * Stop starting new records (commands in flight still complete theirs) and
 * stop counting queue occupancy.
 */
void ntp_loader_disarm(struct ntp_loader* l);

/* Poll the ring buffers; returns records consumed or a negative error. */
int ntp_loader_poll(struct ntp_loader* l, int timeout_ms);

/* Sum of a per-CPU counter over all loaded objects. */
uint64_t ntp_loader_counter(struct ntp_loader* l, enum ntp_counter c);

/* Print which hooks this kernel offers for @c's role and transport. */
int ntp_probe(const struct ntp_config* c, FILE* f);

#endif /* NTPROF_LOADER_H */
