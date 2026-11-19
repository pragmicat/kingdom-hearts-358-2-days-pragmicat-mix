#include "static_registry.h"
#include "window.h"
#include "runtime.h"
#include "interfaces.h"
#include "fixture_memory.h"
#include <kh/kh_engine.h>

#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(KHDAYS_BROWSER_MEMORY)
static int stack_source_ready;
/* This test covers production map/registry integration. It registers but does
 * not create a game fiber; allocator installation is the bounded stub here. */
void khdays_context_stack_source(void *(*allocate)(size_t), void (*release)(void *, size_t))
{
    stack_source_ready = allocate != NULL && release != NULL;
}
#else
static unsigned char memory[128];
#endif
static int started = -99, permitted = 1, game_thread = 1;
static KhPlugin *owner = (KhPlugin *)(uintptr_t)1;

const char *khdays_diag_function_at(const void *address) { (void)address; return "test"; }
void khdays_log(const char *text, ...) { (void)text; }
void khdays_log_close(void) {}
void khdays_hal_unimplemented(const char *what) { fprintf(stderr, "%s\n", what); abort(); }
void *khdays_native_at(unsigned address) { (void)address; return NULL; }
int khdays_overlay_function_at(unsigned address, int *module) { (void)address; (void)module; return 0; }
void khdays_overlay_started(int module) { started = module; }
int khdays_overlay_loaded(int module) { return module == 7; }
unsigned khdays_events_vblank_count(void) { return 42u; }
int khdays_plugins_on_game_thread(void) { return game_thread; }
KhPlugin *khdays_plugins_called(void) { return owner; }
int khdays_plugins_may_change(KhPlugin *plugin) { (void)plugin; return permitted; }
void khdays_plugins_tell(KhPlugin *plugin, const char *text) { (void)plugin; (void)text; }

typedef struct { const char *name; void (*native)(void); int module; } KhdaysRoutineName;
typedef struct { const char *name; unsigned reference; } KhdaysDatumName;
const KhdaysRoutineName khdays_routine_names[1] = {{0}};
const unsigned khdays_routine_name_count = 0;
const KhdaysDatumName khdays_datum_names[1] = {{0}};
const unsigned khdays_datum_name_count = 0;

#if !defined(TEST_EMPTY_REGISTRY)
int alpha(int value);
int beta(int value);
static int replaced_alpha(int value) { return value + 100; }
static int replaced_again(int value) { return value + 1000; }
#endif

int main(void)
{
#if defined(KHDAYS_BROWSER_MEMORY)
    assert(khdays_memory_map());
    unsigned char *memory = KHDAYS_W(FIXTURE_MEMORY_BASE);
    assert(memory != NULL && stack_source_ready);
#else
    khdays_window = memory;
#endif
    const KhEngine *engine = khdays_plugins_engine();
#if defined(KHDAYS_BROWSER_MEMORY)
    assert(engine->pointer(FIXTURE_MEMORY_BASE + 16u) == memory + 16);
    assert(engine->reference(memory + 16) == FIXTURE_MEMORY_BASE + 16u);
#endif
#if defined(TEST_EMPTY_REGISTRY)
    assert(khdays_static_data_count == 0 && khdays_static_function_count == 0 && khdays_static_route_count == 0);
    khdays_data_init(-4);
    khdays_data_init(7);
    assert(started == 7);
    assert(khdays_function_lookup(0xfe000004u) == NULL);
    assert(khdays_function_lookup(0xfe000004u) == NULL);
    assert(engine->replace("missing", (void *)main, NULL) == KH_ERROR);
    assert(engine->replace("missing", (void *)main, NULL) == KH_ERROR);
    puts("empty production static registries: passed");
#else
    assert(khdays_static_data_count == 3 && khdays_static_function_count == 3 && khdays_static_route_count == 2);
    unsigned value;
    khdays_data_init(-4);
    memcpy(&value, memory + 16, sizeof(value));
#if defined(TEST_REVERSED_ORDER)
    assert(value == 0x12345678u);
#else
    assert(value == 0xaabbccddu);
#endif
    memcpy(&value, memory + 32, sizeof(value));
    assert(value == 0 && started == -99);
    khdays_data_init(7);
    memcpy(&value, memory + 32, sizeof(value));
    assert(value == 0x87654321u && started == 7);
    assert(khdays_function_lookup(0xfe000004u) == (khdays_function_pointer)beta);
    assert(khdays_function_lookup(0xfe00000cu) == (khdays_function_pointer)alpha);
    assert(((int (*)(int))khdays_function(0xfe000004u))(4) == 12);
    assert(((int (*)(int))khdays_function(0xfe00000cu))(4) == 14);
    assert(khdays_function_lookup(0xfe000000u) == NULL);
    void *previous = NULL;
    assert(engine->replace("alpha", (void *)replaced_alpha, &previous) == KH_OK);
    assert(((int (*)(int))previous)(4) == 14);
    assert(alpha(4) == 104 && beta(4) == 12);
    assert(((int (*)(int))khdays_function(0xfe00000cu))(4) == 104);
    permitted = 0;
    khdays_plugins_engine_frame();
    assert(alpha(4) == 14);
    permitted = 1;
    khdays_plugins_engine_frame();
    assert(alpha(4) == 104);
    owner = (KhPlugin *)(uintptr_t)2;
    assert(engine->replace("alpha", (void *)replaced_again, &previous) == KH_OK);
    assert(alpha(4) == 1004 && ((int (*)(int))previous)(4) == 104);
    khdays_plugins_engine_forget((KhPlugin *)(uintptr_t)2);
    assert(alpha(4) == 104);
    khdays_plugins_engine_forget((KhPlugin *)(uintptr_t)1);
    assert(alpha(4) == 14);
    permitted = 0;
    assert(engine->replace("beta", (void *)replaced_again, NULL) == KH_OK);
    assert(beta(4) == 12);
    permitted = 1;
    khdays_plugins_engine_frame();
    assert(beta(4) == 1004);
    khdays_plugins_engine_forget(owner);
    assert(beta(4) == 12);
    game_thread = 0;
    assert(engine->replace("beta", (void *)replaced_again, NULL) == KH_ERROR);
    assert(beta(4) == 12);
    puts("production multi-TU data/function registries and engine hooks: passed");
#endif
#if defined(KHDAYS_BROWSER_MEMORY)
    khdays_memory_unmap();
    assert(!stack_source_ready && khdays_window_range(FIXTURE_MEMORY_BASE, 1) == NULL);
    puts("production browser memory and static registry integration: passed");
#endif
    return 0;
}
