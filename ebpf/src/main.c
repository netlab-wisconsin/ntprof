// SPDX-License-Identifier: GPL-2.0
/*
 * ntprof v2 command line.
 *
 * The ini task specification (-c) is read first; options given on the
 * command line override it.
 */
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "commands.h"
#include "config.h"
#include "util.h"
#include "version.h"

static void usage(FILE* f) {
  fprintf(f,
          "ntprof %s: where the time of each NVMe-over-Fabrics I/O goes, stage by\n"
          "stage (NVMe/TCP, NVMe/RDMA, SPDK), with eBPF on an unmodified kernel\n"
          "\n"
          "usage: ntprof <command> [options] [files]\n"
          "\n"
          "commands:\n"
          "  record             profile the running kernel or SPDK (needs root)\n"
          "  report FILE...     report recordings: the host's, and the target's\n"
          "  compare BASE LOAD  latency amplification (LAD) of each stage between two\n"
          "                     runs; a run is host.ntp or host.ntp,target.ntp\n"
          "  probe              check which hooks this kernel offers\n"
          "  dump FILE...       print the records of recordings as CSV\n"
          "  version            print the version\n"
          "\n"
          "examples:\n"
          "  sudo ntprof record -r both -d 10       host and target on one machine\n"
          "  sudo ntprof record -r target -o t.ntp  on the target (start it first) ...\n"
          "  sudo ntprof record -o h.ntp -d 30      ... and on the host\n"
          "  ntprof report h.ntp t.ntp              the two joined\n"
          "\n"
          "record options:\n"
          "  -r, --role ROLE       host (the initiator; default) | target | both\n"
          "  -t, --transport T     tcp (default) | rdma\n"
          "  -d, --duration SEC    stop after SEC seconds (default: Ctrl-C)\n"
          "  -i, --interval SEC    one line per group every SEC seconds (default 1)\n"
          "      --offline         no interval lines, only the final report\n"
          "  -s, --sample N        record one command in N (default 1000); the\n"
          "                        host and the target must use the same N\n"
          "  -o, --output FILE     save the records to FILE\n"
          "      --session DEV     namespace to profile, nvmeXnY (default: all)\n"
          "      --io-type T       read | write | both (default)\n"
          "      --size LIST       only these I/O sizes, e.g. 4K,128K\n"
          "      --spdk-host PATH  the host is SPDK: the binary with the ntprof\n"
          "                        probes (shared build: its libraries, comma-\n"
          "                        separated), e.g. build/bin/spdk_nvme_perf\n"
          "      --spdk-target PATH  the target is SPDK, e.g. build/bin/nvmf_tgt\n"
          "      --exact-queues    count queue lengths per command (costs map\n"
          "                        operations on every I/O; default: the mean number\n"
          "                        in each stage, by Little's law)\n"
          "      --btf DIR         BTF from tools/gen-btf.sh, for kernels without BTF\n"
          "      --kprobes         use kprobes even where fentry/fexit work\n"
          "      --fentry          use fentry/fexit even when a busy CPU would make\n"
          "                        attaching them wait (default: fall back to kprobes)\n"
          "\n"
          "report options (record, report, compare):\n"
          "      --detail          every component, not only the stages\n"
          "      --all-groups      also groups with under 1 %% of the records\n"
          "      --aggregation A   avg (mean, p50, p99, p99.9) | min | max | dist\n"
          "  -j, --json FILE       write the final report as JSON\n"
          "\n"
          "  -c, --config FILE     task specification (v1's ini format); options\n"
          "                        given on the command line override it\n"
          "  -v, --verbose         more messages (twice: libbpf debug)\n"
          "  -h, --help            this text; -V, --version: the version\n",
          NTP_VERSION);
}

enum {
  OPT_SESSION = 256,
  OPT_IO_TYPE,
  OPT_SIZE,
  OPT_AGGR,
  OPT_OFFLINE,
  OPT_BTF,
  OPT_EXACT_QUEUES,
  OPT_KPROBES,
  OPT_FENTRY,
  OPT_SPDK_HOST,
  OPT_SPDK_TARGET,
  OPT_DETAIL,
  OPT_ALL_GROUPS,
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
    {"exact-queues", no_argument, NULL, OPT_EXACT_QUEUES},
    {"kprobes", no_argument, NULL, OPT_KPROBES},
    {"fentry", no_argument, NULL, OPT_FENTRY},
    {"spdk-host", required_argument, NULL, OPT_SPDK_HOST},
    {"spdk-target", required_argument, NULL, OPT_SPDK_TARGET},
    {"detail", no_argument, NULL, OPT_DETAIL},
    {"all-groups", no_argument, NULL, OPT_ALL_GROUPS},
    {"verbose", no_argument, NULL, 'v'},
    {"version", no_argument, NULL, 'V'},
    {"help", no_argument, NULL, 'h'},
    {NULL, 0, NULL, 0},
};

static const char short_opts[] = "c:r:t:d:i:s:o:j:vVh";

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
    case OPT_FENTRY:
      c->force_fentry = true;
      return 0;
    case OPT_SPDK_HOST:
      snprintf(c->spdk_host, sizeof(c->spdk_host), "%s", v);
      return 0;
    case OPT_SPDK_TARGET:
      snprintf(c->spdk_target, sizeof(c->spdk_target), "%s", v);
      return 0;
    case OPT_DETAIL:
      c->rep_detail = true;
      return 0;
    case OPT_ALL_GROUPS:
      c->rep_all_groups = true;
      return 0;
  }
  return -1;
}

static int version(void) {
  printf("ntprof %s (%s)\n", NTP_VERSION, NTP_REVISION);
  return EXIT_SUCCESS;
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
  if (!strcmp(argv[1], "version") || !strcmp(argv[1], "-V") ||
      !strcmp(argv[1], "--version"))
    return version();
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
    } else if (opt == 'V') {
      return version();
    } else if (opt == '?') {
      usage(stderr);
      return EXIT_FAILURE;
    }
  }
  config_defaults(&c);
  if (ini && config_load_ini(&c, ini)) return EXIT_FAILURE;

  optind = 0; /* glibc: rescan from the start */
  while ((opt = getopt_long(argc, argv, short_opts, long_opts, NULL)) != -1) {
    if (opt == 'c' || opt == 'v' || opt == 'V') continue;
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

  ntp_err("unknown command %s (ntprof --help lists them)", cmd);
  return EXIT_FAILURE;
}
