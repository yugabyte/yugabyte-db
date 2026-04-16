#ifndef ODYSSEY_ATOMIC_H
#define ODYSSEY_ATOMIC_H

/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

typedef volatile uint32_t od_atomic_u32_t;
typedef volatile uint64_t od_atomic_u64_t;

static inline uint32_t od_atomic_u32_of(od_atomic_u32_t *atomic)
{
	return __sync_fetch_and_add(atomic, 0);
}

static inline uint32_t od_atomic_u32_inc(od_atomic_u32_t *atomic)
{
	return __sync_fetch_and_add(atomic, 1);
}

static inline uint32_t od_atomic_u32_dec(od_atomic_u32_t *atomic)
{
	return __sync_fetch_and_sub(atomic, 1);
}

static inline uint32_t od_atomic_u32_add(od_atomic_u32_t *atomic,
					 uint32_t value)
{
	return __sync_add_and_fetch(atomic, value);
}

static inline uint32_t od_atomic_u32_sub(od_atomic_u32_t *atomic,
					 uint32_t value)
{
	return __sync_sub_and_fetch(atomic, value);
}

static inline uint32_t od_atomic_u32_or(od_atomic_u32_t *atomic, uint32_t value)
{
	return __sync_or_and_fetch(atomic, value);
}

static inline uint32_t od_atomic_u32_xor(od_atomic_u32_t *atomic,
					 uint32_t value)
{
	return __sync_xor_and_fetch(atomic, value);
}

static inline uint64_t od_atomic_u64_of(od_atomic_u64_t *atomic)
{
	return __sync_fetch_and_add(atomic, 0);
}

static inline uint64_t od_atomic_u64_inc(od_atomic_u64_t *atomic)
{
	return __sync_fetch_and_add(atomic, 1);
}

static inline uint64_t od_atomic_u64_dec(od_atomic_u64_t *atomic)
{
	return __sync_fetch_and_sub(atomic, 1);
}

static inline uint64_t od_atomic_u64_add(od_atomic_u64_t *atomic,
					 uint64_t value)
{
	return __sync_add_and_fetch(atomic, value);
}

static inline uint64_t od_atomic_u64_sub(od_atomic_u64_t *atomic,
					 uint64_t value)
{
	return __sync_sub_and_fetch(atomic, value);
}

static inline uint32_t od_atomic_u32_cas(od_atomic_u32_t *atomic,
					 uint32_t compValue, uint32_t exchValue)
{
	return __sync_val_compare_and_swap(atomic, compValue, exchValue);
}

static inline uint64_t od_atomic_u64_cas(od_atomic_u64_t *atomic,
					 uint64_t compValue, uint64_t exchValue)
{
	return __sync_val_compare_and_swap(atomic, compValue, exchValue);
}

#endif /* ODYSSEY_ATOMIC_H */
