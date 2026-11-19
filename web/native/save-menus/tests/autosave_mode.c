/* Exercise the browser initialization in the real autosave compilation unit.
 * The linker drops gameplay functions this test does not call. */
#define _POSIX_C_SOURCE 200809L
#include "runtime/menus/autosave.c"

int main(int argc, char **argv)
{
    const int practice = argc > 1 && strcmp(argv[1], "practice") == 0;
    if (practice && setenv("PRAGMICAT_PRACTICE", "1", 1) != 0) {
        return 1;
    }
    if (argc > 1 && strcmp(argv[1], "explicit") == 0) {
        khdays_autosave_set_mode(MODE_PLAYED);
        initialize_browser_mode();
        return mode != MODE_PLAYED;
    }
    initialize_browser_mode();
    return mode != (practice ? MODE_OFF : MODE_FILE99);
}
