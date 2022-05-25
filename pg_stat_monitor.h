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

#include "access/hash.h"
#include "catalog/pg_authid.h"
#include "executor/instrument.h"
#include "common/ip.h"
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

#define MAX_BACKEND_PROCESES (MaxBackends + NUM_AUXILIARY_PROCS + max_prepared_xacts)
#define  IntArrayGetTextDatum(x,y) intarray_get_datum(x,y)

/* XXX: Should USAGE_EXEC reflect execution time and/or buffer usage? */
#define USAGE_EXEC(duration)	(1.0)
#define USAGE_INIT				(1.0)	/* including initial planning */
#define ASSUMED_MEDIAN_INIT		(10.0)	/* initial assumed median usage */
#define ASSUMED_LENGTH_INIT		1024	/* initial assumed mean query length */
#define USAGE_DECREASE_FACTOR	(0.99)	/* decreased every entry_dealloc */
#define STICKY_DECREASE_FACTOR	(0.50)	/* factor for sticky entries */
#define USAGE_DEALLOC_PERCENT	5	/* free this % of entries at once */

#define JUMBLE_SIZE				1024	/* query serialization buffer size */

#define MAX_RESPONSE_BUCKET 50
#define INVALID_BUCKET_ID	-1
#define MAX_REL_LEN			255
#define MAX_BUCKETS			10
#define TEXT_LEN			255
#define ERROR_MESSAGE_LEN	100
#define REL_LST				10
#define REL_LEN				1000
#define CMD_LST				10
#define CMD_LEN				20
#define APPLICATIONNAME_LEN	100
#define COMMENTS_LEN        512
#define PGSM_OVER_FLOW_MAX	10
#define PLAN_TEXT_LEN		1024
/* the assumption of query max nested level */
#define DEFAULT_MAX_NESTED_LEVEL	10

#define MAX_QUERY_BUF						(PGSM_QUERY_SHARED_BUFFER * 1024 * 1024)
#define MAX_BUCKETS_MEM 					(PGSM_MAX * 1024 * 1024)
#define BUCKETS_MEM_OVERFLOW() 				((hash_get_num_entries(pgss_hash) * sizeof(pgssEntry)) >= MAX_BUCKETS_MEM)
#define MAX_BUCKET_ENTRIES 					(MAX_BUCKETS_MEM / sizeof(pgssEntry))
#define QUERY_BUFFER_OVERFLOW(x,y)  		((x + y + sizeof(uint64) + sizeof(uint64)) > MAX_QUERY_BUF)
#define QUERY_MARGIN 						100
#define MIN_QUERY_LEN						10
#define SQLCODE_LEN                         20

#if PG_VERSION_NUM >= 130000
#define	MAX_SETTINGS                        15
#else
#define MAX_SETTINGS                        14
#endif

/* Update this if need a enum GUC with more options. */
#define MAX_ENUM_OPTIONS 6
typedef struct GucVariables
{
    enum    config_type type;   /* PGC_BOOL, PGC_INT, PGC_REAL, PGC_STRING, PGC_ENUM */
    int     guc_variable;
    char    guc_name[TEXT_LEN];
    char    guc_desc[TEXT_LEN];
    int     guc_default;
    int     guc_min;
    int     guc_max;
    int     guc_unit;
    int     *guc_value;
    bool    guc_restart;
    int     n_options;
    char    guc_options[MAX_ENUM_OPTIONS][32];
} GucVariable;


#if PG_VERSION_NUM < 130000
typedef struct WalUsage
{
    long        wal_records;    /* # of WAL records produced */
    long        wal_fpi;        /* # of WAL full page images produced */
    uint64      wal_bytes;      /* size of WAL records produced */
} WalUsage;
#endif

typedef enum OVERFLOW_TARGET
{
	OVERFLOW_TARGET_NONE = 0,
	OVERFLOW_TARGET_DISK
} OVERFLOW_TARGET;

typedef enum pgssStoreKind
{
	PGSS_INVALID = -1,

	/*
	 * PGSS_PLAN and PGSS_EXEC must be respectively 0 and 1 as they're used to
	 * reference the underlying values in the arrays in the Counters struct,
	 * and this order is required in pg_stat_statements_internal().
	 */
	PGSS_PARSE = 0,
	PGSS_PLAN,
	PGSS_EXEC,
	PGSS_FINISHED,
	PGSS_ERROR,

	PGSS_NUMKIND				/* Must be last value of this enum */
} pgssStoreKind;

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
} AGG_KEY;

#define MAX_QUERY_LEN 1024

/* shared memory storage for the query */
typedef struct CallTime
{
	double		total_time;					/* total execution time, in msec */
	double		min_time;					/* minimum execution time in msec */
	double		max_time;					/* maximum execution time in msec */
	double		mean_time;					/* mean execution time in msec */
	double		sum_var_time;				/* sum of variances in execution time in msec */
} CallTime;

/*
 * Entry type for queries hash table (query ID).
 *
 * We use a hash table to keep track of query IDs that have their
 * corresponding query text added to the query buffer (pgsm_query_shared_buffer).
 *
 * This allow us to avoid adding duplicated queries to the buffer, therefore
 * leaving more space for other queries and saving some CPU.
 */
typedef struct pgssQueryEntry
{
	uint64		queryid;    /* query identifier, also the key. */
	size_t		query_pos;  /* query location within query buffer */
} pgssQueryEntry;

typedef struct PlanInfo
{
	uint64		planid;						/* plan identifier */
	char 		plan_text[PLAN_TEXT_LEN];	/* plan text */
	size_t		plan_len;                   /* strlen(plan_text) */
} PlanInfo;

typedef struct pgssHashKey
{
	uint64		bucket_id;		/* bucket number */
	uint64		queryid;		/* query identifier */
	uint64		userid;			/* user OID */
	uint64		dbid;			/* database OID */
	uint64		ip;				/* client ip address */
	uint64		planid;			/* plan identifier */
	uint64		appid;			/* hash of application name */
    uint64        toplevel;       /* query executed at top level */
} pgssHashKey;

typedef struct QueryInfo
{
	uint64		parentid;					/* parent queryid of current query*/
	int64       type; 						/* type of query, options are query, info, warning, error, fatal */
	char		application_name[APPLICATIONNAME_LEN];
	char		comments[COMMENTS_LEN];
	char		relations[REL_LST][REL_LEN];         /* List of relation involved in the query */
	int			num_relations;				/*  Number of relation in the query */
	CmdType		cmd_type;                   /* query command type SELECT/UPDATE/DELETE/INSERT */
} QueryInfo;

typedef struct ErrorInfo
{
	int64	elevel;							/* error elevel */
	char    sqlcode[SQLCODE_LEN];			/* error sqlcode  */
	char	message[ERROR_MESSAGE_LEN];   	/* error message text */
} ErrorInfo;

typedef struct Calls
{
	int64		calls;						/* # of times executed */
	int64		rows;						/* total # of retrieved or affected rows */
	double		usage;						/* usage factor */
} Calls;


typedef struct Blocks
{
	int64		shared_blks_hit;			/* # of shared buffer hits */
	int64		shared_blks_read;			/* # of shared disk blocks read */
	int64		shared_blks_dirtied;		/* # of shared disk blocks dirtied */
	int64		shared_blks_written;		/* # of shared disk blocks written */
	int64		local_blks_hit;				/* # of local buffer hits */
	int64		local_blks_read;			/* # of local disk blocks read */
	int64		local_blks_dirtied;			/* # of local disk blocks dirtied */
	int64		local_blks_written;			/* # of local disk blocks written */
	int64		temp_blks_read;				/* # of temp blocks read */
	int64		temp_blks_written;			/* # of temp blocks written */
	double		blk_read_time;				/* time spent reading, in msec */
	double		blk_write_time;				/* time spent writing, in msec */
} Blocks;

typedef struct SysInfo
{
	float		utime;						/* user cpu time */
	float		stime;						/* system cpu time */
} SysInfo;

typedef struct Wal_Usage
{
	int64		wal_records;	/* # of WAL records generated */
	int64		wal_fpi;		/* # of WAL full page images generated */
	uint64		wal_bytes;		/* total amount of WAL bytes generated */
} Wal_Usage;

typedef struct Counters
{
	uint64		bucket_id;		/* bucket id */
	Calls		calls;
	QueryInfo	info;
	CallTime	time;

	Calls		plancalls;
	CallTime	plantime;
	PlanInfo    planinfo;

	Blocks		blocks;
	SysInfo		sysinfo;
	ErrorInfo   error;
	Wal_Usage   walusage;
	int			resp_calls[MAX_RESPONSE_BUCKET];	/* execution time's in msec */
	uint64		state;		/* query state */
} Counters;

/* Some global structure to get the cpu usage, really don't like the idea of global variable */

/*
 * Statistics per statement
 */
typedef struct pgssEntry
{
	pgssHashKey		key;			/* hash key of entry - MUST BE FIRST */
	Counters		counters;		/* the statistics for this query */
	int				encoding;		/* query text encoding */
	slock_t			mutex;			/* protects the counters only */
	size_t			query_pos;      /* query location within query buffer */
} pgssEntry;

/*
 * Global shared state
 */
typedef struct pgssSharedState
{
	LWLock				*lock;				/* protects hashtable search/modification */
	double				cur_median_usage;	/* current median usage in hashtable */
	slock_t				mutex;				/* protects following fields only: */
	Size				extent;				/* current extent of query file */
	int64				n_writers;			/* number of active writers to query file */
	pg_atomic_uint64	current_wbucket;
	pg_atomic_uint64	prev_bucket_usec;
	uint64				bucket_entry[MAX_BUCKETS];
	char				bucket_start_time[MAX_BUCKETS][60];   	/* start time of the bucket */
	LWLock				*errors_lock;		/* protects errors hashtable search/modification */
	/*
	 * These variables are used when pgsm_overflow_target is ON.
	 *
	 * overflow is set to true when the query buffer overflows.
	 *
	 * n_bucket_cycles counts the number of times we changed bucket
	 * since the query buffer overflowed. When it reaches pgsm_max_buckets
	 * we remove the dump file, also reset the counter.
	 *
	 * This allows us to avoid having a large file on disk that would also
	 * slowdown queries to the pg_stat_monitor view.
	 */
	bool				overflow;
	size_t				n_bucket_cycles;
} pgssSharedState;

#define ResetSharedState(x) \
do { \
		x->cur_median_usage = ASSUMED_MEDIAN_INIT; \
		x->cur_median_usage = ASSUMED_MEDIAN_INIT; \
		x->n_writers = 0; \
		pg_atomic_init_u64(&x->current_wbucket, 0); \
		pg_atomic_init_u64(&x->prev_bucket_usec, 0); \
		memset(&x->bucket_entry, 0, MAX_BUCKETS * sizeof(uint64)); \
} while(0)


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

/* Links to shared memory state */

bool SaveQueryText(uint64 bucketid,
				   uint64 queryid,
				   unsigned char *buf,
				   const char *query,
				   uint64 query_len,
				   size_t *query_pos);

/* guc.c */
void init_guc(void);
GucVariable *get_conf(int i);

/* hash_create.c */
bool IsHashInitialize(void);
void pgss_shmem_startup(void);
void pgss_shmem_shutdown(int code, Datum arg);
int pgsm_get_bucket_size(void);
pgssSharedState* pgsm_get_ss(void);
HTAB *pgsm_get_plan_hash(void);
HTAB *pgsm_get_hash(void);
HTAB *pgsm_get_query_hash(void);
HTAB *pgsm_get_plan_hash(void);
void hash_entry_reset(void);
void hash_query_entryies_reset(void);
void hash_query_entries();
void hash_query_entry_dealloc(int new_bucket_id, int old_bucket_id, unsigned char *query_buffer[]);
void hash_entry_dealloc(int new_bucket_id, int old_bucket_id, unsigned char *query_buffer);
pgssEntry* hash_entry_alloc(pgssSharedState *pgss, pgssHashKey *key, int encoding);
Size hash_memsize(void);

int read_query_buffer(int bucket_id, uint64 queryid, char *query_txt, size_t pos);
uint64 read_query(unsigned char *buf, uint64 queryid, char * query, size_t pos);
void pgss_startup(void);
void set_qbuf(unsigned char *);

/* hash_query.c */
void pgss_startup(void);
/*---- GUC variables ----*/
typedef enum {
    PSGM_TRACK_NONE = 0, /* track no statements */
    PGSM_TRACK_TOP,      /* only top level statements */
    PGSM_TRACK_ALL       /* all statements, including nested ones */
} PGSMTrackLevel;
static const struct config_enum_entry track_options[] =
{
    {"none", PSGM_TRACK_NONE, false},
    {"top", PGSM_TRACK_TOP, false},
    {"all", PGSM_TRACK_ALL, false},
    {NULL, 0, false}
};

#define PGSM_MAX get_conf(0)->guc_variable
#define PGSM_QUERY_MAX_LEN get_conf(1)->guc_variable
#define PGSM_TRACK_UTILITY get_conf(2)->guc_variable
#define PGSM_NORMALIZED_QUERY get_conf(3)->guc_variable
#define PGSM_MAX_BUCKETS get_conf(4)->guc_variable
#define PGSM_BUCKET_TIME get_conf(5)->guc_variable
#define PGSM_HISTOGRAM_MIN get_conf(6)->guc_variable
#define PGSM_HISTOGRAM_MAX get_conf(7)->guc_variable
#define PGSM_HISTOGRAM_BUCKETS get_conf(8)->guc_variable
#define PGSM_QUERY_SHARED_BUFFER get_conf(9)->guc_variable
#define PGSM_OVERFLOW_TARGET get_conf(10)->guc_variable
#define PGSM_QUERY_PLAN get_conf(11)->guc_variable
#define PGSM_TRACK get_conf(12)->guc_variable
#define PGSM_EXTRACT_COMMENTS get_conf(13)->guc_variable
#define PGSM_TRACK_PLANNING get_conf(14)->guc_variable


/*---- Benchmarking ----*/
#ifdef BENCHMARK
/* 
 * These enumerator values are used as index in the hook stats array.
 * STATS_START and STATS_END are used only to delimit the range.
 * STATS_END is also the length of the valid items in the enum.
 */
enum pg_hook_stats_id {
	STATS_START = -1,
	STATS_PGSS_POST_PARSE_ANALYZE,
	STATS_PGSS_EXECUTORSTART,
	STATS_PGSS_EXECUTORUN,
	STATS_PGSS_EXECUTORFINISH,
	STATS_PGSS_EXECUTOREND,
	STATS_PGSS_PROCESSUTILITY,
#if PG_VERSION_NUM >= 130000
	STATS_PGSS_PLANNER_HOOK,
#endif
	STATS_PGSM_EMIT_LOG_HOOK,
	STATS_PGSS_EXECUTORCHECKPERMS,
	STATS_END
};

/* Hold time to execute statistics for a hook. */
struct pg_hook_stats_t {
	char hook_name[64];
	double min_time;
	double max_time;
	double total_time;
	uint64 ncalls;
};

#define HOOK_STATS_SIZE MAXALIGN((size_t)STATS_END * sizeof(struct pg_hook_stats_t))

/* Allocate a pg_hook_stats_t array of size HOOK_STATS_SIZE on shared memory. */
void init_hook_stats(void);

/* Update hook time execution statistics. */
void update_hook_stats(enum pg_hook_stats_id hook_id, double time_elapsed);

/*
 * Macro used to declare a hook function:
 * Example:
 *    DECLARE_HOOK(void my_hook, const char *query, size_t length);
 * Will expand to:
 *    static void my_hook(const char *query, size_t length);
 *    static void my_hook_benchmark(const char *query, size_t length);
 */
#define DECLARE_HOOK(hook, ...) \
        static hook(__VA_ARGS__); \
        static hook##_benchmark(__VA_ARGS__);

/*
 * Macro used to wrap a hook when pg_stat_monitor is compiled with -DBENCHMARK.
 *
 * It is intended to be used as follows in _PG_init():
 *     pg_hook_function = HOOK(my_hook_function);
 * Then, if pg_stat_monitor is compiled with -DBENCHMARK this will expand to:
 *     pg_hook_name = my_hook_function_benchmark;
 * Otherwise it will simple expand to:
 *     pg_hook_name = my_hook_function;
 */
#define HOOK(name) name##_benchmark

#else /* #ifdef BENCHMARK */

#define DECLARE_HOOK(hook, ...) \
        static hook(__VA_ARGS__);
#define HOOK(name) name
#define HOOK_STATS_SIZE 0
#endif

#endif
