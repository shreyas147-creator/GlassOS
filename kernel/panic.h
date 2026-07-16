#ifndef PANIC_H
#define PANIC_H

void panic_impl(const char *message, const char *file, int line) __attribute__((noreturn));

#define PANIC(message) panic_impl((message), __FILE__, __LINE__)
#define ASSERT(condition) \
    do { \
        if (!(condition)) { \
            panic_impl("assertion failed: " #condition, __FILE__, __LINE__); \
        } \
    } while (0)

#endif
