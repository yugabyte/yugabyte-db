#ifndef KIWI_MACRO_H
#define KIWI_MACRO_H

/*
 * kiwi.
 *
 * postgreSQL protocol interaction library.
 */

#define kiwi_likely(expr) __builtin_expect(!!(expr), 1)
#define kiwi_unlikely(expr) __builtin_expect(!!(expr), 0)

#define kiwi_container_of(ptr, type, field) \
	((type *)((char *)(ptr) - __builtin_offsetof(type, field)))

#define KIWI_API

#endif /* KIWI_MACRO_H */
