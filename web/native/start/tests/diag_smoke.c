#include "runtime/core/events.h"
#include "runtime/core/runtime.h"
#include "runtime/core/window.h"
#include "runtime/plugins/interfaces.h"

#include <kh/kh_engine.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void khdays_check_update(int fn, int next);
void khdays_trace_script(void *state, void *entry);

unsigned char *khdays_window;
static int update_calls;
static void idle(void) {}

khdays_function_pointer khdays_function_lookup(KhdaysRef ref)
{
    return ref == 1 ? idle : NULL;
}

u32 khdays_events_vblank_count(void)
{
    return 1;
}

static void *find(const char *name, int32_t *module)
{
    (void)module;
    return strcmp(name, "test_idle") == 0 ? (void *)idle : NULL;
}

const void *khdays_plugins_engine(void)
{
    static const KhEngine engine = {.routine = find};
    return &engine;
}

static void expect_update(uintptr_t update)
{
    if (update != (uintptr_t)idle) abort();
    ++update_calls;
}

void khdays_input_update_ran(khdays_uptr update) { expect_update((uintptr_t)update); }
void khdays_cinelist_update_ran(uintptr_t update) { expect_update(update); }
void khdays_daycard_update_ran(uintptr_t update) { expect_update(update); }
void khdays_roomlist_update_ran(uintptr_t update) { expect_update(update); }
void khdays_charselect_update_ran(uintptr_t update) { expect_update(update); }
void khdays_camp_update_ran(uintptr_t update) { expect_update(update); }
void khdays_shop_update_ran(uintptr_t update) { expect_update(update); }
void khdays_field_wipe_update_ran(uintptr_t update) { expect_update(update); }
void khdays_situation_update_ran(uintptr_t update) { expect_update(update); }

static void *stepped;
void khdays_scene_step(void *state)
{
    stepped = state;
}

int main(int argc, char **argv)
{
    (void)argv;
    if (argc > 1) {
        khdays_check_update(1, 2);
        return 99;
    }
    khdays_check_update(1, -1);
    khdays_check_update(1, 1);
    if (update_calls != 18) abort();
    u32 state[76] = {0}, entry[20] = {0};
    khdays_trace_script(state, entry);
    if (stepped != state || (uintptr_t)khdays_diag_symbol("test_idle") != (uintptr_t)idle ||
        khdays_diag_symbol("missing") != 0) abort();
    puts("Wasm startup diagnostics preserve game update and scene callbacks.");
    return 0;
}
