/* Test-only process-exit shim; production uses the browser platform layer. */
#ifndef TEST_WINDOWS_H
#define TEST_WINDOWS_H
#include <stdlib.h>
#define ExitProcess(code) exit((int)(code))
#endif
