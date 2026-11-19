/* ROM-free fixture around a temporary generated ORIGINAL Days battle routine. */
#include "khdays_references.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

unsigned char *khdays_window;
extern int Ov022_IsFrameInComboWindow(int actor, int frame);
extern const struct khdays_route_record khdays_route_record_Ov022_IsFrameInComboWindow;
static int hook(int actor, int frame) { (void)actor; return frame == 77; }
int main(void)
{
    const unsigned actor=0x02000100u, slot=0x02001000u, window=0x02001100u;
    khdays_window=calloc(1,0x02400000u);
    assert(khdays_window);
    *(unsigned *)KHDAYS_W(actor+0x470)=slot;
    *(unsigned *)KHDAYS_W(slot+0x14)=window;
    *(int *)KHDAYS_W(window+0x10)=20;
    *(int *)KHDAYS_W(window+0x14)=40;
    assert(!Ov022_IsFrameInComboWindow(actor,19));
    assert(Ov022_IsFrameInComboWindow(actor,20));
    assert(Ov022_IsFrameInComboWindow(actor,39));
    assert(!Ov022_IsFrameInComboWindow(actor,40));
    *(int *)KHDAYS_W(window+0x14)=-0x1000;
    assert(Ov022_IsFrameInComboWindow(actor,100000));
    const struct khdays_route_record *record=&khdays_route_record_Ov022_IsFrameInComboWindow;
    assert(record->route && record->body && *record->route==record->body);
    *record->route=(void *)hook;
    assert(Ov022_IsFrameInComboWindow(actor,77));
    assert(!Ov022_IsFrameInComboWindow(actor,78));
    *record->route=record->body;
    assert(Ov022_IsFrameInComboWindow(actor,78));
    free(khdays_window);
    puts("Actual Clang/wasm32 reference pass + original Days combo + generated route: passed");
    return 0;
}
