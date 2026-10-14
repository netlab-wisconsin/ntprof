// SPDX-License-Identifier: GPL-2.0
#include "trace_file.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>

#include "util.h"

void ntp_header_init(struct ntp_file_header* hdr, uint32_t role,
                     uint32_t transport, uint32_t sample_every) {
  struct utsname u;

  memset(hdr, 0, sizeof(*hdr));
  memcpy(hdr->magic, NTP_FILE_MAGIC, sizeof(NTP_FILE_MAGIC));
  hdr->version = NTP_FILE_VERSION;
  hdr->header_size = sizeof(*hdr);
  hdr->host_rec_size = sizeof(struct ntp_host_rec);
  hdr->tgt_rec_size = sizeof(struct ntp_tgt_rec);
  hdr->role = role;
  hdr->transport = transport;
  hdr->sample_every = sample_every;
  hdr->start_mono_ns = now_mono_ns();
  hdr->start_real_ns = now_real_ns();
  if (gethostname(hdr->hostname, sizeof(hdr->hostname) - 1))
    snprintf(hdr->hostname, sizeof(hdr->hostname), "unknown");
  if (!uname(&u)) snprintf(hdr->kernel, sizeof(hdr->kernel), "%.63s", u.release);
}

int ntp_writer_open(struct ntp_writer* w, const char* path,
                    const struct ntp_file_header* hdr) {
  w->records = 0;
  w->f = fopen(path, "wb");
  if (!w->f) {
    ntp_err("cannot create %s: %s", path, strerror(errno));
    return -1;
  }
  if (fwrite(hdr, sizeof(*hdr), 1, w->f) != 1) {
    ntp_err("cannot write %s: %s", path, strerror(errno));
    fclose(w->f);
    w->f = NULL;
    return -1;
  }
  return 0;
}

int ntp_writer_add(struct ntp_writer* w, const void* rec, size_t size) {
  if (fwrite(rec, size, 1, w->f) != 1) return -1;
  w->records++;
  return 0;
}

int ntp_writer_close(struct ntp_writer* w) {
  int ret = 0;

  if (!w->f) return 0;
  if (fclose(w->f)) ret = -1;
  w->f = NULL;
  return ret;
}

int ntp_read_file(const char* path, struct ntp_file_header* hdr,
                  const struct ntp_reader_ops* ops, void* arg) {
  union {
    struct ntp_rec_hdr hdr;
    struct ntp_host_rec host;
    struct ntp_tgt_rec tgt;
  } rec;
  FILE* f = fopen(path, "rb");
  size_t tgt_size;
  int ret = 0;

  if (!f) {
    ntp_err("cannot open %s: %s", path, strerror(errno));
    return -1;
  }
  if (fread(hdr, sizeof(*hdr), 1, f) != 1 ||
      memcmp(hdr->magic, NTP_FILE_MAGIC, sizeof(NTP_FILE_MAGIC))) {
    ntp_err("%s is not an ntprof v2 record file", path);
    fclose(f);
    return -1;
  }
  /* version 2: target records end before ts[NTP_T_SEND_DONE] */
  tgt_size = hdr->version == 2
                 ? offsetof(struct ntp_tgt_rec, ts) + NTP_T_SEND_DONE * sizeof(__u64)
                 : sizeof(struct ntp_tgt_rec);
  if ((hdr->version != NTP_FILE_VERSION && hdr->version != 2) ||
      hdr->header_size != sizeof(*hdr) ||
      hdr->host_rec_size != sizeof(struct ntp_host_rec) ||
      hdr->tgt_rec_size != tgt_size) {
    ntp_err("%s: written by another ntprof version (file v%u)", path,
            hdr->version);
    fclose(f);
    return -1;
  }

  while (!ret && fread(&rec.hdr, sizeof(rec.hdr), 1, f) == 1) {
    size_t size, rest;

    switch (rec.hdr.kind) {
      case NTP_REC_HOST:
        size = sizeof(rec.host);
        break;
      case NTP_REC_TARGET:
        memset((char*)&rec + sizeof(rec.hdr), 0, sizeof(rec) - sizeof(rec.hdr));
        size = tgt_size;
        break;
      default:
        ntp_err("%s: corrupt record (kind %u)", path, rec.hdr.kind);
        ret = -1;
        continue;
    }
    rest = size - sizeof(rec.hdr);
    if (fread((char*)&rec + sizeof(rec.hdr), rest, 1, f) != 1) {
      ntp_err("%s: truncated record", path);
      ret = -1;
      break;
    }
    if (rec.hdr.kind == NTP_REC_HOST && ops->host)
      ret = ops->host(&rec.host, arg);
    else if (rec.hdr.kind == NTP_REC_TARGET && ops->target)
      ret = ops->target(&rec.tgt, arg);
  }
  if (ferror(f)) {
    ntp_err("%s: read error", path);
    ret = -1;
  }
  fclose(f);
  return ret < 0 ? -1 : 0;
}
