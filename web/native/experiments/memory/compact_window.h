#ifndef KHDAYS_WEB_COMPACT_WINDOW_H
#define KHDAYS_WEB_COMPACT_WINDOW_H

#include <stddef.h>
#include <stdint.h>

/* A bounded translation experiment. It is not a drop-in replacement for
 * upstream window.h: bulk accesses must explicitly ask for their whole span. */
#define KHDAYS_WEB_MAX_RANGES 256
typedef struct {
    uint32_t reference;
    uint32_t size;
    uint32_t canonical;
    unsigned char *host;
    int alias;
} KhdaysWebRange;
typedef struct {
    KhdaysWebRange ranges[KHDAYS_WEB_MAX_RANGES];
    unsigned count;
} KhdaysWebWindow;

void khdays_web_window_init(KhdaysWebWindow *window);
/* Storage ownership remains with the caller; mappings must be removed before
 * releasing it. Canonical host ranges and DS reference ranges cannot overlap. */
int khdays_web_window_add(KhdaysWebWindow *window, uint32_t reference, uint32_t size, void *storage);
/* Aliases share storage immediately. Reverse conversion deliberately returns
 * the canonical DS address, because equal host pointers cannot retain aliases. */
int khdays_web_window_alias(KhdaysWebWindow *window, uint32_t reference, uint32_t size, uint32_t canonical);
/* Refuses removal while another mapping aliases this range. */
int khdays_web_window_remove(KhdaysWebWindow *window, uint32_t reference);
/* Rejects unmapped or cross-boundary spans. size must be positive. */
void *khdays_web_window_pointer(const KhdaysWebWindow *window, uint32_t reference, size_t size);
/* NULL => 0; an address outside canonical storage => failure/0. */
int khdays_web_window_reference(const KhdaysWebWindow *window, const void *host, uint32_t *reference);

#endif
