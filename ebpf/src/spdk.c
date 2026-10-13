// SPDX-License-Identifier: GPL-2.0
#include "spdk.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <gelf.h>
#include <libelf.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "util.h"

#define GATE_SYMBOL "g_ntprof_sample_every"
/* the library that defines GATE_SYMBOL in a shared-library SPDK build */
#define GATE_LIBRARY "libspdk_sock"
#define PROVIDER "ntprof"
#define NT_STAPSDT 3

/* ---- ELF ---- */

struct elf_file {
  int fd;
  Elf* e;
};

static int elf_open(const char* path, struct elf_file* ef) {
  if (elf_version(EV_CURRENT) == EV_NONE) return -1;
  ef->fd = open(path, O_RDONLY | O_CLOEXEC);
  if (ef->fd < 0) return -1;
  ef->e = elf_begin(ef->fd, ELF_C_READ, NULL);
  if (!ef->e || elf_kind(ef->e) != ELF_K_ELF) {
    if (ef->e) elf_end(ef->e);
    close(ef->fd);
    return -1;
  }
  return 0;
}

static void elf_close(struct elf_file* ef) {
  elf_end(ef->e);
  close(ef->fd);
}

/* Where a symbol of a file lands in memory. */
struct sym_loc {
  uint64_t value;      /* st_value */
  bool fixed;          /* ET_EXEC: the value is the address */
  uint64_t load_vaddr; /* first PT_LOAD segment: its p_vaddr */
  uint64_t load_off;   /* and its p_offset */
};

static int elf_find_symbol(const char* path, const char* name,
                           struct sym_loc* out) {
  struct elf_file ef;
  Elf_Scn* scn = NULL;
  GElf_Ehdr eh;
  size_t nph;
  bool load = false;
  int ret = -1;

  if (elf_open(path, &ef)) return -1;
  if (!gelf_getehdr(ef.e, &eh) || elf_getphdrnum(ef.e, &nph)) goto out;
  out->fixed = eh.e_type == ET_EXEC;
  for (size_t i = 0; i < nph && !load; i++) {
    GElf_Phdr ph;

    if (gelf_getphdr(ef.e, (int)i, &ph) && ph.p_type == PT_LOAD) {
      out->load_vaddr = ph.p_vaddr;
      out->load_off = ph.p_offset;
      load = true;
    }
  }
  if (!load) goto out;

  while (ret && (scn = elf_nextscn(ef.e, scn))) {
    GElf_Shdr sh;
    Elf_Data* d;

    if (!gelf_getshdr(scn, &sh) || !sh.sh_entsize ||
        (sh.sh_type != SHT_SYMTAB && sh.sh_type != SHT_DYNSYM))
      continue;
    d = elf_getdata(scn, NULL);
    if (!d) continue;
    for (size_t k = 0; k < sh.sh_size / sh.sh_entsize; k++) {
      GElf_Sym sym;
      const char* nm;

      if (!gelf_getsym(d, (int)k, &sym) || sym.st_shndx == SHN_UNDEF) continue;
      nm = elf_strptr(ef.e, sh.sh_link, sym.st_name);
      if (nm && !strcmp(nm, name)) {
        out->value = sym.st_value;
        ret = 0;
        break;
      }
    }
  }
out:
  elf_close(&ef);
  return ret;
}

int ntp_spdk_list_probes(const char* path, FILE* f) {
  struct {
    char name[64];
    int sites;
  } probes[64];
  struct elf_file ef;
  Elf_Scn* scn = NULL;
  size_t addr_size;
  int n = 0, total = 0;

  if (elf_open(path, &ef)) {
    ntp_err("%s: not a readable ELF file", path);
    return -1;
  }
  addr_size = gelf_getclass(ef.e) == ELFCLASS32 ? 4 : 8;
  while ((scn = elf_nextscn(ef.e, scn))) {
    GElf_Shdr sh;
    GElf_Nhdr nh;
    Elf_Data* d;
    size_t off = 0, next, name_off, desc_off;

    if (!gelf_getshdr(scn, &sh) || sh.sh_type != SHT_NOTE) continue;
    d = elf_getdata(scn, NULL);
    if (!d) continue;
    while ((next = gelf_getnote(d, off, &nh, &name_off, &desc_off)) > 0) {
      const char* desc = (const char*)d->d_buf + desc_off;
      const char* end = desc + nh.n_descsz;
      const char *prov, *name;
      int i;

      off = next;
      /* stapsdt: pc, base, semaphore; then provider, name, arguments */
      if (nh.n_type != NT_STAPSDT || nh.n_namesz != sizeof("stapsdt") ||
          memcmp((const char*)d->d_buf + name_off, "stapsdt", nh.n_namesz) ||
          nh.n_descsz <= 3 * addr_size)
        continue;
      prov = desc + 3 * addr_size;
      name = memchr(prov, '\0', end - prov);
      if (!name || ++name >= end || !memchr(name, '\0', end - name) ||
          strcmp(prov, PROVIDER))
        continue;
      total++;
      for (i = 0; i < n && strcmp(probes[i].name, name); i++) {
      }
      if (i == n) {
        if (n == (int)ARRAY_SIZE(probes)) continue;
        snprintf(probes[n].name, sizeof(probes[n].name), "%s", name);
        probes[n++].sites = 0;
      }
      probes[i].sites++;
    }
  }
  elf_close(&ef);
  for (int i = 0; i < n; i++)
    fprintf(f, "  ok   %-22s %d site%s\n", probes[i].name, probes[i].sites,
            probes[i].sites == 1 ? "" : "s");
  return total;
}

/* ---- processes ---- */

struct paths {
  char** v;
  int n;
};

static void paths_free(struct paths* p) {
  for (int i = 0; i < p->n; i++) free(p->v[i]);
  free(p->v);
}

/* The comma-separated list, as canonical paths (as /proc/PID/maps has them). */
static int paths_parse(const char* list, struct paths* p) {
  char* copy = xstrdup(list);
  char* save = NULL;
  int ret = 0;

  memset(p, 0, sizeof(*p));
  for (char* tok = strtok_r(copy, ",", &save); tok;
       tok = strtok_r(NULL, ",", &save)) {
    char real[PATH_MAX];

    tok = strip(tok);
    if (!*tok) continue;
    if (!realpath(tok, real)) {
      ntp_err("%s: %s", tok, strerror(errno));
      ret = -1;
      break;
    }
    p->v = xrealloc(p->v, (p->n + 1) * sizeof(*p->v));
    p->v[p->n++] = xstrdup(real);
  }
  free(copy);
  if (!ret && !p->n) {
    ntp_err("no SPDK binary given");
    ret = -1;
  }
  if (ret) paths_free(p);
  return ret;
}

struct mapping {
  uint64_t start;
  uint64_t offset;
  char* path;
};

struct maps {
  struct mapping* v;
  size_t n;
};

static void maps_free(struct maps* m) {
  for (size_t i = 0; i < m->n; i++) free(m->v[i].path);
  free(m->v);
}

/* The file-backed mappings of a process. */
static int maps_read(pid_t pid, struct maps* m) {
  char path[64], line[PATH_MAX + 128];
  size_t cap = 0;
  FILE* f;

  memset(m, 0, sizeof(*m));
  snprintf(path, sizeof(path), "/proc/%d/maps", (int)pid);
  f = fopen(path, "r");
  if (!f) return -1;
  while (fgets(line, sizeof(line), f)) {
    unsigned long long start, offset;
    char file[PATH_MAX];

    file[0] = '\0';
    if (sscanf(line, "%llx-%*x %*s %llx %*s %*s %4095[^\n]", &start, &offset,
               file) < 3 ||
        file[0] != '/')
      continue;
    if (m->n == cap) {
      cap = cap ? cap * 2 : 64;
      m->v = xrealloc(m->v, cap * sizeof(*m->v));
    }
    m->v[m->n].start = start;
    m->v[m->n].offset = offset;
    m->v[m->n++].path = xstrdup(file);
  }
  fclose(f);
  return 0;
}

static bool maps_has(const struct maps* m, const char* path) {
  for (size_t i = 0; i < m->n; i++)
    if (!strcmp(m->v[i].path, path)) return true;
  return false;
}

/* Address of GATE_SYMBOL of file @path in the process whose maps are @m. */
static int gate_in(const struct maps* m, const char* path, uint64_t* addr) {
  uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
  struct sym_loc s;

  if (elf_find_symbol(path, GATE_SYMBOL, &s)) return -1;
  if (s.fixed) {
    *addr = s.value;
    return 0;
  }
  /* position-independent: find where the first segment was mapped */
  for (size_t i = 0; i < m->n; i++) {
    if (strcmp(m->v[i].path, path) || m->v[i].offset != (s.load_off & ~(page - 1)))
      continue;
    *addr = m->v[i].start - (s.load_vaddr & ~(page - 1)) + s.value;
    return 0;
  }
  return -1;
}

static int gate_address(const struct maps* m, const struct paths* p,
                        uint64_t* addr) {
  /* the given files first (a static build has it there), then libspdk_sock */
  for (int i = 0; i < p->n; i++)
    if (maps_has(m, p->v[i]) && !gate_in(m, p->v[i], addr)) return 0;
  for (size_t i = 0; i < m->n; i++) {
    const char* base = strrchr(m->v[i].path, '/');

    if (base && !strncmp(base + 1, GATE_LIBRARY, strlen(GATE_LIBRARY)) &&
        !gate_in(m, m->v[i].path, addr))
      return 0;
  }
  return -1;
}

int ntp_spdk_find(const char* list, struct ntp_spdk_proc* procs, int max) {
  pid_t self = getpid();
  struct dirent* de;
  struct paths p;
  bool full = false;
  int n = 0;
  DIR* d;

  if (paths_parse(list, &p)) return -1;
  d = opendir("/proc");
  if (!d) {
    ntp_err("cannot read /proc: %s", strerror(errno));
    paths_free(&p);
    return -1;
  }
  while (n >= 0 && !full && (de = readdir(d))) {
    char* end;
    long pid = strtol(de->d_name, &end, 10);
    bool match = false;
    struct maps m;

    if (*end || pid <= 0 || pid == self || maps_read((pid_t)pid, &m)) continue;
    for (int i = 0; i < p.n && !match; i++) match = maps_has(&m, p.v[i]);
    if (match && n == max) {
      ntp_warn("more than %d processes run %s; profiling the first %d", max,
               list, max);
      full = true;
    } else if (match) {
      procs[n].pid = (pid_t)pid;
      if (gate_address(&m, &p, &procs[n].gate)) {
        ntp_err("process %ld runs %s but has no %s: is SPDK built with "
                "ebpf/spdk's patch?",
                pid, list, GATE_SYMBOL);
        n = -1;
      } else {
        n++;
      }
    }
    maps_free(&m);
  }
  closedir(d);
  paths_free(&p);
  return n;
}

int ntp_spdk_set_gate(const struct ntp_spdk_proc* p, uint32_t every) {
  char path[64];
  uint32_t old = 0;
  int fd, err = 0;

  snprintf(path, sizeof(path), "/proc/%d/mem", (int)p->pid);
  fd = open(path, O_RDWR | O_CLOEXEC);
  if (fd < 0) return -errno;
  if (every && pread(fd, &old, sizeof(old), (off_t)p->gate) == sizeof(old) && old)
    ntp_warn("process %d already samples 1 of %u commands (another ntprof, or "
             "one that was killed); setting 1 of %u",
             (int)p->pid, old, every);
  if (pwrite(fd, &every, sizeof(every), (off_t)p->gate) != sizeof(every))
    err = errno ? -errno : -EIO;
  close(fd);
  return err;
}
