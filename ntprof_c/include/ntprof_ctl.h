#ifndef NTPROF_CTL_H
#define NTPROF_CTL_H

#include <linux/ioctl.h>
#include "config.h"
#include "analyze.h"

#define NTPROF_MAGIC 'N'

#define NTPROF_IOCTL_START _IOW(NTPROF_MAGIC, 1, struct ntprof_config)
#define NTPROF_IOCTL_STOP  _IO(NTPROF_MAGIC, 2)
#define NTPROF_IOCTL_ANALYZE _IOWR(NTPROF_MAGIC, 3, struct analyze_arg)

#endif //NTPROF_CTL_H
