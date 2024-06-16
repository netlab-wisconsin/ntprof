#ifndef _K_NTTM_H_
#define _K_NTTM_H_

#include "config.h"

/** inticator, to record or not */
static int ctrl = 0;

/** for storing the device name */
// static char device_name[32] = "";

static Arguments* args;

/** a thread, periodically update the communication data strucure */
static struct task_struct *update_routine_thread;

struct proc_dir_entry *parent_dir;
struct proc_dir_entry *entry_ctrl;
struct proc_dir_entry *entry_args;


#endif // _K_NTTM_H_