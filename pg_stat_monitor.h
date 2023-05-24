/*-------------------------------------------------------------------------
 *
 * pg_stat_monitor.h
 *		Track statement execution times across a whole database cluster.
 *
 * Portions Copyright © 2018-2020, Percona LLC and/or its affiliates
 *
 * Portions Copyright (c) 1996-2020, PostgreSQL Global Development Group
 *
 * Portions Copyright (c) 1994, The Regents of the University of California
 *
 * IDENTIFICATION
 *	  contrib/pg_stat_monitor/pg_stat_monitor.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef __PG_STAT_MONITOR_H__
#define __PG_STAT_MONITOR_H__

#include "postgres.h"

#include <arpa/inet.h>
#include <math.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <sys/resource.h>

#include "lib/dshash.h"
#include "utils/dsa.h"

#include "access/hash.h"
#include "catalog/pg_authid.h"
#include "executor/instrument.h"
#include "common/ip.h"
#include "jit/jit.h"
#include "funcapi.h"
#include "access/twophase.h"
#include "mb/pg_wchar.h"
#include "miscadmin.h"
#include "optimizer/planner.h"
#include "postmaster/bgworker.h"
#include "parser/analyze.h"
#include "parser/parsetree.h"
#include "parser/scanner.h"
#include "parser/scansup.h"
#include "pgstat.h"
#include "storage/fd.h"
#include "storage/ipc.h"
#include "storage/spin.h"
#include "tcop/utility.h"
#include "utils/acl.h"
#include "utils/builtins.h"
#include "utils/memutils.h"
#include "utils/timestamp.h"
#include "utils/lsyscache.h"
#include "utils/guc.h"
#include "utils/guc_tables.h"
#include "utils/memutils.h"
#include "utils/palloc.h"


#define MAX_BACKEND_PROCESES (MaxBackends + NUM_AUXILIARY_PROCS + max_prepared_xacts)
#define  IntArrayGetTextDatum(x,y) intarray_get_datum(x,y)

/* XXX: Should USAGE_EXEC reflect execution time and/or buffer usage? */
#define USAGE_EXEC(duration)	(1.0)
#define USAGE_INIT				(1.0)	/* including initial planning */
#define ASSUMED_LENGTH_INIT		1024	/* initial assumed mean query length */
#define USAGE_DECREASE_FACTOR	(0.99)	/* decreased every entry_dealloc */
#define STICKY_DECREASE_FACTOR	(0.50)	/* factor for sticky entries */
#define USAGE_DEALLOC_PERCENT	5	/* free this % of entries at once */

#define JUMBLE_SIZE				1024	/* query serialization buffer size */

#define HISTOGRAM_MAX_TIME		50000000
#define MAX_RESPONSE_BUCKET 50
#define INVALID_BUCKET_ID	-1
#define TEXT_LEN			255
#define ERROR_MESSAGE_LEN	100
#define REL_TYPENAME_LEN	64
#define REL_LST				10
#define REL_LEN				132 /* REL_TYPENAME_LEN * 2 (relname + schema) + 1
								 * (for view indication) + 1 and dot and
								 * string terminator */
#define CMD_LST				10
#define CMD_LEN				20
#define APPLICATIONNAME_LEN	NAMEDATALEN
#define COMMENTS_LEN        256
#define PGSM_OVER_FLOW_MAX	10
#define PLAN_TEXT_LEN		1024
/* the assumption of query max nested level */
#define DEFAULT_MAX_NESTED_LEVEL	10

#define MAX_QUERY_BUF						(pgsm_query_shared_buffer * 1024 * 1024)
#define MAX_BUCKETS_MEM 					(pgsm_max * 1024 * 1024)
#define BUCKETS_MEM_OVERFLOW() 				((hash_get_num_entries(pgsm_hash) * sizeof(pgsmEntry)) >= MAX_BUCKETS_MEM)
#define MAX_BUCKET_ENTRIES 					(MAX_BUCKETS_MEM / sizeof(pgsmEntry))
#define QUERY_BUFFER_OVERFLOW(x,y)  		((x + y + sizeof(uint64) + sizeof(uint64)) > MAX_QUERY_BUF)
#define QUERY_MARGIN 						100
#define MIN_QUERY_LEN						10
#define SQLCODE_LEN                         20
#define TOTAL_RELS_LENGTH					(REL_LST * REL_LEN)

#if PG_VERSION_NUM >= 130000
#define	MAX_SETTINGS                        15
#else
#define MAX_SETTINGS                        14
#endif

/* Update this if need a enum GUC with more options. */
#define MAX_ENUM_OPTIONS 6

/*
 * API for disabling error capture ereport(ERROR,..) by PGSM's error capture hook
 * pgsm_emit_log_hook()
 *
 * Use these macros as follows:
 * 		 	PGSM_DISABLE_ERROR_CAPUTRE();
 * 			{
 * 				... code that might throw ereport(ERROR) ...
 * 			}PGSM_END_DISABLE_ERROR_CAPTURE();
 *
 * These macros can be used to error recursion if the error gets
 * thrown from within the function called from pgsm_emit_log_hook()
 */
extern volatile bool __pgsm_do_not_capture_error;
#define PGSM_DISABLE_ERROR_CAPUTRE() \
	do { \
		__pgsm_do_not_capture_error = true

#define PGSM_END_DISABLE_ERROR_CAPTURE() \
	__pgsm_do_not_capture_error = false; \
	} while (0)

#define PGSM_ERROR_CAPTURE_ENABLED \
	__pgsm_do_not_capture_error == false

/*
 * pg_stat_monitor uses the hash structure to store all query statistics
 * except the query text, which gets stored out of line in the raw DSA area.
 * Enabling USE_DYNAMIC_HASH uses the dshash for storing the query statistics
 * that get created in the DSA area and can grow to any size.
 *
 * The only issue with using the dshash is that the newly created hash entries
 * are explicitly locked by dshash, and its caller is required to release the lock.
 * That works well as long as we do not want to swallow the errors thrown from
 * dshash function. Since the lightweight locks acquired internally by dshash
 * automatically get released by error.
 * But throwing an error from pg_stat_monitor would mean erroring out the user query,
 * which is not acceptable for any stat collector extension.
 *
 * Moreover, some of the pg_stat_monitor functions perform the sequence scan on the
 * hash table, while the sequence scan support for dshash table is only available
 * for PG 15 and onwards.
 * So until we figure out the way to release the locks acquired internally by dshash
 * in case of an error while ignoring the error at the same time, we will keep using
 * the classic shared memory hash table.
 */
#ifdef USE_DYNAMIC_HASH
#define	PGSM_HASH_TABLE	dshash_table
#define	PGSM_HASH_TABLE_HANDLE	dshash_table_handle
#define	PGSM_HASH_SEQ_STATUS	dshash_seq_status
#else
#define	PGSM_HASH_TABLE	HTAB
#define	PGSM_HASH_TABLE_HANDLE	HTAB*
#define	PGSM_HASH_SEQ_STATUS	HASH_SEQ_STATUS
#endif


#if PG_VERSION_NUM < 130000
typedef struct WalUsage
{
	long		wal_records;	/* # of WAL records produced */
	long		wal_fpi;		/* # of WAL full page images produced */
	uint64		wal_bytes;		/* size of WAL records produced */
} WalUsage;
#endif


typedef enum pgsmStoreKind
{
	PGSM_INVALID = -1,

	/*
	 * PGSM_PLAN and PGSM_EXEC must be respectively 0 and 1 as they're used to
	 * reference the underlying values in the arrays in the Counters struct,
	 * and this order is required in pg_stat_monitor_internal().
	 */
	PGSM_PARSE = 0,
	PGSM_PLAN,
	PGSM_EXEC,
	PGSM_STORE,
	PGSM_ERROR,

	PGSM_NUMKIND				/* Must be last value of this enum */
}			pgsmStoreKind;

/* the assumption of query max nested level */
#define DEFAULT_MAX_NESTED_LEVEL	10

/*
 * Type of aggregate keys
 */
typedef enum AGG_KEY
{
	AGG_KEY_DATABASE = 0,
	AGG_KEY_USER,
	AGG_KEY_HOST
}			AGG_KEY;

#define MAX_QUERY_LEN 1024

/* shared memory storage for the query */
typedef struct CallTime
{
	double		total_time;		/* total execution time, in msec */
	double		min_time;		/* minimum execution time in msec */
	double		max_time;		/* maximum execution time in msec */
	double		mean_time;		/* mean execution time in msec */
	double		sum_var_time;	/* sum of variances in execution time in msec */
}			CallTime;


typedef struct PlanInfo
{
	uint64		planid;			/* plan identifier */
	char		plan_text[PLAN_TEXT_LEN];	/* plan text */
	size_t		plan_len;		/* strlen(plan_text) */
}			PlanInfo;

typedef struct pgsmHashKey
{
	uint64		bucket_id;		/* bucket number */
	uint64		queryid;		/* query identifier */
	uint64		planid;			/* plan identifier */
	uint64		appid;			/* hash of application name */
	Oid			userid;			/* user OID */
	Oid			dbid;			/* database OID */
	uint32		ip;				/* client ip address */
	bool		toplevel;		/* query executed at top level */
}			pgsmHashKey;

typedef struct QueryInfo
{
	uint64		parentid;		/* parent queryid of current query */
	dsa_pointer parent_query;
	int64		type;			/* type of query, options are query, info,
								 * warning, error, fatal */
	char		application_name[APPLICATIONNAME_LEN];
	char		comments[COMMENTS_LEN];
	char		relations[REL_LST][REL_LEN];	/* List of relation involved
												 * in the query */
	int			num_relations;	/* Number of relation in the query */
	CmdType		cmd_type;		/* query command type
								 * SELECT/UPDATE/DELETE/INSERT */
} QueryInfo;

typedef struct ErrorInfo
{
	int64		elevel;			/* error elevel */
	char		sqlcode[SQLCODE_LEN];	/* error sqlcode  */
	char		message[ERROR_MESSAGE_LEN]; /* error message text */
}			ErrorInfo;

typedef struct Calls
{
	int64		calls;			/* # of times executed */
	int64		rows;			/* total # of retrieved or affected rows */
	double		usage;			/* usage factor */
}			Calls;


typedef struct Blocks
{
	int64		shared_blks_hit;	/* # of shared buffer hits */
	int64		shared_blks_read;	/* # of shared disk blocks read */
	int64		shared_blks_dirtied;	/* # of shared disk blocks dirtied */
	int64		shared_blks_written;	/* # of shared disk blocks written */
	int64		local_blks_hit; /* # of local buffer hits */
	int64		local_blks_read;	/* # of local disk blocks read */
	int64		local_blks_dirtied; /* # of local disk blocks dirtied */
	int64		local_blks_written; /* # of local disk blocks written */
	int64		temp_blks_read; /* # of temp blocks read */
	int64		temp_blks_written;	/* # of temp blocks written */
	double		blk_read_time;	/* time spent reading, in msec */
	double		blk_write_time; /* time spent writing, in msec */

	double		temp_blk_read_time; /* time spent reading temp blocks, in msec */
	double		temp_blk_write_time;	/* time spent writing temp blocks, in
										 * msec */

	/*
	 * Variables for local entry. The values to be passed to pgsm_update_entry
	 * from pgsm_store.
	 */
	instr_time	instr_blk_read_time;	/* time spent reading blocks */
	instr_time	instr_blk_write_time;	/* time spent writing blocks */
	instr_time	instr_temp_blk_read_time;	/* time spent reading temp blocks */
	instr_time	instr_temp_blk_write_time;	/* time spent writing temp blocks */
}			Blocks;

typedef struct JitInfo
{
	int64		jit_functions;	/* total number of JIT functions emitted */
	double		jit_generation_time;	/* total time to generate jit code */
	int64		jit_inlining_count; /* number of times inlining time has been
									 * > 0 */
	double		jit_inlining_time;	/* total time to inline jit code */
	int64		jit_optimization_count; /* number of times optimization time
										 * has been > 0 */
	double		jit_optimization_time;	/* total time to optimize jit code */
	int64		jit_emission_count; /* number of times emission time has been
									 * > 0 */
	double		jit_emission_time;	/* total time to emit jit code */

	/*
	 * Variables for local entry. The values to be passed to pgsm_update_entry
	 * from pgsm_store.
	 */
	instr_time	instr_generation_counter;	/* generation counter */
	instr_time	instr_inlining_counter; /* inlining counter */
	instr_time	instr_optimization_counter; /* optimization counter */
	instr_time	instr_emission_counter; /* emission counter */
}			JitInfo;

typedef struct SysInfo
{
	double		utime;			/* user cpu time */
	double		stime;			/* system cpu time */
}			SysInfo;

typedef struct Wal_Usage
{
	int64		wal_records;	/* # of WAL records generated */
	int64		wal_fpi;		/* # of WAL full page images generated */
	uint64		wal_bytes;		/* total amount of WAL bytes generated */
}			Wal_Usage;

typedef struct Counters
{
	Calls		calls;
	QueryInfo	info;
	CallTime	time;

	Calls		plancalls;
	CallTime	plantime;
	PlanInfo	planinfo;

	Blocks		blocks;
	SysInfo		sysinfo;
	JitInfo		jitinfo;
	ErrorInfo	error;
	Wal_Usage	walusage;
	int			resp_calls[MAX_RESPONSE_BUCKET];	/* execution time's in
													 * msec */
} Counters;

/* Some global structure to get the cpu usage, really don't like the idea of global variable */

/*
 * Statistics per statement
 */
typedef struct pgsmEntry
{
	pgsmHashKey key;			/* hash key of entry - MUST BE FIRST */
	uint64		pgsm_query_id;	/* pgsm generate normalized query hash */
	char		datname[NAMEDATALEN];	/* database name */
	char		username[NAMEDATALEN];	/* user name */
	Counters	counters;		/* the statistics for this query */
	int			encoding;		/* query text encoding */
	slock_t		mutex;			/* protects the counters only */
	union
	{
		dsa_pointer query_pos;	/* query location within query buffer */
		char	   *query_pointer;
	}			query_text;
}			pgsmEntry;

/*
 * Global shared state
 */
typedef struct pgsmSharedState
{
	LWLock	   *lock;			/* protects hashtable search/modification */
	slock_t		mutex;			/* protects following fields only: */
	pg_atomic_uint64 current_wbucket;
	pg_atomic_uint64 prev_bucket_sec;
	int			hash_tranche_id;
	void	   *raw_dsa_area;	/* DSA area pointer to store query texts.
								 * dshash also lives in this memory when
								 * USE_DYNAMIC_HASH is enabled */
	PGSM_HASH_TABLE_HANDLE hash_handle;

	/*
	 * hash table handle. can be either classic shared memory hash or dshash
	 * (if we are using USE_DYNAMIC_HASH)
	 */

	bool		pgsm_oom;
	TimestampTz bucket_start_time[]; /* start time of the bucket */
}			pgsmSharedState;

typedef struct pgsmLocalState
{
	pgsmSharedState *shared_pgsmState;
	dsa_area   *dsa;			/* local dsa area for backend attached to the
								 * dsa area created by postmaster at startup. */
	PGSM_HASH_TABLE *shared_hash;
    MemoryContext pgsm_mem_cxt;

}			pgsmLocalState;

#if PG_VERSION_NUM < 140000
/*
 * Struct for tracking locations/lengths of constants during normalization
 */
typedef struct LocationLen
{
	int			location;		/* start offset in query text */
	int			length;			/* length in bytes, or -1 to ignore */
} LocationLen;

/*
 * Working state for computing a query jumble and producing a normalized
 * query string
 */
typedef struct JumbleState
{
	/* Jumble of current query tree */
	unsigned char *jumble;

	/* Number of bytes used in jumble[] */
	Size		jumble_len;

	/* Array of locations of constants that should be removed */
	LocationLen *clocations;

	/* Allocated length of clocations array */
	int			clocations_buf_size;

	/* Current number of valid entries in clocations array */
	int			clocations_count;

	/* highest Param id we've seen, in order to start normalization correctly */
	int			highest_extern_param_id;
} JumbleState;
#endif

/* guc.c */
void		init_guc(void);

/* hash_create.c */
dsa_area   *get_dsa_area_for_query_text(void);
PGSM_HASH_TABLE *get_pgsmHash(void);

void		pgsm_attach_shmem(void);
bool		IsHashInitialize(void);
bool		IsSystemOOM(void);
void		pgsm_shmem_startup(void);
void		pgsm_shmem_shutdown(int code, Datum arg);
int			pgsm_get_bucket_size(void);
pgsmSharedState *pgsm_get_ss(void);
void		hash_query_entries();
void		hash_query_entry_dealloc(int new_bucket_id, int old_bucket_id, unsigned char *query_buffer[]);
void		hash_entry_dealloc(int new_bucket_id, int old_bucket_id, unsigned char *query_buffer);
pgsmEntry  *hash_entry_alloc(pgsmSharedState * pgsm, pgsmHashKey * key, int encoding);
Size		pgsm_ShmemSize(void);
void		pgsm_startup(void);

/* hash_query.c */
void		pgsm_startup(void);
MemoryContext GetPgsmMemoryContext(void);

/* guc.c */
void		init_guc(void);

/* GUC variables*/
/*---- GUC variables ----*/
typedef enum
{
	PSGM_TRACK_NONE = 0,		/* track no statements */
	PGSM_TRACK_TOP,				/* only top level statements */
	PGSM_TRACK_ALL				/* all statements, including nested ones */
}			PGSMTrackLevel;
static const struct config_enum_entry track_options[] =
{
	{"none", PSGM_TRACK_NONE, false},
	{"top", PGSM_TRACK_TOP, false},
	{"all", PGSM_TRACK_ALL, false},
	{NULL, 0, false}
};

typedef enum
{
	HISTOGRAM_START,
	HISTOGRAM_END,
	HISTOGRAM_COUNT
}			HistogramTimingType;

extern int	pgsm_max;
extern int	pgsm_query_max_len;
extern int	pgsm_bucket_time;
extern int	pgsm_max_buckets;
extern int	pgsm_histogram_buckets;
extern double pgsm_histogram_min;
extern double pgsm_histogram_max;
extern int	pgsm_query_shared_buffer;
extern bool pgsm_track_planning;
extern bool pgsm_extract_comments;
extern bool pgsm_enable_query_plan;
extern bool pgsm_enable_overflow;
extern bool pgsm_normalized_query;
extern bool pgsm_track_utility;
extern bool pgsm_enable_pgsm_query_id;
extern int	pgsm_track;

#define DECLARE_HOOK(hook, ...) \
        static hook(__VA_ARGS__);
#define HOOK(name) name
#define HOOK_STATS_SIZE 0
#endif

void	   *pgsm_hash_find_or_insert(PGSM_HASH_TABLE * shared_hash, pgsmHashKey * key, bool *found);
void	   *pgsm_hash_find(PGSM_HASH_TABLE * shared_hash, pgsmHashKey * key, bool *found);
void		pgsm_hash_seq_init(PGSM_HASH_SEQ_STATUS * hstat, PGSM_HASH_TABLE * shared_hash, bool lock);
void	   *pgsm_hash_seq_next(PGSM_HASH_SEQ_STATUS * hstat);
void		pgsm_hash_seq_term(PGSM_HASH_SEQ_STATUS * hstat);
void		pgsm_hash_delete_current(PGSM_HASH_SEQ_STATUS * hstat, PGSM_HASH_TABLE * shared_hash, void *key);
