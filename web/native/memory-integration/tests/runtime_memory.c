#include "runtime/core/game_heap.h"
#include "runtime/core/io.h"
#include "runtime/core/runtime.h"
#include "runtime/core/window.h"
#include "runtime/plugins/interfaces.h"
#include "runtime/scripts/game_link.h"
#include "host/app/context.h"
#include "sound/snd7_internal.h"
#include <kh/kh_engine.h>
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

void *generated_address(KhdaysRef reference);
void *generated_host(KhdaysRef reference);
KhdaysRef generated_reference(const void *host);

static KhdaysContext *root;
static KhdaysRef local_reference;
static unsigned heap_requested;
static const unsigned main_ram = 0x02000000u;

static void fiber(void *unused)
{
    (void)unused;
    volatile unsigned sum = 17;
    local_reference = KHDAYS_REF((const void *)&sum);
    assert(local_reference >= 0x0c010000u && local_reference < 0x1c000000u);
    khdays_context_switch(root);
    assert(sum == 31);
    khdays_context_switch(root);
    abort();
}

/* Unused SDK tables, diagnostic sinks and the boundary to the original
 * Nitro heap routines. The memory, conversion and heap routing under test
 * all come from the patched upstream production units. */
const struct { const char *name; void (*native)(void); int module; } khdays_routine_names[1] = {{0}};
const unsigned khdays_routine_name_count = 0;
const struct { const char *name; unsigned reference; } khdays_datum_names[1] = {{0}};
const unsigned khdays_datum_name_count = 0;
void khdays_log(const char *format, ...) { (void)format; }
void khdays_log_close(void) {}
const char *khdays_diag_function_at(const void *address) { (void)address; return "?"; }
void khdays_hal_unimplemented(const char *what) { fputs(what, stderr); abort(); }
void *khdays_native_at(unsigned address) { (void)address; return NULL; }
int khdays_overlay_function_at(unsigned address, int *module) { (void)address; (void)module; return 0; }
int khdays_overlay_loaded(int id) { (void)id; return 0; }
unsigned khdays_events_vblank_count(void) { return 0; }
int khdays_plugins_on_game_thread(void) { return 1; }
KhPlugin *khdays_plugins_called(void) { return NULL; }
int khdays_plugins_may_change(KhPlugin *plugin) { (void)plugin; return 1; }
void khdays_plugins_tell(KhPlugin *plugin, const char *text) { (void)plugin; (void)text; }

KhdaysRef OS_AllocFromArenaLo(int arena, unsigned size, unsigned alignment)
{
    assert(arena == 0 && alignment == 16 && size == 256);
    heap_requested++;
    return main_ram + 0x1000;
}

KhdaysRef NNS_FndCreateExpHeapEx(KhdaysRef start, unsigned size, unsigned short option)
{
    assert(start == main_ram + 0x1000 && size == 256 && option == 0);
    *(unsigned *)KHDAYS_W(start + 0x18) = start + 32;
    *(unsigned *)KHDAYS_W(start + 0x1c) = start + size;
    return start;
}

int main(void)
{
    assert(khdays_window_range(main_ram, 1) == NULL);
    assert(khdays_memory_map());
    unsigned *data = KHDAYS_W(main_ram + 0x20);
    assert(*data == 0);
    *data = 0xbeef1234u;
    assert(generated_host(main_ram + 0x20) == data && generated_host(0) == NULL);
    assert(generated_address(0x02400020u) == data);
    assert(generated_address(0x02800020u) == data);
    assert(generated_address(0x02c00020u) == data);
    assert(generated_reference(generated_address(0x02c00020u)) == main_ram + 0x20);
    assert(snd7_rd32(0x02400020u) == 0xbeef1234u);
    assert(KHDAYS_W(0x027f0020u) == KHDAYS_W(0x023f0020u));
    assert(KHDAYS_W(0x027e0020u) != KHDAYS_W(0x023e0020u));
    assert(khdays_window_range(0x027e4000u, 1) != NULL); /* Native plain() padding. */
    assert(khdays_window_range(0x023fffffu, 2) == NULL); /* Main-RAM wrap. */
    assert(khdays_window_range(0x027dffffu, 2) == NULL); /* DTCM boundary. */
    assert(khdays_window_range(0x04000000u, 1) == NULL); /* Handled by I/O routines. */
    assert(khdays_window_range(0x20000000u, 1) == NULL);
    assert(khdays_window_range(0xffffffffu, 2) == NULL);
    assert(khdays_window_range(0x06000000u, 0x008a4000u) != NULL);
    assert(khdays_window_range(0x09bb0000u, 0x10000u) != NULL);
    assert(khdays_window_range(0x09c00000u, 0x10000u) != NULL);
    assert(khdays_io_host == KHDAYS_W(0x0b100000u));
    assert(khdays_io2_host == KHDAYS_W(0x0b110000u));
    assert(*(unsigned *)KHDAYS_W(0x08000000u) == 0xffffffffu);
    assert(KHDAYS_W(0x08000000u) == KHDAYS_W(0x0a000000u));
    assert(khdays_memory_map() && *data == 0xbeef1234u);
    assert(khdays_game_heap_ds_memory());
    assert(khdays_game_heap(0, 256) == KHDAYS_W(main_ram + 0x1000));
    assert(heap_requested == 1 && khdays_heaps_committed() == 0);

    const KhEngine *sdk = khdays_plugins_engine();
    assert(sdk->pointer(main_ram + 0x20) == data);
    assert(sdk->pointer(0x02800020u) == data);
    assert(sdk->reference(data) == main_ram + 0x20);
    assert(sdk->pointer(0x20000000u) == NULL);
    assert(sdk->reference(&root) == 0);

    unsigned from_lua = 0;
    assert(khdays_link_read(0x02800020u, &from_lua, sizeof(from_lua)));
    assert(from_lua == *data);
    from_lua = 37;
    assert(khdays_link_write(main_ram + 0x20, &from_lua, sizeof(from_lua)));
    assert(*data == from_lua);
    assert(!khdays_link_reachable(0x02bfffffu, 2));
    assert(!khdays_link_reachable(0x20000000u, 1));
    assert(!khdays_link_reachable(main_ram, ULLONG_MAX));
    assert(khdays_link_write(0x02400000u, NULL, 0));

    root = khdays_context_init();
    assert(root != NULL);
    KhdaysContext *game = khdays_context_create(fiber, NULL);
    assert(game != NULL);
    khdays_context_switch(game);
    assert(khdays_context_current() == root);
    const KhdaysRef first_local = local_reference;
    *(unsigned *)KHDAYS_W(local_reference) = 31;
    khdays_context_switch(game);
    khdays_context_delete(game);
    assert(khdays_window_range(first_local, 1) == NULL);

    game = khdays_context_create(fiber, NULL);
    assert(game != NULL);
    khdays_context_switch(game);
    assert(local_reference == first_local);
    khdays_context_delete(game);

    khdays_memory_unmap();
    assert(khdays_io_host == NULL && khdays_io2_host == NULL);
    assert(khdays_window_range(main_ram, 1) == NULL);
    assert(khdays_memory_map() && *(unsigned *)KHDAYS_W(main_ram + 0x20) == 0);
    khdays_memory_unmap();
    puts("native memory map, generated pointers, SDK, Lua memory and Asyncify stacks passed");
    return 0;
}
