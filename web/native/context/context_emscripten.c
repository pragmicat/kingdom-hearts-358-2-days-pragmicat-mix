/* Cooperative DS execution contexts for the browser port of khdays-port.
 * Replaces context_win.c/context_x64.asm; link the ENTIRE game with -sASYNCIFY.
 * The C stack stays in the runtime-provided game-memory window so references
 * to game locals retain the same semantics as the native Windows port.
 * Asyncify continuation storage is separate host memory, as Emscripten requires.
 */
#include "context.h"

#include <emscripten/fiber.h>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

/* Initial limits; native game profiling still needs to establish suitable
 * production sizes. Both are configurable at build time, in bytes. */
#ifndef KHDAYS_WEB_CONTEXT_STACK_SIZE
#define KHDAYS_WEB_CONTEXT_STACK_SIZE (1024u * 1024u)
#endif
#ifndef KHDAYS_WEB_CONTEXT_ASYNCIFY_SIZE
#define KHDAYS_WEB_CONTEXT_ASYNCIFY_SIZE (1024u * 1024u)
#endif

_Static_assert(KHDAYS_WEB_CONTEXT_STACK_SIZE > 0 && KHDAYS_WEB_CONTEXT_STACK_SIZE % 16 == 0,
               "fiber C stack size must be a positive multiple of 16");
_Static_assert(KHDAYS_WEB_CONTEXT_ASYNCIFY_SIZE > 0, "fiber Asyncify stack size must be positive");

struct KhdaysContext {
    emscripten_fiber_t fiber;
    void *stack;
    size_t size;
    void (*release)(void *stack, size_t size);
    void *asyncify_stack;
    void (*entry)(void *argument);
    void *argument;
};

/* The SDK schedules all contexts on one host thread, as on the Windows port.
 * Fibers must never migrate to another thread: their rewind IDs are local. */
static KhdaysContext *current;
static void *(*allocate_stack)(size_t size);
static void (*release_stack)(void *stack, size_t size);

static void host_release(void *stack, size_t size)
{
    (void)size;
    free(stack);
}

void khdays_context_stack_source(void *(*allocate)(size_t size), void (*release)(void *stack, size_t size))
{
    allocate_stack = allocate;
    release_stack = release;
}

static void started(void *argument)
{
    KhdaysContext *context = argument;
    assert(current == context);
    context->entry(context->argument);
    /* Unlike an Emscripten entry's default process-exit behavior, a returning
     * game thread is a runtime defect. Preserve upstream's fail-fast contract. */
    abort();
}

KhdaysContext *khdays_context_init(void)
{
    KhdaysContext *context;
    if (current != NULL) {
        return NULL; /* context.h permits initialization exactly once */
    }
    context = calloc(1, sizeof(*context));
    if (context == NULL) {
        return NULL;
    }
    context->asyncify_stack = malloc(KHDAYS_WEB_CONTEXT_ASYNCIFY_SIZE);
    if (context->asyncify_stack == NULL) {
        free(context);
        return NULL;
    }
    emscripten_fiber_init_from_current_context(&context->fiber, context->asyncify_stack,
                                              KHDAYS_WEB_CONTEXT_ASYNCIFY_SIZE);
    current = context;
    return context;
}

KhdaysContext *khdays_context_current(void)
{
    return current;
}

KhdaysContext *khdays_context_create(void (*entry)(void *argument), void *argument)
{
    KhdaysContext *context;
    if (current == NULL || entry == NULL || (allocate_stack != NULL && release_stack == NULL)) {
        return NULL;
    }
    context = calloc(1, sizeof(*context));
    if (context == NULL) {
        return NULL;
    }
    context->size = KHDAYS_WEB_CONTEXT_STACK_SIZE;
    context->release = allocate_stack != NULL ? release_stack : host_release;
    context->stack = allocate_stack != NULL ? allocate_stack(context->size) : aligned_alloc(16, context->size);
    if (context->stack == NULL) {
        free(context);
        return NULL;
    }
    /* The Emscripten API requires at least 16-byte aligned C stack bounds.
     * Reject a broken custom allocator before invoking undefined behavior. */
    if ((uintptr_t)context->stack % 16 != 0) {
        context->release(context->stack, context->size);
        free(context);
        return NULL;
    }
    context->asyncify_stack = malloc(KHDAYS_WEB_CONTEXT_ASYNCIFY_SIZE);
    if (context->asyncify_stack == NULL) {
        context->release(context->stack, context->size);
        free(context);
        return NULL;
    }
    context->entry = entry;
    context->argument = argument;
    emscripten_fiber_init(&context->fiber, started, context, context->stack, context->size,
                          context->asyncify_stack, KHDAYS_WEB_CONTEXT_ASYNCIFY_SIZE);
    return context;
}

void khdays_context_switch(KhdaysContext *to)
{
    KhdaysContext *from = current;
    assert(from != NULL && to != NULL);
    if (to == from) {
        return;
    }
    current = to;
    emscripten_fiber_swap(&from->fiber, &to->fiber);
    /* Whoever resumed us set current before its swap. Restore explicitly too:
     * this is local state, independent of Asyncify's rewritten Wasm stack. */
    current = from;
}

void khdays_context_delete(KhdaysContext *context)
{
    if (context == NULL) {
        return;
    }
    assert(context != current); /* context.h: caller must suspend it first */
    if (context->stack != NULL) {
        context->release(context->stack, context->size);
    }
    free(context->asyncify_stack);
    free(context);
}
