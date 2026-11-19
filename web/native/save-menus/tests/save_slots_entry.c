#include <emscripten.h>

int main(int argc, char **argv);

EMSCRIPTEN_KEEPALIVE int days_test_save_slots(void)
{
    char name[] = "days-test";
    char directory[] = "saves";
    char *arguments[] = {name, directory, 0};
    return main(2, arguments);
}
