#define _POSIX_C_SOURCE 200809L
#include <emscripten.h>
#include <stdlib.h>

/* Called once by the browser launcher before the native entry point. The real
 * practice_boot.cpp reads this same flag when the built-in plugin starts. */
EMSCRIPTEN_KEEPALIVE int khdays_pragmicat_select_mode(int practice)
{
    static int selected;
    if (selected || (practice != 0 && practice != 1)) {
        return 0;
    }
    const int result = practice ? setenv("PRAGMICAT_PRACTICE", "1", 1) :
        unsetenv("PRAGMICAT_PRACTICE");
    if (result != 0) {
        return 0;
    }
    selected = 1;
    return 1;
}
