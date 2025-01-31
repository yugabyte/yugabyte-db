/*-------------------------------------------------------------------------
 *
 * sinvaladt.c
 *	  POSTGRES shared cache invalidation data manager.
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/storage/ipc/sinvaladt.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include <signal.h>
#include <unistd.h>

#include "access/transam.h"
#include "miscadmin.h"
#include "storage/backendid.h"
#include "storage/ipc.h"
#include "storage/proc.h"
#include "storage/procsignal.h"
#include "storage/shmem.h"
#include "storage/sinvaladt.h"
#include "storage/spin.h"

/* YB includes. */
#include "pg_yb_utils.h"

/*
 * Conceptually, the shared cache invalidation messages are stored in an
 * infinite array, where maxMsgNum is the next array subscript to store a
 * submitted message in, minMsgNum is the smallest array subscript containing
 * a message not yet read by all backends, and we always have maxMsgNum >=
 * minMsgNum.  (They are equal when there are no messages pending.)  For each
 * active backend, there is a nextMsgNum pointer indicating the next message it
 * needs to read; we have maxMsgNum >= nextMsgNum >= minMsgNum for every
 * backend.
 *
 * (In the current implementation, minMsgNum is a lower bound for the
 * per-process nextMsgNum values, but it isn't rigorously kept equal to the
 * smallest nextMsgNum --- it may lag behind.  We only update it when
 * SICleanupQueue is called, and we try not to do that often.)
 *
 * In reality, the messages are stored in a circular buffer of MAXNUMMESSAGES
 * entries.  We translate MsgNum values into circular-buffer indexes by
 * computing MsgNum % MAXNUMMESSAGES (this should be fast as long as
 * MAXNUMMESSAGES is a constant and a power of 2).  As long as maxMsgNum
 * doesn't exceed minMsgNum by more than MAXNUMMESSAGES, we have enough space
 * in the buffer.  If the buffer does overflow, we recover by setting the
 * "reset" flag for each backend that has fallen too far behind.  A backend
 * that is in "reset" state is ignored while determining minMsgNum.  When
 * it does finally attempt to receive inval messages, it must discard all
 * its invalidatable state, since it won't know what it missed.
 *
 * To reduce the probability of needing resets, we send a "catchup" interrupt
 * to any backend that seems to be falling unreasonably far behind.  The
 * normal behavior is that at most one such interrupt is in flight at a time;
 * when a backend completes processing a catchup interrupt, it executes
 * SICleanupQueue, which will signal the next-furthest-behind backend if
 * needed.  This avoids undue contention from multiple backends all trying
 * to catch up at once.  However, the furthest-back backend might be stuck
 * in a state where it can't catch up.  Eventually it will get reset, so it
 * won't cause any more problems for anyone but itself.  But we don't want
 * to find that a bunch of other backends are now too close to the reset
 * threshold to be saved.  So SICleanupQueue is designed to occasionally
 * send extra catchup interrupts as the queue gets fuller, to backends that
 * are far behind and haven't gotten one yet.  As long as there aren't a lot
 * of "stuck" backends, we won't need a lot of extra interrupts, since ones
 * that aren't stuck will propagate their interrupts to the next guy.
 *
 * We would have problems if the MsgNum values overflow an integer, so
 * whenever minMsgNum exceeds MSGNUMWRAPAROUND, we subtract MSGNUMWRAPAROUND
 * from all the MsgNum variables simultaneously.  MSGNUMWRAPAROUND can be
 * large so that we don't need to do this often.  It must be a multiple of
 * MAXNUMMESSAGES so that the existing circular-buffer entries don't need
 * to be moved when we do it.
 *
 * Access to the shared sinval array is protected by two locks, SInvalReadLock
 * and SInvalWriteLock.  Readers take SInvalReadLock in shared mode; this
 * authorizes them to modify their own ProcState but not to modify or even
 * look at anyone else's.  When we need to perform array-wide updates,
 * such as in SICleanupQueue, we take SInvalReadLock in exclusive mode to
 * lock out all readers.  Writers take SInvalWriteLock (always in exclusive
 * mode) to serialize adding messages to the queue.  Note that a writer
 * can operate in parallel with one or more readers, because the writer
 * has no need to touch anyone's ProcState, except in the infrequent cases
 * when SICleanupQueue is needed.  The only point of overlap is that
 * the writer wants to change maxMsgNum while readers need to read it.
 * We deal with that by having a spinlock that readers must take for just
 * long enough to read maxMsgNum, while writers take it for just long enough
 * to write maxMsgNum.  (The exact rule is that you need the spinlock to
 * read maxMsgNum if you are not holding SInvalWriteLock, and you need the
 * spinlock to write maxMsgNum unless you are holding both locks.)
 *
 * Note: since maxMsgNum is an int and hence presumably atomically readable/
 * writable, the spinlock might seem unnecessary.  The reason it is needed
 * is to provide a memory barrier: we need to be sure that messages written
 * to the array are actually there before maxMsgNum is increased, and that
 * readers will see that data after fetching maxMsgNum.  Multiprocessors
 * that have weak memory-ordering guarantees can fail without the memory
 * barrier instructions that are included in the spinlock sequences.
 */


/*
 * Configurable parameters.
 *
 * MAXNUMMESSAGES: max number of shared-inval messages we can buffer.
 * Must be a power of 2 for speed.
 *
 * MSGNUMWRAPAROUND: how often to reduce MsgNum variables to avoid overflow.
 * Must be a multiple of MAXNUMMESSAGES.  Should be large.
 *
 * CLEANUP_MIN: the minimum number of messages that must be in the buffer
 * before we bother to call SICleanupQueue.
 *
 * CLEANUP_QUANTUM: how often (in messages) to call SICleanupQueue once
 * we exceed CLEANUP_MIN.  Should be a power of 2 for speed.
 *
 * SIG_THRESHOLD: the minimum number of messages a backend must have fallen
 * behind before we'll send it PROCSIG_CATCHUP_INTERRUPT.
 *
 * WRITE_QUANTUM: the max number of messages to push into the buffer per
 * iteration of SIInsertDataEntries.  Noncritical but should be less than
 * CLEANUP_QUANTUM, because we only consider calling SICleanupQueue once
 * per iteration.
 */

#define MAXNUMMESSAGES 4096
#define MSGNUMWRAPAROUND (MAXNUMMESSAGES * 262144)
#define CLEANUP_MIN (MAXNUMMESSAGES / 2)
#define CLEANUP_QUANTUM (MAXNUMMESSAGES / 16)
#define SIG_THRESHOLD (MAXNUMMESSAGES / 2)
#define WRITE_QUANTUM 64

struct SISeg *shmInvalBuffer;	/* pointer to the shared inval buffer */

/* Per-backend state in shared invalidation structure */
typedef struct ProcState
{
	/* procPid is zero in an inactive ProcState array entry. */
	pid_t		procPid;		/* PID of backend, for signaling */
	PGPROC	   *proc;			/* PGPROC of backend */
	/* nextMsgNum is meaningless if procPid == 0 or resetState is true. */
	int			nextMsgNum;		/* next message number to read */
	bool		resetState;		/* backend needs to reset its state */
	bool		signaled;		/* backend has been sent catchup signal */
	bool		hasMessages;	/* backend has unread messages */

	/*
	 * Backend only sends invalidations, never receives them. This only makes
	 * sense for Startup process during recovery because it doesn't maintain a
	 * relcache, yet it fires inval messages to allow query backends to see
	 * schema changes.
	 */
	bool		sendOnly;		/* backend only sends, never receives */

	/*
	 * Next LocalTransactionId to use for each idle backend slot.  We keep
	 * this here because it is indexed by BackendId and it is convenient to
	 * copy the value to and from local memory when MyBackendId is set. It's
	 * meaningless in an active ProcState entry.
	 */
	LocalTransactionId nextLXID;
} ProcState;

/* Shared cache invalidation memory segment */
typedef struct SISeg
{
	/*
	 * General state information
	 */
	int			minMsgNum;		/* oldest message still needed */
	int			maxMsgNum;		/* next message number to be assigned */
	int			nextThreshold;	/* # of messages to call SICleanupQueue */
	int			lastBackend;	/* index of last active procState entry, +1 */
	int			maxBackends;	/* size of procState array */

	slock_t		msgnumLock;		/* spinlock protecting maxMsgNum */

	/*
	 * Circular buffer holding shared-inval messages
	 */
	SharedInvalidationMessage buffer[MAXNUMMESSAGES];

	/*
	 * Per-backend invalidation state info (has MaxBackends entries).
	 */
	ProcState	procState[FLEXIBLE_ARRAY_MEMBER];
} SISeg;


static LocalTransactionId nextLocalTransactionId;


/*
 * SInvalShmemSize --- return shared-memory space needed
 */
Size
SInvalShmemSize(void)
{
	Size		size;

	size = offsetof(SISeg, procState);

	/*
	 * In Hot Standby mode, the startup process requests a procState array
	 * slot using InitRecoveryTransactionEnvironment(). Even though
	 * MaxBackends doesn't account for the startup process, it is guaranteed
	 * to get a free slot. This is because the autovacuum launcher and worker
	 * processes, which are included in MaxBackends, are not started in Hot
	 * Standby mode.
	 */
	size = add_size(size, mul_size(sizeof(ProcState), MaxBackends));

	return size;
}

/*
 * CreateSharedInvalidationState
 *		Create and initialize the SI message buffer
 */
void
CreateSharedInvalidationState(void)
{
	int			i;
	bool		found;

	/* Allocate space in shared memory */
	shmInvalBuffer = (SISeg *)
		ShmemInitStruct("shmInvalBuffer", SInvalShmemSize(), &found);
	if (found)
		return;

	/* Clear message counters, save size of procState array, init spinlock */
	shmInvalBuffer->minMsgNum = 0;
	shmInvalBuffer->maxMsgNum = 0;
	shmInvalBuffer->nextThreshold = CLEANUP_MIN;
	shmInvalBuffer->lastBackend = 0;
	shmInvalBuffer->maxBackends = MaxBackends;
	SpinLockInit(&shmInvalBuffer->msgnumLock);

	/* The buffer[] array is initially all unused, so we need not fill it */

	/* Mark all backends inactive, and initialize nextLXID */
	for (i = 0; i < shmInvalBuffer->maxBackends; i++)
	{
		shmInvalBuffer->procState[i].procPid = 0;	/* inactive */
		shmInvalBuffer->procState[i].proc = NULL;
		shmInvalBuffer->procState[i].nextMsgNum = 0;	/* meaningless */
		shmInvalBuffer->procState[i].resetState = false;
		shmInvalBuffer->procState[i].signaled = false;
		shmInvalBuffer->procState[i].hasMessages = false;
		shmInvalBuffer->procState[i].nextLXID = InvalidLocalTransactionId;
	}
}

/*
 * SharedInvalBackendInit
 *		Initialize a new backend to operate on the sinval buffer
 */
void
SharedInvalBackendInit(bool sendOnly)
{
	int			index;
	ProcState  *stateP = NULL;
	SISeg	   *segP = shmInvalBuffer;

	/*
	 * This can run in parallel with read operations, but not with write
	 * operations, since SIInsertDataEntries relies on lastBackend to set
	 * hasMessages appropriately.
	 */
	LWLockAcquire(SInvalWriteLock, LW_EXCLUSIVE);

	/* Look for a free entry in the procState array */
	for (index = 0; index < segP->lastBackend; index++)
	{
		if (segP->procState[index].procPid == 0)	/* inactive slot? */
		{
			stateP = &segP->procState[index];
			break;
		}
	}

	if (stateP == NULL)
	{
		if (segP->lastBackend < segP->maxBackends)
		{
			stateP = &segP->procState[segP->lastBackend];
			Assert(stateP->procPid == 0);
			segP->lastBackend++;
		}
		else
		{
			/*
			 * out of procState slots: MaxBackends exceeded -- report normally
			 */
			MyBackendId = InvalidBackendId;
			LWLockRelease(SInvalWriteLock);
			ereport(FATAL,
					(errcode(ERRCODE_TOO_MANY_CONNECTIONS),
					 errmsg("sorry, too many clients already")));
		}
	}

	MyBackendId = (stateP - &segP->procState[0]) + 1;

	/* Advertise assigned backend ID in MyProc */
	MyProc->backendId = MyBackendId;

	/* Fetch next local transaction ID into local memory */
	nextLocalTransactionId = stateP->nextLXID;

	/* mark myself active, with all extant messages already read */
	stateP->procPid = MyProcPid;
	stateP->proc = MyProc;
	stateP->nextMsgNum = segP->maxMsgNum;
	stateP->resetState = false;
	stateP->signaled = false;
	stateP->hasMessages = false;
	stateP->sendOnly = sendOnly;

	LWLockRelease(SInvalWriteLock);

	/* register exit routine to mark my entry inactive at exit */
	on_shmem_exit(CleanupInvalidationState, PointerGetDatum(segP));

	elog(DEBUG4, "my backend ID is %d", MyBackendId);
}

/*
 * CleanupInvalidationState
 *		Mark the current backend as no longer active.
 */
static void
CleanupInvalidationStateInternal(SISeg *segP, PGPROC *proc)
{
	ProcState  *stateP;
	int			i;

	Assert(PointerIsValid(segP));

	LWLockAcquire(SInvalWriteLock, LW_EXCLUSIVE);

	stateP = &segP->procState[proc->backendId - 1];

	/* Update next local transaction ID for next holder of this backendID */
	stateP->nextLXID = nextLocalTransactionId;

	/* Mark myself inactive */
	stateP->procPid = 0;
	stateP->proc = NULL;
	stateP->nextMsgNum = 0;
	stateP->resetState = false;
	stateP->signaled = false;

	/* Recompute index of last active backend */
	for (i = segP->lastBackend; i > 0; i--)
	{
		if (segP->procState[i - 1].procPid != 0)
			break;
	}
	segP->lastBackend = i;

	LWLockRelease(SInvalWriteLock);
}

/*
 * CleanupInvalidationState
 *		Mark the current backend as no longer active.
 *
 * This function is called via on_shmem_exit() during backend shutdown.
 *
 * arg is really of type "SISeg*".
 */
void
CleanupInvalidationState(int status, Datum arg)
{
	SISeg	   *segP = (SISeg *) DatumGetPointer(arg);

	CleanupInvalidationStateInternal(segP, MyProc);
}

/*
 * CleanupInvalidationStateForProc
 *		Mark the current backend as no longer active.
 *
 * This function is called from reaper() when the parent is notified that its
 * child died unexpectedly.
 */
void
CleanupInvalidationStateForProc(PGPROC *proc)
{
	CleanupInvalidationStateInternal(shmInvalBuffer, proc);
}

/*
 * BackendIdGetProc
 *		Get the PGPROC structure for a backend, given the backend ID.
 *		The result may be out of date arbitrarily quickly, so the caller
 *		must be careful about how this information is used.  NULL is
 *		returned if the backend is not active.
 */
PGPROC *
BackendIdGetProc(int backendID)
{
	PGPROC	   *result = NULL;
	SISeg	   *segP = shmInvalBuffer;

	/* Need to lock out additions/removals of backends */
	LWLockAcquire(SInvalWriteLock, LW_SHARED);

	if (backendID > 0 && backendID <= segP->lastBackend)
	{
		ProcState  *stateP = &segP->procState[backendID - 1];

		result = stateP->proc;
	}

	LWLockRelease(SInvalWriteLock);

	return result;
}

/*
 * BackendIdGetTransactionIds
 *		Get the xid and xmin of the backend. The result may be out of date
 *		arbitrarily quickly, so the caller must be careful about how this
 *		information is used.
 */
void
BackendIdGetTransactionIds(int backendID, TransactionId *xid, TransactionId *xmin)
{
	SISeg	   *segP = shmInvalBuffer;

	*xid = InvalidTransactionId;
	*xmin = InvalidTransactionId;

	/* Need to lock out additions/removals of backends */
	LWLockAcquire(SInvalWriteLock, LW_SHARED);

	if (backendID > 0 && backendID <= segP->lastBackend)
	{
		ProcState  *stateP = &segP->procState[backendID - 1];
		PGPROC	   *proc = stateP->proc;

		if (proc != NULL)
		{
			*xid = proc->xid;
			*xmin = proc->xmin;
		}
	}

	LWLockRelease(SInvalWriteLock);
}

/*
 * SIInsertDataEntries
 *		Add new invalidation message(s) to the buffer.
 */
void
SIInsertDataEntries(const SharedInvalidationMessage *data, int n)
{
	SISeg	   *segP = shmInvalBuffer;

	/*
	 * N can be arbitrarily large.  We divide the work into groups of no more
	 * than WRITE_QUANTUM messages, to be sure that we don't hold the lock for
	 * an unreasonably long time.  (This is not so much because we care about
	 * letting in other writers, as that some just-caught-up backend might be
	 * trying to do SICleanupQueue to pass on its signal, and we don't want it
	 * to have to wait a long time.)  Also, we need to consider calling
	 * SICleanupQueue every so often.
	 */
	while (n > 0)
	{
		int			nthistime = Min(n, WRITE_QUANTUM);
		int			numMsgs;
		int			max;
		int			i;

		n -= nthistime;

		LWLockAcquire(SInvalWriteLock, LW_EXCLUSIVE);

		/*
		 * If the buffer is full, we *must* acquire some space.  Clean the
		 * queue and reset anyone who is preventing space from being freed.
		 * Otherwise, clean the queue only when it's exceeded the next
		 * fullness threshold.  We have to loop and recheck the buffer state
		 * after any call of SICleanupQueue.
		 */
		for (;;)
		{
			numMsgs = segP->maxMsgNum - segP->minMsgNum;
			if (numMsgs + nthistime > MAXNUMMESSAGES ||
				numMsgs >= segP->nextThreshold)
				SICleanupQueue(true, nthistime);
			else
				break;
		}

		/*
		 * Insert new message(s) into proper slot of circular buffer
		 */
		max = segP->maxMsgNum;
		while (nthistime-- > 0)
		{
			SharedInvalidationMessage *dest = &segP->buffer[max % MAXNUMMESSAGES];

			*dest = *data++;
			dest->yb_header.sender_pid = getpid();
			max++;
		}

		/* Update current value of maxMsgNum using spinlock */
		SpinLockAcquire(&segP->msgnumLock);
		segP->maxMsgNum = max;
		SpinLockRelease(&segP->msgnumLock);

		/*
		 * Now that the maxMsgNum change is globally visible, we give everyone
		 * a swift kick to make sure they read the newly added messages.
		 * Releasing SInvalWriteLock will enforce a full memory barrier, so
		 * these (unlocked) changes will be committed to memory before we exit
		 * the function.
		 */
		for (i = 0; i < segP->lastBackend; i++)
		{
			ProcState  *stateP = &segP->procState[i];

			stateP->hasMessages = true;
		}

		LWLockRelease(SInvalWriteLock);
	}
}

/*
 * SIGetDataEntries
 *		get next SI message(s) for current backend, if there are any
 *
 * Possible return values:
 *	0:	 no SI message available
 *	n>0: next n SI messages have been extracted into data[]
 * -1:	 SI reset message extracted
 *
 * If the return value is less than the array size "datasize", the caller
 * can assume that there are no more SI messages after the one(s) returned.
 * Otherwise, another call is needed to collect more messages.
 *
 * NB: this can run in parallel with other instances of SIGetDataEntries
 * executing on behalf of other backends, since each instance will modify only
 * fields of its own backend's ProcState, and no instance will look at fields
 * of other backends' ProcStates.  We express this by grabbing SInvalReadLock
 * in shared mode.  Note that this is not exactly the normal (read-only)
 * interpretation of a shared lock! Look closely at the interactions before
 * allowing SInvalReadLock to be grabbed in shared mode for any other reason!
 *
 * NB: this can also run in parallel with SIInsertDataEntries.  It is not
 * guaranteed that we will return any messages added after the routine is
 * entered.
 *
 * Note: we assume that "datasize" is not so large that it might be important
 * to break our hold on SInvalReadLock into segments.
 */
int
SIGetDataEntries(SharedInvalidationMessage *data, int datasize)
{
	SISeg	   *segP;
	ProcState  *stateP;
	int			max;
	int			n;

	segP = shmInvalBuffer;
	stateP = &segP->procState[MyBackendId - 1];

	/*
	 * Before starting to take locks, do a quick, unlocked test to see whether
	 * there can possibly be anything to read.  On a multiprocessor system,
	 * it's possible that this load could migrate backwards and occur before
	 * we actually enter this function, so we might miss a sinval message that
	 * was just added by some other processor.  But they can't migrate
	 * backwards over a preceding lock acquisition, so it should be OK.  If we
	 * haven't acquired a lock preventing against further relevant
	 * invalidations, any such occurrence is not much different than if the
	 * invalidation had arrived slightly later in the first place.
	 */
	if (!stateP->hasMessages)
		return 0;

	LWLockAcquire(SInvalReadLock, LW_SHARED);

	/*
	 * We must reset hasMessages before determining how many messages we're
	 * going to read.  That way, if new messages arrive after we have
	 * determined how many we're reading, the flag will get reset and we'll
	 * notice those messages part-way through.
	 *
	 * Note that, if we don't end up reading all of the messages, we had
	 * better be certain to reset this flag before exiting!
	 */
	stateP->hasMessages = false;

	/* Fetch current value of maxMsgNum using spinlock */
	SpinLockAcquire(&segP->msgnumLock);
	max = segP->maxMsgNum;
	SpinLockRelease(&segP->msgnumLock);

	if (stateP->resetState)
	{
		/*
		 * Force reset.  We can say we have dealt with any messages added
		 * since the reset, as well; and that means we should clear the
		 * signaled flag, too.
		 */
		stateP->nextMsgNum = max;
		stateP->resetState = false;
		stateP->signaled = false;
		LWLockRelease(SInvalReadLock);
		return -1;
	}

	/*
	 * Retrieve messages and advance backend's counter, until data array is
	 * full or there are no more messages.
	 *
	 * There may be other backends that haven't read the message(s), so we
	 * cannot delete them here.  SICleanupQueue() will eventually remove them
	 * from the queue.
	 */
	n = 0;
	while (n < datasize && stateP->nextMsgNum < max)
	{
		data[n++] = segP->buffer[stateP->nextMsgNum % MAXNUMMESSAGES];
		stateP->nextMsgNum++;
	}

	/*
	 * If we have caught up completely, reset our "signaled" flag so that
	 * we'll get another signal if we fall behind again.
	 *
	 * If we haven't caught up completely, reset the hasMessages flag so that
	 * we see the remaining messages next time.
	 */
	if (stateP->nextMsgNum >= max)
		stateP->signaled = false;
	else
		stateP->hasMessages = true;

	LWLockRelease(SInvalReadLock);
	return n;
}

/*
 * SICleanupQueue
 *		Remove messages that have been consumed by all active backends
 *
 * callerHasWriteLock is true if caller is holding SInvalWriteLock.
 * minFree is the minimum number of message slots to make free.
 *
 * Possible side effects of this routine include marking one or more
 * backends as "reset" in the array, and sending PROCSIG_CATCHUP_INTERRUPT
 * to some backend that seems to be getting too far behind.  We signal at
 * most one backend at a time, for reasons explained at the top of the file.
 *
 * Caution: because we transiently release write lock when we have to signal
 * some other backend, it is NOT guaranteed that there are still minFree
 * free message slots at exit.  Caller must recheck and perhaps retry.
 */
void
SICleanupQueue(bool callerHasWriteLock, int minFree)
{
	SISeg	   *segP = shmInvalBuffer;
	int			min,
				minsig,
				lowbound,
				numMsgs,
				i;
	ProcState  *needSig = NULL;

	/* Lock out all writers and readers */
	if (!callerHasWriteLock)
		LWLockAcquire(SInvalWriteLock, LW_EXCLUSIVE);
	LWLockAcquire(SInvalReadLock, LW_EXCLUSIVE);

	/*
	 * Recompute minMsgNum = minimum of all backends' nextMsgNum, identify the
	 * furthest-back backend that needs signaling (if any), and reset any
	 * backends that are too far back.  Note that because we ignore sendOnly
	 * backends here it is possible for them to keep sending messages without
	 * a problem even when they are the only active backend.
	 */
	min = segP->maxMsgNum;
	minsig = min - SIG_THRESHOLD;
	lowbound = min - MAXNUMMESSAGES + minFree;

	/*
	 * YB: We only support concurrent non-global-impact DDLs in per-database
	 * catalog version mode. Let's say this is backend A and in order to
	 * free some space it needs to discard some invalidation messages in
	 * the shared invalidation message buffer that backend B hasn't consumed
	 * yet. PG will set backend B's resetState to true. This will trigger
	 * backend B to call InvalidateSystemCaches when it tries to process its
	 * next invalidation message. InvalidateSystemCaches will call
	 * YbResetCatalogCacheVersion to reset yb_catalog_cache_version to 0,
	 * leading to "Catalog snapshot used for this transaction has been
	 * invalidated" error in backend B. To reduce such likehood, we try to
	 * avoid resetState for backend B if possible: backend B only processes
	 * an invalidation message when the sender_pid of the message matches
	 * backend B's pid.
	 */
	ProcState **yb_reset_candidates = NULL;
	int			yb_num_reset_candidates = 0;

	for (i = 0; i < segP->lastBackend; i++)
	{
		ProcState  *stateP = &segP->procState[i];
		int			n = stateP->nextMsgNum;

		/* Ignore if inactive or already in reset state */
		if (stateP->procPid == 0 || stateP->resetState || stateP->sendOnly)
			continue;

		/*
		 * If we must free some space and this backend is preventing it, force
		 * him into reset state and then ignore until he catches up.
		 */
		if (n < lowbound)
		{
			/*
			 * In YSQL per-database catalog version mode, we don't just set
			 * resetState to true for stateP. We save stateP as a potential
			 * candidate and will do a post-pass after min (the new minMsgNum)
			 * is computed.
			 */
			if (YBIsDBCatalogVersionMode())
			{
				if (!yb_reset_candidates)
				{
					Size		sz = sizeof(ProcState *) * segP->lastBackend;

					yb_reset_candidates = (ProcState **) palloc(sz);
				}
				yb_reset_candidates[yb_num_reset_candidates++] = stateP;
				continue;
			}

			stateP->resetState = true;
			/* no point in signaling him ... */
			continue;
		}

		/* Track the global minimum nextMsgNum */
		if (n < min)
			min = n;

		/* Also see who's furthest back of the unsignaled backends */
		if (n < minsig && !stateP->signaled)
		{
			minsig = n;
			needSig = stateP;
		}
	}
	if (YBIsDBCatalogVersionMode())
	{
		int			cand;
		int			next;

		for (cand = 0; cand < yb_num_reset_candidates; cand++)
		{
			ProcState  *stateP = yb_reset_candidates[cand];
			pid_t		procPid = stateP->procPid;

			/*
			 * In YSQL, the backend of stateP only applies messages sent
			 * by itself. Therefore we do not need to set stateP->resetState
			 * to true if those discarded messages that have not been consumed
			 * by the backend of stateP yet were sent by other backends.
			 */
			for (next = stateP->nextMsgNum; next < min; next++)
			{
				SharedInvalidationMessage *msg = &segP->buffer[next %
															   MAXNUMMESSAGES];

				if (msg->yb_header.sender_pid == procPid)
				{
					stateP->resetState = true;
					break;
				}
			}
			if (next == min)
				stateP->nextMsgNum = min;
		}
		if (yb_reset_candidates)
			pfree(yb_reset_candidates);
	}
	segP->minMsgNum = min;

	/*
	 * When minMsgNum gets really large, decrement all message counters so as
	 * to forestall overflow of the counters.  This happens seldom enough that
	 * folding it into the previous loop would be a loser.
	 */
	if (min >= MSGNUMWRAPAROUND)
	{
		segP->minMsgNum -= MSGNUMWRAPAROUND;
		segP->maxMsgNum -= MSGNUMWRAPAROUND;
		for (i = 0; i < segP->lastBackend; i++)
		{
			/* we don't bother skipping inactive entries here */
			segP->procState[i].nextMsgNum -= MSGNUMWRAPAROUND;
		}
	}

	/*
	 * Determine how many messages are still in the queue, and set the
	 * threshold at which we should repeat SICleanupQueue().
	 */
	numMsgs = segP->maxMsgNum - segP->minMsgNum;
	if (numMsgs < CLEANUP_MIN)
		segP->nextThreshold = CLEANUP_MIN;
	else
		segP->nextThreshold = (numMsgs / CLEANUP_QUANTUM + 1) * CLEANUP_QUANTUM;

	/*
	 * Lastly, signal anyone who needs a catchup interrupt.  Since
	 * SendProcSignal() might not be fast, we don't want to hold locks while
	 * executing it.
	 */
	if (needSig)
	{
		pid_t		his_pid = needSig->procPid;
		BackendId	his_backendId = (needSig - &segP->procState[0]) + 1;

		needSig->signaled = true;
		LWLockRelease(SInvalReadLock);
		LWLockRelease(SInvalWriteLock);
		elog(DEBUG4, "sending sinval catchup signal to PID %d", (int) his_pid);
		SendProcSignal(his_pid, PROCSIG_CATCHUP_INTERRUPT, his_backendId);
		if (callerHasWriteLock)
			LWLockAcquire(SInvalWriteLock, LW_EXCLUSIVE);
	}
	else
	{
		LWLockRelease(SInvalReadLock);
		if (!callerHasWriteLock)
			LWLockRelease(SInvalWriteLock);
	}
}


/*
 * GetNextLocalTransactionId --- allocate a new LocalTransactionId
 *
 * We split VirtualTransactionIds into two parts so that it is possible
 * to allocate a new one without any contention for shared memory, except
 * for a bit of additional overhead during backend startup/shutdown.
 * The high-order part of a VirtualTransactionId is a BackendId, and the
 * low-order part is a LocalTransactionId, which we assign from a local
 * counter.  To avoid the risk of a VirtualTransactionId being reused
 * within a short interval, successive procs occupying the same backend ID
 * slot should use a consecutive sequence of local IDs, which is implemented
 * by copying nextLocalTransactionId as seen above.
 */
LocalTransactionId
GetNextLocalTransactionId(void)
{
	LocalTransactionId result;

	/* loop to avoid returning InvalidLocalTransactionId at wraparound */
	do
	{
		result = nextLocalTransactionId++;
	} while (!LocalTransactionIdIsValid(result));

	return result;
}
