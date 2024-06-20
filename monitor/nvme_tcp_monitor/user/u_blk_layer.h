#ifndef U_BLK_LAYER_H
#define U_BLK_LAYER_H

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdbool.h>

#include "ntm_com.h"


static struct shared_blk_layer_stat *shared;

void blk_layer_monitor_display();

void init_blk_layer_monitor();

void exit_blk_layer_monitor();


#endif // U_BLK_LAYER_H