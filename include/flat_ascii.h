#ifndef FLAT_ASCII_H
#define FLAT_ASCII_H

/* FlatL source syntax is ASCII-defined and must not depend on process locale. */
static inline int flat_ascii_is_space(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static inline int flat_ascii_is_alpha(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static inline int flat_ascii_is_digit(unsigned char c) {
    return c >= '0' && c <= '9';
}

static inline int flat_ascii_is_alnum(unsigned char c) {
    return flat_ascii_is_alpha(c) || flat_ascii_is_digit(c);
}

static inline int flat_ascii_is_xdigit(unsigned char c) {
    return flat_ascii_is_digit(c) || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}

static inline int flat_ascii_tolower(unsigned char c) {
    return (c >= 'A' && c <= 'Z') ? (int)(c + ('a' - 'A')) : (int)c;
}

static inline int flat_ascii_toupper(unsigned char c) {
    return (c >= 'a' && c <= 'z') ? (int)(c - ('a' - 'A')) : (int)c;
}

#endif
