// SPDX-License-Identifier: GPL-2.0
#include "util.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

int ntp_verbose;

static void vlog(const char* prefix, const char* fmt, va_list ap) {
  fprintf(stderr, "ntprof: %s", prefix);
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
}

void ntp_err(const char* fmt, ...) {
  va_list ap;

  va_start(ap, fmt);
  vlog("error: ", fmt, ap);
  va_end(ap);
}

void ntp_warn(const char* fmt, ...) {
  va_list ap;

  va_start(ap, fmt);
  vlog("warning: ", fmt, ap);
  va_end(ap);
}

void ntp_info(const char* fmt, ...) {
  va_list ap;

  va_start(ap, fmt);
  vlog("", fmt, ap);
  va_end(ap);
}

void ntp_debug(const char* fmt, ...) {
  va_list ap;

  if (!ntp_verbose) return;
  va_start(ap, fmt);
  vlog("debug: ", fmt, ap);
  va_end(ap);
}

static void* check_alloc(void* p) {
  if (!p) {
    ntp_err("out of memory");
    exit(EXIT_FAILURE);
  }
  return p;
}

void* xmalloc(size_t size) { return check_alloc(malloc(size ? size : 1)); }

void* xcalloc(size_t n, size_t size) {
  return check_alloc(calloc(n ? n : 1, size ? size : 1));
}

void* xrealloc(void* p, size_t size) {
  return check_alloc(realloc(p, size ? size : 1));
}

char* xstrdup(const char* s) { return check_alloc(strdup(s)); }

static uint64_t clock_ns(clockid_t id) {
  struct timespec ts;

  clock_gettime(id, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

uint64_t now_mono_ns(void) { return clock_ns(CLOCK_MONOTONIC); }

uint64_t now_real_ns(void) { return clock_ns(CLOCK_REALTIME); }

int parse_size(const char* s, uint64_t* out) {
  char* end;
  unsigned long long v;

  if (!s || !*s || !isdigit((unsigned char)*s)) return -1;
  errno = 0;
  v = strtoull(s, &end, 10);
  if (errno) return -1;
  switch (tolower((unsigned char)*end)) {
    case '\0':
      break;
    case 'k':
      v <<= 10;
      end++;
      break;
    case 'm':
      v <<= 20;
      end++;
      break;
    case 'g':
      v <<= 30;
      end++;
      break;
    default:
      return -1;
  }
  if (tolower((unsigned char)*end) == 'b') end++;
  if (*end) return -1;
  *out = v;
  return 0;
}

int parse_bool(const char* s, bool* out) {
  if (!strcasecmp(s, "true") || !strcasecmp(s, "yes") || !strcmp(s, "1")) {
    *out = true;
    return 0;
  }
  if (!strcasecmp(s, "false") || !strcasecmp(s, "no") || !strcmp(s, "0")) {
    *out = false;
    return 0;
  }
  return -1;
}

char* strip(char* s) {
  char* e;

  while (isspace((unsigned char)*s)) s++;
  e = s + strlen(s);
  while (e > s && isspace((unsigned char)e[-1])) e--;
  *e = '\0';
  if (e - s >= 2 && (*s == '"' || *s == '\'') && e[-1] == *s) {
    e[-1] = '\0';
    s++;
  }
  return s;
}

void i64vec_push(struct i64vec* vec, int64_t x) {
  if (vec->n == vec->cap) {
    vec->cap = vec->cap ? vec->cap * 2 : 1024;
    vec->v = xrealloc(vec->v, vec->cap * sizeof(*vec->v));
  }
  vec->v[vec->n++] = x;
}

void i64vec_free(struct i64vec* vec) {
  free(vec->v);
  vec->v = NULL;
  vec->n = vec->cap = 0;
}
