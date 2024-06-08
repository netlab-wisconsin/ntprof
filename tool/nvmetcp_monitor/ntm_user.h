#ifndef NTM_USER_H
#define NTM_USER_H

static volatile int keep_running = 1;

void int_handler(int dummy) { keep_running = 0; }

#endif  // NTM_USER_H