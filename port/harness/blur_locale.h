#ifndef BLUR_LOCALE_H
#define BLUR_LOCALE_H

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

/* Win32 language IDs actually consumed by Blur through GetUserDefault*.
 * Unknown or malformed values use the strict English fallback. */
static inline int blur_locale_has_language(const char *locale, char first, char second) {
    if (!locale || !locale[0] || !locale[1] ||
        (locale[0] | 0x20) != first || (locale[1] | 0x20) != second)
        return 0;
    if (!locale[2]) return 1;
    if (locale[2] != '-' && locale[2] != '_') return 0;

    const unsigned char *subtag = (const unsigned char *)locale + 3;
    unsigned length = 0;
    for (;;) {
        unsigned char byte = *subtag++;
        if ((byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') ||
            (byte >= '0' && byte <= '9')) {
            if (++length > 8) return 0;
        } else if (byte == '-' || byte == '_') {
            if (length < 2) return 0;
            length = 0;
        } else if (!byte) {
            return length >= 2;
        } else {
            return 0;
        }
    }
}

static inline unsigned blur_language_id_from_locale(const char *locale) {
    return blur_locale_has_language(locale, 'f', 'r') ? 0x040cU : 0x0409U;
}

static inline unsigned blur_language_id(void) {
    static atomic_uint cached;
    unsigned language_id = atomic_load_explicit(&cached, memory_order_acquire);
    if (language_id > 1) return language_id;

    /* BLUR_LANG is process-start configuration. Read it once and never retain
     * getenv's borrowed pointer, so later API calls are lifetime-safe and
     * concurrent calls do not race on mutable locale state. */
    unsigned expected = 0;
    if (atomic_compare_exchange_strong_explicit(
            &cached, &expected, 1, memory_order_acq_rel, memory_order_acquire)) {
        language_id = blur_language_id_from_locale(getenv("BLUR_LANG"));
        atomic_store_explicit(&cached, language_id, memory_order_release);
        return language_id;
    }

    do {
        language_id = atomic_load_explicit(&cached, memory_order_acquire);
    } while (language_id == 1);
    return language_id;
}

#endif
