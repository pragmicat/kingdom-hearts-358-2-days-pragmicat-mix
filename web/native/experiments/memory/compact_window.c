#include "compact_window.h"

#include <string.h>

void khdays_web_window_init(KhdaysWebWindow *window)
{
    memset(window, 0, sizeof(*window));
}

static int valid_span(uint32_t reference, uint32_t size)
{
    return reference != 0 && size != 0 && (uint64_t)reference + size <= UINT64_C(0x100000000);
}

static int reference_free(const KhdaysWebWindow *window, uint32_t reference, uint32_t size)
{
    const uint64_t end = (uint64_t)reference + size;
    for (unsigned i = 0; i < window->count; ++i) {
        const KhdaysWebRange *range = &window->ranges[i];
        if ((uint64_t)reference < (uint64_t)range->reference + range->size && range->reference < end) {
            return 0;
        }
    }
    return 1;
}

int khdays_web_window_add(KhdaysWebWindow *window, uint32_t reference, uint32_t size, void *storage)
{
    const uintptr_t start = (uintptr_t)storage;
    if (window->count == KHDAYS_WEB_MAX_RANGES || storage == NULL || !valid_span(reference, size) ||
        UINTPTR_MAX - start < size || !reference_free(window, reference, size)) {
        return 0;
    }
    for (unsigned i = 0; i < window->count; ++i) {
        const KhdaysWebRange *range = &window->ranges[i];
        const uintptr_t existing = (uintptr_t)range->host;
        if (!range->alias && start < existing + range->size && existing < start + size) {
            return 0; /* canonical reverse conversion must be unambiguous */
        }
    }
    window->ranges[window->count++] = (KhdaysWebRange){ reference, size, reference, storage, 0 };
    return 1;
}

void *khdays_web_window_pointer(const KhdaysWebWindow *window, uint32_t reference, size_t size)
{
    if (size == 0 || size > UINT32_MAX) {
        return NULL;
    }
    for (unsigned i = 0; i < window->count; ++i) {
        const KhdaysWebRange *range = &window->ranges[i];
        if (reference >= range->reference && (uint64_t)reference + size <= (uint64_t)range->reference + range->size) {
            return range->host + (reference - range->reference);
        }
    }
    return NULL;
}

int khdays_web_window_alias(KhdaysWebWindow *window, uint32_t reference, uint32_t size, uint32_t canonical)
{
    if (window->count == KHDAYS_WEB_MAX_RANGES || !valid_span(reference, size) ||
        !reference_free(window, reference, size)) {
        return 0;
    }
    /* Resolve only into canonical storage, never into another alias. */
    for (unsigned i = 0; i < window->count; ++i) {
        const KhdaysWebRange *range = &window->ranges[i];
        if (!range->alias && canonical >= range->reference &&
            (uint64_t)canonical + size <= (uint64_t)range->reference + range->size) {
            window->ranges[window->count++] = (KhdaysWebRange){ reference, size, canonical,
                range->host + (canonical - range->reference), 1 };
            return 1;
        }
    }
    return 0;
}

int khdays_web_window_remove(KhdaysWebWindow *window, uint32_t reference)
{
    for (unsigned i = 0; i < window->count; ++i) {
        const KhdaysWebRange range = window->ranges[i];
        if (range.reference == reference) {
            if (!range.alias) {
                for (unsigned j = 0; j < window->count; ++j) {
                    const KhdaysWebRange *other = &window->ranges[j];
                    if (other->alias && other->canonical >= reference &&
                        (uint64_t)other->canonical < (uint64_t)reference + range.size) {
                        return 0;
                    }
                }
            }
            window->ranges[i] = window->ranges[--window->count];
            return 1;
        }
    }
    return 0;
}

int khdays_web_window_reference(const KhdaysWebWindow *window, const void *host, uint32_t *reference)
{
    const uintptr_t address = (uintptr_t)host;
    *reference = 0;
    if (host == NULL) {
        return 1;
    }
    for (unsigned i = 0; i < window->count; ++i) {
        const KhdaysWebRange *range = &window->ranges[i];
        const uintptr_t start = (uintptr_t)range->host;
        if (!range->alias && address >= start && address - start < range->size) {
            *reference = range->reference + (uint32_t)(address - start);
            return 1;
        }
    }
    return 0;
}
