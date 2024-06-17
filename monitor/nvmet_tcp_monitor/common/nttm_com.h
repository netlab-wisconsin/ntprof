#ifndef _NTTM_COM_H_
#define _NTTM_COM_H_



struct nvmet_tcp_read_breakdown {
  int toremove;
};

struct nvmet_tcp_write_breakdown {
  int toremove;
};

struct nvmet_tcp_stat {
  struct nvmet_tcp_read_breakdown read_breakdown;
  struct nvmet_tcp_write_breakdown write_breakdown;
};

#endif // _NTTM_COM_H_