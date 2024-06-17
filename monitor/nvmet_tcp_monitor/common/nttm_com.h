#ifndef _NTTM_COM_H_
#define _NTTM_COM_H_



struct nvmet_tcp_read_breakdown {
  unsigned long long in_nvmet_tcp_time;
  unsigned long long in_blk_time;
  unsigned long long end2end_time;
  unsigned long cnt;
};

struct nvmet_tcp_write_breakdown {
  unsigned long long make_r2t_time;
  unsigned long long in_nvmet_tcp_time;
  unsigned long long in_blk_time;
  unsigned long long end2end_time;
  unsigned long cnt;
};

struct nvmet_tcp_stat {
  struct nvmet_tcp_read_breakdown read_breakdown;
  struct nvmet_tcp_write_breakdown write_breakdown;
};

#endif // _NTTM_COM_H_