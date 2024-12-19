/*-------------------------------------------------------------------------
 *
 * xact.c
 *	  top level transaction system support routines
 *
 * See src/backend/access/transam/README for more information.
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/access/transam/xact.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include <time.h>
#include <unistd.h>

#include "access/commit_ts.h"
#include "access/multixact.h"
#include "access/parallel.h"
#include "access/subtrans.h"
#include "access/transam.h"
#include "access/twophase.h"
#include "access/xact.h"
#include "access/xlog.h"
#include "access/xloginsert.h"
#include "access/xlogrecovery.h"
#include "access/xlogutils.h"
#include "catalog/index.h"
#include "catalog/namespace.h"
#include "catalog/pg_enum.h"
#include "catalog/storage.h"
#include "commands/async.h"
#include "commands/tablecmds.h"
#include "commands/trigger.h"
#include "common/pg_prng.h"
#include "executor/spi.h"
#include "libpq/be-fsstubs.h"
#include "libpq/pqsignal.h"
#include "miscadmin.h"
#include "pg_trace.h"
#include "pgstat.h"
#include "replication/logical.h"
#include "replication/logicallauncher.h"
#include "replication/origin.h"
#include "replication/snapbuild.h"
#include "replication/syncrep.h"
#include "replication/walsender.h"
#include "storage/condition_variable.h"
#include "storage/fd.h"
#include "storage/lmgr.h"
#include "storage/md.h"
#include "storage/predicate.h"
#include "storage/proc.h"
#include "storage/procarray.h"
#include "storage/sinvaladt.h"
#include "storage/smgr.h"
#include "utils/builtins.h"
#include "utils/catcache.h"
#include "utils/combocid.h"
#include "utils/guc.h"
#include "utils/inval.h"
#include "utils/memutils.h"
#include "utils/relmapper.h"
#include "utils/snapmgr.h"
#include "utils/timeout.h"
#include "utils/timestamp.h"

#include "pg_yb_utils.h"
#include "yb/yql/pggate/ybc_pg_typedefs.h"

/*
 *	User-tweakable parameters
 */
int			DefaultXactIsoLevel = XACT_READ_COMMITTED;
int			XactIsoLevel;

bool		DefaultXactReadOnly = false;
bool		XactReadOnly;

bool		DefaultXactDeferrable = false;
bool		XactDeferrable;

int			synchronous_commit = SYNCHRONOUS_COMMIT_ON;

/*
 * CheckXidAlive is a xid value pointing to a possibly ongoing (sub)
 * transaction.  Currently, it is used in logical decoding.  It's possible
 * that such transactions can get aborted while the decoding is ongoing in
 * which case we skip decoding that particular transaction.  To ensure that we
 * check whether the CheckXidAlive is aborted after fetching the tuple from
 * system tables.  We also ensure that during logical decoding we never
 * directly access the tableam or heap APIs because we are checking for the
 * concurrent aborts only in systable_* APIs.
 */
TransactionId CheckXidAlive = InvalidTransactionId;
bool		bsysscan = false;

/*
 * When running as a parallel worker, we place only a single
 * TransactionStateData on the parallel worker's state stack, and the XID
 * reflected there will be that of the *innermost* currently-active
 * subtransaction in the backend that initiated parallelism.  However,
 * GetTopTransactionId() and TransactionIdIsCurrentTransactionId()
 * need to return the same answers in the parallel worker as they would have
 * in the user backend, so we need some additional bookkeeping.
 *
 * XactTopFullTransactionId stores the XID of our toplevel transaction, which
 * will be the same as TopTransactionStateData.fullTransactionId in an
 * ordinary backend; but in a parallel backend, which does not have the entire
 * transaction state, it will instead be copied from the backend that started
 * the parallel operation.
 *
 * nParallelCurrentXids will be 0 and ParallelCurrentXids NULL in an ordinary
 * backend, but in a parallel backend, nParallelCurrentXids will contain the
 * number of XIDs that need to be considered current, and ParallelCurrentXids
 * will contain the XIDs themselves.  This includes all XIDs that were current
 * or sub-committed in the parent at the time the parallel operation began.
 * The XIDs are stored sorted in numerical order (not logical order) to make
 * lookups as fast as possible.
 */
static FullTransactionId XactTopFullTransactionId = {InvalidTransactionId};
static int	nParallelCurrentXids = 0;
static TransactionId *ParallelCurrentXids;

/*
 * Miscellaneous flag bits to record events which occur on the top level
 * transaction. These flags are only persisted in MyXactFlags and are intended
 * so we remember to do certain things later on in the transaction. This is
 * globally accessible, so can be set from anywhere in the code that requires
 * recording flags.
 */
int			MyXactFlags;

/*
 *	transaction states - transaction state from server perspective
 */
typedef enum TransState
{
	TRANS_DEFAULT,				/* idle */
	TRANS_START,				/* transaction starting */
	TRANS_INPROGRESS,			/* inside a valid transaction */
	TRANS_COMMIT,				/* commit in progress */
	TRANS_ABORT,				/* abort in progress */
	TRANS_PREPARE				/* prepare in progress */
} TransState;

/*
 *	transaction block states - transaction state of client queries
 *
 * Note: the subtransaction states are used only for non-topmost
 * transactions; the others appear only in the topmost transaction.
 */
typedef enum TBlockState
{
	/* not-in-transaction-block states */
	TBLOCK_DEFAULT,				/* idle */
	TBLOCK_STARTED,				/* running single-query transaction */

	/* transaction block states */
	TBLOCK_BEGIN,				/* starting transaction block */
	TBLOCK_INPROGRESS,			/* live transaction */
	TBLOCK_IMPLICIT_INPROGRESS, /* live transaction after implicit BEGIN */
	TBLOCK_PARALLEL_INPROGRESS, /* live transaction inside parallel worker */
	TBLOCK_END,					/* COMMIT received */
	TBLOCK_ABORT,				/* failed xact, awaiting ROLLBACK */
	TBLOCK_ABORT_END,			/* failed xact, ROLLBACK received */
	TBLOCK_ABORT_PENDING,		/* live xact, ROLLBACK received */
	TBLOCK_PREPARE,				/* live xact, PREPARE received */

	/* subtransaction states */
	TBLOCK_SUBBEGIN,			/* starting a subtransaction */
	TBLOCK_SUBINPROGRESS,		/* live subtransaction */
	TBLOCK_SUBRELEASE,			/* RELEASE received */
	TBLOCK_SUBCOMMIT,			/* COMMIT received while TBLOCK_SUBINPROGRESS */
	TBLOCK_SUBABORT,			/* failed subxact, awaiting ROLLBACK */
	TBLOCK_SUBABORT_END,		/* failed subxact, ROLLBACK received */
	TBLOCK_SUBABORT_PENDING,	/* live subxact, ROLLBACK received */
	TBLOCK_SUBRESTART,			/* live subxact, ROLLBACK TO received */
	TBLOCK_SUBABORT_RESTART		/* failed subxact, ROLLBACK TO received */
} TBlockState;

/*
 *	transaction state structure
 */
typedef struct TransactionStateData
{
	FullTransactionId fullTransactionId;	/* my FullTransactionId */
	SubTransactionId subTransactionId;	/* my subxact ID */
	char	   *name;			/* savepoint name, if any */
	int			savepointLevel; /* savepoint level */
	TransState	state;			/* low-level state */
	TBlockState blockState;		/* high-level state */
	int			nestingLevel;	/* transaction nesting depth */
	int			gucNestLevel;	/* GUC context nesting depth */
	MemoryContext curTransactionContext;	/* my xact-lifetime context */
	ResourceOwner curTransactionOwner;	/* my query resources */
	TransactionId *childXids;	/* subcommitted child XIDs, in XID order */
	int			nChildXids;		/* # of subcommitted child XIDs */
	int			maxChildXids;	/* allocated size of childXids[] */
	Oid			prevUser;		/* previous CurrentUserId setting */
	int			prevSecContext; /* previous SecurityRestrictionContext */
	bool		prevXactReadOnly;	/* entry-time xact r/o state */
	bool		startedInRecovery;	/* did we start in recovery? */
	bool		didLogXid;		/* has xid been included in WAL record? */
	int			parallelModeLevel;	/* Enter/ExitParallelMode counter */
	bool		chain;			/* start a new block after this one */
	bool		topXidLogged;	/* for a subxact: is top-level XID logged? */
	struct TransactionStateData *parent;	/* back link to parent */

	bool		ybDataSent;		/* Whether some tuples have been transmitted to
								 * frontend as part of this execution */
	bool		ybDataSentForCurrQuery;	/* Whether any data has been sent to
										 * frontend as part of current query's
										 * execution */
	bool		isYBTxnWithPostgresRel;	/* does the current transaction
										 * operate on a postgres table? */
	List	   *YBPostponedDdlOps;	/* We postpone execution of non-revertable
									 * DocDB operations (e.g. drop table/index)
									 * until the rest of the txn succeeds */
	int			ybUncommittedStickyObjectCount;	/* Count of objects that require
												 * stickiness within a certain
												 * transaction (e.g. TEMP
												 * TABLES/WITH HOLD CURSORS) */
	bool		ybIsInternalRcSubTransaction;	/* Whether this sub transaction
												 * was started internally for
												 * READ COMMITTED isolation */
} TransactionStateData;

typedef TransactionStateData *TransactionState;

/*
 * Serialized representation used to transmit transaction state to parallel
 * workers through shared memory.
 */
typedef struct SerializedTransactionState
{
	int			xactIsoLevel;
	bool		xactDeferrable;
	FullTransactionId topFullTransactionId;
	FullTransactionId currentFullTransactionId;
	CommandId	currentCommandId;
	int			nParallelCurrentXids;
	TransactionId parallelCurrentXids[FLEXIBLE_ARRAY_MEMBER];
} SerializedTransactionState;

/* The size of SerializedTransactionState, not including the final array. */
#define SerializedTransactionStateHeaderSize \
	offsetof(SerializedTransactionState, parallelCurrentXids)

/*
 * CurrentTransactionState always points to the current transaction state
 * block.  It will point to TopTransactionStateData when not in a
 * transaction at all, or when in a top-level transaction.
 */
static TransactionStateData TopTransactionStateData = {
	.state = TRANS_DEFAULT,
	.blockState = TBLOCK_DEFAULT,
	.topXidLogged = false,
	.ybDataSent = false,
	.ybDataSentForCurrQuery = false,
	.isYBTxnWithPostgresRel = false,
	.YBPostponedDdlOps = NULL,
	.ybIsInternalRcSubTransaction = false,
};

/*
 * unreportedXids holds XIDs of all subtransactions that have not yet been
 * reported in an XLOG_XACT_ASSIGNMENT record.
 */
static int	nUnreportedXids;
static TransactionId unreportedXids[PGPROC_MAX_CACHED_SUBXIDS];

static TransactionState CurrentTransactionState = &TopTransactionStateData;

/*
 * The subtransaction ID and command ID assignment counters are global
 * to a whole transaction, so we do not keep them in the state stack.
 */
static SubTransactionId currentSubTransactionId;
static CommandId currentCommandId;
static bool currentCommandIdUsed;

/*
 * xactStartTimestamp is the value of transaction_timestamp().
 * stmtStartTimestamp is the value of statement_timestamp().
 * xactStopTimestamp is the time at which we log a commit or abort WAL record.
 * These do not change as we enter and exit subtransactions, so we don't
 * keep them inside the TransactionState stack.
 */
static TimestampTz xactStartTimestamp;
static TimestampTz stmtStartTimestamp;
static TimestampTz xactStopTimestamp;

/*
 * GID to be used for preparing the current transaction.  This is also
 * global to a whole transaction, so we don't keep it in the state stack.
 */
static char *prepareGID;

/*
 * Some commands want to force synchronous commit.
 */
static bool forceSyncCommit = false;

/* Flag for logging statements in a transaction. */
bool		xact_is_sampled = false;

/*
 * Private context for transaction-abort work --- we reserve space for this
 * at startup to ensure that AbortTransaction and AbortSubTransaction can work
 * when we've run out of memory.
 */
static MemoryContext TransactionAbortContext = NULL;

/*
 * List of add-on start- and end-of-xact callbacks
 */
typedef struct XactCallbackItem
{
	struct XactCallbackItem *next;
	XactCallback callback;
	void	   *arg;
} XactCallbackItem;

static XactCallbackItem *Xact_callbacks = NULL;

/*
 * List of add-on start- and end-of-subxact callbacks
 */
typedef struct SubXactCallbackItem
{
	struct SubXactCallbackItem *next;
	SubXactCallback callback;
	void	   *arg;
} SubXactCallbackItem;

static SubXactCallbackItem *SubXact_callbacks = NULL;

/* local function prototypes */
static void AssignTransactionId(TransactionState s);
static void AbortTransaction(void);
static void AtAbort_Memory(void);
static void AtCleanup_Memory(void);
static void AtAbort_ResourceOwner(void);
static void AtCCI_LocalCache(void);
static void AtCommit_Memory(void);
static void AtStart_Cache(void);
static void AtStart_Memory(void);
static void AtStart_ResourceOwner(void);
static void CallXactCallbacks(XactEvent event);
static void CallSubXactCallbacks(SubXactEvent event,
								 SubTransactionId mySubid,
								 SubTransactionId parentSubid);
static void CleanupTransaction(void);
static void CheckTransactionBlock(bool isTopLevel, bool throwError,
								  const char *stmtType);
static void CommitTransaction(void);
static TransactionId RecordTransactionAbort(bool isSubXact);
static void StartTransaction(void);

static void StartSubTransaction(void);
static void CommitSubTransaction(void);
static void AbortSubTransaction(void);
static void CleanupSubTransaction(void);
static void PushTransaction(void);
static void PopTransaction(void);

static void AtSubAbort_Memory(void);
static void AtSubCleanup_Memory(void);
static void AtSubAbort_ResourceOwner(void);
static void AtSubCommit_Memory(void);
static void AtSubStart_Memory(void);
static void AtSubStart_ResourceOwner(void);

static void ShowTransactionState(const char *str);
static void ShowTransactionStateRec(const char *str, TransactionState state);
static const char *BlockStateAsString(TBlockState blockState);
static const char *TransStateAsString(TransState state);


/* ----------------------------------------------------------------
 *	transaction state accessors
 * ----------------------------------------------------------------
 */

/*
 *	IsTransactionState
 *
 *	This returns true if we are inside a valid transaction; that is,
 *	it is safe to initiate database access, take heavyweight locks, etc.
 */
bool
IsTransactionState(void)
{
	TransactionState s = CurrentTransactionState;

	/*
	 * TRANS_DEFAULT and TRANS_ABORT are obviously unsafe states.  However, we
	 * also reject the startup/shutdown states TRANS_START, TRANS_COMMIT,
	 * TRANS_PREPARE since it might be too soon or too late within those
	 * transition states to do anything interesting.  Hence, the only "valid"
	 * state is TRANS_INPROGRESS.
	 */
	return (s->state == TRANS_INPROGRESS);
}

/*
 *	IsAbortedTransactionBlockState
 *
 *	This returns true if we are within an aborted transaction block.
 */
bool
IsAbortedTransactionBlockState(void)
{
	TransactionState s = CurrentTransactionState;

	if (s->blockState == TBLOCK_ABORT ||
		s->blockState == TBLOCK_SUBABORT)
		return true;

	return false;
}


/*
 *	GetTopTransactionId
 *
 * This will return the XID of the main transaction, assigning one if
 * it's not yet set.  Be careful to call this only inside a valid xact.
 */
TransactionId
GetTopTransactionId(void)
{
	if (!FullTransactionIdIsValid(XactTopFullTransactionId))
		AssignTransactionId(&TopTransactionStateData);
	return XidFromFullTransactionId(XactTopFullTransactionId);
}

/*
 *	GetTopTransactionIdIfAny
 *
 * This will return the XID of the main transaction, if one is assigned.
 * It will return InvalidTransactionId if we are not currently inside a
 * transaction, or inside a transaction that hasn't yet been assigned an XID.
 */
TransactionId
GetTopTransactionIdIfAny(void)
{
	return XidFromFullTransactionId(XactTopFullTransactionId);
}

/*
 *	GetCurrentTransactionId
 *
 * This will return the XID of the current transaction (main or sub
 * transaction), assigning one if it's not yet set.  Be careful to call this
 * only inside a valid xact.
 */
TransactionId
GetCurrentTransactionId(void)
{
	TransactionState s = CurrentTransactionState;

	if (!FullTransactionIdIsValid(s->fullTransactionId))
		AssignTransactionId(s);
	return XidFromFullTransactionId(s->fullTransactionId);
}

/*
 *	GetCurrentTransactionIdIfAny
 *
 * This will return the XID of the current sub xact, if one is assigned.
 * It will return InvalidTransactionId if we are not currently inside a
 * transaction, or inside a transaction that hasn't been assigned an XID yet.
 */
TransactionId
GetCurrentTransactionIdIfAny(void)
{
	return XidFromFullTransactionId(CurrentTransactionState->fullTransactionId);
}

/*
 *	GetTopFullTransactionId
 *
 * This will return the FullTransactionId of the main transaction, assigning
 * one if it's not yet set.  Be careful to call this only inside a valid xact.
 */
FullTransactionId
GetTopFullTransactionId(void)
{
	if (!FullTransactionIdIsValid(XactTopFullTransactionId))
		AssignTransactionId(&TopTransactionStateData);
	return XactTopFullTransactionId;
}

/*
 *	GetTopFullTransactionIdIfAny
 *
 * This will return the FullTransactionId of the main transaction, if one is
 * assigned.  It will return InvalidFullTransactionId if we are not currently
 * inside a transaction, or inside a transaction that hasn't yet been assigned
 * one.
 */
FullTransactionId
GetTopFullTransactionIdIfAny(void)
{
	return XactTopFullTransactionId;
}

/*
 *	GetCurrentFullTransactionId
 *
 * This will return the FullTransactionId of the current transaction (main or
 * sub transaction), assigning one if it's not yet set.  Be careful to call
 * this only inside a valid xact.
 */
FullTransactionId
GetCurrentFullTransactionId(void)
{
	TransactionState s = CurrentTransactionState;

	if (!FullTransactionIdIsValid(s->fullTransactionId))
		AssignTransactionId(s);
	return s->fullTransactionId;
}

/*
 *	GetCurrentFullTransactionIdIfAny
 *
 * This will return the FullTransactionId of the current sub xact, if one is
 * assigned.  It will return InvalidFullTransactionId if we are not currently
 * inside a transaction, or inside a transaction that hasn't been assigned one
 * yet.
 */
FullTransactionId
GetCurrentFullTransactionIdIfAny(void)
{
	return CurrentTransactionState->fullTransactionId;
}

/*
 *	MarkCurrentTransactionIdLoggedIfAny
 *
 * Remember that the current xid - if it is assigned - now has been wal logged.
 */
void
MarkCurrentTransactionIdLoggedIfAny(void)
{
	if (FullTransactionIdIsValid(CurrentTransactionState->fullTransactionId))
		CurrentTransactionState->didLogXid = true;
}

/*
 * IsSubxactTopXidLogPending
 *
 * This is used to decide whether we need to WAL log the top-level XID for
 * operation in a subtransaction.  We require that for logical decoding, see
 * LogicalDecodingProcessRecord.
 *
 * This returns true if wal_level >= logical and we are inside a valid
 * subtransaction, for which the assignment was not yet written to any WAL
 * record.
 */
bool
IsSubxactTopXidLogPending(void)
{
	/* check whether it is already logged */
	if (CurrentTransactionState->topXidLogged)
		return false;

	/* wal_level has to be logical */
	if (!XLogLogicalInfoActive())
		return false;

	/* we need to be in a transaction state */
	if (!IsTransactionState())
		return false;

	/* it has to be a subtransaction */
	if (!IsSubTransaction())
		return false;

	/* the subtransaction has to have a XID assigned */
	if (!TransactionIdIsValid(GetCurrentTransactionIdIfAny()))
		return false;

	return true;
}

/*
 * MarkSubxactTopXidLogged
 *
 * Remember that the top transaction id for the current subtransaction is WAL
 * logged now.
 */
void
MarkSubxactTopXidLogged(void)
{
	Assert(IsSubxactTopXidLogPending());

	CurrentTransactionState->topXidLogged = true;
}

/*
 *	GetStableLatestTransactionId
 *
 * Get the transaction's XID if it has one, else read the next-to-be-assigned
 * XID.  Once we have a value, return that same value for the remainder of the
 * current transaction.  This is meant to provide the reference point for the
 * age(xid) function, but might be useful for other maintenance tasks as well.
 */
TransactionId
GetStableLatestTransactionId(void)
{
	static LocalTransactionId lxid = InvalidLocalTransactionId;
	static TransactionId stablexid = InvalidTransactionId;

	if (lxid != MyProc->lxid)
	{
		lxid = MyProc->lxid;
		stablexid = GetTopTransactionIdIfAny();
		if (!TransactionIdIsValid(stablexid))
			stablexid = ReadNextTransactionId();
	}

	Assert(TransactionIdIsValid(stablexid));

	return stablexid;
}

/*
 * AssignTransactionId
 *
 * Assigns a new permanent FullTransactionId to the given TransactionState.
 * We do not assign XIDs to transactions until/unless this is called.
 * Also, any parent TransactionStates that don't yet have XIDs are assigned
 * one; this maintains the invariant that a child transaction has an XID
 * following its parent's.
 */
static void
AssignTransactionId(TransactionState s)
{
	bool		isSubXact = (s->parent != NULL);
	ResourceOwner currentOwner;
	bool		log_unknown_top = false;

	/* Assert that caller didn't screw up */
	Assert(!FullTransactionIdIsValid(s->fullTransactionId));
	Assert(s->state == TRANS_INPROGRESS);

	/*
	 * Workers synchronize transaction state at the beginning of each parallel
	 * operation, so we can't account for new XIDs at this point.
	 */
	if (IsInParallelMode() || IsParallelWorker())
		elog(ERROR, "cannot assign XIDs during a parallel operation");

	/*
	 * Ensure parent(s) have XIDs, so that a child always has an XID later
	 * than its parent.  Mustn't recurse here, or we might get a stack
	 * overflow if we're at the bottom of a huge stack of subtransactions none
	 * of which have XIDs yet.
	 */
	if (isSubXact && !FullTransactionIdIsValid(s->parent->fullTransactionId))
	{
		TransactionState p = s->parent;
		TransactionState *parents;
		size_t		parentOffset = 0;

		parents = palloc(sizeof(TransactionState) * s->nestingLevel);
		while (p != NULL && !FullTransactionIdIsValid(p->fullTransactionId))
		{
			parents[parentOffset++] = p;
			p = p->parent;
		}

		/*
		 * This is technically a recursive call, but the recursion will never
		 * be more than one layer deep.
		 */
		while (parentOffset != 0)
			AssignTransactionId(parents[--parentOffset]);

		pfree(parents);
	}

	/*
	 * When wal_level=logical, guarantee that a subtransaction's xid can only
	 * be seen in the WAL stream if its toplevel xid has been logged before.
	 * If necessary we log an xact_assignment record with fewer than
	 * PGPROC_MAX_CACHED_SUBXIDS. Note that it is fine if didLogXid isn't set
	 * for a transaction even though it appears in a WAL record, we just might
	 * superfluously log something. That can happen when an xid is included
	 * somewhere inside a wal record, but not in XLogRecord->xl_xid, like in
	 * xl_standby_locks.
	 */
	if (isSubXact && XLogLogicalInfoActive() &&
		!TopTransactionStateData.didLogXid)
		log_unknown_top = true;

	/*
	 * Generate a new FullTransactionId and record its xid in PG_PROC and
	 * pg_subtrans.
	 *
	 * NB: we must make the subtrans entry BEFORE the Xid appears anywhere in
	 * shared storage other than PG_PROC; because if there's no room for it in
	 * PG_PROC, the subtrans entry is needed to ensure that other backends see
	 * the Xid as "running".  See GetNewTransactionId.
	 */
	s->fullTransactionId = GetNewTransactionId(isSubXact);
	if (!isSubXact)
		XactTopFullTransactionId = s->fullTransactionId;

	if (isSubXact)
		SubTransSetParent(XidFromFullTransactionId(s->fullTransactionId),
						  XidFromFullTransactionId(s->parent->fullTransactionId));

	/*
	 * If it's a top-level transaction, the predicate locking system needs to
	 * be told about it too.
	 */
	if (!isSubXact)
		RegisterPredicateLockingXid(XidFromFullTransactionId(s->fullTransactionId));

	/*
	 * Acquire lock on the transaction XID.  (We assume this cannot block.) We
	 * have to ensure that the lock is assigned to the transaction's own
	 * ResourceOwner.
	 */
	currentOwner = CurrentResourceOwner;
	CurrentResourceOwner = s->curTransactionOwner;

	XactLockTableInsert(XidFromFullTransactionId(s->fullTransactionId));

	CurrentResourceOwner = currentOwner;

	/*
	 * Every PGPROC_MAX_CACHED_SUBXIDS assigned transaction ids within each
	 * top-level transaction we issue a WAL record for the assignment. We
	 * include the top-level xid and all the subxids that have not yet been
	 * reported using XLOG_XACT_ASSIGNMENT records.
	 *
	 * This is required to limit the amount of shared memory required in a hot
	 * standby server to keep track of in-progress XIDs. See notes for
	 * RecordKnownAssignedTransactionIds().
	 *
	 * We don't keep track of the immediate parent of each subxid, only the
	 * top-level transaction that each subxact belongs to. This is correct in
	 * recovery only because aborted subtransactions are separately WAL
	 * logged.
	 *
	 * This is correct even for the case where several levels above us didn't
	 * have an xid assigned as we recursed up to them beforehand.
	 */
	if (isSubXact && XLogStandbyInfoActive())
	{
		unreportedXids[nUnreportedXids] = XidFromFullTransactionId(s->fullTransactionId);
		nUnreportedXids++;

		/*
		 * ensure this test matches similar one in
		 * RecoverPreparedTransactions()
		 */
		if (nUnreportedXids >= PGPROC_MAX_CACHED_SUBXIDS ||
			log_unknown_top)
		{
			xl_xact_assignment xlrec;

			/*
			 * xtop is always set by now because we recurse up transaction
			 * stack to the highest unassigned xid and then come back down
			 */
			xlrec.xtop = GetTopTransactionId();
			Assert(TransactionIdIsValid(xlrec.xtop));
			xlrec.nsubxacts = nUnreportedXids;

			XLogBeginInsert();
			XLogRegisterData((char *) &xlrec, MinSizeOfXactAssignment);
			XLogRegisterData((char *) unreportedXids,
							 nUnreportedXids * sizeof(TransactionId));

			(void) XLogInsert(RM_XACT_ID, XLOG_XACT_ASSIGNMENT);

			nUnreportedXids = 0;
			/* mark top, not current xact as having been logged */
			TopTransactionStateData.didLogXid = true;
		}
	}
}

/*
 *	GetCurrentSubTransactionId
 */
SubTransactionId
GetCurrentSubTransactionId(void)
{
	TransactionState s = CurrentTransactionState;

	return s->subTransactionId;
}

/*
 *	SubTransactionIsActive
 *
 * Test if the specified subxact ID is still active.  Note caller is
 * responsible for checking whether this ID is relevant to the current xact.
 */
bool
SubTransactionIsActive(SubTransactionId subxid)
{
	TransactionState s;

	for (s = CurrentTransactionState; s != NULL; s = s->parent)
	{
		if (s->state == TRANS_ABORT)
			continue;
		if (s->subTransactionId == subxid)
			return true;
	}
	return false;
}


/*
 *	GetCurrentCommandId
 *
 * "used" must be true if the caller intends to use the command ID to mark
 * inserted/updated/deleted tuples.  false means the ID is being fetched
 * for read-only purposes (ie, as a snapshot validity cutoff).  See
 * CommandCounterIncrement() for discussion.
 */
CommandId
GetCurrentCommandId(bool used)
{
	/* this is global to a transaction, not subtransaction-local */
	if (used)
	{
		/*
		 * Forbid setting currentCommandIdUsed in a parallel worker, because
		 * we have no provision for communicating this back to the leader.  We
		 * could relax this restriction when currentCommandIdUsed was already
		 * true at the start of the parallel operation.
		 */
		Assert(!IsParallelWorker());
		currentCommandIdUsed = true;
	}
	return currentCommandId;
}

/*
 *	SetParallelStartTimestamps
 *
 * In a parallel worker, we should inherit the parent transaction's
 * timestamps rather than setting our own.  The parallel worker
 * infrastructure must call this to provide those values before
 * calling StartTransaction() or SetCurrentStatementStartTimestamp().
 */
void
SetParallelStartTimestamps(TimestampTz xact_ts, TimestampTz stmt_ts)
{
	Assert(IsParallelWorker());
	xactStartTimestamp = xact_ts;
	stmtStartTimestamp = stmt_ts;
}

/*
 *	GetCurrentTransactionStartTimestamp
 */
TimestampTz
GetCurrentTransactionStartTimestamp(void)
{
	return xactStartTimestamp;
}

/*
 *	GetCurrentStatementStartTimestamp
 */
TimestampTz
GetCurrentStatementStartTimestamp(void)
{
	return stmtStartTimestamp;
}

/*
 *	GetCurrentTransactionStopTimestamp
 *
 * We return current time if the transaction stop time hasn't been set
 * (which can happen if we decide we don't need to log an XLOG record).
 */
TimestampTz
GetCurrentTransactionStopTimestamp(void)
{
	if (xactStopTimestamp != 0)
		return xactStopTimestamp;
	return GetCurrentTimestamp();
}

/*
 *	SetCurrentStatementStartTimestamp
 *
 * In a parallel worker, this should already have been provided by a call
 * to SetParallelStartTimestamps().
 */
void
SetCurrentStatementStartTimestamp(void)
{
	if (!IsParallelWorker())
		stmtStartTimestamp = GetCurrentTimestamp();
	else
		Assert(stmtStartTimestamp != 0);
}

/*
 *	SetCurrentTransactionStopTimestamp
 */
static inline void
SetCurrentTransactionStopTimestamp(void)
{
	xactStopTimestamp = GetCurrentTimestamp();
}

/*
 *	GetCurrentTransactionNestLevel
 *
 * Note: this will return zero when not inside any transaction, one when
 * inside a top-level transaction, etc.
 */
int
GetCurrentTransactionNestLevel(void)
{
	TransactionState s = CurrentTransactionState;

	return s->nestingLevel;
}

const char *
GetCurrentTransactionName(void)
{
	TransactionState s = CurrentTransactionState;

	return s->name;
}


/*
 *	TransactionIdIsCurrentTransactionId
 */
bool
TransactionIdIsCurrentTransactionId(TransactionId xid)
{
	TransactionState s;

	/*
	 * We always say that BootstrapTransactionId is "not my transaction ID"
	 * even when it is (ie, during bootstrap).  Along with the fact that
	 * transam.c always treats BootstrapTransactionId as already committed,
	 * this causes the heapam_visibility.c routines to see all tuples as
	 * committed, which is what we need during bootstrap.  (Bootstrap mode
	 * only inserts tuples, it never updates or deletes them, so all tuples
	 * can be presumed good immediately.)
	 *
	 * Likewise, InvalidTransactionId and FrozenTransactionId are certainly
	 * not my transaction ID, so we can just return "false" immediately for
	 * any non-normal XID.
	 */
	if (!TransactionIdIsNormal(xid))
		return false;

	if (TransactionIdEquals(xid, GetTopTransactionIdIfAny()))
		return true;

	/*
	 * In parallel workers, the XIDs we must consider as current are stored in
	 * ParallelCurrentXids rather than the transaction-state stack.  Note that
	 * the XIDs in this array are sorted numerically rather than according to
	 * transactionIdPrecedes order.
	 */
	if (nParallelCurrentXids > 0)
	{
		int			low,
					high;

		low = 0;
		high = nParallelCurrentXids - 1;
		while (low <= high)
		{
			int			middle;
			TransactionId probe;

			middle = low + (high - low) / 2;
			probe = ParallelCurrentXids[middle];
			if (probe == xid)
				return true;
			else if (probe < xid)
				low = middle + 1;
			else
				high = middle - 1;
		}
		return false;
	}

	/*
	 * We will return true for the Xid of the current subtransaction, any of
	 * its subcommitted children, any of its parents, or any of their
	 * previously subcommitted children.  However, a transaction being aborted
	 * is no longer "current", even though it may still have an entry on the
	 * state stack.
	 */
	for (s = CurrentTransactionState; s != NULL; s = s->parent)
	{
		int			low,
					high;

		if (s->state == TRANS_ABORT)
			continue;
		if (!FullTransactionIdIsValid(s->fullTransactionId))
			continue;			/* it can't have any child XIDs either */
		if (TransactionIdEquals(xid, XidFromFullTransactionId(s->fullTransactionId)))
			return true;
		/* As the childXids array is ordered, we can use binary search */
		low = 0;
		high = s->nChildXids - 1;
		while (low <= high)
		{
			int			middle;
			TransactionId probe;

			middle = low + (high - low) / 2;
			probe = s->childXids[middle];
			if (TransactionIdEquals(probe, xid))
				return true;
			else if (TransactionIdPrecedes(probe, xid))
				low = middle + 1;
			else
				high = middle - 1;
		}
	}

	return false;
}

/*
 *	TransactionStartedDuringRecovery
 *
 * Returns true if the current transaction started while recovery was still
 * in progress. Recovery might have ended since so RecoveryInProgress() might
 * return false already.
 */
bool
TransactionStartedDuringRecovery(void)
{
	return CurrentTransactionState->startedInRecovery;
}

/*
 *	EnterParallelMode
 */
void
EnterParallelMode(void)
{
	TransactionState s = CurrentTransactionState;

	Assert(s->parallelModeLevel >= 0);

	++s->parallelModeLevel;
}

/*
 *	ExitParallelMode
 */
void
ExitParallelMode(void)
{
	TransactionState s = CurrentTransactionState;

	Assert(s->parallelModeLevel > 0);
	Assert(s->parallelModeLevel > 1 || !ParallelContextActive());

	--s->parallelModeLevel;
}

/*
 *	IsInParallelMode
 *
 * Are we in a parallel operation, as either the leader or a worker?  Check
 * this to prohibit operations that change backend-local state expected to
 * match across all workers.  Mere caches usually don't require such a
 * restriction.  State modified in a strict push/pop fashion, such as the
 * active snapshot stack, is often fine.
 */
bool
IsInParallelMode(void)
{
	return CurrentTransactionState->parallelModeLevel != 0;
}

/*
 *	CommandCounterIncrement
 */
void
CommandCounterIncrement(void)
{
	/*
	 * If the current value of the command counter hasn't been "used" to mark
	 * tuples, we need not increment it, since there's no need to distinguish
	 * a read-only command from others.  This helps postpone command counter
	 * overflow, and keeps no-op CommandCounterIncrement operations cheap.
	 */
	if (currentCommandIdUsed)
	{
		/*
		 * Workers synchronize transaction state at the beginning of each
		 * parallel operation, so we can't account for new commands after that
		 * point.
		 */
		if (IsInParallelMode() || IsParallelWorker())
			elog(ERROR, "cannot start commands during a parallel operation");

		currentCommandId += 1;
		if (currentCommandId == InvalidCommandId)
		{
			currentCommandId -= 1;
			ereport(ERROR,
					(errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
					 errmsg("cannot have more than 2^32-2 commands in a transaction")));
		}
		currentCommandIdUsed = false;

		/* Propagate new command ID into static snapshots */
		SnapshotSetCommandId(currentCommandId);

		/*
		 * Make any catalog changes done by the just-completed command visible
		 * in the local syscache.  We obviously don't need to do this after a
		 * read-only command.  (But see hacks in inval.c to make real sure we
		 * don't think a command that queued inval messages was read-only.)
		 */
		AtCCI_LocalCache();
	}
}

/*
 * ForceSyncCommit
 *
 * Interface routine to allow commands to force a synchronous commit of the
 * current top-level transaction.  Currently, two-phase commit does not
 * persist and restore this variable.  So long as all callers use
 * PreventInTransactionBlock(), that omission has no consequences.
 */
void
ForceSyncCommit(void)
{
	forceSyncCommit = true;
}

/*
 * Mark current transaction as having sent some data back to the client.
 * This prevents automatic transaction restart.
 */
void
YBMarkDataSent(void)
{
	TransactionState s = CurrentTransactionState;
	s->ybDataSent = true;
	s->ybDataSentForCurrQuery = true;
}

/*
 * Mark current transaction as having no data sent to the client.
 */
void
YBMarkDataNotSent(void)
{
	TransactionState s = CurrentTransactionState;
	s->ybDataSent = false;
}

void
YBMarkDataNotSentForCurrQuery(void)
{
	TransactionState s = CurrentTransactionState;
	s->ybDataSentForCurrQuery = false;
}

/*
 * Whether some data has been transmitted to frontend as part of this transaction.
 */
bool
YBIsDataSent(void)
{
	/* Note: we don't support nested transactions (savepoints) yet,
	 * but once we do - we have to make sure this works as intended. */
	TransactionState s = CurrentTransactionState;
	/* Ignoring "idle" transaction state, a leftover from a previous
	 * transaction */
	return s->blockState != TBLOCK_DEFAULT && s->ybDataSent;
}

/*
 * Whether some data has been transmitted to frontend as part of this query.
 */
bool
YBIsDataSentForCurrQuery(void)
{
	TransactionState s = CurrentTransactionState;
	return s->ybDataSentForCurrQuery;
}

/* ----------------------------------------------------------------
 *						StartTransaction stuff
 * ----------------------------------------------------------------
 */

/*
 *	AtStart_Cache
 */
static void
AtStart_Cache(void)
{
	AcceptInvalidationMessages();
}

/*
 *	AtStart_Memory
 */
static void
AtStart_Memory(void)
{
	TransactionState s = CurrentTransactionState;

	/*
	 * If this is the first time through, create a private context for
	 * AbortTransaction to work in.  By reserving some space now, we can
	 * insulate AbortTransaction from out-of-memory scenarios.  Like
	 * ErrorContext, we set it up with slow growth rate and a nonzero minimum
	 * size, so that space will be reserved immediately.
	 */
	if (TransactionAbortContext == NULL)
		TransactionAbortContext =
			AllocSetContextCreate(TopMemoryContext,
								  "TransactionAbortContext",
								  32 * 1024,
								  32 * 1024,
								  32 * 1024);

	/*
	 * We shouldn't have a transaction context already.
	 */
	Assert(TopTransactionContext == NULL);

	/*
	 * Create a toplevel context for the transaction.
	 */
	TopTransactionContext =
		AllocSetContextCreate(TopMemoryContext,
							  "TopTransactionContext",
							  ALLOCSET_DEFAULT_SIZES);

	/*
	 * In a top-level transaction, CurTransactionContext is the same as
	 * TopTransactionContext.
	 */
	CurTransactionContext = TopTransactionContext;
	s->curTransactionContext = CurTransactionContext;

	/* Make the CurTransactionContext active. */
	MemoryContextSwitchTo(CurTransactionContext);
}

/*
 *	AtStart_ResourceOwner
 */
static void
AtStart_ResourceOwner(void)
{
	TransactionState s = CurrentTransactionState;

	/*
	 * We shouldn't have a transaction resource owner already.
	 */
	Assert(TopTransactionResourceOwner == NULL);

	/*
	 * Create a toplevel resource owner for the transaction.
	 */
	s->curTransactionOwner = ResourceOwnerCreate(NULL, "TopTransaction");

	TopTransactionResourceOwner = s->curTransactionOwner;
	CurTransactionResourceOwner = s->curTransactionOwner;
	CurrentResourceOwner = s->curTransactionOwner;
}

/* ----------------------------------------------------------------
 *						StartSubTransaction stuff
 * ----------------------------------------------------------------
 */

/*
 * AtSubStart_Memory
 */
static void
AtSubStart_Memory(void)
{
	TransactionState s = CurrentTransactionState;

	Assert(CurTransactionContext != NULL);

	/*
	 * Create a CurTransactionContext, which will be used to hold data that
	 * survives subtransaction commit but disappears on subtransaction abort.
	 * We make it a child of the immediate parent's CurTransactionContext.
	 */
	CurTransactionContext = AllocSetContextCreate(CurTransactionContext,
												  "CurTransactionContext",
												  ALLOCSET_DEFAULT_SIZES);
	s->curTransactionContext = CurTransactionContext;

	/* Make the CurTransactionContext active. */
	MemoryContextSwitchTo(CurTransactionContext);
}

/*
 * AtSubStart_ResourceOwner
 */
static void
AtSubStart_ResourceOwner(void)
{
	TransactionState s = CurrentTransactionState;

	Assert(s->parent != NULL);

	/*
	 * Create a resource owner for the subtransaction.  We make it a child of
	 * the immediate parent's resource owner.
	 */
	s->curTransactionOwner =
		ResourceOwnerCreate(s->parent->curTransactionOwner,
							"SubTransaction");

	CurTransactionResourceOwner = s->curTransactionOwner;
	CurrentResourceOwner = s->curTransactionOwner;
}

/* ----------------------------------------------------------------
 *						CommitTransaction stuff
 * ----------------------------------------------------------------
 */

/*
 *	RecordTransactionCommit
 *
 * Returns latest XID among xact and its children, or InvalidTransactionId
 * if the xact has no XID.  (We compute that here just because it's easier.)
 *
 * If you change this function, see RecordTransactionCommitPrepared also.
 */
static TransactionId
RecordTransactionCommit(void)
{
	TransactionId xid = GetTopTransactionIdIfAny();
	bool		markXidCommitted = TransactionIdIsValid(xid);
	TransactionId latestXid = InvalidTransactionId;
	int			nrels;
	RelFileNode *rels;
	int			nchildren;
	TransactionId *children;
	int			ndroppedstats = 0;
	xl_xact_stats_item *droppedstats = NULL;
	int			nmsgs = 0;
	SharedInvalidationMessage *invalMessages = NULL;
	bool		RelcacheInitFileInval = false;
	bool		wrote_xlog;

	if (IsYugaByteEnabled() && !IsCurrentTxnWithPGRel())
	{
		return latestXid;
	}

	/*
	 * Log pending invalidations for logical decoding of in-progress
	 * transactions.  Normally for DDLs, we log this at each command end,
	 * however, for certain cases where we directly update the system table
	 * without a transaction block, the invalidations are not logged till this
	 * time.
	 */
	if (XLogLogicalInfoActive())
		LogLogicalInvalidations();

	/* Get data needed for commit record */
	nrels = smgrGetPendingDeletes(true, &rels);
	nchildren = xactGetCommittedChildren(&children);
	ndroppedstats = pgstat_get_transactional_drops(true, &droppedstats);
	if (XLogStandbyInfoActive())
		nmsgs = xactGetCommittedInvalidationMessages(&invalMessages,
													 &RelcacheInitFileInval);
	wrote_xlog = (XactLastRecEnd != 0);

	/*
	 * If we haven't been assigned an XID yet, we neither can, nor do we want
	 * to write a COMMIT record.
	 */
	if (!markXidCommitted)
	{
		/*
		 * We expect that every RelationDropStorage is followed by a catalog
		 * update, and hence XID assignment, so we shouldn't get here with any
		 * pending deletes. Same is true for dropping stats.
		 *
		 * Use a real test not just an Assert to check this, since it's a bit
		 * fragile.
		 */
		if (nrels != 0 || ndroppedstats != 0)
			elog(ERROR, "cannot commit a transaction that deleted files but has no xid");

		/* Can't have child XIDs either; AssignTransactionId enforces this */
		Assert(nchildren == 0);

		/*
		 * Transactions without an assigned xid can contain invalidation
		 * messages (e.g. explicit relcache invalidations or catcache
		 * invalidations for inplace updates); standbys need to process those.
		 * We can't emit a commit record without an xid, and we don't want to
		 * force assigning an xid, because that'd be problematic for e.g.
		 * vacuum.  Hence we emit a bespoke record for the invalidations. We
		 * don't want to use that in case a commit record is emitted, so they
		 * happen synchronously with commits (besides not wanting to emit more
		 * WAL records).
		 */
		if (nmsgs != 0)
		{
			LogStandbyInvalidations(nmsgs, invalMessages,
									RelcacheInitFileInval);
			wrote_xlog = true;	/* not strictly necessary */
		}

		/*
		 * If we didn't create XLOG entries, we're done here; otherwise we
		 * should trigger flushing those entries the same as a commit record
		 * would.  This will primarily happen for HOT pruning and the like; we
		 * want these to be flushed to disk in due time.
		 */
		if (!wrote_xlog)
			goto cleanup;
	}
	else
	{
		bool		replorigin;

		/*
		 * Are we using the replication origins feature?  Or, in other words,
		 * are we replaying remote actions?
		 */
		replorigin = (replorigin_session_origin != InvalidRepOriginId &&
					  replorigin_session_origin != DoNotReplicateId);

		/*
		 * Begin commit critical section and insert the commit XLOG record.
		 */
		/* Tell bufmgr and smgr to prepare for commit */
		BufmgrCommit();

		/*
		 * Mark ourselves as within our "commit critical section".  This
		 * forces any concurrent checkpoint to wait until we've updated
		 * pg_xact.  Without this, it is possible for the checkpoint to set
		 * REDO after the XLOG record but fail to flush the pg_xact update to
		 * disk, leading to loss of the transaction commit if the system
		 * crashes a little later.
		 *
		 * Note: we could, but don't bother to, set this flag in
		 * RecordTransactionAbort.  That's because loss of a transaction abort
		 * is noncritical; the presumption would be that it aborted, anyway.
		 *
		 * It's safe to change the delayChkptFlags flag of our own backend
		 * without holding the ProcArrayLock, since we're the only one
		 * modifying it.  This makes checkpoint's determination of which xacts
		 * are delaying the checkpoint a bit fuzzy, but it doesn't matter.
		 */
		Assert((MyProc->delayChkptFlags & DELAY_CHKPT_START) == 0);
		START_CRIT_SECTION();
		MyProc->delayChkptFlags |= DELAY_CHKPT_START;

		SetCurrentTransactionStopTimestamp();

		XactLogCommitRecord(xactStopTimestamp,
							nchildren, children, nrels, rels,
							ndroppedstats, droppedstats,
							nmsgs, invalMessages,
							RelcacheInitFileInval,
							MyXactFlags,
							InvalidTransactionId, NULL /* plain commit */ );

		if (replorigin)
			/* Move LSNs forward for this replication origin */
			replorigin_session_advance(replorigin_session_origin_lsn,
									   XactLastRecEnd);

		/*
		 * Record commit timestamp.  The value comes from plain commit
		 * timestamp if there's no replication origin; otherwise, the
		 * timestamp was already set in replorigin_session_origin_timestamp by
		 * replication.
		 *
		 * We don't need to WAL-log anything here, as the commit record
		 * written above already contains the data.
		 */

		if (!replorigin || replorigin_session_origin_timestamp == 0)
			replorigin_session_origin_timestamp = xactStopTimestamp;

		TransactionTreeSetCommitTsData(xid, nchildren, children,
									   replorigin_session_origin_timestamp,
									   replorigin_session_origin);
	}

	/*
	 * Check if we want to commit asynchronously.  We can allow the XLOG flush
	 * to happen asynchronously if synchronous_commit=off, or if the current
	 * transaction has not performed any WAL-logged operation or didn't assign
	 * an xid.  The transaction can end up not writing any WAL, even if it has
	 * an xid, if it only wrote to temporary and/or unlogged tables.  It can
	 * end up having written WAL without an xid if it did HOT pruning.  In
	 * case of a crash, the loss of such a transaction will be irrelevant;
	 * temp tables will be lost anyway, unlogged tables will be truncated and
	 * HOT pruning will be done again later. (Given the foregoing, you might
	 * think that it would be unnecessary to emit the XLOG record at all in
	 * this case, but we don't currently try to do that.  It would certainly
	 * cause problems at least in Hot Standby mode, where the
	 * KnownAssignedXids machinery requires tracking every XID assignment.  It
	 * might be OK to skip it only when wal_level < replica, but for now we
	 * don't.)
	 *
	 * However, if we're doing cleanup of any non-temp rels or committing any
	 * command that wanted to force sync commit, then we must flush XLOG
	 * immediately.  (We must not allow asynchronous commit if there are any
	 * non-temp tables to be deleted, because we might delete the files before
	 * the COMMIT record is flushed to disk.  We do allow asynchronous commit
	 * if all to-be-deleted tables are temporary though, since they are lost
	 * anyway if we crash.)
	 */
	if ((wrote_xlog && markXidCommitted &&
		 synchronous_commit > SYNCHRONOUS_COMMIT_OFF) ||
		forceSyncCommit || nrels > 0)
	{
		XLogFlush(XactLastRecEnd);

		/*
		 * Now we may update the CLOG, if we wrote a COMMIT record above
		 */
		if (markXidCommitted)
			TransactionIdCommitTree(xid, nchildren, children);
	}
	else
	{
		/*
		 * Asynchronous commit case:
		 *
		 * This enables possible committed transaction loss in the case of a
		 * postmaster crash because WAL buffers are left unwritten. Ideally we
		 * could issue the WAL write without the fsync, but some
		 * wal_sync_methods do not allow separate write/fsync.
		 *
		 * Report the latest async commit LSN, so that the WAL writer knows to
		 * flush this commit.
		 */
		XLogSetAsyncXactLSN(XactLastRecEnd);

		/*
		 * We must not immediately update the CLOG, since we didn't flush the
		 * XLOG. Instead, we store the LSN up to which the XLOG must be
		 * flushed before the CLOG may be updated.
		 */
		if (markXidCommitted)
			TransactionIdAsyncCommitTree(xid, nchildren, children, XactLastRecEnd);
	}

	/*
	 * If we entered a commit critical section, leave it now, and let
	 * checkpoints proceed.
	 */
	if (markXidCommitted)
	{
		MyProc->delayChkptFlags &= ~DELAY_CHKPT_START;
		END_CRIT_SECTION();
	}

	/* Compute latestXid while we have the child XIDs handy */
	latestXid = TransactionIdLatest(xid, nchildren, children);

	/*
	 * Wait for synchronous replication, if required. Similar to the decision
	 * above about using committing asynchronously we only want to wait if
	 * this backend assigned an xid and wrote WAL.  No need to wait if an xid
	 * was assigned due to temporary/unlogged tables or due to HOT pruning.
	 *
	 * Note that at this stage we have marked clog, but still show as running
	 * in the procarray and continue to hold locks.
	 */
	if (wrote_xlog && markXidCommitted)
		SyncRepWaitForLSN(XactLastRecEnd, true);

	/* remember end of last commit record */
	XactLastCommitEnd = XactLastRecEnd;

	/* Reset XactLastRecEnd until the next transaction writes something */
	XactLastRecEnd = 0;
cleanup:
	/* Clean up local data */
	if (rels)
		pfree(rels);
	if (ndroppedstats)
		pfree(droppedstats);

	return latestXid;
}


/*
 *	AtCCI_LocalCache
 */
static void
AtCCI_LocalCache(void)
{
	/*
	 * Make any pending relation map changes visible.  We must do this before
	 * processing local sinval messages, so that the map changes will get
	 * reflected into the relcache when relcache invals are processed.
	 */
	AtCCI_RelationMap();

	/*
	 * Make catalog changes visible to me for the next command.
	 */
	CommandEndInvalidationMessages();
}

/*
 *	AtCommit_Memory
 */
static void
AtCommit_Memory(void)
{
	/*
	 * Now that we're "out" of a transaction, have the system allocate things
	 * in the top memory context instead of per-transaction contexts.
	 */
	MemoryContextSwitchTo(TopMemoryContext);

	/*
	 * Release all transaction-local memory.
	 */
	Assert(TopTransactionContext != NULL);
	MemoryContextDelete(TopTransactionContext);
	TopTransactionContext = NULL;
	CurTransactionContext = NULL;
	CurrentTransactionState->curTransactionContext = NULL;
}

/* ----------------------------------------------------------------
 *						CommitSubTransaction stuff
 * ----------------------------------------------------------------
 */

/*
 * AtSubCommit_Memory
 */
static void
AtSubCommit_Memory(void)
{
	TransactionState s = CurrentTransactionState;

	Assert(s->parent != NULL);

	/* Return to parent transaction level's memory context. */
	CurTransactionContext = s->parent->curTransactionContext;
	MemoryContextSwitchTo(CurTransactionContext);

	/*
	 * Ordinarily we cannot throw away the child's CurTransactionContext,
	 * since the data it contains will be needed at upper commit.  However, if
	 * there isn't actually anything in it, we can throw it away.  This avoids
	 * a small memory leak in the common case of "trivial" subxacts.
	 */
	if (MemoryContextIsEmpty(s->curTransactionContext))
	{
		MemoryContextDelete(s->curTransactionContext);
		s->curTransactionContext = NULL;
	}
}

/*
 * AtSubCommit_childXids
 *
 * Pass my own XID and my child XIDs up to my parent as committed children.
 */
static void
AtSubCommit_childXids(void)
{
	TransactionState s = CurrentTransactionState;
	int			new_nChildXids;

	Assert(s->parent != NULL);

	/*
	 * The parent childXids array will need to hold my XID and all my
	 * childXids, in addition to the XIDs already there.
	 */
	new_nChildXids = s->parent->nChildXids + s->nChildXids + 1;

	/* Allocate or enlarge the parent array if necessary */
	if (s->parent->maxChildXids < new_nChildXids)
	{
		int			new_maxChildXids;
		TransactionId *new_childXids;

		/*
		 * Make it 2x what's needed right now, to avoid having to enlarge it
		 * repeatedly. But we can't go above MaxAllocSize.  (The latter limit
		 * is what ensures that we don't need to worry about integer overflow
		 * here or in the calculation of new_nChildXids.)
		 */
		new_maxChildXids = Min(new_nChildXids * 2,
							   (int) (MaxAllocSize / sizeof(TransactionId)));

		if (new_maxChildXids < new_nChildXids)
			ereport(ERROR,
					(errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
					 errmsg("maximum number of committed subtransactions (%d) exceeded",
							(int) (MaxAllocSize / sizeof(TransactionId)))));

		/*
		 * We keep the child-XID arrays in TopTransactionContext; this avoids
		 * setting up child-transaction contexts for what might be just a few
		 * bytes of grandchild XIDs.
		 */
		if (s->parent->childXids == NULL)
			new_childXids =
				MemoryContextAlloc(TopTransactionContext,
								   new_maxChildXids * sizeof(TransactionId));
		else
			new_childXids = repalloc(s->parent->childXids,
									 new_maxChildXids * sizeof(TransactionId));

		s->parent->childXids = new_childXids;
		s->parent->maxChildXids = new_maxChildXids;
	}

	/*
	 * Copy all my XIDs to parent's array.
	 *
	 * Note: We rely on the fact that the XID of a child always follows that
	 * of its parent.  By copying the XID of this subtransaction before the
	 * XIDs of its children, we ensure that the array stays ordered. Likewise,
	 * all XIDs already in the array belong to subtransactions started and
	 * subcommitted before us, so their XIDs must precede ours.
	 */
	s->parent->childXids[s->parent->nChildXids] = XidFromFullTransactionId(s->fullTransactionId);

	if (s->nChildXids > 0)
		memcpy(&s->parent->childXids[s->parent->nChildXids + 1],
			   s->childXids,
			   s->nChildXids * sizeof(TransactionId));

	s->parent->nChildXids = new_nChildXids;

	/* Release child's array to avoid leakage */
	if (s->childXids != NULL)
		pfree(s->childXids);
	/* We must reset these to avoid double-free if fail later in commit */
	s->childXids = NULL;
	s->nChildXids = 0;
	s->maxChildXids = 0;
}

/* ----------------------------------------------------------------
 *						AbortTransaction stuff
 * ----------------------------------------------------------------
 */

/*
 *	RecordTransactionAbort
 *
 * Returns latest XID among xact and its children, or InvalidTransactionId
 * if the xact has no XID.  (We compute that here just because it's easier.)
 */
static TransactionId
RecordTransactionAbort(bool isSubXact)
{
	TransactionId xid = GetCurrentTransactionIdIfAny();
	TransactionId latestXid;
	int			nrels;
	RelFileNode *rels;
	int			ndroppedstats = 0;
	xl_xact_stats_item *droppedstats = NULL;
	int			nchildren;
	TransactionId *children;
	TimestampTz xact_time;

	/*
	 * If we haven't been assigned an XID, nobody will care whether we aborted
	 * or not.  Hence, we're done in that case.  It does not matter if we have
	 * rels to delete (note that this routine is not responsible for actually
	 * deleting 'em).  We cannot have any child XIDs, either.
	 */
	if (!TransactionIdIsValid(xid))
	{
		/* Reset XactLastRecEnd until the next transaction writes something */
		if (!isSubXact)
			XactLastRecEnd = 0;
		return InvalidTransactionId;
	}

	/*
	 * We have a valid XID, so we should write an ABORT record for it.
	 *
	 * We do not flush XLOG to disk here, since the default assumption after a
	 * crash would be that we aborted, anyway.  For the same reason, we don't
	 * need to worry about interlocking against checkpoint start.
	 */

	/*
	 * Check that we haven't aborted halfway through RecordTransactionCommit.
	 */
	if (TransactionIdDidCommit(xid))
		elog(PANIC, "cannot abort transaction %u, it was already committed",
			 xid);

	/* Fetch the data we need for the abort record */
	nrels = smgrGetPendingDeletes(false, &rels);
	nchildren = xactGetCommittedChildren(&children);
	ndroppedstats = pgstat_get_transactional_drops(false, &droppedstats);

	/* XXX do we really need a critical section here? */
	START_CRIT_SECTION();

	/* Write the ABORT record */
	if (isSubXact)
		xact_time = GetCurrentTimestamp();
	else
	{
		SetCurrentTransactionStopTimestamp();
		xact_time = xactStopTimestamp;
	}

	XactLogAbortRecord(xact_time,
					   nchildren, children,
					   nrels, rels,
					   ndroppedstats, droppedstats,
					   MyXactFlags, InvalidTransactionId,
					   NULL);

	/*
	 * Report the latest async abort LSN, so that the WAL writer knows to
	 * flush this abort. There's nothing to be gained by delaying this, since
	 * WALWriter may as well do this when it can. This is important with
	 * streaming replication because if we don't flush WAL regularly we will
	 * find that large aborts leave us with a long backlog for when commits
	 * occur after the abort, increasing our window of data loss should
	 * problems occur at that point.
	 */
	if (!isSubXact)
		XLogSetAsyncXactLSN(XactLastRecEnd);

	/*
	 * Mark the transaction aborted in clog.  This is not absolutely necessary
	 * but we may as well do it while we are here; also, in the subxact case
	 * it is helpful because XactLockTableWait makes use of it to avoid
	 * waiting for already-aborted subtransactions.  It is OK to do it without
	 * having flushed the ABORT record to disk, because in event of a crash
	 * we'd be assumed to have aborted anyway.
	 */
	TransactionIdAbortTree(xid, nchildren, children);

	END_CRIT_SECTION();

	/* Compute latestXid while we have the child XIDs handy */
	latestXid = TransactionIdLatest(xid, nchildren, children);

	/*
	 * If we're aborting a subtransaction, we can immediately remove failed
	 * XIDs from PGPROC's cache of running child XIDs.  We do that here for
	 * subxacts, because we already have the child XID array at hand.  For
	 * main xacts, the equivalent happens just after this function returns.
	 */
	if (isSubXact)
		XidCacheRemoveRunningXids(xid, nchildren, children, latestXid);

	/* Reset XactLastRecEnd until the next transaction writes something */
	if (!isSubXact)
		XactLastRecEnd = 0;

	/* And clean up local data */
	if (rels)
		pfree(rels);
	if (ndroppedstats)
		pfree(droppedstats);

	return latestXid;
}

/*
 *	AtAbort_Memory
 */
static void
AtAbort_Memory(void)
{
	/*
	 * Switch into TransactionAbortContext, which should have some free space
	 * even if nothing else does.  We'll work in this context until we've
	 * finished cleaning up.
	 *
	 * It is barely possible to get here when we've not been able to create
	 * TransactionAbortContext yet; if so use TopMemoryContext.
	 */
	if (TransactionAbortContext != NULL)
		MemoryContextSwitchTo(TransactionAbortContext);
	else
		MemoryContextSwitchTo(TopMemoryContext);
}

/*
 * AtSubAbort_Memory
 */
static void
AtSubAbort_Memory(void)
{
	Assert(TransactionAbortContext != NULL);

	MemoryContextSwitchTo(TransactionAbortContext);
}


/*
 *	AtAbort_ResourceOwner
 */
static void
AtAbort_ResourceOwner(void)
{
	/*
	 * Make sure we have a valid ResourceOwner, if possible (else it will be
	 * NULL, which is OK)
	 */
	CurrentResourceOwner = TopTransactionResourceOwner;
}

/*
 * AtSubAbort_ResourceOwner
 */
static void
AtSubAbort_ResourceOwner(void)
{
	TransactionState s = CurrentTransactionState;

	/* Make sure we have a valid ResourceOwner */
	CurrentResourceOwner = s->curTransactionOwner;
}


/*
 * AtSubAbort_childXids
 */
static void
AtSubAbort_childXids(void)
{
	TransactionState s = CurrentTransactionState;

	/*
	 * We keep the child-XID arrays in TopTransactionContext (see
	 * AtSubCommit_childXids).  This means we'd better free the array
	 * explicitly at abort to avoid leakage.
	 */
	if (s->childXids != NULL)
		pfree(s->childXids);
	s->childXids = NULL;
	s->nChildXids = 0;
	s->maxChildXids = 0;

	/*
	 * We could prune the unreportedXids array here. But we don't bother. That
	 * would potentially reduce number of XLOG_XACT_ASSIGNMENT records but it
	 * would likely introduce more CPU time into the more common paths, so we
	 * choose not to do that.
	 */
}

/* ----------------------------------------------------------------
 *						CleanupTransaction stuff
 * ----------------------------------------------------------------
 */

/*
 *	AtCleanup_Memory
 */
static void
AtCleanup_Memory(void)
{
	Assert(CurrentTransactionState->parent == NULL);

	/*
	 * Now that we're "out" of a transaction, have the system allocate things
	 * in the top memory context instead of per-transaction contexts.
	 */
	MemoryContextSwitchTo(TopMemoryContext);

	/*
	 * Clear the special abort context for next time.
	 */
	if (TransactionAbortContext != NULL)
		MemoryContextResetAndDeleteChildren(TransactionAbortContext);

	/*
	 * Release all transaction-local memory.
	 */
	if (TopTransactionContext != NULL)
		MemoryContextDelete(TopTransactionContext);
	TopTransactionContext = NULL;
	CurTransactionContext = NULL;
	CurrentTransactionState->curTransactionContext = NULL;
}


/* ----------------------------------------------------------------
 *						CleanupSubTransaction stuff
 * ----------------------------------------------------------------
 */

/*
 * AtSubCleanup_Memory
 */
static void
AtSubCleanup_Memory(void)
{
	TransactionState s = CurrentTransactionState;

	Assert(s->parent != NULL);

	/* Make sure we're not in an about-to-be-deleted context */
	MemoryContextSwitchTo(s->parent->curTransactionContext);
	CurTransactionContext = s->parent->curTransactionContext;

	/*
	 * Clear the special abort context for next time.
	 */
	if (TransactionAbortContext != NULL)
		MemoryContextResetAndDeleteChildren(TransactionAbortContext);

	/*
	 * Delete the subxact local memory contexts. Its CurTransactionContext can
	 * go too (note this also kills CurTransactionContexts from any children
	 * of the subxact).
	 */
	if (s->curTransactionContext)
		MemoryContextDelete(s->curTransactionContext);
	s->curTransactionContext = NULL;
}

/* ----------------------------------------------------------------
 *						interface routines
 * ----------------------------------------------------------------
 */

static void
YBUpdateActiveSubTransaction(TransactionState s)
{
	YBCSetActiveSubTransaction(s->subTransactionId);
}

/*
 * Do a Yugabyte-specific initialization of transaction when it starts,
 * called as a part of StartTransaction
 */
static void
YBStartTransaction(TransactionState s)
{
	elog(DEBUG2, "YBStartTransaction");
	s->isYBTxnWithPostgresRel = !IsYugaByteEnabled();
	s->ybDataSent             = false;
	s->ybDataSentForCurrQuery = false;
	s->YBPostponedDdlOps      = NULL;

	if (IsYugaByteEnabled())
	{
		YBInitializeTransaction();
	}
}

/*
 * The isolation level in Postgres code (i.e., XactIsoLevel) maps to a certain
 * isolation level as seen by pggate. This function returns the mapped isolation
 * level that pggate layer is supposed to see.
 */
int
YBGetEffectivePggateIsolationLevel()
{
	int mapped_pg_isolation_level = XactIsoLevel;

	/*
	 * For the txn manager, logic for XACT_READ_UNCOMMITTED is same as
	 * XACT_READ_COMMITTED.
	 */
	if (mapped_pg_isolation_level == XACT_READ_UNCOMMITTED)
		mapped_pg_isolation_level = XACT_READ_COMMITTED;

	/*
	 * If READ COMMITTED mode is not on, XACT_READ_COMMITTED maps to
	 * XACT_REPEATABLE_READ.
	 */
	if ((mapped_pg_isolation_level == XACT_READ_COMMITTED) &&
			!IsYBReadCommitted())
		mapped_pg_isolation_level = XACT_REPEATABLE_READ;

	return mapped_pg_isolation_level;
}

void
YBInitializeTransaction(void)
{
	if (YBTransactionsEnabled())
	{
		HandleYBStatus(YBCPgBeginTransaction(xactStartTimestamp));

		HandleYBStatus(
			YBCPgSetTransactionIsolationLevel(YBGetEffectivePggateIsolationLevel()));
		HandleYBStatus(YBCPgUpdateFollowerReadsConfig(
			YBReadFromFollowersEnabled(), YBFollowerReadStalenessMs()));
		HandleYBStatus(YBCPgSetTransactionReadOnly(XactReadOnly));
		HandleYBStatus(YBCPgSetEnableTracing(YBEnableTracing()));
		HandleYBStatus(YBCPgSetTransactionDeferrable(XactDeferrable));
	}
}

/*
 *	StartTransaction
 */
static void
StartTransaction(void)
{
	TransactionState s;
	VirtualTransactionId vxid;

	/*
	 * Let's just make sure the state stack is empty
	 */
	s = &TopTransactionStateData;
	CurrentTransactionState = s;

	Assert(!FullTransactionIdIsValid(XactTopFullTransactionId));

	/* check the current transaction state */
	Assert(s->state == TRANS_DEFAULT);

	/*
	 * Set the current transaction state information appropriately during
	 * start processing.  Note that once the transaction status is switched
	 * this process cannot fail until the user ID and the security context
	 * flags are fetched below.
	 */
	s->state = TRANS_START;
	s->fullTransactionId = InvalidFullTransactionId;	/* until assigned */

	/* Determine if statements are logged in this transaction */
	xact_is_sampled = log_xact_sample_rate != 0 &&
		(log_xact_sample_rate == 1 ||
		 pg_prng_double(&pg_global_prng_state) <= log_xact_sample_rate);

	/*
	 * initialize current transaction state fields
	 *
	 * note: prevXactReadOnly is not used at the outermost level
	 */
	s->nestingLevel = 1;
	s->gucNestLevel = 1;
	s->childXids = NULL;
	s->nChildXids = 0;
	s->maxChildXids = 0;

	/*
	 * Once the current user ID and the security context flags are fetched,
	 * both will be properly reset even if transaction startup fails.
	 */
	GetUserIdAndSecContext(&s->prevUser, &s->prevSecContext);

	/* SecurityRestrictionContext should never be set outside a transaction */
	Assert(s->prevSecContext == 0);

	/*
	 * Make sure we've reset xact state variables
	 *
	 * If recovery is still in progress, mark this transaction as read-only.
	 * We have lower level defences in XLogInsert and elsewhere to stop us
	 * from modifying data during recovery, but this gives the normal
	 * indication to the user that the transaction is read-only.
	 */
	if (RecoveryInProgress())
	{
		s->startedInRecovery = true;
		XactReadOnly = true;
	}
	else
	{
		s->startedInRecovery = false;
		XactReadOnly = DefaultXactReadOnly;
	}
	XactDeferrable = DefaultXactDeferrable;
	XactIsoLevel = DefaultXactIsoLevel;
	forceSyncCommit = false;
	MyXactFlags = 0;

	/*
	 * reinitialize within-transaction counters
	 */
	s->subTransactionId = TopSubTransactionId;
	currentSubTransactionId = TopSubTransactionId;
	currentCommandId = FirstCommandId;
	currentCommandIdUsed = false;

	/*
	 * initialize reported xid accounting
	 */
	nUnreportedXids = 0;
	s->didLogXid = false;

	/*
	 * must initialize resource-management stuff first
	 */
	AtStart_Memory();
	AtStart_ResourceOwner();

	/*
	 * Assign a new LocalTransactionId, and combine it with the backendId to
	 * form a virtual transaction id.
	 */
	vxid.backendId = MyBackendId;
	vxid.localTransactionId = GetNextLocalTransactionId();

	/*
	 * Lock the virtual transaction id before we announce it in the proc array
	 */
	VirtualXactLockTableInsert(vxid);

	/*
	 * Advertise it in the proc array.  We assume assignment of
	 * localTransactionId is atomic, and the backendId should be set already.
	 */
	Assert(MyProc->backendId == vxid.backendId);
	MyProc->lxid = vxid.localTransactionId;

	TRACE_POSTGRESQL_TRANSACTION_START(vxid.localTransactionId);

	/*
	 * set transaction_timestamp() (a/k/a now()).  Normally, we want this to
	 * be the same as the first command's statement_timestamp(), so don't do a
	 * fresh GetCurrentTimestamp() call (which'd be expensive anyway).  But
	 * for transactions started inside procedures (i.e., nonatomic SPI
	 * contexts), we do need to advance the timestamp.  Also, in a parallel
	 * worker, the timestamp should already have been provided by a call to
	 * SetParallelStartTimestamps().
	 */
	if (!IsParallelWorker())
	{
		if (!SPI_inside_nonatomic_context())
			xactStartTimestamp = stmtStartTimestamp;
		else
			xactStartTimestamp = GetCurrentTimestamp();
	}
	else
		Assert(xactStartTimestamp != 0);
	pgstat_report_xact_timestamp(xactStartTimestamp);
	/* Mark xactStopTimestamp as unset. */
	xactStopTimestamp = 0;

	/*
	 * initialize other subsystems for new transaction
	 */
	AtStart_GUC();
	AtStart_Cache();
	AfterTriggerBeginXact();

	/*
	 * done with start processing, set current transaction state to "in
	 * progress"
	 */
	s->state = TRANS_INPROGRESS;

	YBStartTransaction(s);

	s->ybUncommittedStickyObjectCount = 0;

	/* Check for superuser history to determine stickiness of connection */
	if (YbIsClientYsqlConnMgr() && OidIsValid(s->prevUser))
		yb_ysql_conn_mgr_superuser_existed = yb_ysql_conn_mgr_superuser_existed || superuser();
	ShowTransactionState("StartTransaction");
}

/*
 * Recreates the state required to restart the write that received a transaction
 * conflict.
 */
void
YBCRestartWriteTransaction()
{
	/*
	 * Presence of triggers pushes additional snapshots. Pop all of them. Given
	 * that we restart the writes only when we haven't sent any data back to the
	 * user, removing all snapshots is safe.
	 */
	PopAllActiveSnapshots();

	AtEOXact_SPI(false /* isCommit */);

	/*
	 * Recreate the global state present for triggers that would have changed
	 * during the execution of the failed write.
	 */
	AfterTriggerEndXact(false /* isCommit */);
	AfterTriggerBeginXact();
}

/*
 *	CommitTransaction
 *
 * NB: if you change this routine, better look at PrepareTransaction too!
 */
static void
CommitTransaction(void)
{
	TransactionState s = CurrentTransactionState;
	TransactionId latestXid;
	bool		is_parallel_worker;

	is_parallel_worker = (s->blockState == TBLOCK_PARALLEL_INPROGRESS);

	/* Enforce parallel mode restrictions during parallel worker commit. */
	if (is_parallel_worker)
		EnterParallelMode();

	ShowTransactionState("CommitTransaction");

	/*
	 * check the current transaction state
	 */
	if (s->state != TRANS_INPROGRESS)
		elog(WARNING, "CommitTransaction while in %s state",
			 TransStateAsString(s->state));
	Assert(s->parent == NULL);

	/*
	 * Do pre-commit processing that involves calling user-defined code, such
	 * as triggers.  SECURITY_RESTRICTED_OPERATION contexts must not queue an
	 * action that would run here, because that would bypass the sandbox.
	 * Since closing cursors could queue trigger actions, triggers could open
	 * cursors, etc, we have to keep looping until there's nothing left to do.
	 */
	for (;;)
	{
		/*
		 * Fire all currently pending deferred triggers.
		 */
		AfterTriggerFireDeferred();

		/*
		 * Close open portals (converting holdable ones into static portals).
		 * If there weren't any, we are done ... otherwise loop back to check
		 * if they queued deferred triggers.  Lather, rinse, repeat.
		 */
		if (!PreCommit_Portals(false))
			break;
	}

	/*
	 * Firing the triggers may abort current transaction.
	 * At this point all the them has been fired already.
	 * It is time to commit YB transaction.
	 * Postgres transaction can be aborted at this point without an issue
	 * in case of YBCCommitTransaction failure.
	 */
	YBCCommitTransaction();

	/*
	 * The remaining actions cannot call any user-defined code, so it's safe
	 * to start shutting down within-transaction services.  But note that most
	 * of this stuff could still throw an error, which would switch us into
	 * the transaction-abort path.
	 */

	CallXactCallbacks(is_parallel_worker ? XACT_EVENT_PARALLEL_PRE_COMMIT
					  : XACT_EVENT_PRE_COMMIT);

	/* If we might have parallel workers, clean them up now. */
	if (IsInParallelMode())
		AtEOXact_Parallel(true);

	/* Shut down the deferred-trigger manager */
	AfterTriggerEndXact(true);

	/*
	 * Let ON COMMIT management do its thing (must happen after closing
	 * cursors, to avoid dangling-reference problems)
	 */
	PreCommit_on_commit_actions();

	/*
	 * Synchronize files that are created and not WAL-logged during this
	 * transaction. This must happen before AtEOXact_RelationMap(), so that we
	 * don't see committed-but-broken files after a crash.
	 */
	smgrDoPendingSyncs(true, is_parallel_worker);

	/* close large objects before lower-level cleanup */
	AtEOXact_LargeObject(true);

	/*
	 * Insert notifications sent by NOTIFY commands into the queue.  This
	 * should be late in the pre-commit sequence to minimize time spent
	 * holding the notify-insertion lock.  However, this could result in
	 * creating a snapshot, so we must do it before serializable cleanup.
	 */
	PreCommit_Notify();

	/*
	 * Mark serializable transaction as complete for predicate locking
	 * purposes.  This should be done as late as we can put it and still allow
	 * errors to be raised for failure patterns found at commit.  This is not
	 * appropriate in a parallel worker however, because we aren't committing
	 * the leader's transaction and its serializable state will live on.
	 */
	if (!is_parallel_worker)
		PreCommit_CheckForSerializationFailure();

	/* Prevent cancel/die interrupt while cleaning up */
	HOLD_INTERRUPTS();

	/* Commit updates to the relation map --- do this as late as possible */
	AtEOXact_RelationMap(true, is_parallel_worker);

	/*
	 * set the current transaction state information appropriately during
	 * commit processing
	 */
	s->state = TRANS_COMMIT;
	s->parallelModeLevel = 0;

	if (!is_parallel_worker)
	{
		/*
		 * We need to mark our XIDs as committed in pg_xact.  This is where we
		 * durably commit.
		 */
		latestXid = RecordTransactionCommit();
	}
	else
	{
		/*
		 * We must not mark our XID committed; the parallel leader is
		 * responsible for that.
		 */
		latestXid = InvalidTransactionId;

		/*
		 * Make sure the leader will know about any WAL we wrote before it
		 * commits.
		 */
		ParallelWorkerReportLastRecEnd(XactLastRecEnd);
	}

	TRACE_POSTGRESQL_TRANSACTION_COMMIT(MyProc->lxid);

	/*
	 * Let others know about no transaction in progress by me. Note that this
	 * must be done _before_ releasing locks we hold and _after_
	 * RecordTransactionCommit.
	 */
	ProcArrayEndTransaction(MyProc, latestXid);

	/*
	 * This is all post-commit cleanup.  Note that if an error is raised here,
	 * it's too late to abort the transaction.  This should be just
	 * noncritical resource releasing.
	 *
	 * The ordering of operations is not entirely random.  The idea is:
	 * release resources visible to other backends (eg, files, buffer pins);
	 * then release locks; then release backend-local resources. We want to
	 * release locks at the point where any backend waiting for us will see
	 * our transaction as being fully cleaned up.
	 *
	 * Resources that can be associated with individual queries are handled by
	 * the ResourceOwner mechanism.  The other calls here are for backend-wide
	 * state.
	 */

	CallXactCallbacks(is_parallel_worker ? XACT_EVENT_PARALLEL_COMMIT
					  : XACT_EVENT_COMMIT);

	ResourceOwnerRelease(TopTransactionResourceOwner,
						 RESOURCE_RELEASE_BEFORE_LOCKS,
						 true, true);

	/* Check we've released all buffer pins */
	AtEOXact_Buffers(true);

	/* Clean up the relation cache */
	AtEOXact_RelationCache(true);

	/*
	 * Make catalog changes visible to all backends.  This has to happen after
	 * relcache references are dropped (see comments for
	 * AtEOXact_RelationCache), but before locks are released (if anyone is
	 * waiting for lock on a relation we've modified, we want them to know
	 * about the catalog change before they start using the relation).
	 */
	AtEOXact_Inval(true);

	AtEOXact_MultiXact();

	ResourceOwnerRelease(TopTransactionResourceOwner,
						 RESOURCE_RELEASE_LOCKS,
						 true, true);
	ResourceOwnerRelease(TopTransactionResourceOwner,
						 RESOURCE_RELEASE_AFTER_LOCKS,
						 true, true);

	/*
	 * Likewise, dropping of files deleted during the transaction is best done
	 * after releasing relcache and buffer pins.  (This is not strictly
	 * necessary during commit, since such pins should have been released
	 * already, but this ordering is definitely critical during abort.)  Since
	 * this may take many seconds, also delay until after releasing locks.
	 * Other backends will observe the attendant catalog changes and not
	 * attempt to access affected files.
	 */
	smgrDoPendingDeletes(true);

	/*
	 * Send out notification signals to other backends (and do other
	 * post-commit NOTIFY cleanup).  This must not happen until after our
	 * transaction is fully done from the viewpoint of other backends.
	 */
	AtCommit_Notify();

	/*
	 * Everything after this should be purely internal-to-this-backend
	 * cleanup.
	 */
	AtEOXact_GUC(true, 1);
	AtEOXact_SPI(true);
	AtEOXact_Enum();
	AtEOXact_on_commit_actions(true);
	AtEOXact_Namespace(true, is_parallel_worker);
	AtEOXact_SMgr();
	AtEOXact_Files(true);
	AtEOXact_ComboCid();
	AtEOXact_HashTables(true);
	AtEOXact_PgStat(true, is_parallel_worker);
	AtEOXact_Snapshot(true, false);
	AtEOXact_ApplyLauncher(true);
	pgstat_report_xact_timestamp(0);

	CurrentResourceOwner = NULL;
	ResourceOwnerDelete(TopTransactionResourceOwner);
	s->curTransactionOwner = NULL;
	CurTransactionResourceOwner = NULL;
	TopTransactionResourceOwner = NULL;

	AtCommit_Memory();

	s->fullTransactionId = InvalidFullTransactionId;
	s->subTransactionId = InvalidSubTransactionId;
	s->nestingLevel = 0;
	s->gucNestLevel = 0;
	s->childXids = NULL;
	s->nChildXids = 0;
	s->maxChildXids = 0;

	XactTopFullTransactionId = InvalidFullTransactionId;
	nParallelCurrentXids = 0;

	/*
	 * done with commit processing, set current transaction state back to
	 * default
	 */
	s->state = TRANS_DEFAULT;

	RESUME_INTERRUPTS();
}


/*
 *	PrepareTransaction
 *
 * NB: if you change this routine, better look at CommitTransaction too!
 */
static void
PrepareTransaction(void)
{
	TransactionState s = CurrentTransactionState;
	TransactionId xid = GetCurrentTransactionId();
	GlobalTransaction gxact;
	TimestampTz prepared_at;

	Assert(!IsInParallelMode());

	ShowTransactionState("PrepareTransaction");

	/*
	 * check the current transaction state
	 */
	if (s->state != TRANS_INPROGRESS)
		elog(WARNING, "PrepareTransaction while in %s state",
			 TransStateAsString(s->state));
	Assert(s->parent == NULL);

	/*
	 * Do pre-commit processing that involves calling user-defined code, such
	 * as triggers.  Since closing cursors could queue trigger actions,
	 * triggers could open cursors, etc, we have to keep looping until there's
	 * nothing left to do.
	 */
	for (;;)
	{
		/*
		 * Fire all currently pending deferred triggers.
		 */
		AfterTriggerFireDeferred();

		/*
		 * Close open portals (converting holdable ones into static portals).
		 * If there weren't any, we are done ... otherwise loop back to check
		 * if they queued deferred triggers.  Lather, rinse, repeat.
		 */
		if (!PreCommit_Portals(true))
			break;
	}

	CallXactCallbacks(XACT_EVENT_PRE_PREPARE);

	/*
	 * The remaining actions cannot call any user-defined code, so it's safe
	 * to start shutting down within-transaction services.  But note that most
	 * of this stuff could still throw an error, which would switch us into
	 * the transaction-abort path.
	 */

	/* Shut down the deferred-trigger manager */
	AfterTriggerEndXact(true);

	/*
	 * Let ON COMMIT management do its thing (must happen after closing
	 * cursors, to avoid dangling-reference problems)
	 */
	PreCommit_on_commit_actions();

	/*
	 * Synchronize files that are created and not WAL-logged during this
	 * transaction. This must happen before EndPrepare(), so that we don't see
	 * committed-but-broken files after a crash and COMMIT PREPARED.
	 */
	smgrDoPendingSyncs(true, false);

	/* close large objects before lower-level cleanup */
	AtEOXact_LargeObject(true);

	/* NOTIFY requires no work at this point */

	/*
	 * Mark serializable transaction as complete for predicate locking
	 * purposes.  This should be done as late as we can put it and still allow
	 * errors to be raised for failure patterns found at commit.
	 */
	PreCommit_CheckForSerializationFailure();

	/*
	 * Don't allow PREPARE TRANSACTION if we've accessed a temporary table in
	 * this transaction.  Having the prepared xact hold locks on another
	 * backend's temp table seems a bad idea --- for instance it would prevent
	 * the backend from exiting.  There are other problems too, such as how to
	 * clean up the source backend's local buffers and ON COMMIT state if the
	 * prepared xact includes a DROP of a temp table.
	 *
	 * Other objects types, like functions, operators or extensions, share the
	 * same restriction as they should not be created, locked or dropped as
	 * this can mess up with this session or even a follow-up session trying
	 * to use the same temporary namespace.
	 *
	 * We must check this after executing any ON COMMIT actions, because they
	 * might still access a temp relation.
	 *
	 * XXX In principle this could be relaxed to allow some useful special
	 * cases, such as a temp table created and dropped all within the
	 * transaction.  That seems to require much more bookkeeping though.
	 */
	if ((MyXactFlags & XACT_FLAGS_ACCESSEDTEMPNAMESPACE))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("cannot PREPARE a transaction that has operated on temporary objects")));

	/*
	 * Likewise, don't allow PREPARE after pg_export_snapshot.  This could be
	 * supported if we added cleanup logic to twophase.c, but for now it
	 * doesn't seem worth the trouble.
	 */
	if (XactHasExportedSnapshots())
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("cannot PREPARE a transaction that has exported snapshots")));

	/* Prevent cancel/die interrupt while cleaning up */
	HOLD_INTERRUPTS();

	/*
	 * set the current transaction state information appropriately during
	 * prepare processing
	 */
	s->state = TRANS_PREPARE;

	prepared_at = GetCurrentTimestamp();

	/* Tell bufmgr and smgr to prepare for commit */
	BufmgrCommit();

	/*
	 * Reserve the GID for this transaction. This could fail if the requested
	 * GID is invalid or already in use.
	 */
	gxact = MarkAsPreparing(xid, prepareGID, prepared_at,
							GetUserId(), MyDatabaseId);
	prepareGID = NULL;

	/*
	 * Collect data for the 2PC state file.  Note that in general, no actual
	 * state change should happen in the called modules during this step,
	 * since it's still possible to fail before commit, and in that case we
	 * want transaction abort to be able to clean up.  (In particular, the
	 * AtPrepare routines may error out if they find cases they cannot
	 * handle.)  State cleanup should happen in the PostPrepare routines
	 * below.  However, some modules can go ahead and clear state here because
	 * they wouldn't do anything with it during abort anyway.
	 *
	 * Note: because the 2PC state file records will be replayed in the same
	 * order they are made, the order of these calls has to match the order in
	 * which we want things to happen during COMMIT PREPARED or ROLLBACK
	 * PREPARED; in particular, pay attention to whether things should happen
	 * before or after releasing the transaction's locks.
	 */
	StartPrepare(gxact);

	AtPrepare_Notify();
	if (YBIsPgLockingEnabled())
	{
		AtPrepare_Locks();
		AtPrepare_PredicateLocks();
	}
	AtPrepare_PgStat();
	AtPrepare_MultiXact();
	AtPrepare_RelationMap();

	/*
	 * Here is where we really truly prepare.
	 *
	 * We have to record transaction prepares even if we didn't make any
	 * updates, because the transaction manager might get confused if we lose
	 * a global transaction.
	 */
	EndPrepare(gxact);

	/*
	 * Now we clean up backend-internal state and release internal resources.
	 */

	/* Reset XactLastRecEnd until the next transaction writes something */
	XactLastRecEnd = 0;

	/*
	 * Transfer our locks to a dummy PGPROC.  This has to be done before
	 * ProcArrayClearTransaction().  Otherwise, a GetLockConflicts() would
	 * conclude "xact already committed or aborted" for our locks.
	 */
	PostPrepare_Locks(xid);

	/*
	 * Let others know about no transaction in progress by me.  This has to be
	 * done *after* the prepared transaction has been marked valid, else
	 * someone may think it is unlocked and recyclable.
	 */
	ProcArrayClearTransaction(MyProc);

	/*
	 * In normal commit-processing, this is all non-critical post-transaction
	 * cleanup.  When the transaction is prepared, however, it's important
	 * that the locks and other per-backend resources are transferred to the
	 * prepared transaction's PGPROC entry.  Note that if an error is raised
	 * here, it's too late to abort the transaction. XXX: This probably should
	 * be in a critical section, to force a PANIC if any of this fails, but
	 * that cure could be worse than the disease.
	 */

	CallXactCallbacks(XACT_EVENT_PREPARE);

	ResourceOwnerRelease(TopTransactionResourceOwner,
						 RESOURCE_RELEASE_BEFORE_LOCKS,
						 true, true);

	/* Check we've released all buffer pins */
	AtEOXact_Buffers(true);

	/* Clean up the relation cache */
	AtEOXact_RelationCache(true);

	/* notify doesn't need a postprepare call */

	PostPrepare_PgStat();

	PostPrepare_Inval();

	PostPrepare_smgr();

	PostPrepare_MultiXact(xid);

	if (YBIsPgLockingEnabled())
		PostPrepare_PredicateLocks(xid);

	ResourceOwnerRelease(TopTransactionResourceOwner,
						 RESOURCE_RELEASE_LOCKS,
						 true, true);
	ResourceOwnerRelease(TopTransactionResourceOwner,
						 RESOURCE_RELEASE_AFTER_LOCKS,
						 true, true);

	/*
	 * Allow another backend to finish the transaction.  After
	 * PostPrepare_Twophase(), the transaction is completely detached from our
	 * backend.  The rest is just non-critical cleanup of backend-local state.
	 */
	PostPrepare_Twophase();

	/* PREPARE acts the same as COMMIT as far as GUC is concerned */
	AtEOXact_GUC(true, 1);
	AtEOXact_SPI(true);
	AtEOXact_Enum();
	AtEOXact_on_commit_actions(true);
	AtEOXact_Namespace(true, false);
	AtEOXact_SMgr();
	AtEOXact_Files(true);
	AtEOXact_ComboCid();
	AtEOXact_HashTables(true);
	/* don't call AtEOXact_PgStat here; we fixed pgstat state above */
	AtEOXact_Snapshot(true, true);
	pgstat_report_xact_timestamp(0);

	CurrentResourceOwner = NULL;
	ResourceOwnerDelete(TopTransactionResourceOwner);
	s->curTransactionOwner = NULL;
	CurTransactionResourceOwner = NULL;
	TopTransactionResourceOwner = NULL;

	AtCommit_Memory();

	s->fullTransactionId = InvalidFullTransactionId;
	s->subTransactionId = InvalidSubTransactionId;
	s->nestingLevel = 0;
	s->gucNestLevel = 0;
	s->childXids = NULL;
	s->nChildXids = 0;
	s->maxChildXids = 0;

	XactTopFullTransactionId = InvalidFullTransactionId;
	nParallelCurrentXids = 0;

	/*
	 * done with 1st phase commit processing, set current transaction state
	 * back to default
	 */
	s->state = TRANS_DEFAULT;

	RESUME_INTERRUPTS();
}


/*
 *	AbortTransaction
 */
static void
AbortTransaction(void)
{
	TransactionState s = CurrentTransactionState;
	TransactionId latestXid;
	bool		is_parallel_worker;

	/* Prevent cancel/die interrupt while cleaning up */
	HOLD_INTERRUPTS();

	/* Make sure we have a valid memory context and resource owner */
	AtAbort_Memory();
	AtAbort_ResourceOwner();

	/*
	 * Release any LW locks we might be holding as quickly as possible.
	 * (Regular locks, however, must be held till we finish aborting.)
	 * Releasing LW locks is critical since we might try to grab them again
	 * while cleaning up!
	 */
	LWLockReleaseAll();

	/* Clear wait information and command progress indicator */
	pgstat_report_wait_end();
	pgstat_progress_end_command();

	/* Clean up buffer I/O and buffer context locks, too */
	AbortBufferIO();
	UnlockBuffers();

	/* Reset WAL record construction state */
	XLogResetInsertion();

	/* Cancel condition variable sleep */
	ConditionVariableCancelSleep();

	if (YBIsPgLockingEnabled())
	{
		/*
		 * Also clean up any open wait for lock, since the lock manager will choke
		 * if we try to wait for another lock before doing this.
		 */
		LockErrorCleanup();
	}

	/*
	 * If any timeout events are still active, make sure the timeout interrupt
	 * is scheduled.  This covers possible loss of a timeout interrupt due to
	 * longjmp'ing out of the SIGINT handler (see notes in handle_sig_alarm).
	 * We delay this till after LockErrorCleanup so that we don't uselessly
	 * reschedule lock or deadlock check timeouts.
	 */
	reschedule_timeouts();

	/*
	 * Re-enable signals, in case we got here by longjmp'ing out of a signal
	 * handler.  We do this fairly early in the sequence so that the timeout
	 * infrastructure will be functional if needed while aborting.
	 */
	PG_SETMASK(&UnBlockSig);

	/*
	 * check the current transaction state
	 */
	is_parallel_worker = (s->blockState == TBLOCK_PARALLEL_INPROGRESS);
	if (s->state != TRANS_INPROGRESS && s->state != TRANS_PREPARE)
		elog(WARNING, "AbortTransaction while in %s state",
			 TransStateAsString(s->state));
	Assert(s->parent == NULL);

	/*
	 * set the current transaction state information appropriately during the
	 * abort processing
	 */
	s->state = TRANS_ABORT;

	/*
	 * Reset user ID which might have been changed transiently.  We need this
	 * to clean up in case control escaped out of a SECURITY DEFINER function
	 * or other local change of CurrentUserId; therefore, the prior value of
	 * SecurityRestrictionContext also needs to be restored.
	 *
	 * (Note: it is not necessary to restore session authorization or role
	 * settings here because those can only be changed via GUC, and GUC will
	 * take care of rolling them back if need be.)
	 */
	SetUserIdAndSecContext(s->prevUser, s->prevSecContext);

	/* Forget about any active REINDEX. */
	ResetReindexState(s->nestingLevel);

	/* Reset logical streaming state. */
	ResetLogicalStreamingState();

	/* Reset snapshot export state. */
	SnapBuildResetExportedSnapshotState();

	/* If in parallel mode, clean up workers and exit parallel mode. */
	if (IsInParallelMode())
	{
		AtEOXact_Parallel(false);
		s->parallelModeLevel = 0;
	}

	/*
	 * do abort processing
	 */
	AfterTriggerEndXact(false); /* 'false' means it's abort */
	AtAbort_Portals();
	smgrDoPendingSyncs(false, is_parallel_worker);
	AtEOXact_LargeObject(false);
	AtAbort_Notify();
	AtEOXact_RelationMap(false, is_parallel_worker);
	AtAbort_Twophase();

	/*
	 * Advertise the fact that we aborted in pg_xact (assuming that we got as
	 * far as assigning an XID to advertise).  But if we're inside a parallel
	 * worker, skip this; the user backend must be the one to write the abort
	 * record.
	 */
	if (!is_parallel_worker)
		latestXid = RecordTransactionAbort(false);
	else
	{
		latestXid = InvalidTransactionId;

		/*
		 * Since the parallel leader won't get our value of XactLastRecEnd in
		 * this case, we nudge WAL-writer ourselves in this case.  See related
		 * comments in RecordTransactionAbort for why this matters.
		 */
		XLogSetAsyncXactLSN(XactLastRecEnd);
	}

	TRACE_POSTGRESQL_TRANSACTION_ABORT(MyProc->lxid);

	/*
	 * Let others know about no transaction in progress by me. Note that this
	 * must be done _before_ releasing locks we hold and _after_
	 * RecordTransactionAbort.
	 */
	ProcArrayEndTransaction(MyProc, latestXid);

	/*
	 * Post-abort cleanup.  See notes in CommitTransaction() concerning
	 * ordering.  We can skip all of it if the transaction failed before
	 * creating a resource owner.
	 */
	if (TopTransactionResourceOwner != NULL)
	{
		if (is_parallel_worker)
			CallXactCallbacks(XACT_EVENT_PARALLEL_ABORT);
		else
			CallXactCallbacks(XACT_EVENT_ABORT);

		ResourceOwnerRelease(TopTransactionResourceOwner,
							 RESOURCE_RELEASE_BEFORE_LOCKS,
							 false, true);
		AtEOXact_Buffers(false);
		AtEOXact_RelationCache(false);
		AtEOXact_Inval(false);
		AtEOXact_MultiXact();
		ResourceOwnerRelease(TopTransactionResourceOwner,
							 RESOURCE_RELEASE_LOCKS,
							 false, true);
		ResourceOwnerRelease(TopTransactionResourceOwner,
							 RESOURCE_RELEASE_AFTER_LOCKS,
							 false, true);
		smgrDoPendingDeletes(false);

		AtEOXact_GUC(false, 1);
		AtEOXact_SPI(false);
		AtEOXact_Enum();
		AtEOXact_on_commit_actions(false);
		AtEOXact_Namespace(false, is_parallel_worker);
		AtEOXact_SMgr();
		AtEOXact_Files(false);
		AtEOXact_ComboCid();
		AtEOXact_HashTables(false);
		AtEOXact_PgStat(false, is_parallel_worker);
		AtEOXact_ApplyLauncher(false);
		pgstat_report_xact_timestamp(0);
	}

	YBCAbortTransaction();

	/* Reset the value of the sticky connection */
	s->ybUncommittedStickyObjectCount = 0;

	/*
	 * State remains TRANS_ABORT until CleanupTransaction().
	 */
	RESUME_INTERRUPTS();
}

/*
 *	CleanupTransaction
 */
static void
CleanupTransaction(void)
{
	TransactionState s = CurrentTransactionState;

	/*
	 * State should still be TRANS_ABORT from AbortTransaction().
	 */
	if (s->state != TRANS_ABORT)
		elog(FATAL, "CleanupTransaction: unexpected state %s",
			 TransStateAsString(s->state));

	/*
	 * do abort cleanup processing
	 */
	AtCleanup_Portals();		/* now safe to release portal memory */
	AtEOXact_Snapshot(false, true); /* and release the transaction's snapshots */

	CurrentResourceOwner = NULL;	/* and resource owner */
	if (TopTransactionResourceOwner)
		ResourceOwnerDelete(TopTransactionResourceOwner);
	s->curTransactionOwner = NULL;
	CurTransactionResourceOwner = NULL;
	TopTransactionResourceOwner = NULL;

	AtCleanup_Memory();			/* and transaction memory */

	s->fullTransactionId = InvalidFullTransactionId;
	s->subTransactionId = InvalidSubTransactionId;
	s->nestingLevel = 0;
	s->gucNestLevel = 0;
	s->childXids = NULL;
	s->nChildXids = 0;
	s->maxChildXids = 0;
	s->parallelModeLevel = 0;

	XactTopFullTransactionId = InvalidFullTransactionId;
	nParallelCurrentXids = 0;

	/*
	 * done with abort processing, set current transaction state back to
	 * default
	 */
	s->state = TRANS_DEFAULT;
}

/*
 *	StartTransactionCommandInternal
 */
static void
StartTransactionCommandInternal(bool yb_skip_read_committed_handling)
{
	TransactionState s = CurrentTransactionState;

	switch (s->blockState)
	{
			/*
			 * if we aren't in a transaction block, we just do our usual start
			 * transaction.
			 */
		case TBLOCK_DEFAULT:
			StartTransaction();
			s->blockState = TBLOCK_STARTED;
			break;

			/*
			 * We are somewhere in a transaction block or subtransaction and
			 * about to start a new command.  For now we do nothing, but
			 * someday we may do command-local resource initialization. (Note
			 * that any needed CommandCounterIncrement was done by the
			 * previous CommitTransactionCommand.)
			 */
		case TBLOCK_INPROGRESS:
		case TBLOCK_IMPLICIT_INPROGRESS:
		case TBLOCK_SUBINPROGRESS:
			/*
			 * YB specific logic.
			 *
			 * For READ COMMITTED isolation, we want to reset the read point to current ht time so that
			 * the query works on a newer snapshot that will include all txns committed before this
			 * command.
			 *
			 * Read restart handling per statement
			 * -----------------------------------
			 * Note that by "all txns committed before this command" we intend to include any txn that
			 * might have been committed before the statement was issued, as per real time (i.e., as
			 * perceived by any client).
			 *
			 * Since there might be clock skew, during a read, if a txn participant finds committed
			 * records with ht after the chosen read ht and is unsure if the records were committed before
			 * the client issued read (as per real time), a kReadRestart will be received by postgres.
			 *
			 * Read restart retries are handled transparently for every statement in the txn in
			 * yb_attempt_to_restart_on_error().
			 */
			if (YBTransactionsEnabled() && IsYBReadCommitted() && !yb_skip_read_committed_handling)
			{
				/*
				 * Reset field ybDataSentForCurrQuery (indicates whether any data was sent as part of the
				 * current query). This helps track if automatic restart of a query is possible in
				 * READ COMMITTED isolation level.
				 */
				s->ybDataSentForCurrQuery = false;

				/*
				 * Create a new internal sub txn before any execution. This aids in rolling back any changes
				 * before restarting the statement.
				 *
				 * We don't rely on the name of the internal sub transaction for rolling back to it in
				 * yb_attempt_to_restart_on_error(). We just assert that the name of the current sub txn
				 * matches before calling RollbackAndReleaseCurrentSubTransaction() to restart the
				 * statement.
				 *
				 * Instead of calling BeginInternalSubTransaction(), we have copy-pasted necessary logic
				 * into a new function since BeginInternalSubTransaction() again calls
				 * CommitTransactionCommand() and StartTransactionCommand() which will result in recursion.
				 * We could have solved the recursion problem by plumbing a flag to skip calling
				 * BeginInternalSubTransaction() again, but it is simpler and less error-prone to just copy
				 * the minimal required logic.
				 */
				BeginInternalSubTransactionForReadCommittedStatement();
			}

			break;

			/*
			 * Here we are in a failed transaction block (one of the commands
			 * caused an abort) so we do nothing but remain in the abort
			 * state.  Eventually we will get a ROLLBACK command which will
			 * get us out of this state.  (It is up to other code to ensure
			 * that no commands other than ROLLBACK will be processed in these
			 * states.)
			 */
		case TBLOCK_ABORT:
		case TBLOCK_SUBABORT:
			break;

			/* These cases are invalid. */
		case TBLOCK_STARTED:
		case TBLOCK_BEGIN:
		case TBLOCK_PARALLEL_INPROGRESS:
		case TBLOCK_SUBBEGIN:
		case TBLOCK_END:
		case TBLOCK_SUBRELEASE:
		case TBLOCK_SUBCOMMIT:
		case TBLOCK_ABORT_END:
		case TBLOCK_SUBABORT_END:
		case TBLOCK_ABORT_PENDING:
		case TBLOCK_SUBABORT_PENDING:
		case TBLOCK_SUBRESTART:
		case TBLOCK_SUBABORT_RESTART:
		case TBLOCK_PREPARE:
			elog(ERROR, "StartTransactionCommand: unexpected state %s",
				 BlockStateAsString(s->blockState));
			break;
	}

	/*
	 * We must switch to CurTransactionContext before returning. This is
	 * already done if we called StartTransaction, otherwise not.
	 */
	Assert(CurTransactionContext != NULL);
	MemoryContextSwitchTo(CurTransactionContext);
}

/*
 *	StartTransactionCommand
 */
void
StartTransactionCommand(void)
{
	elog(DEBUG2, "StartTransactionCommand");
	StartTransactionCommandInternal(false /* yb_skip_read_committed_handling */);
}

/*
 * Simple system for saving and restoring transaction characteristics
 * (isolation level, read only, deferrable).  We need this for transaction
 * chaining, so that we can set the characteristics of the new transaction to
 * be the same as the previous one.  (We need something like this because the
 * GUC system resets the characteristics at transaction end, so for example
 * just skipping the reset in StartTransaction() won't work.)
 */
void
SaveTransactionCharacteristics(SavedTransactionCharacteristics *s)
{
	s->save_XactIsoLevel = XactIsoLevel;
	s->save_XactReadOnly = XactReadOnly;
	s->save_XactDeferrable = XactDeferrable;
}

void
RestoreTransactionCharacteristics(const SavedTransactionCharacteristics *s)
{
	XactIsoLevel = s->save_XactIsoLevel;
	XactReadOnly = s->save_XactReadOnly;
	XactDeferrable = s->save_XactDeferrable;
}

void
SetTxnWithPGRel(void)
{
	TransactionState s = CurrentTransactionState;
	/*
	 * YB doesn't support subtransactions for now and only top level transaction is committed.
	 * So the isYBTxnWithPostgresRel flag must be set on current and all top level transactions.
	 */
	while (s != NULL && !s->isYBTxnWithPostgresRel)
	{
		s->isYBTxnWithPostgresRel = true;
		s = s->parent;
	}
}

bool
IsCurrentTxnWithPGRel(void)
{
	return CurrentTransactionState->isYBTxnWithPostgresRel;
}

/*
 *	CommitTransactionCommand
 */
void
CommitTransactionCommand(void)
{
	elog(DEBUG2, "CommitTransactionCommand");
	TransactionState s = CurrentTransactionState;
	SavedTransactionCharacteristics savetc;

	SaveTransactionCharacteristics(&savetc);

	/* TODO(jayant): add YB prefix to prevState. */
	TBlockState prevState = s->blockState;

	switch (s->blockState)
	{
			/*
			 * These shouldn't happen.  TBLOCK_DEFAULT means the previous
			 * StartTransactionCommand didn't set the STARTED state
			 * appropriately, while TBLOCK_PARALLEL_INPROGRESS should be ended
			 * by EndParallelWorkerTransaction(), not this function.
			 */
		case TBLOCK_DEFAULT:
		case TBLOCK_PARALLEL_INPROGRESS:
			elog(FATAL, "CommitTransactionCommand: unexpected state %s",
				 BlockStateAsString(s->blockState));
			break;

			/*
			 * If we aren't in a transaction block, just do our usual
			 * transaction commit, and return to the idle state.
			 */
		case TBLOCK_STARTED:
			CommitTransaction();
			s->blockState = TBLOCK_DEFAULT;
			break;

			/*
			 * We are completing a "BEGIN TRANSACTION" command, so we change
			 * to the "transaction block in progress" state and return.  (We
			 * assume the BEGIN did nothing to the database, so we need no
			 * CommandCounterIncrement.)
			 */
		case TBLOCK_BEGIN:
			s->blockState = TBLOCK_INPROGRESS;
			break;

			/*
			 * This is the case when we have finished executing a command
			 * someplace within a transaction block.  We increment the command
			 * counter and return.
			 */
		case TBLOCK_INPROGRESS:
		case TBLOCK_IMPLICIT_INPROGRESS:
		case TBLOCK_SUBINPROGRESS:
			CommandCounterIncrement();
			break;

			/*
			 * We are completing a "COMMIT" command.  Do it and return to the
			 * idle state.
			 */
		case TBLOCK_END:
			CommitTransaction();
			s->blockState = TBLOCK_DEFAULT;
			if (s->chain)
			{
				StartTransaction();
				s->blockState = TBLOCK_INPROGRESS;
				s->chain = false;
				RestoreTransactionCharacteristics(&savetc);
			}
			break;

			/*
			 * Here we are in the middle of a transaction block but one of the
			 * commands caused an abort so we do nothing but remain in the
			 * abort state.  Eventually we will get a ROLLBACK command.
			 */
		case TBLOCK_ABORT:
		case TBLOCK_SUBABORT:
			break;

			/*
			 * Here we were in an aborted transaction block and we just got
			 * the ROLLBACK command from the user, so clean up the
			 * already-aborted transaction and return to the idle state.
			 */
		case TBLOCK_ABORT_END:
			CleanupTransaction();
			s->blockState = TBLOCK_DEFAULT;
			if (s->chain)
			{
				StartTransaction();
				s->blockState = TBLOCK_INPROGRESS;
				s->chain = false;
				RestoreTransactionCharacteristics(&savetc);
			}
			break;

			/*
			 * Here we were in a perfectly good transaction block but the user
			 * told us to ROLLBACK anyway.  We have to abort the transaction
			 * and then clean up.
			 */
		case TBLOCK_ABORT_PENDING:
			AbortTransaction();
			CleanupTransaction();
			s->blockState = TBLOCK_DEFAULT;
			if (s->chain)
			{
				StartTransaction();
				s->blockState = TBLOCK_INPROGRESS;
				s->chain = false;
				RestoreTransactionCharacteristics(&savetc);
			}
			break;

			/*
			 * We are completing a "PREPARE TRANSACTION" command.  Do it and
			 * return to the idle state.
			 */
		case TBLOCK_PREPARE:
			PrepareTransaction();
			s->blockState = TBLOCK_DEFAULT;
			break;

			/*
			 * We were just issued a SAVEPOINT inside a transaction block.
			 * Start a subtransaction.  (DefineSavepoint already did
			 * PushTransaction, so as to have someplace to put the SUBBEGIN
			 * state.)
			 */
		case TBLOCK_SUBBEGIN:
			StartSubTransaction();
			s->blockState = TBLOCK_SUBINPROGRESS;
			break;

			/*
			 * We were issued a RELEASE command, so we end the current
			 * subtransaction and return to the parent transaction. The parent
			 * might be ended too, so repeat till we find an INPROGRESS
			 * transaction or subtransaction.
			 */
		case TBLOCK_SUBRELEASE:
			do
			{
				CommitSubTransaction();
				s = CurrentTransactionState;	/* changed by pop */
			} while (s->blockState == TBLOCK_SUBRELEASE);

			Assert(s->blockState == TBLOCK_INPROGRESS ||
				   s->blockState == TBLOCK_SUBINPROGRESS);
			break;

			/*
			 * We were issued a COMMIT, so we end the current subtransaction
			 * hierarchy and perform final commit. We do this by rolling up
			 * any subtransactions into their parent, which leads to O(N^2)
			 * operations with respect to resource owners - this isn't that
			 * bad until we approach a thousands of savepoints but is
			 * necessary for correctness should after triggers create new
			 * resource owners.
			 */
		case TBLOCK_SUBCOMMIT:
			do
			{
				CommitSubTransaction();
				s = CurrentTransactionState;	/* changed by pop */
			} while (s->blockState == TBLOCK_SUBCOMMIT);
			/* If we had a COMMIT command, finish off the main xact too */
			if (s->blockState == TBLOCK_END)
			{
				Assert(s->parent == NULL);
				CommitTransaction();
				s->blockState = TBLOCK_DEFAULT;
				if (s->chain)
				{
					StartTransaction();
					s->blockState = TBLOCK_INPROGRESS;
					s->chain = false;
					RestoreTransactionCharacteristics(&savetc);
				}
			}
			else if (s->blockState == TBLOCK_PREPARE)
			{
				Assert(s->parent == NULL);
				PrepareTransaction();
				s->blockState = TBLOCK_DEFAULT;
			}
			else
				elog(ERROR, "CommitTransactionCommand: unexpected state %s",
					 BlockStateAsString(s->blockState));
			break;

			/*
			 * The current already-failed subtransaction is ending due to a
			 * ROLLBACK or ROLLBACK TO command, so pop it and recursively
			 * examine the parent (which could be in any of several states).
			 */
		case TBLOCK_SUBABORT_END:
			CleanupSubTransaction();
			CommitTransactionCommand();
			break;

			/*
			 * As above, but it's not dead yet, so abort first.
			 */
		case TBLOCK_SUBABORT_PENDING:
			AbortSubTransaction();
			CleanupSubTransaction();
			CommitTransactionCommand();
			break;

			/*
			 * The current subtransaction is the target of a ROLLBACK TO
			 * command.  Abort and pop it, then start a new subtransaction
			 * with the same name.
			 */
		case TBLOCK_SUBRESTART:
			{
				char	   *name;
				int			savepointLevel;

				/* save name and keep Cleanup from freeing it */
				name = s->name;
				s->name = NULL;
				savepointLevel = s->savepointLevel;

				AbortSubTransaction();
				CleanupSubTransaction();

				DefineSavepoint(NULL);
				s = CurrentTransactionState;	/* changed by push */
				s->name = name;
				s->savepointLevel = savepointLevel;

				/* This is the same as TBLOCK_SUBBEGIN case */
				AssertState(s->blockState == TBLOCK_SUBBEGIN);
				StartSubTransaction();
				s->blockState = TBLOCK_SUBINPROGRESS;
			}
			break;

			/*
			 * Same as above, but the subtransaction had already failed, so we
			 * don't need AbortSubTransaction.
			 */
		case TBLOCK_SUBABORT_RESTART:
			{
				char	   *name;
				int			savepointLevel;

				/* save name and keep Cleanup from freeing it */
				name = s->name;
				s->name = NULL;
				savepointLevel = s->savepointLevel;

				CleanupSubTransaction();

				DefineSavepoint(NULL);
				s = CurrentTransactionState;	/* changed by push */
				s->name = name;
				s->savepointLevel = savepointLevel;

				/* This is the same as TBLOCK_SUBBEGIN case */
				AssertState(s->blockState == TBLOCK_SUBBEGIN);
				StartSubTransaction();
				s->blockState = TBLOCK_SUBINPROGRESS;
			}
			break;
	}

	/* Update the session parameter values in the shared memory */
	if (YbIsClientYsqlConnMgr())
	{
		/*
		 * At the end of a single query transaction (when autocommit is enabled)
		 * the blockState will be TBLOCK_STARTED.
		 * At the end of a normal transaction (when autocommit is disabled)
		 * the blockState will be TBLOCK_END.
		 * So in the case of TBLOCK_ENDand TBLOCK_STARTED,
		 *
		 * UpdateSharedMemory is called at the end of a transaction.
		 * i.e. TBLOCK_END and TBLOCK_STARTED, not TBLOCK_BEGIN.
		 * This is done to update the shared memory in case any
		 * session parameter might have changed.
		 *
		 * YbCleanChangedSessionParameter is called both at the beginning
		 * and at the end of the transaction (after updating shared memory) .
		 * YbCleanChangedSessionParameter basically cleans the local cach, so
		 * when a logical connection is attached to a new physical connection,
		 * the cach needs to be cleaned. Also once this cach has been used to
		 * update the shared memory (YbUpdateSharedMemory) this cach should be
		 * cleaned.
		 */
		switch (prevState)
		{
			case TBLOCK_END:	 /* COMMIT received */
			case TBLOCK_STARTED: /* running single-query transaction */
				/* Copy the session parameter from the local memory to the
				 * shared memory */
				YbUpdateSharedMemory();

				YbCleanChangedSessionParameters();
				break;
			case TBLOCK_BEGIN:
				YbCleanChangedSessionParameters();
				break;
			default:
				/* do nothing for sub transaction, process changed session
				 * parameters only at the end of the transaction. */
				break;
		}
	}
}

/*
 *	AbortCurrentTransaction
 */
void
AbortCurrentTransaction(void)
{
	TransactionState s = CurrentTransactionState;

	switch (s->blockState)
	{
		case TBLOCK_DEFAULT:
			if (s->state == TRANS_DEFAULT)
			{
				/* we are idle, so nothing to do */
			}
			else
			{
				/*
				 * We can get here after an error during transaction start
				 * (state will be TRANS_START).  Need to clean up the
				 * incompletely started transaction.  First, adjust the
				 * low-level state to suppress warning message from
				 * AbortTransaction.
				 */
				if (s->state == TRANS_START)
					s->state = TRANS_INPROGRESS;
				AbortTransaction();
				CleanupTransaction();
			}
			break;

			/*
			 * If we aren't in a transaction block, we just do the basic abort
			 * & cleanup transaction.  For this purpose, we treat an implicit
			 * transaction block as if it were a simple statement.
			 */
		case TBLOCK_STARTED:
		case TBLOCK_IMPLICIT_INPROGRESS:
			AbortTransaction();
			CleanupTransaction();
			s->blockState = TBLOCK_DEFAULT;
			break;

			/*
			 * If we are in TBLOCK_BEGIN it means something screwed up right
			 * after reading "BEGIN TRANSACTION".  We assume that the user
			 * will interpret the error as meaning the BEGIN failed to get him
			 * into a transaction block, so we should abort and return to idle
			 * state.
			 */
		case TBLOCK_BEGIN:
			AbortTransaction();
			CleanupTransaction();
			s->blockState = TBLOCK_DEFAULT;
			break;

			/*
			 * We are somewhere in a transaction block and we've gotten a
			 * failure, so we abort the transaction and set up the persistent
			 * ABORT state.  We will stay in ABORT until we get a ROLLBACK.
			 */
		case TBLOCK_INPROGRESS:
		case TBLOCK_PARALLEL_INPROGRESS:
			AbortTransaction();
			s->blockState = TBLOCK_ABORT;
			/* CleanupTransaction happens when we exit TBLOCK_ABORT_END */
			break;

			/*
			 * Here, we failed while trying to COMMIT.  Clean up the
			 * transaction and return to idle state (we do not want to stay in
			 * the transaction).
			 */
		case TBLOCK_END:
			AbortTransaction();
			CleanupTransaction();
			s->blockState = TBLOCK_DEFAULT;
			break;

			/*
			 * Here, we are already in an aborted transaction state and are
			 * waiting for a ROLLBACK, but for some reason we failed again! So
			 * we just remain in the abort state.
			 */
		case TBLOCK_ABORT:
		case TBLOCK_SUBABORT:
			break;

			/*
			 * We are in a failed transaction and we got the ROLLBACK command.
			 * We have already aborted, we just need to cleanup and go to idle
			 * state.
			 */
		case TBLOCK_ABORT_END:
			CleanupTransaction();
			s->blockState = TBLOCK_DEFAULT;
			break;

			/*
			 * We are in a live transaction and we got a ROLLBACK command.
			 * Abort, cleanup, go to idle state.
			 */
		case TBLOCK_ABORT_PENDING:
			AbortTransaction();
			CleanupTransaction();
			s->blockState = TBLOCK_DEFAULT;
			break;

			/*
			 * Here, we failed while trying to PREPARE.  Clean up the
			 * transaction and return to idle state (we do not want to stay in
			 * the transaction).
			 */
		case TBLOCK_PREPARE:
			AbortTransaction();
			CleanupTransaction();
			s->blockState = TBLOCK_DEFAULT;
			break;

			/*
			 * We got an error inside a subtransaction.  Abort just the
			 * subtransaction, and go to the persistent SUBABORT state until
			 * we get ROLLBACK.
			 */
		case TBLOCK_SUBINPROGRESS:
			AbortSubTransaction();
			s->blockState = TBLOCK_SUBABORT;
			break;

			/*
			 * If we failed while trying to create a subtransaction, clean up
			 * the broken subtransaction and abort the parent.  The same
			 * applies if we get a failure while ending a subtransaction.
			 */
		case TBLOCK_SUBBEGIN:
		case TBLOCK_SUBRELEASE:
		case TBLOCK_SUBCOMMIT:
		case TBLOCK_SUBABORT_PENDING:
		case TBLOCK_SUBRESTART:
			AbortSubTransaction();
			CleanupSubTransaction();
			AbortCurrentTransaction();
			break;

			/*
			 * Same as above, except the Abort() was already done.
			 */
		case TBLOCK_SUBABORT_END:
		case TBLOCK_SUBABORT_RESTART:
			CleanupSubTransaction();
			AbortCurrentTransaction();
			break;
	}
}

/*
 *	PreventInTransactionBlock
 *
 *	This routine is to be called by statements that must not run inside
 *	a transaction block, typically because they have non-rollback-able
 *	side effects or do internal commits.
 *
 *	If this routine completes successfully, then the calling statement is
 *	guaranteed that if it completes without error, its results will be
 *	committed immediately.
 *
 *	If we have already started a transaction block, issue an error; also issue
 *	an error if we appear to be running inside a user-defined function (which
 *	could issue more commands and possibly cause a failure after the statement
 *	completes).  Subtransactions are verboten too.
 *
 *	We must also set XACT_FLAGS_NEEDIMMEDIATECOMMIT in MyXactFlags, to ensure
 *	that postgres.c follows through by committing after the statement is done.
 *
 *	isTopLevel: passed down from ProcessUtility to determine whether we are
 *	inside a function.  (We will always fail if this is false, but it's
 *	convenient to centralize the check here instead of making callers do it.)
 *	stmtType: statement type name, for error messages.
 */
void
PreventInTransactionBlock(bool isTopLevel, const char *stmtType)
{
	/*
	 * xact block already started?
	 */
	if (IsTransactionBlock())
		ereport(ERROR,
				(errcode(ERRCODE_ACTIVE_SQL_TRANSACTION),
		/* translator: %s represents an SQL statement name */
				 errmsg("%s cannot run inside a transaction block",
						stmtType)));

	/*
	 * subtransaction?
	 */
	if (IsSubTransaction())
		ereport(ERROR,
				(errcode(ERRCODE_ACTIVE_SQL_TRANSACTION),
		/* translator: %s represents an SQL statement name */
				 errmsg("%s cannot run inside a subtransaction",
						stmtType)));

	/*
	 * inside a pipeline that has started an implicit transaction?
	 */
	if (MyXactFlags & XACT_FLAGS_PIPELINING)
		ereport(ERROR,
				(errcode(ERRCODE_ACTIVE_SQL_TRANSACTION),
		/* translator: %s represents an SQL statement name */
				 errmsg("%s cannot be executed within a pipeline",
						stmtType)));

	/*
	 * inside a function call?
	 */
	if (!isTopLevel)
		ereport(ERROR,
				(errcode(ERRCODE_ACTIVE_SQL_TRANSACTION),
		/* translator: %s represents an SQL statement name */
				 errmsg("%s cannot be executed from a function", stmtType)));

	/* If we got past IsTransactionBlock test, should be in default state */
	if (CurrentTransactionState->blockState != TBLOCK_DEFAULT &&
		CurrentTransactionState->blockState != TBLOCK_STARTED)
		elog(FATAL, "cannot prevent transaction chain");

	/* All okay.  Set the flag to make sure the right thing happens later. */
	MyXactFlags |= XACT_FLAGS_NEEDIMMEDIATECOMMIT;
}

/*
 *	WarnNoTransactionBlock
 *	RequireTransactionBlock
 *
 *	These two functions allow for warnings or errors if a command is executed
 *	outside of a transaction block.  This is useful for commands that have no
 *	effects that persist past transaction end (and so calling them outside a
 *	transaction block is presumably an error).  DECLARE CURSOR is an example.
 *	While top-level transaction control commands (BEGIN/COMMIT/ABORT) and SET
 *	that have no effect issue warnings, all other no-effect commands generate
 *	errors.
 *
 *	If we appear to be running inside a user-defined function, we do not
 *	issue anything, since the function could issue more commands that make
 *	use of the current statement's results.  Likewise subtransactions.
 *	Thus these are inverses for PreventInTransactionBlock.
 *
 *	isTopLevel: passed down from ProcessUtility to determine whether we are
 *	inside a function.
 *	stmtType: statement type name, for warning or error messages.
 */
void
WarnNoTransactionBlock(bool isTopLevel, const char *stmtType)
{
	CheckTransactionBlock(isTopLevel, false, stmtType);
}

void
RequireTransactionBlock(bool isTopLevel, const char *stmtType)
{
	CheckTransactionBlock(isTopLevel, true, stmtType);
}

/*
 * This is the implementation of the above two.
 */
static void
CheckTransactionBlock(bool isTopLevel, bool throwError, const char *stmtType)
{
	/*
	 * xact block already started?
	 */
	if (IsTransactionBlock())
		return;

	/*
	 * subtransaction?
	 */
	if (IsSubTransaction())
		return;

	/*
	 * inside a function call?
	 */
	if (!isTopLevel)
		return;

	ereport(throwError ? ERROR : WARNING,
			(errcode(ERRCODE_NO_ACTIVE_SQL_TRANSACTION),
	/* translator: %s represents an SQL statement name */
			 errmsg("%s can only be used in transaction blocks",
					stmtType)));
}

/*
 *	IsInTransactionBlock
 *
 *	This routine is for statements that need to behave differently inside
 *	a transaction block than when running as single commands.  ANALYZE is
 *	currently the only example.
 *
 *	If this routine returns "false", then the calling statement is allowed
 *	to perform internal transaction-commit-and-start cycles; there is not a
 *	risk of messing up any transaction already in progress.  (Note that this
 *	is not the identical guarantee provided by PreventInTransactionBlock,
 *	since we will not force a post-statement commit.)
 *
 *	isTopLevel: passed down from ProcessUtility to determine whether we are
 *	inside a function.
 */
bool
IsInTransactionBlock(bool isTopLevel)
{
	/*
	 * Return true on same conditions that would make
	 * PreventInTransactionBlock error out
	 */
	if (IsTransactionBlock())
		return true;

	if (IsSubTransaction())
		return true;

	if (MyXactFlags & XACT_FLAGS_PIPELINING)
		return true;

	if (!isTopLevel)
		return true;

	return false;
}


/*
 * Register or deregister callback functions for start- and end-of-xact
 * operations.
 *
 * These functions are intended for use by dynamically loaded modules.
 * For built-in modules we generally just hardwire the appropriate calls
 * (mainly because it's easier to control the order that way, where needed).
 *
 * At transaction end, the callback occurs post-commit or post-abort, so the
 * callback functions can only do noncritical cleanup.
 */
void
RegisterXactCallback(XactCallback callback, void *arg)
{
	XactCallbackItem *item;

	item = (XactCallbackItem *)
		MemoryContextAlloc(TopMemoryContext, sizeof(XactCallbackItem));
	item->callback = callback;
	item->arg = arg;
	item->next = Xact_callbacks;
	Xact_callbacks = item;
}

void
UnregisterXactCallback(XactCallback callback, void *arg)
{
	XactCallbackItem *item;
	XactCallbackItem *prev;

	prev = NULL;
	for (item = Xact_callbacks; item; prev = item, item = item->next)
	{
		if (item->callback == callback && item->arg == arg)
		{
			if (prev)
				prev->next = item->next;
			else
				Xact_callbacks = item->next;
			pfree(item);
			break;
		}
	}
}

static void
CallXactCallbacks(XactEvent event)
{
	XactCallbackItem *item;

	for (item = Xact_callbacks; item; item = item->next)
		item->callback(event, item->arg);
}


/*
 * Register or deregister callback functions for start- and end-of-subxact
 * operations.
 *
 * Pretty much same as above, but for subtransaction events.
 *
 * At subtransaction end, the callback occurs post-subcommit or post-subabort,
 * so the callback functions can only do noncritical cleanup.  At
 * subtransaction start, the callback is called when the subtransaction has
 * finished initializing.
 */
void
RegisterSubXactCallback(SubXactCallback callback, void *arg)
{
	SubXactCallbackItem *item;

	item = (SubXactCallbackItem *)
		MemoryContextAlloc(TopMemoryContext, sizeof(SubXactCallbackItem));
	item->callback = callback;
	item->arg = arg;
	item->next = SubXact_callbacks;
	SubXact_callbacks = item;
}

void
UnregisterSubXactCallback(SubXactCallback callback, void *arg)
{
	SubXactCallbackItem *item;
	SubXactCallbackItem *prev;

	prev = NULL;
	for (item = SubXact_callbacks; item; prev = item, item = item->next)
	{
		if (item->callback == callback && item->arg == arg)
		{
			if (prev)
				prev->next = item->next;
			else
				SubXact_callbacks = item->next;
			pfree(item);
			break;
		}
	}
}

static void
CallSubXactCallbacks(SubXactEvent event,
					 SubTransactionId mySubid,
					 SubTransactionId parentSubid)
{
	SubXactCallbackItem *item;

	for (item = SubXact_callbacks; item; item = item->next)
		item->callback(event, mySubid, parentSubid, item->arg);
}


/* ----------------------------------------------------------------
 *					   transaction block support
 * ----------------------------------------------------------------
 */

/*
 *	BeginTransactionBlock
 *		This executes a BEGIN command.
 */
void
BeginTransactionBlock(void)
{
	elog(DEBUG2, "BeginTransactionBlock");
	TransactionState s = CurrentTransactionState;

	switch (s->blockState)
	{
			/*
			 * We are not inside a transaction block, so allow one to begin.
			 */
		case TBLOCK_STARTED:
			s->blockState = TBLOCK_BEGIN;
			break;

			/*
			 * BEGIN converts an implicit transaction block to a regular one.
			 * (Note that we allow this even if we've already done some
			 * commands, which is a bit odd but matches historical practice.)
			 */
		case TBLOCK_IMPLICIT_INPROGRESS:
			s->blockState = TBLOCK_BEGIN;
			break;

			/*
			 * Already a transaction block in progress.
			 */
		case TBLOCK_INPROGRESS:
		case TBLOCK_PARALLEL_INPROGRESS:
		case TBLOCK_SUBINPROGRESS:
		case TBLOCK_ABORT:
		case TBLOCK_SUBABORT:
			ereport(WARNING,
					(errcode(ERRCODE_ACTIVE_SQL_TRANSACTION),
					 errmsg("there is already a transaction in progress")));
			break;

			/* These cases are invalid. */
		case TBLOCK_DEFAULT:
		case TBLOCK_BEGIN:
		case TBLOCK_SUBBEGIN:
		case TBLOCK_END:
		case TBLOCK_SUBRELEASE:
		case TBLOCK_SUBCOMMIT:
		case TBLOCK_ABORT_END:
		case TBLOCK_SUBABORT_END:
		case TBLOCK_ABORT_PENDING:
		case TBLOCK_SUBABORT_PENDING:
		case TBLOCK_SUBRESTART:
		case TBLOCK_SUBABORT_RESTART:
		case TBLOCK_PREPARE:
			elog(FATAL, "BeginTransactionBlock: unexpected state %s",
				 BlockStateAsString(s->blockState));
			break;
	}

	/* YB: Notify pggate that we are within a txn block. */
	if (IsYugaByteEnabled())
		HandleYBStatus(YBCPgSetInTxnBlock(true));
}

/*
 *	PrepareTransactionBlock
 *		This executes a PREPARE command.
 *
 * Since PREPARE may actually do a ROLLBACK, the result indicates what
 * happened: true for PREPARE, false for ROLLBACK.
 *
 * Note that we don't actually do anything here except change blockState.
 * The real work will be done in the upcoming PrepareTransaction().
 * We do it this way because it's not convenient to change memory context,
 * resource owner, etc while executing inside a Portal.
 */
bool
PrepareTransactionBlock(const char *gid)
{
	TransactionState s;
	bool		result;

	/* Set up to commit the current transaction */
	result = EndTransactionBlock(false);

	/* If successful, change outer tblock state to PREPARE */
	if (result)
	{
		s = CurrentTransactionState;

		while (s->parent != NULL)
			s = s->parent;

		if (s->blockState == TBLOCK_END)
		{
			/* Save GID where PrepareTransaction can find it again */
			prepareGID = MemoryContextStrdup(TopTransactionContext, gid);

			s->blockState = TBLOCK_PREPARE;
		}
		else
		{
			/*
			 * ignore case where we are not in a transaction;
			 * EndTransactionBlock already issued a warning.
			 */
			Assert(s->blockState == TBLOCK_STARTED ||
				   s->blockState == TBLOCK_IMPLICIT_INPROGRESS);
			/* Don't send back a PREPARE result tag... */
			result = false;
		}
	}

	return result;
}

/*
 *	EndTransactionBlock
 *		This executes a COMMIT command.
 *
 * Since COMMIT may actually do a ROLLBACK, the result indicates what
 * happened: true for COMMIT, false for ROLLBACK.
 *
 * Note that we don't actually do anything here except change blockState.
 * The real work will be done in the upcoming CommitTransactionCommand().
 * We do it this way because it's not convenient to change memory context,
 * resource owner, etc while executing inside a Portal.
 */
bool
EndTransactionBlock(bool chain)
{
	TransactionState s = CurrentTransactionState;
	bool		result = false;

	switch (s->blockState)
	{
			/*
			 * We are in a transaction block, so tell CommitTransactionCommand
			 * to COMMIT.
			 */
		case TBLOCK_INPROGRESS:
			s->blockState = TBLOCK_END;
			result = true;
			break;

			/*
			 * We are in an implicit transaction block.  If AND CHAIN was
			 * specified, error.  Otherwise commit, but issue a warning
			 * because there was no explicit BEGIN before this.
			 */
		case TBLOCK_IMPLICIT_INPROGRESS:
			if (chain)
				ereport(ERROR,
						(errcode(ERRCODE_NO_ACTIVE_SQL_TRANSACTION),
				/* translator: %s represents an SQL statement name */
						 errmsg("%s can only be used in transaction blocks",
								"COMMIT AND CHAIN")));
			else
				ereport(WARNING,
						(errcode(ERRCODE_NO_ACTIVE_SQL_TRANSACTION),
						 errmsg("there is no transaction in progress")));
			s->blockState = TBLOCK_END;
			result = true;
			break;

			/*
			 * We are in a failed transaction block.  Tell
			 * CommitTransactionCommand it's time to exit the block.
			 */
		case TBLOCK_ABORT:
			s->blockState = TBLOCK_ABORT_END;
			break;

			/*
			 * We are in a live subtransaction block.  Set up to subcommit all
			 * open subtransactions and then commit the main transaction.
			 */
		case TBLOCK_SUBINPROGRESS:
			while (s->parent != NULL)
			{
				if (s->blockState == TBLOCK_SUBINPROGRESS)
					s->blockState = TBLOCK_SUBCOMMIT;
				else
					elog(FATAL, "EndTransactionBlock: unexpected state %s",
						 BlockStateAsString(s->blockState));
				s = s->parent;
			}
			if (s->blockState == TBLOCK_INPROGRESS)
				s->blockState = TBLOCK_END;
			else
				elog(FATAL, "EndTransactionBlock: unexpected state %s",
					 BlockStateAsString(s->blockState));
			result = true;
			break;

			/*
			 * Here we are inside an aborted subtransaction.  Treat the COMMIT
			 * as ROLLBACK: set up to abort everything and exit the main
			 * transaction.
			 */
		case TBLOCK_SUBABORT:
			while (s->parent != NULL)
			{
				if (s->blockState == TBLOCK_SUBINPROGRESS)
					s->blockState = TBLOCK_SUBABORT_PENDING;
				else if (s->blockState == TBLOCK_SUBABORT)
					s->blockState = TBLOCK_SUBABORT_END;
				else
					elog(FATAL, "EndTransactionBlock: unexpected state %s",
						 BlockStateAsString(s->blockState));
				s = s->parent;
			}
			if (s->blockState == TBLOCK_INPROGRESS)
				s->blockState = TBLOCK_ABORT_PENDING;
			else if (s->blockState == TBLOCK_ABORT)
				s->blockState = TBLOCK_ABORT_END;
			else
				elog(FATAL, "EndTransactionBlock: unexpected state %s",
					 BlockStateAsString(s->blockState));
			break;

			/*
			 * The user issued COMMIT when not inside a transaction.  For
			 * COMMIT without CHAIN, issue a WARNING, staying in
			 * TBLOCK_STARTED state.  The upcoming call to
			 * CommitTransactionCommand() will then close the transaction and
			 * put us back into the default state.  For COMMIT AND CHAIN,
			 * error.
			 */
		case TBLOCK_STARTED:
			if (chain)
				ereport(ERROR,
						(errcode(ERRCODE_NO_ACTIVE_SQL_TRANSACTION),
				/* translator: %s represents an SQL statement name */
						 errmsg("%s can only be used in transaction blocks",
								"COMMIT AND CHAIN")));
			else
				ereport(WARNING,
						(errcode(ERRCODE_NO_ACTIVE_SQL_TRANSACTION),
						 errmsg("there is no transaction in progress")));
			result = true;
			break;

			/*
			 * The user issued a COMMIT that somehow ran inside a parallel
			 * worker.  We can't cope with that.
			 */
		case TBLOCK_PARALLEL_INPROGRESS:
			ereport(FATAL,
					(errcode(ERRCODE_INVALID_TRANSACTION_STATE),
					 errmsg("cannot commit during a parallel operation")));
			break;

			/* These cases are invalid. */
		case TBLOCK_DEFAULT:
		case TBLOCK_BEGIN:
		case TBLOCK_SUBBEGIN:
		case TBLOCK_END:
		case TBLOCK_SUBRELEASE:
		case TBLOCK_SUBCOMMIT:
		case TBLOCK_ABORT_END:
		case TBLOCK_SUBABORT_END:
		case TBLOCK_ABORT_PENDING:
		case TBLOCK_SUBABORT_PENDING:
		case TBLOCK_SUBRESTART:
		case TBLOCK_SUBABORT_RESTART:
		case TBLOCK_PREPARE:
			elog(FATAL, "EndTransactionBlock: unexpected state %s",
				 BlockStateAsString(s->blockState));
			break;
	}

	Assert(s->blockState == TBLOCK_STARTED ||
		   s->blockState == TBLOCK_END ||
		   s->blockState == TBLOCK_ABORT_END ||
		   s->blockState == TBLOCK_ABORT_PENDING);

	s->chain = chain;

	return result;
}

/*
 *	UserAbortTransactionBlock
 *		This executes a ROLLBACK command.
 *
 * As above, we don't actually do anything here except change blockState.
 */
void
UserAbortTransactionBlock(bool chain)
{
	TransactionState s = CurrentTransactionState;

	switch (s->blockState)
	{
			/*
			 * We are inside a transaction block and we got a ROLLBACK command
			 * from the user, so tell CommitTransactionCommand to abort and
			 * exit the transaction block.
			 */
		case TBLOCK_INPROGRESS:
			s->blockState = TBLOCK_ABORT_PENDING;
			break;

			/*
			 * We are inside a failed transaction block and we got a ROLLBACK
			 * command from the user.  Abort processing is already done, so
			 * CommitTransactionCommand just has to cleanup and go back to
			 * idle state.
			 */
		case TBLOCK_ABORT:
			s->blockState = TBLOCK_ABORT_END;
			break;

			/*
			 * We are inside a subtransaction.  Mark everything up to top
			 * level as exitable.
			 */
		case TBLOCK_SUBINPROGRESS:
		case TBLOCK_SUBABORT:
			while (s->parent != NULL)
			{
				if (s->blockState == TBLOCK_SUBINPROGRESS)
					s->blockState = TBLOCK_SUBABORT_PENDING;
				else if (s->blockState == TBLOCK_SUBABORT)
					s->blockState = TBLOCK_SUBABORT_END;
				else
					elog(FATAL, "UserAbortTransactionBlock: unexpected state %s",
						 BlockStateAsString(s->blockState));
				s = s->parent;
			}
			if (s->blockState == TBLOCK_INPROGRESS)
				s->blockState = TBLOCK_ABORT_PENDING;
			else if (s->blockState == TBLOCK_ABORT)
				s->blockState = TBLOCK_ABORT_END;
			else
				elog(FATAL, "UserAbortTransactionBlock: unexpected state %s",
					 BlockStateAsString(s->blockState));
			break;

			/*
			 * The user issued ABORT when not inside a transaction.  For
			 * ROLLBACK without CHAIN, issue a WARNING and go to abort state.
			 * The upcoming call to CommitTransactionCommand() will then put
			 * us back into the default state.  For ROLLBACK AND CHAIN, error.
			 *
			 * We do the same thing with ABORT inside an implicit transaction,
			 * although in this case we might be rolling back actual database
			 * state changes.  (It's debatable whether we should issue a
			 * WARNING in this case, but we have done so historically.)
			 */
		case TBLOCK_STARTED:
		case TBLOCK_IMPLICIT_INPROGRESS:
			if (chain)
				ereport(ERROR,
						(errcode(ERRCODE_NO_ACTIVE_SQL_TRANSACTION),
				/* translator: %s represents an SQL statement name */
						 errmsg("%s can only be used in transaction blocks",
								"ROLLBACK AND CHAIN")));
			else
				ereport(WARNING,
						(errcode(ERRCODE_NO_ACTIVE_SQL_TRANSACTION),
						 errmsg("there is no transaction in progress")));
			s->blockState = TBLOCK_ABORT_PENDING;
			break;

			/*
			 * The user issued an ABORT that somehow ran inside a parallel
			 * worker.  We can't cope with that.
			 */
		case TBLOCK_PARALLEL_INPROGRESS:
			ereport(FATAL,
					(errcode(ERRCODE_INVALID_TRANSACTION_STATE),
					 errmsg("cannot abort during a parallel operation")));
			break;

			/* These cases are invalid. */
		case TBLOCK_DEFAULT:
		case TBLOCK_BEGIN:
		case TBLOCK_SUBBEGIN:
		case TBLOCK_END:
		case TBLOCK_SUBRELEASE:
		case TBLOCK_SUBCOMMIT:
		case TBLOCK_ABORT_END:
		case TBLOCK_SUBABORT_END:
		case TBLOCK_ABORT_PENDING:
		case TBLOCK_SUBABORT_PENDING:
		case TBLOCK_SUBRESTART:
		case TBLOCK_SUBABORT_RESTART:
		case TBLOCK_PREPARE:
			elog(FATAL, "UserAbortTransactionBlock: unexpected state %s",
				 BlockStateAsString(s->blockState));
			break;
	}

	Assert(s->blockState == TBLOCK_ABORT_END ||
		   s->blockState == TBLOCK_ABORT_PENDING);

	s->chain = chain;
}

/*
 * BeginImplicitTransactionBlock
 *		Start an implicit transaction block if we're not already in one.
 *
 * Unlike BeginTransactionBlock, this is called directly from the main loop
 * in postgres.c, not within a Portal.  So we can just change blockState
 * without a lot of ceremony.  We do not expect caller to do
 * CommitTransactionCommand/StartTransactionCommand.
 */
void
BeginImplicitTransactionBlock(void)
{
	TransactionState s = CurrentTransactionState;

	/*
	 * If we are in STARTED state (that is, no transaction block is open),
	 * switch to IMPLICIT_INPROGRESS state, creating an implicit transaction
	 * block.
	 *
	 * For caller convenience, we consider all other transaction states as
	 * legal here; otherwise the caller would need its own state check, which
	 * seems rather pointless.
	 */
	if (s->blockState == TBLOCK_STARTED)
		s->blockState = TBLOCK_IMPLICIT_INPROGRESS;

	/* YB: Notify pggate that we are within an (implicit) txn block. */
	if (IsYugaByteEnabled())
		HandleYBStatus(YBCPgSetInTxnBlock(true));
}

/*
 * EndImplicitTransactionBlock
 *		End an implicit transaction block, if we're in one.
 *
 * Like EndTransactionBlock, we just make any needed blockState change here.
 * The real work will be done in the upcoming CommitTransactionCommand().
 */
void
EndImplicitTransactionBlock(void)
{
	TransactionState s = CurrentTransactionState;

	/*
	 * If we are in IMPLICIT_INPROGRESS state, switch back to STARTED state,
	 * allowing CommitTransactionCommand to commit whatever happened during
	 * the implicit transaction block as though it were a single statement.
	 *
	 * For caller convenience, we consider all other transaction states as
	 * legal here; otherwise the caller would need its own state check, which
	 * seems rather pointless.
	 */
	if (s->blockState == TBLOCK_IMPLICIT_INPROGRESS)
		s->blockState = TBLOCK_STARTED;
}

/*
 * DefineSavepoint
 *		This executes a SAVEPOINT command.
 */
void
DefineSavepoint(const char *name)
{
	TransactionState s = CurrentTransactionState;

	/*
	 * Workers synchronize transaction state at the beginning of each parallel
	 * operation, so we can't account for new subtransactions after that
	 * point.  (Note that this check will certainly error out if s->blockState
	 * is TBLOCK_PARALLEL_INPROGRESS, so we can treat that as an invalid case
	 * below.)
	 */
	if (IsInParallelMode())
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TRANSACTION_STATE),
				 errmsg("cannot define savepoints during a parallel operation")));

	switch (s->blockState)
	{
		case TBLOCK_INPROGRESS:
		case TBLOCK_SUBINPROGRESS:
			/* Normal subtransaction start */
			PushTransaction();
			s = CurrentTransactionState;	/* changed by push */
			elog(DEBUG2, "new sub txn created by savepoint, subtxn_id: %d",
					 s->subTransactionId);

			/*
			 * Savepoint names, like the TransactionState block itself, live
			 * in TopTransactionContext.
			 */
			if (name)
				s->name = MemoryContextStrdup(TopTransactionContext, name);
			break;

			/*
			 * We disallow savepoint commands in implicit transaction blocks.
			 * There would be no great difficulty in allowing them so far as
			 * this module is concerned, but a savepoint seems inconsistent
			 * with exec_simple_query's behavior of abandoning the whole query
			 * string upon error.  Also, the point of an implicit transaction
			 * block (as opposed to a regular one) is to automatically close
			 * after an error, so it's hard to see how a savepoint would fit
			 * into that.
			 *
			 * The error messages for this are phrased as if there were no
			 * active transaction block at all, which is historical but
			 * perhaps could be improved.
			 */
		case TBLOCK_IMPLICIT_INPROGRESS:
			ereport(ERROR,
					(errcode(ERRCODE_NO_ACTIVE_SQL_TRANSACTION),
			/* translator: %s represents an SQL statement name */
					 errmsg("%s can only be used in transaction blocks",
							"SAVEPOINT")));
			break;

			/* These cases are invalid. */
		case TBLOCK_DEFAULT:
		case TBLOCK_STARTED:
		case TBLOCK_BEGIN:
		case TBLOCK_PARALLEL_INPROGRESS:
		case TBLOCK_SUBBEGIN:
		case TBLOCK_END:
		case TBLOCK_SUBRELEASE:
		case TBLOCK_SUBCOMMIT:
		case TBLOCK_ABORT:
		case TBLOCK_SUBABORT:
		case TBLOCK_ABORT_END:
		case TBLOCK_SUBABORT_END:
		case TBLOCK_ABORT_PENDING:
		case TBLOCK_SUBABORT_PENDING:
		case TBLOCK_SUBRESTART:
		case TBLOCK_SUBABORT_RESTART:
		case TBLOCK_PREPARE:
			elog(FATAL, "DefineSavepoint: unexpected state %s",
				 BlockStateAsString(s->blockState));
			break;
	}
}

/*
 * ReleaseSavepoint
 *		This executes a RELEASE command.
 *
 * As above, we don't actually do anything here except change blockState.
 */
void
ReleaseSavepoint(const char *name)
{
	TransactionState s = CurrentTransactionState;
	TransactionState target,
				xact;

	/*
	 * Workers synchronize transaction state at the beginning of each parallel
	 * operation, so we can't account for transaction state change after that
	 * point.  (Note that this check will certainly error out if s->blockState
	 * is TBLOCK_PARALLEL_INPROGRESS, so we can treat that as an invalid case
	 * below.)
	 */
	if (IsInParallelMode())
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TRANSACTION_STATE),
				 errmsg("cannot release savepoints during a parallel operation")));

	switch (s->blockState)
	{
			/*
			 * We can't release a savepoint if there is no savepoint defined.
			 */
		case TBLOCK_INPROGRESS:
			ereport(ERROR,
					(errcode(ERRCODE_S_E_INVALID_SPECIFICATION),
					 errmsg("savepoint \"%s\" does not exist", name)));
			break;

		case TBLOCK_IMPLICIT_INPROGRESS:
			/* See comment about implicit transactions in DefineSavepoint */
			ereport(ERROR,
					(errcode(ERRCODE_NO_ACTIVE_SQL_TRANSACTION),
			/* translator: %s represents an SQL statement name */
					 errmsg("%s can only be used in transaction blocks",
							"RELEASE SAVEPOINT")));
			break;

			/*
			 * We are in a non-aborted subtransaction.  This is the only valid
			 * case.
			 */
		case TBLOCK_SUBINPROGRESS:
			break;

			/* These cases are invalid. */
		case TBLOCK_DEFAULT:
		case TBLOCK_STARTED:
		case TBLOCK_BEGIN:
		case TBLOCK_PARALLEL_INPROGRESS:
		case TBLOCK_SUBBEGIN:
		case TBLOCK_END:
		case TBLOCK_SUBRELEASE:
		case TBLOCK_SUBCOMMIT:
		case TBLOCK_ABORT:
		case TBLOCK_SUBABORT:
		case TBLOCK_ABORT_END:
		case TBLOCK_SUBABORT_END:
		case TBLOCK_ABORT_PENDING:
		case TBLOCK_SUBABORT_PENDING:
		case TBLOCK_SUBRESTART:
		case TBLOCK_SUBABORT_RESTART:
		case TBLOCK_PREPARE:
			elog(FATAL, "ReleaseSavepoint: unexpected state %s",
				 BlockStateAsString(s->blockState));
			break;
	}

	for (target = s; PointerIsValid(target); target = target->parent)
	{
		if (PointerIsValid(target->name) && strcmp(target->name, name) == 0)
			break;
	}

	if (!PointerIsValid(target))
		ereport(ERROR,
				(errcode(ERRCODE_S_E_INVALID_SPECIFICATION),
				 errmsg("savepoint \"%s\" does not exist", name)));

	/* disallow crossing savepoint level boundaries */
	if (target->savepointLevel != s->savepointLevel)
		ereport(ERROR,
				(errcode(ERRCODE_S_E_INVALID_SPECIFICATION),
				 errmsg("savepoint \"%s\" does not exist within current savepoint level", name)));

	/*
	 * Mark "commit pending" all subtransactions up to the target
	 * subtransaction.  The actual commits will happen when control gets to
	 * CommitTransactionCommand.
	 */
	xact = CurrentTransactionState;
	for (;;)
	{
		Assert(xact->blockState == TBLOCK_SUBINPROGRESS);
		xact->blockState = TBLOCK_SUBRELEASE;
		if (xact == target)
			break;
		xact = xact->parent;
		Assert(PointerIsValid(xact));
	}
}

/*
 * RollbackToSavepoint
 *		This executes a ROLLBACK TO <savepoint> command.
 *
 * As above, we don't actually do anything here except change blockState.
 */
void
RollbackToSavepoint(const char *name)
{
	TransactionState s = CurrentTransactionState;
	TransactionState target,
				xact;

	/*
	 * Workers synchronize transaction state at the beginning of each parallel
	 * operation, so we can't account for transaction state change after that
	 * point.  (Note that this check will certainly error out if s->blockState
	 * is TBLOCK_PARALLEL_INPROGRESS, so we can treat that as an invalid case
	 * below.)
	 */
	if (IsInParallelMode())
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TRANSACTION_STATE),
				 errmsg("cannot rollback to savepoints during a parallel operation")));

	switch (s->blockState)
	{
			/*
			 * We can't rollback to a savepoint if there is no savepoint
			 * defined.
			 */
		case TBLOCK_INPROGRESS:
		case TBLOCK_ABORT:
			ereport(ERROR,
					(errcode(ERRCODE_S_E_INVALID_SPECIFICATION),
					 errmsg("savepoint \"%s\" does not exist", name)));
			break;

		case TBLOCK_IMPLICIT_INPROGRESS:
			/* See comment about implicit transactions in DefineSavepoint */
			ereport(ERROR,
					(errcode(ERRCODE_NO_ACTIVE_SQL_TRANSACTION),
			/* translator: %s represents an SQL statement name */
					 errmsg("%s can only be used in transaction blocks",
							"ROLLBACK TO SAVEPOINT")));
			break;

			/*
			 * There is at least one savepoint, so proceed.
			 */
		case TBLOCK_SUBINPROGRESS:
		case TBLOCK_SUBABORT:
			break;

			/* These cases are invalid. */
		case TBLOCK_DEFAULT:
		case TBLOCK_STARTED:
		case TBLOCK_BEGIN:
		case TBLOCK_PARALLEL_INPROGRESS:
		case TBLOCK_SUBBEGIN:
		case TBLOCK_END:
		case TBLOCK_SUBRELEASE:
		case TBLOCK_SUBCOMMIT:
		case TBLOCK_ABORT_END:
		case TBLOCK_SUBABORT_END:
		case TBLOCK_ABORT_PENDING:
		case TBLOCK_SUBABORT_PENDING:
		case TBLOCK_SUBRESTART:
		case TBLOCK_SUBABORT_RESTART:
		case TBLOCK_PREPARE:
			elog(FATAL, "RollbackToSavepoint: unexpected state %s",
				 BlockStateAsString(s->blockState));
			break;
	}

	for (target = s; PointerIsValid(target); target = target->parent)
	{
		if (PointerIsValid(target->name) && strcmp(target->name, name) == 0)
			break;
	}

	if (!PointerIsValid(target))
		ereport(ERROR,
				(errcode(ERRCODE_S_E_INVALID_SPECIFICATION),
				 errmsg("savepoint \"%s\" does not exist", name)));

	/* disallow crossing savepoint level boundaries */
	if (target->savepointLevel != s->savepointLevel)
		ereport(ERROR,
				(errcode(ERRCODE_S_E_INVALID_SPECIFICATION),
				 errmsg("savepoint \"%s\" does not exist within current savepoint level", name)));

	/*
	 * Mark "abort pending" all subtransactions up to the target
	 * subtransaction.  The actual aborts will happen when control gets to
	 * CommitTransactionCommand.
	 */
	xact = CurrentTransactionState;
	for (;;)
	{
		if (xact == target)
			break;
		if (xact->blockState == TBLOCK_SUBINPROGRESS)
			xact->blockState = TBLOCK_SUBABORT_PENDING;
		else if (xact->blockState == TBLOCK_SUBABORT)
			xact->blockState = TBLOCK_SUBABORT_END;
		else
			elog(FATAL, "RollbackToSavepoint: unexpected state %s",
				 BlockStateAsString(xact->blockState));
		xact = xact->parent;
		Assert(PointerIsValid(xact));
	}

	/* And mark the target as "restart pending" */
	if (xact->blockState == TBLOCK_SUBINPROGRESS)
		xact->blockState = TBLOCK_SUBRESTART;
	else if (xact->blockState == TBLOCK_SUBABORT)
		xact->blockState = TBLOCK_SUBABORT_RESTART;
	else
		elog(FATAL, "RollbackToSavepoint: unexpected state %s",
			 BlockStateAsString(xact->blockState));

	YBCRollbackToSubTransaction(target->subTransactionId);
}

/*
 * BeginInternalSubTransaction
 *		This is the same as DefineSavepoint except it allows TBLOCK_STARTED,
 *		TBLOCK_IMPLICIT_INPROGRESS, TBLOCK_END, and TBLOCK_PREPARE states,
 *		and therefore it can safely be used in functions that might be called
 *		when not inside a BEGIN block or when running deferred triggers at
 *		COMMIT/PREPARE time.  Also, it automatically does
 *		CommitTransactionCommand/StartTransactionCommand instead of expecting
 *		the caller to do it.
 */
void
BeginInternalSubTransaction(const char *name)
{
	/*
	 * The subtransaction corresponding to the buffered operations must be
	 * current and in the INPROGRESS state for correct error handling.
	 * An error thrown while/after switching over to a new subtransaction
	 * would lead to a fatal error or unpredictable behavior.
	 */
	YBFlushBufferedOperations();
	TransactionState s = CurrentTransactionState;

	/*
	 * Workers synchronize transaction state at the beginning of each parallel
	 * operation, so we can't account for new subtransactions after that
	 * point. We might be able to make an exception for the type of
	 * subtransaction established by this function, which is typically used in
	 * contexts where we're going to release or roll back the subtransaction
	 * before proceeding further, so that no enduring change to the
	 * transaction state occurs. For now, however, we prohibit this case along
	 * with all the others.
	 */
	if (IsInParallelMode())
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TRANSACTION_STATE),
				 errmsg("cannot start subtransactions during a parallel operation")));

	switch (s->blockState)
	{
		case TBLOCK_STARTED:
		case TBLOCK_INPROGRESS:
		case TBLOCK_IMPLICIT_INPROGRESS:
		case TBLOCK_END:
		case TBLOCK_PREPARE:
		case TBLOCK_SUBINPROGRESS:
			/* Normal subtransaction start */
			PushTransaction();
			s = CurrentTransactionState;	/* changed by push */
			elog(DEBUG2, "new sub txn created internally, subtxn_id: %d",
					 s->subTransactionId);

			/*
			 * Savepoint names, like the TransactionState block itself, live
			 * in TopTransactionContext.
			 */
			if (name)
				s->name = MemoryContextStrdup(TopTransactionContext, name);
			break;

			/* These cases are invalid. */
		case TBLOCK_DEFAULT:
		case TBLOCK_BEGIN:
		case TBLOCK_PARALLEL_INPROGRESS:
		case TBLOCK_SUBBEGIN:
		case TBLOCK_SUBRELEASE:
		case TBLOCK_SUBCOMMIT:
		case TBLOCK_ABORT:
		case TBLOCK_SUBABORT:
		case TBLOCK_ABORT_END:
		case TBLOCK_SUBABORT_END:
		case TBLOCK_ABORT_PENDING:
		case TBLOCK_SUBABORT_PENDING:
		case TBLOCK_SUBRESTART:
		case TBLOCK_SUBABORT_RESTART:
			elog(FATAL, "BeginInternalSubTransaction: unexpected state %s",
				 BlockStateAsString(s->blockState));
			break;
	}

	CommitTransactionCommand();

	StartTransactionCommandInternal(true /* yb_skip_read_committed_handling */);
}

/*
 * BeginInternalSubTransactionForReadCommittedStatement
 *		This is similar to BeginInternalSubTransaction() but doesn't call CommitTransactionCommand()
 *    and StartTransactionCommand(). It is okay to not call those since this method is called only
 *    in 2 specific cases (i.e., when starting a new statement in an already existing txn in
 *    READ COMMITED mode, or when rolling back to the internal sub txn while restarting a
 *    statement) and both cases satisfy the following property -
 *      CurrentTransactionState->blockState is TBLOCK_INPROGRESS, TBLOCK_IMPLICIT_INPROGRESS or
 *			TBLOCK_SUBINPROGRESS.
 */
void
BeginInternalSubTransactionForReadCommittedStatement()
{

	YBFlushBufferedOperations();
	TransactionState s = CurrentTransactionState;

	Assert(s->blockState == TBLOCK_SUBINPROGRESS ||
				 s->blockState == TBLOCK_IMPLICIT_INPROGRESS ||
				 s->blockState == TBLOCK_INPROGRESS);

	if (IsInParallelMode())
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TRANSACTION_STATE),
				errmsg("cannot start subtransactions during a parallel operation")));

	/* Normal subtransaction start */
	PushTransaction();
	s = CurrentTransactionState;	/* changed by push */
	elog(DEBUG2, "new internal sub txn in READ COMMITTED subtxn_id: %d", s->subTransactionId);

	s->name = MemoryContextStrdup(TopTransactionContext, YB_READ_COMMITTED_INTERNAL_SUB_TXN_NAME);

	StartSubTransaction();
	s->blockState = TBLOCK_SUBINPROGRESS;
	s->ybIsInternalRcSubTransaction = true;
}

/*
 * ReleaseCurrentSubTransaction
 *
 * RELEASE (ie, commit) the innermost subtransaction, regardless of its
 * savepoint name (if any).
 * NB: do NOT use CommitTransactionCommand/StartTransactionCommand with this.
 */
void
ReleaseCurrentSubTransaction(void)
{
	/*
	 * The subtransaction corresponding to the buffered operations must be
	 * current and in the INPROGRESS state for correct error handling.
	 * An error thrown while/after commiting/releasing it would lead to a
	 * fatal error or unpredictable behavior.
	 */
	YBFlushBufferedOperations();
	TransactionState s = CurrentTransactionState;

	/*
	 * Workers synchronize transaction state at the beginning of each parallel
	 * operation, so we can't account for commit of subtransactions after that
	 * point.  This should not happen anyway.  Code calling this would
	 * typically have called BeginInternalSubTransaction() first, failing
	 * there.
	 */
	if (IsInParallelMode())
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TRANSACTION_STATE),
				 errmsg("cannot commit subtransactions during a parallel operation")));

	if (s->blockState != TBLOCK_SUBINPROGRESS)
		elog(ERROR, "ReleaseCurrentSubTransaction: unexpected state %s",
			 BlockStateAsString(s->blockState));
	Assert(s->state == TRANS_INPROGRESS);
	MemoryContextSwitchTo(CurTransactionContext);
	CommitSubTransaction();
	s = CurrentTransactionState;	/* changed by pop */
	Assert(s->state == TRANS_INPROGRESS);
}

/*
 * RollbackAndReleaseCurrentSubTransaction
 *
 * ROLLBACK and RELEASE (ie, abort) the innermost subtransaction, regardless
 * of its savepoint name (if any).
 * NB: do NOT use CommitTransactionCommand/StartTransactionCommand with this.
 */
void
RollbackAndReleaseCurrentSubTransaction(void)
{
	TransactionState s = CurrentTransactionState;

	/*
	 * Unlike ReleaseCurrentSubTransaction(), this is nominally permitted
	 * during parallel operations.  That's because we may be in the leader,
	 * recovering from an error thrown while we were in parallel mode.  We
	 * won't reach here in a worker, because BeginInternalSubTransaction()
	 * will have failed.
	 */

	switch (s->blockState)
	{
			/* Must be in a subtransaction */
		case TBLOCK_SUBINPROGRESS:
		case TBLOCK_SUBABORT:
			break;

			/* These cases are invalid. */
		case TBLOCK_DEFAULT:
		case TBLOCK_STARTED:
		case TBLOCK_BEGIN:
		case TBLOCK_IMPLICIT_INPROGRESS:
		case TBLOCK_PARALLEL_INPROGRESS:
		case TBLOCK_SUBBEGIN:
		case TBLOCK_INPROGRESS:
		case TBLOCK_END:
		case TBLOCK_SUBRELEASE:
		case TBLOCK_SUBCOMMIT:
		case TBLOCK_ABORT:
		case TBLOCK_ABORT_END:
		case TBLOCK_SUBABORT_END:
		case TBLOCK_ABORT_PENDING:
		case TBLOCK_SUBABORT_PENDING:
		case TBLOCK_SUBRESTART:
		case TBLOCK_SUBABORT_RESTART:
		case TBLOCK_PREPARE:
			elog(FATAL, "RollbackAndReleaseCurrentSubTransaction: unexpected state %s",
				 BlockStateAsString(s->blockState));
			break;
	}

	/*
	 * Abort the current subtransaction, if needed.
	 */
	if (s->blockState == TBLOCK_SUBINPROGRESS)
		AbortSubTransaction();

	/* And clean it up, too */
	CleanupSubTransaction();

	s = CurrentTransactionState;	/* changed by pop */
	AssertState(s->blockState == TBLOCK_SUBINPROGRESS ||
				s->blockState == TBLOCK_INPROGRESS ||
				s->blockState == TBLOCK_IMPLICIT_INPROGRESS ||
				s->blockState == TBLOCK_STARTED);
}

/*
 *	AbortOutOfAnyTransaction
 *
 *	This routine is provided for error recovery purposes.  It aborts any
 *	active transaction or transaction block, leaving the system in a known
 *	idle state.
 */
void
AbortOutOfAnyTransaction(void)
{
	elog(DEBUG2, "AbortOutOfAnyTransaction");
	TransactionState s = CurrentTransactionState;

	/* Ensure we're not running in a doomed memory context */
	AtAbort_Memory();

	/*
	 * Get out of any transaction or nested transaction
	 */
	do
	{
		switch (s->blockState)
		{
			case TBLOCK_DEFAULT:
				if (s->state == TRANS_DEFAULT)
				{
					/* Not in a transaction, do nothing */
				}
				else
				{
					/*
					 * We can get here after an error during transaction start
					 * (state will be TRANS_START).  Need to clean up the
					 * incompletely started transaction.  First, adjust the
					 * low-level state to suppress warning message from
					 * AbortTransaction.
					 */
					if (s->state == TRANS_START)
						s->state = TRANS_INPROGRESS;
					AbortTransaction();
					CleanupTransaction();
				}
				break;
			case TBLOCK_STARTED:
			case TBLOCK_BEGIN:
			case TBLOCK_INPROGRESS:
			case TBLOCK_IMPLICIT_INPROGRESS:
			case TBLOCK_PARALLEL_INPROGRESS:
			case TBLOCK_END:
			case TBLOCK_ABORT_PENDING:
			case TBLOCK_PREPARE:
				/* In a transaction, so clean up */
				AbortTransaction();
				CleanupTransaction();
				s->blockState = TBLOCK_DEFAULT;
				break;
			case TBLOCK_ABORT:
			case TBLOCK_ABORT_END:

				/*
				 * AbortTransaction is already done, still need Cleanup.
				 * However, if we failed partway through running ROLLBACK,
				 * there will be an active portal running that command, which
				 * we need to shut down before doing CleanupTransaction.
				 */
				AtAbort_Portals();
				CleanupTransaction();
				s->blockState = TBLOCK_DEFAULT;
				break;

				/*
				 * In a subtransaction, so clean it up and abort parent too
				 */
			case TBLOCK_SUBBEGIN:
			case TBLOCK_SUBINPROGRESS:
			case TBLOCK_SUBRELEASE:
			case TBLOCK_SUBCOMMIT:
			case TBLOCK_SUBABORT_PENDING:
			case TBLOCK_SUBRESTART:
				AbortSubTransaction();
				CleanupSubTransaction();
				s = CurrentTransactionState;	/* changed by pop */
				break;

			case TBLOCK_SUBABORT:
			case TBLOCK_SUBABORT_END:
			case TBLOCK_SUBABORT_RESTART:
				/* As above, but AbortSubTransaction already done */
				if (s->curTransactionOwner)
				{
					/* As in TBLOCK_ABORT, might have a live portal to zap */
					AtSubAbort_Portals(s->subTransactionId,
									   s->parent->subTransactionId,
									   s->curTransactionOwner,
									   s->parent->curTransactionOwner);
				}
				CleanupSubTransaction();
				s = CurrentTransactionState;	/* changed by pop */
				break;
		}
	} while (s->blockState != TBLOCK_DEFAULT);

	/* Should be out of all subxacts now */
	Assert(s->parent == NULL);

	/* If we didn't actually have anything to do, revert to TopMemoryContext */
	AtCleanup_Memory();
}

/*
 * IsTransactionBlock --- are we within a transaction block?
 */
bool
IsTransactionBlock(void)
{
	TransactionState s = CurrentTransactionState;

	if (s->blockState == TBLOCK_DEFAULT || s->blockState == TBLOCK_STARTED)
		return false;

	return true;
}

/*
 * IsTransactionOrTransactionBlock --- are we within either a transaction
 * or a transaction block?	(The backend is only really "idle" when this
 * returns false.)
 *
 * This should match up with IsTransactionBlock and IsTransactionState.
 */
bool
IsTransactionOrTransactionBlock(void)
{
	TransactionState s = CurrentTransactionState;

	if (s->blockState == TBLOCK_DEFAULT)
		return false;

	return true;
}

/*
 * TransactionBlockStatusCode - return status code to send in ReadyForQuery
 */
char
TransactionBlockStatusCode(void)
{
	TransactionState s = CurrentTransactionState;

	switch (s->blockState)
	{
		case TBLOCK_DEFAULT:
		case TBLOCK_STARTED:
			return ((YbIsClientYsqlConnMgr() && \
					YbIsStickyConnection(&(s->ybUncommittedStickyObjectCount)))
				? 'i' : 'I');			/* idle --- not in transaction */
		case TBLOCK_BEGIN:
		case TBLOCK_SUBBEGIN:
		case TBLOCK_INPROGRESS:
		case TBLOCK_IMPLICIT_INPROGRESS:
		case TBLOCK_PARALLEL_INPROGRESS:
		case TBLOCK_SUBINPROGRESS:
		case TBLOCK_END:
		case TBLOCK_SUBRELEASE:
		case TBLOCK_SUBCOMMIT:
		case TBLOCK_PREPARE:
			return 'T';			/* in transaction */
		case TBLOCK_ABORT:
		case TBLOCK_SUBABORT:
		case TBLOCK_ABORT_END:
		case TBLOCK_SUBABORT_END:
		case TBLOCK_ABORT_PENDING:
		case TBLOCK_SUBABORT_PENDING:
		case TBLOCK_SUBRESTART:
		case TBLOCK_SUBABORT_RESTART:
			return 'E';			/* in failed transaction */
	}

	/* should never get here */
	elog(FATAL, "invalid transaction block state: %s",
		 BlockStateAsString(s->blockState));
	return 0;					/* keep compiler quiet */
}

/*
 * IsSubTransaction
 */
bool
IsSubTransaction(void)
{
	TransactionState s = CurrentTransactionState;

	if (s->nestingLevel >= 2)
		return true;

	return false;
}

/*
 * StartSubTransaction
 *
 * If you're wondering why this is separate from PushTransaction: it's because
 * we can't conveniently do this stuff right inside DefineSavepoint.  The
 * SAVEPOINT utility command will be executed inside a Portal, and if we
 * muck with GetCurrentMemoryContext() or CurrentResourceOwner then exit from
 * the Portal will undo those settings.  So we make DefineSavepoint just
 * push a dummy transaction block, and when control returns to the main
 * idle loop, CommitTransactionCommand will be called, and we'll come here
 * to finish starting the subtransaction.
 */
static void
StartSubTransaction(void)
{
	TransactionState s = CurrentTransactionState;

	if (s->state != TRANS_DEFAULT)
		elog(WARNING, "StartSubTransaction while in %s state",
			 TransStateAsString(s->state));

	s->state = TRANS_START;

	/*
	 * Initialize subsystems for new subtransaction
	 *
	 * must initialize resource-management stuff first
	 */
	AtSubStart_Memory();
	AtSubStart_ResourceOwner();
	AfterTriggerBeginSubXact();

	s->state = TRANS_INPROGRESS;

	/*
	 * Call start-of-subxact callbacks
	 */
	CallSubXactCallbacks(SUBXACT_EVENT_START_SUB, s->subTransactionId,
						 s->parent->subTransactionId);

	ShowTransactionState("StartSubTransaction");

	/*
	 * Update the value of the sticky objects from parent transaction
	 */
	if (CurrentTransactionState->parent)
		CurrentTransactionState->ybUncommittedStickyObjectCount =
			CurrentTransactionState->parent->ybUncommittedStickyObjectCount;
}

/*
 * CommitSubTransaction
 *
 *	The caller has to make sure to always reassign CurrentTransactionState
 *	if it has a local pointer to it after calling this function.
 */
static void
CommitSubTransaction(void)
{
	TransactionState s = CurrentTransactionState;

	ShowTransactionState("CommitSubTransaction");

	if (s->state != TRANS_INPROGRESS)
		elog(WARNING, "CommitSubTransaction while in %s state",
			 TransStateAsString(s->state));

	/* Pre-commit processing goes here */

	CallSubXactCallbacks(SUBXACT_EVENT_PRE_COMMIT_SUB, s->subTransactionId,
						 s->parent->subTransactionId);

	/* If in parallel mode, clean up workers and exit parallel mode. */
	if (IsInParallelMode())
	{
		AtEOSubXact_Parallel(true, s->subTransactionId);
		s->parallelModeLevel = 0;
	}

	/* Do the actual "commit", such as it is */
	s->state = TRANS_COMMIT;

	/* Must CCI to ensure commands of subtransaction are seen as done */
	CommandCounterIncrement();

	/*
	 * Prior to 8.4 we marked subcommit in clog at this point.  We now only
	 * perform that step, if required, as part of the atomic update of the
	 * whole transaction tree at top level commit or abort.
	 */

	/* Post-commit cleanup */
	if (FullTransactionIdIsValid(s->fullTransactionId))
		AtSubCommit_childXids();
	AfterTriggerEndSubXact(true);
	AtSubCommit_Portals(s->subTransactionId,
						s->parent->subTransactionId,
						s->parent->nestingLevel,
						s->parent->curTransactionOwner);
	AtEOSubXact_LargeObject(true, s->subTransactionId,
							s->parent->subTransactionId);
	AtSubCommit_Notify();

	CallSubXactCallbacks(SUBXACT_EVENT_COMMIT_SUB, s->subTransactionId,
						 s->parent->subTransactionId);

	ResourceOwnerRelease(s->curTransactionOwner,
						 RESOURCE_RELEASE_BEFORE_LOCKS,
						 true, false);
	AtEOSubXact_RelationCache(true, s->subTransactionId,
							  s->parent->subTransactionId);
	AtEOSubXact_Inval(true);
	AtSubCommit_smgr();

	/*
	 * The only lock we actually release here is the subtransaction XID lock.
	 */
	CurrentResourceOwner = s->curTransactionOwner;
	if (FullTransactionIdIsValid(s->fullTransactionId))
		XactLockTableDelete(XidFromFullTransactionId(s->fullTransactionId));

	/*
	 * Other locks should get transferred to their parent resource owner.
	 */
	ResourceOwnerRelease(s->curTransactionOwner,
						 RESOURCE_RELEASE_LOCKS,
						 true, false);
	ResourceOwnerRelease(s->curTransactionOwner,
						 RESOURCE_RELEASE_AFTER_LOCKS,
						 true, false);

	AtEOXact_GUC(true, s->gucNestLevel);
	AtEOSubXact_SPI(true, s->subTransactionId);
	AtEOSubXact_on_commit_actions(true, s->subTransactionId,
								  s->parent->subTransactionId);
	AtEOSubXact_Namespace(true, s->subTransactionId,
						  s->parent->subTransactionId);
	AtEOSubXact_Files(true, s->subTransactionId,
					  s->parent->subTransactionId);
	AtEOSubXact_HashTables(true, s->nestingLevel);
	AtEOSubXact_PgStat(true, s->nestingLevel);
	AtSubCommit_Snapshot(s->nestingLevel);

	/*
	 * We need to restore the upper transaction's read-only state, in case the
	 * upper is read-write while the child is read-only; GUC will incorrectly
	 * think it should leave the child state in place.
	 */
	XactReadOnly = s->prevXactReadOnly;

	CurrentResourceOwner = s->parent->curTransactionOwner;
	CurTransactionResourceOwner = s->parent->curTransactionOwner;
	ResourceOwnerDelete(s->curTransactionOwner);
	s->curTransactionOwner = NULL;

	AtSubCommit_Memory();

	s->state = TRANS_DEFAULT;

	/* Conserve sticky object count before popping transaction state. */
	s->parent->ybUncommittedStickyObjectCount = s->ybUncommittedStickyObjectCount;

	PopTransaction();
}

/*
 * AbortSubTransaction
 */
static void
AbortSubTransaction(void)
{
	TransactionState s = CurrentTransactionState;

	/* Prevent cancel/die interrupt while cleaning up */
	HOLD_INTERRUPTS();

	/* Make sure we have a valid memory context and resource owner */
	AtSubAbort_Memory();
	AtSubAbort_ResourceOwner();

	/*
	 * Release any LW locks we might be holding as quickly as possible.
	 * (Regular locks, however, must be held till we finish aborting.)
	 * Releasing LW locks is critical since we might try to grab them again
	 * while cleaning up!
	 *
	 * FIXME This may be incorrect --- Are there some locks we should keep?
	 * Buffer locks, for example?  I don't think so but I'm not sure.
	 */
	LWLockReleaseAll();

	pgstat_report_wait_end();
	pgstat_progress_end_command();
	AbortBufferIO();
	UnlockBuffers();

	/* Reset WAL record construction state */
	XLogResetInsertion();

	/* Cancel condition variable sleep */
	ConditionVariableCancelSleep();

	/*
	 * Also clean up any open wait for lock, since the lock manager will choke
	 * if we try to wait for another lock before doing this.
	 */
	LockErrorCleanup();

	/*
	 * If any timeout events are still active, make sure the timeout interrupt
	 * is scheduled.  This covers possible loss of a timeout interrupt due to
	 * longjmp'ing out of the SIGINT handler (see notes in handle_sig_alarm).
	 * We delay this till after LockErrorCleanup so that we don't uselessly
	 * reschedule lock or deadlock check timeouts.
	 */
	reschedule_timeouts();

	/*
	 * Re-enable signals, in case we got here by longjmp'ing out of a signal
	 * handler.  We do this fairly early in the sequence so that the timeout
	 * infrastructure will be functional if needed while aborting.
	 */
	PG_SETMASK(&UnBlockSig);

	/*
	 * check the current transaction state
	 */
	ShowTransactionState("AbortSubTransaction");

	if (s->state != TRANS_INPROGRESS)
		elog(WARNING, "AbortSubTransaction while in %s state",
			 TransStateAsString(s->state));

	s->state = TRANS_ABORT;

	/*
	 * Reset user ID which might have been changed transiently.  (See notes in
	 * AbortTransaction.)
	 */
	SetUserIdAndSecContext(s->prevUser, s->prevSecContext);

	/* Forget about any active REINDEX. */
	ResetReindexState(s->nestingLevel);

	/* Reset logical streaming state. */
	ResetLogicalStreamingState();

	/*
	 * No need for SnapBuildResetExportedSnapshotState() here, snapshot
	 * exports are not supported in subtransactions.
	 */

	/* Exit from parallel mode, if necessary. */
	if (IsInParallelMode())
	{
		AtEOSubXact_Parallel(false, s->subTransactionId);
		s->parallelModeLevel = 0;
	}

	/*
	 * We can skip all this stuff if the subxact failed before creating a
	 * ResourceOwner...
	 */
	if (s->curTransactionOwner)
	{
		AfterTriggerEndSubXact(false);
		AtSubAbort_Portals(s->subTransactionId,
						   s->parent->subTransactionId,
						   s->curTransactionOwner,
						   s->parent->curTransactionOwner);
		AtEOSubXact_LargeObject(false, s->subTransactionId,
								s->parent->subTransactionId);
		AtSubAbort_Notify();

		/* Advertise the fact that we aborted in pg_xact. */
		(void) RecordTransactionAbort(true);

		/* Post-abort cleanup */
		if (FullTransactionIdIsValid(s->fullTransactionId))
			AtSubAbort_childXids();

		CallSubXactCallbacks(SUBXACT_EVENT_ABORT_SUB, s->subTransactionId,
							 s->parent->subTransactionId);

		ResourceOwnerRelease(s->curTransactionOwner,
							 RESOURCE_RELEASE_BEFORE_LOCKS,
							 false, false);
		AtEOSubXact_RelationCache(false, s->subTransactionId,
								  s->parent->subTransactionId);
		AtEOSubXact_Inval(false);
		ResourceOwnerRelease(s->curTransactionOwner,
							 RESOURCE_RELEASE_LOCKS,
							 false, false);
		ResourceOwnerRelease(s->curTransactionOwner,
							 RESOURCE_RELEASE_AFTER_LOCKS,
							 false, false);
		AtSubAbort_smgr();

		AtEOXact_GUC(false, s->gucNestLevel);
		AtEOSubXact_SPI(false, s->subTransactionId);
		AtEOSubXact_on_commit_actions(false, s->subTransactionId,
									  s->parent->subTransactionId);
		AtEOSubXact_Namespace(false, s->subTransactionId,
							  s->parent->subTransactionId);
		AtEOSubXact_Files(false, s->subTransactionId,
						  s->parent->subTransactionId);
		AtEOSubXact_HashTables(false, s->nestingLevel);
		AtEOSubXact_PgStat(false, s->nestingLevel);
		AtSubAbort_Snapshot(s->nestingLevel);
	}

	YBCRollbackToSubTransaction(s->subTransactionId);

	/*
	 * Restore the upper transaction's read-only state, too.  This should be
	 * redundant with GUC's cleanup but we may as well do it for consistency
	 * with the commit case.
	 */
	XactReadOnly = s->prevXactReadOnly;

	RESUME_INTERRUPTS();
}

/*
 * CleanupSubTransaction
 *
 *	The caller has to make sure to always reassign CurrentTransactionState
 *	if it has a local pointer to it after calling this function.
 */
static void
CleanupSubTransaction(void)
{
	TransactionState s = CurrentTransactionState;

	ShowTransactionState("CleanupSubTransaction");

	if (s->state != TRANS_ABORT)
		elog(WARNING, "CleanupSubTransaction while in %s state",
			 TransStateAsString(s->state));

	AtSubCleanup_Portals(s->subTransactionId);

	CurrentResourceOwner = s->parent->curTransactionOwner;
	CurTransactionResourceOwner = s->parent->curTransactionOwner;
	if (s->curTransactionOwner)
		ResourceOwnerDelete(s->curTransactionOwner);
	s->curTransactionOwner = NULL;

	AtSubCleanup_Memory();

	s->state = TRANS_DEFAULT;

	PopTransaction();
}

/*
 * PushTransaction
 *		Create transaction state stack entry for a subtransaction
 *
 *	The caller has to make sure to always reassign CurrentTransactionState
 *	if it has a local pointer to it after calling this function.
 */
static void
PushTransaction(void)
{
	elog(DEBUG2, "PushTransaction increment sub-txn id from %d -> %d",
			 currentSubTransactionId, currentSubTransactionId+1);
	TransactionState p = CurrentTransactionState;
	TransactionState s;

	/*
	 * We keep subtransaction state nodes in TopTransactionContext.
	 */
	s = (TransactionState)
		MemoryContextAllocZero(TopTransactionContext,
							   sizeof(TransactionStateData));

	/*
	 * Assign a subtransaction ID, watching out for counter wraparound.
	 */
	currentSubTransactionId += 1;
	if (currentSubTransactionId == InvalidSubTransactionId)
	{
		currentSubTransactionId -= 1;
		pfree(s);
		ereport(ERROR,
				(errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
				 errmsg("cannot have more than 2^32-1 subtransactions in a transaction")));
	}

	/*
	 * We can now stack a minimally valid subtransaction without fear of
	 * failure.
	 */
	s->fullTransactionId = InvalidFullTransactionId;	/* until assigned */
	s->subTransactionId = currentSubTransactionId;
	s->parent = p;
	s->nestingLevel = p->nestingLevel + 1;
	s->gucNestLevel = NewGUCNestLevel();
	s->savepointLevel = p->savepointLevel;
	s->state = TRANS_DEFAULT;
	s->blockState = TBLOCK_SUBBEGIN;
	GetUserIdAndSecContext(&s->prevUser, &s->prevSecContext);
	s->prevXactReadOnly = XactReadOnly;
	s->parallelModeLevel = 0;
	s->topXidLogged = false;

	s->ybDataSentForCurrQuery = p->ybDataSentForCurrQuery;
	s->ybDataSent = p->ybDataSent;

	CurrentTransactionState = s;

	YBUpdateActiveSubTransaction(CurrentTransactionState);

	/*
	 * AbortSubTransaction and CleanupSubTransaction have to be able to cope
	 * with the subtransaction from here on out; in particular they should not
	 * assume that it necessarily has a transaction context, resource owner,
	 * or XID.
	 */
}

/*
 * PopTransaction
 *		Pop back to parent transaction state
 *
 *	The caller has to make sure to always reassign CurrentTransactionState
 *	if it has a local pointer to it after calling this function.
 */
static void
PopTransaction(void)
{
	elog(DEBUG2, "PopTransaction sub-txn id %d", currentSubTransactionId);
	TransactionState s = CurrentTransactionState;

	if (s->state != TRANS_DEFAULT)
		elog(WARNING, "PopTransaction while in %s state",
			 TransStateAsString(s->state));

	if (s->parent == NULL)
		elog(FATAL, "PopTransaction with no parent");

	/* Propagate the data sent information to the parent. */
	s->parent->ybDataSent = s->parent->ybDataSent || s->ybDataSent;
	s->parent->ybDataSentForCurrQuery = s->parent->ybDataSentForCurrQuery ||
										s->ybDataSentForCurrQuery;

	CurrentTransactionState = s->parent;
	YBUpdateActiveSubTransaction(CurrentTransactionState);

	/* Let's just make sure CurTransactionContext is good */
	CurTransactionContext = s->parent->curTransactionContext;
	MemoryContextSwitchTo(CurTransactionContext);

	/* Ditto for ResourceOwner links */
	CurTransactionResourceOwner = s->parent->curTransactionOwner;
	CurrentResourceOwner = s->parent->curTransactionOwner;

	/* Free the old child structure */
	if (s->name)
		pfree(s->name);
	pfree(s);
}

/*
 * EstimateTransactionStateSpace
 *		Estimate the amount of space that will be needed by
 *		SerializeTransactionState.  It would be OK to overestimate slightly,
 *		but it's simple for us to work out the precise value, so we do.
 */
Size
EstimateTransactionStateSpace(void)
{
	TransactionState s;
	Size		nxids = 0;
	Size		size = SerializedTransactionStateHeaderSize;

	for (s = CurrentTransactionState; s != NULL; s = s->parent)
	{
		if (FullTransactionIdIsValid(s->fullTransactionId))
			nxids = add_size(nxids, 1);
		nxids = add_size(nxids, s->nChildXids);
	}

	return add_size(size, mul_size(sizeof(TransactionId), nxids));
}

/*
 * SerializeTransactionState
 *		Write out relevant details of our transaction state that will be
 *		needed by a parallel worker.
 *
 * We need to save and restore XactDeferrable, XactIsoLevel, and the XIDs
 * associated with this transaction.  These are serialized into a
 * caller-supplied buffer big enough to hold the number of bytes reported by
 * EstimateTransactionStateSpace().  We emit the XIDs in sorted order for the
 * convenience of the receiving process.
 */
void
SerializeTransactionState(Size maxsize, char *start_address)
{
	TransactionState s;
	Size		nxids = 0;
	Size		i = 0;
	TransactionId *workspace;
	SerializedTransactionState *result;

	result = (SerializedTransactionState *) start_address;

	result->xactIsoLevel = XactIsoLevel;
	result->xactDeferrable = XactDeferrable;
	result->topFullTransactionId = XactTopFullTransactionId;
	result->currentFullTransactionId =
		CurrentTransactionState->fullTransactionId;
	result->currentCommandId = currentCommandId;

	/*
	 * If we're running in a parallel worker and launching a parallel worker
	 * of our own, we can just pass along the information that was passed to
	 * us.
	 */
	if (nParallelCurrentXids > 0)
	{
		result->nParallelCurrentXids = nParallelCurrentXids;
		memcpy(&result->parallelCurrentXids[0], ParallelCurrentXids,
			   nParallelCurrentXids * sizeof(TransactionId));
		return;
	}

	/*
	 * OK, we need to generate a sorted list of XIDs that our workers should
	 * view as current.  First, figure out how many there are.
	 */
	for (s = CurrentTransactionState; s != NULL; s = s->parent)
	{
		if (FullTransactionIdIsValid(s->fullTransactionId))
			nxids = add_size(nxids, 1);
		nxids = add_size(nxids, s->nChildXids);
	}
	Assert(SerializedTransactionStateHeaderSize + nxids * sizeof(TransactionId)
		   <= maxsize);

	/* Copy them to our scratch space. */
	workspace = palloc(nxids * sizeof(TransactionId));
	for (s = CurrentTransactionState; s != NULL; s = s->parent)
	{
		if (FullTransactionIdIsValid(s->fullTransactionId))
			workspace[i++] = XidFromFullTransactionId(s->fullTransactionId);
		if (s->nChildXids > 0)
			memcpy(&workspace[i], s->childXids,
				   s->nChildXids * sizeof(TransactionId));
		i += s->nChildXids;
	}
	Assert(i == nxids);

	/* Sort them. */
	qsort(workspace, nxids, sizeof(TransactionId), xidComparator);

	/* Copy data into output area. */
	result->nParallelCurrentXids = nxids;
	memcpy(&result->parallelCurrentXids[0], workspace,
		   nxids * sizeof(TransactionId));
}

/*
 * StartParallelWorkerTransaction
 *		Start a parallel worker transaction, restoring the relevant
 *		transaction state serialized by SerializeTransactionState.
 */
void
StartParallelWorkerTransaction(char *tstatespace)
{
	SerializedTransactionState *tstate;

	Assert(CurrentTransactionState->blockState == TBLOCK_DEFAULT);
	StartTransaction();

	tstate = (SerializedTransactionState *) tstatespace;
	XactIsoLevel = tstate->xactIsoLevel;
	XactDeferrable = tstate->xactDeferrable;
	XactTopFullTransactionId = tstate->topFullTransactionId;
	CurrentTransactionState->fullTransactionId =
		tstate->currentFullTransactionId;
	currentCommandId = tstate->currentCommandId;
	nParallelCurrentXids = tstate->nParallelCurrentXids;
	ParallelCurrentXids = &tstate->parallelCurrentXids[0];

	CurrentTransactionState->blockState = TBLOCK_PARALLEL_INPROGRESS;
}

/*
 * EndParallelWorkerTransaction
 *		End a parallel worker transaction.
 */
void
EndParallelWorkerTransaction(void)
{
	Assert(CurrentTransactionState->blockState == TBLOCK_PARALLEL_INPROGRESS);
	CommitTransaction();
	CurrentTransactionState->blockState = TBLOCK_DEFAULT;
}

/*
 * ShowTransactionState
 *		Debug support
 */
static void
ShowTransactionState(const char *str)
{
	/* skip work if message will definitely not be printed */
	if (message_level_is_interesting(DEBUG5))
		ShowTransactionStateRec(str, CurrentTransactionState);
}

/*
 * ShowTransactionStateRec
 *		Recursive subroutine for ShowTransactionState
 */
static void
ShowTransactionStateRec(const char *str, TransactionState s)
{
	StringInfoData buf;

	initStringInfo(&buf);

	if (s->nChildXids > 0)
	{
		int			i;

		appendStringInfo(&buf, ", children: %u", s->childXids[0]);
		for (i = 1; i < s->nChildXids; i++)
			appendStringInfo(&buf, " %u", s->childXids[i]);
	}

	if (s->parent)
		ShowTransactionStateRec(str, s->parent);

	ereport(DEBUG5,
			(errmsg_internal("%s(%d) name: %s; blockState: %s; "
							 "state: %s, ybDataSent: %s, ybDataSentForCurrQuery: %s, xid/subid/cid: %u/%u/%u%s%s",
							 str, s->nestingLevel,
							 PointerIsValid(s->name) ? s->name : "unnamed",
							 BlockStateAsString(s->blockState),
							 TransStateAsString(s->state),
							 s->ybDataSent ? "Y" : "N",
							 s->ybDataSentForCurrQuery ? "Y" : "N",
							 (unsigned int) XidFromFullTransactionId(s->fullTransactionId),
							 (unsigned int) s->subTransactionId,
							 (unsigned int) currentCommandId,
							 currentCommandIdUsed ? " (used)" : "",
							 buf.data)));

	pfree(buf.data);
}

/*
 * BlockStateAsString
 *		Debug support
 */
static const char *
BlockStateAsString(TBlockState blockState)
{
	switch (blockState)
	{
		case TBLOCK_DEFAULT:
			return "DEFAULT";
		case TBLOCK_STARTED:
			return "STARTED";
		case TBLOCK_BEGIN:
			return "BEGIN";
		case TBLOCK_INPROGRESS:
			return "INPROGRESS";
		case TBLOCK_IMPLICIT_INPROGRESS:
			return "IMPLICIT_INPROGRESS";
		case TBLOCK_PARALLEL_INPROGRESS:
			return "PARALLEL_INPROGRESS";
		case TBLOCK_END:
			return "END";
		case TBLOCK_ABORT:
			return "ABORT";
		case TBLOCK_ABORT_END:
			return "ABORT_END";
		case TBLOCK_ABORT_PENDING:
			return "ABORT_PENDING";
		case TBLOCK_PREPARE:
			return "PREPARE";
		case TBLOCK_SUBBEGIN:
			return "SUBBEGIN";
		case TBLOCK_SUBINPROGRESS:
			return "SUBINPROGRESS";
		case TBLOCK_SUBRELEASE:
			return "SUBRELEASE";
		case TBLOCK_SUBCOMMIT:
			return "SUBCOMMIT";
		case TBLOCK_SUBABORT:
			return "SUBABORT";
		case TBLOCK_SUBABORT_END:
			return "SUBABORT_END";
		case TBLOCK_SUBABORT_PENDING:
			return "SUBABORT_PENDING";
		case TBLOCK_SUBRESTART:
			return "SUBRESTART";
		case TBLOCK_SUBABORT_RESTART:
			return "SUBABORT_RESTART";
	}
	return "UNRECOGNIZED";
}

/*
 * TransStateAsString
 *		Debug support
 */
static const char *
TransStateAsString(TransState state)
{
	switch (state)
	{
		case TRANS_DEFAULT:
			return "DEFAULT";
		case TRANS_START:
			return "START";
		case TRANS_INPROGRESS:
			return "INPROGRESS";
		case TRANS_COMMIT:
			return "COMMIT";
		case TRANS_ABORT:
			return "ABORT";
		case TRANS_PREPARE:
			return "PREPARE";
	}
	return "UNRECOGNIZED";
}

/*
 * xactGetCommittedChildren
 *
 * Gets the list of committed children of the current transaction.  The return
 * value is the number of child transactions.  *ptr is set to point to an
 * array of TransactionIds.  The array is allocated in TopTransactionContext;
 * the caller should *not* pfree() it (this is a change from pre-8.4 code!).
 * If there are no subxacts, *ptr is set to NULL.
 */
int
xactGetCommittedChildren(TransactionId **ptr)
{
	TransactionState s = CurrentTransactionState;

	if (s->nChildXids == 0)
		*ptr = NULL;
	else
		*ptr = s->childXids;

	return s->nChildXids;
}

/*
 *	XLOG support routines
 */


/*
 * Log the commit record for a plain or twophase transaction commit.
 *
 * A 2pc commit will be emitted when twophase_xid is valid, a plain one
 * otherwise.
 */
XLogRecPtr
XactLogCommitRecord(TimestampTz commit_time,
					int nsubxacts, TransactionId *subxacts,
					int nrels, RelFileNode *rels,
					int ndroppedstats, xl_xact_stats_item *droppedstats,
					int nmsgs, SharedInvalidationMessage *msgs,
					bool relcacheInval,
					int xactflags, TransactionId twophase_xid,
					const char *twophase_gid)
{
	xl_xact_commit xlrec;
	xl_xact_xinfo xl_xinfo;
	xl_xact_dbinfo xl_dbinfo;
	xl_xact_subxacts xl_subxacts;
	xl_xact_relfilenodes xl_relfilenodes;
	xl_xact_stats_items xl_dropped_stats;
	xl_xact_invals xl_invals;
	xl_xact_twophase xl_twophase;
	xl_xact_origin xl_origin;
	uint8		info;

	Assert(CritSectionCount > 0);

	xl_xinfo.xinfo = 0;

	/* decide between a plain and 2pc commit */
	if (!TransactionIdIsValid(twophase_xid))
		info = XLOG_XACT_COMMIT;
	else
		info = XLOG_XACT_COMMIT_PREPARED;

	/* First figure out and collect all the information needed */

	xlrec.xact_time = commit_time;

	if (relcacheInval)
		xl_xinfo.xinfo |= XACT_COMPLETION_UPDATE_RELCACHE_FILE;
	if (forceSyncCommit)
		xl_xinfo.xinfo |= XACT_COMPLETION_FORCE_SYNC_COMMIT;
	if ((xactflags & XACT_FLAGS_ACQUIREDACCESSEXCLUSIVELOCK))
		xl_xinfo.xinfo |= XACT_XINFO_HAS_AE_LOCKS;

	/*
	 * Check if the caller would like to ask standbys for immediate feedback
	 * once this commit is applied.
	 */
	if (synchronous_commit >= SYNCHRONOUS_COMMIT_REMOTE_APPLY)
		xl_xinfo.xinfo |= XACT_COMPLETION_APPLY_FEEDBACK;

	/*
	 * Relcache invalidations requires information about the current database
	 * and so does logical decoding.
	 */
	if (nmsgs > 0 || XLogLogicalInfoActive())
	{
		xl_xinfo.xinfo |= XACT_XINFO_HAS_DBINFO;
		xl_dbinfo.dbId = MyDatabaseId;
		xl_dbinfo.tsId = MyDatabaseTableSpace;
	}

	if (nsubxacts > 0)
	{
		xl_xinfo.xinfo |= XACT_XINFO_HAS_SUBXACTS;
		xl_subxacts.nsubxacts = nsubxacts;
	}

	if (nrels > 0)
	{
		xl_xinfo.xinfo |= XACT_XINFO_HAS_RELFILENODES;
		xl_relfilenodes.nrels = nrels;
		info |= XLR_SPECIAL_REL_UPDATE;
	}

	if (ndroppedstats > 0)
	{
		xl_xinfo.xinfo |= XACT_XINFO_HAS_DROPPED_STATS;
		xl_dropped_stats.nitems = ndroppedstats;
	}

	if (nmsgs > 0)
	{
		xl_xinfo.xinfo |= XACT_XINFO_HAS_INVALS;
		xl_invals.nmsgs = nmsgs;
	}

	if (TransactionIdIsValid(twophase_xid))
	{
		xl_xinfo.xinfo |= XACT_XINFO_HAS_TWOPHASE;
		xl_twophase.xid = twophase_xid;
		Assert(twophase_gid != NULL);

		if (XLogLogicalInfoActive())
			xl_xinfo.xinfo |= XACT_XINFO_HAS_GID;
	}

	/* dump transaction origin information */
	if (replorigin_session_origin != InvalidRepOriginId)
	{
		xl_xinfo.xinfo |= XACT_XINFO_HAS_ORIGIN;

		xl_origin.origin_lsn = replorigin_session_origin_lsn;
		xl_origin.origin_timestamp = replorigin_session_origin_timestamp;
	}

	if (xl_xinfo.xinfo != 0)
		info |= XLOG_XACT_HAS_INFO;

	/* Then include all the collected data into the commit record. */

	XLogBeginInsert();

	XLogRegisterData((char *) (&xlrec), sizeof(xl_xact_commit));

	if (xl_xinfo.xinfo != 0)
		XLogRegisterData((char *) (&xl_xinfo.xinfo), sizeof(xl_xinfo.xinfo));

	if (xl_xinfo.xinfo & XACT_XINFO_HAS_DBINFO)
		XLogRegisterData((char *) (&xl_dbinfo), sizeof(xl_dbinfo));

	if (xl_xinfo.xinfo & XACT_XINFO_HAS_SUBXACTS)
	{
		XLogRegisterData((char *) (&xl_subxacts),
						 MinSizeOfXactSubxacts);
		XLogRegisterData((char *) subxacts,
						 nsubxacts * sizeof(TransactionId));
	}

	if (xl_xinfo.xinfo & XACT_XINFO_HAS_RELFILENODES)
	{
		XLogRegisterData((char *) (&xl_relfilenodes),
						 MinSizeOfXactRelfilenodes);
		XLogRegisterData((char *) rels,
						 nrels * sizeof(RelFileNode));
	}

	if (xl_xinfo.xinfo & XACT_XINFO_HAS_DROPPED_STATS)
	{
		XLogRegisterData((char *) (&xl_dropped_stats),
						 MinSizeOfXactStatsItems);
		XLogRegisterData((char *) droppedstats,
						 ndroppedstats * sizeof(xl_xact_stats_item));
	}

	if (xl_xinfo.xinfo & XACT_XINFO_HAS_INVALS)
	{
		XLogRegisterData((char *) (&xl_invals), MinSizeOfXactInvals);
		XLogRegisterData((char *) msgs,
						 nmsgs * sizeof(SharedInvalidationMessage));
	}

	if (xl_xinfo.xinfo & XACT_XINFO_HAS_TWOPHASE)
	{
		XLogRegisterData((char *) (&xl_twophase), sizeof(xl_xact_twophase));
		if (xl_xinfo.xinfo & XACT_XINFO_HAS_GID)
			XLogRegisterData(unconstify(char *, twophase_gid), strlen(twophase_gid) + 1);
	}

	if (xl_xinfo.xinfo & XACT_XINFO_HAS_ORIGIN)
		XLogRegisterData((char *) (&xl_origin), sizeof(xl_xact_origin));

	/* we allow filtering by xacts */
	XLogSetRecordFlags(XLOG_INCLUDE_ORIGIN);

	return XLogInsert(RM_XACT_ID, info);
}

/*
 * Log the commit record for a plain or twophase transaction abort.
 *
 * A 2pc abort will be emitted when twophase_xid is valid, a plain one
 * otherwise.
 */
XLogRecPtr
XactLogAbortRecord(TimestampTz abort_time,
				   int nsubxacts, TransactionId *subxacts,
				   int nrels, RelFileNode *rels,
				   int ndroppedstats, xl_xact_stats_item *droppedstats,
				   int xactflags, TransactionId twophase_xid,
				   const char *twophase_gid)
{
	xl_xact_abort xlrec;
	xl_xact_xinfo xl_xinfo;
	xl_xact_subxacts xl_subxacts;
	xl_xact_relfilenodes xl_relfilenodes;
	xl_xact_stats_items xl_dropped_stats;
	xl_xact_twophase xl_twophase;
	xl_xact_dbinfo xl_dbinfo;
	xl_xact_origin xl_origin;

	uint8		info;

	Assert(CritSectionCount > 0);

	xl_xinfo.xinfo = 0;

	/* decide between a plain and 2pc abort */
	if (!TransactionIdIsValid(twophase_xid))
		info = XLOG_XACT_ABORT;
	else
		info = XLOG_XACT_ABORT_PREPARED;


	/* First figure out and collect all the information needed */

	xlrec.xact_time = abort_time;

	if ((xactflags & XACT_FLAGS_ACQUIREDACCESSEXCLUSIVELOCK))
		xl_xinfo.xinfo |= XACT_XINFO_HAS_AE_LOCKS;

	if (nsubxacts > 0)
	{
		xl_xinfo.xinfo |= XACT_XINFO_HAS_SUBXACTS;
		xl_subxacts.nsubxacts = nsubxacts;
	}

	if (nrels > 0)
	{
		xl_xinfo.xinfo |= XACT_XINFO_HAS_RELFILENODES;
		xl_relfilenodes.nrels = nrels;
		info |= XLR_SPECIAL_REL_UPDATE;
	}

	if (ndroppedstats > 0)
	{
		xl_xinfo.xinfo |= XACT_XINFO_HAS_DROPPED_STATS;
		xl_dropped_stats.nitems = ndroppedstats;
	}

	if (TransactionIdIsValid(twophase_xid))
	{
		xl_xinfo.xinfo |= XACT_XINFO_HAS_TWOPHASE;
		xl_twophase.xid = twophase_xid;
		Assert(twophase_gid != NULL);

		if (XLogLogicalInfoActive())
			xl_xinfo.xinfo |= XACT_XINFO_HAS_GID;
	}

	if (TransactionIdIsValid(twophase_xid) && XLogLogicalInfoActive())
	{
		xl_xinfo.xinfo |= XACT_XINFO_HAS_DBINFO;
		xl_dbinfo.dbId = MyDatabaseId;
		xl_dbinfo.tsId = MyDatabaseTableSpace;
	}

	/*
	 * Dump transaction origin information only for abort prepared. We need
	 * this during recovery to update the replication origin progress.
	 */
	if ((replorigin_session_origin != InvalidRepOriginId) &&
		TransactionIdIsValid(twophase_xid))
	{
		xl_xinfo.xinfo |= XACT_XINFO_HAS_ORIGIN;

		xl_origin.origin_lsn = replorigin_session_origin_lsn;
		xl_origin.origin_timestamp = replorigin_session_origin_timestamp;
	}

	if (xl_xinfo.xinfo != 0)
		info |= XLOG_XACT_HAS_INFO;

	/* Then include all the collected data into the abort record. */

	XLogBeginInsert();

	XLogRegisterData((char *) (&xlrec), MinSizeOfXactAbort);

	if (xl_xinfo.xinfo != 0)
		XLogRegisterData((char *) (&xl_xinfo), sizeof(xl_xinfo));

	if (xl_xinfo.xinfo & XACT_XINFO_HAS_DBINFO)
		XLogRegisterData((char *) (&xl_dbinfo), sizeof(xl_dbinfo));

	if (xl_xinfo.xinfo & XACT_XINFO_HAS_SUBXACTS)
	{
		XLogRegisterData((char *) (&xl_subxacts),
						 MinSizeOfXactSubxacts);
		XLogRegisterData((char *) subxacts,
						 nsubxacts * sizeof(TransactionId));
	}

	if (xl_xinfo.xinfo & XACT_XINFO_HAS_RELFILENODES)
	{
		XLogRegisterData((char *) (&xl_relfilenodes),
						 MinSizeOfXactRelfilenodes);
		XLogRegisterData((char *) rels,
						 nrels * sizeof(RelFileNode));
	}

	if (xl_xinfo.xinfo & XACT_XINFO_HAS_DROPPED_STATS)
	{
		XLogRegisterData((char *) (&xl_dropped_stats),
						 MinSizeOfXactStatsItems);
		XLogRegisterData((char *) droppedstats,
						 ndroppedstats * sizeof(xl_xact_stats_item));
	}

	if (xl_xinfo.xinfo & XACT_XINFO_HAS_TWOPHASE)
	{
		XLogRegisterData((char *) (&xl_twophase), sizeof(xl_xact_twophase));
		if (xl_xinfo.xinfo & XACT_XINFO_HAS_GID)
			XLogRegisterData(unconstify(char *, twophase_gid), strlen(twophase_gid) + 1);
	}

	if (xl_xinfo.xinfo & XACT_XINFO_HAS_ORIGIN)
		XLogRegisterData((char *) (&xl_origin), sizeof(xl_xact_origin));

	if (TransactionIdIsValid(twophase_xid))
		XLogSetRecordFlags(XLOG_INCLUDE_ORIGIN);

	return XLogInsert(RM_XACT_ID, info);
}

/*
 * Before 9.0 this was a fairly short function, but now it performs many
 * actions for which the order of execution is critical.
 */
static void
xact_redo_commit(xl_xact_parsed_commit *parsed,
				 TransactionId xid,
				 XLogRecPtr lsn,
				 RepOriginId origin_id)
{
	TransactionId max_xid;
	TimestampTz commit_time;

	Assert(TransactionIdIsValid(xid));

	max_xid = TransactionIdLatest(xid, parsed->nsubxacts, parsed->subxacts);

	/* Make sure nextXid is beyond any XID mentioned in the record. */
	AdvanceNextFullTransactionIdPastXid(max_xid);

	Assert(((parsed->xinfo & XACT_XINFO_HAS_ORIGIN) == 0) ==
		   (origin_id == InvalidRepOriginId));

	if (parsed->xinfo & XACT_XINFO_HAS_ORIGIN)
		commit_time = parsed->origin_timestamp;
	else
		commit_time = parsed->xact_time;

	/* Set the transaction commit timestamp and metadata */
	TransactionTreeSetCommitTsData(xid, parsed->nsubxacts, parsed->subxacts,
								   commit_time, origin_id);

	if (standbyState == STANDBY_DISABLED)
	{
		/*
		 * Mark the transaction committed in pg_xact.
		 */
		TransactionIdCommitTree(xid, parsed->nsubxacts, parsed->subxacts);
	}
	else
	{
		/*
		 * If a transaction completion record arrives that has as-yet
		 * unobserved subtransactions then this will not have been fully
		 * handled by the call to RecordKnownAssignedTransactionIds() in the
		 * main recovery loop in xlog.c. So we need to do bookkeeping again to
		 * cover that case. This is confusing and it is easy to think this
		 * call is irrelevant, which has happened three times in development
		 * already. Leave it in.
		 */
		RecordKnownAssignedTransactionIds(max_xid);

		/*
		 * Mark the transaction committed in pg_xact. We use async commit
		 * protocol during recovery to provide information on database
		 * consistency for when users try to set hint bits. It is important
		 * that we do not set hint bits until the minRecoveryPoint is past
		 * this commit record. This ensures that if we crash we don't see hint
		 * bits set on changes made by transactions that haven't yet
		 * recovered. It's unlikely but it's good to be safe.
		 */
		TransactionIdAsyncCommitTree(xid, parsed->nsubxacts, parsed->subxacts, lsn);

		/*
		 * We must mark clog before we update the ProcArray.
		 */
		ExpireTreeKnownAssignedTransactionIds(xid, parsed->nsubxacts, parsed->subxacts, max_xid);

		/*
		 * Send any cache invalidations attached to the commit. We must
		 * maintain the same order of invalidation then release locks as
		 * occurs in CommitTransaction().
		 */
		ProcessCommittedInvalidationMessages(parsed->msgs, parsed->nmsgs,
											 XactCompletionRelcacheInitFileInval(parsed->xinfo),
											 parsed->dbId, parsed->tsId);

		/*
		 * Release locks, if any. We do this for both two phase and normal one
		 * phase transactions. In effect we are ignoring the prepare phase and
		 * just going straight to lock release.
		 */
		if (parsed->xinfo & XACT_XINFO_HAS_AE_LOCKS)
			StandbyReleaseLockTree(xid, parsed->nsubxacts, parsed->subxacts);
	}

	if (parsed->xinfo & XACT_XINFO_HAS_ORIGIN)
	{
		/* recover apply progress */
		replorigin_advance(origin_id, parsed->origin_lsn, lsn,
						   false /* backward */ , false /* WAL */ );
	}

	/* Make sure files supposed to be dropped are dropped */
	if (parsed->nrels > 0)
	{
		/*
		 * First update minimum recovery point to cover this WAL record. Once
		 * a relation is deleted, there's no going back. The buffer manager
		 * enforces the WAL-first rule for normal updates to relation files,
		 * so that the minimum recovery point is always updated before the
		 * corresponding change in the data file is flushed to disk, but we
		 * have to do the same here since we're bypassing the buffer manager.
		 *
		 * Doing this before deleting the files means that if a deletion fails
		 * for some reason, you cannot start up the system even after restart,
		 * until you fix the underlying situation so that the deletion will
		 * succeed. Alternatively, we could update the minimum recovery point
		 * after deletion, but that would leave a small window where the
		 * WAL-first rule would be violated.
		 */
		XLogFlush(lsn);

		/* Make sure files supposed to be dropped are dropped */
		DropRelationFiles(parsed->xnodes, parsed->nrels, true);
	}

	if (parsed->nstats > 0)
	{
		/* see equivalent call for relations above */
		XLogFlush(lsn);

		pgstat_execute_transactional_drops(parsed->nstats, parsed->stats, true);
	}

	/*
	 * We issue an XLogFlush() for the same reason we emit ForceSyncCommit()
	 * in normal operation. For example, in CREATE DATABASE, we copy all files
	 * from the template database, and then commit the transaction. If we
	 * crash after all the files have been copied but before the commit, you
	 * have files in the data directory without an entry in pg_database. To
	 * minimize the window for that, we use ForceSyncCommit() to rush the
	 * commit record to disk as quick as possible. We have the same window
	 * during recovery, and forcing an XLogFlush() (which updates
	 * minRecoveryPoint during recovery) helps to reduce that problem window,
	 * for any user that requested ForceSyncCommit().
	 */
	if (XactCompletionForceSyncCommit(parsed->xinfo))
		XLogFlush(lsn);

	/*
	 * If asked by the primary (because someone is waiting for a synchronous
	 * commit = remote_apply), we will need to ask walreceiver to send a reply
	 * immediately.
	 */
	if (XactCompletionApplyFeedback(parsed->xinfo))
		XLogRequestWalReceiverReply();
}

/*
 * Be careful with the order of execution, as with xact_redo_commit().
 * The two functions are similar but differ in key places.
 *
 * Note also that an abort can be for a subtransaction and its children,
 * not just for a top level abort. That means we have to consider
 * topxid != xid, whereas in commit we would find topxid == xid always
 * because subtransaction commit is never WAL logged.
 */
static void
xact_redo_abort(xl_xact_parsed_abort *parsed, TransactionId xid,
				XLogRecPtr lsn, RepOriginId origin_id)
{
	TransactionId max_xid;

	Assert(TransactionIdIsValid(xid));

	/* Make sure nextXid is beyond any XID mentioned in the record. */
	max_xid = TransactionIdLatest(xid,
								  parsed->nsubxacts,
								  parsed->subxacts);
	AdvanceNextFullTransactionIdPastXid(max_xid);

	if (standbyState == STANDBY_DISABLED)
	{
		/* Mark the transaction aborted in pg_xact, no need for async stuff */
		TransactionIdAbortTree(xid, parsed->nsubxacts, parsed->subxacts);
	}
	else
	{
		/*
		 * If a transaction completion record arrives that has as-yet
		 * unobserved subtransactions then this will not have been fully
		 * handled by the call to RecordKnownAssignedTransactionIds() in the
		 * main recovery loop in xlog.c. So we need to do bookkeeping again to
		 * cover that case. This is confusing and it is easy to think this
		 * call is irrelevant, which has happened three times in development
		 * already. Leave it in.
		 */
		RecordKnownAssignedTransactionIds(max_xid);

		/* Mark the transaction aborted in pg_xact, no need for async stuff */
		TransactionIdAbortTree(xid, parsed->nsubxacts, parsed->subxacts);

		/*
		 * We must update the ProcArray after we have marked clog.
		 */
		ExpireTreeKnownAssignedTransactionIds(xid, parsed->nsubxacts, parsed->subxacts, max_xid);

		/*
		 * There are no invalidation messages to send or undo.
		 */

		/*
		 * Release locks, if any. There are no invalidations to send.
		 */
		if (parsed->xinfo & XACT_XINFO_HAS_AE_LOCKS)
			StandbyReleaseLockTree(xid, parsed->nsubxacts, parsed->subxacts);
	}

	if (parsed->xinfo & XACT_XINFO_HAS_ORIGIN)
	{
		/* recover apply progress */
		replorigin_advance(origin_id, parsed->origin_lsn, lsn,
						   false /* backward */ , false /* WAL */ );
	}

	/* Make sure files supposed to be dropped are dropped */
	if (parsed->nrels > 0)
	{
		/*
		 * See comments about update of minimum recovery point on truncation,
		 * in xact_redo_commit().
		 */
		XLogFlush(lsn);

		DropRelationFiles(parsed->xnodes, parsed->nrels, true);
	}

	if (parsed->nstats > 0)
	{
		/* see equivalent call for relations above */
		XLogFlush(lsn);

		pgstat_execute_transactional_drops(parsed->nstats, parsed->stats, true);
	}
}

void
xact_redo(XLogReaderState *record)
{
	uint8		info = XLogRecGetInfo(record) & XLOG_XACT_OPMASK;

	/* Backup blocks are not used in xact records */
	Assert(!XLogRecHasAnyBlockRefs(record));

	if (info == XLOG_XACT_COMMIT)
	{
		xl_xact_commit *xlrec = (xl_xact_commit *) XLogRecGetData(record);
		xl_xact_parsed_commit parsed;

		ParseCommitRecord(XLogRecGetInfo(record), xlrec, &parsed);
		xact_redo_commit(&parsed, XLogRecGetXid(record),
						 record->EndRecPtr, XLogRecGetOrigin(record));
	}
	else if (info == XLOG_XACT_COMMIT_PREPARED)
	{
		xl_xact_commit *xlrec = (xl_xact_commit *) XLogRecGetData(record);
		xl_xact_parsed_commit parsed;

		ParseCommitRecord(XLogRecGetInfo(record), xlrec, &parsed);
		xact_redo_commit(&parsed, parsed.twophase_xid,
						 record->EndRecPtr, XLogRecGetOrigin(record));

		/* Delete TwoPhaseState gxact entry and/or 2PC file. */
		LWLockAcquire(TwoPhaseStateLock, LW_EXCLUSIVE);
		PrepareRedoRemove(parsed.twophase_xid, false);
		LWLockRelease(TwoPhaseStateLock);
	}
	else if (info == XLOG_XACT_ABORT)
	{
		xl_xact_abort *xlrec = (xl_xact_abort *) XLogRecGetData(record);
		xl_xact_parsed_abort parsed;

		ParseAbortRecord(XLogRecGetInfo(record), xlrec, &parsed);
		xact_redo_abort(&parsed, XLogRecGetXid(record),
						record->EndRecPtr, XLogRecGetOrigin(record));
	}
	else if (info == XLOG_XACT_ABORT_PREPARED)
	{
		xl_xact_abort *xlrec = (xl_xact_abort *) XLogRecGetData(record);
		xl_xact_parsed_abort parsed;

		ParseAbortRecord(XLogRecGetInfo(record), xlrec, &parsed);
		xact_redo_abort(&parsed, parsed.twophase_xid,
						record->EndRecPtr, XLogRecGetOrigin(record));

		/* Delete TwoPhaseState gxact entry and/or 2PC file. */
		LWLockAcquire(TwoPhaseStateLock, LW_EXCLUSIVE);
		PrepareRedoRemove(parsed.twophase_xid, false);
		LWLockRelease(TwoPhaseStateLock);
	}
	else if (info == XLOG_XACT_PREPARE)
	{
		/*
		 * Store xid and start/end pointers of the WAL record in TwoPhaseState
		 * gxact entry.
		 */
		LWLockAcquire(TwoPhaseStateLock, LW_EXCLUSIVE);
		PrepareRedoAdd(XLogRecGetData(record),
					   record->ReadRecPtr,
					   record->EndRecPtr,
					   XLogRecGetOrigin(record));
		LWLockRelease(TwoPhaseStateLock);
	}
	else if (info == XLOG_XACT_ASSIGNMENT)
	{
		xl_xact_assignment *xlrec = (xl_xact_assignment *) XLogRecGetData(record);

		if (standbyState >= STANDBY_INITIALIZED)
			ProcArrayApplyXidAssignment(xlrec->xtop,
										xlrec->nsubxacts, xlrec->xsub);
	}
	else if (info == XLOG_XACT_INVALIDATIONS)
	{
		/*
		 * XXX we do ignore this for now, what matters are invalidations
		 * written into the commit record.
		 */
	}
	else
		elog(PANIC, "xact_redo: unknown op code %u", info);
}

void
YBSaveDdlHandle(YBCPgStatement handle)
{
	CurrentTransactionState->YBPostponedDdlOps = lappend(CurrentTransactionState->YBPostponedDdlOps, handle);
}

List *
YBGetDdlHandles()
{
	return CurrentTransactionState->YBPostponedDdlOps;
}

void
YBClearDdlHandles()
{
	CurrentTransactionState->YBPostponedDdlOps = NULL;
}

void
YbClearCurrentTransactionId()
{
	CurrentTransactionState->fullTransactionId = InvalidFullTransactionId;
	MyProc->xid = InvalidTransactionId;
}

/*
 * YbClearParallelContexts
 * Clean up parallel contexts as a part of transparent query restart.
 */
void
YbClearParallelContexts()
{
	TransactionState s = CurrentTransactionState;
	Assert(IsInParallelMode());
	if (s->subTransactionId == InvalidSubTransactionId)
		AtEOXact_Parallel(false);
	else
		AtEOSubXact_Parallel(false, s->subTransactionId);
	ExitParallelMode();
}

/*
 * ```increment_sticky_object_count()``` is called when any database object which requires
 * stickiness is created.
 */
void
increment_sticky_object_count()
{
	CurrentTransactionState->ybUncommittedStickyObjectCount++;
}

/*
 * ```decrement_sticky_object_count()``` is called when any database object which required
 * stickiness is deleted.
 */
void
decrement_sticky_object_count()
{
	CurrentTransactionState->ybUncommittedStickyObjectCount--;
}

/*
 * Check if all sub transactions are internal ones started before each statement for READ COMMITTED
 * isolation level.
 */
bool
YbHasOnlyInternalRcSubTransactions()
{
	for (TransactionState s = CurrentTransactionState; s != NULL; s = s->parent)
	{
		if (s->nestingLevel >= 2 && !s->ybIsInternalRcSubTransaction)
			return false;
	}
	return true;
}
