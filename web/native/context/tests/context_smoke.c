#include "context.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static KhdaysContext *root, *a, *b;
static unsigned events[8], event_count, allocated, released;
static unsigned char *custom_stack;
static size_t custom_size;
static void event(unsigned value) { assert(event_count < 8); events[event_count++] = value; }
static void *stack_alloc(size_t size)
{
    ++allocated;
    custom_size = size;
    custom_stack = aligned_alloc(16, size);
    return custom_stack;
}
static void stack_release(void *stack, size_t size) { (void)size; ++released; free(stack); }
static void *refuse(size_t size) { (void)size; return NULL; }

static void entry_b(void *argument)
{
    assert((uintptr_t)argument == 22);
    assert(khdays_context_current() == b);
    event(2);
    khdays_context_switch(a);
    abort(); /* b is deleted while suspended, so it must never resume */
}

static void entry_a(void *argument)
{
    volatile unsigned local = 100;
    assert((uintptr_t)argument == 11);
    assert(khdays_context_current() == a);
    /* Volatile local must be on the supplied C stack, not a separate host
     * stack. The real runtime depends on this to translate locals to DS refs. */
    assert((uintptr_t)&local >= (uintptr_t)custom_stack);
    assert((uintptr_t)&local + sizeof(local) <= (uintptr_t)custom_stack + custom_size);
    event(1);
    khdays_context_switch(a); /* switching to self is harmless */
    khdays_context_switch(b);
    assert(khdays_context_current() == a && local == 100);
    ++local;
    event(3);
    khdays_context_switch(root);
    assert(khdays_context_current() == a && local == 101);
    event(4);
    for (;;) khdays_context_switch(root);
}

int main(void)
{
    assert(khdays_context_create(entry_a, NULL) == NULL);
    root = khdays_context_init();
    assert(root && khdays_context_current() == root);
    assert(khdays_context_init() == NULL);
    khdays_context_stack_source(refuse, stack_release);
    assert(khdays_context_create(entry_a, NULL) == NULL);
    khdays_context_stack_source(stack_alloc, NULL);
    assert(khdays_context_create(entry_a, NULL) == NULL);
    khdays_context_stack_source(stack_alloc, stack_release);
    assert(khdays_context_create(NULL, NULL) == NULL);
    a = khdays_context_create(entry_a, (void *)(uintptr_t)11);
    khdays_context_stack_source(NULL, NULL);
    b = khdays_context_create(entry_b, (void *)(uintptr_t)22);
    assert(a && b && allocated == 1 && released == 0);
    khdays_context_switch(a);
    assert(khdays_context_current() == root);
    assert(event_count == 3 && events[0] == 1 && events[1] == 2 && events[2] == 3);
    khdays_context_delete(b);
    assert(released == 0); /* b used the host allocator */
    khdays_context_switch(a);
    assert(khdays_context_current() == root);
    assert(event_count == 4 && events[3] == 4);
    khdays_context_delete(a);
    assert(released == 1); /* a retains its allocator despite later source changes */
    khdays_context_delete(NULL);
    puts("Emscripten fiber context smoke passed");
    return 0;
}
