/* ----------
 * pgstat.c
 *	  Infrastructure for the cumulative statistics system.
 *
 * The cumulative statistics system accumulates statistics for different kinds
 * of objects. Some kinds of statistics are collected for a fixed number of
 * objects (most commonly 1), e.g., checkpointer statistics. Other kinds of
 * statistics are collected for a varying number of objects
 * (e.g. relations). See PgStat_KindInfo for a list of currently handled
 * statistics.
 *
 * Statistics are loaded from the filesystem during startup (by the startup
 * process), unless preceded by a crash, in which case all stats are
 * discarded. They are written out by the checkpointer process just before
 * shutting down, except when shutting down in immediate mode.
 *
 * Fixed-numbered stats are stored in plain (non-dynamic) shared memory.
 *
 * Statistics for variable-numbered objects are stored in dynamic shared
 * memory and can be found via a dshash hashtable. The statistics counters are
 * not part of the dshash entry (PgStatShared_HashEntry) directly, but are
 * separately allocated (PgStatShared_HashEntry->body). The separate
 * allocation allows different kinds of statistics to be stored in the same
 * hashtable without wasting space in PgStatShared_HashEntry.
 *
 * Variable-numbered stats are addressed by PgStat_HashKey while running.  It
 * is not possible to have statistics for an object that cannot be addressed
 * that way at runtime. A wider identifier can be used when serializing to
 * disk (used for replication slot stats).
 *
 * To avoid contention on the shared hashtable, each backend has a
 * backend-local hashtable (pgStatEntryRefHash) in front of the shared
 * hashtable, containing references (PgStat_EntryRef) to shared hashtable
 * entries. The shared hashtable only needs to be accessed when no prior
 * reference is found in the local hashtable. Besides pointing to the
 * shared hashtable entry (PgStatShared_HashEntry) PgStat_EntryRef also
 * contains a pointer to the shared statistics data, as a process-local
 * address, to reduce access costs.
 *
 * The names for structs stored in shared memory are prefixed with
 * PgStatShared instead of PgStat. Each stats entry in shared memory is
 * protected by a dedicated lwlock.
 *
 * Most stats updates are first accumulated locally in each process as pending
 * entries, then later flushed to shared memory (just after commit, or by
 * idle-timeout). This practically eliminates contention on individual stats
 * entries. For most kinds of variable-numbered pending stats data is stored
 * in PgStat_EntryRef->pending. All entries with pending data are in the
 * pgStatPending list. Pending statistics updates are flushed out by
 * pgstat_report_stat().
 *
 * The behavior of different kinds of statistics is determined by the kind's
 * entry in pgstat_kind_infos, see PgStat_KindInfo for details.
 *
 * The consistency of read accesses to statistics can be configured using the
 * stats_fetch_consistency GUC (see config.sgml and monitoring.sgml for the
 * settings). When using PGSTAT_FETCH_CONSISTENCY_CACHE or
 * PGSTAT_FETCH_CONSISTENCY_SNAPSHOT statistics are stored in
 * pgStatLocal.snapshot.
 *
 * To keep things manageable, stats handling is split across several
 * files. Infrastructure pieces are in:
 * - pgstat.c - this file, to tie it all together
 * - pgstat_shmem.c - nearly everything dealing with shared memory, including
 *   the maintenance of hashtable entries
 * - pgstat_xact.c - transactional integration, including the transactional
 *   creation and dropping of stats entries
 *
 * Each statistics kind is handled in a dedicated file:
 * - pgstat_archiver.c
 * - pgstat_bgwriter.c
 * - pgstat_checkpointer.c
 * - pgstat_database.c
 * - pgstat_function.c
 * - pgstat_relation.c
 * - pgstat_replslot.c
 * - pgstat_slru.c
 * - pgstat_subscription.c
 * - pgstat_wal.c
 *
 * Whenever possible infrastructure files should not contain code related to
 * specific kinds of stats.
 *
 *
 * Copyright (c) 2001-2022, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *	  src/backend/utils/activity/pgstat.c
 * ----------
 */
#include "postgres.h"

#include <unistd.h>

#include "access/transam.h"
#include "access/xact.h"
#include "lib/dshash.h"
#include "pgstat.h"
#include "port/atomics.h"
#include "storage/fd.h"
#include "storage/ipc.h"
#include "storage/lwlock.h"
#include "storage/pg_shmem.h"
#include "storage/shmem.h"
#include "utils/guc.h"
#include "utils/memutils.h"
#include "utils/pgstat_internal.h"
#include "utils/timestamp.h"


/* ----------
 * Timer definitions.
 *
 * In milliseconds.
 * ----------
 */

/* minimum interval non-forced stats flushes.*/
#define PGSTAT_MIN_INTERVAL			1000
/* how long until to block flushing pending stats updates */
#define PGSTAT_MAX_INTERVAL			60000
/* when to call pgstat_report_stat() again, even when idle */
#define PGSTAT_IDLE_INTERVAL		10000

/* ----------
 * Initial size hints for the hash tables used in statistics.
 * ----------
 */

#define PGSTAT_SNAPSHOT_HASH_SIZE	512


/* hash table for statistics snapshots entry */
typedef struct PgStat_SnapshotEntry
{
	PgStat_HashKey key;
	char		status;			/* for simplehash use */
	void	   *data;			/* the stats data itself */
} PgStat_SnapshotEntry;


/* ----------
 * Backend-local Hash Table Definitions
 * ----------
 */

/* for stats snapshot entries */
#define SH_PREFIX pgstat_snapshot
#define SH_ELEMENT_TYPE PgStat_SnapshotEntry
#define SH_KEY_TYPE PgStat_HashKey
#define SH_KEY key
#define SH_HASH_KEY(tb, key) \
	pgstat_hash_hash_key(&key, sizeof(PgStat_HashKey), NULL)
#define SH_EQUAL(tb, a, b) \
	pgstat_cmp_hash_key(&a, &b, sizeof(PgStat_HashKey), NULL) == 0
#define SH_SCOPE static inline
#define SH_DEFINE
#define SH_DECLARE
#include "lib/simplehash.h"


/* ----------
 * Local function forward declarations
 * ----------
 */

static void pgstat_write_statsfile(void);
static void pgstat_read_statsfile(void);

static void pgstat_reset_after_failure(void);

static bool pgstat_flush_pending_entries(bool nowait);

static void pgstat_prep_snapshot(void);
static void pgstat_build_snapshot(void);
static void pgstat_build_snapshot_fixed(PgStat_Kind kind);

static inline bool pgstat_is_kind_valid(int ikind);

uint64_t   *yb_new_conn = NULL;

/* ----------
 * GUC parameters
 * ----------
 */

bool		pgstat_track_counts = false;
int			pgstat_fetch_consistency = PGSTAT_FETCH_CONSISTENCY_CACHE;


/* ----------
 * state shared with pgstat_*.c
 * ----------
 */

PgStat_LocalState pgStatLocal;

/*
 * Used in YB to indicate whether the statuses for ongoing concurrent
 * indexes have been retrieved in this transaction.
 */
bool		yb_retrieved_concurrent_index_progress = false;

/* ----------
 * Local data
 *
 * NB: There should be only variables related to stats infrastructure here,
 * not for specific kinds of stats.
 * ----------
 */

/*
 * Memory contexts containing the pgStatEntryRefHash table, the
 * pgStatSharedRef entries, and pending data respectively. Mostly to make it
 * easier to track / attribute memory usage.
 */

static MemoryContext pgStatPendingContext = NULL;

/*
 * Backend local list of PgStat_EntryRef with unflushed pending stats.
 *
 * Newly pending entries should only ever be added to the end of the list,
 * otherwise pgstat_flush_pending_entries() might not see them immediately.
 */
static dlist_head pgStatPending = DLIST_STATIC_INIT(pgStatPending);


/*
 * Force the next stats flush to happen regardless of
 * PGSTAT_MIN_INTERVAL. Useful in test scripts.
 */
static bool pgStatForceNextFlush = false;

/*
 * For assertions that check pgstat is not used before initialization / after
 * shutdown.
 */
#ifdef USE_ASSERT_CHECKING
static bool pgstat_is_initialized = false;
static bool pgstat_is_shutdown = false;
#endif


/*
 * The different kinds of statistics.
 *
 * If reasonably possible, handling specific to one kind of stats should go
 * through this abstraction, rather than making more of pgstat.c aware.
 *
 * See comments for struct PgStat_KindInfo for details about the individual
 * fields.
 *
 * XXX: It'd be nicer to define this outside of this file. But there doesn't
 * seem to be a great way of doing that, given the split across multiple
 * files.
 */
static const PgStat_KindInfo pgstat_kind_infos[PGSTAT_NUM_KINDS] = {

	/* stats kinds for variable-numbered objects */

	[PGSTAT_KIND_DATABASE] = {
		.name = "database",

		.fixed_amount = false,
		/* so pg_stat_database entries can be seen in all databases */
		.accessed_across_databases = true,

		.shared_size = sizeof(PgStatShared_Database),
		.shared_data_off = offsetof(PgStatShared_Database, stats),
		.shared_data_len = sizeof(((PgStatShared_Database *) 0)->stats),
		.pending_size = sizeof(PgStat_StatDBEntry),

		.flush_pending_cb = pgstat_database_flush_cb,
		.reset_timestamp_cb = pgstat_database_reset_timestamp_cb,
	},

	[PGSTAT_KIND_RELATION] = {
		.name = "relation",

		.fixed_amount = false,

		.shared_size = sizeof(PgStatShared_Relation),
		.shared_data_off = offsetof(PgStatShared_Relation, stats),
		.shared_data_len = sizeof(((PgStatShared_Relation *) 0)->stats),
		.pending_size = sizeof(PgStat_TableStatus),

		.flush_pending_cb = pgstat_relation_flush_cb,
		.delete_pending_cb = pgstat_relation_delete_pending_cb,
	},

	[PGSTAT_KIND_FUNCTION] = {
		.name = "function",

		.fixed_amount = false,

		.shared_size = sizeof(PgStatShared_Function),
		.shared_data_off = offsetof(PgStatShared_Function, stats),
		.shared_data_len = sizeof(((PgStatShared_Function *) 0)->stats),
		.pending_size = sizeof(PgStat_BackendFunctionEntry),

		.flush_pending_cb = pgstat_function_flush_cb,
	},

	[PGSTAT_KIND_REPLSLOT] = {
		.name = "replslot",

		.fixed_amount = false,

		.accessed_across_databases = true,
		.named_on_disk = true,

		.shared_size = sizeof(PgStatShared_ReplSlot),
		.shared_data_off = offsetof(PgStatShared_ReplSlot, stats),
		.shared_data_len = sizeof(((PgStatShared_ReplSlot *) 0)->stats),

		.reset_timestamp_cb = pgstat_replslot_reset_timestamp_cb,
		.to_serialized_name = pgstat_replslot_to_serialized_name_cb,
		.from_serialized_name = pgstat_replslot_from_serialized_name_cb,
	},

	[PGSTAT_KIND_SUBSCRIPTION] = {
		.name = "subscription",

		.fixed_amount = false,
		/* so pg_stat_subscription_stats entries can be seen in all databases */
		.accessed_across_databases = true,

		.shared_size = sizeof(PgStatShared_Subscription),
		.shared_data_off = offsetof(PgStatShared_Subscription, stats),
		.shared_data_len = sizeof(((PgStatShared_Subscription *) 0)->stats),
		.pending_size = sizeof(PgStat_BackendSubEntry),

		.flush_pending_cb = pgstat_subscription_flush_cb,
		.reset_timestamp_cb = pgstat_subscription_reset_timestamp_cb,
	},


	/* stats for fixed-numbered (mostly 1) objects */

	[PGSTAT_KIND_ARCHIVER] = {
		.name = "archiver",

		.fixed_amount = true,

		.reset_all_cb = pgstat_archiver_reset_all_cb,
		.snapshot_cb = pgstat_archiver_snapshot_cb,
	},

	[PGSTAT_KIND_BGWRITER] = {
		.name = "bgwriter",

		.fixed_amount = true,

		.reset_all_cb = pgstat_bgwriter_reset_all_cb,
		.snapshot_cb = pgstat_bgwriter_snapshot_cb,
	},

	[PGSTAT_KIND_CHECKPOINTER] = {
		.name = "checkpointer",

		.fixed_amount = true,

		.reset_all_cb = pgstat_checkpointer_reset_all_cb,
		.snapshot_cb = pgstat_checkpointer_snapshot_cb,
	},

	[PGSTAT_KIND_SLRU] = {
		.name = "slru",

		.fixed_amount = true,

		.reset_all_cb = pgstat_slru_reset_all_cb,
		.snapshot_cb = pgstat_slru_snapshot_cb,
	},

	[PGSTAT_KIND_WAL] = {
		.name = "wal",

		.fixed_amount = true,

		.reset_all_cb = pgstat_wal_reset_all_cb,
		.snapshot_cb = pgstat_wal_snapshot_cb,
	},
};


/* ------------------------------------------------------------
 * Functions managing the state of the stats system for all backends.
 * ------------------------------------------------------------
 */

/*
 * Read on-disk stats into memory at server start.
 *
 * Should only be called by the startup process or in single user mode.
 */
void
pgstat_restore_stats(void)
{
	pgstat_read_statsfile();
}

/*
 * Remove the stats file.  This is currently used only if WAL recovery is
 * needed after a crash.
 *
 * Should only be called by the startup process or in single user mode.
 */
void
pgstat_discard_stats(void)
{
	int			ret;

	/* NB: this needs to be done even in single user mode */

	ret = unlink(PGSTAT_STAT_PERMANENT_FILENAME);
	if (ret != 0)
	{
		if (errno == ENOENT)
			elog(DEBUG2,
				 "didn't need to unlink permanent stats file \"%s\" - didn't exist",
				 PGSTAT_STAT_PERMANENT_FILENAME);
		else
			ereport(LOG,
					(errcode_for_file_access(),
					 errmsg("could not unlink permanent statistics file \"%s\": %m",
							PGSTAT_STAT_PERMANENT_FILENAME)));
	}
	else
	{
		ereport(DEBUG2,
				(errcode_for_file_access(),
				 errmsg_internal("unlinked permanent statistics file \"%s\"",
								 PGSTAT_STAT_PERMANENT_FILENAME)));
	}

	/*
	 * Reset stats contents. This will set reset timestamps of fixed-numbered
	 * stats to the current time (no variable stats exist).
	 */
	pgstat_reset_after_failure();
}

/*
 * pgstat_before_server_shutdown() needs to be called by exactly one process
 * during regular server shutdowns. Otherwise all stats will be lost.
 *
 * We currently only write out stats for proc_exit(0). We might want to change
 * that at some point... But right now pgstat_discard_stats() would be called
 * during the start after a disorderly shutdown, anyway.
 */
void
pgstat_before_server_shutdown(int code, Datum arg)
{
	Assert(pgStatLocal.shmem != NULL);
	Assert(!pgStatLocal.shmem->is_shutdown);

	/*
	 * Stats should only be reported after pgstat_initialize() and before
	 * pgstat_shutdown(). This is a convenient point to catch most violations
	 * of this rule.
	 */
	Assert(pgstat_is_initialized && !pgstat_is_shutdown);

	/* flush out our own pending changes before writing out */
	pgstat_report_stat(true);

	/*
	 * Only write out file during normal shutdown. Don't even signal that
	 * we've shutdown during irregular shutdowns, because the shutdown
	 * sequence isn't coordinated to ensure this backend shuts down last.
	 */
	if (code == 0)
	{
		pgStatLocal.shmem->is_shutdown = true;
		pgstat_write_statsfile();
	}
}


/* ------------------------------------------------------------
 * Backend initialization / shutdown functions
 * ------------------------------------------------------------
 */

/*
 * Shut down a single backend's statistics reporting at process exit.
 *
 * Flush out any remaining statistics counts.  Without this, operations
 * triggered during backend exit (such as temp table deletions) won't be
 * counted.
 */
static void
pgstat_shutdown_hook(int code, Datum arg)
{
	Assert(!pgstat_is_shutdown);
	Assert(IsUnderPostmaster || !IsPostmasterEnvironment);

	/*
	 * If we got as far as discovering our own database ID, we can flush out
	 * what we did so far.  Otherwise, we'd be reporting an invalid database
	 * ID, so forget it.  (This means that accesses to pg_database during
	 * failed backend starts might never get counted.)
	 */
	if (OidIsValid(MyDatabaseId))
		pgstat_report_disconnect(MyDatabaseId);

	pgstat_report_stat(true);

	/* there shouldn't be any pending changes left */
	Assert(dlist_is_empty(&pgStatPending));
	dlist_init(&pgStatPending);

	pgstat_detach_shmem();

#ifdef USE_ASSERT_CHECKING
	pgstat_is_shutdown = true;
#endif
}

/*
 * Initialize pgstats state, and set up our on-proc-exit hook. Called from
 * BaseInit().
 *
 * NOTE: MyDatabaseId isn't set yet; so the shutdown hook has to be careful.
 */
void
pgstat_initialize(void)
{
	Assert(!pgstat_is_initialized);

	pgstat_attach_shmem();

	pgstat_init_wal();

	/* Set up a process-exit hook to clean up */
	before_shmem_exit(pgstat_shutdown_hook, 0);

#ifdef USE_ASSERT_CHECKING
	pgstat_is_initialized = true;
#endif
}


/* ------------------------------------------------------------
 * Public functions used by backends follow
 * ------------------------------------------------------------
 */

/*
 * Must be called by processes that performs DML: tcop/postgres.c, logical
 * receiver processes, SPI worker, etc. to flush pending statistics updates to
 * shared memory.
 *
 * Unless called with 'force', pending stats updates are flushed happen once
 * per PGSTAT_MIN_INTERVAL (1000ms). When not forced, stats flushes do not
 * block on lock acquisition, except if stats updates have been pending for
 * longer than PGSTAT_MAX_INTERVAL (60000ms).
 *
 * Whenever pending stats updates remain at the end of pgstat_report_stat() a
 * suggested idle timeout is returned. Currently this is always
 * PGSTAT_IDLE_INTERVAL (10000ms). Callers can use the returned time to set up
 * a timeout after which to call pgstat_report_stat(true), but are not
 * required to to do so.
 *
 * Note that this is called only when not within a transaction, so it is fair
 * to use transaction stop time as an approximation of current time.
 */
long
pgstat_report_stat(bool force)
{
	static TimestampTz pending_since = 0;
	static TimestampTz last_flush = 0;
	bool		partial_flush;
	TimestampTz now;
	bool		nowait;

	pgstat_assert_is_up();
	Assert(!IsTransactionOrTransactionBlock());

	/* "absorb" the forced flush even if there's nothing to flush */
	if (pgStatForceNextFlush)
	{
		force = true;
		pgStatForceNextFlush = false;
	}

	/* Don't expend a clock check if nothing to do */
	if (dlist_is_empty(&pgStatPending) &&
		!have_slrustats &&
		!pgstat_have_pending_wal())
	{
#ifdef YB_TODO
		/*
		 * This assertion fails intermittently during drop table operation.
		 * The test
		 * TestPgRegressDistinctPushdown#testPgRegressDistinctPushdown
		 * reproduces this issue consistently. Temporarily disable this
		 * assertion as a workaround. The root cause is unknown and needs to
		 * be investigated.
		 */
		Assert(pending_since == 0);
#endif
		return 0;
	}

	/*
	 * There should never be stats to report once stats are shut down. Can't
	 * assert that before the checks above, as there is an unconditional
	 * pgstat_report_stat() call in pgstat_shutdown_hook() - which at least
	 * the process that ran pgstat_before_server_shutdown() will still call.
	 */
	Assert(!pgStatLocal.shmem->is_shutdown);

	now = GetCurrentTransactionStopTimestamp();

	if (!force)
	{
		if (pending_since > 0 &&
			TimestampDifferenceExceeds(pending_since, now, PGSTAT_MAX_INTERVAL))
		{
			/* don't keep pending updates longer than PGSTAT_MAX_INTERVAL */
			force = true;
		}
		else if (last_flush > 0 &&
				 !TimestampDifferenceExceeds(last_flush, now, PGSTAT_MIN_INTERVAL))
		{
			/* don't flush too frequently */
			if (pending_since == 0)
				pending_since = now;

			return PGSTAT_IDLE_INTERVAL;
		}
	}

	pgstat_update_dbstats(now);

	/* don't wait for lock acquisition when !force */
	nowait = !force;

	partial_flush = false;

	/* flush database / relation / function / ... stats */
	partial_flush |= pgstat_flush_pending_entries(nowait);

	/* flush wal stats */
	partial_flush |= pgstat_flush_wal(nowait);

	/* flush SLRU stats */
	partial_flush |= pgstat_slru_flush(nowait);

	last_flush = now;

	/*
	 * If some of the pending stats could not be flushed due to lock
	 * contention, let the caller know when to retry.
	 */
	if (partial_flush)
	{
		/* force should have prevented us from getting here */
		Assert(!force);

		/* remember since when stats have been pending */
		if (pending_since == 0)
			pending_since = now;

		return PGSTAT_IDLE_INTERVAL;
	}

	pending_since = 0;

	return 0;
}

/*
 * Force locally pending stats to be flushed during the next
 * pgstat_report_stat() call. This is useful for writing tests.
 */
void
pgstat_force_next_flush(void)
{
	pgStatForceNextFlush = true;
}

/*
 * Only for use by pgstat_reset_counters()
 */
static bool
match_db_entries(PgStatShared_HashEntry *entry, Datum match_data)
{
	return entry->key.dboid == DatumGetObjectId(MyDatabaseId);
}

/*
 * Reset counters for our database.
 *
 * Permission checking for this function is managed through the normal
 * GRANT system.
 */
void
pgstat_reset_counters(void)
{
	TimestampTz ts = GetCurrentTimestamp();

	pgstat_reset_matching_entries(match_db_entries,
								  ObjectIdGetDatum(MyDatabaseId),
								  ts);
}

/*
 * Reset a single variable-numbered entry.
 *
 * If the stats kind is within a database, also reset the database's
 * stat_reset_timestamp.
 *
 * Permission checking for this function is managed through the normal
 * GRANT system.
 */
void
pgstat_reset(PgStat_Kind kind, Oid dboid, Oid objoid)
{
	const PgStat_KindInfo *kind_info = pgstat_get_kind_info(kind);
	TimestampTz ts = GetCurrentTimestamp();

	/* not needed atm, and doesn't make sense with the current signature */
	Assert(!pgstat_get_kind_info(kind)->fixed_amount);

	/* reset the "single counter" */
	pgstat_reset_entry(kind, dboid, objoid, ts);

	if (!kind_info->accessed_across_databases)
		pgstat_reset_database_timestamp(dboid, ts);
}

/*
 * Reset stats for all entries of a kind.
 *
 * Permission checking for this function is managed through the normal
 * GRANT system.
 */
void
pgstat_reset_of_kind(PgStat_Kind kind)
{
	const PgStat_KindInfo *kind_info = pgstat_get_kind_info(kind);
	TimestampTz ts = GetCurrentTimestamp();

	if (kind_info->fixed_amount)
		kind_info->reset_all_cb(ts);
	else
		pgstat_reset_entries_of_kind(kind, ts);
}


/* ------------------------------------------------------------
 * Fetching of stats
 * ------------------------------------------------------------
 */

/*
 * Discard any data collected in the current transaction.  Any subsequent
 * request will cause new snapshots to be read.
 *
 * This is also invoked during transaction commit or abort to discard
 * the no-longer-wanted snapshot.
 */
void
pgstat_clear_snapshot(void)
{
	pgstat_assert_is_up();

	memset(&pgStatLocal.snapshot.fixed_valid, 0,
		   sizeof(pgStatLocal.snapshot.fixed_valid));
	pgStatLocal.snapshot.stats = NULL;
	pgStatLocal.snapshot.mode = PGSTAT_FETCH_CONSISTENCY_NONE;

	/* Release memory, if any was allocated */
	if (pgStatLocal.snapshot.context)
	{
		MemoryContextDelete(pgStatLocal.snapshot.context);

		/* Reset variables */
		pgStatLocal.snapshot.context = NULL;
	}

	/*
	 * Historically the backend_status.c facilities lived in this file, and
	 * were reset with the same function. For now keep it that way, and
	 * forward the reset request.
	 */
	pgstat_clear_backend_activity_snapshot();
	yb_retrieved_concurrent_index_progress = false;
}

void *
pgstat_fetch_entry(PgStat_Kind kind, Oid dboid, Oid objoid)
{
	PgStat_HashKey key;
	PgStat_EntryRef *entry_ref;
	void	   *stats_data;
	const PgStat_KindInfo *kind_info = pgstat_get_kind_info(kind);

	/* should be called from backends */
	Assert(IsUnderPostmaster || !IsPostmasterEnvironment);
	AssertArg(!kind_info->fixed_amount);

	pgstat_prep_snapshot();

	key.kind = kind;
	key.dboid = dboid;
	key.objoid = objoid;

	/* if we need to build a full snapshot, do so */
	if (pgstat_fetch_consistency == PGSTAT_FETCH_CONSISTENCY_SNAPSHOT)
		pgstat_build_snapshot();

	/* if caching is desired, look up in cache */
	if (pgstat_fetch_consistency > PGSTAT_FETCH_CONSISTENCY_NONE)
	{
		PgStat_SnapshotEntry *entry = NULL;

		entry = pgstat_snapshot_lookup(pgStatLocal.snapshot.stats, key);

		if (entry)
			return entry->data;

		/*
		 * If we built a full snapshot and the key is not in
		 * pgStatLocal.snapshot.stats, there are no matching stats.
		 */
		if (pgstat_fetch_consistency == PGSTAT_FETCH_CONSISTENCY_SNAPSHOT)
			return NULL;
	}

	pgStatLocal.snapshot.mode = pgstat_fetch_consistency;

	entry_ref = pgstat_get_entry_ref(kind, dboid, objoid, false, NULL);

	if (entry_ref == NULL || entry_ref->shared_entry->dropped)
	{
		/* create empty entry when using PGSTAT_FETCH_CONSISTENCY_CACHE */
		if (pgstat_fetch_consistency == PGSTAT_FETCH_CONSISTENCY_CACHE)
		{
			PgStat_SnapshotEntry *entry = NULL;
			bool		found;

			entry = pgstat_snapshot_insert(pgStatLocal.snapshot.stats, key, &found);
			Assert(!found);
			entry->data = NULL;
		}
		return NULL;
	}

	/*
	 * Allocate in caller's context for PGSTAT_FETCH_CONSISTENCY_NONE,
	 * otherwise we could quickly end up with a fair bit of memory used due to
	 * repeated accesses.
	 */
	if (pgstat_fetch_consistency == PGSTAT_FETCH_CONSISTENCY_NONE)
		stats_data = palloc(kind_info->shared_data_len);
	else
		stats_data = MemoryContextAlloc(pgStatLocal.snapshot.context,
										kind_info->shared_data_len);

	pgstat_lock_entry_shared(entry_ref, false);
	memcpy(stats_data,
		   pgstat_get_entry_data(kind, entry_ref->shared_stats),
		   kind_info->shared_data_len);
	pgstat_unlock_entry(entry_ref);

	if (pgstat_fetch_consistency > PGSTAT_FETCH_CONSISTENCY_NONE)
	{
		PgStat_SnapshotEntry *entry = NULL;
		bool		found;

		entry = pgstat_snapshot_insert(pgStatLocal.snapshot.stats, key, &found);
		entry->data = stats_data;
	}

	return stats_data;
}

/*
 * If a stats snapshot has been taken, return the timestamp at which that was
 * done, and set *have_snapshot to true. Otherwise *have_snapshot is set to
 * false.
 */
TimestampTz
pgstat_get_stat_snapshot_timestamp(bool *have_snapshot)
{
	if (pgStatLocal.snapshot.mode == PGSTAT_FETCH_CONSISTENCY_SNAPSHOT)
	{
		*have_snapshot = true;
		return pgStatLocal.snapshot.snapshot_timestamp;
	}

	*have_snapshot = false;

	return 0;
}

bool
pgstat_have_entry(PgStat_Kind kind, Oid dboid, Oid objoid)
{
	/* fixed-numbered stats always exist */
	if (pgstat_get_kind_info(kind)->fixed_amount)
		return true;

	return pgstat_get_entry_ref(kind, dboid, objoid, false, NULL) != NULL;
}

/*
 * Ensure snapshot for fixed-numbered 'kind' exists.
 *
 * Typically used by the pgstat_fetch_* functions for a kind of stats, before
 * massaging the data into the desired format.
 */
void
pgstat_snapshot_fixed(PgStat_Kind kind)
{
	AssertArg(pgstat_is_kind_valid(kind));
	AssertArg(pgstat_get_kind_info(kind)->fixed_amount);

	if (pgstat_fetch_consistency == PGSTAT_FETCH_CONSISTENCY_SNAPSHOT)
		pgstat_build_snapshot();
	else
		pgstat_build_snapshot_fixed(kind);

	Assert(pgStatLocal.snapshot.fixed_valid[kind]);
}

static void
pgstat_prep_snapshot(void)
{
	if (pgstat_fetch_consistency == PGSTAT_FETCH_CONSISTENCY_NONE ||
		pgStatLocal.snapshot.stats != NULL)
		return;

	if (!pgStatLocal.snapshot.context)
		pgStatLocal.snapshot.context = AllocSetContextCreate(TopMemoryContext,
															 "PgStat Snapshot",
															 ALLOCSET_SMALL_SIZES);

	pgStatLocal.snapshot.stats =
		pgstat_snapshot_create(pgStatLocal.snapshot.context,
							   PGSTAT_SNAPSHOT_HASH_SIZE,
							   NULL);
}

static void
pgstat_build_snapshot(void)
{
	dshash_seq_status hstat;
	PgStatShared_HashEntry *p;

	/* should only be called when we need a snapshot */
	Assert(pgstat_fetch_consistency == PGSTAT_FETCH_CONSISTENCY_SNAPSHOT);

	/* snapshot already built */
	if (pgStatLocal.snapshot.mode == PGSTAT_FETCH_CONSISTENCY_SNAPSHOT)
		return;

	pgstat_prep_snapshot();

	Assert(pgStatLocal.snapshot.stats->members == 0);

	pgStatLocal.snapshot.snapshot_timestamp = GetCurrentTimestamp();

	/*
	 * Snapshot all variable stats.
	 */
	dshash_seq_init(&hstat, pgStatLocal.shared_hash, false);
	while ((p = dshash_seq_next(&hstat)) != NULL)
	{
		PgStat_Kind kind = p->key.kind;
		const PgStat_KindInfo *kind_info = pgstat_get_kind_info(kind);
		bool		found;
		PgStat_SnapshotEntry *entry;
		PgStatShared_Common *stats_data;

		/*
		 * Check if the stats object should be included in the snapshot.
		 * Unless the stats kind can be accessed from all databases (e.g.,
		 * database stats themselves), we only include stats for the current
		 * database or objects not associated with a database (e.g. shared
		 * relations).
		 */
		if (p->key.dboid != MyDatabaseId &&
			p->key.dboid != InvalidOid &&
			!kind_info->accessed_across_databases)
			continue;

		if (p->dropped)
			continue;

		Assert(pg_atomic_read_u32(&p->refcount) > 0);

		stats_data = dsa_get_address(pgStatLocal.dsa, p->body);
		Assert(stats_data);

		entry = pgstat_snapshot_insert(pgStatLocal.snapshot.stats, p->key, &found);
		Assert(!found);

		entry->data = MemoryContextAlloc(pgStatLocal.snapshot.context,
										 kind_info->shared_size);
		/*
		 * Acquire the LWLock directly instead of using
		 * pg_stat_lock_entry_shared() which requires a reference.
		 */
		LWLockAcquire(&stats_data->lock, LW_SHARED);
		memcpy(entry->data,
			   pgstat_get_entry_data(kind, stats_data),
			   kind_info->shared_size);
		LWLockRelease(&stats_data->lock);
	}
	dshash_seq_term(&hstat);

	/*
	 * Build snapshot of all fixed-numbered stats.
	 */
	for (int kind = PGSTAT_KIND_FIRST_VALID; kind <= PGSTAT_KIND_LAST; kind++)
	{
		const PgStat_KindInfo *kind_info = pgstat_get_kind_info(kind);

		if (!kind_info->fixed_amount)
		{
			Assert(kind_info->snapshot_cb == NULL);
			continue;
		}

		pgstat_build_snapshot_fixed(kind);
	}

	pgStatLocal.snapshot.mode = PGSTAT_FETCH_CONSISTENCY_SNAPSHOT;
}

static void
pgstat_build_snapshot_fixed(PgStat_Kind kind)
{
	const PgStat_KindInfo *kind_info = pgstat_get_kind_info(kind);

	Assert(kind_info->fixed_amount);
	Assert(kind_info->snapshot_cb != NULL);

	if (pgstat_fetch_consistency == PGSTAT_FETCH_CONSISTENCY_NONE)
	{
		/* rebuild every time */
		pgStatLocal.snapshot.fixed_valid[kind] = false;
	}
	else if (pgStatLocal.snapshot.fixed_valid[kind])
	{
		/* in snapshot mode we shouldn't get called again */
		Assert(pgstat_fetch_consistency == PGSTAT_FETCH_CONSISTENCY_CACHE);
		return;
	}

	Assert(!pgStatLocal.snapshot.fixed_valid[kind]);

	kind_info->snapshot_cb();

	Assert(!pgStatLocal.snapshot.fixed_valid[kind]);
	pgStatLocal.snapshot.fixed_valid[kind] = true;
}


/* ------------------------------------------------------------
 * Backend-local pending stats infrastructure
 * ------------------------------------------------------------
 */

/*
 * Returns the appropriate PgStat_EntryRef, preparing it to receive pending
 * stats if not already done.
 *
 * If created_entry is non-NULL, it'll be set to true if the entry is newly
 * created, false otherwise.
 */
PgStat_EntryRef *
pgstat_prep_pending_entry(PgStat_Kind kind, Oid dboid, Oid objoid, bool *created_entry)
{
	PgStat_EntryRef *entry_ref;

	/* need to be able to flush out */
	Assert(pgstat_get_kind_info(kind)->flush_pending_cb != NULL);

	if (unlikely(!pgStatPendingContext))
	{
		pgStatPendingContext =
			AllocSetContextCreate(TopMemoryContext,
								  "PgStat Pending",
								  ALLOCSET_SMALL_SIZES);
	}

	entry_ref = pgstat_get_entry_ref(kind, dboid, objoid,
									 true, created_entry);

	if (entry_ref->pending == NULL)
	{
		size_t		entrysize = pgstat_get_kind_info(kind)->pending_size;

		Assert(entrysize != (size_t) -1);

		entry_ref->pending = MemoryContextAllocZero(pgStatPendingContext, entrysize);
		dlist_push_tail(&pgStatPending, &entry_ref->pending_node);
	}

	return entry_ref;
}

/*
 * Return an existing stats entry, or NULL.
 *
 * This should only be used for helper function for pgstatfuncs.c - outside of
 * that it shouldn't be needed.
 */
PgStat_EntryRef *
pgstat_fetch_pending_entry(PgStat_Kind kind, Oid dboid, Oid objoid)
{
	PgStat_EntryRef *entry_ref;

	entry_ref = pgstat_get_entry_ref(kind, dboid, objoid, false, NULL);

	if (entry_ref == NULL || entry_ref->pending == NULL)
		return NULL;

	return entry_ref;
}

void
pgstat_delete_pending_entry(PgStat_EntryRef *entry_ref)
{
	PgStat_Kind kind = entry_ref->shared_entry->key.kind;
	const PgStat_KindInfo *kind_info = pgstat_get_kind_info(kind);
	void	   *pending_data = entry_ref->pending;

	Assert(pending_data != NULL);
	/* !fixed_amount stats should be handled explicitly */
	Assert(!pgstat_get_kind_info(kind)->fixed_amount);

	if (kind_info->delete_pending_cb)
		kind_info->delete_pending_cb(entry_ref);

	pfree(pending_data);
	entry_ref->pending = NULL;

	dlist_delete(&entry_ref->pending_node);
}

/*
 * Flush out pending stats for database objects (databases, relations,
 * functions).
 */
static bool
pgstat_flush_pending_entries(bool nowait)
{
	bool		have_pending = false;
	dlist_node *cur = NULL;

	/*
	 * Need to be a bit careful iterating over the list of pending entries.
	 * Processing a pending entry may queue further pending entries to the end
	 * of the list that we want to process, so a simple iteration won't do.
	 * Further complicating matters is that we want to delete the current
	 * entry in each iteration from the list if we flushed successfully.
	 *
	 * So we just keep track of the next pointer in each loop iteration.
	 */
	if (!dlist_is_empty(&pgStatPending))
		cur = dlist_head_node(&pgStatPending);

	while (cur)
	{
		PgStat_EntryRef *entry_ref =
		dlist_container(PgStat_EntryRef, pending_node, cur);
		PgStat_HashKey key = entry_ref->shared_entry->key;
		PgStat_Kind kind = key.kind;
		const PgStat_KindInfo *kind_info = pgstat_get_kind_info(kind);
		bool		did_flush;
		dlist_node *next;

		Assert(!kind_info->fixed_amount);
		Assert(kind_info->flush_pending_cb != NULL);

		/* flush the stats, if possible */
		did_flush = kind_info->flush_pending_cb(entry_ref, nowait);

		Assert(did_flush || nowait);

		/* determine next entry, before deleting the pending entry */
		if (dlist_has_next(&pgStatPending, cur))
			next = dlist_next_node(&pgStatPending, cur);
		else
			next = NULL;

		/* if successfully flushed, remove entry */
		if (did_flush)
			pgstat_delete_pending_entry(entry_ref);
		else
			have_pending = true;

		cur = next;
	}

	Assert(dlist_is_empty(&pgStatPending) == !have_pending);

	return have_pending;
}


/* ------------------------------------------------------------
 * Helper / infrastructure functions
 * ------------------------------------------------------------
 */

PgStat_Kind
pgstat_get_kind_from_str(char *kind_str)
{
	for (int kind = PGSTAT_KIND_FIRST_VALID; kind <= PGSTAT_KIND_LAST; kind++)
	{
		if (pg_strcasecmp(kind_str, pgstat_kind_infos[kind].name) == 0)
			return kind;
	}

	ereport(ERROR,
			(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			 errmsg("invalid statistics kind: \"%s\"", kind_str)));
	return PGSTAT_KIND_DATABASE;	/* avoid compiler warnings */
}

static inline bool
pgstat_is_kind_valid(int ikind)
{
	return ikind >= PGSTAT_KIND_FIRST_VALID && ikind <= PGSTAT_KIND_LAST;
}

const PgStat_KindInfo *
pgstat_get_kind_info(PgStat_Kind kind)
{
	AssertArg(pgstat_is_kind_valid(kind));

	return &pgstat_kind_infos[kind];
}

/*
 * Stats should only be reported after pgstat_initialize() and before
 * pgstat_shutdown(). This check is put in a few central places to catch
 * violations of this rule more easily.
 */
#ifdef USE_ASSERT_CHECKING
void
pgstat_assert_is_up(void)
{
	Assert(pgstat_is_initialized && !pgstat_is_shutdown);
}
#endif


/* ------------------------------------------------------------
 * reading and writing of on-disk stats file
 * ------------------------------------------------------------
 */

/* helpers for pgstat_write_statsfile() */
static void
write_chunk(FILE *fpout, void *ptr, size_t len)
{
	int			rc;

	rc = fwrite(ptr, len, 1, fpout);

	/* we'll check for errors with ferror once at the end */
	(void) rc;
}

#define write_chunk_s(fpout, ptr) write_chunk(fpout, ptr, sizeof(*ptr))

/*
 * This function is called in the last process that is accessing the shared
 * stats so locking is not required.
 */
static void
pgstat_write_statsfile(void)
{
	FILE	   *fpout;
	int32		format_id;
	const char *tmpfile = PGSTAT_STAT_PERMANENT_TMPFILE;
	const char *statfile = PGSTAT_STAT_PERMANENT_FILENAME;
	dshash_seq_status hstat;
	PgStatShared_HashEntry *ps;

	pgstat_assert_is_up();

	/* we're shutting down, so it's ok to just override this */
	pgstat_fetch_consistency = PGSTAT_FETCH_CONSISTENCY_NONE;

	elog(DEBUG2, "writing stats file \"%s\"", statfile);

	/*
	 * Open the statistics temp file to write out the current values.
	 */
	fpout = AllocateFile(tmpfile, PG_BINARY_W);
	if (fpout == NULL)
	{
		ereport(LOG,
				(errcode_for_file_access(),
				 errmsg("could not open temporary statistics file \"%s\": %m",
						tmpfile)));
		return;
	}

	/*
	 * Write the file header --- currently just a format ID.
	 */
	format_id = PGSTAT_FILE_FORMAT_ID;
	write_chunk_s(fpout, &format_id);

	/*
	 * XXX: The following could now be generalized to just iterate over
	 * pgstat_kind_infos instead of knowing about the different kinds of
	 * stats.
	 */

	/*
	 * Write archiver stats struct
	 */
	pgstat_build_snapshot_fixed(PGSTAT_KIND_ARCHIVER);
	write_chunk_s(fpout, &pgStatLocal.snapshot.archiver);

	/*
	 * Write bgwriter stats struct
	 */
	pgstat_build_snapshot_fixed(PGSTAT_KIND_BGWRITER);
	write_chunk_s(fpout, &pgStatLocal.snapshot.bgwriter);

	/*
	 * Write checkpointer stats struct
	 */
	pgstat_build_snapshot_fixed(PGSTAT_KIND_CHECKPOINTER);
	write_chunk_s(fpout, &pgStatLocal.snapshot.checkpointer);

	/*
	 * Write SLRU stats struct
	 */
	pgstat_build_snapshot_fixed(PGSTAT_KIND_SLRU);
	write_chunk_s(fpout, &pgStatLocal.snapshot.slru);

	/*
	 * Write WAL stats struct
	 */
	pgstat_build_snapshot_fixed(PGSTAT_KIND_WAL);
	write_chunk_s(fpout, &pgStatLocal.snapshot.wal);

	/*
	 * Walk through the stats entries
	 */
	dshash_seq_init(&hstat, pgStatLocal.shared_hash, false);
	while ((ps = dshash_seq_next(&hstat)) != NULL)
	{
		PgStatShared_Common *shstats;
		const PgStat_KindInfo *kind_info = NULL;

		CHECK_FOR_INTERRUPTS();

		/* we may have some "dropped" entries not yet removed, skip them */
		Assert(!ps->dropped);
		if (ps->dropped)
			continue;

		shstats = (PgStatShared_Common *) dsa_get_address(pgStatLocal.dsa, ps->body);

		kind_info = pgstat_get_kind_info(ps->key.kind);

		/* if not dropped the valid-entry refcount should exist */
		Assert(pg_atomic_read_u32(&ps->refcount) > 0);

		if (!kind_info->to_serialized_name)
		{
			/* normal stats entry, identified by PgStat_HashKey */
			fputc('S', fpout);
			write_chunk_s(fpout, &ps->key);
		}
		else
		{
			/* stats entry identified by name on disk (e.g. slots) */
			NameData	name;

			kind_info->to_serialized_name(&ps->key, shstats, &name);

			fputc('N', fpout);
			write_chunk_s(fpout, &ps->key.kind);
			write_chunk_s(fpout, &name);
		}

		/* Write except the header part of the entry */
		write_chunk(fpout,
					pgstat_get_entry_data(ps->key.kind, shstats),
					pgstat_get_entry_len(ps->key.kind));
	}
	dshash_seq_term(&hstat);

	/*
	 * No more output to be done. Close the temp file and replace the old
	 * pgstat.stat with it.  The ferror() check replaces testing for error
	 * after each individual fputc or fwrite (in write_chunk()) above.
	 */
	fputc('E', fpout);

	if (ferror(fpout))
	{
		ereport(LOG,
				(errcode_for_file_access(),
				 errmsg("could not write temporary statistics file \"%s\": %m",
						tmpfile)));
		FreeFile(fpout);
		unlink(tmpfile);
	}
	else if (FreeFile(fpout) < 0)
	{
		ereport(LOG,
				(errcode_for_file_access(),
				 errmsg("could not close temporary statistics file \"%s\": %m",
						tmpfile)));
		unlink(tmpfile);
	}
	else if (rename(tmpfile, statfile) < 0)
	{
		ereport(LOG,
				(errcode_for_file_access(),
				 errmsg("could not rename temporary statistics file \"%s\" to \"%s\": %m",
						tmpfile, statfile)));
		unlink(tmpfile);
	}
}

/* helpers for pgstat_read_statsfile() */
static bool
read_chunk(FILE *fpin, void *ptr, size_t len)
{
	return fread(ptr, 1, len, fpin) == len;
}

#define read_chunk_s(fpin, ptr) read_chunk(fpin, ptr, sizeof(*ptr))

/*
 * Reads in existing statistics file into the shared stats hash.
 *
 * This function is called in the only process that is accessing the shared
 * stats so locking is not required.
 */
static void
pgstat_read_statsfile(void)
{
	FILE	   *fpin;
	int32		format_id;
	bool		found;
	const char *statfile = PGSTAT_STAT_PERMANENT_FILENAME;
	PgStat_ShmemControl *shmem = pgStatLocal.shmem;

	/* shouldn't be called from postmaster */
	Assert(IsUnderPostmaster || !IsPostmasterEnvironment);

	elog(DEBUG2, "reading stats file \"%s\"", statfile);

	/*
	 * Try to open the stats file. If it doesn't exist, the backends simply
	 * returns zero for anything and statistics simply starts from scratch
	 * with empty counters.
	 *
	 * ENOENT is a possibility if stats collection was previously disabled or
	 * has not yet written the stats file for the first time.  Any other
	 * failure condition is suspicious.
	 */
	if ((fpin = AllocateFile(statfile, PG_BINARY_R)) == NULL)
	{
		if (errno != ENOENT)
			ereport(LOG,
					(errcode_for_file_access(),
					 errmsg("could not open statistics file \"%s\": %m",
							statfile)));
		pgstat_reset_after_failure();
		return;
	}

	/*
	 * Verify it's of the expected format.
	 */
	if (!read_chunk_s(fpin, &format_id) ||
		format_id != PGSTAT_FILE_FORMAT_ID)
		goto error;

	/*
	 * XXX: The following could now be generalized to just iterate over
	 * pgstat_kind_infos instead of knowing about the different kinds of
	 * stats.
	 */

	/*
	 * Read archiver stats struct
	 */
	if (!read_chunk_s(fpin, &shmem->archiver.stats))
		goto error;

	/*
	 * Read bgwriter stats struct
	 */
	if (!read_chunk_s(fpin, &shmem->bgwriter.stats))
		goto error;

	/*
	 * Read checkpointer stats struct
	 */
	if (!read_chunk_s(fpin, &shmem->checkpointer.stats))
		goto error;

	/*
	 * Read SLRU stats struct
	 */
	if (!read_chunk_s(fpin, &shmem->slru.stats))
		goto error;

	/*
	 * Read WAL stats struct
	 */
	if (!read_chunk_s(fpin, &shmem->wal.stats))
		goto error;

	/*
	 * We found an existing statistics file. Read it and put all the hash
	 * table entries into place.
	 */
	for (;;)
	{
		int			t = fgetc(fpin);

		switch (t)
		{
			case 'S':
			case 'N':
				{
					PgStat_HashKey key;
					PgStatShared_HashEntry *p;
					PgStatShared_Common *header;

					CHECK_FOR_INTERRUPTS();

					if (t == 'S')
					{
						/* normal stats entry, identified by PgStat_HashKey */
						if (!read_chunk_s(fpin, &key))
							goto error;

						if (!pgstat_is_kind_valid(key.kind))
							goto error;
					}
					else
					{
						/* stats entry identified by name on disk (e.g. slots) */
						const PgStat_KindInfo *kind_info = NULL;
						PgStat_Kind kind;
						NameData	name;

						if (!read_chunk_s(fpin, &kind))
							goto error;
						if (!read_chunk_s(fpin, &name))
							goto error;
						if (!pgstat_is_kind_valid(kind))
							goto error;

						kind_info = pgstat_get_kind_info(kind);

						if (!kind_info->from_serialized_name)
							goto error;

						if (!kind_info->from_serialized_name(&name, &key))
						{
							/* skip over data for entry we don't care about */
							if (fseek(fpin, pgstat_get_entry_len(kind), SEEK_CUR) != 0)
								goto error;

							continue;
						}

						Assert(key.kind == kind);
					}

					/*
					 * This intentionally doesn't use pgstat_get_entry_ref() -
					 * putting all stats into checkpointer's
					 * pgStatEntryRefHash would be wasted effort and memory.
					 */
					p = dshash_find_or_insert(pgStatLocal.shared_hash, &key, &found);

					/* don't allow duplicate entries */
					if (found)
					{
						dshash_release_lock(pgStatLocal.shared_hash, p);
						elog(WARNING, "found duplicate stats entry %d/%u/%u",
							 key.kind, key.dboid, key.objoid);
						goto error;
					}

					header = pgstat_init_entry(key.kind, p);
					dshash_release_lock(pgStatLocal.shared_hash, p);

					if (!read_chunk(fpin,
									pgstat_get_entry_data(key.kind, header),
									pgstat_get_entry_len(key.kind)))
						goto error;

					break;
				}
			case 'E':
				/* check that 'E' actually signals end of file */
				if (fgetc(fpin) != EOF)
					goto error;

				goto done;

			default:
				goto error;
		}
	}

done:
	FreeFile(fpin);

	elog(DEBUG2, "removing permanent stats file \"%s\"", statfile);
	unlink(statfile);

	return;

error:
	ereport(LOG,
			(errmsg("corrupted statistics file \"%s\"", statfile)));

	pgstat_reset_after_failure();

	goto done;
}

/*
 * Helper to reset / drop stats after a crash or after restoring stats from
 * disk failed, potentially after already loading parts.
 */
static void
pgstat_reset_after_failure(void)
{
	TimestampTz ts = GetCurrentTimestamp();

	/* reset fixed-numbered stats */
	for (int kind = PGSTAT_KIND_FIRST_VALID; kind <= PGSTAT_KIND_LAST; kind++)
	{
		const PgStat_KindInfo *kind_info = pgstat_get_kind_info(kind);

		if (!kind_info->fixed_amount)
			continue;

		kind_info->reset_all_cb(ts);
	}

	/* and drop variable-numbered ones */
	pgstat_drop_all_entries();
}
