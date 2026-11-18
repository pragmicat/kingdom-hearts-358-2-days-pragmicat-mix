/* The upstream sound suite does not test logging. Keep its real sound bridge
 * linked while excluding the unrelated Windows session-log platform. */
int khdays_log_on(void) { return 0; }
void khdays_log(const char *format, ...) { (void)format; }
