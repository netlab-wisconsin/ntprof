// SPDX-License-Identifier: GPL-2.0
/*
 * ntprof v2 command line.
 *
 * The ini task specification (-c) is read first; options given on the
 * command line override it.
 */
#include <getopt.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "commands.h"
#include "config.h"
#include "util.h"

static void usage(FILE* f) {
  fprintf(f,
          "usage: ntprof <command> [options] [files]\n"
          "\n"
          "commands:\n"
          "  record             profile the running kernel (root)\n"
          "  report FILE...     analyse record files: the host's, and the "
          "target's if any\n"
          "  compare BASE LOAD  LAD per stage; each run is host.ntp[,target.ntp]\n"
          "  probe              list the hooks this kernel offers\n"
          "  dump FILE...       print the records of record files as CSV\n"
          "\n"
          "options:\n"
          "  -c, --config FILE     task specification (v1 ini format)\n"
          "  -r, --role ROLE       host | target | both (default host)\n"
          "  -t, --transport T     tcp (default) | rdma\n"
          "  -d, --duration SEC    stop after SEC seconds (default: Ctrl-C)\n"
          "  -i, --interval SEC    online report interval (TIME_INTERVAL)\n"
          "  -s, --sample N        record one command in N (FREQUENCY)\n"
          "  -o, --output FILE     save the records to FILE\n"
          "  -j, --json FILE       write the final report as JSON\n"
          "      --session DEV     namespace to profile, nvmeXnY or all\n"
          "      --io-type T       read | write | both\n"
          "      --size LIST       exact I/O sizes, e.g. 4K,128K\n"
          "      --aggregation A   avg | min | max | dist\n"
          "      --offline         no interval reports (IS_ONLINE=false)\n"
          "      --btf DIR         BTF from tools/gen-btf.sh, for kernels "
          "without BTF\n"
          "      --ifname IF       time packet arrivals only on interface IF\n"
          "      --exact-queues    count queue occupancy per request (map\n"
          "                        operations on every I/O; default: mean\n"
          "                        occupancy per stage by Little's law)\n"
          "      --kprobes         use kprobes even where fentry/fexit work\n"
          "  -v, --verbose         more messages (twice: libbpf debug)\n"
          "  -h, --help\n");
}

enum {
  OPT_SESSION = 256,
  OPT_IO_TYPE,
  OPT_SIZE,
  OPT_AGGR,
  OPT_OFFLINE,
  OPT_BTF,
  OPT_IFNAME,
  OPT_EXACT_QUEUES,
  OPT_KPROBES,
};

static const struct option long_opts[] = {
    {"config", required_argument, NULL, 'c'},
    {"role", required_argument, NULL, 'r'},
    {"transport", required_argument, NULL, 't'},
    {"duration", required_argument, NULL, 'd'},
    {"interval", required_argument, NULL, 'i'},
    {"sample", required_argument, NULL, 's'},
    {"output", required_argument, NULL, 'o'},
    {"json", required_argument, NULL, 'j'},
    {"session", required_argument, NULL, OPT_SESSION},
    {"io-type", required_argument, NULL, OPT_IO_TYPE},
    {"size", required_argument, NULL, OPT_SIZE},
    {"aggregation", required_argument, NULL, OPT_AGGR},
    {"offline", no_argument, NULL, OPT_OFFLINE},
    {"btf", required_argument, NULL, OPT_BTF},
    {"ifname", required_argument, NULL, OPT_IFNAME},
    {"exact-queues", no_argument, NULL, OPT_EXACT_QUEUES},
    {"kprobes", no_argument, NULL, OPT_KPROBES},
    {"verbose", no_argument, NULL, 'v'},
    {"help", no_argument, NULL, 'h'},
    {NULL, 0, NULL, 0},
};

static const char short_opts[] = "c:r:t:d:i:s:o:j:vh";

static int parse_uint(const char* v, unsigned* out, unsigned min) {
  char* end;
  unsigned long x = strtoul(v, &end, 10);

  if (!*v || *end || x < min || x > 0xffffffffUL) return -1;
  *out = (unsigned)x;
  return 0;
}

static int bad(const char* opt, const char* v) {
  ntp_err("invalid value for %s: %s", opt, v);
  return -1;
}

/* Second pass: everything except --config and --verbose. */
static int apply_option(struct ntp_config* c, int opt, const char* v) {
  switch (opt) {
    case 'r':
      if (!strcmp(v, "host"))
        c->role = NTP_ROLE_HOST;
      else if (!strcmp(v, "target"))
        c->role = NTP_ROLE_TARGET;
      else if (!strcmp(v, "both"))
        c->role = NTP_ROLE_BOTH;
      else
        return bad("--role", v);
      return 0;
    case 't':
      if (!strcmp(v, "tcp"))
        c->transport = NTP_TR_TCP;
      else if (!strcmp(v, "rdma"))
        c->transport = NTP_TR_RDMA;
      else
        return bad("--transport", v);
      return 0;
    case 'd':
      return parse_uint(v, &c->duration_s, 1) ? bad("--duration", v) : 0;
    case 'i':
      return parse_uint(v, &c->interval_s, 1) ? bad("--interval", v) : 0;
    case 's':
      return parse_uint(v, &c->sample_every, 1) ? bad("--sample", v) : 0;
    case 'o':
      snprintf(c->output, sizeof(c->output), "%s", v);
      return 0;
    case 'j':
      snprintf(c->json, sizeof(c->json), "%s", v);
      return 0;
    case OPT_SESSION:
      snprintf(c->session, sizeof(c->session), "%s", v);
      return 0;
    case OPT_IO_TYPE:
      return config_set_io_type(c, v) ? bad("--io-type", v) : 0;
    case OPT_SIZE:
      return config_set_sizes(c, v) ? bad("--size", v) : 0;
    case OPT_AGGR:
      return config_set_aggr(c, v) ? bad("--aggregation", v) : 0;
    case OPT_OFFLINE:
      c->online = false;
      return 0;
    case OPT_BTF:
      snprintf(c->btf_dir, sizeof(c->btf_dir), "%s", v);
      return 0;
    case OPT_EXACT_QUEUES:
      c->exact_queues = true;
      return 0;
    case OPT_KPROBES:
      c->force_kprobes = true;
      return 0;
    case OPT_IFNAME:
      if (!if_nametoindex(v)) return bad("--ifname", v);
      snprintf(c->ifname, sizeof(c->ifname), "%s", v);
      return 0;
  }
  return -1;
}

int main(int argc, char** argv) {
  struct ntp_config c;
  const char* cmd;
  const char* ini = NULL;
  int opt;

  if (argc < 2 || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
    usage(argc < 2 ? stderr : stdout);
    return argc < 2 ? EXIT_FAILURE : EXIT_SUCCESS;
  }
  cmd = argv[1];
  argv++;
  argc--;

  /* first pass: the ini file and verbosity */
  while ((opt = getopt_long(argc, argv, short_opts, long_opts, NULL)) != -1) {
    if (opt == 'c')
      ini = optarg;
    else if (opt == 'v')
      ntp_verbose++;
    else if (opt == 'h') {
      usage(stdout);
      return EXIT_SUCCESS;
    } else if (opt == '?') {
      usage(stderr);
      return EXIT_FAILURE;
    }
  }
  config_defaults(&c);
  if (ini && config_load_ini(&c, ini)) return EXIT_FAILURE;

  optind = 0; /* glibc: rescan from the start */
  while ((opt = getopt_long(argc, argv, short_opts, long_opts, NULL)) != -1) {
    if (opt == 'c' || opt == 'v') continue;
    if (apply_option(&c, opt, optarg)) return EXIT_FAILURE;
  }
  if (ntp_verbose) {
    fprintf(stderr, "ntprof: ");
    config_print(&c, stderr);
  }

  if (!strcmp(cmd, "record")) {
    if (optind != argc) {
      ntp_err("record takes no file arguments");
      return EXIT_FAILURE;
    }
    return cmd_record(&c);
  }
  if (!strcmp(cmd, "report")) return cmd_report(&c, argv + optind, argc - optind);
  if (!strcmp(cmd, "compare")) {
    if (argc - optind != 2) {
      ntp_err("compare needs two runs: BASE LOADED");
      return EXIT_FAILURE;
    }
    return cmd_compare(&c, argv[optind], argv[optind + 1]);
  }
  if (!strcmp(cmd, "probe")) return cmd_probe(&c);
  if (!strcmp(cmd, "dump")) return cmd_dump(argv + optind, argc - optind);

  ntp_err("unknown command %s", cmd);
  usage(stderr);
  return EXIT_FAILURE;
}
