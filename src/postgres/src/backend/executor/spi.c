/*-------------------------------------------------------------------------
 *
 * spi.c
 *				Server Programming Interface
 *
 * Portions Copyright (c) 1996-2018, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/executor/spi.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/htup_details.h"
#include "access/printtup.h"
#include "access/sysattr.h"
#include "access/xact.h"
#include "catalog/heap.h"
#include "catalog/pg_type.h"
#include "commands/trigger.h"
#include "executor/executor.h"
#include "executor/spi_priv.h"
#include "miscadmin.h"
#include "tcop/pquery.h"
#include "tcop/utility.h"
#include "utils/builtins.h"
#include "utils/datum.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "utils/rel.h"
#include "utils/snapmgr.h"
#include "utils/syscache.h"
#include "utils/typcache.h"

/* YB includes. */
#include "pg_yb_utils.h"


/*
 * These global variables are part of the API for various SPI functions
 * (a horrible API choice, but it's too late now).  To reduce the risk of
 * interference between different SPI callers, we save and restore them
 * when entering/exiting a SPI nesting level.
 */
uint64		SPI_processed = 0;
Oid			SPI_lastoid = InvalidOid;
SPITupleTable *SPI_tuptable = NULL;
int			SPI_result = 0;

static _SPI_connection *_SPI_stack = NULL;
static _SPI_connection *_SPI_current = NULL;
static int	_SPI_stack_depth = 0;	/* allocated size of _SPI_stack */
static int	_SPI_connected = -1;	/* current stack index */

static Portal SPI_cursor_open_internal(const char *name, SPIPlanPtr plan,
						 ParamListInfo paramLI, bool read_only);

static void _SPI_prepare_plan(const char *src, SPIPlanPtr plan);

static void _SPI_prepare_oneshot_plan(const char *src, SPIPlanPtr plan);

static int _SPI_execute_plan(SPIPlanPtr plan, ParamListInfo paramLI,
				  Snapshot snapshot, Snapshot crosscheck_snapshot,
				  bool read_only, bool fire_triggers, uint64 tcount,
				  bool yb_reuse_existing_snapshot_in_read_committed);

static ParamListInfo _SPI_convert_params(int nargs, Oid *argtypes,
					Datum *Values, const char *Nulls);

static int	_SPI_pquery(QueryDesc *queryDesc, bool fire_triggers, uint64 tcount);

static void _SPI_error_callback(void *arg);

static void _SPI_cursor_operation(Portal portal,
					  FetchDirection direction, long count,
					  DestReceiver *dest);

static SPIPlanPtr _SPI_make_plan_non_temp(SPIPlanPtr plan);
static SPIPlanPtr _SPI_save_plan(SPIPlanPtr plan);

static int	_SPI_begin_call(bool use_exec);
static int	_SPI_end_call(bool use_exec);
static MemoryContext _SPI_execmem(void);
static MemoryContext _SPI_procmem(void);
static bool _SPI_checktuples(void);


/* =================== interface functions =================== */

int
SPI_connect(void)
{
	return SPI_connect_ext(0);
}

int
SPI_connect_ext(int options)
{
	int			newdepth;

	/* Enlarge stack if necessary */
	if (_SPI_stack == NULL)
	{
		if (_SPI_connected != -1 || _SPI_stack_depth != 0)
			elog(ERROR, "SPI stack corrupted");
		newdepth = 16;
		_SPI_stack = (_SPI_connection *)
			MemoryContextAlloc(TopMemoryContext,
							   newdepth * sizeof(_SPI_connection));
		_SPI_stack_depth = newdepth;
	}
	else
	{
		if (_SPI_stack_depth <= 0 || _SPI_stack_depth <= _SPI_connected)
			elog(ERROR, "SPI stack corrupted");
		if (_SPI_stack_depth == _SPI_connected + 1)
		{
			newdepth = _SPI_stack_depth * 2;
			_SPI_stack = (_SPI_connection *)
				repalloc(_SPI_stack,
						 newdepth * sizeof(_SPI_connection));
			_SPI_stack_depth = newdepth;
		}
	}

	/* Enter new stack level */
	_SPI_connected++;
	Assert(_SPI_connected >= 0 && _SPI_connected < _SPI_stack_depth);

	_SPI_current = &(_SPI_stack[_SPI_connected]);
	_SPI_current->processed = 0;
	_SPI_current->lastoid = InvalidOid;
	_SPI_current->tuptable = NULL;
	_SPI_current->execSubid = InvalidSubTransactionId;
	slist_init(&_SPI_current->tuptables);
	_SPI_current->procCxt = NULL;	/* in case we fail to create 'em */
	_SPI_current->execCxt = NULL;
	_SPI_current->connectSubid = GetCurrentSubTransactionId();
	_SPI_current->queryEnv = NULL;
	_SPI_current->atomic = (options & SPI_OPT_NONATOMIC ? false : true);
	_SPI_current->internal_xact = false;
	_SPI_current->outer_processed = SPI_processed;
	_SPI_current->outer_lastoid = SPI_lastoid;
	_SPI_current->outer_tuptable = SPI_tuptable;
	_SPI_current->outer_result = SPI_result;

	/*
	 * Create memory contexts for this procedure
	 *
	 * In atomic contexts (the normal case), we use TopTransactionContext,
	 * otherwise PortalContext, so that it lives across transaction
	 * boundaries.
	 *
	 * XXX It could be better to use PortalContext as the parent context in
	 * all cases, but we may not be inside a portal (consider deferred-trigger
	 * execution).  Perhaps CurTransactionContext could be an option?  For now
	 * it doesn't matter because we clean up explicitly in AtEOSubXact_SPI().
	 */
	_SPI_current->procCxt = AllocSetContextCreate(_SPI_current->atomic ? TopTransactionContext : PortalContext,
												  "SPI Proc",
												  ALLOCSET_DEFAULT_SIZES);
	_SPI_current->execCxt = AllocSetContextCreate(_SPI_current->atomic ? TopTransactionContext : _SPI_current->procCxt,
												  "SPI Exec",
												  ALLOCSET_DEFAULT_SIZES);
	/* ... and switch to procedure's context */
	_SPI_current->savedcxt = MemoryContextSwitchTo(_SPI_current->procCxt);

	/*
	 * Reset API global variables so that current caller cannot accidentally
	 * depend on state of an outer caller.
	 */
	SPI_processed = 0;
	SPI_lastoid = InvalidOid;
	SPI_tuptable = NULL;
	SPI_result = 0;

	return SPI_OK_CONNECT;
}

int
SPI_finish(void)
{
	int			res;

	res = _SPI_begin_call(false);	/* just check we're connected */
	if (res < 0)
		return res;

	/* Restore memory context as it was before procedure call */
	MemoryContextSwitchTo(_SPI_current->savedcxt);

	/* Release memory used in procedure call (including tuptables) */
	MemoryContextDelete(_SPI_current->execCxt);
	_SPI_current->execCxt = NULL;
	MemoryContextDelete(_SPI_current->procCxt);
	_SPI_current->procCxt = NULL;

	/*
	 * Restore outer API variables, especially SPI_tuptable which is probably
	 * pointing at a just-deleted tuptable
	 */
	SPI_processed = _SPI_current->outer_processed;
	SPI_lastoid = _SPI_current->outer_lastoid;
	SPI_tuptable = _SPI_current->outer_tuptable;
	SPI_result = _SPI_current->outer_result;

	/* Exit stack level */
	_SPI_connected--;
	if (_SPI_connected < 0)
		_SPI_current = NULL;
	else
		_SPI_current = &(_SPI_stack[_SPI_connected]);

	return SPI_OK_FINISH;
}

void
SPI_start_transaction(void)
{
	MemoryContext oldcontext = GetCurrentMemoryContext();

	StartTransactionCommand();
	MemoryContextSwitchTo(oldcontext);
}

void
SPI_commit(void)
{
	MemoryContext oldcontext = GetCurrentMemoryContext();

	if (_SPI_current->atomic)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TRANSACTION_TERMINATION),
				 errmsg("invalid transaction termination")));

	/*
	 * This restriction is required by PLs implemented on top of SPI.  They
	 * use subtransactions to establish exception blocks that are supposed to
	 * be rolled back together if there is an error.  Terminating the
	 * top-level transaction in such a block violates that idea.  A future PL
	 * implementation might have different ideas about this, in which case
	 * this restriction would have to be refined or the check possibly be
	 * moved out of SPI into the PLs.
	 */
	if (IsSubTransaction())
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TRANSACTION_TERMINATION),
				 errmsg("cannot commit while a subtransaction is active")));

	_SPI_current->internal_xact = true;

	/*
	 * Before committing, pop all active snapshots to avoid error about
	 * "snapshot %p still active".
	 */
	while (ActiveSnapshotSet())
		PopActiveSnapshot();

	CommitTransactionCommand();
	MemoryContextSwitchTo(oldcontext);

	_SPI_current->internal_xact = false;
}

void
SPI_rollback(void)
{
	MemoryContext oldcontext = GetCurrentMemoryContext();

	if (_SPI_current->atomic)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TRANSACTION_TERMINATION),
				 errmsg("invalid transaction termination")));

	/* see under SPI_commit() */
	if (IsSubTransaction())
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TRANSACTION_TERMINATION),
				 errmsg("cannot roll back while a subtransaction is active")));

	_SPI_current->internal_xact = true;

	AbortCurrentTransaction();
	MemoryContextSwitchTo(oldcontext);

	_SPI_current->internal_xact = false;
}

/*
 * Clean up SPI state.  Called on transaction end (of non-SPI-internal
 * transactions) and when returning to the main loop on error.
 */
void
SPICleanup(void)
{
	_SPI_current = NULL;
	_SPI_connected = -1;
	/* Reset API global variables, too */
	SPI_processed = 0;
	SPI_lastoid = InvalidOid;
	SPI_tuptable = NULL;
	SPI_result = 0;
}

/*
 * Clean up SPI state at transaction commit or abort.
 */
void
AtEOXact_SPI(bool isCommit)
{
	/* Do nothing if the transaction end was initiated by SPI. */
	if (_SPI_current && _SPI_current->internal_xact)
		return;

	if (isCommit && _SPI_connected != -1)
		ereport(WARNING,
				(errcode(ERRCODE_WARNING),
				 errmsg("transaction left non-empty SPI stack"),
				 errhint("Check for missing \"SPI_finish\" calls.")));

	SPICleanup();
}

/*
 * Clean up SPI state at subtransaction commit or abort.
 *
 * During commit, there shouldn't be any unclosed entries remaining from
 * the current subtransaction; we emit a warning if any are found.
 */
void
AtEOSubXact_SPI(bool isCommit, SubTransactionId mySubid)
{
	bool		found = false;

	while (_SPI_connected >= 0)
	{
		_SPI_connection *connection = &(_SPI_stack[_SPI_connected]);

		if (connection->connectSubid != mySubid)
			break;				/* couldn't be any underneath it either */

		if (connection->internal_xact)
			break;

		found = true;

		/*
		 * Release procedure memory explicitly (see note in SPI_connect)
		 */
		if (connection->execCxt)
		{
			MemoryContextDelete(connection->execCxt);
			connection->execCxt = NULL;
		}
		if (connection->procCxt)
		{
			MemoryContextDelete(connection->procCxt);
			connection->procCxt = NULL;
		}

		/*
		 * Restore outer global variables and pop the stack entry.  Unlike
		 * SPI_finish(), we don't risk switching to memory contexts that might
		 * be already gone.
		 */
		SPI_processed = connection->outer_processed;
		SPI_lastoid = connection->outer_lastoid;
		SPI_tuptable = connection->outer_tuptable;
		SPI_result = connection->outer_result;

		_SPI_connected--;
		if (_SPI_connected < 0)
			_SPI_current = NULL;
		else
			_SPI_current = &(_SPI_stack[_SPI_connected]);
	}

	if (found && isCommit)
		ereport(WARNING,
				(errcode(ERRCODE_WARNING),
				 errmsg("subtransaction left non-empty SPI stack"),
				 errhint("Check for missing \"SPI_finish\" calls.")));

	/*
	 * If we are aborting a subtransaction and there is an open SPI context
	 * surrounding the subxact, clean up to prevent memory leakage.
	 */
	if (_SPI_current && !isCommit)
	{
		slist_mutable_iter siter;

		/*
		 * Throw away executor state if current executor operation was started
		 * within current subxact (essentially, force a _SPI_end_call(true)).
		 */
		if (_SPI_current->execSubid >= mySubid)
		{
			_SPI_current->execSubid = InvalidSubTransactionId;
			MemoryContextResetAndDeleteChildren(_SPI_current->execCxt);
		}

		/* throw away any tuple tables created within current subxact */
		slist_foreach_modify(siter, &_SPI_current->tuptables)
		{
			SPITupleTable *tuptable;

			tuptable = slist_container(SPITupleTable, next, siter.cur);
			if (tuptable->subid >= mySubid)
			{
				/*
				 * If we used SPI_freetuptable() here, its internal search of
				 * the tuptables list would make this operation O(N^2).
				 * Instead, just free the tuptable manually.  This should
				 * match what SPI_freetuptable() does.
				 */
				slist_delete_current(&siter);
				if (tuptable == _SPI_current->tuptable)
					_SPI_current->tuptable = NULL;
				if (tuptable == SPI_tuptable)
					SPI_tuptable = NULL;
				MemoryContextDelete(tuptable->tuptabcxt);
			}
		}
	}
}

/*
 * Are we executing inside a procedure (that is, a nonatomic SPI context)?
 */
bool
SPI_inside_nonatomic_context(void)
{
	if (_SPI_current == NULL)
		return false;			/* not in any SPI context at all */
	if (_SPI_current->atomic)
		return false;			/* it's atomic (ie function not procedure) */
	return true;
}


/* Parse, plan, and execute a query string */
int
SPI_execute(const char *src, bool read_only, long tcount)
{
	_SPI_plan	plan;
	int			res;

	if (src == NULL || tcount < 0)
		return SPI_ERROR_ARGUMENT;

	res = _SPI_begin_call(true);
	if (res < 0)
		return res;

	memset(&plan, 0, sizeof(_SPI_plan));
	plan.magic = _SPI_PLAN_MAGIC;
	plan.cursor_options = CURSOR_OPT_PARALLEL_OK;

	_SPI_prepare_oneshot_plan(src, &plan);

	res = _SPI_execute_plan(&plan, NULL,
							InvalidSnapshot, InvalidSnapshot,
							read_only, true, tcount,
							false /* yb_reuse_existing_snapshot_in_read_committed */ );

	_SPI_end_call(true);
	return res;
}

/* Obsolete version of SPI_execute */
int
SPI_exec(const char *src, long tcount)
{
	return SPI_execute(src, false, tcount);
}

/* Execute a previously prepared plan */
int
SPI_execute_plan(SPIPlanPtr plan, Datum *Values, const char *Nulls,
				 bool read_only, long tcount)
{
	int			res;

	if (plan == NULL || plan->magic != _SPI_PLAN_MAGIC || tcount < 0)
		return SPI_ERROR_ARGUMENT;

	if (plan->nargs > 0 && Values == NULL)
		return SPI_ERROR_PARAM;

	res = _SPI_begin_call(true);
	if (res < 0)
		return res;

	res = _SPI_execute_plan(plan,
							_SPI_convert_params(plan->nargs, plan->argtypes,
												Values, Nulls),
							InvalidSnapshot, InvalidSnapshot,
							read_only, true, tcount,
							false /* yb_reuse_existing_snapshot_in_read_committed */ );

	_SPI_end_call(true);
	return res;
}

/* Obsolete version of SPI_execute_plan */
int
SPI_execp(SPIPlanPtr plan, Datum *Values, const char *Nulls, long tcount)
{
	return SPI_execute_plan(plan, Values, Nulls, false, tcount);
}

/* Execute a previously prepared plan */
int
SPI_execute_plan_with_paramlist(SPIPlanPtr plan, ParamListInfo params,
								bool read_only, long tcount)
{
	return SPI_yb_execute_plan_with_paramlist(plan, params, read_only, tcount,
											  false /* yb_reuse_existing_snapshot_in_read_committed */ );
}

int
SPI_yb_execute_plan_with_paramlist(SPIPlanPtr plan, ParamListInfo params,
								   bool read_only, long tcount,
								   bool yb_reuse_existing_snapshot_in_read_committed)
{
	int			res;

	if (plan == NULL || plan->magic != _SPI_PLAN_MAGIC || tcount < 0)
		return SPI_ERROR_ARGUMENT;

	res = _SPI_begin_call(true);
	if (res < 0)
		return res;

	res = _SPI_execute_plan(plan, params,
							InvalidSnapshot, InvalidSnapshot,
							read_only, true, tcount,
							yb_reuse_existing_snapshot_in_read_committed);

	_SPI_end_call(true);
	return res;
}

/*
 * SPI_execute_snapshot -- identical to SPI_execute_plan, except that we allow
 * the caller to specify exactly which snapshots to use, which will be
 * registered here.  Also, the caller may specify that AFTER triggers should be
 * queued as part of the outer query rather than being fired immediately at the
 * end of the command.
 *
 * This is currently not documented in spi.sgml because it is only intended
 * for use by RI triggers.
 *
 * Passing snapshot == InvalidSnapshot will select the normal behavior of
 * fetching a new snapshot for each query.
 */
int
SPI_execute_snapshot(SPIPlanPtr plan,
					 Datum *Values, const char *Nulls,
					 Snapshot snapshot, Snapshot crosscheck_snapshot,
					 bool read_only, bool fire_triggers, long tcount)
{
	int			res;

	if (plan == NULL || plan->magic != _SPI_PLAN_MAGIC || tcount < 0)
		return SPI_ERROR_ARGUMENT;

	if (plan->nargs > 0 && Values == NULL)
		return SPI_ERROR_PARAM;

	res = _SPI_begin_call(true);
	if (res < 0)
		return res;

	res = _SPI_execute_plan(plan,
							_SPI_convert_params(plan->nargs, plan->argtypes,
												Values, Nulls),
							snapshot, crosscheck_snapshot,
							read_only, fire_triggers, tcount,
							false /* yb_reuse_existing_snapshot_in_read_committed */ );

	_SPI_end_call(true);
	return res;
}

/*
 * SPI_execute_with_args -- plan and execute a query with supplied arguments
 *
 * This is functionally equivalent to SPI_prepare followed by
 * SPI_execute_plan.
 */
int
SPI_execute_with_args(const char *src,
					  int nargs, Oid *argtypes,
					  Datum *Values, const char *Nulls,
					  bool read_only, long tcount)
{
	int			res;
	_SPI_plan	plan;
	ParamListInfo paramLI;

	if (src == NULL || nargs < 0 || tcount < 0)
		return SPI_ERROR_ARGUMENT;

	if (nargs > 0 && (argtypes == NULL || Values == NULL))
		return SPI_ERROR_PARAM;

	res = _SPI_begin_call(true);
	if (res < 0)
		return res;

	memset(&plan, 0, sizeof(_SPI_plan));
	plan.magic = _SPI_PLAN_MAGIC;
	plan.cursor_options = CURSOR_OPT_PARALLEL_OK;
	plan.nargs = nargs;
	plan.argtypes = argtypes;
	plan.parserSetup = NULL;
	plan.parserSetupArg = NULL;

	paramLI = _SPI_convert_params(nargs, argtypes,
								  Values, Nulls);

	_SPI_prepare_oneshot_plan(src, &plan);

	res = _SPI_execute_plan(&plan, paramLI,
							InvalidSnapshot, InvalidSnapshot,
							read_only, true, tcount,
							false /* yb_reuse_existing_snapshot_in_read_committed */ );

	_SPI_end_call(true);
	return res;
}

SPIPlanPtr
SPI_prepare(const char *src, int nargs, Oid *argtypes)
{
	return SPI_prepare_cursor(src, nargs, argtypes, 0);
}

SPIPlanPtr
SPI_prepare_cursor(const char *src, int nargs, Oid *argtypes,
				   int cursorOptions)
{
	_SPI_plan	plan;
	SPIPlanPtr	result;

	if (src == NULL || nargs < 0 || (nargs > 0 && argtypes == NULL))
	{
		SPI_result = SPI_ERROR_ARGUMENT;
		return NULL;
	}

	SPI_result = _SPI_begin_call(true);
	if (SPI_result < 0)
		return NULL;

	memset(&plan, 0, sizeof(_SPI_plan));
	plan.magic = _SPI_PLAN_MAGIC;
	plan.cursor_options = cursorOptions;
	plan.nargs = nargs;
	plan.argtypes = argtypes;
	plan.parserSetup = NULL;
	plan.parserSetupArg = NULL;

	_SPI_prepare_plan(src, &plan);

	/* copy plan to procedure context */
	result = _SPI_make_plan_non_temp(&plan);

	_SPI_end_call(true);

	return result;
}

SPIPlanPtr
SPI_prepare_params(const char *src,
				   ParserSetupHook parserSetup,
				   void *parserSetupArg,
				   int cursorOptions)
{
	_SPI_plan	plan;
	SPIPlanPtr	result;

	if (src == NULL)
	{
		SPI_result = SPI_ERROR_ARGUMENT;
		return NULL;
	}

	SPI_result = _SPI_begin_call(true);
	if (SPI_result < 0)
		return NULL;

	memset(&plan, 0, sizeof(_SPI_plan));
	plan.magic = _SPI_PLAN_MAGIC;
	plan.cursor_options = cursorOptions;
	plan.nargs = 0;
	plan.argtypes = NULL;
	plan.parserSetup = parserSetup;
	plan.parserSetupArg = parserSetupArg;

	_SPI_prepare_plan(src, &plan);

	/* copy plan to procedure context */
	result = _SPI_make_plan_non_temp(&plan);

	_SPI_end_call(true);

	return result;
}

int
SPI_keepplan(SPIPlanPtr plan)
{
	ListCell   *lc;

	if (plan == NULL || plan->magic != _SPI_PLAN_MAGIC ||
		plan->saved || plan->oneshot)
		return SPI_ERROR_ARGUMENT;

	/*
	 * Mark it saved, reparent it under CacheMemoryContext, and mark all the
	 * component CachedPlanSources as saved.  This sequence cannot fail
	 * partway through, so there's no risk of long-term memory leakage.
	 */
	plan->saved = true;
	MemoryContextSetParent(plan->plancxt, CacheMemoryContext);

	foreach(lc, plan->plancache_list)
	{
		CachedPlanSource *plansource = (CachedPlanSource *) lfirst(lc);

		SaveCachedPlan(plansource);
	}

	return 0;
}

SPIPlanPtr
SPI_saveplan(SPIPlanPtr plan)
{
	SPIPlanPtr	newplan;

	if (plan == NULL || plan->magic != _SPI_PLAN_MAGIC)
	{
		SPI_result = SPI_ERROR_ARGUMENT;
		return NULL;
	}

	SPI_result = _SPI_begin_call(false);	/* don't change context */
	if (SPI_result < 0)
		return NULL;

	newplan = _SPI_save_plan(plan);

	SPI_result = _SPI_end_call(false);

	return newplan;
}

int
SPI_freeplan(SPIPlanPtr plan)
{
	ListCell   *lc;

	if (plan == NULL || plan->magic != _SPI_PLAN_MAGIC)
		return SPI_ERROR_ARGUMENT;

	/* Release the plancache entries */
	foreach(lc, plan->plancache_list)
	{
		CachedPlanSource *plansource = (CachedPlanSource *) lfirst(lc);

		DropCachedPlan(plansource);
	}

	/* Now get rid of the _SPI_plan and subsidiary data in its plancxt */
	MemoryContextDelete(plan->plancxt);

	return 0;
}

HeapTuple
SPI_copytuple(HeapTuple tuple)
{
	MemoryContext oldcxt;
	HeapTuple	ctuple;

	if (tuple == NULL)
	{
		SPI_result = SPI_ERROR_ARGUMENT;
		return NULL;
	}

	if (_SPI_current == NULL)
	{
		SPI_result = SPI_ERROR_UNCONNECTED;
		return NULL;
	}

	oldcxt = MemoryContextSwitchTo(_SPI_current->savedcxt);

	ctuple = heap_copytuple(tuple);

	MemoryContextSwitchTo(oldcxt);

	return ctuple;
}

HeapTupleHeader
SPI_returntuple(HeapTuple tuple, TupleDesc tupdesc)
{
	MemoryContext oldcxt;
	HeapTupleHeader dtup;

	if (tuple == NULL || tupdesc == NULL)
	{
		SPI_result = SPI_ERROR_ARGUMENT;
		return NULL;
	}

	if (_SPI_current == NULL)
	{
		SPI_result = SPI_ERROR_UNCONNECTED;
		return NULL;
	}

	/* For RECORD results, make sure a typmod has been assigned */
	if (tupdesc->tdtypeid == RECORDOID &&
		tupdesc->tdtypmod < 0)
		assign_record_type_typmod(tupdesc);

	oldcxt = MemoryContextSwitchTo(_SPI_current->savedcxt);

	dtup = DatumGetHeapTupleHeader(heap_copy_tuple_as_datum(tuple, tupdesc));

	MemoryContextSwitchTo(oldcxt);

	return dtup;
}

HeapTuple
SPI_modifytuple(Relation rel, HeapTuple tuple, int natts, int *attnum,
				Datum *Values, const char *Nulls)
{
	MemoryContext oldcxt;
	HeapTuple	mtuple;
	int			numberOfAttributes;
	Datum	   *v;
	bool	   *n;
	int			i;

	if (rel == NULL || tuple == NULL || natts < 0 || attnum == NULL || Values == NULL)
	{
		SPI_result = SPI_ERROR_ARGUMENT;
		return NULL;
	}

	if (_SPI_current == NULL)
	{
		SPI_result = SPI_ERROR_UNCONNECTED;
		return NULL;
	}

	oldcxt = MemoryContextSwitchTo(_SPI_current->savedcxt);

	SPI_result = 0;

	numberOfAttributes = rel->rd_att->natts;
	v = (Datum *) palloc(numberOfAttributes * sizeof(Datum));
	n = (bool *) palloc(numberOfAttributes * sizeof(bool));

	/* fetch old values and nulls */
	heap_deform_tuple(tuple, rel->rd_att, v, n);

	/* replace values and nulls */
	for (i = 0; i < natts; i++)
	{
		if (attnum[i] <= 0 || attnum[i] > numberOfAttributes)
			break;
		v[attnum[i] - 1] = Values[i];
		n[attnum[i] - 1] = (Nulls && Nulls[i] == 'n') ? true : false;
	}

	if (i == natts)				/* no errors in *attnum */
	{
		mtuple = heap_form_tuple(rel->rd_att, v, n);

		/*
		 * copy the identification info of the old tuple: t_ctid, t_self, and
		 * OID (if any)
		 */
		mtuple->t_data->t_ctid = tuple->t_data->t_ctid;
		mtuple->t_self = tuple->t_self;
		mtuple->t_tableOid = tuple->t_tableOid;
		HEAPTUPLE_COPY_YBCTID(tuple->t_ybctid, mtuple->t_ybctid);
		if (rel->rd_att->tdhasoid)
			HeapTupleSetOid(mtuple, HeapTupleGetOid(tuple));
	}
	else
	{
		mtuple = NULL;
		SPI_result = SPI_ERROR_NOATTRIBUTE;
	}

	pfree(v);
	pfree(n);

	MemoryContextSwitchTo(oldcxt);

	return mtuple;
}

int
SPI_fnumber(TupleDesc tupdesc, const char *fname)
{
	int			res;
	Form_pg_attribute sysatt;

	for (res = 0; res < tupdesc->natts; res++)
	{
		Form_pg_attribute attr = TupleDescAttr(tupdesc, res);

		if (namestrcmp(&attr->attname, fname) == 0 &&
			!attr->attisdropped)
			return res + 1;
	}

	sysatt = SystemAttributeByName(fname, true /* "oid" will be accepted */ );
	if (sysatt != NULL)
		return sysatt->attnum;

	/* SPI_ERROR_NOATTRIBUTE is different from all sys column numbers */
	return SPI_ERROR_NOATTRIBUTE;
}

char *
SPI_fname(TupleDesc tupdesc, int fnumber)
{
	Form_pg_attribute att;

	SPI_result = 0;

	if (fnumber > tupdesc->natts || fnumber == 0 ||
		fnumber <= FirstLowInvalidHeapAttributeNumber)
	{
		SPI_result = SPI_ERROR_NOATTRIBUTE;
		return NULL;
	}

	if (fnumber > 0)
		att = TupleDescAttr(tupdesc, fnumber - 1);
	else
		att = SystemAttributeDefinition(fnumber, true);

	return pstrdup(NameStr(att->attname));
}

char *
SPI_getvalue(HeapTuple tuple, TupleDesc tupdesc, int fnumber)
{
	Datum		val;
	bool		isnull;
	Oid			typoid,
				foutoid;
	bool		typisvarlena;

	SPI_result = 0;

	if (fnumber > tupdesc->natts || fnumber == 0 ||
		fnumber <= FirstLowInvalidHeapAttributeNumber)
	{
		SPI_result = SPI_ERROR_NOATTRIBUTE;
		return NULL;
	}

	val = heap_getattr(tuple, fnumber, tupdesc, &isnull);
	if (isnull)
		return NULL;

	if (fnumber > 0)
		typoid = TupleDescAttr(tupdesc, fnumber - 1)->atttypid;
	else
		typoid = (SystemAttributeDefinition(fnumber, true))->atttypid;

	getTypeOutputInfo(typoid, &foutoid, &typisvarlena);

	return OidOutputFunctionCall(foutoid, val);
}

Datum
SPI_getbinval(HeapTuple tuple, TupleDesc tupdesc, int fnumber, bool *isnull)
{
	SPI_result = 0;

	if (fnumber > tupdesc->natts || fnumber == 0 ||
		fnumber <= FirstLowInvalidHeapAttributeNumber)
	{
		SPI_result = SPI_ERROR_NOATTRIBUTE;
		*isnull = true;
		return (Datum) NULL;
	}

	return heap_getattr(tuple, fnumber, tupdesc, isnull);
}

char *
SPI_gettype(TupleDesc tupdesc, int fnumber)
{
	Oid			typoid;
	HeapTuple	typeTuple;
	char	   *result;

	SPI_result = 0;

	if (fnumber > tupdesc->natts || fnumber == 0 ||
		fnumber <= FirstLowInvalidHeapAttributeNumber)
	{
		SPI_result = SPI_ERROR_NOATTRIBUTE;
		return NULL;
	}

	if (fnumber > 0)
		typoid = TupleDescAttr(tupdesc, fnumber - 1)->atttypid;
	else
		typoid = (SystemAttributeDefinition(fnumber, true))->atttypid;

	typeTuple = SearchSysCache1(TYPEOID, ObjectIdGetDatum(typoid));

	if (!HeapTupleIsValid(typeTuple))
	{
		SPI_result = SPI_ERROR_TYPUNKNOWN;
		return NULL;
	}

	result = pstrdup(NameStr(((Form_pg_type) GETSTRUCT(typeTuple))->typname));
	ReleaseSysCache(typeTuple);
	return result;
}

/*
 * Get the data type OID for a column.
 *
 * There's nothing similar for typmod and typcollation.  The rare consumers
 * thereof should inspect the TupleDesc directly.
 */
Oid
SPI_gettypeid(TupleDesc tupdesc, int fnumber)
{
	SPI_result = 0;

	if (fnumber > tupdesc->natts || fnumber == 0 ||
		fnumber <= FirstLowInvalidHeapAttributeNumber)
	{
		SPI_result = SPI_ERROR_NOATTRIBUTE;
		return InvalidOid;
	}

	if (fnumber > 0)
		return TupleDescAttr(tupdesc, fnumber - 1)->atttypid;
	else
		return (SystemAttributeDefinition(fnumber, true))->atttypid;
}

char *
SPI_getrelname(Relation rel)
{
	return pstrdup(RelationGetRelationName(rel));
}

char *
SPI_getnspname(Relation rel)
{
	return get_namespace_name(RelationGetNamespace(rel));
}

void *
SPI_palloc(Size size)
{
	if (_SPI_current == NULL)
		elog(ERROR, "SPI_palloc called while not connected to SPI");

	return MemoryContextAlloc(_SPI_current->savedcxt, size);
}

void *
SPI_repalloc(void *pointer, Size size)
{
	/* No longer need to worry which context chunk was in... */
	return repalloc(pointer, size);
}

void
SPI_pfree(void *pointer)
{
	/* No longer need to worry which context chunk was in... */
	pfree(pointer);
}

Datum
SPI_datumTransfer(Datum value, bool typByVal, int typLen)
{
	MemoryContext oldcxt;
	Datum		result;

	if (_SPI_current == NULL)
		elog(ERROR, "SPI_datumTransfer called while not connected to SPI");

	oldcxt = MemoryContextSwitchTo(_SPI_current->savedcxt);

	result = datumTransfer(value, typByVal, typLen);

	MemoryContextSwitchTo(oldcxt);

	return result;
}

void
SPI_freetuple(HeapTuple tuple)
{
	/* No longer need to worry which context tuple was in... */
	heap_freetuple(tuple);
}

void
SPI_freetuptable(SPITupleTable *tuptable)
{
	bool		found = false;

	/* ignore call if NULL pointer */
	if (tuptable == NULL)
		return;

	/*
	 * Search only the topmost SPI context for a matching tuple table.
	 */
	if (_SPI_current != NULL)
	{
		slist_mutable_iter siter;

		/* find tuptable in active list, then remove it */
		slist_foreach_modify(siter, &_SPI_current->tuptables)
		{
			SPITupleTable *tt;

			tt = slist_container(SPITupleTable, next, siter.cur);
			if (tt == tuptable)
			{
				slist_delete_current(&siter);
				found = true;
				break;
			}
		}
	}

	/*
	 * Refuse the deletion if we didn't find it in the topmost SPI context.
	 * This is primarily a guard against double deletion, but might prevent
	 * other errors as well.  Since the worst consequence of not deleting a
	 * tuptable would be a transient memory leak, this is just a WARNING.
	 */
	if (!found)
	{
		elog(WARNING, "attempt to delete invalid SPITupleTable %p", tuptable);
		return;
	}

	/* for safety, reset global variables that might point at tuptable */
	if (tuptable == _SPI_current->tuptable)
		_SPI_current->tuptable = NULL;
	if (tuptable == SPI_tuptable)
		SPI_tuptable = NULL;

	/* release all memory belonging to tuptable */
	MemoryContextDelete(tuptable->tuptabcxt);
}


/*
 * SPI_cursor_open()
 *
 *	Open a prepared SPI plan as a portal
 */
Portal
SPI_cursor_open(const char *name, SPIPlanPtr plan,
				Datum *Values, const char *Nulls,
				bool read_only)
{
	Portal		portal;
	ParamListInfo paramLI;

	/* build transient ParamListInfo in caller's context */
	paramLI = _SPI_convert_params(plan->nargs, plan->argtypes,
								  Values, Nulls);

	portal = SPI_cursor_open_internal(name, plan, paramLI, read_only);

	/* done with the transient ParamListInfo */
	if (paramLI)
		pfree(paramLI);

	return portal;
}


/*
 * SPI_cursor_open_with_args()
 *
 * Parse and plan a query and open it as a portal.
 */
Portal
SPI_cursor_open_with_args(const char *name,
						  const char *src,
						  int nargs, Oid *argtypes,
						  Datum *Values, const char *Nulls,
						  bool read_only, int cursorOptions)
{
	Portal		result;
	_SPI_plan	plan;
	ParamListInfo paramLI;

	if (src == NULL || nargs < 0)
		elog(ERROR, "SPI_cursor_open_with_args called with invalid arguments");

	if (nargs > 0 && (argtypes == NULL || Values == NULL))
		elog(ERROR, "SPI_cursor_open_with_args called with missing parameters");

	SPI_result = _SPI_begin_call(true);
	if (SPI_result < 0)
		elog(ERROR, "SPI_cursor_open_with_args called while not connected");

	memset(&plan, 0, sizeof(_SPI_plan));
	plan.magic = _SPI_PLAN_MAGIC;
	plan.cursor_options = cursorOptions;
	plan.nargs = nargs;
	plan.argtypes = argtypes;
	plan.parserSetup = NULL;
	plan.parserSetupArg = NULL;

	/* build transient ParamListInfo in executor context */
	paramLI = _SPI_convert_params(nargs, argtypes,
								  Values, Nulls);

	_SPI_prepare_plan(src, &plan);

	/* We needn't copy the plan; SPI_cursor_open_internal will do so */

	result = SPI_cursor_open_internal(name, &plan, paramLI, read_only);

	/* And clean up */
	_SPI_end_call(true);

	return result;
}


/*
 * SPI_cursor_open_with_paramlist()
 *
 *	Same as SPI_cursor_open except that parameters (if any) are passed
 *	as a ParamListInfo, which supports dynamic parameter set determination
 */
Portal
SPI_cursor_open_with_paramlist(const char *name, SPIPlanPtr plan,
							   ParamListInfo params, bool read_only)
{
	return SPI_cursor_open_internal(name, plan, params, read_only);
}


/*
 * SPI_cursor_open_internal()
 *
 *	Common code for SPI_cursor_open variants
 */
static Portal
SPI_cursor_open_internal(const char *name, SPIPlanPtr plan,
						 ParamListInfo paramLI, bool read_only)
{
	CachedPlanSource *plansource;
	CachedPlan *cplan;
	List	   *stmt_list;
	char	   *query_string;
	Snapshot	snapshot;
	MemoryContext oldcontext;
	Portal		portal;
	ErrorContextCallback spierrcontext;

	/*
	 * Check that the plan is something the Portal code will special-case as
	 * returning one tupleset.
	 */
	if (!SPI_is_cursor_plan(plan))
	{
		/* try to give a good error message */
		if (list_length(plan->plancache_list) != 1)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_CURSOR_DEFINITION),
					 errmsg("cannot open multi-query plan as cursor")));
		plansource = (CachedPlanSource *) linitial(plan->plancache_list);
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_CURSOR_DEFINITION),
		/* translator: %s is name of a SQL command, eg INSERT */
				 errmsg("cannot open %s query as cursor",
						plansource->commandTag)));
	}

	Assert(list_length(plan->plancache_list) == 1);
	plansource = (CachedPlanSource *) linitial(plan->plancache_list);

	/* Push the SPI stack */
	if (_SPI_begin_call(true) < 0)
		elog(ERROR, "SPI_cursor_open called while not connected");

	/* Reset SPI result (note we deliberately don't touch lastoid) */
	SPI_processed = 0;
	SPI_tuptable = NULL;
	_SPI_current->processed = 0;
	_SPI_current->tuptable = NULL;

	/* Create the portal */
	if (name == NULL || name[0] == '\0')
	{
		/* Use a random nonconflicting name */
		portal = CreateNewPortal();
	}
	else
	{
		/* In this path, error if portal of same name already exists */
		portal = CreatePortal(name, false, false);
	}

	/* Copy the plan's query string into the portal */
	query_string = MemoryContextStrdup(portal->portalContext,
									   plansource->query_string);

	/*
	 * Setup error traceback support for ereport(), in case GetCachedPlan
	 * throws an error.
	 */
	spierrcontext.callback = _SPI_error_callback;
	spierrcontext.arg = (void *) plansource->query_string;
	spierrcontext.previous = error_context_stack;
	error_context_stack = &spierrcontext;

	/*
	 * Note: for a saved plan, we mustn't have any failure occur between
	 * GetCachedPlan and PortalDefineQuery; that would result in leaking our
	 * plancache refcount.
	 */

	/* Replan if needed, and increment plan refcount for portal */
	cplan = GetCachedPlan(plansource, paramLI, false, _SPI_current->queryEnv);
	stmt_list = cplan->stmt_list;

	if (!plan->saved)
	{
		/*
		 * We don't want the portal to depend on an unsaved CachedPlanSource,
		 * so must copy the plan into the portal's context.  An error here
		 * will result in leaking our refcount on the plan, but it doesn't
		 * matter because the plan is unsaved and hence transient anyway.
		 */
		oldcontext = MemoryContextSwitchTo(portal->portalContext);
		stmt_list = copyObject(stmt_list);
		MemoryContextSwitchTo(oldcontext);
		ReleaseCachedPlan(cplan, false);
		cplan = NULL;			/* portal shouldn't depend on cplan */
	}

	/*
	 * Set up the portal.
	 */
	PortalDefineQuery(portal,
					  NULL,		/* no statement name */
					  query_string,
					  plansource->commandTag,
					  stmt_list,
					  cplan);

	/*
	 * Set up options for portal.  Default SCROLL type is chosen the same way
	 * as PerformCursorOpen does it.
	 */
	portal->cursorOptions = plan->cursor_options;
	if (!(portal->cursorOptions & (CURSOR_OPT_SCROLL | CURSOR_OPT_NO_SCROLL)))
	{
		if (list_length(stmt_list) == 1 &&
			linitial_node(PlannedStmt, stmt_list)->commandType != CMD_UTILITY &&
			linitial_node(PlannedStmt, stmt_list)->rowMarks == NIL &&
			ExecSupportsBackwardScan(linitial_node(PlannedStmt, stmt_list)->planTree))
			portal->cursorOptions |= CURSOR_OPT_SCROLL;
		else
			portal->cursorOptions |= CURSOR_OPT_NO_SCROLL;
	}

	/*
	 * Disallow SCROLL with SELECT FOR UPDATE.  This is not redundant with the
	 * check in transformDeclareCursorStmt because the cursor options might
	 * not have come through there.
	 */
	if (portal->cursorOptions & CURSOR_OPT_SCROLL)
	{
		if (list_length(stmt_list) == 1 &&
			linitial_node(PlannedStmt, stmt_list)->commandType != CMD_UTILITY &&
			linitial_node(PlannedStmt, stmt_list)->rowMarks != NIL)
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 errmsg("DECLARE SCROLL CURSOR ... FOR UPDATE/SHARE is not supported"),
					 errdetail("Scrollable cursors must be READ ONLY.")));
	}

	/* Make current query environment available to portal at execution time. */
	portal->queryEnv = _SPI_current->queryEnv;

	/*
	 * If told to be read-only, or in parallel mode, verify that this query is
	 * in fact read-only.  This can't be done earlier because we need to look
	 * at the finished, planned queries.  (In particular, we don't want to do
	 * it between GetCachedPlan and PortalDefineQuery, because throwing an
	 * error between those steps would result in leaking our plancache
	 * refcount.)
	 */
	if (read_only || IsInParallelMode())
	{
		ListCell   *lc;

		foreach(lc, stmt_list)
		{
			PlannedStmt *pstmt = lfirst_node(PlannedStmt, lc);

			if (!CommandIsReadOnly(pstmt))
			{
				if (read_only)
					ereport(ERROR,
							(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					/* translator: %s is a SQL statement name */
							 errmsg("%s is not allowed in a non-volatile function",
									CreateCommandTag((Node *) pstmt))));
				else
					PreventCommandIfParallelMode(CreateCommandTag((Node *) pstmt));
			}
		}
	}

	/* Set up the snapshot to use. */
	if (read_only)
		snapshot = GetActiveSnapshot();
	else
	{
		CommandCounterIncrement();
		snapshot = GetTransactionSnapshot();
	}

	/*
	 * If the plan has parameters, copy them into the portal.  Note that this
	 * must be done after revalidating the plan, because in dynamic parameter
	 * cases the set of parameters could have changed during re-parsing.
	 */
	if (paramLI)
	{
		oldcontext = MemoryContextSwitchTo(portal->portalContext);
		paramLI = copyParamList(paramLI);
		MemoryContextSwitchTo(oldcontext);
	}

	/*
	 * Start portal execution.
	 */
	PortalStart(portal, paramLI, 0, snapshot);

	Assert(portal->strategy != PORTAL_MULTI_QUERY);

	/* Pop the error context stack */
	error_context_stack = spierrcontext.previous;

	/* Pop the SPI stack */
	_SPI_end_call(true);

	/* Return the created portal */
	return portal;
}


/*
 * SPI_cursor_find()
 *
 *	Find the portal of an existing open cursor
 */
Portal
SPI_cursor_find(const char *name)
{
	return GetPortalByName(name);
}


/*
 * SPI_cursor_fetch()
 *
 *	Fetch rows in a cursor
 */
void
SPI_cursor_fetch(Portal portal, bool forward, long count)
{
	_SPI_cursor_operation(portal,
						  forward ? FETCH_FORWARD : FETCH_BACKWARD, count,
						  CreateDestReceiver(DestSPI));
	/* we know that the DestSPI receiver doesn't need a destroy call */
}


/*
 * SPI_cursor_move()
 *
 *	Move in a cursor
 */
void
SPI_cursor_move(Portal portal, bool forward, long count)
{
	_SPI_cursor_operation(portal,
						  forward ? FETCH_FORWARD : FETCH_BACKWARD, count,
						  None_Receiver);
}


/*
 * SPI_scroll_cursor_fetch()
 *
 *	Fetch rows in a scrollable cursor
 */
void
SPI_scroll_cursor_fetch(Portal portal, FetchDirection direction, long count)
{
	_SPI_cursor_operation(portal,
						  direction, count,
						  CreateDestReceiver(DestSPI));
	/* we know that the DestSPI receiver doesn't need a destroy call */
}


/*
 * SPI_scroll_cursor_move()
 *
 *	Move in a scrollable cursor
 */
void
SPI_scroll_cursor_move(Portal portal, FetchDirection direction, long count)
{
	_SPI_cursor_operation(portal, direction, count, None_Receiver);
}


/*
 * SPI_cursor_close()
 *
 *	Close a cursor
 */
void
SPI_cursor_close(Portal portal)
{
	if (!PortalIsValid(portal))
		elog(ERROR, "invalid portal in SPI cursor operation");

	PortalDrop(portal, false);
}

/*
 * Returns the Oid representing the type id for argument at argIndex. First
 * parameter is at index zero.
 */
Oid
SPI_getargtypeid(SPIPlanPtr plan, int argIndex)
{
	if (plan == NULL || plan->magic != _SPI_PLAN_MAGIC ||
		argIndex < 0 || argIndex >= plan->nargs)
	{
		SPI_result = SPI_ERROR_ARGUMENT;
		return InvalidOid;
	}
	return plan->argtypes[argIndex];
}

/*
 * Returns the number of arguments for the prepared plan.
 */
int
SPI_getargcount(SPIPlanPtr plan)
{
	if (plan == NULL || plan->magic != _SPI_PLAN_MAGIC)
	{
		SPI_result = SPI_ERROR_ARGUMENT;
		return -1;
	}
	return plan->nargs;
}

/*
 * Returns true if the plan contains exactly one command
 * and that command returns tuples to the caller (eg, SELECT or
 * INSERT ... RETURNING, but not SELECT ... INTO). In essence,
 * the result indicates if the command can be used with SPI_cursor_open
 *
 * Parameters
 *	  plan: A plan previously prepared using SPI_prepare
 */
bool
SPI_is_cursor_plan(SPIPlanPtr plan)
{
	CachedPlanSource *plansource;

	if (plan == NULL || plan->magic != _SPI_PLAN_MAGIC)
	{
		SPI_result = SPI_ERROR_ARGUMENT;
		return false;
	}

	if (list_length(plan->plancache_list) != 1)
	{
		SPI_result = 0;
		return false;			/* not exactly 1 pre-rewrite command */
	}
	plansource = (CachedPlanSource *) linitial(plan->plancache_list);

	/*
	 * We used to force revalidation of the cached plan here, but that seems
	 * unnecessary: invalidation could mean a change in the rowtype of the
	 * tuples returned by a plan, but not whether it returns tuples at all.
	 */
	SPI_result = 0;

	/* Does it return tuples? */
	if (plansource->resultDesc)
		return true;

	return false;
}

/*
 * SPI_plan_is_valid --- test whether a SPI plan is currently valid
 * (that is, not marked as being in need of revalidation).
 *
 * See notes for CachedPlanIsValid before using this.
 */
bool
SPI_plan_is_valid(SPIPlanPtr plan)
{
	ListCell   *lc;

	Assert(plan->magic == _SPI_PLAN_MAGIC);

	foreach(lc, plan->plancache_list)
	{
		CachedPlanSource *plansource = (CachedPlanSource *) lfirst(lc);

		if (!CachedPlanIsValid(plansource))
			return false;
	}
	return true;
}

/*
 * SPI_result_code_string --- convert any SPI return code to a string
 *
 * This is often useful in error messages.  Most callers will probably
 * only pass negative (error-case) codes, but for generality we recognize
 * the success codes too.
 */
const char *
SPI_result_code_string(int code)
{
	static char buf[64];

	switch (code)
	{
		case SPI_ERROR_CONNECT:
			return "SPI_ERROR_CONNECT";
		case SPI_ERROR_COPY:
			return "SPI_ERROR_COPY";
		case SPI_ERROR_OPUNKNOWN:
			return "SPI_ERROR_OPUNKNOWN";
		case SPI_ERROR_UNCONNECTED:
			return "SPI_ERROR_UNCONNECTED";
		case SPI_ERROR_ARGUMENT:
			return "SPI_ERROR_ARGUMENT";
		case SPI_ERROR_PARAM:
			return "SPI_ERROR_PARAM";
		case SPI_ERROR_TRANSACTION:
			return "SPI_ERROR_TRANSACTION";
		case SPI_ERROR_NOATTRIBUTE:
			return "SPI_ERROR_NOATTRIBUTE";
		case SPI_ERROR_NOOUTFUNC:
			return "SPI_ERROR_NOOUTFUNC";
		case SPI_ERROR_TYPUNKNOWN:
			return "SPI_ERROR_TYPUNKNOWN";
		case SPI_ERROR_REL_DUPLICATE:
			return "SPI_ERROR_REL_DUPLICATE";
		case SPI_ERROR_REL_NOT_FOUND:
			return "SPI_ERROR_REL_NOT_FOUND";
		case SPI_OK_CONNECT:
			return "SPI_OK_CONNECT";
		case SPI_OK_FINISH:
			return "SPI_OK_FINISH";
		case SPI_OK_FETCH:
			return "SPI_OK_FETCH";
		case SPI_OK_UTILITY:
			return "SPI_OK_UTILITY";
		case SPI_OK_SELECT:
			return "SPI_OK_SELECT";
		case SPI_OK_SELINTO:
			return "SPI_OK_SELINTO";
		case SPI_OK_INSERT:
			return "SPI_OK_INSERT";
		case SPI_OK_DELETE:
			return "SPI_OK_DELETE";
		case SPI_OK_UPDATE:
			return "SPI_OK_UPDATE";
		case SPI_OK_CURSOR:
			return "SPI_OK_CURSOR";
		case SPI_OK_INSERT_RETURNING:
			return "SPI_OK_INSERT_RETURNING";
		case SPI_OK_DELETE_RETURNING:
			return "SPI_OK_DELETE_RETURNING";
		case SPI_OK_UPDATE_RETURNING:
			return "SPI_OK_UPDATE_RETURNING";
		case SPI_OK_REWRITTEN:
			return "SPI_OK_REWRITTEN";
		case SPI_OK_REL_REGISTER:
			return "SPI_OK_REL_REGISTER";
		case SPI_OK_REL_UNREGISTER:
			return "SPI_OK_REL_UNREGISTER";
	}
	/* Unrecognized code ... return something useful ... */
	sprintf(buf, "Unrecognized SPI code %d", code);
	return buf;
}

/*
 * SPI_plan_get_plan_sources --- get a SPI plan's underlying list of
 * CachedPlanSources.
 *
 * This is exported so that PL/pgSQL can use it (this beats letting PL/pgSQL
 * look directly into the SPIPlan for itself).  It's not documented in
 * spi.sgml because we'd just as soon not have too many places using this.
 */
List *
SPI_plan_get_plan_sources(SPIPlanPtr plan)
{
	Assert(plan->magic == _SPI_PLAN_MAGIC);
	return plan->plancache_list;
}

/*
 * SPI_plan_get_cached_plan --- get a SPI plan's generic CachedPlan,
 * if the SPI plan contains exactly one CachedPlanSource.  If not,
 * return NULL.  Caller is responsible for doing ReleaseCachedPlan().
 *
 * This is exported so that PL/pgSQL can use it (this beats letting PL/pgSQL
 * look directly into the SPIPlan for itself).  It's not documented in
 * spi.sgml because we'd just as soon not have too many places using this.
 */
CachedPlan *
SPI_plan_get_cached_plan(SPIPlanPtr plan)
{
	CachedPlanSource *plansource;
	CachedPlan *cplan;
	ErrorContextCallback spierrcontext;

	Assert(plan->magic == _SPI_PLAN_MAGIC);

	/* Can't support one-shot plans here */
	if (plan->oneshot)
		return NULL;

	/* Must have exactly one CachedPlanSource */
	if (list_length(plan->plancache_list) != 1)
		return NULL;
	plansource = (CachedPlanSource *) linitial(plan->plancache_list);

	/* Setup error traceback support for ereport() */
	spierrcontext.callback = _SPI_error_callback;
	spierrcontext.arg = (void *) plansource->query_string;
	spierrcontext.previous = error_context_stack;
	error_context_stack = &spierrcontext;

	/* Get the generic plan for the query */
	cplan = GetCachedPlan(plansource, NULL, plan->saved,
						  _SPI_current->queryEnv);
	Assert(cplan == plansource->gplan);

	/* Pop the error context stack */
	error_context_stack = spierrcontext.previous;

	return cplan;
}


/* =================== private functions =================== */

/*
 * spi_dest_startup
 *		Initialize to receive tuples from Executor into SPITupleTable
 *		of current SPI procedure
 */
void
spi_dest_startup(DestReceiver *self, int operation, TupleDesc typeinfo)
{
	SPITupleTable *tuptable;
	MemoryContext oldcxt;
	MemoryContext tuptabcxt;

	if (_SPI_current == NULL)
		elog(ERROR, "spi_dest_startup called while not connected to SPI");

	if (_SPI_current->tuptable != NULL)
		elog(ERROR, "improper call to spi_dest_startup");

	/* We create the tuple table context as a child of procCxt */

	oldcxt = _SPI_procmem();	/* switch to procedure memory context */

	tuptabcxt = AllocSetContextCreate(GetCurrentMemoryContext(),
									  "SPI TupTable",
									  ALLOCSET_DEFAULT_SIZES);
	MemoryContextSwitchTo(tuptabcxt);

	_SPI_current->tuptable = tuptable = (SPITupleTable *)
		palloc0(sizeof(SPITupleTable));
	tuptable->tuptabcxt = tuptabcxt;
	tuptable->subid = GetCurrentSubTransactionId();

	/*
	 * The tuptable is now valid enough to be freed by AtEOSubXact_SPI, so put
	 * it onto the SPI context's tuptables list.  This will ensure it's not
	 * leaked even in the unlikely event the following few lines fail.
	 */
	slist_push_head(&_SPI_current->tuptables, &tuptable->next);

	/* set up initial allocations */
	tuptable->alloced = tuptable->free = 128;
	tuptable->vals = (HeapTuple *) palloc(tuptable->alloced * sizeof(HeapTuple));
	tuptable->tupdesc = CreateTupleDescCopy(typeinfo);

	MemoryContextSwitchTo(oldcxt);
}

/*
 * spi_printtup
 *		store tuple retrieved by Executor into SPITupleTable
 *		of current SPI procedure
 */
bool
spi_printtup(TupleTableSlot *slot, DestReceiver *self)
{
	SPITupleTable *tuptable;
	MemoryContext oldcxt;

	if (_SPI_current == NULL)
		elog(ERROR, "spi_printtup called while not connected to SPI");

	tuptable = _SPI_current->tuptable;
	if (tuptable == NULL)
		elog(ERROR, "improper call to spi_printtup");

	oldcxt = MemoryContextSwitchTo(tuptable->tuptabcxt);

	if (tuptable->free == 0)
	{
		/* Double the size of the pointer array */
		tuptable->free = tuptable->alloced;
		tuptable->alloced += tuptable->free;
		tuptable->vals = (HeapTuple *) repalloc_huge(tuptable->vals,
													 tuptable->alloced * sizeof(HeapTuple));
	}

	tuptable->vals[tuptable->alloced - tuptable->free] =
		ExecCopySlotTuple(slot);
	(tuptable->free)--;

	MemoryContextSwitchTo(oldcxt);

	return true;
}

/*
 * Static functions
 */

/*
 * Parse and analyze a querystring.
 *
 * At entry, plan->argtypes and plan->nargs (or alternatively plan->parserSetup
 * and plan->parserSetupArg) must be valid, as must plan->cursor_options.
 *
 * Results are stored into *plan (specifically, plan->plancache_list).
 * Note that the result data is all in GetCurrentMemoryContext() or child contexts
 * thereof; in practice this means it is in the SPI executor context, and
 * what we are creating is a "temporary" SPIPlan.  Cruft generated during
 * parsing is also left in GetCurrentMemoryContext().
 */
static void
_SPI_prepare_plan(const char *src, SPIPlanPtr plan)
{
	List	   *raw_parsetree_list;
	List	   *plancache_list;
	ListCell   *list_item;
	ErrorContextCallback spierrcontext;

	/*
	 * Setup error traceback support for ereport()
	 */
	spierrcontext.callback = _SPI_error_callback;
	spierrcontext.arg = (void *) src;
	spierrcontext.previous = error_context_stack;
	error_context_stack = &spierrcontext;

	/*
	 * Parse the request string into a list of raw parse trees.
	 */
	raw_parsetree_list = pg_parse_query(src);

	/*
	 * Do parse analysis and rule rewrite for each raw parsetree, storing the
	 * results into unsaved plancache entries.
	 */
	plancache_list = NIL;

	foreach(list_item, raw_parsetree_list)
	{
		RawStmt    *parsetree = lfirst_node(RawStmt, list_item);
		List	   *stmt_list;
		CachedPlanSource *plansource;

		/*
		 * Create the CachedPlanSource before we do parse analysis, since it
		 * needs to see the unmodified raw parse tree.
		 */
		plansource = CreateCachedPlan(parsetree,
									  src,
									  CreateCommandTag(parsetree->stmt));

		/*
		 * Parameter datatypes are driven by parserSetup hook if provided,
		 * otherwise we use the fixed parameter list.
		 */
		if (plan->parserSetup != NULL)
		{
			Assert(plan->nargs == 0);
			stmt_list = pg_analyze_and_rewrite_params(parsetree,
													  src,
													  plan->parserSetup,
													  plan->parserSetupArg,
													  _SPI_current->queryEnv);
		}
		else
		{
			stmt_list = pg_analyze_and_rewrite(parsetree,
											   src,
											   plan->argtypes,
											   plan->nargs,
											   _SPI_current->queryEnv);
		}

		/* Finish filling in the CachedPlanSource */
		CompleteCachedPlan(plansource,
						   stmt_list,
						   NULL,
						   plan->argtypes,
						   plan->nargs,
						   plan->parserSetup,
						   plan->parserSetupArg,
						   plan->cursor_options,
						   false);	/* not fixed result */

		plancache_list = lappend(plancache_list, plansource);
	}

	plan->plancache_list = plancache_list;
	plan->oneshot = false;

	/*
	 * Pop the error context stack
	 */
	error_context_stack = spierrcontext.previous;
}

/*
 * Parse, but don't analyze, a querystring.
 *
 * This is a stripped-down version of _SPI_prepare_plan that only does the
 * initial raw parsing.  It creates "one shot" CachedPlanSources
 * that still require parse analysis before execution is possible.
 *
 * The advantage of using the "one shot" form of CachedPlanSource is that
 * we eliminate data copying and invalidation overhead.  Postponing parse
 * analysis also prevents issues if some of the raw parsetrees are DDL
 * commands that affect validity of later parsetrees.  Both of these
 * attributes are good things for SPI_execute() and similar cases.
 *
 * Results are stored into *plan (specifically, plan->plancache_list).
 * Note that the result data is all in GetCurrentMemoryContext() or child contexts
 * thereof; in practice this means it is in the SPI executor context, and
 * what we are creating is a "temporary" SPIPlan.  Cruft generated during
 * parsing is also left in GetCurrentMemoryContext().
 */
static void
_SPI_prepare_oneshot_plan(const char *src, SPIPlanPtr plan)
{
	List	   *raw_parsetree_list;
	List	   *plancache_list;
	ListCell   *list_item;
	ErrorContextCallback spierrcontext;

	/*
	 * Setup error traceback support for ereport()
	 */
	spierrcontext.callback = _SPI_error_callback;
	spierrcontext.arg = (void *) src;
	spierrcontext.previous = error_context_stack;
	error_context_stack = &spierrcontext;

	/*
	 * Parse the request string into a list of raw parse trees.
	 */
	raw_parsetree_list = pg_parse_query(src);

	/*
	 * Construct plancache entries, but don't do parse analysis yet.
	 */
	plancache_list = NIL;

	foreach(list_item, raw_parsetree_list)
	{
		RawStmt    *parsetree = lfirst_node(RawStmt, list_item);
		CachedPlanSource *plansource;

		plansource = CreateOneShotCachedPlan(parsetree,
											 src,
											 CreateCommandTag(parsetree->stmt));

		plancache_list = lappend(plancache_list, plansource);
	}

	plan->plancache_list = plancache_list;
	plan->oneshot = true;

	/*
	 * Pop the error context stack
	 */
	error_context_stack = spierrcontext.previous;
}

/*
 * Execute the given plan with the given parameter values
 *
 * snapshot: query snapshot to use, or InvalidSnapshot for the normal
 *		behavior of taking a new snapshot for each query.
 * crosscheck_snapshot: for RI use, all others pass InvalidSnapshot
 * read_only: true for read-only execution (no CommandCounterIncrement)
 * fire_triggers: true to fire AFTER triggers at end of query (normal case);
 *		false means any AFTER triggers are postponed to end of outer query
 * tcount: execution tuple-count limit, or 0 for none
 */
static int
_SPI_execute_plan(SPIPlanPtr plan, ParamListInfo paramLI,
				  Snapshot snapshot, Snapshot crosscheck_snapshot,
				  bool read_only, bool fire_triggers, uint64 tcount,
				  bool yb_reuse_existing_snapshot_in_read_committed)
{
	int			my_res = 0;
	uint64		my_processed = 0;
	Oid			my_lastoid = InvalidOid;
	SPITupleTable *my_tuptable = NULL;
	int			res = 0;
	bool		pushed_active_snap = false;
	ErrorContextCallback spierrcontext;
	CachedPlan *cplan = NULL;
	ListCell   *lc1;

	/*
	 * Setup error traceback support for ereport()
	 */
	spierrcontext.callback = _SPI_error_callback;
	spierrcontext.arg = NULL;	/* we'll fill this below */
	spierrcontext.previous = error_context_stack;
	error_context_stack = &spierrcontext;

	/*
	 * We support four distinct snapshot management behaviors:
	 *
	 * snapshot != InvalidSnapshot, read_only = true: use exactly the given
	 * snapshot.
	 *
	 * snapshot != InvalidSnapshot, read_only = false: use the given snapshot,
	 * modified by advancing its command ID before each querytree.
	 *
	 * snapshot == InvalidSnapshot, read_only = true: use the entry-time
	 * ActiveSnapshot, if any (if there isn't one, we run with no snapshot).
	 *
	 * snapshot == InvalidSnapshot, read_only = false: take a full new
	 * snapshot for each user command, and advance its command ID before each
	 * querytree within the command.
	 *
	 * In the first two cases, we can just push the snap onto the stack once
	 * for the whole plan list.
	 *
	 * But if the plan has no_snapshots set to true, then don't manage
	 * snapshots at all.  The caller should then take care of that.
	 */
	if (snapshot != InvalidSnapshot && !plan->no_snapshots)
	{
		if (read_only)
		{
			PushActiveSnapshot(snapshot);
			pushed_active_snap = true;
		}
		else
		{
			/* Make sure we have a private copy of the snapshot to modify */
			PushCopiedSnapshot(snapshot);
			pushed_active_snap = true;
		}
	}

	foreach(lc1, plan->plancache_list)
	{
		CachedPlanSource *plansource = (CachedPlanSource *) lfirst(lc1);
		List	   *stmt_list;
		ListCell   *lc2;

		/*
		 * If the planner found a pg relation in this plan, set the appropriate
		 * flag for the execution txn.
		 */
		if (plansource->usesPostgresRel) {
			YbSetTxnWithPgOps(YB_TXN_USES_TEMPORARY_RELATIONS);
		}

		spierrcontext.arg = (void *) plansource->query_string;

		/*
		 * If this is a one-shot plan, we still need to do parse analysis.
		 */
		if (plan->oneshot)
		{
			RawStmt    *parsetree = plansource->raw_parse_tree;
			const char *src = plansource->query_string;
			List	   *stmt_list;

			/*
			 * Parameter datatypes are driven by parserSetup hook if provided,
			 * otherwise we use the fixed parameter list.
			 */
			if (parsetree == NULL)
				stmt_list = NIL;
			else if (plan->parserSetup != NULL)
			{
				Assert(plan->nargs == 0);
				stmt_list = pg_analyze_and_rewrite_params(parsetree,
														  src,
														  plan->parserSetup,
														  plan->parserSetupArg,
														  _SPI_current->queryEnv);
			}
			else
			{
				stmt_list = pg_analyze_and_rewrite(parsetree,
												   src,
												   plan->argtypes,
												   plan->nargs,
												   _SPI_current->queryEnv);
			}

			/* Finish filling in the CachedPlanSource */
			CompleteCachedPlan(plansource,
							   stmt_list,
							   NULL,
							   plan->argtypes,
							   plan->nargs,
							   plan->parserSetup,
							   plan->parserSetupArg,
							   plan->cursor_options,
							   false);	/* not fixed result */
		}

		/*
		 * Replan if needed, and increment plan refcount.  If it's a saved
		 * plan, the refcount must be backed by the CurrentResourceOwner.
		 */
		cplan = GetCachedPlan(plansource, paramLI, plan->saved, _SPI_current->queryEnv);
		stmt_list = cplan->stmt_list;

		/*
		 * In the default non-read-only case, get a new snapshot, replacing
		 * any that we pushed in a previous cycle.
		 * YB: When batching of writes across queries is requested in Read
		 * Committed isolation, skip creating a new snapshot (and
		 * consequently a read point) as this would cause previously
		 * buffered writes to be flushed. As a result, all the statements in
		 * the batch share the same snapshot. In case of a serialization
		 * error, the entire top level statement will be retried and not just
		 * individual statements in the batch. So, skipping the snapshot does
		 * not alter the retry logic.
		 * TODO(kramanathan): Use this as a workaround until we can explicitly
		 * specify that multiple statements share a read point in RC mode if
		 * they do not perform any reads.
		 */
		if (snapshot == InvalidSnapshot && !read_only && !plan->no_snapshots &&
			!yb_reuse_existing_snapshot_in_read_committed)
		{
			if (pushed_active_snap)
				PopActiveSnapshot();
			PushActiveSnapshot(GetTransactionSnapshot());
			pushed_active_snap = true;
		}

		foreach(lc2, stmt_list)
		{
			PlannedStmt *stmt = lfirst_node(PlannedStmt, lc2);
			bool		canSetTag = stmt->canSetTag;
			DestReceiver *dest;

			_SPI_current->processed = 0;
			_SPI_current->lastoid = InvalidOid;
			_SPI_current->tuptable = NULL;

			if (stmt->utilityStmt)
			{
				if (IsA(stmt->utilityStmt, CopyStmt))
				{
					CopyStmt   *cstmt = (CopyStmt *) stmt->utilityStmt;

					if (cstmt->filename == NULL)
					{
						my_res = SPI_ERROR_COPY;
						goto fail;
					}
				}
				else if (IsA(stmt->utilityStmt, TransactionStmt))
				{
					my_res = SPI_ERROR_TRANSACTION;
					goto fail;
				}
			}

			if (read_only && !CommandIsReadOnly(stmt))
				ereport(ERROR,
						(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				/* translator: %s is a SQL statement name */
						 errmsg("%s is not allowed in a non-volatile function",
								CreateCommandTag((Node *) stmt))));

			if (IsInParallelMode() && !CommandIsReadOnly(stmt))
				PreventCommandIfParallelMode(CreateCommandTag((Node *) stmt));

			/*
			 * If not read-only mode, advance the command counter before each
			 * command and update the snapshot. (But skip it if the snapshot
			 * isn't under our control.)
			 */
			if (!read_only && pushed_active_snap)
			{
				CommandCounterIncrement();
				UpdateActiveSnapshotCommandId();
			}

			dest = CreateDestReceiver(canSetTag ? DestSPI : DestNone);

			if (stmt->utilityStmt == NULL)
			{
				QueryDesc  *qdesc;
				Snapshot	snap;

				if (ActiveSnapshotSet())
					snap = GetActiveSnapshot();
				else
					snap = InvalidSnapshot;

				qdesc = CreateQueryDesc(stmt,
										plansource->query_string,
										snap, crosscheck_snapshot,
										dest,
										paramLI, _SPI_current->queryEnv,
										0);
				res = _SPI_pquery(qdesc, fire_triggers,
								  canSetTag ? tcount : 0);
				FreeQueryDesc(qdesc);
			}
			else
			{
				char		completionTag[COMPLETION_TAG_BUFSIZE];
				ProcessUtilityContext context;

				/*
				 * If the SPI context is atomic, or we are asked to manage
				 * snapshots, then we are in an atomic execution context.
				 * Conversely, to propagate a nonatomic execution context, the
				 * caller must be in a nonatomic SPI context and manage
				 * snapshots itself.
				 */
				if (_SPI_current->atomic || !plan->no_snapshots)
					context = PROCESS_UTILITY_QUERY;
				else
					context = PROCESS_UTILITY_QUERY_NONATOMIC;

				ProcessUtility(stmt,
							   plansource->query_string,
							   context,
							   paramLI,
							   _SPI_current->queryEnv,
							   dest,
							   completionTag);

				/* Update "processed" if stmt returned tuples */
				if (_SPI_current->tuptable)
					_SPI_current->processed = _SPI_current->tuptable->alloced -
						_SPI_current->tuptable->free;

				res = SPI_OK_UTILITY;

				/*
				 * Some utility statements return a row count, even though the
				 * tuples are not returned to the caller.
				 */
				if (IsA(stmt->utilityStmt, CreateTableAsStmt))
				{
					CreateTableAsStmt *ctastmt = (CreateTableAsStmt *) stmt->utilityStmt;

					if (strncmp(completionTag, "SELECT ", 7) == 0)
						_SPI_current->processed =
							pg_strtouint64(completionTag + 7, NULL, 10);
					else
					{
						/*
						 * Must be an IF NOT EXISTS that did nothing, or a
						 * CREATE ... WITH NO DATA.
						 */
						Assert(ctastmt->if_not_exists ||
							   ctastmt->into->skipData);
						_SPI_current->processed = 0;
					}

					/*
					 * For historical reasons, if CREATE TABLE AS was spelled
					 * as SELECT INTO, return a special return code.
					 */
					if (ctastmt->is_select_into)
						res = SPI_OK_SELINTO;
				}
				else if (IsA(stmt->utilityStmt, CopyStmt))
				{
					Assert(strncmp(completionTag, "COPY ", 5) == 0);
					_SPI_current->processed = pg_strtouint64(completionTag + 5,
															 NULL, 10);
				}
			}

			/*
			 * The last canSetTag query sets the status values returned to the
			 * caller.  Be careful to free any tuptables not returned, to
			 * avoid intra-transaction memory leak.
			 */
			if (canSetTag)
			{
				my_processed = _SPI_current->processed;
				my_lastoid = _SPI_current->lastoid;
				SPI_freetuptable(my_tuptable);
				my_tuptable = _SPI_current->tuptable;
				my_res = res;
			}
			else
			{
				SPI_freetuptable(_SPI_current->tuptable);
				_SPI_current->tuptable = NULL;
			}
			/* we know that the receiver doesn't need a destroy call */
			if (res < 0)
			{
				my_res = res;
				goto fail;
			}
		}

		/* Done with this plan, so release refcount */
		ReleaseCachedPlan(cplan, plan->saved);
		cplan = NULL;

		/*
		 * If not read-only mode, advance the command counter after the last
		 * command.  This ensures that its effects are visible, in case it was
		 * DDL that would affect the next CachedPlanSource.
		 */
		if (!read_only)
			CommandCounterIncrement();
	}

fail:

	/* Pop the snapshot off the stack if we pushed one */
	if (pushed_active_snap)
		PopActiveSnapshot();

	/* We no longer need the cached plan refcount, if any */
	if (cplan)
		ReleaseCachedPlan(cplan, plan->saved);

	/*
	 * Pop the error context stack
	 */
	error_context_stack = spierrcontext.previous;

	/* Save results for caller */
	SPI_processed = my_processed;
	SPI_lastoid = my_lastoid;
	SPI_tuptable = my_tuptable;

	/* tuptable now is caller's responsibility, not SPI's */
	_SPI_current->tuptable = NULL;

	/*
	 * If none of the queries had canSetTag, return SPI_OK_REWRITTEN. Prior to
	 * 8.4, we used return the last query's result code, but not its auxiliary
	 * results, but that's confusing.
	 */
	if (my_res == 0)
		my_res = SPI_OK_REWRITTEN;

	return my_res;
}

/*
 * Convert arrays of query parameters to form wanted by planner and executor
 */
static ParamListInfo
_SPI_convert_params(int nargs, Oid *argtypes,
					Datum *Values, const char *Nulls)
{
	ParamListInfo paramLI;

	if (nargs > 0)
	{
		int			i;

		paramLI = (ParamListInfo) palloc(offsetof(ParamListInfoData, params) +
										 nargs * sizeof(ParamExternData));
		/* we have static list of params, so no hooks needed */
		paramLI->paramFetch = NULL;
		paramLI->paramFetchArg = NULL;
		paramLI->paramCompile = NULL;
		paramLI->paramCompileArg = NULL;
		paramLI->parserSetup = NULL;
		paramLI->parserSetupArg = NULL;
		paramLI->numParams = nargs;

		for (i = 0; i < nargs; i++)
		{
			ParamExternData *prm = &paramLI->params[i];

			prm->value = Values[i];
			prm->isnull = (Nulls && Nulls[i] == 'n');
			prm->pflags = PARAM_FLAG_CONST;
			prm->ptype = argtypes[i];
		}
	}
	else
		paramLI = NULL;
	return paramLI;
}

static int
_SPI_pquery(QueryDesc *queryDesc, bool fire_triggers, uint64 tcount)
{
	int			operation = queryDesc->operation;
	int			eflags;
	int			res;

	switch (operation)
	{
		case CMD_SELECT:
			if (queryDesc->dest->mydest != DestSPI)
			{
				/* Don't return SPI_OK_SELECT if we're discarding result */
				res = SPI_OK_UTILITY;
			}
			else
				res = SPI_OK_SELECT;
			break;
		case CMD_INSERT:
			if (queryDesc->plannedstmt->hasReturning)
				res = SPI_OK_INSERT_RETURNING;
			else
				res = SPI_OK_INSERT;
			break;
		case CMD_DELETE:
			if (queryDesc->plannedstmt->hasReturning)
				res = SPI_OK_DELETE_RETURNING;
			else
				res = SPI_OK_DELETE;
			break;
		case CMD_UPDATE:
			if (queryDesc->plannedstmt->hasReturning)
				res = SPI_OK_UPDATE_RETURNING;
			else
				res = SPI_OK_UPDATE;
			break;
		default:
			return SPI_ERROR_OPUNKNOWN;
	}

#ifdef SPI_EXECUTOR_STATS
	if (ShowExecutorStats)
		ResetUsage();
#endif

	/* Select execution options */
	if (fire_triggers)
		eflags = 0;				/* default run-to-completion flags */
	else
		eflags = EXEC_FLAG_SKIP_TRIGGERS;

	ExecutorStart(queryDesc, eflags);

	ExecutorRun(queryDesc, ForwardScanDirection, tcount, true);

	_SPI_current->processed = queryDesc->estate->es_processed;
	_SPI_current->lastoid = queryDesc->estate->es_lastoid;

	if ((res == SPI_OK_SELECT || queryDesc->plannedstmt->hasReturning) &&
		queryDesc->dest->mydest == DestSPI)
	{
		if (_SPI_checktuples())
			elog(ERROR, "consistency check on SPI tuple count failed");
	}

	ExecutorFinish(queryDesc);
	ExecutorEnd(queryDesc);
	/* FreeQueryDesc is done by the caller */

#ifdef SPI_EXECUTOR_STATS
	if (ShowExecutorStats)
		ShowUsage("SPI EXECUTOR STATS");
#endif

	return res;
}

/*
 * _SPI_error_callback
 *
 * Add context information when a query invoked via SPI fails
 */
static void
_SPI_error_callback(void *arg)
{
	const char *query = (const char *) arg;
	int			syntaxerrposition;

	if (query == NULL)			/* in case arg wasn't set yet */
		return;

	/*
	 * If there is a syntax error position, convert to internal syntax error;
	 * otherwise treat the query as an item of context stack
	 */
	syntaxerrposition = geterrposition();
	if (syntaxerrposition > 0)
	{
		errposition(0);
		internalerrposition(syntaxerrposition);
		internalerrquery(query);
	}
	else
		errcontext("SQL statement \"%s\"", query);
}

/*
 * _SPI_cursor_operation()
 *
 *	Do a FETCH or MOVE in a cursor
 */
static void
_SPI_cursor_operation(Portal portal, FetchDirection direction, long count,
					  DestReceiver *dest)
{
	uint64		nfetched;

	/* Check that the portal is valid */
	if (!PortalIsValid(portal))
		elog(ERROR, "invalid portal in SPI cursor operation");

	/* Push the SPI stack */
	if (_SPI_begin_call(true) < 0)
		elog(ERROR, "SPI cursor operation called while not connected");

	/* Reset the SPI result (note we deliberately don't touch lastoid) */
	SPI_processed = 0;
	SPI_tuptable = NULL;
	_SPI_current->processed = 0;
	_SPI_current->tuptable = NULL;

	/* Run the cursor */
	nfetched = PortalRunFetch(portal,
							  direction,
							  count,
							  dest);

	/*
	 * Think not to combine this store with the preceding function call. If
	 * the portal contains calls to functions that use SPI, then SPI_stack is
	 * likely to move around while the portal runs.  When control returns,
	 * _SPI_current will point to the correct stack entry... but the pointer
	 * may be different than it was beforehand. So we must be sure to re-fetch
	 * the pointer after the function call completes.
	 */
	_SPI_current->processed = nfetched;

	if (dest->mydest == DestSPI && _SPI_checktuples())
		elog(ERROR, "consistency check on SPI tuple count failed");

	/* Put the result into place for access by caller */
	SPI_processed = _SPI_current->processed;
	SPI_tuptable = _SPI_current->tuptable;

	/* tuptable now is caller's responsibility, not SPI's */
	_SPI_current->tuptable = NULL;

	/* Pop the SPI stack */
	_SPI_end_call(true);
}


static MemoryContext
_SPI_execmem(void)
{
	return MemoryContextSwitchTo(_SPI_current->execCxt);
}

static MemoryContext
_SPI_procmem(void)
{
	return MemoryContextSwitchTo(_SPI_current->procCxt);
}

/*
 * _SPI_begin_call: begin a SPI operation within a connected procedure
 *
 * use_exec is true if we intend to make use of the procedure's execCxt
 * during this SPI operation.  We'll switch into that context, and arrange
 * for it to be cleaned up at _SPI_end_call or if an error occurs.
 */
static int
_SPI_begin_call(bool use_exec)
{
	if (_SPI_current == NULL)
		return SPI_ERROR_UNCONNECTED;

	if (use_exec)
	{
		/* remember when the Executor operation started */
		_SPI_current->execSubid = GetCurrentSubTransactionId();
		/* switch to the Executor memory context */
		_SPI_execmem();
	}

	return 0;
}

/*
 * _SPI_end_call: end a SPI operation within a connected procedure
 *
 * use_exec must be the same as in the previous _SPI_begin_call
 *
 * Note: this currently has no failure return cases, so callers don't check
 */
static int
_SPI_end_call(bool use_exec)
{
	if (use_exec)
	{
		/* switch to the procedure memory context */
		_SPI_procmem();
		/* mark Executor context no longer in use */
		_SPI_current->execSubid = InvalidSubTransactionId;
		/* and free Executor memory */
		MemoryContextResetAndDeleteChildren(_SPI_current->execCxt);
	}

	return 0;
}

static bool
_SPI_checktuples(void)
{
	uint64		processed = _SPI_current->processed;
	SPITupleTable *tuptable = _SPI_current->tuptable;
	bool		failed = false;

	if (tuptable == NULL)		/* spi_dest_startup was not called */
		failed = true;
	else if (processed != (tuptable->alloced - tuptable->free))
		failed = true;

	return failed;
}

/*
 * Convert a "temporary" SPIPlan into an "unsaved" plan.
 *
 * The passed _SPI_plan struct is on the stack, and all its subsidiary data
 * is in or under the current SPI executor context.  Copy the plan into the
 * SPI procedure context so it will survive _SPI_end_call().  To minimize
 * data copying, this destructively modifies the input plan, by taking the
 * plancache entries away from it and reparenting them to the new SPIPlan.
 */
static SPIPlanPtr
_SPI_make_plan_non_temp(SPIPlanPtr plan)
{
	SPIPlanPtr	newplan;
	MemoryContext parentcxt = _SPI_current->procCxt;
	MemoryContext plancxt;
	MemoryContext oldcxt;
	ListCell   *lc;

	/* Assert the input is a temporary SPIPlan */
	Assert(plan->magic == _SPI_PLAN_MAGIC);
	Assert(plan->plancxt == NULL);
	/* One-shot plans can't be saved */
	Assert(!plan->oneshot);

	/*
	 * Create a memory context for the plan, underneath the procedure context.
	 * We don't expect the plan to be very large.
	 */
	plancxt = AllocSetContextCreate(parentcxt,
									"SPI Plan",
									ALLOCSET_SMALL_SIZES);
	oldcxt = MemoryContextSwitchTo(plancxt);

	/* Copy the SPI_plan struct and subsidiary data into the new context */
	newplan = (SPIPlanPtr) palloc0(sizeof(_SPI_plan));
	newplan->magic = _SPI_PLAN_MAGIC;
	newplan->plancxt = plancxt;
	newplan->cursor_options = plan->cursor_options;
	newplan->nargs = plan->nargs;
	if (plan->nargs > 0)
	{
		newplan->argtypes = (Oid *) palloc(plan->nargs * sizeof(Oid));
		memcpy(newplan->argtypes, plan->argtypes, plan->nargs * sizeof(Oid));
	}
	else
		newplan->argtypes = NULL;
	newplan->parserSetup = plan->parserSetup;
	newplan->parserSetupArg = plan->parserSetupArg;

	/*
	 * Reparent all the CachedPlanSources into the procedure context.  In
	 * theory this could fail partway through due to the pallocs, but we don't
	 * care too much since both the procedure context and the executor context
	 * would go away on error.
	 */
	foreach(lc, plan->plancache_list)
	{
		CachedPlanSource *plansource = (CachedPlanSource *) lfirst(lc);

		CachedPlanSetParentContext(plansource, parentcxt);

		/* Build new list, with list cells in plancxt */
		newplan->plancache_list = lappend(newplan->plancache_list, plansource);
	}

	MemoryContextSwitchTo(oldcxt);

	/* For safety, unlink the CachedPlanSources from the temporary plan */
	plan->plancache_list = NIL;

	return newplan;
}

/*
 * Make a "saved" copy of the given plan.
 */
static SPIPlanPtr
_SPI_save_plan(SPIPlanPtr plan)
{
	SPIPlanPtr	newplan;
	MemoryContext plancxt;
	MemoryContext oldcxt;
	ListCell   *lc;

	/* One-shot plans can't be saved */
	Assert(!plan->oneshot);

	/*
	 * Create a memory context for the plan.  We don't expect the plan to be
	 * very large, so use smaller-than-default alloc parameters.  It's a
	 * transient context until we finish copying everything.
	 */
	plancxt = AllocSetContextCreate(GetCurrentMemoryContext(),
									"SPI Plan",
									ALLOCSET_SMALL_SIZES);
	oldcxt = MemoryContextSwitchTo(plancxt);

	/* Copy the SPI plan into its own context */
	newplan = (SPIPlanPtr) palloc0(sizeof(_SPI_plan));
	newplan->magic = _SPI_PLAN_MAGIC;
	newplan->plancxt = plancxt;
	newplan->cursor_options = plan->cursor_options;
	newplan->nargs = plan->nargs;
	if (plan->nargs > 0)
	{
		newplan->argtypes = (Oid *) palloc(plan->nargs * sizeof(Oid));
		memcpy(newplan->argtypes, plan->argtypes, plan->nargs * sizeof(Oid));
	}
	else
		newplan->argtypes = NULL;
	newplan->parserSetup = plan->parserSetup;
	newplan->parserSetupArg = plan->parserSetupArg;

	/* Copy all the plancache entries */
	foreach(lc, plan->plancache_list)
	{
		CachedPlanSource *plansource = (CachedPlanSource *) lfirst(lc);
		CachedPlanSource *newsource;

		newsource = CopyCachedPlan(plansource);
		newplan->plancache_list = lappend(newplan->plancache_list, newsource);
	}

	MemoryContextSwitchTo(oldcxt);

	/*
	 * Mark it saved, reparent it under CacheMemoryContext, and mark all the
	 * component CachedPlanSources as saved.  This sequence cannot fail
	 * partway through, so there's no risk of long-term memory leakage.
	 */
	newplan->saved = true;
	MemoryContextSetParent(newplan->plancxt, CacheMemoryContext);

	foreach(lc, newplan->plancache_list)
	{
		CachedPlanSource *plansource = (CachedPlanSource *) lfirst(lc);

		SaveCachedPlan(plansource);
	}

	return newplan;
}

/*
 * Internal lookup of ephemeral named relation by name.
 */
static EphemeralNamedRelation
_SPI_find_ENR_by_name(const char *name)
{
	/* internal static function; any error is bug in SPI itself */
	Assert(name != NULL);

	/* fast exit if no tuplestores have been added */
	if (_SPI_current->queryEnv == NULL)
		return NULL;

	return get_ENR(_SPI_current->queryEnv, name);
}

/*
 * Register an ephemeral named relation for use by the planner and executor on
 * subsequent calls using this SPI connection.
 */
int
SPI_register_relation(EphemeralNamedRelation enr)
{
	EphemeralNamedRelation match;
	int			res;

	if (enr == NULL || enr->md.name == NULL)
		return SPI_ERROR_ARGUMENT;

	res = _SPI_begin_call(false);	/* keep current memory context */
	if (res < 0)
		return res;

	match = _SPI_find_ENR_by_name(enr->md.name);
	if (match)
		res = SPI_ERROR_REL_DUPLICATE;
	else
	{
		if (_SPI_current->queryEnv == NULL)
			_SPI_current->queryEnv = create_queryEnv();

		register_ENR(_SPI_current->queryEnv, enr);
		res = SPI_OK_REL_REGISTER;
	}

	_SPI_end_call(false);

	return res;
}

/*
 * Unregister an ephemeral named relation by name.  This will probably be a
 * rarely used function, since SPI_finish will clear it automatically.
 */
int
SPI_unregister_relation(const char *name)
{
	EphemeralNamedRelation match;
	int			res;

	if (name == NULL)
		return SPI_ERROR_ARGUMENT;

	res = _SPI_begin_call(false);	/* keep current memory context */
	if (res < 0)
		return res;

	match = _SPI_find_ENR_by_name(name);
	if (match)
	{
		unregister_ENR(_SPI_current->queryEnv, match->md.name);
		res = SPI_OK_REL_UNREGISTER;
	}
	else
		res = SPI_ERROR_REL_NOT_FOUND;

	_SPI_end_call(false);

	return res;
}

/*
 * Register the transient relations from 'tdata' using this SPI connection.
 * This should be called by PL implementations' trigger handlers after
 * connecting, in order to make transition tables visible to any queries run
 * in this connection.
 */
int
SPI_register_trigger_data(TriggerData *tdata)
{
	if (tdata == NULL)
		return SPI_ERROR_ARGUMENT;

	if (tdata->tg_newtable)
	{
		EphemeralNamedRelation enr =
		palloc(sizeof(EphemeralNamedRelationData));
		int			rc;

		enr->md.name = tdata->tg_trigger->tgnewtable;
		enr->md.reliddesc = tdata->tg_relation->rd_id;
		enr->md.tupdesc = NULL;
		enr->md.enrtype = ENR_NAMED_TUPLESTORE;
		enr->md.enrtuples = tuplestore_tuple_count(tdata->tg_newtable);
		enr->reldata = tdata->tg_newtable;
		rc = SPI_register_relation(enr);
		if (rc != SPI_OK_REL_REGISTER)
			return rc;
	}

	if (tdata->tg_oldtable)
	{
		EphemeralNamedRelation enr =
		palloc(sizeof(EphemeralNamedRelationData));
		int			rc;

		enr->md.name = tdata->tg_trigger->tgoldtable;
		enr->md.reliddesc = tdata->tg_relation->rd_id;
		enr->md.tupdesc = NULL;
		enr->md.enrtype = ENR_NAMED_TUPLESTORE;
		enr->md.enrtuples = tuplestore_tuple_count(tdata->tg_oldtable);
		enr->reldata = tdata->tg_oldtable;
		rc = SPI_register_relation(enr);
		if (rc != SPI_OK_REL_REGISTER)
			return rc;
	}

	return SPI_OK_TD_REGISTER;
}
