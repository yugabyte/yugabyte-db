/* ----------
 *	pgstat.h
 *
 *	Definitions for the PostgreSQL cumulative statistics system.
 *
 *	Copyright (c) 2001-2022, PostgreSQL Global Development Group
 *
 *	src/include/pgstat.h
 * ----------
 */
#ifndef PGSTAT_H
#define PGSTAT_H

#include "datatype/timestamp.h"
#include "portability/instr_time.h"
#include "postmaster/pgarch.h"	/* for MAX_XFN_CHARS */
#include "utils/backend_progress.h" /* for backward compatibility */
#include "utils/backend_status.h"	/* for backward compatibility */
#include "utils/relcache.h"
#include "utils/wait_event.h"	/* for backward compatibility */


/* ----------
 * Paths for the statistics files (relative to installation's $PGDATA).
 * ----------
 */
#define PGSTAT_STAT_PERMANENT_DIRECTORY		"pg_stat"
#define PGSTAT_STAT_PERMANENT_FILENAME		"pg_stat/pgstat.stat"
#define PGSTAT_STAT_PERMANENT_TMPFILE		"pg_stat/pgstat.tmp"

#define PGSTAT_YBSTAT_PERMANENT_FILENAME    "pg_stat/yb_global.stat"
#define PGSTAT_YBSTAT_PERMANENT_TMPFILE     "pg_stat/yb_global.tmp"

/* Default directory to store temporary statistics data in */
#define PG_STAT_TMP_DIR		"pg_stat_tmp"

/* Caps the number of queries which can be stored in the array. */
#define TERMINATED_QUERIES_SIZE 1000

/* The types of statistics entries */
typedef enum PgStat_Kind
{
	/* use 0 for INVALID, to catch zero-initialized data */
	PGSTAT_KIND_INVALID = 0,

	/* stats for variable-numbered objects */
	PGSTAT_KIND_DATABASE,		/* database-wide statistics */
	PGSTAT_KIND_RELATION,		/* per-table statistics */
	PGSTAT_KIND_FUNCTION,		/* per-function statistics */
	PGSTAT_KIND_REPLSLOT,		/* per-slot statistics */
	PGSTAT_KIND_SUBSCRIPTION,	/* per-subscription statistics */

	/* stats for fixed-numbered objects */
	PGSTAT_KIND_ARCHIVER,
	PGSTAT_KIND_BGWRITER,
	PGSTAT_KIND_CHECKPOINTER,
	PGSTAT_KIND_SLRU,
	PGSTAT_KIND_WAL,
} PgStat_Kind;

#define PGSTAT_KIND_FIRST_VALID PGSTAT_KIND_DATABASE
#define PGSTAT_KIND_LAST PGSTAT_KIND_WAL
#define PGSTAT_NUM_KINDS (PGSTAT_KIND_LAST + 1)

/* Values for track_functions GUC variable --- order is significant! */
typedef enum TrackFunctionsLevel
{
	TRACK_FUNC_OFF,
	TRACK_FUNC_PL,
	TRACK_FUNC_ALL
}			TrackFunctionsLevel;

typedef enum PgStat_FetchConsistency
{
	PGSTAT_FETCH_CONSISTENCY_NONE,
	PGSTAT_FETCH_CONSISTENCY_CACHE,
	PGSTAT_FETCH_CONSISTENCY_SNAPSHOT,
} PgStat_FetchConsistency;

/* Values to track the cause of session termination */
typedef enum SessionEndType
{
	DISCONNECT_NOT_YET,			/* still active */
	DISCONNECT_NORMAL,
	DISCONNECT_CLIENT_EOF,
	DISCONNECT_FATAL,
	DISCONNECT_KILLED
} SessionEndType;

/* ----------
 * The data type used for counters.
 * ----------
 */
typedef int64 PgStat_Counter;

/* YB_TODO(neil)
 * - "typedef enum StatMsgType" or MTYPE is obsolete.
 * - Replace PGSTAT_MTYPE_ANALYZE must be reimplemented.
 */

/* ------------------------------------------------------------
 * Structures kept in backend local memory while accumulating counts
 * ------------------------------------------------------------
 */

/* ----------
 * PgStat_FunctionCounts	The actual per-function counts kept by a backend
 *
 * This struct should contain only actual event counters, because we memcmp
 * it against zeroes to detect whether there are any pending stats.
 *
 * Note that the time counters are in instr_time format here.  We convert to
 * microseconds in PgStat_Counter format when flushing out pending statistics.
 * ----------
 */
typedef struct PgStat_FunctionCounts
{
	PgStat_Counter f_numcalls;
	instr_time	f_total_time;
	instr_time	f_self_time;
} PgStat_FunctionCounts;

/* ----------
 * PgStat_BackendFunctionEntry	Non-flushed function stats.
 * ----------
 */
typedef struct PgStat_BackendFunctionEntry
{
	PgStat_FunctionCounts f_counts;
} PgStat_BackendFunctionEntry;

/*
 * Working state needed to accumulate per-function-call timing statistics.
 */
typedef struct PgStat_FunctionCallUsage
{
	/* Link to function's hashtable entry (must still be there at exit!) */
	/* NULL means we are not tracking the current function call */
	PgStat_FunctionCounts *fs;
	/* Total time previously charged to function, as of function start */
	instr_time	save_f_total_time;
	/* Backend-wide total time as of function start */
	instr_time	save_total;
	/* system clock as of function start */
	instr_time	f_start;
} PgStat_FunctionCallUsage;

/* ----------
 * PgStat_BackendSubEntry	Non-flushed subscription stats.
 * ----------
 */
typedef struct PgStat_BackendSubEntry
{
	PgStat_Counter apply_error_count;
	PgStat_Counter sync_error_count;
} PgStat_BackendSubEntry;

/* ----------
 * PgStat_TableCounts			The actual per-table counts kept by a backend
 *
 * This struct should contain only actual event counters, because we memcmp
 * it against zeroes to detect whether there are any stats updates to apply.
 * It is a component of PgStat_TableStatus (within-backend state).
 *
 * Note: for a table, tuples_returned is the number of tuples successfully
 * fetched by heap_getnext, while tuples_fetched is the number of tuples
 * successfully fetched by heap_fetch under the control of bitmap indexscans.
 * For an index, tuples_returned is the number of index entries returned by
 * the index AM, while tuples_fetched is the number of tuples successfully
 * fetched by heap_fetch under the control of simple indexscans for this index.
 *
 * tuples_inserted/updated/deleted/hot_updated count attempted actions,
 * regardless of whether the transaction committed.  delta_live_tuples,
 * delta_dead_tuples, and changed_tuples are set depending on commit or abort.
 * Note that delta_live_tuples and delta_dead_tuples can be negative!
 * ----------
 */
typedef struct PgStat_TableCounts
{
	PgStat_Counter t_numscans;

	PgStat_Counter t_tuples_returned;
	PgStat_Counter t_tuples_fetched;

	PgStat_Counter t_tuples_inserted;
	PgStat_Counter t_tuples_updated;
	PgStat_Counter t_tuples_deleted;
	PgStat_Counter t_tuples_hot_updated;
	bool		t_truncdropped;

	PgStat_Counter t_delta_live_tuples;
	PgStat_Counter t_delta_dead_tuples;
	PgStat_Counter t_changed_tuples;

	PgStat_Counter t_blocks_fetched;
	PgStat_Counter t_blocks_hit;
} PgStat_TableCounts;

/* ----------
 * PgStat_TableStatus			Per-table status within a backend
 *
 * Many of the event counters are nontransactional, ie, we count events
 * in committed and aborted transactions alike.  For these, we just count
 * directly in the PgStat_TableStatus.  However, delta_live_tuples,
 * delta_dead_tuples, and changed_tuples must be derived from event counts
 * with awareness of whether the transaction or subtransaction committed or
 * aborted.  Hence, we also keep a stack of per-(sub)transaction status
 * records for every table modified in the current transaction.  At commit
 * or abort, we propagate tuples_inserted/updated/deleted up to the
 * parent subtransaction level, or out to the parent PgStat_TableStatus,
 * as appropriate.
 * ----------
 */
typedef struct PgStat_TableStatus
{
	Oid			t_id;			/* table's OID */
	bool		t_shared;		/* is it a shared catalog? */
	struct PgStat_TableXactStatus *trans;	/* lowest subxact's counts */
	PgStat_TableCounts t_counts;	/* event counts to be sent */
	Relation	relation;		/* rel that is using this entry */
} PgStat_TableStatus;

/* ----------
 * PgStat_TableXactStatus		Per-table, per-subtransaction status
 * ----------
 */
typedef struct PgStat_TableXactStatus
{
	PgStat_Counter tuples_inserted; /* tuples inserted in (sub)xact */
	PgStat_Counter tuples_updated;	/* tuples updated in (sub)xact */
	PgStat_Counter tuples_deleted;	/* tuples deleted in (sub)xact */
	bool		truncdropped;	/* relation truncated/dropped in this
								 * (sub)xact */
	/* tuples i/u/d prior to truncate/drop */
	PgStat_Counter inserted_pre_truncdrop;
	PgStat_Counter updated_pre_truncdrop;
	PgStat_Counter deleted_pre_truncdrop;
	int			nest_level;		/* subtransaction nest level */
	/* links to other structs for same relation: */
	struct PgStat_TableXactStatus *upper;	/* next higher subxact if any */
	PgStat_TableStatus *parent; /* per-table status */
	/* structs of same subxact level are linked here: */
	struct PgStat_TableXactStatus *next;	/* next of same subxact */
} PgStat_TableXactStatus;


/* YB_TODO(neil) This feature needs changes to match new implementation */
#ifdef YB_TODO
#define QUERY_TEXT_SIZE 		256
#define QUERY_TERMINATION_SIZE	256
typedef struct PgStat_MsgQueryTermination
{
	PgStat_MsgHdr m_hdr;

	Oid m_st_userid;
	Oid m_databaseoid;
	int32 backend_pid;
	TimestampTz activity_start_timestamp;
	TimestampTz activity_end_timestamp;
	char query_string[QUERY_TEXT_SIZE];
	char termination_reason[QUERY_TERMINATION_SIZE];
} PgStat_MsgQueryTermination;
typedef union PgStat_Msg
{
	PgStat_MsgQueryTermination msg_querytermination;
} PgStat_Msg;

typedef struct PgStat_YBStatQueryEntry
{
	/*
	 * query_oid is not an actual oid. It is an index that
	 * represents its location in the array that stores the
	 * terminated queries modulo TERMINATED_QUERIES_SIZE.
	 */
	Oid query_oid;

	/*
	 * We need to store the owner ID of the database for
	 * security validation when the queries are fetched by the user.
	 */
	Oid st_userid;
	Oid database_oid;
	int32 backend_pid;
	TimestampTz activity_start_timestamp;
	TimestampTz activity_end_timestamp;

	/*
	 * query_string_size: records the length of the string
	 * so that when writing this string to file, we only write
	 * that many characters.
	 */
	size_t query_string_size;
	char query_string[QUERY_TEXT_SIZE];

	/*
	 * termination_reason_size: records the length of the string
	 * so that when writing this string to file, we only write
	 * that many characters.
	 */
	size_t termination_reason_size;
	char termination_reason[QUERY_TERMINATION_SIZE];
} PgStat_YBStatQueryEntry;

#endif

/* ------------------------------------------------------------
 * Data structures on disk and in shared memory follow
 *
 * PGSTAT_FILE_FORMAT_ID should be changed whenever any of these
 * data structures change.
 * ------------------------------------------------------------
 */

#define PGSTAT_FILE_FORMAT_ID	0x01A5BCA7

typedef struct PgStat_ArchiverStats
{
	PgStat_Counter archived_count;	/* archival successes */
	char		last_archived_wal[MAX_XFN_CHARS + 1];	/* last WAL file
														 * archived */
	TimestampTz last_archived_timestamp;	/* last archival success time */
	PgStat_Counter failed_count;	/* failed archival attempts */
	char		last_failed_wal[MAX_XFN_CHARS + 1]; /* WAL file involved in
													 * last failure */
	TimestampTz last_failed_timestamp;	/* last archival failure time */
	TimestampTz stat_reset_timestamp;
} PgStat_ArchiverStats;

typedef struct PgStat_BgWriterStats
{
	PgStat_Counter buf_written_clean;
	PgStat_Counter maxwritten_clean;
	PgStat_Counter buf_alloc;
	TimestampTz stat_reset_timestamp;
} PgStat_BgWriterStats;

typedef struct PgStat_CheckpointerStats
{
	PgStat_Counter timed_checkpoints;
	PgStat_Counter requested_checkpoints;
	PgStat_Counter checkpoint_write_time;	/* times in milliseconds */
	PgStat_Counter checkpoint_sync_time;
	PgStat_Counter buf_written_checkpoints;
	PgStat_Counter buf_written_backend;
	PgStat_Counter buf_fsync_backend;
} PgStat_CheckpointerStats;

typedef struct PgStat_StatDBEntry
{
	PgStat_Counter n_xact_commit;
	PgStat_Counter n_xact_rollback;
	PgStat_Counter n_blocks_fetched;
	PgStat_Counter n_blocks_hit;
	PgStat_Counter n_tuples_returned;
	PgStat_Counter n_tuples_fetched;
	PgStat_Counter n_tuples_inserted;
	PgStat_Counter n_tuples_updated;
	PgStat_Counter n_tuples_deleted;
	TimestampTz last_autovac_time;
	PgStat_Counter n_conflict_tablespace;
	PgStat_Counter n_conflict_lock;
	PgStat_Counter n_conflict_snapshot;
	PgStat_Counter n_conflict_bufferpin;
	PgStat_Counter n_conflict_startup_deadlock;
	PgStat_Counter n_temp_files;
	PgStat_Counter n_temp_bytes;
	PgStat_Counter n_deadlocks;
	PgStat_Counter n_checksum_failures;
	TimestampTz last_checksum_failure;
	PgStat_Counter n_block_read_time;	/* times in microseconds */
	PgStat_Counter n_block_write_time;
	PgStat_Counter n_sessions;
	PgStat_Counter total_session_time;
	PgStat_Counter total_active_time;
	PgStat_Counter total_idle_in_xact_time;
	PgStat_Counter n_sessions_abandoned;
	PgStat_Counter n_sessions_fatal;
	PgStat_Counter n_sessions_killed;

	TimestampTz stat_reset_timestamp;
} PgStat_StatDBEntry;

typedef struct PgStat_StatFuncEntry
{
	PgStat_Counter f_numcalls;

	PgStat_Counter f_total_time;	/* times in microseconds */
	PgStat_Counter f_self_time;
} PgStat_StatFuncEntry;

typedef struct PgStat_StatReplSlotEntry
{
	/*
	 * In PG 15 this field is unused, but not removed, to avoid changing
	 * PGSTAT_FILE_FORMAT_ID.
	 */
	NameData	slotname_unused;
	PgStat_Counter spill_txns;
	PgStat_Counter spill_count;
	PgStat_Counter spill_bytes;
	PgStat_Counter stream_txns;
	PgStat_Counter stream_count;
	PgStat_Counter stream_bytes;
	PgStat_Counter total_txns;
	PgStat_Counter total_bytes;
	TimestampTz stat_reset_timestamp;
} PgStat_StatReplSlotEntry;

typedef struct PgStat_SLRUStats
{
	PgStat_Counter blocks_zeroed;
	PgStat_Counter blocks_hit;
	PgStat_Counter blocks_read;
	PgStat_Counter blocks_written;
	PgStat_Counter blocks_exists;
	PgStat_Counter flush;
	PgStat_Counter truncate;
	TimestampTz stat_reset_timestamp;
} PgStat_SLRUStats;

typedef struct PgStat_StatSubEntry
{
	PgStat_Counter apply_error_count;
	PgStat_Counter sync_error_count;
	TimestampTz stat_reset_timestamp;
} PgStat_StatSubEntry;

typedef struct PgStat_StatTabEntry
{
	PgStat_Counter numscans;

	PgStat_Counter tuples_returned;
	PgStat_Counter tuples_fetched;

	PgStat_Counter tuples_inserted;
	PgStat_Counter tuples_updated;
	PgStat_Counter tuples_deleted;
	PgStat_Counter tuples_hot_updated;

	PgStat_Counter n_live_tuples;
	PgStat_Counter n_dead_tuples;
	PgStat_Counter changes_since_analyze;
	PgStat_Counter inserts_since_vacuum;

	PgStat_Counter blocks_fetched;
	PgStat_Counter blocks_hit;

	TimestampTz vacuum_timestamp;	/* user initiated vacuum */
	PgStat_Counter vacuum_count;
	TimestampTz autovac_vacuum_timestamp;	/* autovacuum initiated */
	PgStat_Counter autovac_vacuum_count;
	TimestampTz analyze_timestamp;	/* user initiated */
	PgStat_Counter analyze_count;
	TimestampTz autovac_analyze_timestamp;	/* autovacuum initiated */
	PgStat_Counter autovac_analyze_count;
} PgStat_StatTabEntry;

#ifdef YB_TODO
/* YB_TODO(neil) Postgres no longer uses the following structures. */

/* ----------
 * Wait Events - Timeout
 *
 * Use this category when a process is waiting for a timeout to expire.
 * ----------
 */
typedef enum
{
	WAIT_EVENT_BASE_BACKUP_THROTTLE = PG_WAIT_TIMEOUT,
	WAIT_EVENT_PG_SLEEP,
	WAIT_EVENT_RECOVERY_APPLY_DELAY,
	WAIT_EVENT_YB_TXN_CONFLICT_BACKOFF
} WaitEventTimeout;

/* ----------
 * Command type for progress reporting purposes
 * ----------
 */
typedef enum ProgressCommandType
{
	PROGRESS_COMMAND_INVALID,
	PROGRESS_COMMAND_VACUUM,
	PROGRESS_COMMAND_COPY,
	PROGRESS_COMMAND_CREATE_INDEX
} ProgressCommandType;

#define PGSTAT_NUM_PROGRESS_PARAM	17

#ifdef YB_TODO
/*
 * YbPgBackendCatalogVersionStatus
 *
 * Each live backend maintains a YbPgBackendCatalogVersionStatus struct in
 * shared memory indicating what catalog version it is at.  A backend in the
 * middle of a query or transaction uses a consistent snapshot of the system
 * catalog (technically, only the cache does, not direct reads/writes to/from
 * system catalog).  The catalog version indicates that snapshot.  has_version
 * is false for backends that are idle (and not in txn) or non-client backends.
 */
typedef struct YbPgBackendCatalogVersionStatus
{
	bool		has_version;	/* whether the backend is using the following
								   version */
	uint64_t	version;		/* if has_version, catalog version that the
								   backend is on */
} YbPgBackendCatalogVersionStatus;


/* YB_TODO(neil) Pg15 Move this to utils/backend_status.h ?? */
/* ----------
 * PgBackendStatus
 *
 * Each live backend maintains a PgBackendStatus struct in shared memory
 * showing its current activity.  (The structs are allocated according to
 * BackendId, but that is not critical.)  Note that the collector process
 * has no involvement in, or even access to, these structs.
 *
 * Each auxiliary process also maintains a PgBackendStatus struct in shared
 * memory.
 * ----------
 */
typedef struct PgBackendStatus
{
	/*
	 * To avoid locking overhead, we use the following protocol: a backend
	 * increments st_changecount before modifying its entry, and again after
	 * finishing a modification.  A would-be reader should note the value of
	 * st_changecount, copy the entry into private memory, then check
	 * st_changecount again.  If the value hasn't changed, and if it's even,
	 * the copy is valid; otherwise start over.  This makes updates cheap
	 * while reads are potentially expensive, but that's the tradeoff we want.
	 *
	 * The above protocol needs memory barriers to ensure that the apparent
	 * order of execution is as it desires.  Otherwise, for example, the CPU
	 * might rearrange the code so that st_changecount is incremented twice
	 * before the modification on a machine with weak memory ordering.  Hence,
	 * use the macros defined below for manipulating st_changecount, rather
	 * than touching it directly.
	 */
	int			st_changecount;

	/* The entry is valid iff st_procpid > 0, unused if st_procpid == 0 */
	int			st_procpid;

	/* Type of backends */
	BackendType st_backendType;

	/* Times when current backend, transaction, and activity started */
	TimestampTz st_proc_start_timestamp;
	TimestampTz st_xact_start_timestamp;
	TimestampTz st_activity_start_timestamp;
	TimestampTz st_state_start_timestamp;

	/* Database OID, owning user's OID, connection client address */
	Oid			st_databaseid;
	Oid			st_userid;
	SockAddr	st_clientaddr;
	char	   *st_clienthostname;	/* MUST be null-terminated */
	char 		*st_databasename; /* Used in YB Mode */

	/* Information about SSL connection */
	bool		st_ssl;
	PgBackendSSLStatus *st_sslstatus;

	/* current state */
	BackendState st_state;

	/* application name; MUST be null-terminated */
	char	   *st_appname;

	/*
	 * Current command string; MUST be null-terminated. Note that this string
	 * possibly is truncated in the middle of a multi-byte character. As
	 * activity strings are stored more frequently than read, that allows to
	 * move the cost of correct truncation to the display side. Use
	 * pgstat_clip_activity() to truncate correctly.
	 */
	char	   *st_activity_raw;

	/*
	 * Command progress reporting.  Any command which wishes can advertise
	 * that it is running by setting st_progress_command,
	 * st_progress_command_target, and st_progress_param[].
	 * st_progress_command_target should be the OID of the relation which the
	 * command targets (we assume there's just one, as this is meant for
	 * utility commands), but the meaning of each element in the
	 * st_progress_param array is command-specific.
	 */
	ProgressCommandType st_progress_command;
	Oid			st_progress_command_target;
	int64		st_progress_param[PGSTAT_NUM_PROGRESS_PARAM];

	/*
	 * Memory usage of backend from TCMalloc, including PostgreSQL memory usage
	 * + pggate memory usage + cached memory - memory that was freed but not recycled
	 */
	int64_t yb_st_allocated_mem_bytes;

	/* YB catalog version */
	YbPgBackendCatalogVersionStatus yb_st_catalog_version;
} PgBackendStatus;
#endif

/* ----------
 * LocalPgBackendStatus
 *
 * When we build the backend status array, we use LocalPgBackendStatus to be
 * able to add new values to the struct when needed without adding new fields
 * to the shared memory. It contains the backend status as a first member.
 * ----------
 */
typedef struct LocalPgBackendStatus
{
	/*
	 * Local version of the backend status entry.
	 */
	PgBackendStatus backendStatus;

	/*
	 * The xid of the current transaction if available, InvalidTransactionId
	 * if not.
	 */
	TransactionId backend_xid;

	/*
	 * The xmin of the current session if available, InvalidTransactionId if
	 * not.
	 */
	TransactionId backend_xmin;

	/* Backend's RSS memory usage */
	int64_t yb_backend_rss_mem_bytes;
} LocalPgBackendStatus;

#endif

typedef struct PgStat_WalStats
{
	PgStat_Counter wal_records;
	PgStat_Counter wal_fpi;
	uint64		wal_bytes;
	PgStat_Counter wal_buffers_full;
	PgStat_Counter wal_write;
	PgStat_Counter wal_sync;
	PgStat_Counter wal_write_time;
	PgStat_Counter wal_sync_time;
	TimestampTz stat_reset_timestamp;
} PgStat_WalStats;

/*
 * Functions in pgstat.c
 */

/* functions called from postmaster */
extern Size StatsShmemSize(void);
extern void StatsShmemInit(void);

/* Functions called during server startup / shutdown */
extern void pgstat_restore_stats(void);
extern void pgstat_discard_stats(void);
extern void pgstat_before_server_shutdown(int code, Datum arg);

/* Functions for backend initialization */
extern void pgstat_initialize(void);

/* Functions called from backends */
extern long pgstat_report_stat(bool force);
extern void pgstat_force_next_flush(void);

extern void pgstat_reset_counters(void);
extern void pgstat_reset(PgStat_Kind kind, Oid dboid, Oid objectid);
extern void pgstat_reset_of_kind(PgStat_Kind kind);

/* stats accessors */
extern void pgstat_clear_snapshot(void);
extern TimestampTz pgstat_get_stat_snapshot_timestamp(bool *have_snapshot);

/* helpers */
extern PgStat_Kind pgstat_get_kind_from_str(char *kind_str);
extern bool pgstat_have_entry(PgStat_Kind kind, Oid dboid, Oid objoid);

/*
 * Functions in pgstat_archiver.c
 */

extern void pgstat_report_archiver(const char *xlog, bool failed);
extern PgStat_ArchiverStats *pgstat_fetch_stat_archiver(void);

/*
 * Functions in pgstat_bgwriter.c
 */

extern void pgstat_report_bgwriter(void);
extern PgStat_BgWriterStats *pgstat_fetch_stat_bgwriter(void);


/*
 * Functions in pgstat_checkpointer.c
 */

extern void pgstat_report_checkpointer(void);
extern PgStat_CheckpointerStats *pgstat_fetch_stat_checkpointer(void);


/*
 * Functions in pgstat_database.c
 */

extern void pgstat_drop_database(Oid databaseid);
extern void pgstat_report_autovac(Oid dboid);
extern void pgstat_report_recovery_conflict(int reason);
extern void pgstat_report_deadlock(void);
extern void pgstat_report_checksum_failures_in_db(Oid dboid, int failurecount);
extern void pgstat_report_checksum_failure(void);
extern void pgstat_report_connect(Oid dboid);
extern void yb_pgstat_clear_entry_pid(int pid);
#ifdef YB_TODO
/* Postgres changed the implemenation for stats. Need to rework. */
extern void pgstat_report_query_termination(const char *termination_reason,
											int32 backend_pid);
#endif

#define pgstat_count_buffer_read_time(n)							\
	(pgStatBlockReadTime += (n))
#define pgstat_count_buffer_write_time(n)							\
	(pgStatBlockWriteTime += (n))
#define pgstat_count_conn_active_time(n)							\
	(pgStatActiveTime += (n))
#define pgstat_count_conn_txn_idle_time(n)							\
	(pgStatTransactionIdleTime += (n))

extern PgStat_StatDBEntry *pgstat_fetch_stat_dbentry(Oid dbid);

/*
 * Functions in pgstat_function.c
 */

extern void pgstat_create_function(Oid proid);
extern void pgstat_drop_function(Oid proid);

/* YB_TODO(ted@yugabyte) Need to look at all related code every time we merge. */
struct FunctionCallInfoBaseData;

extern void pgstat_init_function_usage_org(struct FunctionCallInfoBaseData *fcinfo,
										   PgStat_FunctionCallUsage *fcu);
/* YB_TODO(neil) Need to remove Ted's call structure */
extern void pgstat_init_function_usage(struct FunctionCallInfoBaseData *fcinfo,
									   PgStat_FunctionCallUsage *fcu);
extern void pgstat_end_function_usage(PgStat_FunctionCallUsage *fcu,
									  bool finalize);

extern PgStat_StatFuncEntry *pgstat_fetch_stat_funcentry(Oid funcid);
extern PgStat_BackendFunctionEntry *find_funcstat_entry(Oid func_id);

#ifdef YB_TODO
/* YB_TODO(neil) These functions are no longer used in Pg15 */
/* ----------
 * pgstat_report_wait_end_for_proc(PGPROC *proc) -
 *
 *	Called to report end of a wait for a specific process.
 *
 * NB: this *must* be able to survive being called before MyProc has been
 * initialized.
 * ----------
 */
static inline void
pgstat_report_wait_end_for_proc(volatile PGPROC *proc)
{
	if (!pgstat_track_activities || !proc)
		return;

	/*
	 * Since this is a four-byte field which is always read and written as
	 * four-bytes, updates are atomic.
	 */
	proc->wait_event_info = 0;
}


/* ----------
 * pgstat_report_wait_end() -
 *
 *	Called to report end of a wait.
 *
 * NB: this *must* be able to survive being called before MyProc has been
 * initialized.
 * ----------
 */
static inline void
pgstat_report_wait_end(void)
{
	return pgstat_report_wait_end_for_proc(MyProc);
}
#endif

/*
 * Functions in pgstat_relation.c
 */

extern void pgstat_create_relation(Relation rel);
extern void pgstat_drop_relation(Relation rel);
extern void pgstat_copy_relation_stats(Relation dstrel, Relation srcrel);

extern void pgstat_init_relation(Relation rel);
extern void pgstat_assoc_relation(Relation rel);
extern void pgstat_unlink_relation(Relation rel);

extern void pgstat_report_vacuum(Oid tableoid, bool shared,
								 PgStat_Counter livetuples, PgStat_Counter deadtuples);
extern void pgstat_report_analyze(Relation rel,
								  PgStat_Counter livetuples, PgStat_Counter deadtuples,
								  bool resetcounter);

/*
 * If stats are enabled, but pending data hasn't been prepared yet, call
 * pgstat_assoc_relation() to do so. See its comment for why this is done
 * separately from pgstat_init_relation().
 */
#define pgstat_should_count_relation(rel)                           \
	(likely((rel)->pgstat_info != NULL) ? true :                    \
	 ((rel)->pgstat_enabled ? pgstat_assoc_relation(rel), true : false))

/* nontransactional event counts are simple enough to inline */

#define pgstat_count_heap_scan(rel)									\
	do {															\
		if (pgstat_should_count_relation(rel))						\
			(rel)->pgstat_info->t_counts.t_numscans++;				\
	} while (0)
#define pgstat_count_heap_getnext(rel)								\
	do {															\
		if (pgstat_should_count_relation(rel))						\
			(rel)->pgstat_info->t_counts.t_tuples_returned++;		\
	} while (0)
#define pgstat_count_heap_fetch(rel)								\
	do {															\
		if (pgstat_should_count_relation(rel))						\
			(rel)->pgstat_info->t_counts.t_tuples_fetched++;		\
	} while (0)
#define pgstat_count_index_scan(rel)								\
	do {															\
		if (pgstat_should_count_relation(rel))						\
			(rel)->pgstat_info->t_counts.t_numscans++;				\
	} while (0)
#define pgstat_count_index_tuples(rel, n)							\
	do {															\
		if (pgstat_should_count_relation(rel))						\
			(rel)->pgstat_info->t_counts.t_tuples_returned += (n);	\
	} while (0)
#define pgstat_count_buffer_read(rel)								\
	do {															\
		if (pgstat_should_count_relation(rel))						\
			(rel)->pgstat_info->t_counts.t_blocks_fetched++;		\
	} while (0)
#define pgstat_count_buffer_hit(rel)								\
	do {															\
		if (pgstat_should_count_relation(rel))						\
			(rel)->pgstat_info->t_counts.t_blocks_hit++;			\
	} while (0)

extern void pgstat_count_heap_insert(Relation rel, PgStat_Counter n);
extern void pgstat_count_heap_update(Relation rel, bool hot);
extern void pgstat_count_heap_delete(Relation rel);
extern void pgstat_count_truncate(Relation rel);
extern void pgstat_update_heap_dead_tuples(Relation rel, int delta);

extern void pgstat_twophase_postcommit(TransactionId xid, uint16 info,
									   void *recdata, uint32 len);
extern void pgstat_twophase_postabort(TransactionId xid, uint16 info,
									  void *recdata, uint32 len);
extern PgStat_StatTabEntry *pgstat_fetch_stat_tabentry(Oid relid);
extern PgStat_StatTabEntry *pgstat_fetch_stat_tabentry_ext(bool shared,
														   Oid relid);
extern PgStat_TableStatus *find_tabstat_entry(Oid rel_id);

/*
 * Functions in pgstat_replslot.c
 */

extern void pgstat_reset_replslot(const char *name);
struct ReplicationSlot;
extern void pgstat_report_replslot(struct ReplicationSlot *slot, const PgStat_StatReplSlotEntry *repSlotStat);
extern void pgstat_create_replslot(struct ReplicationSlot *slot);
extern void pgstat_acquire_replslot(struct ReplicationSlot *slot);
extern void pgstat_drop_replslot(struct ReplicationSlot *slot);
extern PgStat_StatReplSlotEntry *pgstat_fetch_replslot(NameData slotname);


/*
 * Functions in pgstat_slru.c
 */

extern void pgstat_reset_slru(const char *);
extern void pgstat_count_slru_page_zeroed(int slru_idx);
extern void pgstat_count_slru_page_hit(int slru_idx);
extern void pgstat_count_slru_page_read(int slru_idx);
extern void pgstat_count_slru_page_written(int slru_idx);
extern void pgstat_count_slru_page_exists(int slru_idx);
extern void pgstat_count_slru_flush(int slru_idx);
extern void pgstat_count_slru_truncate(int slru_idx);
extern const char *pgstat_get_slru_name(int slru_idx);
extern int	pgstat_get_slru_index(const char *name);
extern PgStat_SLRUStats *pgstat_fetch_slru(void);


/*
 * Functions in pgstat_subscription.c
 */

extern void pgstat_report_subscription_error(Oid subid, bool is_apply_error);
extern void pgstat_create_subscription(Oid subid);
extern void pgstat_drop_subscription(Oid subid);
extern PgStat_StatSubEntry *pgstat_fetch_stat_subscription(Oid subid);


/*
 * Functions in pgstat_xact.c
 */

extern void AtEOXact_PgStat(bool isCommit, bool parallel);
extern void AtEOSubXact_PgStat(bool isCommit, int nestDepth);
extern void AtPrepare_PgStat(void);
extern void PostPrepare_PgStat(void);
struct xl_xact_stats_item;
extern int	pgstat_get_transactional_drops(bool isCommit, struct xl_xact_stats_item **items);
extern void pgstat_execute_transactional_drops(int ndrops, struct xl_xact_stats_item *items, bool is_redo);


/*
 * Functions in pgstat_wal.c
 */

extern void pgstat_report_wal(bool force);
extern PgStat_WalStats *pgstat_fetch_stat_wal(void);


/*
 * Variables in pgstat.c
 */

/* GUC parameters */
extern PGDLLIMPORT bool pgstat_track_counts;
extern PGDLLIMPORT int pgstat_track_functions;
extern PGDLLIMPORT int pgstat_fetch_consistency;
extern char *pgstat_ybstat_filename;
extern char *pgstat_ybstat_tmpname;

/*
 * Metric to track number of sql connections established since
 * postmaster started.
 */
extern uint64_t *yb_new_conn;

/*
 * Variables in pgstat_bgwriter.c
 */

/* updated directly by bgwriter and bufmgr */
extern PGDLLIMPORT PgStat_BgWriterStats PendingBgWriterStats;


/*
 * Variables in pgstat_checkpointer.c
 */

/*
 * Checkpointer statistics counters are updated directly by checkpointer and
 * bufmgr.
 */
extern PGDLLIMPORT PgStat_CheckpointerStats PendingCheckpointerStats;


/*
 * Variables in pgstat_database.c
 */

/* Updated by pgstat_count_buffer_*_time macros */
extern PGDLLIMPORT PgStat_Counter pgStatBlockReadTime;
extern PGDLLIMPORT PgStat_Counter pgStatBlockWriteTime;

/*
 * Updated by pgstat_count_conn_*_time macros, called by
 * pgstat_report_activity().
 */
extern PGDLLIMPORT PgStat_Counter pgStatActiveTime;
extern PGDLLIMPORT PgStat_Counter pgStatTransactionIdleTime;

/* updated by the traffic cop and in errfinish() */
extern PGDLLIMPORT SessionEndType pgStatSessionEndCause;


/*
 * Variables in pgstat_wal.c
 */

/* updated directly by backends and background processes */
extern PGDLLIMPORT PgStat_WalStats PendingWalStats;

#ifdef YB_TODO
/* YB_TODO This function need new implementation to match with Postgres 15. */
/* ----------
 * YB functions called from backends
 * ----------
 */
extern PgBackendStatus *getBackendStatusArray(void);
extern void yb_pgstat_report_allocated_mem_bytes(void);
extern void yb_pgstat_set_catalog_version(uint64_t catalog_version);
extern void yb_pgstat_set_has_catalog_version(bool has_catalog_version);

extern PgStat_YBStatQueryEntry *pgstat_fetch_ybstat_queries(Oid db_oid, size_t* num_queries);
#endif

#endif							/* PGSTAT_H */
