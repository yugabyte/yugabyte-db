/*-------------------------------------------------------------------------
 *
 * vacuumlazy.c
 *	  Concurrent ("lazy") vacuuming.
 *
 * The major space usage for vacuuming is storage for the array of dead TIDs
 * that are to be removed from indexes.  We want to ensure we can vacuum even
 * the very largest relations with finite memory space usage.  To do that, we
 * set upper bounds on the number of TIDs we can keep track of at once.
 *
 * We are willing to use at most maintenance_work_mem (or perhaps
 * autovacuum_work_mem) memory space to keep track of dead TIDs.  We initially
 * allocate an array of TIDs of that size, with an upper limit that depends on
 * table size (this limit ensures we don't allocate a huge area uselessly for
 * vacuuming small tables).  If the array threatens to overflow, we must call
 * lazy_vacuum to vacuum indexes (and to vacuum the pages that we've pruned).
 * This frees up the memory space dedicated to storing dead TIDs.
 *
 * In practice VACUUM will often complete its initial pass over the target
 * heap relation without ever running out of space to store TIDs.  This means
 * that there only needs to be one call to lazy_vacuum, after the initial pass
 * completes.
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/access/heap/vacuumlazy.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include <math.h>

#include "access/amapi.h"
#include "access/genam.h"
#include "access/heapam.h"
#include "access/heapam_xlog.h"
#include "access/htup_details.h"
#include "access/multixact.h"
#include "access/transam.h"
#include "access/visibilitymap.h"
#include "access/xact.h"
#include "access/xlog.h"
#include "access/xloginsert.h"
#include "catalog/index.h"
#include "catalog/storage.h"
#include "commands/dbcommands.h"
#include "commands/progress.h"
#include "commands/vacuum.h"
#include "executor/instrument.h"
#include "miscadmin.h"
#include "optimizer/paths.h"
#include "pgstat.h"
#include "portability/instr_time.h"
#include "postmaster/autovacuum.h"
#include "storage/bufmgr.h"
#include "storage/freespace.h"
#include "storage/lmgr.h"
#include "tcop/tcopprot.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "utils/pg_rusage.h"
#include "utils/timestamp.h"


/*
 * Space/time tradeoff parameters: do these need to be user-tunable?
 *
 * To consider truncating the relation, we want there to be at least
 * REL_TRUNCATE_MINIMUM or (relsize / REL_TRUNCATE_FRACTION) (whichever
 * is less) potentially-freeable pages.
 */
#define REL_TRUNCATE_MINIMUM	1000
#define REL_TRUNCATE_FRACTION	16

/*
 * Timing parameters for truncate locking heuristics.
 *
 * These were not exposed as user tunable GUC values because it didn't seem
 * that the potential for improvement was great enough to merit the cost of
 * supporting them.
 */
#define VACUUM_TRUNCATE_LOCK_CHECK_INTERVAL		20	/* ms */
#define VACUUM_TRUNCATE_LOCK_WAIT_INTERVAL		50	/* ms */
#define VACUUM_TRUNCATE_LOCK_TIMEOUT			5000	/* ms */

/*
 * Threshold that controls whether we bypass index vacuuming and heap
 * vacuuming as an optimization
 */
#define BYPASS_THRESHOLD_PAGES	0.02	/* i.e. 2% of rel_pages */

/*
 * Perform a failsafe check every 4GB during the heap scan, approximately
 */
#define FAILSAFE_EVERY_PAGES \
	((BlockNumber) (((uint64) 4 * 1024 * 1024 * 1024) / BLCKSZ))

/*
 * When a table has no indexes, vacuum the FSM after every 8GB, approximately
 * (it won't be exact because we only vacuum FSM after processing a heap page
 * that has some removable tuples).  When there are indexes, this is ignored,
 * and we vacuum FSM after each index/heap cleaning pass.
 */
#define VACUUM_FSM_EVERY_PAGES \
	((BlockNumber) (((uint64) 8 * 1024 * 1024 * 1024) / BLCKSZ))

/*
 * Before we consider skipping a page that's marked as clean in
 * visibility map, we must've seen at least this many clean pages.
 */
#define SKIP_PAGES_THRESHOLD	((BlockNumber) 32)

/*
 * Size of the prefetch window for lazy vacuum backwards truncation scan.
 * Needs to be a power of 2.
 */
#define PREFETCH_SIZE			((BlockNumber) 32)

/*
 * Macro to check if we are in a parallel vacuum.  If true, we are in the
 * parallel mode and the DSM segment is initialized.
 */
#define ParallelVacuumIsActive(vacrel) ((vacrel)->pvs != NULL)

/* Phases of vacuum during which we report error context. */
typedef enum
{
	VACUUM_ERRCB_PHASE_UNKNOWN,
	VACUUM_ERRCB_PHASE_SCAN_HEAP,
	VACUUM_ERRCB_PHASE_VACUUM_INDEX,
	VACUUM_ERRCB_PHASE_VACUUM_HEAP,
	VACUUM_ERRCB_PHASE_INDEX_CLEANUP,
	VACUUM_ERRCB_PHASE_TRUNCATE
} VacErrPhase;

typedef struct LVRelState
{
	/* Target heap relation and its indexes */
	Relation	rel;
	Relation   *indrels;
	int			nindexes;

	/* Aggressive VACUUM? (must set relfrozenxid >= FreezeLimit) */
	bool		aggressive;
	/* Use visibility map to skip? (disabled by DISABLE_PAGE_SKIPPING) */
	bool		skipwithvm;
	/* Wraparound failsafe has been triggered? */
	bool		failsafe_active;
	/* Consider index vacuuming bypass optimization? */
	bool		consider_bypass_optimization;

	/* Doing index vacuuming, index cleanup, rel truncation? */
	bool		do_index_vacuuming;
	bool		do_index_cleanup;
	bool		do_rel_truncate;

	/* Buffer access strategy and parallel vacuum state */
	BufferAccessStrategy bstrategy;
	ParallelVacuumState *pvs;

	/* rel's initial relfrozenxid and relminmxid */
	TransactionId relfrozenxid;
	MultiXactId relminmxid;
	double		old_live_tuples;	/* previous value of pg_class.reltuples */

	/* VACUUM operation's cutoffs for freezing and pruning */
	TransactionId OldestXmin;
	GlobalVisState *vistest;
	/* VACUUM operation's target cutoffs for freezing XIDs and MultiXactIds */
	TransactionId FreezeLimit;
	MultiXactId MultiXactCutoff;
	/* Tracks oldest extant XID/MXID for setting relfrozenxid/relminmxid */
	TransactionId NewRelfrozenXid;
	MultiXactId NewRelminMxid;
	bool		skippedallvis;

	/* Error reporting state */
	char	   *relnamespace;
	char	   *relname;
	char	   *indname;		/* Current index name */
	BlockNumber blkno;			/* used only for heap operations */
	OffsetNumber offnum;		/* used only for heap operations */
	VacErrPhase phase;
	bool		verbose;		/* VACUUM VERBOSE? */

	/*
	 * dead_items stores TIDs whose index tuples are deleted by index
	 * vacuuming. Each TID points to an LP_DEAD line pointer from a heap page
	 * that has been processed by lazy_scan_prune.  Also needed by
	 * lazy_vacuum_heap_rel, which marks the same LP_DEAD line pointers as
	 * LP_UNUSED during second heap pass.
	 */
	VacDeadItems *dead_items;	/* TIDs whose index tuples we'll delete */
	BlockNumber rel_pages;		/* total number of pages */
	BlockNumber scanned_pages;	/* # pages examined (not skipped via VM) */
	BlockNumber removed_pages;	/* # pages removed by relation truncation */
	BlockNumber lpdead_item_pages;	/* # pages with LP_DEAD items */
	BlockNumber missed_dead_pages;	/* # pages with missed dead tuples */
	BlockNumber nonempty_pages; /* actually, last nonempty page + 1 */

	/* Statistics output by us, for table */
	double		new_rel_tuples; /* new estimated total # of tuples */
	double		new_live_tuples;	/* new estimated total # of live tuples */
	/* Statistics output by index AMs */
	IndexBulkDeleteResult **indstats;

	/* Instrumentation counters */
	int			num_index_scans;
	/* Counters that follow are only for scanned_pages */
	int64		tuples_deleted; /* # deleted from table */
	int64		lpdead_items;	/* # deleted from indexes */
	int64		live_tuples;	/* # live tuples remaining */
	int64		recently_dead_tuples;	/* # dead, but not yet removable */
	int64		missed_dead_tuples; /* # removable, but not removed */
} LVRelState;

/*
 * State returned by lazy_scan_prune()
 */
typedef struct LVPagePruneState
{
	bool		hastup;			/* Page prevents rel truncation? */
	bool		has_lpdead_items;	/* includes existing LP_DEAD items */

	/*
	 * State describes the proper VM bit states to set for the page following
	 * pruning and freezing.  all_visible implies !has_lpdead_items, but don't
	 * trust all_frozen result unless all_visible is also set to true.
	 */
	bool		all_visible;	/* Every item visible to all? */
	bool		all_frozen;		/* provided all_visible is also true */
	TransactionId visibility_cutoff_xid;	/* For recovery conflicts */
} LVPagePruneState;

/* Struct for saving and restoring vacuum error information. */
typedef struct LVSavedErrInfo
{
	BlockNumber blkno;
	OffsetNumber offnum;
	VacErrPhase phase;
} LVSavedErrInfo;


/* non-export function prototypes */
static void lazy_scan_heap(LVRelState *vacrel);
static BlockNumber lazy_scan_skip(LVRelState *vacrel, Buffer *vmbuffer,
								  BlockNumber next_block,
								  bool *next_unskippable_allvis,
								  bool *skipping_current_range);
static bool lazy_scan_new_or_empty(LVRelState *vacrel, Buffer buf,
								   BlockNumber blkno, Page page,
								   bool sharelock, Buffer vmbuffer);
static void lazy_scan_prune(LVRelState *vacrel, Buffer buf,
							BlockNumber blkno, Page page,
							LVPagePruneState *prunestate);
static bool lazy_scan_noprune(LVRelState *vacrel, Buffer buf,
							  BlockNumber blkno, Page page,
							  bool *hastup, bool *recordfreespace);
static void lazy_vacuum(LVRelState *vacrel);
static bool lazy_vacuum_all_indexes(LVRelState *vacrel);
static void lazy_vacuum_heap_rel(LVRelState *vacrel);
static int	lazy_vacuum_heap_page(LVRelState *vacrel, BlockNumber blkno,
								  Buffer buffer, int index, Buffer *vmbuffer);
static bool lazy_check_wraparound_failsafe(LVRelState *vacrel);
static void lazy_cleanup_all_indexes(LVRelState *vacrel);
static IndexBulkDeleteResult *lazy_vacuum_one_index(Relation indrel,
													IndexBulkDeleteResult *istat,
													double reltuples,
													LVRelState *vacrel);
static IndexBulkDeleteResult *lazy_cleanup_one_index(Relation indrel,
													 IndexBulkDeleteResult *istat,
													 double reltuples,
													 bool estimated_count,
													 LVRelState *vacrel);
static bool should_attempt_truncation(LVRelState *vacrel);
static void lazy_truncate_heap(LVRelState *vacrel);
static BlockNumber count_nondeletable_pages(LVRelState *vacrel,
											bool *lock_waiter_detected);
static void dead_items_alloc(LVRelState *vacrel, int nworkers);
static void dead_items_cleanup(LVRelState *vacrel);
static bool heap_page_is_all_visible(LVRelState *vacrel, Buffer buf,
									 TransactionId *visibility_cutoff_xid, bool *all_frozen);
static void update_relstats_all_indexes(LVRelState *vacrel);
static void vacuum_error_callback(void *arg);
static void update_vacuum_error_info(LVRelState *vacrel,
									 LVSavedErrInfo *saved_vacrel,
									 int phase, BlockNumber blkno,
									 OffsetNumber offnum);
static void restore_vacuum_error_info(LVRelState *vacrel,
									  const LVSavedErrInfo *saved_vacrel);


/*
 *	heap_vacuum_rel() -- perform VACUUM for one heap relation
 *
 *		This routine sets things up for and then calls lazy_scan_heap, where
 *		almost all work actually takes place.  Finalizes everything after call
 *		returns by managing relation truncation and updating rel's pg_class
 *		entry. (Also updates pg_class entries for any indexes that need it.)
 *
 *		At entry, we have already established a transaction and opened
 *		and locked the relation.
 */
void
heap_vacuum_rel(Relation rel, VacuumParams *params,
				BufferAccessStrategy bstrategy)
{
	LVRelState *vacrel;
	bool		verbose,
				instrument,
				aggressive,
				skipwithvm,
				frozenxid_updated,
				minmulti_updated;
	TransactionId OldestXmin,
				FreezeLimit;
	MultiXactId OldestMxact,
				MultiXactCutoff;
	BlockNumber orig_rel_pages,
				new_rel_pages,
				new_rel_allvisible;
	PGRUsage	ru0;
	TimestampTz starttime = 0;
	PgStat_Counter startreadtime = 0,
				startwritetime = 0;
	WalUsage	startwalusage = pgWalUsage;
	int64		StartPageHit = VacuumPageHit,
				StartPageMiss = VacuumPageMiss,
				StartPageDirty = VacuumPageDirty;
	ErrorContextCallback errcallback;
	char	  **indnames = NULL;

	verbose = (params->options & VACOPT_VERBOSE) != 0;
	instrument = (verbose || (IsAutoVacuumWorkerProcess() &&
							  params->log_min_duration >= 0));
	if (instrument)
	{
		pg_rusage_init(&ru0);
		starttime = GetCurrentTimestamp();
		if (track_io_timing)
		{
			startreadtime = pgStatBlockReadTime;
			startwritetime = pgStatBlockWriteTime;
		}
	}

	pgstat_progress_start_command(PROGRESS_COMMAND_VACUUM,
								  RelationGetRelid(rel));

	/*
	 * Get OldestXmin cutoff, which is used to determine which deleted tuples
	 * are considered DEAD, not just RECENTLY_DEAD.  Also get related cutoffs
	 * used to determine which XIDs/MultiXactIds will be frozen.  If this is
	 * an aggressive VACUUM then lazy_scan_heap cannot leave behind unfrozen
	 * XIDs < FreezeLimit (all MXIDs < MultiXactCutoff also need to go away).
	 */
	aggressive = vacuum_set_xid_limits(rel,
									   params->freeze_min_age,
									   params->freeze_table_age,
									   params->multixact_freeze_min_age,
									   params->multixact_freeze_table_age,
									   &OldestXmin, &OldestMxact,
									   &FreezeLimit, &MultiXactCutoff);

	skipwithvm = true;
	if (params->options & VACOPT_DISABLE_PAGE_SKIPPING)
	{
		/*
		 * Force aggressive mode, and disable skipping blocks using the
		 * visibility map (even those set all-frozen)
		 */
		aggressive = true;
		skipwithvm = false;
	}

	/*
	 * Setup error traceback support for ereport() first.  The idea is to set
	 * up an error context callback to display additional information on any
	 * error during a vacuum.  During different phases of vacuum, we update
	 * the state so that the error context callback always display current
	 * information.
	 *
	 * Copy the names of heap rel into local memory for error reporting
	 * purposes, too.  It isn't always safe to assume that we can get the name
	 * of each rel.  It's convenient for code in lazy_scan_heap to always use
	 * these temp copies.
	 */
	vacrel = (LVRelState *) palloc0(sizeof(LVRelState));
	vacrel->relnamespace = get_namespace_name(RelationGetNamespace(rel));
	vacrel->relname = pstrdup(RelationGetRelationName(rel));
	vacrel->indname = NULL;
	vacrel->phase = VACUUM_ERRCB_PHASE_UNKNOWN;
	vacrel->verbose = verbose;
	errcallback.callback = vacuum_error_callback;
	errcallback.arg = vacrel;
	errcallback.previous = error_context_stack;
	error_context_stack = &errcallback;
	if (verbose)
	{
		Assert(!IsAutoVacuumWorkerProcess());
		if (aggressive)
			ereport(INFO,
					(errmsg("aggressively vacuuming \"%s.%s.%s\"",
							get_database_name(MyDatabaseId),
							vacrel->relnamespace, vacrel->relname)));
		else
			ereport(INFO,
					(errmsg("vacuuming \"%s.%s.%s\"",
							get_database_name(MyDatabaseId),
							vacrel->relnamespace, vacrel->relname)));
	}

	/* Set up high level stuff about rel and its indexes */
	vacrel->rel = rel;
	vac_open_indexes(vacrel->rel, RowExclusiveLock, &vacrel->nindexes,
					 &vacrel->indrels);
	if (instrument && vacrel->nindexes > 0)
	{
		/* Copy index names used by instrumentation (not error reporting) */
		indnames = palloc(sizeof(char *) * vacrel->nindexes);
		for (int i = 0; i < vacrel->nindexes; i++)
			indnames[i] = pstrdup(RelationGetRelationName(vacrel->indrels[i]));
	}

	/*
	 * The index_cleanup param either disables index vacuuming and cleanup or
	 * forces it to go ahead when we would otherwise apply the index bypass
	 * optimization.  The default is 'auto', which leaves the final decision
	 * up to lazy_vacuum().
	 *
	 * The truncate param allows user to avoid attempting relation truncation,
	 * though it can't force truncation to happen.
	 */
	Assert(params->index_cleanup != VACOPTVALUE_UNSPECIFIED);
	Assert(params->truncate != VACOPTVALUE_UNSPECIFIED &&
		   params->truncate != VACOPTVALUE_AUTO);
	vacrel->aggressive = aggressive;
	vacrel->skipwithvm = skipwithvm;
	vacrel->failsafe_active = false;
	vacrel->consider_bypass_optimization = true;
	vacrel->do_index_vacuuming = true;
	vacrel->do_index_cleanup = true;
	vacrel->do_rel_truncate = (params->truncate != VACOPTVALUE_DISABLED);
	if (params->index_cleanup == VACOPTVALUE_DISABLED)
	{
		/* Force disable index vacuuming up-front */
		vacrel->do_index_vacuuming = false;
		vacrel->do_index_cleanup = false;
	}
	else if (params->index_cleanup == VACOPTVALUE_ENABLED)
	{
		/* Force index vacuuming.  Note that failsafe can still bypass. */
		vacrel->consider_bypass_optimization = false;
	}
	else
	{
		/* Default/auto, make all decisions dynamically */
		Assert(params->index_cleanup == VACOPTVALUE_AUTO);
	}

	vacrel->bstrategy = bstrategy;
	vacrel->relfrozenxid = rel->rd_rel->relfrozenxid;
	vacrel->relminmxid = rel->rd_rel->relminmxid;
	vacrel->old_live_tuples = rel->rd_rel->reltuples;

	/* Initialize page counters explicitly (be tidy) */
	vacrel->scanned_pages = 0;
	vacrel->removed_pages = 0;
	vacrel->lpdead_item_pages = 0;
	vacrel->missed_dead_pages = 0;
	vacrel->nonempty_pages = 0;
	/* dead_items_alloc allocates vacrel->dead_items later on */

	/* Allocate/initialize output statistics state */
	vacrel->new_rel_tuples = 0;
	vacrel->new_live_tuples = 0;
	vacrel->indstats = (IndexBulkDeleteResult **)
		palloc0(vacrel->nindexes * sizeof(IndexBulkDeleteResult *));

	/* Initialize remaining counters (be tidy) */
	vacrel->num_index_scans = 0;
	vacrel->tuples_deleted = 0;
	vacrel->lpdead_items = 0;
	vacrel->live_tuples = 0;
	vacrel->recently_dead_tuples = 0;
	vacrel->missed_dead_tuples = 0;

	/*
	 * Determine the extent of the blocks that we'll scan in lazy_scan_heap,
	 * and finalize cutoffs used for freezing and pruning in lazy_scan_prune.
	 *
	 * We expect vistest will always make heap_page_prune remove any deleted
	 * tuple whose xmax is < OldestXmin.  lazy_scan_prune must never become
	 * confused about whether a tuple should be frozen or removed.  (In the
	 * future we might want to teach lazy_scan_prune to recompute vistest from
	 * time to time, to increase the number of dead tuples it can prune away.)
	 *
	 * We must determine rel_pages _after_ OldestXmin has been established.
	 * lazy_scan_heap's physical heap scan (scan of pages < rel_pages) is
	 * thereby guaranteed to not miss any tuples with XIDs < OldestXmin. These
	 * XIDs must at least be considered for freezing (though not necessarily
	 * frozen) during its scan.
	 */
	vacrel->rel_pages = orig_rel_pages = RelationGetNumberOfBlocks(rel);
	vacrel->OldestXmin = OldestXmin;
	vacrel->vistest = GlobalVisTestFor(rel);
	/* FreezeLimit controls XID freezing (always <= OldestXmin) */
	vacrel->FreezeLimit = FreezeLimit;
	/* MultiXactCutoff controls MXID freezing (always <= OldestMxact) */
	vacrel->MultiXactCutoff = MultiXactCutoff;
	/* Initialize state used to track oldest extant XID/MXID */
	vacrel->NewRelfrozenXid = OldestXmin;
	vacrel->NewRelminMxid = OldestMxact;
	vacrel->skippedallvis = false;

	/*
	 * Allocate dead_items array memory using dead_items_alloc.  This handles
	 * parallel VACUUM initialization as part of allocating shared memory
	 * space used for dead_items.  (But do a failsafe precheck first, to
	 * ensure that parallel VACUUM won't be attempted at all when relfrozenxid
	 * is already dangerously old.)
	 */
	lazy_check_wraparound_failsafe(vacrel);
	dead_items_alloc(vacrel, params->nworkers);

	/*
	 * Call lazy_scan_heap to perform all required heap pruning, index
	 * vacuuming, and heap vacuuming (plus related processing)
	 */
	lazy_scan_heap(vacrel);

	/*
	 * Free resources managed by dead_items_alloc.  This ends parallel mode in
	 * passing when necessary.
	 */
	dead_items_cleanup(vacrel);
	Assert(!IsInParallelMode());

	/*
	 * Update pg_class entries for each of rel's indexes where appropriate.
	 *
	 * Unlike the later update to rel's pg_class entry, this is not critical.
	 * Maintains relpages/reltuples statistics used by the planner only.
	 */
	if (vacrel->do_index_cleanup)
		update_relstats_all_indexes(vacrel);

	/* Done with rel's indexes */
	vac_close_indexes(vacrel->nindexes, vacrel->indrels, NoLock);

	/* Optionally truncate rel */
	if (should_attempt_truncation(vacrel))
		lazy_truncate_heap(vacrel);

	/* Pop the error context stack */
	error_context_stack = errcallback.previous;

	/* Report that we are now doing final cleanup */
	pgstat_progress_update_param(PROGRESS_VACUUM_PHASE,
								 PROGRESS_VACUUM_PHASE_FINAL_CLEANUP);

	/*
	 * Prepare to update rel's pg_class entry.
	 *
	 * Aggressive VACUUMs must always be able to advance relfrozenxid to a
	 * value >= FreezeLimit, and relminmxid to a value >= MultiXactCutoff.
	 * Non-aggressive VACUUMs may advance them by any amount, or not at all.
	 */
	Assert(vacrel->NewRelfrozenXid == OldestXmin ||
		   TransactionIdPrecedesOrEquals(aggressive ? FreezeLimit :
										 vacrel->relfrozenxid,
										 vacrel->NewRelfrozenXid));
	Assert(vacrel->NewRelminMxid == OldestMxact ||
		   MultiXactIdPrecedesOrEquals(aggressive ? MultiXactCutoff :
									   vacrel->relminmxid,
									   vacrel->NewRelminMxid));
	if (vacrel->skippedallvis)
	{
		/*
		 * Must keep original relfrozenxid in a non-aggressive VACUUM that
		 * chose to skip an all-visible page range.  The state that tracks new
		 * values will have missed unfrozen XIDs from the pages we skipped.
		 */
		Assert(!aggressive);
		vacrel->NewRelfrozenXid = InvalidTransactionId;
		vacrel->NewRelminMxid = InvalidMultiXactId;
	}

	/*
	 * For safety, clamp relallvisible to be not more than what we're setting
	 * pg_class.relpages to
	 */
	new_rel_pages = vacrel->rel_pages;	/* After possible rel truncation */
	visibilitymap_count(rel, &new_rel_allvisible, NULL);
	if (new_rel_allvisible > new_rel_pages)
		new_rel_allvisible = new_rel_pages;

	/*
	 * Now actually update rel's pg_class entry.
	 *
	 * In principle new_live_tuples could be -1 indicating that we (still)
	 * don't know the tuple count.  In practice that can't happen, since we
	 * scan every page that isn't skipped using the visibility map.
	 */
	vac_update_relstats(rel, new_rel_pages, vacrel->new_live_tuples,
						new_rel_allvisible, vacrel->nindexes > 0,
						vacrel->NewRelfrozenXid, vacrel->NewRelminMxid,
						&frozenxid_updated, &minmulti_updated, false);

	/*
	 * Report results to the cumulative stats system, too.
	 *
	 * Deliberately avoid telling the stats system about LP_DEAD items that
	 * remain in the table due to VACUUM bypassing index and heap vacuuming.
	 * ANALYZE will consider the remaining LP_DEAD items to be dead "tuples".
	 * It seems like a good idea to err on the side of not vacuuming again too
	 * soon in cases where the failsafe prevented significant amounts of heap
	 * vacuuming.
	 */
	pgstat_report_vacuum(RelationGetRelid(rel),
						 rel->rd_rel->relisshared,
						 Max(vacrel->new_live_tuples, 0),
						 vacrel->recently_dead_tuples +
						 vacrel->missed_dead_tuples);
	pgstat_progress_end_command();

	if (instrument)
	{
		TimestampTz endtime = GetCurrentTimestamp();

		if (verbose || params->log_min_duration == 0 ||
			TimestampDifferenceExceeds(starttime, endtime,
									   params->log_min_duration))
		{
			long		secs_dur;
			int			usecs_dur;
			WalUsage	walusage;
			StringInfoData buf;
			char	   *msgfmt;
			int32		diff;
			int64		PageHitOp = VacuumPageHit - StartPageHit,
						PageMissOp = VacuumPageMiss - StartPageMiss,
						PageDirtyOp = VacuumPageDirty - StartPageDirty;
			double		read_rate = 0,
						write_rate = 0;

			TimestampDifference(starttime, endtime, &secs_dur, &usecs_dur);
			memset(&walusage, 0, sizeof(WalUsage));
			WalUsageAccumDiff(&walusage, &pgWalUsage, &startwalusage);

			initStringInfo(&buf);
			if (verbose)
			{
				/*
				 * Aggressiveness already reported earlier, in dedicated
				 * VACUUM VERBOSE ereport
				 */
				Assert(!params->is_wraparound);
				msgfmt = _("finished vacuuming \"%s.%s.%s\": index scans: %d\n");
			}
			else if (params->is_wraparound)
			{
				/*
				 * While it's possible for a VACUUM to be both is_wraparound
				 * and !aggressive, that's just a corner-case -- is_wraparound
				 * implies aggressive.  Produce distinct output for the corner
				 * case all the same, just in case.
				 */
				if (aggressive)
					msgfmt = _("automatic aggressive vacuum to prevent wraparound of table \"%s.%s.%s\": index scans: %d\n");
				else
					msgfmt = _("automatic vacuum to prevent wraparound of table \"%s.%s.%s\": index scans: %d\n");
			}
			else
			{
				if (aggressive)
					msgfmt = _("automatic aggressive vacuum of table \"%s.%s.%s\": index scans: %d\n");
				else
					msgfmt = _("automatic vacuum of table \"%s.%s.%s\": index scans: %d\n");
			}
			appendStringInfo(&buf, msgfmt,
							 get_database_name(MyDatabaseId),
							 vacrel->relnamespace,
							 vacrel->relname,
							 vacrel->num_index_scans);
			appendStringInfo(&buf, _("pages: %u removed, %u remain, %u scanned (%.2f%% of total)\n"),
							 vacrel->removed_pages,
							 new_rel_pages,
							 vacrel->scanned_pages,
							 orig_rel_pages == 0 ? 100.0 :
							 100.0 * vacrel->scanned_pages / orig_rel_pages);
			appendStringInfo(&buf,
							 _("tuples: %lld removed, %lld remain, %lld are dead but not yet removable\n"),
							 (long long) vacrel->tuples_deleted,
							 (long long) vacrel->new_rel_tuples,
							 (long long) vacrel->recently_dead_tuples);
			if (vacrel->missed_dead_tuples > 0)
				appendStringInfo(&buf,
								 _("tuples missed: %lld dead from %u pages not removed due to cleanup lock contention\n"),
								 (long long) vacrel->missed_dead_tuples,
								 vacrel->missed_dead_pages);
			diff = (int32) (ReadNextTransactionId() - OldestXmin);
			appendStringInfo(&buf,
							 _("removable cutoff: %u, which was %d XIDs old when operation ended\n"),
							 OldestXmin, diff);
			if (frozenxid_updated)
			{
				diff = (int32) (vacrel->NewRelfrozenXid - vacrel->relfrozenxid);
				appendStringInfo(&buf,
								 _("new relfrozenxid: %u, which is %d XIDs ahead of previous value\n"),
								 vacrel->NewRelfrozenXid, diff);
			}
			if (minmulti_updated)
			{
				diff = (int32) (vacrel->NewRelminMxid - vacrel->relminmxid);
				appendStringInfo(&buf,
								 _("new relminmxid: %u, which is %d MXIDs ahead of previous value\n"),
								 vacrel->NewRelminMxid, diff);
			}
			if (vacrel->do_index_vacuuming)
			{
				if (vacrel->nindexes == 0 || vacrel->num_index_scans == 0)
					appendStringInfoString(&buf, _("index scan not needed: "));
				else
					appendStringInfoString(&buf, _("index scan needed: "));

				msgfmt = _("%u pages from table (%.2f%% of total) had %lld dead item identifiers removed\n");
			}
			else
			{
				if (!vacrel->failsafe_active)
					appendStringInfoString(&buf, _("index scan bypassed: "));
				else
					appendStringInfoString(&buf, _("index scan bypassed by failsafe: "));

				msgfmt = _("%u pages from table (%.2f%% of total) have %lld dead item identifiers\n");
			}
			appendStringInfo(&buf, msgfmt,
							 vacrel->lpdead_item_pages,
							 orig_rel_pages == 0 ? 100.0 :
							 100.0 * vacrel->lpdead_item_pages / orig_rel_pages,
							 (long long) vacrel->lpdead_items);
			for (int i = 0; i < vacrel->nindexes; i++)
			{
				IndexBulkDeleteResult *istat = vacrel->indstats[i];

				if (!istat)
					continue;

				appendStringInfo(&buf,
								 _("index \"%s\": pages: %u in total, %u newly deleted, %u currently deleted, %u reusable\n"),
								 indnames[i],
								 istat->num_pages,
								 istat->pages_newly_deleted,
								 istat->pages_deleted,
								 istat->pages_free);
			}
			if (track_io_timing)
			{
				double		read_ms = (double) (pgStatBlockReadTime - startreadtime) / 1000;
				double		write_ms = (double) (pgStatBlockWriteTime - startwritetime) / 1000;

				appendStringInfo(&buf, _("I/O timings: read: %.3f ms, write: %.3f ms\n"),
								 read_ms, write_ms);
			}
			if (secs_dur > 0 || usecs_dur > 0)
			{
				read_rate = (double) BLCKSZ * PageMissOp / (1024 * 1024) /
					(secs_dur + usecs_dur / 1000000.0);
				write_rate = (double) BLCKSZ * PageDirtyOp / (1024 * 1024) /
					(secs_dur + usecs_dur / 1000000.0);
			}
			appendStringInfo(&buf, _("avg read rate: %.3f MB/s, avg write rate: %.3f MB/s\n"),
							 read_rate, write_rate);
			appendStringInfo(&buf,
							 _("buffer usage: %lld hits, %lld misses, %lld dirtied\n"),
							 (long long) PageHitOp,
							 (long long) PageMissOp,
							 (long long) PageDirtyOp);
			appendStringInfo(&buf,
							 _("WAL usage: %lld records, %lld full page images, %llu bytes\n"),
							 (long long) walusage.wal_records,
							 (long long) walusage.wal_fpi,
							 (unsigned long long) walusage.wal_bytes);
			appendStringInfo(&buf, _("system usage: %s"), pg_rusage_show(&ru0));

			ereport(verbose ? INFO : LOG,
					(errmsg_internal("%s", buf.data)));
			pfree(buf.data);
		}
	}

	/* Cleanup index statistics and index names */
	for (int i = 0; i < vacrel->nindexes; i++)
	{
		if (vacrel->indstats[i])
			pfree(vacrel->indstats[i]);

		if (instrument)
			pfree(indnames[i]);
	}
}

/*
 *	lazy_scan_heap() -- workhorse function for VACUUM
 *
 *		This routine prunes each page in the heap, and considers the need to
 *		freeze remaining tuples with storage (not including pages that can be
 *		skipped using the visibility map).  Also performs related maintenance
 *		of the FSM and visibility map.  These steps all take place during an
 *		initial pass over the target heap relation.
 *
 *		Also invokes lazy_vacuum_all_indexes to vacuum indexes, which largely
 *		consists of deleting index tuples that point to LP_DEAD items left in
 *		heap pages following pruning.  Earlier initial pass over the heap will
 *		have collected the TIDs whose index tuples need to be removed.
 *
 *		Finally, invokes lazy_vacuum_heap_rel to vacuum heap pages, which
 *		largely consists of marking LP_DEAD items (from collected TID array)
 *		as LP_UNUSED.  This has to happen in a second, final pass over the
 *		heap, to preserve a basic invariant that all index AMs rely on: no
 *		extant index tuple can ever be allowed to contain a TID that points to
 *		an LP_UNUSED line pointer in the heap.  We must disallow premature
 *		recycling of line pointers to avoid index scans that get confused
 *		about which TID points to which tuple immediately after recycling.
 *		(Actually, this isn't a concern when target heap relation happens to
 *		have no indexes, which allows us to safely apply the one-pass strategy
 *		as an optimization).
 *
 *		In practice we often have enough space to fit all TIDs, and so won't
 *		need to call lazy_vacuum more than once, after our initial pass over
 *		the heap has totally finished.  Otherwise things are slightly more
 *		complicated: our "initial pass" over the heap applies only to those
 *		pages that were pruned before we needed to call lazy_vacuum, and our
 *		"final pass" over the heap only vacuums these same heap pages.
 *		However, we process indexes in full every time lazy_vacuum is called,
 *		which makes index processing very inefficient when memory is in short
 *		supply.
 */
static void
lazy_scan_heap(LVRelState *vacrel)
{
	BlockNumber rel_pages = vacrel->rel_pages,
				blkno,
				next_unskippable_block,
				next_failsafe_block = 0,
				next_fsm_block_to_vacuum = 0;
	VacDeadItems *dead_items = vacrel->dead_items;
	Buffer		vmbuffer = InvalidBuffer;
	bool		next_unskippable_allvis,
				skipping_current_range;
	const int	initprog_index[] = {
		PROGRESS_VACUUM_PHASE,
		PROGRESS_VACUUM_TOTAL_HEAP_BLKS,
		PROGRESS_VACUUM_MAX_DEAD_TUPLES
	};
	int64		initprog_val[3];

	/* Report that we're scanning the heap, advertising total # of blocks */
	initprog_val[0] = PROGRESS_VACUUM_PHASE_SCAN_HEAP;
	initprog_val[1] = rel_pages;
	initprog_val[2] = dead_items->max_items;
	pgstat_progress_update_multi_param(3, initprog_index, initprog_val);

	/* Set up an initial range of skippable blocks using the visibility map */
	next_unskippable_block = lazy_scan_skip(vacrel, &vmbuffer, 0,
											&next_unskippable_allvis,
											&skipping_current_range);
	for (blkno = 0; blkno < rel_pages; blkno++)
	{
		Buffer		buf;
		Page		page;
		bool		all_visible_according_to_vm;
		LVPagePruneState prunestate;

		if (blkno == next_unskippable_block)
		{
			/*
			 * Can't skip this page safely.  Must scan the page.  But
			 * determine the next skippable range after the page first.
			 */
			all_visible_according_to_vm = next_unskippable_allvis;
			next_unskippable_block = lazy_scan_skip(vacrel, &vmbuffer,
													blkno + 1,
													&next_unskippable_allvis,
													&skipping_current_range);

			Assert(next_unskippable_block >= blkno + 1);
		}
		else
		{
			/* Last page always scanned (may need to set nonempty_pages) */
			Assert(blkno < rel_pages - 1);

			if (skipping_current_range)
				continue;

			/* Current range is too small to skip -- just scan the page */
			all_visible_according_to_vm = true;
		}

		vacrel->scanned_pages++;

		/* Report as block scanned, update error traceback information */
		pgstat_progress_update_param(PROGRESS_VACUUM_HEAP_BLKS_SCANNED, blkno);
		update_vacuum_error_info(vacrel, NULL, VACUUM_ERRCB_PHASE_SCAN_HEAP,
								 blkno, InvalidOffsetNumber);

		vacuum_delay_point();

		/*
		 * Regularly check if wraparound failsafe should trigger.
		 *
		 * There is a similar check inside lazy_vacuum_all_indexes(), but
		 * relfrozenxid might start to look dangerously old before we reach
		 * that point.  This check also provides failsafe coverage for the
		 * one-pass strategy, and the two-pass strategy with the index_cleanup
		 * param set to 'off'.
		 */
		if (blkno - next_failsafe_block >= FAILSAFE_EVERY_PAGES)
		{
			lazy_check_wraparound_failsafe(vacrel);
			next_failsafe_block = blkno;
		}

		/*
		 * Consider if we definitely have enough space to process TIDs on page
		 * already.  If we are close to overrunning the available space for
		 * dead_items TIDs, pause and do a cycle of vacuuming before we tackle
		 * this page.
		 */
		Assert(dead_items->max_items >= MaxHeapTuplesPerPage);
		if (dead_items->max_items - dead_items->num_items < MaxHeapTuplesPerPage)
		{
			/*
			 * Before beginning index vacuuming, we release any pin we may
			 * hold on the visibility map page.  This isn't necessary for
			 * correctness, but we do it anyway to avoid holding the pin
			 * across a lengthy, unrelated operation.
			 */
			if (BufferIsValid(vmbuffer))
			{
				ReleaseBuffer(vmbuffer);
				vmbuffer = InvalidBuffer;
			}

			/* Perform a round of index and heap vacuuming */
			vacrel->consider_bypass_optimization = false;
			lazy_vacuum(vacrel);

			/*
			 * Vacuum the Free Space Map to make newly-freed space visible on
			 * upper-level FSM pages.  Note we have not yet processed blkno.
			 */
			FreeSpaceMapVacuumRange(vacrel->rel, next_fsm_block_to_vacuum,
									blkno);
			next_fsm_block_to_vacuum = blkno;

			/* Report that we are once again scanning the heap */
			pgstat_progress_update_param(PROGRESS_VACUUM_PHASE,
										 PROGRESS_VACUUM_PHASE_SCAN_HEAP);
		}

		/*
		 * Pin the visibility map page in case we need to mark the page
		 * all-visible.  In most cases this will be very cheap, because we'll
		 * already have the correct page pinned anyway.
		 */
		visibilitymap_pin(vacrel->rel, blkno, &vmbuffer);

		/* Finished preparatory checks.  Actually scan the page. */
		buf = ReadBufferExtended(vacrel->rel, MAIN_FORKNUM, blkno,
								 RBM_NORMAL, vacrel->bstrategy);
		page = BufferGetPage(buf);

		/*
		 * We need a buffer cleanup lock to prune HOT chains and defragment
		 * the page in lazy_scan_prune.  But when it's not possible to acquire
		 * a cleanup lock right away, we may be able to settle for reduced
		 * processing using lazy_scan_noprune.
		 */
		if (!ConditionalLockBufferForCleanup(buf))
		{
			bool		hastup,
						recordfreespace;

			LockBuffer(buf, BUFFER_LOCK_SHARE);

			/* Check for new or empty pages before lazy_scan_noprune call */
			if (lazy_scan_new_or_empty(vacrel, buf, blkno, page, true,
									   vmbuffer))
			{
				/* Processed as new/empty page (lock and pin released) */
				continue;
			}

			/* Collect LP_DEAD items in dead_items array, count tuples */
			if (lazy_scan_noprune(vacrel, buf, blkno, page, &hastup,
								  &recordfreespace))
			{
				Size		freespace = 0;

				/*
				 * Processed page successfully (without cleanup lock) -- just
				 * need to perform rel truncation and FSM steps, much like the
				 * lazy_scan_prune case.  Don't bother trying to match its
				 * visibility map setting steps, though.
				 */
				if (hastup)
					vacrel->nonempty_pages = blkno + 1;
				if (recordfreespace)
					freespace = PageGetHeapFreeSpace(page);
				UnlockReleaseBuffer(buf);
				if (recordfreespace)
					RecordPageWithFreeSpace(vacrel->rel, blkno, freespace);
				continue;
			}

			/*
			 * lazy_scan_noprune could not do all required processing.  Wait
			 * for a cleanup lock, and call lazy_scan_prune in the usual way.
			 */
			Assert(vacrel->aggressive);
			LockBuffer(buf, BUFFER_LOCK_UNLOCK);
			LockBufferForCleanup(buf);
		}

		/* Check for new or empty pages before lazy_scan_prune call */
		if (lazy_scan_new_or_empty(vacrel, buf, blkno, page, false, vmbuffer))
		{
			/* Processed as new/empty page (lock and pin released) */
			continue;
		}

		/*
		 * Prune, freeze, and count tuples.
		 *
		 * Accumulates details of remaining LP_DEAD line pointers on page in
		 * dead_items array.  This includes LP_DEAD line pointers that we
		 * pruned ourselves, as well as existing LP_DEAD line pointers that
		 * were pruned some time earlier.  Also considers freezing XIDs in the
		 * tuple headers of remaining items with storage.
		 */
		lazy_scan_prune(vacrel, buf, blkno, page, &prunestate);

		Assert(!prunestate.all_visible || !prunestate.has_lpdead_items);

		/* Remember the location of the last page with nonremovable tuples */
		if (prunestate.hastup)
			vacrel->nonempty_pages = blkno + 1;

		if (vacrel->nindexes == 0)
		{
			/*
			 * Consider the need to do page-at-a-time heap vacuuming when
			 * using the one-pass strategy now.
			 *
			 * The one-pass strategy will never call lazy_vacuum().  The steps
			 * performed here can be thought of as the one-pass equivalent of
			 * a call to lazy_vacuum().
			 */
			if (prunestate.has_lpdead_items)
			{
				Size		freespace;

				lazy_vacuum_heap_page(vacrel, blkno, buf, 0, &vmbuffer);

				/* Forget the LP_DEAD items that we just vacuumed */
				dead_items->num_items = 0;

				/*
				 * Periodically perform FSM vacuuming to make newly-freed
				 * space visible on upper FSM pages.  Note we have not yet
				 * performed FSM processing for blkno.
				 */
				if (blkno - next_fsm_block_to_vacuum >= VACUUM_FSM_EVERY_PAGES)
				{
					FreeSpaceMapVacuumRange(vacrel->rel, next_fsm_block_to_vacuum,
											blkno);
					next_fsm_block_to_vacuum = blkno;
				}

				/*
				 * Now perform FSM processing for blkno, and move on to next
				 * page.
				 *
				 * Our call to lazy_vacuum_heap_page() will have considered if
				 * it's possible to set all_visible/all_frozen independently
				 * of lazy_scan_prune().  Note that prunestate was invalidated
				 * by lazy_vacuum_heap_page() call.
				 */
				freespace = PageGetHeapFreeSpace(page);

				UnlockReleaseBuffer(buf);
				RecordPageWithFreeSpace(vacrel->rel, blkno, freespace);
				continue;
			}

			/*
			 * There was no call to lazy_vacuum_heap_page() because pruning
			 * didn't encounter/create any LP_DEAD items that needed to be
			 * vacuumed.  Prune state has not been invalidated, so proceed
			 * with prunestate-driven visibility map and FSM steps (just like
			 * the two-pass strategy).
			 */
			Assert(dead_items->num_items == 0);
		}

		/*
		 * Handle setting visibility map bit based on information from the VM
		 * (as of last lazy_scan_skip() call), and from prunestate
		 */
		if (!all_visible_according_to_vm && prunestate.all_visible)
		{
			uint8		flags = VISIBILITYMAP_ALL_VISIBLE;

			if (prunestate.all_frozen)
				flags |= VISIBILITYMAP_ALL_FROZEN;

			/*
			 * It should never be the case that the visibility map page is set
			 * while the page-level bit is clear, but the reverse is allowed
			 * (if checksums are not enabled).  Regardless, set both bits so
			 * that we get back in sync.
			 *
			 * NB: If the heap page is all-visible but the VM bit is not set,
			 * we don't need to dirty the heap page.  However, if checksums
			 * are enabled, we do need to make sure that the heap page is
			 * dirtied before passing it to visibilitymap_set(), because it
			 * may be logged.  Given that this situation should only happen in
			 * rare cases after a crash, it is not worth optimizing.
			 */
			PageSetAllVisible(page);
			MarkBufferDirty(buf);
			visibilitymap_set(vacrel->rel, blkno, buf, InvalidXLogRecPtr,
							  vmbuffer, prunestate.visibility_cutoff_xid,
							  flags);
		}

		/*
		 * As of PostgreSQL 9.2, the visibility map bit should never be set if
		 * the page-level bit is clear.  However, it's possible that the bit
		 * got cleared after lazy_scan_skip() was called, so we must recheck
		 * with buffer lock before concluding that the VM is corrupt.
		 */
		else if (all_visible_according_to_vm && !PageIsAllVisible(page)
				 && VM_ALL_VISIBLE(vacrel->rel, blkno, &vmbuffer))
		{
			elog(WARNING, "page is not marked all-visible but visibility map bit is set in relation \"%s\" page %u",
				 vacrel->relname, blkno);
			visibilitymap_clear(vacrel->rel, blkno, vmbuffer,
								VISIBILITYMAP_VALID_BITS);
		}

		/*
		 * It's possible for the value returned by
		 * GetOldestNonRemovableTransactionId() to move backwards, so it's not
		 * wrong for us to see tuples that appear to not be visible to
		 * everyone yet, while PD_ALL_VISIBLE is already set. The real safe
		 * xmin value never moves backwards, but
		 * GetOldestNonRemovableTransactionId() is conservative and sometimes
		 * returns a value that's unnecessarily small, so if we see that
		 * contradiction it just means that the tuples that we think are not
		 * visible to everyone yet actually are, and the PD_ALL_VISIBLE flag
		 * is correct.
		 *
		 * There should never be LP_DEAD items on a page with PD_ALL_VISIBLE
		 * set, however.
		 */
		else if (prunestate.has_lpdead_items && PageIsAllVisible(page))
		{
			elog(WARNING, "page containing LP_DEAD items is marked as all-visible in relation \"%s\" page %u",
				 vacrel->relname, blkno);
			PageClearAllVisible(page);
			MarkBufferDirty(buf);
			visibilitymap_clear(vacrel->rel, blkno, vmbuffer,
								VISIBILITYMAP_VALID_BITS);
		}

		/*
		 * If the all-visible page is all-frozen but not marked as such yet,
		 * mark it as all-frozen.  Note that all_frozen is only valid if
		 * all_visible is true, so we must check both prunestate fields.
		 */
		else if (all_visible_according_to_vm && prunestate.all_visible &&
				 prunestate.all_frozen &&
				 !VM_ALL_FROZEN(vacrel->rel, blkno, &vmbuffer))
		{
			/*
			 * We can pass InvalidTransactionId as the cutoff XID here,
			 * because setting the all-frozen bit doesn't cause recovery
			 * conflicts.
			 */
			visibilitymap_set(vacrel->rel, blkno, buf, InvalidXLogRecPtr,
							  vmbuffer, InvalidTransactionId,
							  VISIBILITYMAP_ALL_FROZEN);
		}

		/*
		 * Final steps for block: drop cleanup lock, record free space in the
		 * FSM
		 */
		if (prunestate.has_lpdead_items && vacrel->do_index_vacuuming)
		{
			/*
			 * Wait until lazy_vacuum_heap_rel() to save free space.  This
			 * doesn't just save us some cycles; it also allows us to record
			 * any additional free space that lazy_vacuum_heap_page() will
			 * make available in cases where it's possible to truncate the
			 * page's line pointer array.
			 *
			 * Note: It's not in fact 100% certain that we really will call
			 * lazy_vacuum_heap_rel() -- lazy_vacuum() might yet opt to skip
			 * index vacuuming (and so must skip heap vacuuming).  This is
			 * deemed okay because it only happens in emergencies, or when
			 * there is very little free space anyway. (Besides, we start
			 * recording free space in the FSM once index vacuuming has been
			 * abandoned.)
			 *
			 * Note: The one-pass (no indexes) case is only supposed to make
			 * it this far when there were no LP_DEAD items during pruning.
			 */
			Assert(vacrel->nindexes > 0);
			UnlockReleaseBuffer(buf);
		}
		else
		{
			Size		freespace = PageGetHeapFreeSpace(page);

			UnlockReleaseBuffer(buf);
			RecordPageWithFreeSpace(vacrel->rel, blkno, freespace);
		}
	}

	vacrel->blkno = InvalidBlockNumber;
	if (BufferIsValid(vmbuffer))
		ReleaseBuffer(vmbuffer);

	/* report that everything is now scanned */
	pgstat_progress_update_param(PROGRESS_VACUUM_HEAP_BLKS_SCANNED, blkno);

	/* now we can compute the new value for pg_class.reltuples */
	vacrel->new_live_tuples = vac_estimate_reltuples(vacrel->rel, rel_pages,
													 vacrel->scanned_pages,
													 vacrel->live_tuples);

	/*
	 * Also compute the total number of surviving heap entries.  In the
	 * (unlikely) scenario that new_live_tuples is -1, take it as zero.
	 */
	vacrel->new_rel_tuples =
		Max(vacrel->new_live_tuples, 0) + vacrel->recently_dead_tuples +
		vacrel->missed_dead_tuples;

	/*
	 * Do index vacuuming (call each index's ambulkdelete routine), then do
	 * related heap vacuuming
	 */
	if (dead_items->num_items > 0)
		lazy_vacuum(vacrel);

	/*
	 * Vacuum the remainder of the Free Space Map.  We must do this whether or
	 * not there were indexes, and whether or not we bypassed index vacuuming.
	 */
	if (blkno > next_fsm_block_to_vacuum)
		FreeSpaceMapVacuumRange(vacrel->rel, next_fsm_block_to_vacuum, blkno);

	/* report all blocks vacuumed */
	pgstat_progress_update_param(PROGRESS_VACUUM_HEAP_BLKS_VACUUMED, blkno);

	/* Do final index cleanup (call each index's amvacuumcleanup routine) */
	if (vacrel->nindexes > 0 && vacrel->do_index_cleanup)
		lazy_cleanup_all_indexes(vacrel);
}

/*
 *	lazy_scan_skip() -- set up range of skippable blocks using visibility map.
 *
 * lazy_scan_heap() calls here every time it needs to set up a new range of
 * blocks to skip via the visibility map.  Caller passes the next block in
 * line.  We return a next_unskippable_block for this range.  When there are
 * no skippable blocks we just return caller's next_block.  The all-visible
 * status of the returned block is set in *next_unskippable_allvis for caller,
 * too.  Block usually won't be all-visible (since it's unskippable), but it
 * can be during aggressive VACUUMs (as well as in certain edge cases).
 *
 * Sets *skipping_current_range to indicate if caller should skip this range.
 * Costs and benefits drive our decision.  Very small ranges won't be skipped.
 *
 * Note: our opinion of which blocks can be skipped can go stale immediately.
 * It's okay if caller "misses" a page whose all-visible or all-frozen marking
 * was concurrently cleared, though.  All that matters is that caller scan all
 * pages whose tuples might contain XIDs < OldestXmin, or MXIDs < OldestMxact.
 * (Actually, non-aggressive VACUUMs can choose to skip all-visible pages with
 * older XIDs/MXIDs.  The vacrel->skippedallvis flag will be set here when the
 * choice to skip such a range is actually made, making everything safe.)
 */
static BlockNumber
lazy_scan_skip(LVRelState *vacrel, Buffer *vmbuffer, BlockNumber next_block,
			   bool *next_unskippable_allvis, bool *skipping_current_range)
{
	BlockNumber rel_pages = vacrel->rel_pages,
				next_unskippable_block = next_block,
				nskippable_blocks = 0;
	bool		skipsallvis = false;

	*next_unskippable_allvis = true;
	while (next_unskippable_block < rel_pages)
	{
		uint8		mapbits = visibilitymap_get_status(vacrel->rel,
													   next_unskippable_block,
													   vmbuffer);

		if ((mapbits & VISIBILITYMAP_ALL_VISIBLE) == 0)
		{
			Assert((mapbits & VISIBILITYMAP_ALL_FROZEN) == 0);
			*next_unskippable_allvis = false;
			break;
		}

		/*
		 * Caller must scan the last page to determine whether it has tuples
		 * (caller must have the opportunity to set vacrel->nonempty_pages).
		 * This rule avoids having lazy_truncate_heap() take access-exclusive
		 * lock on rel to attempt a truncation that fails anyway, just because
		 * there are tuples on the last page (it is likely that there will be
		 * tuples on other nearby pages as well, but those can be skipped).
		 *
		 * Implement this by always treating the last block as unsafe to skip.
		 */
		if (next_unskippable_block == rel_pages - 1)
			break;

		/* DISABLE_PAGE_SKIPPING makes all skipping unsafe */
		if (!vacrel->skipwithvm)
			break;

		/*
		 * Aggressive VACUUM caller can't skip pages just because they are
		 * all-visible.  They may still skip all-frozen pages, which can't
		 * contain XIDs < OldestXmin (XIDs that aren't already frozen by now).
		 */
		if ((mapbits & VISIBILITYMAP_ALL_FROZEN) == 0)
		{
			if (vacrel->aggressive)
				break;

			/*
			 * All-visible block is safe to skip in non-aggressive case.  But
			 * remember that the final range contains such a block for later.
			 */
			skipsallvis = true;
		}

		vacuum_delay_point();
		next_unskippable_block++;
		nskippable_blocks++;
	}

	/*
	 * We only skip a range with at least SKIP_PAGES_THRESHOLD consecutive
	 * pages.  Since we're reading sequentially, the OS should be doing
	 * readahead for us, so there's no gain in skipping a page now and then.
	 * Skipping such a range might even discourage sequential detection.
	 *
	 * This test also enables more frequent relfrozenxid advancement during
	 * non-aggressive VACUUMs.  If the range has any all-visible pages then
	 * skipping makes updating relfrozenxid unsafe, which is a real downside.
	 */
	if (nskippable_blocks < SKIP_PAGES_THRESHOLD)
		*skipping_current_range = false;
	else
	{
		*skipping_current_range = true;
		if (skipsallvis)
			vacrel->skippedallvis = true;
	}

	return next_unskippable_block;
}

/*
 *	lazy_scan_new_or_empty() -- lazy_scan_heap() new/empty page handling.
 *
 * Must call here to handle both new and empty pages before calling
 * lazy_scan_prune or lazy_scan_noprune, since they're not prepared to deal
 * with new or empty pages.
 *
 * It's necessary to consider new pages as a special case, since the rules for
 * maintaining the visibility map and FSM with empty pages are a little
 * different (though new pages can be truncated away during rel truncation).
 *
 * Empty pages are not really a special case -- they're just heap pages that
 * have no allocated tuples (including even LP_UNUSED items).  You might
 * wonder why we need to handle them here all the same.  It's only necessary
 * because of a corner-case involving a hard crash during heap relation
 * extension.  If we ever make relation-extension crash safe, then it should
 * no longer be necessary to deal with empty pages here (or new pages, for
 * that matter).
 *
 * Caller must hold at least a shared lock.  We might need to escalate the
 * lock in that case, so the type of lock caller holds needs to be specified
 * using 'sharelock' argument.
 *
 * Returns false in common case where caller should go on to call
 * lazy_scan_prune (or lazy_scan_noprune).  Otherwise returns true, indicating
 * that lazy_scan_heap is done processing the page, releasing lock on caller's
 * behalf.
 */
static bool
lazy_scan_new_or_empty(LVRelState *vacrel, Buffer buf, BlockNumber blkno,
					   Page page, bool sharelock, Buffer vmbuffer)
{
	Size		freespace;

	if (PageIsNew(page))
	{
		/*
		 * All-zeroes pages can be left over if either a backend extends the
		 * relation by a single page, but crashes before the newly initialized
		 * page has been written out, or when bulk-extending the relation
		 * (which creates a number of empty pages at the tail end of the
		 * relation), and then enters them into the FSM.
		 *
		 * Note we do not enter the page into the visibilitymap. That has the
		 * downside that we repeatedly visit this page in subsequent vacuums,
		 * but otherwise we'll never discover the space on a promoted standby.
		 * The harm of repeated checking ought to normally not be too bad. The
		 * space usually should be used at some point, otherwise there
		 * wouldn't be any regular vacuums.
		 *
		 * Make sure these pages are in the FSM, to ensure they can be reused.
		 * Do that by testing if there's any space recorded for the page. If
		 * not, enter it. We do so after releasing the lock on the heap page,
		 * the FSM is approximate, after all.
		 */
		UnlockReleaseBuffer(buf);

		if (GetRecordedFreeSpace(vacrel->rel, blkno) == 0)
		{
			freespace = BLCKSZ - SizeOfPageHeaderData;

			RecordPageWithFreeSpace(vacrel->rel, blkno, freespace);
		}

		return true;
	}

	if (PageIsEmpty(page))
	{
		/*
		 * It seems likely that caller will always be able to get a cleanup
		 * lock on an empty page.  But don't take any chances -- escalate to
		 * an exclusive lock (still don't need a cleanup lock, though).
		 */
		if (sharelock)
		{
			LockBuffer(buf, BUFFER_LOCK_UNLOCK);
			LockBuffer(buf, BUFFER_LOCK_EXCLUSIVE);

			if (!PageIsEmpty(page))
			{
				/* page isn't new or empty -- keep lock and pin for now */
				return false;
			}
		}
		else
		{
			/* Already have a full cleanup lock (which is more than enough) */
		}

		/*
		 * Unlike new pages, empty pages are always set all-visible and
		 * all-frozen.
		 */
		if (!PageIsAllVisible(page))
		{
			START_CRIT_SECTION();

			/* mark buffer dirty before writing a WAL record */
			MarkBufferDirty(buf);

			/*
			 * It's possible that another backend has extended the heap,
			 * initialized the page, and then failed to WAL-log the page due
			 * to an ERROR.  Since heap extension is not WAL-logged, recovery
			 * might try to replay our record setting the page all-visible and
			 * find that the page isn't initialized, which will cause a PANIC.
			 * To prevent that, check whether the page has been previously
			 * WAL-logged, and if not, do that now.
			 */
			if (RelationNeedsWAL(vacrel->rel) &&
				PageGetLSN(page) == InvalidXLogRecPtr)
				log_newpage_buffer(buf, true);

			PageSetAllVisible(page);
			visibilitymap_set(vacrel->rel, blkno, buf, InvalidXLogRecPtr,
							  vmbuffer, InvalidTransactionId,
							  VISIBILITYMAP_ALL_VISIBLE | VISIBILITYMAP_ALL_FROZEN);
			END_CRIT_SECTION();
		}

		freespace = PageGetHeapFreeSpace(page);
		UnlockReleaseBuffer(buf);
		RecordPageWithFreeSpace(vacrel->rel, blkno, freespace);
		return true;
	}

	/* page isn't new or empty -- keep lock and pin */
	return false;
}

/*
 *	lazy_scan_prune() -- lazy_scan_heap() pruning and freezing.
 *
 * Caller must hold pin and buffer cleanup lock on the buffer.
 *
 * Prior to PostgreSQL 14 there were very rare cases where heap_page_prune()
 * was allowed to disagree with our HeapTupleSatisfiesVacuum() call about
 * whether or not a tuple should be considered DEAD.  This happened when an
 * inserting transaction concurrently aborted (after our heap_page_prune()
 * call, before our HeapTupleSatisfiesVacuum() call).  There was rather a lot
 * of complexity just so we could deal with tuples that were DEAD to VACUUM,
 * but nevertheless were left with storage after pruning.
 *
 * The approach we take now is to restart pruning when the race condition is
 * detected.  This allows heap_page_prune() to prune the tuples inserted by
 * the now-aborted transaction.  This is a little crude, but it guarantees
 * that any items that make it into the dead_items array are simple LP_DEAD
 * line pointers, and that every remaining item with tuple storage is
 * considered as a candidate for freezing.
 */
static void
lazy_scan_prune(LVRelState *vacrel,
				Buffer buf,
				BlockNumber blkno,
				Page page,
				LVPagePruneState *prunestate)
{
	Relation	rel = vacrel->rel;
	OffsetNumber offnum,
				maxoff;
	ItemId		itemid;
	HeapTupleData tuple;
	HTSV_Result res;
	int			tuples_deleted,
				lpdead_items,
				live_tuples,
				recently_dead_tuples;
	int			nnewlpdead;
	int			nfrozen;
	TransactionId NewRelfrozenXid;
	MultiXactId NewRelminMxid;
	OffsetNumber deadoffsets[MaxHeapTuplesPerPage];
	xl_heap_freeze_tuple frozen[MaxHeapTuplesPerPage];

	Assert(BufferGetBlockNumber(buf) == blkno);

	/*
	 * maxoff might be reduced following line pointer array truncation in
	 * heap_page_prune.  That's safe for us to ignore, since the reclaimed
	 * space will continue to look like LP_UNUSED items below.
	 */
	maxoff = PageGetMaxOffsetNumber(page);

retry:

	/* Initialize (or reset) page-level state */
	NewRelfrozenXid = vacrel->NewRelfrozenXid;
	NewRelminMxid = vacrel->NewRelminMxid;
	tuples_deleted = 0;
	lpdead_items = 0;
	live_tuples = 0;
	recently_dead_tuples = 0;

	/*
	 * Prune all HOT-update chains in this page.
	 *
	 * We count tuples removed by the pruning step as tuples_deleted.  Its
	 * final value can be thought of as the number of tuples that have been
	 * deleted from the table.  It should not be confused with lpdead_items;
	 * lpdead_items's final value can be thought of as the number of tuples
	 * that were deleted from indexes.
	 */
	tuples_deleted = heap_page_prune(rel, buf, vacrel->vistest,
									 InvalidTransactionId, 0, &nnewlpdead,
									 &vacrel->offnum);

	/*
	 * Now scan the page to collect LP_DEAD items and check for tuples
	 * requiring freezing among remaining tuples with storage
	 */
	prunestate->hastup = false;
	prunestate->has_lpdead_items = false;
	prunestate->all_visible = true;
	prunestate->all_frozen = true;
	prunestate->visibility_cutoff_xid = InvalidTransactionId;
	nfrozen = 0;

	for (offnum = FirstOffsetNumber;
		 offnum <= maxoff;
		 offnum = OffsetNumberNext(offnum))
	{
		bool		tuple_totally_frozen;

		/*
		 * Set the offset number so that we can display it along with any
		 * error that occurred while processing this tuple.
		 */
		vacrel->offnum = offnum;
		itemid = PageGetItemId(page, offnum);

		if (!ItemIdIsUsed(itemid))
			continue;

		/* Redirect items mustn't be touched */
		if (ItemIdIsRedirected(itemid))
		{
			prunestate->hastup = true;	/* page won't be truncatable */
			continue;
		}

		/*
		 * LP_DEAD items are processed outside of the loop.
		 *
		 * Note that we deliberately don't set hastup=true in the case of an
		 * LP_DEAD item here, which is not how count_nondeletable_pages() does
		 * it -- it only considers pages empty/truncatable when they have no
		 * items at all (except LP_UNUSED items).
		 *
		 * Our assumption is that any LP_DEAD items we encounter here will
		 * become LP_UNUSED inside lazy_vacuum_heap_page() before we actually
		 * call count_nondeletable_pages().  In any case our opinion of
		 * whether or not a page 'hastup' (which is how our caller sets its
		 * vacrel->nonempty_pages value) is inherently race-prone.  It must be
		 * treated as advisory/unreliable, so we might as well be slightly
		 * optimistic.
		 */
		if (ItemIdIsDead(itemid))
		{
			deadoffsets[lpdead_items++] = offnum;
			prunestate->all_visible = false;
			prunestate->has_lpdead_items = true;
			continue;
		}

		Assert(ItemIdIsNormal(itemid));

		ItemPointerSet(&(tuple.t_self), blkno, offnum);
		tuple.t_data = (HeapTupleHeader) PageGetItem(page, itemid);
		tuple.t_len = ItemIdGetLength(itemid);
		tuple.t_tableOid = RelationGetRelid(rel);

		/*
		 * DEAD tuples are almost always pruned into LP_DEAD line pointers by
		 * heap_page_prune(), but it's possible that the tuple state changed
		 * since heap_page_prune() looked.  Handle that here by restarting.
		 * (See comments at the top of function for a full explanation.)
		 */
		res = HeapTupleSatisfiesVacuum(&tuple, vacrel->OldestXmin, buf);

		if (unlikely(res == HEAPTUPLE_DEAD))
			goto retry;

		/*
		 * The criteria for counting a tuple as live in this block need to
		 * match what analyze.c's acquire_sample_rows() does, otherwise VACUUM
		 * and ANALYZE may produce wildly different reltuples values, e.g.
		 * when there are many recently-dead tuples.
		 *
		 * The logic here is a bit simpler than acquire_sample_rows(), as
		 * VACUUM can't run inside a transaction block, which makes some cases
		 * impossible (e.g. in-progress insert from the same transaction).
		 *
		 * We treat LP_DEAD items (which are the closest thing to DEAD tuples
		 * that might be seen here) differently, too: we assume that they'll
		 * become LP_UNUSED before VACUUM finishes.  This difference is only
		 * superficial.  VACUUM effectively agrees with ANALYZE about DEAD
		 * items, in the end.  VACUUM won't remember LP_DEAD items, but only
		 * because they're not supposed to be left behind when it is done.
		 * (Cases where we bypass index vacuuming will violate this optimistic
		 * assumption, but the overall impact of that should be negligible.)
		 */
		switch (res)
		{
			case HEAPTUPLE_LIVE:

				/*
				 * Count it as live.  Not only is this natural, but it's also
				 * what acquire_sample_rows() does.
				 */
				live_tuples++;

				/*
				 * Is the tuple definitely visible to all transactions?
				 *
				 * NB: Like with per-tuple hint bits, we can't set the
				 * PD_ALL_VISIBLE flag if the inserter committed
				 * asynchronously. See SetHintBits for more info. Check that
				 * the tuple is hinted xmin-committed because of that.
				 */
				if (prunestate->all_visible)
				{
					TransactionId xmin;

					if (!HeapTupleHeaderXminCommitted(tuple.t_data))
					{
						prunestate->all_visible = false;
						break;
					}

					/*
					 * The inserter definitely committed. But is it old enough
					 * that everyone sees it as committed?
					 */
					xmin = HeapTupleHeaderGetXmin(tuple.t_data);
					if (!TransactionIdPrecedes(xmin, vacrel->OldestXmin))
					{
						prunestate->all_visible = false;
						break;
					}

					/* Track newest xmin on page. */
					if (TransactionIdFollows(xmin, prunestate->visibility_cutoff_xid))
						prunestate->visibility_cutoff_xid = xmin;
				}
				break;
			case HEAPTUPLE_RECENTLY_DEAD:

				/*
				 * If tuple is recently dead then we must not remove it from
				 * the relation.  (We only remove items that are LP_DEAD from
				 * pruning.)
				 */
				recently_dead_tuples++;
				prunestate->all_visible = false;
				break;
			case HEAPTUPLE_INSERT_IN_PROGRESS:

				/*
				 * We do not count these rows as live, because we expect the
				 * inserting transaction to update the counters at commit, and
				 * we assume that will happen only after we report our
				 * results.  This assumption is a bit shaky, but it is what
				 * acquire_sample_rows() does, so be consistent.
				 */
				prunestate->all_visible = false;
				break;
			case HEAPTUPLE_DELETE_IN_PROGRESS:
				/* This is an expected case during concurrent vacuum */
				prunestate->all_visible = false;

				/*
				 * Count such rows as live.  As above, we assume the deleting
				 * transaction will commit and update the counters after we
				 * report.
				 */
				live_tuples++;
				break;
			default:
				elog(ERROR, "unexpected HeapTupleSatisfiesVacuum result");
				break;
		}

		/*
		 * Non-removable tuple (i.e. tuple with storage).
		 *
		 * Check tuple left behind after pruning to see if needs to be frozen
		 * now.
		 */
		prunestate->hastup = true;	/* page makes rel truncation unsafe */
		if (heap_prepare_freeze_tuple(tuple.t_data,
									  vacrel->relfrozenxid,
									  vacrel->relminmxid,
									  vacrel->FreezeLimit,
									  vacrel->MultiXactCutoff,
									  &frozen[nfrozen], &tuple_totally_frozen,
									  &NewRelfrozenXid, &NewRelminMxid))
		{
			/* Will execute freeze below */
			frozen[nfrozen++].offset = offnum;
		}

		/*
		 * If tuple is not frozen (and not about to become frozen) then caller
		 * had better not go on to set this page's VM bit
		 */
		if (!tuple_totally_frozen)
			prunestate->all_frozen = false;
	}

	vacrel->offnum = InvalidOffsetNumber;

	/*
	 * We have now divided every item on the page into either an LP_DEAD item
	 * that will need to be vacuumed in indexes later, or a LP_NORMAL tuple
	 * that remains and needs to be considered for freezing now (LP_UNUSED and
	 * LP_REDIRECT items also remain, but are of no further interest to us).
	 */
	vacrel->NewRelfrozenXid = NewRelfrozenXid;
	vacrel->NewRelminMxid = NewRelminMxid;

	/*
	 * Consider the need to freeze any items with tuple storage from the page
	 * first (arbitrary)
	 */
	if (nfrozen > 0)
	{
		Assert(prunestate->hastup);

		/*
		 * At least one tuple with storage needs to be frozen -- execute that
		 * now.
		 *
		 * If we need to freeze any tuples we'll mark the buffer dirty, and
		 * write a WAL record recording the changes.  We must log the changes
		 * to be crash-safe against future truncation of CLOG.
		 */
		START_CRIT_SECTION();

		MarkBufferDirty(buf);

		/* execute collected freezes */
		for (int i = 0; i < nfrozen; i++)
		{
			HeapTupleHeader htup;

			itemid = PageGetItemId(page, frozen[i].offset);
			htup = (HeapTupleHeader) PageGetItem(page, itemid);

			heap_execute_freeze_tuple(htup, &frozen[i]);
		}

		/* Now WAL-log freezing if necessary */
		if (RelationNeedsWAL(vacrel->rel))
		{
			XLogRecPtr	recptr;

			recptr = log_heap_freeze(vacrel->rel, buf, vacrel->FreezeLimit,
									 frozen, nfrozen);
			PageSetLSN(page, recptr);
		}

		END_CRIT_SECTION();
	}

	/*
	 * The second pass over the heap can also set visibility map bits, using
	 * the same approach.  This is important when the table frequently has a
	 * few old LP_DEAD items on each page by the time we get to it (typically
	 * because past opportunistic pruning operations freed some non-HOT
	 * tuples).
	 *
	 * VACUUM will call heap_page_is_all_visible() during the second pass over
	 * the heap to determine all_visible and all_frozen for the page -- this
	 * is a specialized version of the logic from this function.  Now that
	 * we've finished pruning and freezing, make sure that we're in total
	 * agreement with heap_page_is_all_visible() using an assertion.
	 */
#ifdef USE_ASSERT_CHECKING
	/* Note that all_frozen value does not matter when !all_visible */
	if (prunestate->all_visible)
	{
		TransactionId cutoff;
		bool		all_frozen;

		if (!heap_page_is_all_visible(vacrel, buf, &cutoff, &all_frozen))
			Assert(false);

		Assert(lpdead_items == 0);
		Assert(prunestate->all_frozen == all_frozen);

		/*
		 * It's possible that we froze tuples and made the page's XID cutoff
		 * (for recovery conflict purposes) FrozenTransactionId.  This is okay
		 * because visibility_cutoff_xid will be logged by our caller in a
		 * moment.
		 */
		Assert(cutoff == FrozenTransactionId ||
			   cutoff == prunestate->visibility_cutoff_xid);
	}
#endif

	/*
	 * Now save details of the LP_DEAD items from the page in vacrel
	 */
	if (lpdead_items > 0)
	{
		VacDeadItems *dead_items = vacrel->dead_items;
		ItemPointerData tmp;

		Assert(!prunestate->all_visible);
		Assert(prunestate->has_lpdead_items);

		vacrel->lpdead_item_pages++;

		ItemPointerSetBlockNumber(&tmp, blkno);

		for (int i = 0; i < lpdead_items; i++)
		{
			ItemPointerSetOffsetNumber(&tmp, deadoffsets[i]);
			dead_items->items[dead_items->num_items++] = tmp;
		}

		Assert(dead_items->num_items <= dead_items->max_items);
		pgstat_progress_update_param(PROGRESS_VACUUM_NUM_DEAD_TUPLES,
									 dead_items->num_items);
	}

	/* Finally, add page-local counts to whole-VACUUM counts */
	vacrel->tuples_deleted += tuples_deleted;
	vacrel->lpdead_items += lpdead_items;
	vacrel->live_tuples += live_tuples;
	vacrel->recently_dead_tuples += recently_dead_tuples;
}

/*
 *	lazy_scan_noprune() -- lazy_scan_prune() without pruning or freezing
 *
 * Caller need only hold a pin and share lock on the buffer, unlike
 * lazy_scan_prune, which requires a full cleanup lock.  While pruning isn't
 * performed here, it's quite possible that an earlier opportunistic pruning
 * operation left LP_DEAD items behind.  We'll at least collect any such items
 * in the dead_items array for removal from indexes.
 *
 * For aggressive VACUUM callers, we may return false to indicate that a full
 * cleanup lock is required for processing by lazy_scan_prune.  This is only
 * necessary when the aggressive VACUUM needs to freeze some tuple XIDs from
 * one or more tuples on the page.  We always return true for non-aggressive
 * callers.
 *
 * See lazy_scan_prune for an explanation of hastup return flag.
 * recordfreespace flag instructs caller on whether or not it should do
 * generic FSM processing for page.
 */
static bool
lazy_scan_noprune(LVRelState *vacrel,
				  Buffer buf,
				  BlockNumber blkno,
				  Page page,
				  bool *hastup,
				  bool *recordfreespace)
{
	OffsetNumber offnum,
				maxoff;
	int			lpdead_items,
				live_tuples,
				recently_dead_tuples,
				missed_dead_tuples;
	HeapTupleHeader tupleheader;
	TransactionId NewRelfrozenXid = vacrel->NewRelfrozenXid;
	MultiXactId NewRelminMxid = vacrel->NewRelminMxid;
	OffsetNumber deadoffsets[MaxHeapTuplesPerPage];

	Assert(BufferGetBlockNumber(buf) == blkno);

	*hastup = false;			/* for now */
	*recordfreespace = false;	/* for now */

	lpdead_items = 0;
	live_tuples = 0;
	recently_dead_tuples = 0;
	missed_dead_tuples = 0;

	maxoff = PageGetMaxOffsetNumber(page);
	for (offnum = FirstOffsetNumber;
		 offnum <= maxoff;
		 offnum = OffsetNumberNext(offnum))
	{
		ItemId		itemid;
		HeapTupleData tuple;

		vacrel->offnum = offnum;
		itemid = PageGetItemId(page, offnum);

		if (!ItemIdIsUsed(itemid))
			continue;

		if (ItemIdIsRedirected(itemid))
		{
			*hastup = true;
			continue;
		}

		if (ItemIdIsDead(itemid))
		{
			/*
			 * Deliberately don't set hastup=true here.  See same point in
			 * lazy_scan_prune for an explanation.
			 */
			deadoffsets[lpdead_items++] = offnum;
			continue;
		}

		*hastup = true;			/* page prevents rel truncation */
		tupleheader = (HeapTupleHeader) PageGetItem(page, itemid);
		if (heap_tuple_would_freeze(tupleheader,
									vacrel->FreezeLimit,
									vacrel->MultiXactCutoff,
									&NewRelfrozenXid, &NewRelminMxid))
		{
			/* Tuple with XID < FreezeLimit (or MXID < MultiXactCutoff) */
			if (vacrel->aggressive)
			{
				/*
				 * Aggressive VACUUMs must always be able to advance rel's
				 * relfrozenxid to a value >= FreezeLimit (and be able to
				 * advance rel's relminmxid to a value >= MultiXactCutoff).
				 * The ongoing aggressive VACUUM won't be able to do that
				 * unless it can freeze an XID (or MXID) from this tuple now.
				 *
				 * The only safe option is to have caller perform processing
				 * of this page using lazy_scan_prune.  Caller might have to
				 * wait a while for a cleanup lock, but it can't be helped.
				 */
				vacrel->offnum = InvalidOffsetNumber;
				return false;
			}

			/*
			 * Non-aggressive VACUUMs are under no obligation to advance
			 * relfrozenxid (even by one XID).  We can be much laxer here.
			 *
			 * Currently we always just accept an older final relfrozenxid
			 * and/or relminmxid value.  We never make caller wait or work a
			 * little harder, even when it likely makes sense to do so.
			 */
		}

		ItemPointerSet(&(tuple.t_self), blkno, offnum);
		tuple.t_data = (HeapTupleHeader) PageGetItem(page, itemid);
		tuple.t_len = ItemIdGetLength(itemid);
		tuple.t_tableOid = RelationGetRelid(vacrel->rel);

		switch (HeapTupleSatisfiesVacuum(&tuple, vacrel->OldestXmin, buf))
		{
			case HEAPTUPLE_DELETE_IN_PROGRESS:
			case HEAPTUPLE_LIVE:

				/*
				 * Count both cases as live, just like lazy_scan_prune
				 */
				live_tuples++;

				break;
			case HEAPTUPLE_DEAD:

				/*
				 * There is some useful work for pruning to do, that won't be
				 * done due to failure to get a cleanup lock.
				 */
				missed_dead_tuples++;
				break;
			case HEAPTUPLE_RECENTLY_DEAD:

				/*
				 * Count in recently_dead_tuples, just like lazy_scan_prune
				 */
				recently_dead_tuples++;
				break;
			case HEAPTUPLE_INSERT_IN_PROGRESS:

				/*
				 * Do not count these rows as live, just like lazy_scan_prune
				 */
				break;
			default:
				elog(ERROR, "unexpected HeapTupleSatisfiesVacuum result");
				break;
		}
	}

	vacrel->offnum = InvalidOffsetNumber;

	/*
	 * By here we know for sure that caller can put off freezing and pruning
	 * this particular page until the next VACUUM.  Remember its details now.
	 * (lazy_scan_prune expects a clean slate, so we have to do this last.)
	 */
	vacrel->NewRelfrozenXid = NewRelfrozenXid;
	vacrel->NewRelminMxid = NewRelminMxid;

	/* Save any LP_DEAD items found on the page in dead_items array */
	if (vacrel->nindexes == 0)
	{
		/* Using one-pass strategy (since table has no indexes) */
		if (lpdead_items > 0)
		{
			/*
			 * Perfunctory handling for the corner case where a single pass
			 * strategy VACUUM cannot get a cleanup lock, and it turns out
			 * that there is one or more LP_DEAD items: just count the LP_DEAD
			 * items as missed_dead_tuples instead. (This is a bit dishonest,
			 * but it beats having to maintain specialized heap vacuuming code
			 * forever, for vanishingly little benefit.)
			 */
			*hastup = true;
			missed_dead_tuples += lpdead_items;
		}

		*recordfreespace = true;
	}
	else if (lpdead_items == 0)
	{
		/*
		 * Won't be vacuuming this page later, so record page's freespace in
		 * the FSM now
		 */
		*recordfreespace = true;
	}
	else
	{
		VacDeadItems *dead_items = vacrel->dead_items;
		ItemPointerData tmp;

		/*
		 * Page has LP_DEAD items, and so any references/TIDs that remain in
		 * indexes will be deleted during index vacuuming (and then marked
		 * LP_UNUSED in the heap)
		 */
		vacrel->lpdead_item_pages++;

		ItemPointerSetBlockNumber(&tmp, blkno);

		for (int i = 0; i < lpdead_items; i++)
		{
			ItemPointerSetOffsetNumber(&tmp, deadoffsets[i]);
			dead_items->items[dead_items->num_items++] = tmp;
		}

		Assert(dead_items->num_items <= dead_items->max_items);
		pgstat_progress_update_param(PROGRESS_VACUUM_NUM_DEAD_TUPLES,
									 dead_items->num_items);

		vacrel->lpdead_items += lpdead_items;

		/*
		 * Assume that we'll go on to vacuum this heap page during final pass
		 * over the heap.  Don't record free space until then.
		 */
		*recordfreespace = false;
	}

	/*
	 * Finally, add relevant page-local counts to whole-VACUUM counts
	 */
	vacrel->live_tuples += live_tuples;
	vacrel->recently_dead_tuples += recently_dead_tuples;
	vacrel->missed_dead_tuples += missed_dead_tuples;
	if (missed_dead_tuples > 0)
		vacrel->missed_dead_pages++;

	/* Caller won't need to call lazy_scan_prune with same page */
	return true;
}

/*
 * Main entry point for index vacuuming and heap vacuuming.
 *
 * Removes items collected in dead_items from table's indexes, then marks the
 * same items LP_UNUSED in the heap.  See the comments above lazy_scan_heap
 * for full details.
 *
 * Also empties dead_items, freeing up space for later TIDs.
 *
 * We may choose to bypass index vacuuming at this point, though only when the
 * ongoing VACUUM operation will definitely only have one index scan/round of
 * index vacuuming.
 */
static void
lazy_vacuum(LVRelState *vacrel)
{
	bool		bypass;

	/* Should not end up here with no indexes */
	Assert(vacrel->nindexes > 0);
	Assert(vacrel->lpdead_item_pages > 0);

	if (!vacrel->do_index_vacuuming)
	{
		Assert(!vacrel->do_index_cleanup);
		vacrel->dead_items->num_items = 0;
		return;
	}

	/*
	 * Consider bypassing index vacuuming (and heap vacuuming) entirely.
	 *
	 * We currently only do this in cases where the number of LP_DEAD items
	 * for the entire VACUUM operation is close to zero.  This avoids sharp
	 * discontinuities in the duration and overhead of successive VACUUM
	 * operations that run against the same table with a fixed workload.
	 * Ideally, successive VACUUM operations will behave as if there are
	 * exactly zero LP_DEAD items in cases where there are close to zero.
	 *
	 * This is likely to be helpful with a table that is continually affected
	 * by UPDATEs that can mostly apply the HOT optimization, but occasionally
	 * have small aberrations that lead to just a few heap pages retaining
	 * only one or two LP_DEAD items.  This is pretty common; even when the
	 * DBA goes out of their way to make UPDATEs use HOT, it is practically
	 * impossible to predict whether HOT will be applied in 100% of cases.
	 * It's far easier to ensure that 99%+ of all UPDATEs against a table use
	 * HOT through careful tuning.
	 */
	bypass = false;
	if (vacrel->consider_bypass_optimization && vacrel->rel_pages > 0)
	{
		BlockNumber threshold;

		Assert(vacrel->num_index_scans == 0);
		Assert(vacrel->lpdead_items == vacrel->dead_items->num_items);
		Assert(vacrel->do_index_vacuuming);
		Assert(vacrel->do_index_cleanup);

		/*
		 * This crossover point at which we'll start to do index vacuuming is
		 * expressed as a percentage of the total number of heap pages in the
		 * table that are known to have at least one LP_DEAD item.  This is
		 * much more important than the total number of LP_DEAD items, since
		 * it's a proxy for the number of heap pages whose visibility map bits
		 * cannot be set on account of bypassing index and heap vacuuming.
		 *
		 * We apply one further precautionary test: the space currently used
		 * to store the TIDs (TIDs that now all point to LP_DEAD items) must
		 * not exceed 32MB.  This limits the risk that we will bypass index
		 * vacuuming again and again until eventually there is a VACUUM whose
		 * dead_items space is not CPU cache resident.
		 *
		 * We don't take any special steps to remember the LP_DEAD items (such
		 * as counting them in our final update to the stats system) when the
		 * optimization is applied.  Though the accounting used in analyze.c's
		 * acquire_sample_rows() will recognize the same LP_DEAD items as dead
		 * rows in its own stats report, that's okay. The discrepancy should
		 * be negligible.  If this optimization is ever expanded to cover more
		 * cases then this may need to be reconsidered.
		 */
		threshold = (double) vacrel->rel_pages * BYPASS_THRESHOLD_PAGES;
		bypass = (vacrel->lpdead_item_pages < threshold &&
				  vacrel->lpdead_items < MAXDEADITEMS(32L * 1024L * 1024L));
	}

	if (bypass)
	{
		/*
		 * There are almost zero TIDs.  Behave as if there were precisely
		 * zero: bypass index vacuuming, but do index cleanup.
		 *
		 * We expect that the ongoing VACUUM operation will finish very
		 * quickly, so there is no point in considering speeding up as a
		 * failsafe against wraparound failure. (Index cleanup is expected to
		 * finish very quickly in cases where there were no ambulkdelete()
		 * calls.)
		 */
		vacrel->do_index_vacuuming = false;
	}
	else if (lazy_vacuum_all_indexes(vacrel))
	{
		/*
		 * We successfully completed a round of index vacuuming.  Do related
		 * heap vacuuming now.
		 */
		lazy_vacuum_heap_rel(vacrel);
	}
	else
	{
		/*
		 * Failsafe case.
		 *
		 * We attempted index vacuuming, but didn't finish a full round/full
		 * index scan.  This happens when relfrozenxid or relminmxid is too
		 * far in the past.
		 *
		 * From this point on the VACUUM operation will do no further index
		 * vacuuming or heap vacuuming.  This VACUUM operation won't end up
		 * back here again.
		 */
		Assert(vacrel->failsafe_active);
	}

	/*
	 * Forget the LP_DEAD items that we just vacuumed (or just decided to not
	 * vacuum)
	 */
	vacrel->dead_items->num_items = 0;
}

/*
 *	lazy_vacuum_all_indexes() -- Main entry for index vacuuming
 *
 * Returns true in the common case when all indexes were successfully
 * vacuumed.  Returns false in rare cases where we determined that the ongoing
 * VACUUM operation is at risk of taking too long to finish, leading to
 * wraparound failure.
 */
static bool
lazy_vacuum_all_indexes(LVRelState *vacrel)
{
	bool		allindexes = true;

	Assert(vacrel->nindexes > 0);
	Assert(vacrel->do_index_vacuuming);
	Assert(vacrel->do_index_cleanup);

	/* Precheck for XID wraparound emergencies */
	if (lazy_check_wraparound_failsafe(vacrel))
	{
		/* Wraparound emergency -- don't even start an index scan */
		return false;
	}

	/* Report that we are now vacuuming indexes */
	pgstat_progress_update_param(PROGRESS_VACUUM_PHASE,
								 PROGRESS_VACUUM_PHASE_VACUUM_INDEX);

	if (!ParallelVacuumIsActive(vacrel))
	{
		for (int idx = 0; idx < vacrel->nindexes; idx++)
		{
			Relation	indrel = vacrel->indrels[idx];
			IndexBulkDeleteResult *istat = vacrel->indstats[idx];

			vacrel->indstats[idx] =
				lazy_vacuum_one_index(indrel, istat, vacrel->old_live_tuples,
									  vacrel);

			if (lazy_check_wraparound_failsafe(vacrel))
			{
				/* Wraparound emergency -- end current index scan */
				allindexes = false;
				break;
			}
		}
	}
	else
	{
		/* Outsource everything to parallel variant */
		parallel_vacuum_bulkdel_all_indexes(vacrel->pvs, vacrel->old_live_tuples,
											vacrel->num_index_scans);

		/*
		 * Do a postcheck to consider applying wraparound failsafe now.  Note
		 * that parallel VACUUM only gets the precheck and this postcheck.
		 */
		if (lazy_check_wraparound_failsafe(vacrel))
			allindexes = false;
	}

	/*
	 * We delete all LP_DEAD items from the first heap pass in all indexes on
	 * each call here (except calls where we choose to do the failsafe). This
	 * makes the next call to lazy_vacuum_heap_rel() safe (except in the event
	 * of the failsafe triggering, which prevents the next call from taking
	 * place).
	 */
	Assert(vacrel->num_index_scans > 0 ||
		   vacrel->dead_items->num_items == vacrel->lpdead_items);
	Assert(allindexes || vacrel->failsafe_active);

	/*
	 * Increase and report the number of index scans.
	 *
	 * We deliberately include the case where we started a round of bulk
	 * deletes that we weren't able to finish due to the failsafe triggering.
	 */
	vacrel->num_index_scans++;
	pgstat_progress_update_param(PROGRESS_VACUUM_NUM_INDEX_VACUUMS,
								 vacrel->num_index_scans);

	return allindexes;
}

/*
 *	lazy_vacuum_heap_rel() -- second pass over the heap for two pass strategy
 *
 * This routine marks LP_DEAD items in vacrel->dead_items array as LP_UNUSED.
 * Pages that never had lazy_scan_prune record LP_DEAD items are not visited
 * at all.
 *
 * We may also be able to truncate the line pointer array of the heap pages we
 * visit.  If there is a contiguous group of LP_UNUSED items at the end of the
 * array, it can be reclaimed as free space.  These LP_UNUSED items usually
 * start out as LP_DEAD items recorded by lazy_scan_prune (we set items from
 * each page to LP_UNUSED, and then consider if it's possible to truncate the
 * page's line pointer array).
 *
 * Note: the reason for doing this as a second pass is we cannot remove the
 * tuples until we've removed their index entries, and we want to process
 * index entry removal in batches as large as possible.
 */
static void
lazy_vacuum_heap_rel(LVRelState *vacrel)
{
	int			index;
	BlockNumber vacuumed_pages;
	Buffer		vmbuffer = InvalidBuffer;
	LVSavedErrInfo saved_err_info;

	Assert(vacrel->do_index_vacuuming);
	Assert(vacrel->do_index_cleanup);
	Assert(vacrel->num_index_scans > 0);

	/* Report that we are now vacuuming the heap */
	pgstat_progress_update_param(PROGRESS_VACUUM_PHASE,
								 PROGRESS_VACUUM_PHASE_VACUUM_HEAP);

	/* Update error traceback information */
	update_vacuum_error_info(vacrel, &saved_err_info,
							 VACUUM_ERRCB_PHASE_VACUUM_HEAP,
							 InvalidBlockNumber, InvalidOffsetNumber);

	vacuumed_pages = 0;

	index = 0;
	while (index < vacrel->dead_items->num_items)
	{
		BlockNumber tblk;
		Buffer		buf;
		Page		page;
		Size		freespace;

		vacuum_delay_point();

		tblk = ItemPointerGetBlockNumber(&vacrel->dead_items->items[index]);
		vacrel->blkno = tblk;
		buf = ReadBufferExtended(vacrel->rel, MAIN_FORKNUM, tblk, RBM_NORMAL,
								 vacrel->bstrategy);
		LockBuffer(buf, BUFFER_LOCK_EXCLUSIVE);
		index = lazy_vacuum_heap_page(vacrel, tblk, buf, index, &vmbuffer);

		/* Now that we've vacuumed the page, record its available space */
		page = BufferGetPage(buf);
		freespace = PageGetHeapFreeSpace(page);

		UnlockReleaseBuffer(buf);
		RecordPageWithFreeSpace(vacrel->rel, tblk, freespace);
		vacuumed_pages++;
	}

	/* Clear the block number information */
	vacrel->blkno = InvalidBlockNumber;

	if (BufferIsValid(vmbuffer))
	{
		ReleaseBuffer(vmbuffer);
		vmbuffer = InvalidBuffer;
	}

	/*
	 * We set all LP_DEAD items from the first heap pass to LP_UNUSED during
	 * the second heap pass.  No more, no less.
	 */
	Assert(index > 0);
	Assert(vacrel->num_index_scans > 1 ||
		   (index == vacrel->lpdead_items &&
			vacuumed_pages == vacrel->lpdead_item_pages));

	ereport(DEBUG2,
			(errmsg("table \"%s\": removed %lld dead item identifiers in %u pages",
					vacrel->relname, (long long) index, vacuumed_pages)));

	/* Revert to the previous phase information for error traceback */
	restore_vacuum_error_info(vacrel, &saved_err_info);
}

/*
 *	lazy_vacuum_heap_page() -- free page's LP_DEAD items listed in the
 *						  vacrel->dead_items array.
 *
 * Caller must have an exclusive buffer lock on the buffer (though a full
 * cleanup lock is also acceptable).
 *
 * index is an offset into the vacrel->dead_items array for the first listed
 * LP_DEAD item on the page.  The return value is the first index immediately
 * after all LP_DEAD items for the same page in the array.
 */
static int
lazy_vacuum_heap_page(LVRelState *vacrel, BlockNumber blkno, Buffer buffer,
					  int index, Buffer *vmbuffer)
{
	VacDeadItems *dead_items = vacrel->dead_items;
	Page		page = BufferGetPage(buffer);
	OffsetNumber unused[MaxHeapTuplesPerPage];
	int			uncnt = 0;
	TransactionId visibility_cutoff_xid;
	bool		all_frozen;
	LVSavedErrInfo saved_err_info;

	Assert(vacrel->nindexes == 0 || vacrel->do_index_vacuuming);

	pgstat_progress_update_param(PROGRESS_VACUUM_HEAP_BLKS_VACUUMED, blkno);

	/* Update error traceback information */
	update_vacuum_error_info(vacrel, &saved_err_info,
							 VACUUM_ERRCB_PHASE_VACUUM_HEAP, blkno,
							 InvalidOffsetNumber);

	START_CRIT_SECTION();

	for (; index < dead_items->num_items; index++)
	{
		BlockNumber tblk;
		OffsetNumber toff;
		ItemId		itemid;

		tblk = ItemPointerGetBlockNumber(&dead_items->items[index]);
		if (tblk != blkno)
			break;				/* past end of tuples for this block */
		toff = ItemPointerGetOffsetNumber(&dead_items->items[index]);
		itemid = PageGetItemId(page, toff);

		Assert(ItemIdIsDead(itemid) && !ItemIdHasStorage(itemid));
		ItemIdSetUnused(itemid);
		unused[uncnt++] = toff;
	}

	Assert(uncnt > 0);

	/* Attempt to truncate line pointer array now */
	PageTruncateLinePointerArray(page);

	/*
	 * Mark buffer dirty before we write WAL.
	 */
	MarkBufferDirty(buffer);

	/* XLOG stuff */
	if (RelationNeedsWAL(vacrel->rel))
	{
		xl_heap_vacuum xlrec;
		XLogRecPtr	recptr;

		xlrec.nunused = uncnt;

		XLogBeginInsert();
		XLogRegisterData((char *) &xlrec, SizeOfHeapVacuum);

		XLogRegisterBuffer(0, buffer, REGBUF_STANDARD);
		XLogRegisterBufData(0, (char *) unused, uncnt * sizeof(OffsetNumber));

		recptr = XLogInsert(RM_HEAP2_ID, XLOG_HEAP2_VACUUM);

		PageSetLSN(page, recptr);
	}

	/*
	 * End critical section, so we safely can do visibility tests (which
	 * possibly need to perform IO and allocate memory!). If we crash now the
	 * page (including the corresponding vm bit) might not be marked all
	 * visible, but that's fine. A later vacuum will fix that.
	 */
	END_CRIT_SECTION();

	/*
	 * Now that we have removed the LD_DEAD items from the page, once again
	 * check if the page has become all-visible.  The page is already marked
	 * dirty, exclusively locked, and, if needed, a full page image has been
	 * emitted.
	 */
	if (heap_page_is_all_visible(vacrel, buffer, &visibility_cutoff_xid,
								 &all_frozen))
		PageSetAllVisible(page);

	/*
	 * All the changes to the heap page have been done. If the all-visible
	 * flag is now set, also set the VM all-visible bit (and, if possible, the
	 * all-frozen bit) unless this has already been done previously.
	 */
	if (PageIsAllVisible(page))
	{
		uint8		flags = 0;
		uint8		vm_status = visibilitymap_get_status(vacrel->rel,
														 blkno, vmbuffer);

		/* Set the VM all-frozen bit to flag, if needed */
		if ((vm_status & VISIBILITYMAP_ALL_VISIBLE) == 0)
			flags |= VISIBILITYMAP_ALL_VISIBLE;
		if ((vm_status & VISIBILITYMAP_ALL_FROZEN) == 0 && all_frozen)
			flags |= VISIBILITYMAP_ALL_FROZEN;

		Assert(BufferIsValid(*vmbuffer));
		if (flags != 0)
			visibilitymap_set(vacrel->rel, blkno, buffer, InvalidXLogRecPtr,
							  *vmbuffer, visibility_cutoff_xid, flags);
	}

	/* Revert to the previous phase information for error traceback */
	restore_vacuum_error_info(vacrel, &saved_err_info);
	return index;
}

/*
 * Trigger the failsafe to avoid wraparound failure when vacrel table has a
 * relfrozenxid and/or relminmxid that is dangerously far in the past.
 * Triggering the failsafe makes the ongoing VACUUM bypass any further index
 * vacuuming and heap vacuuming.  Truncating the heap is also bypassed.
 *
 * Any remaining work (work that VACUUM cannot just bypass) is typically sped
 * up when the failsafe triggers.  VACUUM stops applying any cost-based delay
 * that it started out with.
 *
 * Returns true when failsafe has been triggered.
 */
static bool
lazy_check_wraparound_failsafe(LVRelState *vacrel)
{
	Assert(TransactionIdIsNormal(vacrel->relfrozenxid));
	Assert(MultiXactIdIsValid(vacrel->relminmxid));

	/* Don't warn more than once per VACUUM */
	if (vacrel->failsafe_active)
		return true;

	if (unlikely(vacuum_xid_failsafe_check(vacrel->relfrozenxid,
										   vacrel->relminmxid)))
	{
		vacrel->failsafe_active = true;

		/* Disable index vacuuming, index cleanup, and heap rel truncation */
		vacrel->do_index_vacuuming = false;
		vacrel->do_index_cleanup = false;
		vacrel->do_rel_truncate = false;

		ereport(WARNING,
				(errmsg("bypassing nonessential maintenance of table \"%s.%s.%s\" as a failsafe after %d index scans",
						get_database_name(MyDatabaseId),
						vacrel->relnamespace,
						vacrel->relname,
						vacrel->num_index_scans),
				 errdetail("The table's relfrozenxid or relminmxid is too far in the past."),
				 errhint("Consider increasing configuration parameter \"maintenance_work_mem\" or \"autovacuum_work_mem\".\n"
						 "You might also need to consider other ways for VACUUM to keep up with the allocation of transaction IDs.")));

		/* Stop applying cost limits from this point on */
		VacuumCostActive = false;
		VacuumCostBalance = 0;

		return true;
	}

	return false;
}

/*
 *	lazy_cleanup_all_indexes() -- cleanup all indexes of relation.
 */
static void
lazy_cleanup_all_indexes(LVRelState *vacrel)
{
	double		reltuples = vacrel->new_rel_tuples;
	bool		estimated_count = vacrel->scanned_pages < vacrel->rel_pages;

	Assert(vacrel->do_index_cleanup);
	Assert(vacrel->nindexes > 0);

	/* Report that we are now cleaning up indexes */
	pgstat_progress_update_param(PROGRESS_VACUUM_PHASE,
								 PROGRESS_VACUUM_PHASE_INDEX_CLEANUP);

	if (!ParallelVacuumIsActive(vacrel))
	{
		for (int idx = 0; idx < vacrel->nindexes; idx++)
		{
			Relation	indrel = vacrel->indrels[idx];
			IndexBulkDeleteResult *istat = vacrel->indstats[idx];

			vacrel->indstats[idx] =
				lazy_cleanup_one_index(indrel, istat, reltuples,
									   estimated_count, vacrel);
		}
	}
	else
	{
		/* Outsource everything to parallel variant */
		parallel_vacuum_cleanup_all_indexes(vacrel->pvs, reltuples,
											vacrel->num_index_scans,
											estimated_count);
	}
}

/*
 *	lazy_vacuum_one_index() -- vacuum index relation.
 *
 *		Delete all the index tuples containing a TID collected in
 *		vacrel->dead_items array.  Also update running statistics.
 *		Exact details depend on index AM's ambulkdelete routine.
 *
 *		reltuples is the number of heap tuples to be passed to the
 *		bulkdelete callback.  It's always assumed to be estimated.
 *		See indexam.sgml for more info.
 *
 * Returns bulk delete stats derived from input stats
 */
static IndexBulkDeleteResult *
lazy_vacuum_one_index(Relation indrel, IndexBulkDeleteResult *istat,
					  double reltuples, LVRelState *vacrel)
{
	IndexVacuumInfo ivinfo;
	LVSavedErrInfo saved_err_info;

	ivinfo.index = indrel;
	ivinfo.analyze_only = false;
	ivinfo.report_progress = false;
	ivinfo.estimated_count = true;
	ivinfo.message_level = DEBUG2;
	ivinfo.num_heap_tuples = reltuples;
	ivinfo.strategy = vacrel->bstrategy;

	/*
	 * Update error traceback information.
	 *
	 * The index name is saved during this phase and restored immediately
	 * after this phase.  See vacuum_error_callback.
	 */
	Assert(vacrel->indname == NULL);
	vacrel->indname = pstrdup(RelationGetRelationName(indrel));
	update_vacuum_error_info(vacrel, &saved_err_info,
							 VACUUM_ERRCB_PHASE_VACUUM_INDEX,
							 InvalidBlockNumber, InvalidOffsetNumber);

	/* Do bulk deletion */
	istat = vac_bulkdel_one_index(&ivinfo, istat, (void *) vacrel->dead_items);

	/* Revert to the previous phase information for error traceback */
	restore_vacuum_error_info(vacrel, &saved_err_info);
	pfree(vacrel->indname);
	vacrel->indname = NULL;

	return istat;
}

/*
 *	lazy_cleanup_one_index() -- do post-vacuum cleanup for index relation.
 *
 *		Calls index AM's amvacuumcleanup routine.  reltuples is the number
 *		of heap tuples and estimated_count is true if reltuples is an
 *		estimated value.  See indexam.sgml for more info.
 *
 * Returns bulk delete stats derived from input stats
 */
static IndexBulkDeleteResult *
lazy_cleanup_one_index(Relation indrel, IndexBulkDeleteResult *istat,
					   double reltuples, bool estimated_count,
					   LVRelState *vacrel)
{
	IndexVacuumInfo ivinfo;
	LVSavedErrInfo saved_err_info;

	ivinfo.index = indrel;
	ivinfo.analyze_only = false;
	ivinfo.report_progress = false;
	ivinfo.estimated_count = estimated_count;
	ivinfo.message_level = DEBUG2;

	ivinfo.num_heap_tuples = reltuples;
	ivinfo.strategy = vacrel->bstrategy;

	/*
	 * Update error traceback information.
	 *
	 * The index name is saved during this phase and restored immediately
	 * after this phase.  See vacuum_error_callback.
	 */
	Assert(vacrel->indname == NULL);
	vacrel->indname = pstrdup(RelationGetRelationName(indrel));
	update_vacuum_error_info(vacrel, &saved_err_info,
							 VACUUM_ERRCB_PHASE_INDEX_CLEANUP,
							 InvalidBlockNumber, InvalidOffsetNumber);

	istat = vac_cleanup_one_index(&ivinfo, istat);

	/* Revert to the previous phase information for error traceback */
	restore_vacuum_error_info(vacrel, &saved_err_info);
	pfree(vacrel->indname);
	vacrel->indname = NULL;

	return istat;
}

/*
 * should_attempt_truncation - should we attempt to truncate the heap?
 *
 * Don't even think about it unless we have a shot at releasing a goodly
 * number of pages.  Otherwise, the time taken isn't worth it, mainly because
 * an AccessExclusive lock must be replayed on any hot standby, where it can
 * be particularly disruptive.
 *
 * Also don't attempt it if wraparound failsafe is in effect.  The entire
 * system might be refusing to allocate new XIDs at this point.  The system
 * definitely won't return to normal unless and until VACUUM actually advances
 * the oldest relfrozenxid -- which hasn't happened for target rel just yet.
 * If lazy_truncate_heap attempted to acquire an AccessExclusiveLock to
 * truncate the table under these circumstances, an XID exhaustion error might
 * make it impossible for VACUUM to fix the underlying XID exhaustion problem.
 * There is very little chance of truncation working out when the failsafe is
 * in effect in any case.  lazy_scan_prune makes the optimistic assumption
 * that any LP_DEAD items it encounters will always be LP_UNUSED by the time
 * we're called.
 *
 * Also don't attempt it if we are doing early pruning/vacuuming, because a
 * scan which cannot find a truncated heap page cannot determine that the
 * snapshot is too old to read that page.
 */
static bool
should_attempt_truncation(LVRelState *vacrel)
{
	BlockNumber possibly_freeable;

	if (!vacrel->do_rel_truncate || vacrel->failsafe_active ||
		old_snapshot_threshold >= 0)
		return false;

	possibly_freeable = vacrel->rel_pages - vacrel->nonempty_pages;
	if (possibly_freeable > 0 &&
		(possibly_freeable >= REL_TRUNCATE_MINIMUM ||
		 possibly_freeable >= vacrel->rel_pages / REL_TRUNCATE_FRACTION))
		return true;

	return false;
}

/*
 * lazy_truncate_heap - try to truncate off any empty pages at the end
 */
static void
lazy_truncate_heap(LVRelState *vacrel)
{
	BlockNumber orig_rel_pages = vacrel->rel_pages;
	BlockNumber new_rel_pages;
	bool		lock_waiter_detected;
	int			lock_retry;

	/* Report that we are now truncating */
	pgstat_progress_update_param(PROGRESS_VACUUM_PHASE,
								 PROGRESS_VACUUM_PHASE_TRUNCATE);

	/* Update error traceback information one last time */
	update_vacuum_error_info(vacrel, NULL, VACUUM_ERRCB_PHASE_TRUNCATE,
							 vacrel->nonempty_pages, InvalidOffsetNumber);

	/*
	 * Loop until no more truncating can be done.
	 */
	do
	{
		/*
		 * We need full exclusive lock on the relation in order to do
		 * truncation. If we can't get it, give up rather than waiting --- we
		 * don't want to block other backends, and we don't want to deadlock
		 * (which is quite possible considering we already hold a lower-grade
		 * lock).
		 */
		lock_waiter_detected = false;
		lock_retry = 0;
		while (true)
		{
			if (ConditionalLockRelation(vacrel->rel, AccessExclusiveLock))
				break;

			/*
			 * Check for interrupts while trying to (re-)acquire the exclusive
			 * lock.
			 */
			CHECK_FOR_INTERRUPTS();

			if (++lock_retry > (VACUUM_TRUNCATE_LOCK_TIMEOUT /
								VACUUM_TRUNCATE_LOCK_WAIT_INTERVAL))
			{
				/*
				 * We failed to establish the lock in the specified number of
				 * retries. This means we give up truncating.
				 */
				ereport(vacrel->verbose ? INFO : DEBUG2,
						(errmsg("\"%s\": stopping truncate due to conflicting lock request",
								vacrel->relname)));
				return;
			}

			(void) WaitLatch(MyLatch,
							 WL_LATCH_SET | WL_TIMEOUT | WL_EXIT_ON_PM_DEATH,
							 VACUUM_TRUNCATE_LOCK_WAIT_INTERVAL,
							 WAIT_EVENT_VACUUM_TRUNCATE);
			ResetLatch(MyLatch);
		}

		/*
		 * Now that we have exclusive lock, look to see if the rel has grown
		 * whilst we were vacuuming with non-exclusive lock.  If so, give up;
		 * the newly added pages presumably contain non-deletable tuples.
		 */
		new_rel_pages = RelationGetNumberOfBlocks(vacrel->rel);
		if (new_rel_pages != orig_rel_pages)
		{
			/*
			 * Note: we intentionally don't update vacrel->rel_pages with the
			 * new rel size here.  If we did, it would amount to assuming that
			 * the new pages are empty, which is unlikely. Leaving the numbers
			 * alone amounts to assuming that the new pages have the same
			 * tuple density as existing ones, which is less unlikely.
			 */
			UnlockRelation(vacrel->rel, AccessExclusiveLock);
			return;
		}

		/*
		 * Scan backwards from the end to verify that the end pages actually
		 * contain no tuples.  This is *necessary*, not optional, because
		 * other backends could have added tuples to these pages whilst we
		 * were vacuuming.
		 */
		new_rel_pages = count_nondeletable_pages(vacrel, &lock_waiter_detected);
		vacrel->blkno = new_rel_pages;

		if (new_rel_pages >= orig_rel_pages)
		{
			/* can't do anything after all */
			UnlockRelation(vacrel->rel, AccessExclusiveLock);
			return;
		}

		/*
		 * Okay to truncate.
		 */
		RelationTruncate(vacrel->rel, new_rel_pages);

		/*
		 * We can release the exclusive lock as soon as we have truncated.
		 * Other backends can't safely access the relation until they have
		 * processed the smgr invalidation that smgrtruncate sent out ... but
		 * that should happen as part of standard invalidation processing once
		 * they acquire lock on the relation.
		 */
		UnlockRelation(vacrel->rel, AccessExclusiveLock);

		/*
		 * Update statistics.  Here, it *is* correct to adjust rel_pages
		 * without also touching reltuples, since the tuple count wasn't
		 * changed by the truncation.
		 */
		vacrel->removed_pages += orig_rel_pages - new_rel_pages;
		vacrel->rel_pages = new_rel_pages;

		ereport(vacrel->verbose ? INFO : DEBUG2,
				(errmsg("table \"%s\": truncated %u to %u pages",
						vacrel->relname,
						orig_rel_pages, new_rel_pages)));
		orig_rel_pages = new_rel_pages;
	} while (new_rel_pages > vacrel->nonempty_pages && lock_waiter_detected);
}

/*
 * Rescan end pages to verify that they are (still) empty of tuples.
 *
 * Returns number of nondeletable pages (last nonempty page + 1).
 */
static BlockNumber
count_nondeletable_pages(LVRelState *vacrel, bool *lock_waiter_detected)
{
	BlockNumber blkno;
	BlockNumber prefetchedUntil;
	instr_time	starttime;

	/* Initialize the starttime if we check for conflicting lock requests */
	INSTR_TIME_SET_CURRENT(starttime);

	/*
	 * Start checking blocks at what we believe relation end to be and move
	 * backwards.  (Strange coding of loop control is needed because blkno is
	 * unsigned.)  To make the scan faster, we prefetch a few blocks at a time
	 * in forward direction, so that OS-level readahead can kick in.
	 */
	blkno = vacrel->rel_pages;
	StaticAssertStmt((PREFETCH_SIZE & (PREFETCH_SIZE - 1)) == 0,
					 "prefetch size must be power of 2");
	prefetchedUntil = InvalidBlockNumber;
	while (blkno > vacrel->nonempty_pages)
	{
		Buffer		buf;
		Page		page;
		OffsetNumber offnum,
					maxoff;
		bool		hastup;

		/*
		 * Check if another process requests a lock on our relation. We are
		 * holding an AccessExclusiveLock here, so they will be waiting. We
		 * only do this once per VACUUM_TRUNCATE_LOCK_CHECK_INTERVAL, and we
		 * only check if that interval has elapsed once every 32 blocks to
		 * keep the number of system calls and actual shared lock table
		 * lookups to a minimum.
		 */
		if ((blkno % 32) == 0)
		{
			instr_time	currenttime;
			instr_time	elapsed;

			INSTR_TIME_SET_CURRENT(currenttime);
			elapsed = currenttime;
			INSTR_TIME_SUBTRACT(elapsed, starttime);
			if ((INSTR_TIME_GET_MICROSEC(elapsed) / 1000)
				>= VACUUM_TRUNCATE_LOCK_CHECK_INTERVAL)
			{
				if (LockHasWaitersRelation(vacrel->rel, AccessExclusiveLock))
				{
					ereport(vacrel->verbose ? INFO : DEBUG2,
							(errmsg("table \"%s\": suspending truncate due to conflicting lock request",
									vacrel->relname)));

					*lock_waiter_detected = true;
					return blkno;
				}
				starttime = currenttime;
			}
		}

		/*
		 * We don't insert a vacuum delay point here, because we have an
		 * exclusive lock on the table which we want to hold for as short a
		 * time as possible.  We still need to check for interrupts however.
		 */
		CHECK_FOR_INTERRUPTS();

		blkno--;

		/* If we haven't prefetched this lot yet, do so now. */
		if (prefetchedUntil > blkno)
		{
			BlockNumber prefetchStart;
			BlockNumber pblkno;

			prefetchStart = blkno & ~(PREFETCH_SIZE - 1);
			for (pblkno = prefetchStart; pblkno <= blkno; pblkno++)
			{
				PrefetchBuffer(vacrel->rel, MAIN_FORKNUM, pblkno);
				CHECK_FOR_INTERRUPTS();
			}
			prefetchedUntil = prefetchStart;
		}

		buf = ReadBufferExtended(vacrel->rel, MAIN_FORKNUM, blkno, RBM_NORMAL,
								 vacrel->bstrategy);

		/* In this phase we only need shared access to the buffer */
		LockBuffer(buf, BUFFER_LOCK_SHARE);

		page = BufferGetPage(buf);

		if (PageIsNew(page) || PageIsEmpty(page))
		{
			UnlockReleaseBuffer(buf);
			continue;
		}

		hastup = false;
		maxoff = PageGetMaxOffsetNumber(page);
		for (offnum = FirstOffsetNumber;
			 offnum <= maxoff;
			 offnum = OffsetNumberNext(offnum))
		{
			ItemId		itemid;

			itemid = PageGetItemId(page, offnum);

			/*
			 * Note: any non-unused item should be taken as a reason to keep
			 * this page.  Even an LP_DEAD item makes truncation unsafe, since
			 * we must not have cleaned out its index entries.
			 */
			if (ItemIdIsUsed(itemid))
			{
				hastup = true;
				break;			/* can stop scanning */
			}
		}						/* scan along page */

		UnlockReleaseBuffer(buf);

		/* Done scanning if we found a tuple here */
		if (hastup)
			return blkno + 1;
	}

	/*
	 * If we fall out of the loop, all the previously-thought-to-be-empty
	 * pages still are; we need not bother to look at the last known-nonempty
	 * page.
	 */
	return vacrel->nonempty_pages;
}

/*
 * Returns the number of dead TIDs that VACUUM should allocate space to
 * store, given a heap rel of size vacrel->rel_pages, and given current
 * maintenance_work_mem setting (or current autovacuum_work_mem setting,
 * when applicable).
 *
 * See the comments at the head of this file for rationale.
 */
static int
dead_items_max_items(LVRelState *vacrel)
{
	int64		max_items;
	int			vac_work_mem = IsAutoVacuumWorkerProcess() &&
	autovacuum_work_mem != -1 ?
	autovacuum_work_mem : maintenance_work_mem;

	if (vacrel->nindexes > 0)
	{
		BlockNumber rel_pages = vacrel->rel_pages;

		max_items = MAXDEADITEMS(vac_work_mem * 1024L);
		max_items = Min(max_items, INT_MAX);
		max_items = Min(max_items, MAXDEADITEMS(MaxAllocSize));

		/* curious coding here to ensure the multiplication can't overflow */
		if ((BlockNumber) (max_items / MaxHeapTuplesPerPage) > rel_pages)
			max_items = rel_pages * MaxHeapTuplesPerPage;

		/* stay sane if small maintenance_work_mem */
		max_items = Max(max_items, MaxHeapTuplesPerPage);
	}
	else
	{
		/* One-pass case only stores a single heap page's TIDs at a time */
		max_items = MaxHeapTuplesPerPage;
	}

	return (int) max_items;
}

/*
 * Allocate dead_items (either using palloc, or in dynamic shared memory).
 * Sets dead_items in vacrel for caller.
 *
 * Also handles parallel initialization as part of allocating dead_items in
 * DSM when required.
 */
static void
dead_items_alloc(LVRelState *vacrel, int nworkers)
{
	VacDeadItems *dead_items;
	int			max_items;

	max_items = dead_items_max_items(vacrel);
	Assert(max_items >= MaxHeapTuplesPerPage);

	/*
	 * Initialize state for a parallel vacuum.  As of now, only one worker can
	 * be used for an index, so we invoke parallelism only if there are at
	 * least two indexes on a table.
	 */
	if (nworkers >= 0 && vacrel->nindexes > 1 && vacrel->do_index_vacuuming)
	{
		/*
		 * Since parallel workers cannot access data in temporary tables, we
		 * can't perform parallel vacuum on them.
		 */
		if (RelationUsesLocalBuffers(vacrel->rel))
		{
			/*
			 * Give warning only if the user explicitly tries to perform a
			 * parallel vacuum on the temporary table.
			 */
			if (nworkers > 0)
				ereport(WARNING,
						(errmsg("disabling parallel option of vacuum on \"%s\" --- cannot vacuum temporary tables in parallel",
								vacrel->relname)));
		}
		else
			vacrel->pvs = parallel_vacuum_init(vacrel->rel, vacrel->indrels,
											   vacrel->nindexes, nworkers,
											   max_items,
											   vacrel->verbose ? INFO : DEBUG2,
											   vacrel->bstrategy);

		/* If parallel mode started, dead_items space is allocated in DSM */
		if (ParallelVacuumIsActive(vacrel))
		{
			vacrel->dead_items = parallel_vacuum_get_dead_items(vacrel->pvs);
			return;
		}
	}

	/* Serial VACUUM case */
	dead_items = (VacDeadItems *) palloc(vac_max_items_to_alloc_size(max_items));
	dead_items->max_items = max_items;
	dead_items->num_items = 0;

	vacrel->dead_items = dead_items;
}

/*
 * Perform cleanup for resources allocated in dead_items_alloc
 */
static void
dead_items_cleanup(LVRelState *vacrel)
{
	if (!ParallelVacuumIsActive(vacrel))
	{
		/* Don't bother with pfree here */
		return;
	}

	/* End parallel mode */
	parallel_vacuum_end(vacrel->pvs, vacrel->indstats);
	vacrel->pvs = NULL;
}

/*
 * Check if every tuple in the given page is visible to all current and future
 * transactions. Also return the visibility_cutoff_xid which is the highest
 * xmin amongst the visible tuples.  Set *all_frozen to true if every tuple
 * on this page is frozen.
 *
 * This is a stripped down version of lazy_scan_prune().  If you change
 * anything here, make sure that everything stays in sync.  Note that an
 * assertion calls us to verify that everybody still agrees.  Be sure to avoid
 * introducing new side-effects here.
 */
static bool
heap_page_is_all_visible(LVRelState *vacrel, Buffer buf,
						 TransactionId *visibility_cutoff_xid,
						 bool *all_frozen)
{
	Page		page = BufferGetPage(buf);
	BlockNumber blockno = BufferGetBlockNumber(buf);
	OffsetNumber offnum,
				maxoff;
	bool		all_visible = true;

	*visibility_cutoff_xid = InvalidTransactionId;
	*all_frozen = true;

	maxoff = PageGetMaxOffsetNumber(page);
	for (offnum = FirstOffsetNumber;
		 offnum <= maxoff && all_visible;
		 offnum = OffsetNumberNext(offnum))
	{
		ItemId		itemid;
		HeapTupleData tuple;

		/*
		 * Set the offset number so that we can display it along with any
		 * error that occurred while processing this tuple.
		 */
		vacrel->offnum = offnum;
		itemid = PageGetItemId(page, offnum);

		/* Unused or redirect line pointers are of no interest */
		if (!ItemIdIsUsed(itemid) || ItemIdIsRedirected(itemid))
			continue;

		ItemPointerSet(&(tuple.t_self), blockno, offnum);

		/*
		 * Dead line pointers can have index pointers pointing to them. So
		 * they can't be treated as visible
		 */
		if (ItemIdIsDead(itemid))
		{
			all_visible = false;
			*all_frozen = false;
			break;
		}

		Assert(ItemIdIsNormal(itemid));

		tuple.t_data = (HeapTupleHeader) PageGetItem(page, itemid);
		tuple.t_len = ItemIdGetLength(itemid);
		tuple.t_tableOid = RelationGetRelid(vacrel->rel);

		switch (HeapTupleSatisfiesVacuum(&tuple, vacrel->OldestXmin, buf))
		{
			case HEAPTUPLE_LIVE:
				{
					TransactionId xmin;

					/* Check comments in lazy_scan_prune. */
					if (!HeapTupleHeaderXminCommitted(tuple.t_data))
					{
						all_visible = false;
						*all_frozen = false;
						break;
					}

					/*
					 * The inserter definitely committed. But is it old enough
					 * that everyone sees it as committed?
					 */
					xmin = HeapTupleHeaderGetXmin(tuple.t_data);
					if (!TransactionIdPrecedes(xmin, vacrel->OldestXmin))
					{
						all_visible = false;
						*all_frozen = false;
						break;
					}

					/* Track newest xmin on page. */
					if (TransactionIdFollows(xmin, *visibility_cutoff_xid))
						*visibility_cutoff_xid = xmin;

					/* Check whether this tuple is already frozen or not */
					if (all_visible && *all_frozen &&
						heap_tuple_needs_eventual_freeze(tuple.t_data))
						*all_frozen = false;
				}
				break;

			case HEAPTUPLE_DEAD:
			case HEAPTUPLE_RECENTLY_DEAD:
			case HEAPTUPLE_INSERT_IN_PROGRESS:
			case HEAPTUPLE_DELETE_IN_PROGRESS:
				{
					all_visible = false;
					*all_frozen = false;
					break;
				}
			default:
				elog(ERROR, "unexpected HeapTupleSatisfiesVacuum result");
				break;
		}
	}							/* scan along page */

	/* Clear the offset information once we have processed the given page. */
	vacrel->offnum = InvalidOffsetNumber;

	return all_visible;
}

/*
 * Update index statistics in pg_class if the statistics are accurate.
 */
static void
update_relstats_all_indexes(LVRelState *vacrel)
{
	Relation   *indrels = vacrel->indrels;
	int			nindexes = vacrel->nindexes;
	IndexBulkDeleteResult **indstats = vacrel->indstats;

	Assert(vacrel->do_index_cleanup);

	for (int idx = 0; idx < nindexes; idx++)
	{
		Relation	indrel = indrels[idx];
		IndexBulkDeleteResult *istat = indstats[idx];

		if (istat == NULL || istat->estimated_count)
			continue;

		/* Update index statistics */
		vac_update_relstats(indrel,
							istat->num_pages,
							istat->num_index_tuples,
							0,
							false,
							InvalidTransactionId,
							InvalidMultiXactId,
							NULL, NULL, false);
	}
}

/*
 * Error context callback for errors occurring during vacuum.  The error
 * context messages for index phases should match the messages set in parallel
 * vacuum.  If you change this function for those phases, change
 * parallel_vacuum_error_callback() as well.
 */
static void
vacuum_error_callback(void *arg)
{
	LVRelState *errinfo = arg;

	switch (errinfo->phase)
	{
		case VACUUM_ERRCB_PHASE_SCAN_HEAP:
			if (BlockNumberIsValid(errinfo->blkno))
			{
				if (OffsetNumberIsValid(errinfo->offnum))
					errcontext("while scanning block %u offset %u of relation \"%s.%s\"",
							   errinfo->blkno, errinfo->offnum, errinfo->relnamespace, errinfo->relname);
				else
					errcontext("while scanning block %u of relation \"%s.%s\"",
							   errinfo->blkno, errinfo->relnamespace, errinfo->relname);
			}
			else
				errcontext("while scanning relation \"%s.%s\"",
						   errinfo->relnamespace, errinfo->relname);
			break;

		case VACUUM_ERRCB_PHASE_VACUUM_HEAP:
			if (BlockNumberIsValid(errinfo->blkno))
			{
				if (OffsetNumberIsValid(errinfo->offnum))
					errcontext("while vacuuming block %u offset %u of relation \"%s.%s\"",
							   errinfo->blkno, errinfo->offnum, errinfo->relnamespace, errinfo->relname);
				else
					errcontext("while vacuuming block %u of relation \"%s.%s\"",
							   errinfo->blkno, errinfo->relnamespace, errinfo->relname);
			}
			else
				errcontext("while vacuuming relation \"%s.%s\"",
						   errinfo->relnamespace, errinfo->relname);
			break;

		case VACUUM_ERRCB_PHASE_VACUUM_INDEX:
			errcontext("while vacuuming index \"%s\" of relation \"%s.%s\"",
					   errinfo->indname, errinfo->relnamespace, errinfo->relname);
			break;

		case VACUUM_ERRCB_PHASE_INDEX_CLEANUP:
			errcontext("while cleaning up index \"%s\" of relation \"%s.%s\"",
					   errinfo->indname, errinfo->relnamespace, errinfo->relname);
			break;

		case VACUUM_ERRCB_PHASE_TRUNCATE:
			if (BlockNumberIsValid(errinfo->blkno))
				errcontext("while truncating relation \"%s.%s\" to %u blocks",
						   errinfo->relnamespace, errinfo->relname, errinfo->blkno);
			break;

		case VACUUM_ERRCB_PHASE_UNKNOWN:
		default:
			return;				/* do nothing; the errinfo may not be
								 * initialized */
	}
}

/*
 * Updates the information required for vacuum error callback.  This also saves
 * the current information which can be later restored via restore_vacuum_error_info.
 */
static void
update_vacuum_error_info(LVRelState *vacrel, LVSavedErrInfo *saved_vacrel,
						 int phase, BlockNumber blkno, OffsetNumber offnum)
{
	if (saved_vacrel)
	{
		saved_vacrel->offnum = vacrel->offnum;
		saved_vacrel->blkno = vacrel->blkno;
		saved_vacrel->phase = vacrel->phase;
	}

	vacrel->blkno = blkno;
	vacrel->offnum = offnum;
	vacrel->phase = phase;
}

/*
 * Restores the vacuum information saved via a prior call to update_vacuum_error_info.
 */
static void
restore_vacuum_error_info(LVRelState *vacrel,
						  const LVSavedErrInfo *saved_vacrel)
{
	vacrel->blkno = saved_vacrel->blkno;
	vacrel->offnum = saved_vacrel->offnum;
	vacrel->phase = saved_vacrel->phase;
}
