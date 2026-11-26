#include "flat_locale.h"

#include <locale.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(_WIN32) && !defined(FLAT_WASM_BUILD)
static _locale_t g_c_numeric_locale;
#else
#if !defined(FLAT_WASM_BUILD)
static locale_t g_c_numeric_locale;
#endif
#endif

#if !defined(FLAT_WASM_BUILD)
static atomic_flag g_locale_lock = ATOMIC_FLAG_INIT;
static atomic_int g_locale_initialized = ATOMIC_VAR_INIT(0);

static int flat_locale_init(void) {
    if (atomic_load_explicit(&g_locale_initialized, memory_order_acquire)) {
        return g_c_numeric_locale != 0;
    }
    while (atomic_flag_test_and_set_explicit(&g_locale_lock, memory_order_acquire)) {
        /* Initialization is rare and short; wait until the immutable locale is ready. */
    }
    if (!atomic_load_explicit(&g_locale_initialized, memory_order_relaxed)) {
#if defined(_WIN32)
        g_c_numeric_locale = _create_locale(LC_NUMERIC, "C");
#else
        g_c_numeric_locale = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
#endif
        atomic_store_explicit(&g_locale_initialized, 1, memory_order_release);
    }
    atomic_flag_clear_explicit(&g_locale_lock, memory_order_release);
    return g_c_numeric_locale != 0;
}
#endif

double flat_strtod(const char *text, char **end) {
#if defined(_WIN32) && !defined(FLAT_WASM_BUILD)
    if (!flat_locale_init()) {
        if (end) *end = (char *)text;
        return 0.0;
    }
    return _strtod_l(text, end, g_c_numeric_locale);
#elif defined(FLAT_WASM_BUILD)
    return strtod(text, end); /* WASM runtime uses the C numeric locale. */
#else
    locale_t previous;
    double value;
    if (!flat_locale_init()) {
        if (end) *end = (char *)text;
        return 0.0;
    }
    previous = uselocale(g_c_numeric_locale);
    if (!previous) {
        if (end) *end = (char *)text;
        return 0.0;
    }
    value = strtod(text, end);
    (void)uselocale(previous);
    return value;
#endif
}

int flat_vsnprintf(char *dst, size_t size, const char *format, va_list args) {
#if defined(_WIN32) && !defined(FLAT_WASM_BUILD)
    int result;
    if (!flat_locale_init()) return -1;
    result = _vsnprintf_l(dst, size, format, g_c_numeric_locale, args);
    if (size) dst[size - 1] = '\0';
    return result;
#elif defined(FLAT_WASM_BUILD)
    return vsnprintf(dst, size, format, args);
#else
    locale_t previous;
    int result;
    if (!flat_locale_init()) return -1;
    previous = uselocale(g_c_numeric_locale);
    if (!previous) return -1;
    result = vsnprintf(dst, size, format, args);
    (void)uselocale(previous);
    return result;
#endif
}

int flat_snprintf(char *dst, size_t size, const char *format, ...) {
    va_list args;
    int result;
    va_start(args, format);
    result = flat_vsnprintf(dst, size, format, args);
    va_end(args);
    return result;
}
