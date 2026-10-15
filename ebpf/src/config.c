// SPDX-License-Identifier: GPL-2.0
#include "config.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "util.h"

void config_defaults(struct ntp_config* c) {
  memset(c, 0, sizeof(*c));
  snprintf(c->session, sizeof(c->session), "all");
  c->io_type = NTP_IO_BOTH;
  c->online = true;
  snprintf(c->data_dir, sizeof(c->data_dir), "./data");
  c->interval_s = 1;
  c->sample_every = 1000;
  c->buffer_mb = 16;
  c->aggr = NTP_AGG_AVG;
  c->rep_distribution = true;
  c->rep_throughput = true;
  c->rep_breakdown = true;
  c->rep_queue_length = true;
  c->group_by_size = true;
  c->group_by_type = true;
  c->group_by_session = true;
  c->role = NTP_ROLE_HOST;
  c->transport = NTP_TR_TCP;
}

int config_set_io_type(struct ntp_config* c, const char* v) {
  if (!strcasecmp(v, "read"))
    c->io_type = NTP_IO_READ;
  else if (!strcasecmp(v, "write"))
    c->io_type = NTP_IO_WRITE;
  else if (!strcasecmp(v, "both"))
    c->io_type = NTP_IO_BOTH;
  else
    return -1;
  return 0;
}

/* Comma-separated exact sizes, e.g. "4K,128K". */
int config_set_sizes(struct ntp_config* c, const char* v) {
  char* copy = xstrdup(v);
  char* save = NULL;
  int n = 0, ret = 0;

  for (char* tok = strtok_r(copy, ",", &save); tok;
       tok = strtok_r(NULL, ",", &save)) {
    uint64_t size;

    if (n == NTP_MAX_SIZE_FILTERS || parse_size(strip(tok), &size) ||
        size == 0 || size > UINT32_MAX) {
      ret = -1;
      break;
    }
    c->sizes[n++] = (uint32_t)size;
  }
  if (!ret) c->n_sizes = n;
  free(copy);
  return ret;
}

int config_set_aggr(struct ntp_config* c, const char* v) {
  if (!strcasecmp(v, "avg"))
    c->aggr = NTP_AGG_AVG;
  else if (!strcasecmp(v, "min"))
    c->aggr = NTP_AGG_MIN;
  else if (!strcasecmp(v, "max"))
    c->aggr = NTP_AGG_MAX;
  else if (!strcasecmp(v, "dist"))
    c->aggr = NTP_AGG_DIST;
  else
    return -1;
  return 0;
}

static int set_uint(const char* v, unsigned* out, unsigned min) {
  char* end;
  unsigned long x;

  errno = 0;
  x = strtoul(v, &end, 10);
  if (errno || *end || x < min || x > UINT32_MAX) return -1;
  *out = (unsigned)x;
  return 0;
}

static int set_bool(const char* v, bool* out) { return parse_bool(v, out); }

static int set_size(const char* v, uint64_t* out) { return parse_size(v, out); }

/* Apply one "section:key = value" pair. Unknown keys only warn. */
static int apply(struct ntp_config* c, const char* section, const char* key,
                 const char* v) {
  unsigned u;
#define IS(s, k) (!strcasecmp(section, s) && !strcasecmp(key, k))
  if (IS("Workload", "SESSION_NAME")) {
    snprintf(c->session, sizeof(c->session), "%s", v);
    return 0;
  }
  if (IS("Workload", "IO_TYPE")) return config_set_io_type(c, v);
  if (IS("Workload", "IO_SIZE")) return config_set_sizes(c, v);
  if (IS("Workload", "MIN_IO_SIZE")) return set_size(v, &c->min_size);
  if (IS("Workload", "MAX_IO_SIZE")) return set_size(v, &c->max_size);
  /* v1 parsed these but never used them; accepted for compatibility */
  if (IS("Execution", "NETWORK_BANDWIDTH_LIMIT") ||
      IS("Execution", "NVME_DRIVE_BANDWIDTH_LIMIT"))
    return 0;
  if (IS("Profiling", "IS_ONLINE")) return set_bool(v, &c->online);
  if (IS("Profiling", "DATA_DIR")) {
    snprintf(c->data_dir, sizeof(c->data_dir), "%s", v);
    return 0;
  }
  if (IS("Profiling", "TIME_INTERVAL")) return set_uint(v, &c->interval_s, 1);
  if (IS("Profiling", "FREQUENCY")) {
    if (set_uint(v, &u, 1)) return -1;
    c->sample_every = u;
    return 0;
  }
  if (IS("Profiling", "BUFFER_SIZE")) return set_uint(v, &c->buffer_mb, 1);
  if (IS("Profiling", "AGGREGATION")) return config_set_aggr(c, v);
  if (IS("Report", "ENABLE_LATENCY_DISTRIBUTION"))
    return set_bool(v, &c->rep_distribution);
  if (IS("Report", "ENABLE_THROUGHPUT")) return set_bool(v, &c->rep_throughput);
  if (IS("Report", "ENABLE_LATENCY_BREAKDOWN"))
    return set_bool(v, &c->rep_breakdown);
  if (IS("Report", "ENABLE_QUEUE_LENGTH"))
    return set_bool(v, &c->rep_queue_length);
  /* v2 */
  if (IS("Report", "ENABLE_COMPONENT_DETAIL")) return set_bool(v, &c->rep_detail);
  if (IS("Report", "SHOW_ALL_GROUPS")) return set_bool(v, &c->rep_all_groups);
  if (IS("Report", "ENABLE_GROUP_BY_SIZE")) return set_bool(v, &c->group_by_size);
  if (IS("Report", "ENABLE_GROUP_BY_TYPE")) return set_bool(v, &c->group_by_type);
  if (IS("Report", "ENABLE_GROUP_BY_SESSION"))
    return set_bool(v, &c->group_by_session);
  /* v2: "estimate" (Little's law, default) or "exact" (per-request tracking) */
  if (IS("Report", "QUEUE_TRACKING")) {
    if (!strcasecmp(v, "exact"))
      c->exact_queues = true;
    else if (!strcasecmp(v, "estimate"))
      c->exact_queues = false;
    else
      return -1;
    return 0;
  }
  /* v2: the role runs in SPDK (binaries or libraries with the probes) */
  if (IS("Profiling", "SPDK_HOST")) {
    snprintf(c->spdk_host, sizeof(c->spdk_host), "%s", v);
    return 0;
  }
  if (IS("Profiling", "SPDK_TARGET")) {
    snprintf(c->spdk_target, sizeof(c->spdk_target), "%s", v);
    return 0;
  }
#undef IS
  ntp_warn("ignoring unknown key [%s] %s", section, key);
  return 0;
}

/* Cut a trailing ';' or '#' comment that is not inside quotes. */
static void cut_comment(char* s) {
  char quote = 0;

  for (; *s; s++) {
    if (quote) {
      if (*s == quote) quote = 0;
    } else if (*s == '"' || *s == '\'') {
      quote = *s;
    } else if (*s == ';' || *s == '#') {
      *s = '\0';
      return;
    }
  }
}

int config_load_ini(struct ntp_config* c, const char* path) {
  char line[1024], section[64] = "";
  FILE* f = fopen(path, "r");
  int lineno = 0, ret = 0;

  if (!f) {
    ntp_err("cannot open %s: %s", path, strerror(errno));
    return -1;
  }
  while (fgets(line, sizeof(line), f)) {
    char *s, *eq;

    lineno++;
    cut_comment(line);
    s = strip(line);
    if (!*s) continue;
    if (*s == '[') {
      char* end = strchr(s, ']');

      if (!end) {
        ntp_err("%s:%d: malformed section", path, lineno);
        ret = -1;
        break;
      }
      *end = '\0';
      snprintf(section, sizeof(section), "%s", strip(s + 1));
      continue;
    }
    eq = strchr(s, '=');
    if (!eq) {
      ntp_err("%s:%d: expected KEY=VALUE", path, lineno);
      ret = -1;
      break;
    }
    *eq = '\0';
    if (apply(c, section, strip(s), strip(eq + 1))) {
      ntp_err("%s:%d: bad value for %s", path, lineno, strip(s));
      ret = -1;
      break;
    }
  }
  fclose(f);
  return ret;
}

void config_print(const struct ntp_config* c, FILE* f) {
  static const char* io[] = {"read", "write", "both"};
  static const char* aggr[] = {"avg", "min", "max", "dist"};

  fprintf(f, "session=%s io_type=%s sizes=", c->session, io[c->io_type]);
  if (!c->n_sizes) fprintf(f, "any");
  for (int i = 0; i < c->n_sizes; i++)
    fprintf(f, "%s%u", i ? "," : "", c->sizes[i]);
  fprintf(f, " min=%llu max=%llu sample=1/%u interval=%us aggr=%s\n",
          (unsigned long long)c->min_size, (unsigned long long)c->max_size,
          c->sample_every, c->interval_s, aggr[c->aggr]);
  if (c->spdk_host[0]) fprintf(f, "  SPDK host: %s\n", c->spdk_host);
  if (c->spdk_target[0]) fprintf(f, "  SPDK target: %s\n", c->spdk_target);
}
