#ifndef _NTTM_COM_H_
#define _NTTM_COM_H_



struct nvmet_tcp_read_breakdown {
  u64 in_nvmet_tcp_time;
  u64 in_blk_time;
  u64 end2end_time;
  u32 cnt;
};

struct nvmet_tcp_write_breakdown {
  u64 make_r2t_time;
  u64 in_nvmet_tcp_time;
  u64 in_blk_time;
  u64 end2end_time;
  u32 cnt;
};

struct nvmet_tcp_stat {
  struct nvmet_tcp_read_breakdown read_breakdown;
  struct nvmet_tcp_write_breakdown write_breakdown;
};

#endif // _NTTM_COM_H_