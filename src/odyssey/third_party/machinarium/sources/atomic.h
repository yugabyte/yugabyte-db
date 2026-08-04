#ifndef MM_ATOMIC_H
#define MM_ATOMIC_H

static inline uint64_t mm_atomic_u64_inc(uint64_t *ptr)
{
	return __sync_fetch_and_add(ptr, 1ULL);
}

static inline uint64_t mm_atomic_u64_dec(uint64_t *ptr)
{
	return __sync_fetch_and_sub(ptr, 1ULL);
}

static inline uint64_t mm_atomic_u64_value(uint64_t *ptr)
{
	return __sync_fetch_and_add(ptr, 0ULL);
}

static inline int mm_atomic_u64_cas(uint64_t *ptr, uint64_t old, uint64_t new)
{
	return __sync_bool_compare_and_swap(ptr, old, new);
}

static inline void mm_atomic_u64_set(uint64_t *ptr, uint64_t value)
{
	for (;;) {
		uint64_t v = mm_atomic_u64_value(ptr);

		if (mm_atomic_u64_cas(ptr, v, value)) {
			break;
		}
	}
}

#define mm_atomic_u64_once(ptr, ...)                        \
	for (;;) {                                          \
		uint64_t v = mm_atomic_u64_value((ptr));    \
		if (v) {                                    \
			break;                              \
		}                                           \
		if (mm_atomic_u64_cas((ptr), 0ULL, 1ULL)) { \
			__VA_ARGS__                         \
			break;                              \
		}                                           \
	}

#endif
