/* Separate TU: PCRE2 defines OP_* that collide with FlatL bytecode opcodes in flat.h. */
#ifdef FLAT_RE_BUNDLED
#define PCRE2_CODE_UNIT_WIDTH 8
#define HAVE_CONFIG_H
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wshorten-64-to-32"
#endif
#endif
#include "../../external/pcre2/pcre2_bundle.c"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
#else
typedef int flat_re_pcre2_placeholder;
#endif
