/* SPDX-License-Identifier: GPL-2.0 */
/* Small helpers shared by the ntprof user-space tool. */
#ifndef NTPROF_UTIL_H
#define NTPROF_UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

extern int ntp_verbose;

void ntp_err(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void ntp_warn(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void ntp_info(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void ntp_debug(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

/* Allocation that cannot fail (exits with a message on ENOMEM). */
void* xmalloc(size_t size);
void* xcalloc(size_t n, size_t size);
void* xrealloc(void* p, size_t size);
char* xstrdup(const char* s);

/* CLOCK_MONOTONIC and CLOCK_REALTIME in ns. */
uint64_t now_mono_ns(void);
uint64_t now_real_ns(void);

/*
 * Parse a byte count such as "4096", "4K", "128k", "1M" (powers of 1024).
 * Returns 0 on success, -1 on a malformed value.
 */
int parse_size(const char* s, uint64_t* out);

/* Parse "true"/"false"/"yes"/"no"/"1"/"0" (case-insensitive). */
int parse_bool(const char* s, bool* out);

/* Strip leading and trailing white space and quotes in place. */
char* strip(char* s);

/* Growable vector of int64 samples. */
struct i64vec {
  int64_t* v;
  size_t n;
  size_t cap;
};

void i64vec_push(struct i64vec* vec, int64_t x);
void i64vec_free(struct i64vec* vec);

#endif /* NTPROF_UTIL_H */
