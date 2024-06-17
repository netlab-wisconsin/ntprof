#ifndef _K_NTTM_H_
#define _K_NTTM_H_
#include <linux/random.h>
#include <linux/limits.h>

#include "config.h"
/** sample rate for the sliding window */
#define SAMPLE_RATE 0.001

/**
 * generate a random number and compare it with the sample rate
 * @return true if the random number is less than the sample rate
 */
static bool to_sample(void) {
  unsigned int rand;
  get_random_bytes(&rand, sizeof(rand));
  return rand < SAMPLE_RATE * UINT_MAX;
}


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