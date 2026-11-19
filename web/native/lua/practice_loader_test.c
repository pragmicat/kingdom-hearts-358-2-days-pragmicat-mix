/* The stock upstream fixture runs its original 37 cases before loading the
 * authored Pragmicat practice script through the same sandbox and option API. */
#define main upstream_script_tests
#include "tests/script_test.c"
#undef main

static void test_pragmicat_practice(void)
{
    begin_case("pragmicat_practice");
    char *manifest = khdays_folders_read("/fixture/practice.ini", NULL);
    char *source = khdays_folders_read("/fixture/practice.lua", NULL);
    CHECK(manifest != NULL && source != NULL);
    if (manifest == NULL || source == NULL) {
        goto finish;
    }
    put("mods/Pragmicat Practice/mod.ini", manifest);
    put("mods/Pragmicat Practice/scripts/main.lua", source);
    put("saves/mods/local.pragmicat-practice/options.ini", "targets = 5\n");
    start();
    frames(1);
    const KhdaysMod *practice = mod_by_folder("Pragmicat Practice");
    CHECK(practice != NULL && practice->state == KHDAYS_MOD_LOADED);
    if (practice != NULL && practice->state == KHDAYS_MOD_LOADED) {
        const KhdaysOption *targets = NULL;
        for (int i = 0; i < practice->option_count; ++i) {
            if (strcmp(practice->options[i].key, "targets") == 0) {
                targets = &practice->options[i];
                break;
            }
        }
        CHECK(targets != NULL && targets->kind == KHDAYS_OPTION_CHOICE && targets->value_count == 5);
        if (targets != NULL && targets->value_count == 5) {
            CHECK(targets->value == 5 && strcmp(targets->values[4], "Endless Heartless") == 0);
        }
        CHECK(practice->handler_count >= 5);
    }
finish:
    free(source);
    free(manifest);
    end_case();
}

int main(void)
{
    if (upstream_script_tests() != 0) {
        return 1;
    }
    test_pragmicat_practice();
    if (failures == 0) {
        puts("Pragmicat practice loaded in Lua with Endless Heartless selected");
    }
    return failures != 0;
}
