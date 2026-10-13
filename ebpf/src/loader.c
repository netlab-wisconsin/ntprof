// SPDX-License-Identifier: GPL-2.0
#include "loader.h"

#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <errno.h>
#include <net/if.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "nvme_tcp_host.skel.h"
#include "nvme_tcp_target.skel.h"
#include "util.h"

#define KERNEL_BTF "/sys/kernel/btf/vmlinux"
#define MAX_RINGBUF_MB 256

/* ---- the BPF objects ---- */

struct obj_inst;

struct obj_desc {
  const char* name;
  enum ntp_role role;
  enum ntp_transport transport;
  /* module whose DWARF provides BTF when the kernel has none */
  const char* btf_module;
  /* programs without which the records would be meaningless */
  const char* const* required;
  int (*open)(struct obj_inst* o, const struct bpf_object_open_opts* opts);
  void (*destroy)(struct obj_inst* o);
};

struct obj_inst {
  const struct obj_desc* desc;
  void* skel;
  struct bpf_object* obj;
  struct ntp_filter* filter; /* .rodata */
  volatile bool* armed;      /* .bss */
  struct bpf_map* events;
  struct bpf_map* counters;
  struct bpf_map* skb_arrival;
  struct bpf_link** links;
  size_t nlinks;
};

#define SKEL_ADAPTER(prefix)                                                 \
  static int open_##prefix(struct obj_inst* o,                              \
                           const struct bpf_object_open_opts* opts) {       \
    struct prefix##_bpf* s = prefix##_bpf__open_opts(opts);                  \
                                                                             \
    if (!s) return -errno;                                                   \
    o->skel = s;                                                             \
    o->obj = s->obj;                                                         \
    o->filter = &s->rodata->filter;                                              o->armed = &s->bss->ntp_armed;                                           \
    o->events = s->maps.events;                                              \
    o->counters = s->maps.counters;                                          \
    o->skb_arrival = s->maps.skb_arrival;                                    \
    return 0;                                                                \
  }                                                                          \
  static void destroy_##prefix(struct obj_inst* o) {                         \
    prefix##_bpf__destroy(o->skel);                                          \
  }

SKEL_ADAPTER(nvme_tcp_host)
SKEL_ADAPTER(nvme_tcp_target)

static const char* const tcp_host_required[] = {
    "ntp_h_queue_rq", "ntp_h_setup_cmd", "ntp_h_recv_skb",
    "ntp_h_recv_skb_ret", "ntp_h_complete", "ntp_h_blk_done", NULL};

static const char* const tcp_target_required[] = {
    "ntp_t_io_work", "ntp_t_io_work_ret", "ntp_t_req_init",
    "ntp_t_queue_resp", "ntp_t_sendmsg", NULL};

static const struct obj_desc objects[] = {
    {"nvme_tcp_host", NTP_ROLE_HOST, NTP_TR_TCP, "nvme_tcp", tcp_host_required,
     open_nvme_tcp_host, destroy_nvme_tcp_host},
    {"nvme_tcp_target", NTP_ROLE_TARGET, NTP_TR_TCP, "nvmet_tcp",
     tcp_target_required, open_nvme_tcp_target, destroy_nvme_tcp_target},
};

/* ---- /proc/kallsyms ---- */

struct ksyms {
  char** names;
  size_t n;
};

static int cmp_str(const void* a, const void* b) {
  return strcmp(*(char* const*)a, *(char* const*)b);
}

static int ksyms_load(struct ksyms* k) {
  FILE* f = fopen("/proc/kallsyms", "r");
  char line[512];
  size_t cap = 0;

  memset(k, 0, sizeof(*k));
  if (!f) {
    ntp_err("cannot read /proc/kallsyms: %s", strerror(errno));
    return -1;
  }
  while (fgets(line, sizeof(line), f)) {
    char name[256];

    if (sscanf(line, "%*s %*s %255s", name) != 1) continue;
    if (k->n == cap) {
      cap = cap ? cap * 2 : 1 << 17;
      k->names = xrealloc(k->names, cap * sizeof(*k->names));
    }
    k->names[k->n++] = xstrdup(name);
  }
  fclose(f);
  qsort(k->names, k->n, sizeof(*k->names), cmp_str);
  return 0;
}

static bool ksyms_has(const struct ksyms* k, const char* name) {
  return bsearch(&name, k->names, k->n, sizeof(*k->names), cmp_str) != NULL;
}

static void ksyms_free(struct ksyms* k) {
  for (size_t i = 0; i < k->n; i++) free(k->names[i]);
  free(k->names);
  memset(k, 0, sizeof(*k));
}

/*
 * Is the attach point of @prog present on this kernel? Fills @what with the
 * symbol that was looked up.
 */
static bool target_present(const struct ksyms* k, const struct bpf_program* prog,
                           char* what, size_t len) {
  const char* sec = bpf_program__section_name(prog);
  const char* slash = strchr(sec, '/');
  char sym[256];

  if (!slash) {
    snprintf(what, len, "%s", sec);
    return false;
  }
  if (!strncmp(sec, "raw_tp/", 7) || !strncmp(sec, "tp_btf/", 7)) {
    /* every tracepoint, built in or in a module, has this symbol */
    snprintf(what, len, "tracepoint %s", slash + 1);
    snprintf(sym, sizeof(sym), "__tracepoint_%s", slash + 1);
    return ksyms_has(k, sym);
  }
  snprintf(what, len, "%s", slash + 1);
  return ksyms_has(k, slash + 1);
}

static bool is_required(const struct obj_desc* d, const char* prog) {
  for (const char* const* r = d->required; *r; r++)
    if (!strcmp(*r, prog)) return true;
  return false;
}

/* ---- loader ---- */

struct ntp_loader {
  struct obj_inst inst[ARRAY_SIZE(objects)];
  size_t ninst;
  struct ring_buffer* rb;
  ntp_record_cb cb;
  void* cb_ctx;
};

static int libbpf_log(enum libbpf_print_level level, const char* fmt,
                      va_list ap) {
  if (level == LIBBPF_DEBUG && ntp_verbose < 2) return 0;
  if (level == LIBBPF_INFO && !ntp_verbose) return 0;
  return vfprintf(stderr, fmt, ap);
}

static int rb_cb(void* ctx, void* data, size_t size) {
  struct ntp_loader* l = ctx;

  return l->cb(l->cb_ctx, data, size);
}

static void fill_filter(struct ntp_filter* f, const struct ntp_config* c,
                        const struct ntp_filter* host_sel) {
  memset(f, 0, sizeof(*f));
  f->sample_every = c->sample_every ? c->sample_every : 1;
  f->io_type = (uint8_t)c->io_type;
  f->n_sizes = (uint8_t)c->n_sizes;
  for (int i = 0; i < c->n_sizes; i++) f->sizes[i] = c->sizes[i];
  f->min_bytes = (uint32_t)c->min_size;
  f->max_bytes = (uint32_t)c->max_size;
  if (host_sel) {
    f->ctrl_filter = host_sel->ctrl_filter;
    memcpy(f->ctrl_mask, host_sel->ctrl_mask, sizeof(f->ctrl_mask));
    f->nsid = host_sel->nsid;
  }
  if (c->ifname[0]) f->ifindex = if_nametoindex(c->ifname);
  f->track_queues = c->rep_queue_length;
}

static int read_sysfs_u32(const char* path, uint32_t* out) {
  FILE* f = fopen(path, "r");
  unsigned v;
  int ok;

  if (!f) return -1;
  ok = fscanf(f, "%u", &v) == 1;
  fclose(f);
  if (!ok) return -1;
  *out = v;
  return 0;
}

/*
 * SESSION_NAME names a namespace block device (nvme1n1). With native
 * multipath the I/O runs on hidden path devices nvme1cXn1, one per
 * controller; collect the instances of those controllers.
 */
static int resolve_session(const char* session, struct ntp_filter* sel) {
  char path[256];
  unsigned subsys, ns;
  int any = 0;

  memset(sel, 0, sizeof(*sel));
  if (!strcmp(session, "all")) return 0;
  if (sscanf(session, "nvme%un%u", &subsys, &ns) != 2) {
    ntp_err("SESSION_NAME %s is not an NVMe namespace (nvmeXnY) or \"all\"",
            session);
    return -1;
  }
  snprintf(path, sizeof(path), "/sys/block/%s/nsid", session);
  if (read_sysfs_u32(path, &sel->nsid)) sel->nsid = ns;

  /* path devices nvme<subsys>c<ctrl>n<ns> */
  for (unsigned ctrl = 0; ctrl < 256; ctrl++) {
    snprintf(path, sizeof(path), "/sys/block/nvme%uc%un%u", subsys, ctrl, ns);
    if (access(path, F_OK)) continue;
    sel->ctrl_mask[ctrl >> 6] |= 1ull << (ctrl & 63);
    any = 1;
  }
  if (!any) {
    /* no multipath: the device name carries the controller instance */
    if (subsys > 255) return -1;
    sel->ctrl_mask[subsys >> 6] |= 1ull << (subsys & 63);
  }
  sel->ctrl_filter = 1;
  return 0;
}

static int open_one(struct ntp_loader* l, const struct obj_desc* d,
                    const struct ntp_config* c, const struct ntp_filter* sel,
                    const struct ksyms* ks, struct obj_inst* shared) {
  LIBBPF_OPTS(bpf_object_open_opts, opts);
  struct obj_inst* o = &l->inst[l->ninst];
  char btf_path[512];
  struct bpf_program* prog;
  uint64_t rb_bytes;
  int err;

  if (access(KERNEL_BTF, R_OK)) {
    if (!c->btf_dir[0]) {
      ntp_err("this kernel has no BTF (%s). Generate it from the modules' "
              "DWARF with tools/gen-btf.sh and pass --btf DIR",
              KERNEL_BTF);
      return -1;
    }
    snprintf(btf_path, sizeof(btf_path), "%s/%s.btf", c->btf_dir,
             d->btf_module);
    opts.btf_custom_path = btf_path;
    ntp_debug("%s: CO-RE against %s", d->name, btf_path);
  }

  err = d->open(o, &opts);
  if (err) {
    ntp_err("%s: cannot open BPF object: %s", d->name, strerror(-err));
    return -1;
  }
  o->desc = d;
  l->ninst++;

  fill_filter(o->filter, c, (d->role & NTP_ROLE_HOST) ? sel : NULL);
  rb_bytes = (uint64_t)(c->buffer_mb > MAX_RINGBUF_MB ? MAX_RINGBUF_MB
                                                       : c->buffer_mb)
             << 20;
  /* ring buffer size: a power of two */
  while (rb_bytes & (rb_bytes - 1)) rb_bytes &= rb_bytes - 1;
  bpf_map__set_max_entries(o->events, rb_bytes);

  if (shared) {
    /*
     * One ring buffer, one skb arrival map and one netif_receive_skb hook per
     * process. A single ring buffer keeps records in emission order: a
     * command's target record always comes before its host record.
     */
    err = bpf_map__reuse_fd(o->skb_arrival, bpf_map__fd(shared->skb_arrival));
    if (!err) err = bpf_map__reuse_fd(o->events, bpf_map__fd(shared->events));
    if (err) {
      ntp_err("%s: cannot share maps: %s", d->name, strerror(-err));
      return -1;
    }
  }

  bpf_object__for_each_program(prog, o->obj) {
    const char* name = bpf_program__name(prog);
    char what[256];

    if (shared && !strcmp(name, "ntp_netif_rx")) {
      bpf_program__set_autoload(prog, false);
      continue;
    }
    if (target_present(ks, prog, what, sizeof(what))) continue;
    if (is_required(d, name)) {
      ntp_err("%s: %s not found in /proc/kallsyms; is the %s module loaded?",
              d->name, what, d->btf_module);
      return -1;
    }
    ntp_warn("%s: %s not available on this kernel; %s disabled", d->name, what,
             name);
    bpf_program__set_autoload(prog, false);
  }

  err = bpf_object__load(o->obj);
  if (err) {
    ntp_err("%s: cannot load BPF object: %s (rerun with -v for the verifier log)",
            d->name, strerror(-err));
    return -1;
  }
  return 0;
}

static int attach_all(struct obj_inst* o) {
  struct bpf_program* prog;
  size_t n = 0;

  bpf_object__for_each_program(prog, o->obj) n++;
  o->links = xcalloc(n, sizeof(*o->links));
  bpf_object__for_each_program(prog, o->obj) {
    const char* name = bpf_program__name(prog);
    struct bpf_link* link;

    if (!bpf_program__autoload(prog)) continue;
    link = bpf_program__attach(prog);
    if (!link) {
      int err = -errno;

      if (is_required(o->desc, name)) {
        ntp_err("%s: cannot attach %s: %s", o->desc->name, name, strerror(-err));
        return -1;
      }
      ntp_warn("%s: cannot attach %s (%s); continuing without it",
               o->desc->name, name, strerror(-err));
      continue;
    }
    o->links[o->nlinks++] = link;
  }
  return 0;
}

struct ntp_loader* ntp_loader_open(const struct ntp_config* c, ntp_record_cb cb,
                                   void* cb_ctx) {
  struct ntp_loader* l = xcalloc(1, sizeof(*l));
  struct obj_inst* shared = NULL;
  struct ntp_filter sel;
  struct ksyms ks;

  l->cb = cb;
  l->cb_ctx = cb_ctx;
  libbpf_set_print(libbpf_log);

  if (geteuid() != 0) ntp_warn("not running as root; loading will likely fail");
  if (resolve_session(c->session, &sel)) goto fail;
  if (ksyms_load(&ks)) goto fail;

  for (size_t i = 0; i < ARRAY_SIZE(objects); i++) {
    const struct obj_desc* d = &objects[i];

    if (!(c->role & d->role) || c->transport != d->transport) continue;
    if (open_one(l, d, c, &sel, &ks, shared)) {
      ksyms_free(&ks);
      goto fail;
    }
    if (!shared) shared = &l->inst[l->ninst - 1];
  }
  ksyms_free(&ks);
  if (!l->ninst) {
    ntp_err("no BPF object for this role and transport");
    goto fail;
  }

  for (size_t i = 0; i < l->ninst; i++)
    if (attach_all(&l->inst[i])) goto fail;

  l->rb = ring_buffer__new(bpf_map__fd(l->inst[0].events), rb_cb, l, NULL);
  if (!l->rb) {
    ntp_err("cannot create ring buffer: %s", strerror(errno));
    goto fail;
  }
  /* every hook is in place: start counting and recording */
  for (size_t i = 0; i < l->ninst; i++) *l->inst[i].armed = true;
  return l;

fail:
  ntp_loader_close(l);
  return NULL;
}

void ntp_loader_close(struct ntp_loader* l) {
  if (!l) return;
  ring_buffer__free(l->rb);
  for (size_t i = 0; i < l->ninst; i++) {
    struct obj_inst* o = &l->inst[i];

    for (size_t k = 0; k < o->nlinks; k++) bpf_link__destroy(o->links[k]);
    free(o->links);
    o->desc->destroy(o);
  }
  free(l);
}

void ntp_loader_disarm(struct ntp_loader* l) {
  for (size_t i = 0; i < l->ninst; i++) *l->inst[i].armed = false;
}

int ntp_loader_poll(struct ntp_loader* l, int timeout_ms) {
  int n = ring_buffer__poll(l->rb, timeout_ms);

  return n == -EINTR ? 0 : n;
}

uint64_t ntp_loader_counter(struct ntp_loader* l, enum ntp_counter c) {
  int ncpu = libbpf_num_possible_cpus();
  uint64_t total = 0;
  uint64_t* vals;
  uint32_t key = c;

  if (ncpu <= 0) return 0;
  vals = xcalloc(ncpu, sizeof(*vals));
  for (size_t i = 0; i < l->ninst; i++) {
    if (bpf_map_lookup_elem(bpf_map__fd(l->inst[i].counters), &key, vals))
      continue;
    for (int k = 0; k < ncpu; k++) total += vals[k];
  }
  free(vals);
  return total;
}

/* ---- probe ---- */

int ntp_probe(const struct ntp_config* c, FILE* f) {
  struct ksyms ks;
  bool kbtf = !access(KERNEL_BTF, R_OK);

  libbpf_set_print(libbpf_log);
  if (ksyms_load(&ks)) return -1;
  fprintf(f, "kernel BTF: %s\n",
          kbtf ? "yes" : "no (CO-RE needs --btf DIR, see tools/gen-btf.sh)");

  for (size_t i = 0; i < ARRAY_SIZE(objects); i++) {
    const struct obj_desc* d = &objects[i];
    struct obj_inst o = {.desc = d};
    LIBBPF_OPTS(bpf_object_open_opts, opts);
    struct bpf_program* prog;
    char btf_mod[256];

    if (!(c->role & d->role) || c->transport != d->transport) continue;
    snprintf(btf_mod, sizeof(btf_mod), "/sys/kernel/btf/%s", d->btf_module);
    fprintf(f, "\n%s (module BTF %s: %s)\n", d->name, d->btf_module,
            access(btf_mod, R_OK) ? "absent" : "present");
    if (d->open(&o, &opts)) {
      fprintf(f, "  cannot open object\n");
      continue;
    }
    bpf_object__for_each_program(prog, o.obj) {
      char what[256];
      bool ok = target_present(&ks, prog, what, sizeof(what));

      fprintf(f, "  %-4s %-22s %-40s%s\n", ok ? "ok" : "--",
              bpf_program__name(prog), what,
              is_required(d, bpf_program__name(prog)) ? " (required)" : "");
    }
    d->destroy(&o);
  }
  ksyms_free(&ks);
  return 0;
}
