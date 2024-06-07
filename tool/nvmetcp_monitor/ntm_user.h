static volatile int keep_running = 1;
void int_handler(int dummy) {
  keep_running = 0;
}