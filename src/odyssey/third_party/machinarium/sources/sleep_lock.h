#ifndef MM_SLEEP_LOCK_H
#define MM_SLEEP_LOCK_H

/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

typedef unsigned int mm_sleeplock_t;

#if defined(__x86_64__) || defined(__i386) || defined(_X86_)
#define MM_SLEEPLOCK_BACKOFF __asm__("pause")
#else
#define MM_SLEEPLOCK_BACKOFF
#endif

static inline void mm_sleeplock_init(mm_sleeplock_t *lock)
{
	*lock = 0;
}

static inline void mm_sleeplock_lock(mm_sleeplock_t *lock)
{
	if (__sync_lock_test_and_set(lock, 1) != 0) {
		unsigned int spin_count = 0U;
		for (;;) {
			MM_SLEEPLOCK_BACKOFF;
			if (__sync_val_compare_and_swap(lock, 0, 1) == 0)
				break;
			if (++spin_count > 30U)
				usleep(1);
		}
	}
}

static inline void mm_sleeplock_unlock(mm_sleeplock_t *lock)
{
	__sync_lock_release(lock);
}

#endif /* MM_SLEEP_LOCK_H */
