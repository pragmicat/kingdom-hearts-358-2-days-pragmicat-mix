/* ROM-free harness around one generated ORIGINAL game function. The function
 * itself is fetched from the pinned decomp checkout, never copied here. */
#include "khdays_references.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

unsigned char *khdays_window;
extern int Ov022_IsFrameInComboWindow(int actor, int frame);
extern const struct khdays_route_record khdays_route_record_Ov022_IsFrameInComboWindow;
static int calls;
static int hook(int actor, int frame)
{
    (void)actor;
    ++calls;
    return frame == 77;
}
int main(void)
{
    const unsigned actor = 0x02000100u, slot = 0x02001000u, descriptor = 0x02001100u;
    khdays_window = calloc(1, 0x02400000u);
    assert(khdays_window);
    *(unsigned *)KHDAYS_W(actor + 0x470) = slot;
    *(unsigned *)KHDAYS_W(slot + 0x14) = descriptor;
    *(int *)KHDAYS_W(descriptor + 0x10) = 20;
    *(int *)KHDAYS_W(descriptor + 0x14) = 40;
    assert(Ov022_IsFrameInComboWindow((int)actor, 19) == 0);
    assert(Ov022_IsFrameInComboWindow((int)actor, 20) == 1);
    assert(Ov022_IsFrameInComboWindow((int)actor, 39) == 1);
    assert(Ov022_IsFrameInComboWindow((int)actor, 40) == 0);
    *(int *)KHDAYS_W(descriptor + 0x14) = -0x1000;
    assert(Ov022_IsFrameInComboWindow((int)actor, 100000) == 1);
    assert(Ov022_IsFrameInComboWindow((int)actor, 0x7fffffff) == 0);
    const struct khdays_route_record *route = &khdays_route_record_Ov022_IsFrameInComboWindow;
    assert(route->route && route->body && *route->route == route->body);
    *route->route = (void *)hook;
    assert(Ov022_IsFrameInComboWindow((int)actor, 77) == 1);
    assert(Ov022_IsFrameInComboWindow((int)actor, 78) == 0 && calls == 2);
    *route->route = route->body;
    assert(Ov022_IsFrameInComboWindow((int)actor, 78) == 1 && calls == 2);
    puts("Original Days combo C -> reference writer -> Wasm + explicit hook record: passed");
    free(khdays_window);
    return 0;
}
