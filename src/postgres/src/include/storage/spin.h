/*-------------------------------------------------------------------------
 *
 * spin.h
 *	   Hardware-independent implementation of spinlocks.
 *
 *
 *	The hardware-independent interface to spinlocks is defined by the
 *	typedef "slock_t" and these macros:
 *
 *	void SpinLockInit(volatile slock_t *lock)
 *		Initialize a spinlock (to the unlocked state).
 *
 *	void SpinLockAcquire(volatile slock_t *lock)
 *		Acquire a spinlock, waiting if necessary.
 *		Time out and abort() if unable to acquire the lock in a
 *		"reasonable" amount of time --- roughly 15s.
 *
 *	void SpinLockRelease(volatile slock_t *lock)
 *		Unlock a previously acquired lock.
 *
 *	bool SpinLockFree(slock_t *lock)
 *		Tests if the lock is free. Returns true if free, false if locked.
 *		This does *not* change the state of the lock.
 *
 *	Callers must beware that the macro argument may be evaluated multiple
 *	times!
 *
 *	Load and store operations in calling code are guaranteed not to be
 *	reordered with respect to these operations, because they include a
 *	compiler barrier.  (Before PostgreSQL 9.5, callers needed to use a
 *	volatile qualifier to access data protected by spinlocks.)
 *
 *	Keep in mind the coding rule that spinlocks must not be held for more
 *	than a few instructions.  In particular, we assume it is not possible
 *	for a CHECK_FOR_INTERRUPTS() to occur while holding a spinlock, and so
 *	it is not necessary to do HOLD/RESUME_INTERRUPTS() in these macros.
 *
 *	These macros are implemented in terms of hardware-dependent macros
 *	supplied by s_lock.h.  There is not currently any extra functionality
 *	added by this header, but there has been in the past and may someday
 *	be again.
 *
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/storage/spin.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef SPIN_H
#define SPIN_H

#include "miscadmin.h"
#include "storage/proc.h"
#include "storage/s_lock.h"
#ifndef HAVE_SPINLOCKS
#include "storage/pg_sema.h"
#endif

#define SpinLockInit(lock)	S_INIT_LOCK(lock)

#define SpinLockAcquire(lock) \
	do \
	{ \
		if (IsUnderPostmaster && MyProc) \
			MyProc->ybSpinLocksAcquired++; \
		S_LOCK(lock); \
	} while (0)

#define SpinLockRelease(lock) \
	do \
	{ \
		S_UNLOCK(lock); \
		if (IsUnderPostmaster && MyProc && MyProc->ybSpinLocksAcquired >= 1) \
			MyProc->ybSpinLocksAcquired--; \
	} while (0)

#define SpinLockFree(lock)	S_LOCK_FREE(lock)


extern int	SpinlockSemas(void);
extern Size SpinlockSemaSize(void);

#ifndef HAVE_SPINLOCKS
extern void SpinlockSemaInit(void);
extern PGDLLIMPORT PGSemaphore *SpinlockSemaArray;
#endif

#endif							/* SPIN_H */
