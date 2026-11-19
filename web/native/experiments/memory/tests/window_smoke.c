#include "compact_window.h"
#include "context.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static KhdaysWebWindow window;
static KhdaysContext *root, *child;
static unsigned released;

static unsigned char *pointer(uint32_t ref, size_t size)
{
    void *result = khdays_web_window_pointer(&window, ref, size);
    assert(result != NULL);
    return result;
}
static uint32_t reference(const void *host)
{
    uint32_t result;
    assert(khdays_web_window_reference(&window, host, &result));
    return result;
}
static void *stack_allocate(size_t size)
{
    void *stack = aligned_alloc(16, size);
    assert(stack != NULL && size <= UINT32_MAX);
    assert(khdays_web_window_add(&window, 0x0c010000u, (uint32_t)size, stack));
    return stack;
}
static void stack_release(void *stack, size_t size)
{
    (void)size;
    assert(khdays_web_window_remove(&window, 0x0c010000u));
    ++released;
    free(stack);
}
static void run_child(void *argument)
{
    volatile uint32_t local = 0x12345678u;
    uint32_t ref = reference((const void *)&local);
    (void)argument;
    assert(ref >= 0x0c010000u && ref < 0x0c110000u);
    assert(pointer(ref, sizeof(local)) == (unsigned char *)&local);
    *(volatile uint32_t *)pointer(ref, sizeof(local)) = 0x87654321u;
    khdays_context_switch(root);
    assert(local == 0x87654321u && reference((const void *)&local) == ref);
    for (;;) khdays_context_switch(root);
}

int main(void)
{
    unsigned char *ram = calloc(1, 0x400000u), *dtcm = calloc(1, 0x4000u);
    unsigned char *headers = calloc(1, 0x7f8000u), *low_heap = calloc(1, 0x1000u), *high_heap = calloc(1, 0x1000u);
    uint32_t ref = 99;
    unsigned char outside = 0;
    assert(ram && dtcm && headers && low_heap && high_heap);
    khdays_web_window_init(&window);
    assert(khdays_web_window_add(&window, 0x02000000u, 0x400000u, ram));
    assert(khdays_web_window_alias(&window, 0x02400000u, 0x3e0000u, 0x02000000u));
    assert(khdays_web_window_alias(&window, 0x027f0000u, 0x10000u, 0x023f0000u));
    assert(khdays_web_window_add(&window, 0x027e0000u, 0x4000u, dtcm));
    assert(khdays_web_window_add(&window, 0x02800000u, 0x7f8000u, headers));
    assert(khdays_web_window_add(&window, 0x20000000u, 0x1000u, low_heap));
    assert(khdays_web_window_add(&window, 0x7ffff000u, 0x1000u, high_heap));
    *pointer(0x02400010u, 1) = 0x72;
    assert(*pointer(0x02000010u, 1) == 0x72);
    assert(pointer(0x02400010u, 1) == pointer(0x02000010u, 1));
    assert(reference(pointer(0x02400010u, 1)) == 0x02000010u); /* deliberate alias canonicalization */
    *pointer(0x023f0010u, 1) = 0x4a;
    assert(*pointer(0x027f0010u, 1) == 0x4a);
    *pointer(0x027e0000u, 1) = 0xa1;
    assert(*pointer(0x023e0000u, 1) == 0 && *pointer(0x027e0000u, 1) == 0xa1);
    assert(khdays_web_window_pointer(&window, 0x027e4000u, 1) == NULL); /* DTCM hole */
    assert(khdays_web_window_pointer(&window, 0x027dffffu, 2) == NULL); /* cross-region span */
    assert(khdays_web_window_pointer(&window, 0x023fffffu, 2) == NULL); /* alias wrap */
    assert(reference(pointer(0x02ff7fffu, 1)) == 0x02ff7fffu);
    assert((reference(pointer(0x02ff7ffcu, 4)) - 0x01ff8000u) < 0x1000000u); /* 24-bit header handle */
    assert(reference(pointer(0x20000004u, 4)) == 0x20000004u);
    assert(reference(pointer(0x7ffffffcu, 4)) == 0x7ffffffcu);
    assert(khdays_web_window_pointer(&window, 0x7fffffffu, 2) == NULL);
    assert(reference(NULL) == 0);
    assert(!khdays_web_window_reference(&window, &outside, &ref) && ref == 0);
    assert(!khdays_web_window_add(&window, 0x02100000u, 1, &outside)); /* reference overlap */
    assert(!khdays_web_window_add(&window, 0x10000000u, 1, ram)); /* host overlap */
    assert(!khdays_web_window_alias(&window, 0x10000000u, 1, 0x02400000u)); /* alias of alias */
    assert(!khdays_web_window_add(&window, 0xffffffffu, 2, &outside)); /* wrap */
    assert(!khdays_web_window_remove(&window, 0x02000000u)); /* live aliases */
    /* Upstream KHDAYS_DS_MEMORY=1 is a possible first browser boot path:
     * headers and normal allocations use the original DS arena, and these
     * two upper ranges are RAM aliases rather than expanded header heaps. */
    assert(khdays_web_window_remove(&window, 0x02800000u));
    assert(khdays_web_window_alias(&window, 0x02800000u, 0x400000u, 0x02000000u));
    assert(khdays_web_window_alias(&window, 0x02c00000u, 0x400000u, 0x02000000u));
    *pointer(0x02c01000u, 1) = 0x39;
    assert(*pointer(0x02801000u, 1) == 0x39 && *pointer(0x02001000u, 1) == 0x39);
    assert(reference(pointer(0x02c01000u, 1)) == 0x02001000u);
    root = khdays_context_init();
    assert(root);
    khdays_context_stack_source(stack_allocate, stack_release);
    child = khdays_context_create(run_child, NULL);
    assert(child);
    khdays_context_switch(child);
    khdays_context_switch(child);
    khdays_context_delete(child);
    assert(released == 1 && khdays_web_window_pointer(&window, 0x0c010000u, 1) == NULL);
    puts("Compact memory + actual Asyncify fiber smoke passed");
    free(high_heap); free(low_heap); free(headers); free(dtcm); free(ram);
    return 0;
}
