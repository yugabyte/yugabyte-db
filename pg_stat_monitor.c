/*-------------------------------------------------------------------------
 *
 * pg_stat_monitor.c
 *		Track statement execution times across a whole database cluster.
 *
 * Portions Copyright © 2018-2020, Percona LLC and/or its affiliates
 *
 * Portions Copyright (c) 1996-2020, PostgreSQL Global Development Group
 *
 * Portions Copyright (c) 1994, The Regents of the University of California
 *
 * IDENTIFICATION
 *	  contrib/pg_stat_monitor/pg_stat_monitor.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "access/parallel.h"
#include "nodes/pg_list.h"
#include "utils/guc.h"
#include <regex.h>
#include "pgstat.h"
#include "commands/dbcommands.h"
#include "commands/explain.h"
#include "pg_stat_monitor.h"

 /*
  * Extension version number, for supporting older extension versions' objects
  */
typedef enum pgsmVersion
{
	PGSM_V1_0 = 0,
	PGSM_V2_0
}			pgsmVersion;

PG_MODULE_MAGIC;

#define BUILD_VERSION                   "2.0.3"

/* Number of output arguments (columns) for various API versions */
#define PG_STAT_MONITOR_COLS_V1_0    52
#define PG_STAT_MONITOR_COLS_V2_0    64
#define PG_STAT_MONITOR_COLS         PG_STAT_MONITOR_COLS_V2_0	/* maximum of above */

#define PGSM_TEXT_FILE PGSTAT_STAT_PERMANENT_DIRECTORY "pg_stat_monitor_query"

#define PGUNSIXBIT(val) (((val) & 0x3F) + '0')

#define _snprintf(_str_dst, _str_src, _len, _max_len)\
  memcpy((void *)_str_dst, _str_src, _len < _max_len ? _len : _max_len)

#define pgsm_enabled(level) \
    (!IsParallelWorker() && \
    (pgsm_track == PGSM_TRACK_ALL || \
    (pgsm_track == PGSM_TRACK_TOP && (level) == 0)))

#define _snprintf2(_str_dst, _str_src, _len1, _len2)\
do                                                      \
{                                                       \
	int i;                                            \
	for(i = 0; i < _len1; i++)                        \
		strlcpy((char *)_str_dst[i], _str_src[i], _len2); \
}while(0)

#define PGSM_INVALID_IP_MASK	0xFFFFFFFF

#define pgsm_client_ip_is_valid() \
	(pgsm_client_ip != PGSM_INVALID_IP_MASK)

/*---- Initicalization Function Declarations ----*/
void		_PG_init(void);

/* Current nesting depth of ExecutorRun+ProcessUtility calls */
static int	exec_nested_level = 0;
volatile bool __pgsm_do_not_capture_error = false;

#if PG_VERSION_NUM >= 130000
static int	plan_nested_level = 0;
#endif

/* Histogram bucket variables */
static double hist_bucket_min;
static double hist_bucket_max;
static double hist_bucket_timings[MAX_RESPONSE_BUCKET + 2][2];	/* Start and end timings */
static int	hist_bucket_count_user;
static int	hist_bucket_count_total;

static uint32 pgsm_client_ip = PGSM_INVALID_IP_MASK;

/* The array to store outer layer query id*/
uint64	   *nested_queryids;
char	  **nested_query_txts;
List	   *lentries = NIL;

/* Regex object used to extract query comments. */
static regex_t preg_query_comments;
static char relations[REL_LST][REL_LEN];

static int	num_relations;		/* Number of relation in the query */
static bool system_init = false;
static struct rusage rusage_start;
static struct rusage rusage_end;

/* Application name and length; set each time when an entry is created locally */
char		app_name[APPLICATIONNAME_LEN];
int			app_name_len;


/* Query buffer, store queries' text. */
static char *pgsm_explain(QueryDesc *queryDesc);

static void extract_query_comments(const char *query, char *comments, size_t max_len);
static void set_histogram_bucket_timings(void);
static void histogram_bucket_timings(int index, double *b_start, double *b_end);
static int	get_histogram_bucket(double q_time);

static bool IsSystemInitialized(void);
static double time_diff(struct timeval end, struct timeval start);
static void request_additional_shared_resources(void);


/* Saved hook values in case of unload */

#if PG_VERSION_NUM >= 150000
static void pgsm_shmem_request(void);
static shmem_request_hook_type prev_shmem_request_hook = NULL;
#endif
#if PG_VERSION_NUM >= 130000
static planner_hook_type planner_hook_next = NULL;
#endif
static post_parse_analyze_hook_type prev_post_parse_analyze_hook = NULL;
static ExecutorStart_hook_type prev_ExecutorStart = NULL;
static ExecutorRun_hook_type prev_ExecutorRun = NULL;
static ExecutorFinish_hook_type prev_ExecutorFinish = NULL;
static ExecutorEnd_hook_type prev_ExecutorEnd = NULL;
static ProcessUtility_hook_type prev_ProcessUtility = NULL;
static emit_log_hook_type prev_emit_log_hook = NULL;

DECLARE_HOOK(void pgsm_emit_log_hook, ErrorData *edata);
static shmem_startup_hook_type prev_shmem_startup_hook = NULL;
static ExecutorCheckPerms_hook_type prev_ExecutorCheckPerms_hook = NULL;

PG_FUNCTION_INFO_V1(pg_stat_monitor_version);
PG_FUNCTION_INFO_V1(pg_stat_monitor_reset);
PG_FUNCTION_INFO_V1(pg_stat_monitor_1_0);
PG_FUNCTION_INFO_V1(pg_stat_monitor_2_0);
PG_FUNCTION_INFO_V1(pg_stat_monitor);
PG_FUNCTION_INFO_V1(get_histogram_timings);
PG_FUNCTION_INFO_V1(pg_stat_monitor_hook_stats);

static uint pg_get_client_addr(bool *ok);
static int	pg_get_application_name(char *name, int buff_size);
static PgBackendStatus *pg_get_backend_status(void);
static Datum intarray_get_datum(int32 arr[], int len);

#if PG_VERSION_NUM < 140000
DECLARE_HOOK(void pgsm_post_parse_analyze, ParseState *pstate, Query *query);
#else
DECLARE_HOOK(void pgsm_post_parse_analyze, ParseState *pstate, Query *query, JumbleState *jstate);
#endif

DECLARE_HOOK(void pgsm_ExecutorStart, QueryDesc *queryDesc, int eflags);
DECLARE_HOOK(void pgsm_ExecutorRun, QueryDesc *queryDesc, ScanDirection direction, uint64 count, bool execute_once);
DECLARE_HOOK(void pgsm_ExecutorFinish, QueryDesc *queryDesc);
DECLARE_HOOK(void pgsm_ExecutorEnd, QueryDesc *queryDesc);
#if PG_VERSION_NUM < 160000
DECLARE_HOOK(bool pgsm_ExecutorCheckPerms, List *rt, bool abort);
#else
DECLARE_HOOK(bool pgsm_ExecutorCheckPerms, List *rt, List *rp, bool abort);
#endif

#if PG_VERSION_NUM >= 140000
DECLARE_HOOK(PlannedStmt *pgsm_planner_hook, Query *parse, const char *query_string, int cursorOptions, ParamListInfo boundParams);
DECLARE_HOOK(void pgsm_ProcessUtility, PlannedStmt *pstmt, const char *queryString,
			 bool readOnlyTree,
			 ProcessUtilityContext context,
			 ParamListInfo params, QueryEnvironment *queryEnv,
			 DestReceiver *dest,
			 QueryCompletion *qc);
#elif PG_VERSION_NUM >= 130000
DECLARE_HOOK(PlannedStmt *pgsm_planner_hook, Query *parse, const char *query_string, int cursorOptions, ParamListInfo boundParams);
DECLARE_HOOK(void pgsm_ProcessUtility, PlannedStmt *pstmt, const char *queryString,
			 ProcessUtilityContext context,
			 ParamListInfo params, QueryEnvironment *queryEnv,
			 DestReceiver *dest,
			 QueryCompletion *qc);
#else
static void BufferUsageAccumDiff(BufferUsage *bufusage, BufferUsage *pgBufferUsage, BufferUsage *bufusage_start);

DECLARE_HOOK(void pgsm_ProcessUtility, PlannedStmt *pstmt, const char *queryString,
			 ProcessUtilityContext context, ParamListInfo params,
			 QueryEnvironment *queryEnv,
			 DestReceiver *dest,
			 char *completionTag);
#endif
static uint64 pgsm_hash_string(const char *str, int len);
char	   *unpack_sql_state(int sql_state);

#define PGSM_HANDLED_UTILITY(n)  (!IsA(n, ExecuteStmt) && \
									!IsA(n, PrepareStmt) && \
									!IsA(n, DeallocateStmt))


static pgsmEntry * pgsm_create_hash_entry(uint64 bucket_id, uint64 queryid, PlanInfo * plan_info);
static void pgsm_add_to_list(pgsmEntry * entry, char *query_text, int query_len);
static pgsmEntry * pgsm_get_entry_for_query(uint64 queryid, PlanInfo * plan_info, const char *query_text, int query_len, bool create);
static uint64 get_pgsm_query_id_hash(const char *norm_query, int len);

static void pgsm_cleanup_callback(void *arg);
static void pgsm_store_error(const char *query, ErrorData *edata);

/*---- Local variables ----*/
MemoryContextCallback mem_cxt_reset_callback =
{
	.func = pgsm_cleanup_callback,
	.arg = NULL
};
volatile bool callback_setup = false;

static void pgsm_update_entry(pgsmEntry * entry,
							  const char *query,
							  char *comments,
							  int comments_len,
							  PlanInfo * plan_info,
							  SysInfo * sys_info,
							  ErrorInfo * error_info,
							  double plan_total_time,
							  double exec_total_time,
							  uint64 rows,
							  BufferUsage *bufusage,
							  WalUsage *walusage,
							  const struct JitInstrumentation *jitusage,
							  bool reset,
							  pgsmStoreKind kind);
static void pgsm_store(pgsmEntry * entry);

static void pg_stat_monitor_internal(FunctionCallInfo fcinfo,
									 pgsmVersion api_version,
									 bool showtext);

#if PG_VERSION_NUM < 140000
static void AppendJumble(JumbleState *jstate,
						 const unsigned char *item, Size size);
static void JumbleQuery(JumbleState *jstate, Query *query);
static void JumbleRangeTable(JumbleState *jstate, List *rtable, CmdType cmd_type);
static void JumbleExpr(JumbleState *jstate, Node *node);
static void RecordConstLocation(JumbleState *jstate, int location);

/*
 * Given a possibly multi-statement source string, confine our attention to the
 * relevant part of the string.
 */
static const char *CleanQuerytext(const char *query, int *location, int *len);
static uint64 get_query_id(JumbleState *jstate, Query *query);
#endif

static char *generate_normalized_query(JumbleState *jstate, const char *query,
									   int query_loc, int *query_len_p, int encoding);
static void fill_in_constant_lengths(JumbleState *jstate, const char *query, int query_loc);
static int	comp_location(const void *a, const void *b);

static uint64 get_next_wbucket(pgsmSharedState * pgsm);

/*
 * Module load callback
 */
/*  cppcheck-suppress unusedFunction */
void
_PG_init(void)
{
	int			rc;

	elog(DEBUG2, "[pg_stat_monitor] pg_stat_monitor: %s().", __FUNCTION__);

	/*
	 * In order to create our shared memory area, we have to be loaded via
	 * shared_preload_libraries.  If not, fall out without hooking into any of
	 * the main system.  (We don't throw error here because it seems useful to
	 * allow the pg_stat_monitor functions to be created even when the module
	 * isn't active.  The functions must protect themselves against being
	 * called then, however.)
	 */
	if (!process_shared_preload_libraries_in_progress)
		return;

	/* Inilize the GUC variables */
	init_guc();

	set_histogram_bucket_timings();

#if PG_VERSION_NUM >= 140000

	/*
	 * Inform the postmaster that we want to enable query_id calculation if
	 * compute_query_id is set to auto.
	 */
	EnableQueryId();
#endif


	EmitWarningsOnPlaceholders("pg_stat_monitor");

	/*
	 * Compile regular expression for extracting out query comments only once.
	 */
	rc = regcomp(&preg_query_comments, "/\\*([^*]|[\r\n]|(\\*+([^*/]|[\r\n])))*\\*+/", REG_EXTENDED);
	if (rc != 0)
	{
		elog(ERROR, "[pg_stat_monitor] _PG_init: query comments regcomp() failed, return code=(%d).", rc);
	}

	/*
	 * Install hooks.
	 */
#if PG_VERSION_NUM >= 150000
	prev_shmem_request_hook = shmem_request_hook;
	shmem_request_hook = pgsm_shmem_request;
#else
	request_additional_shared_resources();
#endif
	prev_shmem_startup_hook = shmem_startup_hook;
	shmem_startup_hook = pgsm_shmem_startup;
	prev_post_parse_analyze_hook = post_parse_analyze_hook;
	post_parse_analyze_hook = HOOK(pgsm_post_parse_analyze);
	prev_ExecutorStart = ExecutorStart_hook;
	ExecutorStart_hook = HOOK(pgsm_ExecutorStart);
	prev_ExecutorRun = ExecutorRun_hook;
	ExecutorRun_hook = HOOK(pgsm_ExecutorRun);
	prev_ExecutorFinish = ExecutorFinish_hook;
	ExecutorFinish_hook = HOOK(pgsm_ExecutorFinish);
	prev_ExecutorEnd = ExecutorEnd_hook;
	ExecutorEnd_hook = HOOK(pgsm_ExecutorEnd);
	prev_ProcessUtility = ProcessUtility_hook;
	ProcessUtility_hook = HOOK(pgsm_ProcessUtility);
#if PG_VERSION_NUM >= 130000
	planner_hook_next = planner_hook;
	planner_hook = HOOK(pgsm_planner_hook);
#endif
	prev_emit_log_hook = emit_log_hook;
	emit_log_hook = HOOK(pgsm_emit_log_hook);
	prev_ExecutorCheckPerms_hook = ExecutorCheckPerms_hook;
	ExecutorCheckPerms_hook = HOOK(pgsm_ExecutorCheckPerms);

	nested_queryids = (uint64 *) malloc(sizeof(uint64) * max_stack_depth);
	nested_query_txts = (char **) malloc(sizeof(char *) * max_stack_depth);

	system_init = true;
}

/*
 * shmem_startup hook: allocate or attach to shared memory,
 * then load any pre-existing statistics from file.
 * Also create and load the query-texts file, which is expected to exist
 * (even if empty) while the module is enabled.
 */
void
pgsm_shmem_startup(void)
{
	if (prev_shmem_startup_hook)
		prev_shmem_startup_hook();

	pgsm_startup();
}

static void
request_additional_shared_resources(void)
{
	/*
	 * Request additional shared resources.  (These are no-ops if we're not in
	 * the postmaster process.)  We'll allocate or attach to the shared
	 * resources in pgsm_shmem_startup().
	 */
	RequestAddinShmemSpace(pgsm_ShmemSize() + HOOK_STATS_SIZE);
	RequestNamedLWLockTranche("pg_stat_monitor", 1);
}

/*
 * Select the version of pg_stat_monitor.
 */
Datum
pg_stat_monitor_version(PG_FUNCTION_ARGS)
{
	PG_RETURN_TEXT_P(cstring_to_text(BUILD_VERSION));
}

#if PG_VERSION_NUM >= 150000
/*
 * shmem_request hook: request additional shared resources.  We'll allocate or
 * attach to the shared resources in pgsm_shmem_startup().
 */
static void
pgsm_shmem_request(void)
{
	if (prev_shmem_request_hook)
		prev_shmem_request_hook();
	request_additional_shared_resources();
}
#endif

static void
pgsm_post_parse_analyze_internal(ParseState *pstate, Query *query, JumbleState *jstate)
{
	pgsmEntry  *entry;
	const char *query_text;
	char	   *norm_query = NULL;
	int			norm_query_len;
	int			location;
	int			query_len;

	/* Safety check... */
	if (!IsSystemInitialized())
		return;

	if (callback_setup == false)
	{
		/*
		 * If MessageContext is valid setup a callback to cleanup our local
		 * stats list when the MessagContext gets reset
		 */
		if (MemoryContextIsValid(MessageContext))
		{
			MemoryContextRegisterResetCallback(MessageContext, &mem_cxt_reset_callback);
			callback_setup = true;
		}
	}

	if (!pgsm_enabled(exec_nested_level))
		return;

	/*
	 * Clear queryId for prepared statements related utility, as those will
	 * inherit from the underlying statement's one (except DEALLOCATE which is
	 * entirely untracked).
	 */
	if (query->utilityStmt)
	{
		if (pgsm_track_utility && !PGSM_HANDLED_UTILITY(query->utilityStmt))
			query->queryId = UINT64CONST(0);

		return;
	}

	/*
	 * Let's calculate queryid for versions 13 and below. We don't have to
	 * check that jstate is valid, it always will be for these versions.
	 */
#if PG_VERSION_NUM < 140000
	query->queryId = get_query_id(jstate, query);
#endif

	/*
	 * If we are unlucky enough to get a hash of zero, use 1 instead, to
	 * prevent confusion with the utility-statement case.
	 */
	if (query->queryId == UINT64CONST(0))
		query->queryId = UINT64CONST(1);

	/*
	 * Let's save the normalized query so that we can save the data without in
	 * hash later on without the need of jstate which wouldn't be available.
	 */
	query_text = pstate->p_sourcetext;
	location = query->stmt_location;
	query_len = query->stmt_len;

	/* We should always have a valid query. */
	query_text = CleanQuerytext(query_text, &location, &query_len);
	Assert(query_text);

	norm_query_len = query_len;

	/* Generate a normalized query */
	if (jstate && jstate->clocations_count > 0 && (pgsm_enable_pgsm_query_id || pgsm_normalized_query))
	{
		norm_query = generate_normalized_query(jstate,
											   query_text,	/* query */
											   location,	/* query location */
											   &norm_query_len,
											   GetDatabaseEncoding());

		Assert(norm_query);
	}

	/*
	 * At this point, we don't know which bucket this query will land in, so
	 * passing 0. The store function MUST later update it based on the current
	 * bucket value. The correct bucket value will be needed then to search
	 * the hash table, or create the appropriate entry.
	 */
	entry = pgsm_create_hash_entry(0, query->queryId, NULL);

	/*
	 * Update other member that are not counters, so that we don't have to
	 * worry about these.
	 */
	entry->pgsm_query_id = get_pgsm_query_id_hash(norm_query ? norm_query : query_text, norm_query_len);
	entry->counters.info.cmd_type = query->commandType;

	/*
	 * Add the query text and entry to the local list.
	 *
	 * Preserve the normalized query if needed and we got a valid one.
	 * Otherwise, store the actual query so that we don't have to check what
	 * query to store when saving into the hash.
	 *
	 * In case of query_text, request the function to duplicate it so that it
	 * is put in the relevant memory context.
	 */
	if (pgsm_normalized_query && norm_query)
		pgsm_add_to_list(entry, norm_query, norm_query_len);
	else
	{
		pgsm_add_to_list(entry, (char *) query_text, query_len);
	}

	/* Check that we've not exceeded max_stack_depth */
	Assert(list_length(lentries) <= max_stack_depth);

	if (norm_query)
		pfree(norm_query);
}

#if PG_VERSION_NUM >= 140000
/*
 * Post-parse-analysis hook: mark query with a queryId
 */
static void
pgsm_post_parse_analyze(ParseState *pstate, Query *query, JumbleState *jstate)
{
	if (prev_post_parse_analyze_hook)
		prev_post_parse_analyze_hook(pstate, query, jstate);

	pgsm_post_parse_analyze_internal(pstate, query, jstate);
}
#else
/*
 * Post-parse-analysis hook: mark query with a queryId
 */
static void
pgsm_post_parse_analyze(ParseState *pstate, Query *query)
{
	JumbleState jstate;

	if (prev_post_parse_analyze_hook)
		prev_post_parse_analyze_hook(pstate, query);

	pgsm_post_parse_analyze_internal(pstate, query, &jstate);
}
#endif

/*
 * ExecutorStart hook: start up tracking if needed
 */
static void
pgsm_ExecutorStart(QueryDesc *queryDesc, int eflags)
{
	if (getrusage(RUSAGE_SELF, &rusage_start) != 0)
		elog(DEBUG1, "[pg_stat_monitor] pgsm_ExecutorStart: failed to execute getrusage.");

	if (prev_ExecutorStart)
		prev_ExecutorStart(queryDesc, eflags);
	else
		standard_ExecutorStart(queryDesc, eflags);

	/*
	 * If query has queryId zero, don't track it.  This prevents double
	 * counting of optimizable statements that are directly contained in
	 * utility statements.
	 */
	if (pgsm_enabled(exec_nested_level) &&
		queryDesc->plannedstmt->queryId != UINT64CONST(0))
	{
		/*
		 * Set up to track total elapsed time in ExecutorRun.  Make sure the
		 * space is allocated in the per-query context so it will go away at
		 * ExecutorEnd.
		 */
		if (queryDesc->totaltime == NULL)
		{
			MemoryContext oldcxt;

			oldcxt = MemoryContextSwitchTo(queryDesc->estate->es_query_cxt);
#if PG_VERSION_NUM < 140000
			queryDesc->totaltime = InstrAlloc(1, INSTRUMENT_ALL);
#else
			queryDesc->totaltime = InstrAlloc(1, INSTRUMENT_ALL, false);
#endif
			MemoryContextSwitchTo(oldcxt);
		}
	}
}


/*
 * ExecutorRun hook: all we need do is track nesting depth
 */
static void
pgsm_ExecutorRun(QueryDesc *queryDesc, ScanDirection direction, uint64 count,
				 bool execute_once)
{
	if (exec_nested_level >= 0 && exec_nested_level < max_stack_depth)
	{
		nested_queryids[exec_nested_level] = queryDesc->plannedstmt->queryId;
		nested_query_txts[exec_nested_level] = strdup(queryDesc->sourceText);
	}

	exec_nested_level++;
	PG_TRY();
	{
		if (prev_ExecutorRun)
			prev_ExecutorRun(queryDesc, direction, count, execute_once);
		else
			standard_ExecutorRun(queryDesc, direction, count, execute_once);
		exec_nested_level--;
		if (exec_nested_level >= 0 && exec_nested_level < max_stack_depth)
		{
			nested_queryids[exec_nested_level] = UINT64CONST(0);
			if (nested_query_txts[exec_nested_level])
				free(nested_query_txts[exec_nested_level]);
			nested_query_txts[exec_nested_level] = NULL;
		}
	}
	PG_CATCH();
	{
		exec_nested_level--;
		if (exec_nested_level >= 0 && exec_nested_level < max_stack_depth)
		{
			nested_queryids[exec_nested_level] = UINT64CONST(0);
			if (nested_query_txts[exec_nested_level])
				free(nested_query_txts[exec_nested_level]);
			nested_query_txts[exec_nested_level] = NULL;
		}
		PG_RE_THROW();
	}
	PG_END_TRY();
}

/*
 * ExecutorFinish hook: all we need do is track nesting depth
 */
static void
pgsm_ExecutorFinish(QueryDesc *queryDesc)
{
	exec_nested_level++;

	PG_TRY();
	{
		if (prev_ExecutorFinish)
			prev_ExecutorFinish(queryDesc);
		else
			standard_ExecutorFinish(queryDesc);
		exec_nested_level--;
	}
	PG_CATCH();
	{
		exec_nested_level--;
		PG_RE_THROW();
	}
	PG_END_TRY();

}

static char *
pgsm_explain(QueryDesc *queryDesc)
{
	ExplainState *es = NewExplainState();

	es->buffers = false;
	es->analyze = false;
	es->verbose = false;
	es->costs = false;
	es->format = EXPLAIN_FORMAT_TEXT;

	ExplainBeginOutput(es);
	ExplainPrintPlan(es, queryDesc);
	ExplainEndOutput(es);

	if (es->str->len > 0 && es->str->data[es->str->len - 1] == '\n')
		es->str->data[--es->str->len] = '\0';
	return es->str->data;
}

/*
 * ExecutorEnd hook: store results if needed
 */
static void
pgsm_ExecutorEnd(QueryDesc *queryDesc)
{
	uint64		queryId = queryDesc->plannedstmt->queryId;
	SysInfo		sys_info;
	PlanInfo	plan_info;
	PlanInfo   *plan_ptr = NULL;
	pgsmEntry  *entry = NULL;

	/* Extract the plan information in case of SELECT statement */
	if (queryDesc->operation == CMD_SELECT && pgsm_enable_query_plan)
	{
		int rv;
		MemoryContext oldctx;

		/*
		 * Making sure it is a per query context so that there's no memory
		 * leak when executor ends.
		 */
		oldctx = MemoryContextSwitchTo(queryDesc->estate->es_query_cxt);

		rv = snprintf(plan_info.plan_text, PLAN_TEXT_LEN, "%s", pgsm_explain(queryDesc));

		/*
		 * If snprint didn't write anything or there was an error, let's keep
		 * planinfo as NULL.
		 */
		if (rv > 0)
		{
			plan_info.plan_len = (rv < PLAN_TEXT_LEN) ? rv : PLAN_TEXT_LEN - 1;
			plan_info.planid = pgsm_hash_string(plan_info.plan_text, plan_info.plan_len);
			plan_ptr = &plan_info;
		}

		/* Switch back to old context */
		MemoryContextSwitchTo(oldctx);
	}

	if (queryId != UINT64CONST(0) && queryDesc->totaltime && pgsm_enabled(exec_nested_level))
	{
		entry = pgsm_get_entry_for_query(queryId, plan_ptr, (char *) queryDesc->sourceText, strlen(queryDesc->sourceText), true);
		if (!entry)
		{
			elog(DEBUG2, "[pg_stat_monitor] pgsm_ExecutorEnd: Failed to find entry for [%lu] %s.", queryId, queryDesc->sourceText);
			return;
		}

		if (entry->key.planid == 0)
			entry->key.planid = (plan_ptr) ? plan_ptr->planid : 0;

		/*
		 * Make sure stats accumulation is done.  (Note: it's okay if several
		 * levels of hook all do this.)
		 */
		InstrEndLoop(queryDesc->totaltime);

		sys_info.utime = 0;
		sys_info.stime = 0;

		if (getrusage(RUSAGE_SELF, &rusage_end) != 0)
			elog(DEBUG1, "[pg_stat_monitor] pgsm_ExecutorEnd: Failed to execute getrusage.");
		else
		{
			sys_info.utime = time_diff(rusage_end.ru_utime, rusage_start.ru_utime);
			sys_info.stime = time_diff(rusage_end.ru_stime, rusage_start.ru_stime);
		}

		pgsm_update_entry(entry,	/* entry */
						  NULL, /* query */
						  NULL, /* comments */
						  0,	/* comments length */
						  plan_ptr, /* PlanInfo */
						  &sys_info,	/* SysInfo */
						  NULL, /* ErrorInfo */
						  0,	/* plan_total_time */
						  queryDesc->totaltime->total * 1000.0, /* exec_total_time */
						  queryDesc->estate->es_processed,	/* rows */
						  &queryDesc->totaltime->bufusage,	/* bufusage */
#if PG_VERSION_NUM >= 130000
						  &queryDesc->totaltime->walusage,	/* walusage */
#else
						  NULL,
#endif
#if PG_VERSION_NUM >= 150000
						  queryDesc->estate->es_jit ? &queryDesc->estate->es_jit->instr : NULL, /* jitusage */
#else
						  NULL,
#endif
						  false,	/* reset */
						  PGSM_EXEC);	/* kind */

		pgsm_store(entry);
	}

	if (prev_ExecutorEnd)
		prev_ExecutorEnd(queryDesc);
	else
		standard_ExecutorEnd(queryDesc);

	num_relations = 0;
}

static bool
#if PG_VERSION_NUM < 160000
pgsm_ExecutorCheckPerms(List *rt, bool abort)
#else
pgsm_ExecutorCheckPerms(List *rt, List *rp, bool abort)
#endif
{
	ListCell   *lr = NULL;
	int			i = 0;
	int			j = 0;
	Oid			list_oid[20];

	num_relations = 0;

	foreach(lr, rt)
	{
		RangeTblEntry *rte = lfirst(lr);

		if (rte->rtekind != RTE_RELATION
#if PG_VERSION_NUM >= 160000
            && (rte->rtekind != RTE_SUBQUERY && rte->relkind != 'v')
#endif
           )
			continue;

		if (i < REL_LST)
		{
			bool		found = false;

			for (j = 0; j < i; j++)
			{
				if (list_oid[j] == rte->relid)
					found = true;
			}

			if (!found)
			{
				char	   *namespace_name;
				char	   *relation_name;

				list_oid[j] = rte->relid;
				namespace_name = get_namespace_name(get_rel_namespace(rte->relid));
				relation_name = get_rel_name(rte->relid);
				if (rte->relkind == 'v')
					snprintf(relations[i++], REL_LEN, "%s.%s*", namespace_name, relation_name);
				else
					snprintf(relations[i++], REL_LEN, "%s.%s", namespace_name, relation_name);
			}
		}
	}
	num_relations = i;

	if (prev_ExecutorCheckPerms_hook)
#if PG_VERSION_NUM < 160000
		return prev_ExecutorCheckPerms_hook(rt, abort);
#else
		return prev_ExecutorCheckPerms_hook(rt, rp, abort);
#endif

	return true;
}

#if PG_VERSION_NUM >= 130000
static PlannedStmt *
pgsm_planner_hook(Query *parse, const char *query_string, int cursorOptions, ParamListInfo boundParams)
{
	PlannedStmt *result;


	/*
	 * We can't process the query if no query_string is provided, as
	 * pgsm_store needs it.  We also ignore query without queryid, as it would
	 * be treated as a utility statement, which may not be the case.
	 *
	 * Note that planner_hook can be called from the planner itself, so we
	 * have a specific nesting level for the planner.  However, utility
	 * commands containing optimizable statements can also call the planner,
	 * same for regular DML (for instance for underlying foreign key queries).
	 * So testing the planner nesting level only is not enough to detect real
	 * top level planner call.
	 */

	if (pgsm_enabled(plan_nested_level + exec_nested_level) &&
		pgsm_track_planning && query_string && parse->queryId != UINT64CONST(0))
	{
		pgsmEntry  *entry = NULL;
		instr_time	start;
		instr_time	duration;
		BufferUsage bufusage_start;
		BufferUsage bufusage;
		WalUsage	walusage_start;
		WalUsage	walusage;

		/* We need to track buffer usage as the planner can access them. */
		bufusage_start = pgBufferUsage;

		/*
		 * Similarly the planner could write some WAL records in some cases
		 * (e.g. setting a hint bit with those being WAL-logged)
		 */
		walusage_start = pgWalUsage;
		INSTR_TIME_SET_CURRENT(start);

		if (MemoryContextIsValid(MessageContext))
			entry = pgsm_get_entry_for_query(parse->queryId, NULL, query_string, strlen(query_string), true);

		plan_nested_level++;
		PG_TRY();
		{
			/*
			 * If there is a previous installed hook, then assume it's going
			 * to call standard_planner() function, otherwise we call the
			 * function here. This is to avoid calling standard_planner()
			 * function twice, since it modifies the first argument (Query *),
			 * the second call would trigger an assertion failure.
			 */
			if (planner_hook_next)
				result = planner_hook_next(parse, query_string, cursorOptions, boundParams);
			else
				result = standard_planner(parse, query_string, cursorOptions, boundParams);
		}
		PG_FINALLY();
		{
			plan_nested_level--;
		}
		PG_END_TRY();

		INSTR_TIME_SET_CURRENT(duration);
		INSTR_TIME_SUBTRACT(duration, start);

		/* calc differences of buffer counters. */
		memset(&bufusage, 0, sizeof(BufferUsage));
		BufferUsageAccumDiff(&bufusage, &pgBufferUsage, &bufusage_start);

		/* calc differences of WAL counters. */
		memset(&walusage, 0, sizeof(WalUsage));
		WalUsageAccumDiff(&walusage, &pgWalUsage, &walusage_start);

		/* The plan details are captured when the query finishes */
		if (entry)
			pgsm_update_entry(entry,	/* entry */
							  NULL, /* query */
							  NULL, /* comments */
							  0,	/* comments length */
							  NULL, /* PlanInfo */
							  NULL, /* SysInfo */
							  NULL, /* ErrorInfo */
							  INSTR_TIME_GET_MILLISEC(duration),	/* plan_total_time */
							  0,	/* exec_total_time */
							  0,	/* rows */
							  &bufusage,	/* bufusage */
							  &walusage,	/* walusage */
							  NULL, /* jitusage */
							  false,	/* reset */
							  PGSM_PLAN);	/* kind */
	}
	else
	{
		/*
		 * If there is a previous installed hook, then assume it's going to
		 * call standard_planner() function, otherwise we call the function
		 * here. This is to avoid calling standard_planner() function twice,
		 * since it modifies the first argument (Query *), the second call
		 * would trigger an assertion failure.
		 */
		plan_nested_level++;

		if (planner_hook_next)
			result = planner_hook_next(parse, query_string, cursorOptions, boundParams);
		else
			result = standard_planner(parse, query_string, cursorOptions, boundParams);
		plan_nested_level--;

	}
	return result;
}
#endif

/*
 * ProcessUtility hook
 */
#if PG_VERSION_NUM >= 140000
static void
pgsm_ProcessUtility(PlannedStmt *pstmt, const char *queryString,
					bool readOnlyTree,
					ProcessUtilityContext context,
					ParamListInfo params, QueryEnvironment *queryEnv,
					DestReceiver *dest,
					QueryCompletion *qc)

#elif PG_VERSION_NUM >= 130000
static void
pgsm_ProcessUtility(PlannedStmt *pstmt, const char *queryString,
					ProcessUtilityContext context,
					ParamListInfo params, QueryEnvironment *queryEnv,
					DestReceiver *dest,
					QueryCompletion *qc)

#else
static void
pgsm_ProcessUtility(PlannedStmt *pstmt, const char *queryString,
					ProcessUtilityContext context, ParamListInfo params,
					QueryEnvironment *queryEnv,
					DestReceiver *dest,
					char *completionTag)
#endif
{
	Node	   *parsetree = pstmt->utilityStmt;
	uint64		queryId = 0;

#if PG_VERSION_NUM < 140000
	int			len = strlen(queryString);

	queryId = pgsm_hash_string(queryString, len);
#else
	queryId = pstmt->queryId;

	/*
	 * Force utility statements to get queryId zero.  We do this even in cases
	 * where the statement contains an optimizable statement for which a
	 * queryId could be derived (such as EXPLAIN or DECLARE CURSOR).  For such
	 * cases, runtime control will first go through ProcessUtility and then
	 * the executor, and we don't want the executor hooks to do anything,
	 * since we are already measuring the statement's costs at the utility
	 * level.
	 */
	if (pgsm_track_utility && pgsm_enabled(exec_nested_level))
		pstmt->queryId = UINT64CONST(0);
#endif

	/*
	 * If it's an EXECUTE statement, we don't track it and don't increment the
	 * nesting level.  This allows the cycles to be charged to the underlying
	 * PREPARE instead (by the Executor hooks), which is much more useful.
	 *
	 * We also don't track execution of PREPARE.  If we did, we would get one
	 * hash table entry for the PREPARE (with hash calculated from the query
	 * string), and then a different one with the same query string (but hash
	 * calculated from the query tree) would be used to accumulate costs of
	 * ensuing EXECUTEs.  This would be confusing, and inconsistent with other
	 * cases where planning time is not included at all.
	 *
	 * Likewise, we don't track execution of DEALLOCATE.
	 */
	if (pgsm_track_utility && pgsm_enabled(exec_nested_level) &&
		PGSM_HANDLED_UTILITY(parsetree))
	{
		pgsmEntry  *entry;
		char	   *query_text;
		int			location;
		int			query_len;
		instr_time	start;
		instr_time	duration;
		uint64		rows;
		SysInfo		sys_info;
		BufferUsage bufusage;
		BufferUsage bufusage_start = pgBufferUsage;
#if PG_VERSION_NUM >= 130000
		WalUsage	walusage;
		WalUsage	walusage_start = pgWalUsage;
#endif

		if (getrusage(RUSAGE_SELF, &rusage_start) != 0)
			elog(DEBUG1, "[pg_stat_monitor] pgsm_ProcessUtility: Failed to execute getrusage.");

		INSTR_TIME_SET_CURRENT(start);
		exec_nested_level++;

		PG_TRY();
		{
#if PG_VERSION_NUM >= 140000
			if (prev_ProcessUtility)
				prev_ProcessUtility(pstmt, queryString,
									readOnlyTree,
									context, params, queryEnv,
									dest,
									qc);
			else
				standard_ProcessUtility(pstmt, queryString,
										readOnlyTree,
										context, params, queryEnv,
										dest,
										qc);
#elif PG_VERSION_NUM >= 130000
			if (prev_ProcessUtility)
				prev_ProcessUtility(pstmt, queryString,
									context, params, queryEnv,
									dest,
									qc);
			else
				standard_ProcessUtility(pstmt, queryString,
										context, params, queryEnv,
										dest,
										qc);
#else
			if (prev_ProcessUtility)
				prev_ProcessUtility(pstmt, queryString,
									context, params, queryEnv,
									dest,
									completionTag);
			else
				standard_ProcessUtility(pstmt, queryString,
										context, params, queryEnv,
										dest,
										completionTag);
#endif
			exec_nested_level--;
		}
		PG_CATCH();
		{
			exec_nested_level--;
			PG_RE_THROW();
		}

		sys_info.utime = 0;
		sys_info.stime = 0;

		PG_END_TRY();

		if (getrusage(RUSAGE_SELF, &rusage_end) != 0)
			elog(DEBUG1, "[pg_stat_monitor] pgsm_ProcessUtility: Failed to execute getrusage.");
		else
		{
			sys_info.utime = time_diff(rusage_end.ru_utime, rusage_start.ru_utime);
			sys_info.stime = time_diff(rusage_end.ru_stime, rusage_start.ru_stime);
		}

		INSTR_TIME_SET_CURRENT(duration);
		INSTR_TIME_SUBTRACT(duration, start);

#if PG_VERSION_NUM >= 130000
#if PG_VERSION_NUM >= 140000
		rows = (qc && (qc->commandTag == CMDTAG_COPY ||
					   qc->commandTag == CMDTAG_FETCH ||
					   qc->commandTag == CMDTAG_SELECT ||
					   qc->commandTag == CMDTAG_REFRESH_MATERIALIZED_VIEW))
			? qc->nprocessed
			: 0;
#else
		rows = (qc && qc->commandTag == CMDTAG_COPY) ? qc->nprocessed : 0;
#endif
		/* calc differences of WAL counters. */
		memset(&walusage, 0, sizeof(WalUsage));
		WalUsageAccumDiff(&walusage, &pgWalUsage, &walusage_start);
#else
		/* parse command tag to retrieve the number of affected rows. */
		if (completionTag && strncmp(completionTag, "COPY ", 5) == 0)
			rows = pg_strtouint64(completionTag + 5, NULL, 10);
		else
			rows = 0;
#endif

		/* calc differences of buffer counters. */
		memset(&bufusage, 0, sizeof(BufferUsage));
		BufferUsageAccumDiff(&bufusage, &pgBufferUsage, &bufusage_start);

		/* Create an entry for this query */
		entry = pgsm_create_hash_entry(0, queryId, NULL);

		location = pstmt->stmt_location;
		query_len = pstmt->stmt_len;
		query_text = (char *) CleanQuerytext(queryString, &location, &query_len);

		entry->pgsm_query_id = get_pgsm_query_id_hash(query_text, query_len);
		entry->counters.info.cmd_type = 0;

		pgsm_add_to_list(entry, query_text, query_len);

		/* Check that we've not exceeded max_stack_depth */
		Assert(list_length(lentries) <= max_stack_depth);

		/* The plan details are captured when the query finishes */
		pgsm_update_entry(entry,	/* entry */
						  (char *) query_text,	/* query */
						  NULL, /* comments */
						  0,	/* comments length */
						  NULL, /* PlanInfo */
						  &sys_info,	/* SysInfo */
						  NULL, /* ErrorInfo */
						  0,	/* plan_total_time */
						  INSTR_TIME_GET_MILLISEC(duration),	/* exec_total_time */
						  rows, /* rows */
						  &bufusage,	/* bufusage */
#if PG_VERSION_NUM >= 130000
						  &walusage,	/* walusage */
#else
						  NULL,
#endif
						  NULL, /* jitusage */
						  false,	/* reset */
						  PGSM_EXEC);	/* kind */

		pgsm_store(entry);
	}
	else
	{
#if PG_VERSION_NUM >= 140000
		if (prev_ProcessUtility)
			prev_ProcessUtility(pstmt, queryString,
								readOnlyTree,
								context, params, queryEnv,
								dest,
								qc);
		else
			standard_ProcessUtility(pstmt, queryString,
									readOnlyTree,
									context, params, queryEnv,
									dest,
									qc);
#elif PG_VERSION_NUM >= 130000
		if (prev_ProcessUtility)
			prev_ProcessUtility(pstmt, queryString,
								context, params, queryEnv,
								dest,
								qc);
		else
			standard_ProcessUtility(pstmt, queryString,
									context, params, queryEnv,
									dest,
									qc);
#else
		if (prev_ProcessUtility)
			prev_ProcessUtility(pstmt, queryString,
								context, params, queryEnv,
								dest,
								completionTag);
		else
			standard_ProcessUtility(pstmt, queryString,
									context, params, queryEnv,
									dest,
									completionTag);
#endif
	}
}

#if PG_VERSION_NUM < 130000
static void
BufferUsageAccumDiff(BufferUsage *bufusage, BufferUsage *pgBufferUsage, BufferUsage *bufusage_start)
{
	/* calc differences of buffer counters. */
	bufusage->shared_blks_hit = pgBufferUsage->shared_blks_hit - bufusage_start->shared_blks_hit;
	bufusage->shared_blks_read = pgBufferUsage->shared_blks_read - bufusage_start->shared_blks_read;
	bufusage->shared_blks_dirtied = pgBufferUsage->shared_blks_dirtied - bufusage_start->shared_blks_dirtied;
	bufusage->shared_blks_written = pgBufferUsage->shared_blks_written - bufusage_start->shared_blks_written;
	bufusage->local_blks_hit = pgBufferUsage->local_blks_hit - bufusage_start->local_blks_hit;
	bufusage->local_blks_read = pgBufferUsage->local_blks_read - bufusage_start->local_blks_read;
	bufusage->local_blks_dirtied = pgBufferUsage->local_blks_dirtied - bufusage_start->local_blks_dirtied;
	bufusage->local_blks_written = pgBufferUsage->local_blks_written - bufusage_start->local_blks_written;
	bufusage->temp_blks_read = pgBufferUsage->temp_blks_read - bufusage_start->temp_blks_read;
	bufusage->temp_blks_written = pgBufferUsage->temp_blks_written - bufusage_start->temp_blks_written;
	bufusage->blk_read_time = pgBufferUsage->blk_read_time;
	INSTR_TIME_SUBTRACT(bufusage->blk_read_time, bufusage_start->blk_read_time);
	bufusage->blk_write_time = pgBufferUsage->blk_write_time;
	INSTR_TIME_SUBTRACT(bufusage->blk_write_time, bufusage_start->blk_write_time);
}
#endif

/*
 * Given an arbitrarily long query string, produce a hash for the purposes of
 * identifying the query, without normalizing constants.  Used when hashing
 * utility statements.
 */
static uint64
pgsm_hash_string(const char *str, int len)
{
	return DatumGetUInt64(hash_any_extended((const unsigned char *) str,
											len, 0));
}

static PgBackendStatus *
pg_get_backend_status(void)
{
	LocalPgBackendStatus *local_beentry;
	int			num_backends = pgstat_fetch_stat_numbackends();
	int			i;

	for (i = 1; i <= num_backends; i++)
	{
		PgBackendStatus *beentry;
#if PG_VERSION_NUM < 160000
		local_beentry = pgstat_fetch_stat_local_beentry(i);
#else
		local_beentry = pgstat_get_local_beentry_by_index(i);
#endif
		if (!local_beentry)
			continue;

		beentry = &local_beentry->backendStatus;

		if (beentry->st_procpid == MyProcPid)
			return beentry;
	}
	return NULL;
}

/*
 * The caller should allocate max_len memory to name including terminating null.
 * The function returns the length of the string.
 */
static int
pg_get_application_name(char *name, int buff_size)
{
	PgBackendStatus *beentry;

	/* Try to read application name from GUC directly */
	if (application_name && *application_name)
		snprintf(name, buff_size, "%s", application_name);
	else
	{
		beentry = pg_get_backend_status();

		if (!beentry)
			snprintf(name, buff_size, "%s", "unknown");
		else
			snprintf(name, buff_size, "%s", beentry->st_appname);
	}

	/* Return length so that others don't have to calculate */
	return strlen(name);
}

static uint
pg_get_client_addr(bool *ok)
{
	PgBackendStatus *beentry = pg_get_backend_status();
	char		remote_host[NI_MAXHOST];
	int			ret;

	remote_host[0] = '\0';

	if (!beentry)
		return ntohl(inet_addr("127.0.0.1"));

	*ok = true;

	ret = pg_getnameinfo_all(&beentry->st_clientaddr.addr,
							 beentry->st_clientaddr.salen,
							 remote_host, sizeof(remote_host),
							 NULL, 0,
							 NI_NUMERICHOST | NI_NUMERICSERV);
	if (ret != 0)
		return ntohl(inet_addr("127.0.0.1"));

	if (strcmp(remote_host, "[local]") == 0)
		return ntohl(inet_addr("127.0.0.1"));

	return ntohl(inet_addr(remote_host));
}

static void
pgsm_update_entry(pgsmEntry * entry,
				  const char *query,
				  char *comments,
				  int comments_len,
				  PlanInfo * plan_info,
				  SysInfo * sys_info,
				  ErrorInfo * error_info,
				  double plan_total_time,
				  double exec_total_time,
				  uint64 rows,
				  BufferUsage *bufusage,
				  WalUsage *walusage,
				  const struct JitInstrumentation *jitusage,
				  bool reset,
				  pgsmStoreKind kind)
{
	int			index;
	double		old_mean;
	int			message_len = error_info ? strlen(error_info->message) : 0;
	int			sqlcode_len = error_info ? strlen(error_info->sqlcode) : 0;
	int			plan_text_len = plan_info ? plan_info->plan_len : 0;

	/* Start collecting data for next bucket and reset all counters */
	if (reset)
		memset(&entry->counters, 0, sizeof(Counters));

	/* volatile block */
	{
		volatile	pgsmEntry *e = (volatile pgsmEntry *) entry;

		if (kind == PGSM_STORE)
			SpinLockAcquire(&e->mutex);

		/*
		 * Extract comments if enabled and only when the query has completed
		 * with or without error
		 */
		if (pgsm_extract_comments && kind == PGSM_STORE
			&& !e->counters.info.comments[0] && comments_len > 0)
			_snprintf(e->counters.info.comments, comments, comments_len + 1, COMMENTS_LEN);

		if (kind == PGSM_PLAN || kind == PGSM_STORE)
		{
			if (e->counters.plancalls.calls == 0)
				e->counters.plancalls.usage = USAGE_INIT;

			e->counters.plancalls.calls += 1;
			e->counters.plantime.total_time += plan_total_time;

			if (e->counters.plancalls.calls == 1)
			{
				e->counters.plantime.min_time = plan_total_time;
				e->counters.plantime.max_time = plan_total_time;
				e->counters.plantime.mean_time = plan_total_time;
			}
			else
			{
				/* Increment the counts, except when jstate is not NULL */
				old_mean = e->counters.plantime.mean_time;

				e->counters.plantime.mean_time += (plan_total_time - old_mean) / e->counters.plancalls.calls;
				e->counters.plantime.sum_var_time += (plan_total_time - old_mean) * (plan_total_time - e->counters.plantime.mean_time);

				/* calculate min and max time */
				if (e->counters.plantime.min_time > plan_total_time)
					e->counters.plantime.min_time = plan_total_time;

				if (e->counters.plantime.max_time < plan_total_time)
					e->counters.plantime.max_time = plan_total_time;
			}
		}

		if (kind == PGSM_EXEC || kind == PGSM_STORE)
		{
			if (e->counters.calls.calls == 0)
				e->counters.calls.usage = USAGE_INIT;

			e->counters.calls.calls += 1;
			e->counters.time.total_time += exec_total_time;

			if (e->counters.calls.calls == 1)
			{
				e->counters.time.min_time = exec_total_time;
				e->counters.time.max_time = exec_total_time;
				e->counters.time.mean_time = exec_total_time;
			}
			else
			{
				/* Increment the counts, except when jstate is not NULL */
				old_mean = e->counters.time.mean_time;
				e->counters.time.mean_time += (exec_total_time - old_mean) / e->counters.calls.calls;
				e->counters.time.sum_var_time += (exec_total_time - old_mean) * (exec_total_time - e->counters.time.mean_time);

				/* calculate min and max time */
				if (e->counters.time.min_time > exec_total_time)
					e->counters.time.min_time = exec_total_time;

				if (e->counters.time.max_time < exec_total_time)
					e->counters.time.max_time = exec_total_time;
			}

			index = get_histogram_bucket(exec_total_time);
			e->counters.resp_calls[index]++;
		}

		if (plan_text_len > 0 && !e->counters.planinfo.plan_text[0])
		{
			e->counters.planinfo.planid = plan_info->planid;
			e->counters.planinfo.plan_len = plan_text_len;
			_snprintf(e->counters.planinfo.plan_text, plan_info->plan_text, plan_text_len + 1, PLAN_TEXT_LEN);
		}

		/* Only should process this once when storing the data */
		if (kind == PGSM_STORE)
		{
			if (app_name_len > 0 && !e->counters.info.application_name[0])
				_snprintf(e->counters.info.application_name, app_name, app_name_len + 1, APPLICATIONNAME_LEN);

			e->counters.info.num_relations = num_relations;
			_snprintf2(e->counters.info.relations, relations, num_relations, REL_LEN);

			if (exec_nested_level > 0 && e->counters.info.parentid == 0 && pgsm_track == PGSM_TRACK_ALL)
			{
				if (exec_nested_level >= 0 && exec_nested_level < max_stack_depth)
				{
					int			parent_query_len = nested_query_txts[exec_nested_level - 1] ?
					strlen(nested_query_txts[exec_nested_level - 1]) : 0;

					e->counters.info.parentid = nested_queryids[exec_nested_level - 1];
					e->counters.info.parent_query = InvalidDsaPointer;
					/* If we have a parent query, store it in the raw dsa area */
					if (parent_query_len > 0)
					{
						char	   *qry_buff;
						dsa_area   *query_dsa_area = get_dsa_area_for_query_text();

						/*
						 * Use dsa_allocate_extended with DSA_ALLOC_NO_OOM
						 * flag, as we don't want to get an error if memory
						 * allocation fails.
						 */
						dsa_pointer qry = dsa_allocate_extended(query_dsa_area, parent_query_len + 1, DSA_ALLOC_NO_OOM | DSA_ALLOC_ZERO);

						if (DsaPointerIsValid(qry))
						{
							qry_buff = dsa_get_address(query_dsa_area, qry);
							memcpy(qry_buff, nested_query_txts[exec_nested_level - 1], parent_query_len);
							qry_buff[parent_query_len] = 0;
							/* store the dsa pointer for parent query text */
							e->counters.info.parent_query = qry;
						}
					}
				}
			}
			else
			{
				e->counters.info.parentid = UINT64CONST(0);
				e->counters.info.parent_query = InvalidDsaPointer;
			}
		}

		if (error_info)
		{
			e->counters.error.elevel = error_info->elevel;
			_snprintf(e->counters.error.sqlcode, error_info->sqlcode, sqlcode_len, SQLCODE_LEN);
			_snprintf(e->counters.error.message, error_info->message, message_len, ERROR_MESSAGE_LEN);
		}

		e->counters.calls.rows += rows;

		if (bufusage)
		{
			e->counters.blocks.shared_blks_hit += bufusage->shared_blks_hit;
			e->counters.blocks.shared_blks_read += bufusage->shared_blks_read;
			e->counters.blocks.shared_blks_dirtied += bufusage->shared_blks_dirtied;
			e->counters.blocks.shared_blks_written += bufusage->shared_blks_written;
			e->counters.blocks.local_blks_hit += bufusage->local_blks_hit;
			e->counters.blocks.local_blks_read += bufusage->local_blks_read;
			e->counters.blocks.local_blks_dirtied += bufusage->local_blks_dirtied;
			e->counters.blocks.local_blks_written += bufusage->local_blks_written;
			e->counters.blocks.temp_blks_read += bufusage->temp_blks_read;
			e->counters.blocks.temp_blks_written += bufusage->temp_blks_written;
			e->counters.blocks.blk_read_time += INSTR_TIME_GET_MILLISEC(bufusage->blk_read_time);
			e->counters.blocks.blk_write_time += INSTR_TIME_GET_MILLISEC(bufusage->blk_write_time);
#if PG_VERSION_NUM >= 150000
			e->counters.blocks.temp_blk_read_time += INSTR_TIME_GET_MILLISEC(bufusage->temp_blk_read_time);
			e->counters.blocks.temp_blk_write_time += INSTR_TIME_GET_MILLISEC(bufusage->temp_blk_write_time);
#endif

			memcpy((void *) &e->counters.blocks.instr_blk_read_time, &bufusage->blk_read_time, sizeof(instr_time));
			memcpy((void *) &e->counters.blocks.instr_blk_write_time, &bufusage->blk_write_time, sizeof(instr_time));

#if PG_VERSION_NUM >= 150000
			memcpy((void *) &e->counters.blocks.instr_temp_blk_read_time, &bufusage->temp_blk_read_time, sizeof(bufusage->temp_blk_read_time));
			memcpy((void *) &e->counters.blocks.instr_temp_blk_write_time, &bufusage->temp_blk_write_time, sizeof(bufusage->temp_blk_write_time));
#endif
		}

		e->counters.calls.usage += USAGE_EXEC(exec_total_time + plan_total_time);

		if (sys_info)
		{
			e->counters.sysinfo.utime += sys_info->utime;
			e->counters.sysinfo.stime += sys_info->stime;
		}
		if (walusage)
		{
			e->counters.walusage.wal_records += walusage->wal_records;
			e->counters.walusage.wal_fpi += walusage->wal_fpi;
			e->counters.walusage.wal_bytes += walusage->wal_bytes;
		}
		if (jitusage)
		{
			e->counters.jitinfo.jit_functions += jitusage->created_functions;
			e->counters.jitinfo.jit_generation_time += INSTR_TIME_GET_MILLISEC(jitusage->generation_counter);

			if (INSTR_TIME_GET_MILLISEC(jitusage->inlining_counter))
				e->counters.jitinfo.jit_inlining_count++;
			e->counters.jitinfo.jit_inlining_time += INSTR_TIME_GET_MILLISEC(jitusage->inlining_counter);

			if (INSTR_TIME_GET_MILLISEC(jitusage->optimization_counter))
				e->counters.jitinfo.jit_optimization_count++;
			e->counters.jitinfo.jit_optimization_time += INSTR_TIME_GET_MILLISEC(jitusage->optimization_counter);

			if (INSTR_TIME_GET_MILLISEC(jitusage->emission_counter))
				e->counters.jitinfo.jit_emission_count++;
			e->counters.jitinfo.jit_emission_time += INSTR_TIME_GET_MILLISEC(jitusage->emission_counter);

			/* Only do this for local storage scenarios */
			if (kind != PGSM_STORE)
			{
				memcpy((void *) &e->counters.jitinfo.instr_generation_counter, &jitusage->generation_counter, sizeof(instr_time));
				memcpy((void *) &e->counters.jitinfo.instr_inlining_counter, &jitusage->inlining_counter, sizeof(instr_time));
				memcpy((void *) &e->counters.jitinfo.instr_optimization_counter, &jitusage->optimization_counter, sizeof(instr_time));
				memcpy((void *) &e->counters.jitinfo.instr_emission_counter, &jitusage->emission_counter, sizeof(instr_time));
			}
		}

		if (kind == PGSM_STORE)
			SpinLockRelease(&e->mutex);
	}
}

static void
pgsm_store_error(const char *query, ErrorData *edata)
{
	pgsmEntry  *entry;
	uint64		queryid = 0;
	int			len = strlen(query);

	if (!query || len == 0)
		return;

	len = strlen(query);

	queryid = pgsm_hash_string(query, len);

	entry = pgsm_create_hash_entry(0, queryid, NULL);
	entry->query_text.query_pointer = pnstrdup(query, len);

	entry->pgsm_query_id = get_pgsm_query_id_hash(query, len);

	entry->counters.error.elevel = edata->elevel;
	snprintf(entry->counters.error.message, ERROR_MESSAGE_LEN, "%s", edata->message);
	snprintf(entry->counters.error.sqlcode, SQLCODE_LEN, "%s", unpack_sql_state(edata->sqlerrcode));

	pgsm_store(entry);
}

static void
pgsm_add_to_list(pgsmEntry * entry, char *query_text, int query_len)
{
	/* Switch to pgsm memory context */
	MemoryContext oldctx = MemoryContextSwitchTo(GetPgsmMemoryContext());

	entry->query_text.query_pointer = pnstrdup(query_text, query_len);
	lentries = lappend(lentries, entry);
	MemoryContextSwitchTo(oldctx);
}

static pgsmEntry *
pgsm_get_entry_for_query(uint64 queryid, PlanInfo * plan_info, const char *query_text, int query_len, bool create)
{
	pgsmEntry  *entry = NULL;
	ListCell   *lc = NULL;

	/* First bet is on the last entry */
	if (lentries == NIL && !create)
		return NULL;

	if (lentries)
	{
		entry = (pgsmEntry *) llast(lentries);
		if (entry->key.queryid == queryid)
			return entry;

		foreach(lc, lentries)
		{
			entry = lfirst(lc);
			if (entry->key.queryid == queryid)
				return entry;
		}
	}
	if (create && query_text)
	{
		/*
		 * At this point, we don't know which bucket this query will land in,
		 * so passing 0. The store function MUST later update it based on the
		 * current bucket value. The correct bucket value will be needed then
		 * to search the hash table, or create the appropriate entry.
		 */
		entry = pgsm_create_hash_entry(0, queryid, plan_info);

		/*
		 * Update other member that are not counters, so that we don't have to
		 * worry about these.
		 */
		entry->pgsm_query_id = get_pgsm_query_id_hash(query_text, query_len);
		pgsm_add_to_list(entry, (char *) query_text, query_len);
	}

	return entry;
}

static void
pgsm_cleanup_callback(void *arg)
{
	/* Reset the memory context holding the list */
	MemoryContextReset(GetPgsmMemoryContext());

	lentries = NIL;
	callback_setup = false;
}

/*
 * Function encapsulating some external calls for filling up the hash key data structure.
 * The bucket_id may not be known at this stage. So pass any value that you may wish.
 */
static pgsmEntry *
pgsm_create_hash_entry(uint64 bucket_id, uint64 queryid, PlanInfo * plan_info)
{
	pgsmEntry  *entry;
	int			sec_ctx;
	bool		found_client_addr = false;
	char	   *app_name_ptr = app_name;
	MemoryContext oldctx;
	char	   *datname = NULL;
	char	   *username = NULL;

	/* Create an entry in the pgsm memory context */
	oldctx = MemoryContextSwitchTo(GetPgsmMemoryContext());
	entry = palloc0(sizeof(pgsmEntry));

	/*
	 * Get the user ID. Let's use this instead of GetUserID as this won't
	 * throw an assertion in case of an error.
	 */
	GetUserIdAndSecContext((Oid *) &entry->key.userid, &sec_ctx);

	/* Get the application name and set appid */
	app_name_len = pg_get_application_name(app_name, APPLICATIONNAME_LEN);
	entry->key.appid = pgsm_hash_string((const char *) app_name_ptr, app_name_len);

	/* client address */
	if (!pgsm_client_ip_is_valid())
		pgsm_client_ip = pg_get_client_addr(&found_client_addr);

	entry->key.ip = pgsm_client_ip;

	/* PlanID, if there is one */
	entry->key.planid = plan_info ? plan_info->planid : 0;

	/* Set remaining data */
	entry->key.dbid = MyDatabaseId;
	entry->key.queryid = queryid;
	entry->key.bucket_id = bucket_id;

#if PG_VERSION_NUM < 140000
	entry->key.toplevel = 1;
#else
	entry->key.toplevel = ((exec_nested_level + plan_nested_level) == 0);
#endif

	if (IsTransactionState())
	{
		datname = get_database_name(entry->key.dbid);
		username = GetUserNameFromId(entry->key.userid, true);
	}

	if (!datname)
		datname = pnstrdup("<database name not available>", sizeof(entry->datname) - 1);

	if (!username)
		username = pnstrdup("<user name not available>", sizeof(entry->username) - 1);

	snprintf(entry->datname, sizeof(entry->datname), "%s", datname);
	snprintf(entry->username, sizeof(entry->username), "%s", username);

	pfree(datname);
	pfree(username);

	MemoryContextSwitchTo(oldctx);

	return entry;
}


/*
 * Store some statistics for a statement.
 *
 * If queryId is 0 then this is a utility statement and we should compute
 * a suitable queryId internally.
 *
 * If jstate is not NULL then we're trying to create an entry for which
 * we have no statistics as yet; we just want to record the normalized
 * query string.  total_time, rows, bufusage are ignored in this case.
 */
static void
pgsm_store(pgsmEntry * entry)
{
	pgsmEntry  *shared_hash_entry;
	pgsmSharedState *pgsm;
	bool		found;
	uint64		bucketid;
	uint64		prev_bucket_id;
	bool		reset = false;	/* Only used in update function - HAMID */
	char	   *query;
	int			query_len;
	BufferUsage bufusage;
	WalUsage	walusage;
	JitInstrumentation jitusage;
	char		comments[COMMENTS_LEN] = {0};
	int			comments_len;

	/* Safety check... */
	if (!IsSystemInitialized())
		return;

	pgsm = pgsm_get_ss();

	/*
	 * We should lock the hash table here what if the bucket is removed; e.g.
	 * reset is called - HAMID
	 */
	prev_bucket_id = pg_atomic_read_u64(&pgsm->current_wbucket);
	bucketid = get_next_wbucket(pgsm);

	if (bucketid != prev_bucket_id)
		reset = true;

	entry->key.bucket_id = bucketid;
	query = entry->query_text.query_pointer;
	query_len = strlen(query);

	/* Let's do all the leg work here before we acquire any locks */
	extract_query_comments(query, comments, sizeof(comments));
	comments_len = strlen(comments);

	/* bufusage */
	bufusage.shared_blks_hit = entry->counters.blocks.shared_blks_hit;
	bufusage.shared_blks_read = entry->counters.blocks.shared_blks_read;
	bufusage.shared_blks_dirtied = entry->counters.blocks.shared_blks_dirtied;
	bufusage.shared_blks_written = entry->counters.blocks.shared_blks_written;
	bufusage.local_blks_hit = entry->counters.blocks.local_blks_hit;
	bufusage.local_blks_read = entry->counters.blocks.local_blks_read;
	bufusage.local_blks_dirtied = entry->counters.blocks.local_blks_dirtied;
	bufusage.local_blks_written = entry->counters.blocks.local_blks_written;
	bufusage.temp_blks_read = entry->counters.blocks.temp_blks_read;
	bufusage.temp_blks_written = entry->counters.blocks.temp_blks_written;

	memcpy(&bufusage.blk_read_time, &entry->counters.blocks.instr_blk_read_time, sizeof(instr_time));
	memcpy(&bufusage.blk_write_time, &entry->counters.blocks.instr_blk_write_time, sizeof(instr_time));

#if PG_VERSION_NUM >= 150000
	memcpy(&bufusage.temp_blk_read_time, &entry->counters.blocks.instr_temp_blk_read_time, sizeof(instr_time));
	memcpy(&bufusage.temp_blk_write_time, &entry->counters.blocks.instr_temp_blk_write_time, sizeof(instr_time));
#endif

	/* walusage */
	walusage.wal_records = entry->counters.walusage.wal_records;
	walusage.wal_fpi = entry->counters.walusage.wal_fpi;
	walusage.wal_bytes = entry->counters.walusage.wal_bytes;

	/* jit */
	jitusage.created_functions = entry->counters.jitinfo.jit_functions;
	memcpy(&jitusage.generation_counter, &entry->counters.jitinfo.instr_generation_counter, sizeof(instr_time));
	memcpy(&jitusage.inlining_counter, &entry->counters.jitinfo.instr_inlining_counter, sizeof(instr_time));
	memcpy(&jitusage.optimization_counter, &entry->counters.jitinfo.instr_optimization_counter, sizeof(instr_time));
	memcpy(&jitusage.emission_counter, &entry->counters.jitinfo.instr_emission_counter, sizeof(instr_time));

	/*
	 * Acquire a share lock to start with. We'd have to acquire exclusive if
	 * we need ot create the entry.
	 */
	LWLockAcquire(pgsm->lock, LW_SHARED);
	shared_hash_entry = (pgsmEntry *) pgsm_hash_find(get_pgsmHash(), &entry->key, &found);

	if (!shared_hash_entry)
	{
		dsa_pointer dsa_query_pointer;
		dsa_area   *query_dsa_area;
		char	   *query_buff;

		/* New query, truncate length if necessary. */
		if (query_len > pgsm_query_max_len)
			query_len = pgsm_query_max_len;

		/* Save the query text in raw dsa area */
		query_dsa_area = get_dsa_area_for_query_text();
		dsa_query_pointer = dsa_allocate_extended(query_dsa_area, query_len + 1, DSA_ALLOC_NO_OOM | DSA_ALLOC_ZERO);
		if (!DsaPointerIsValid(dsa_query_pointer))
		{
			LWLockRelease(pgsm->lock);
			return;
		}

		/*
		 * Get the memory address from DSA pointer and copy the query text in
		 * local variable
		 */
		query_buff = dsa_get_address(query_dsa_area, dsa_query_pointer);
		memcpy(query_buff, query, query_len);

		LWLockRelease(pgsm->lock);
		LWLockAcquire(pgsm->lock, LW_EXCLUSIVE);

		/* OK to create a new hashtable entry */
		PGSM_DISABLE_ERROR_CAPUTRE();
		{
			PG_TRY();
			{
				shared_hash_entry = hash_entry_alloc(pgsm, &entry->key, GetDatabaseEncoding());
			}
			PG_CATCH();
			{
				LWLockRelease(pgsm->lock);

				if (DsaPointerIsValid(dsa_query_pointer))
					dsa_free(query_dsa_area, dsa_query_pointer);
				PG_RE_THROW();
			}
			PG_END_TRY();
		} PGSM_END_DISABLE_ERROR_CAPTURE();

		if (shared_hash_entry == NULL)
		{
			/*
			 * Out of memory; report only if the state has changed now.
			 * Otherwise we risk filling up the log file with these message.
			 */
			if (!IsSystemOOM())
			{
				pgsm->pgsm_oom = true;

				ereport(WARNING,
						(errcode(ERRCODE_OUT_OF_MEMORY),
						 errmsg("[pg_stat_monitor] pgsm_store: Hash table is out of memory and can no longer store queries!"),
						 errdetail("You may reset the view or when the buckets are deallocated, pg_stat_monitor will resume saving " \
								   "queries. Alternatively, try increasing the value of pg_stat_monitor.pgsm_max.")));
			}

			LWLockRelease(pgsm->lock);

			if (DsaPointerIsValid(dsa_query_pointer))
				dsa_free(query_dsa_area, dsa_query_pointer);

			return;
		}
		else
		{
			/* If we got a new entry, reset the oom value false */
			pgsm->pgsm_oom = false;
		}

		/* If we already have the pointer set, free this one */
		if (DsaPointerIsValid(shared_hash_entry->query_text.query_pos))
			dsa_free(query_dsa_area, dsa_query_pointer);
		else
			shared_hash_entry->query_text.query_pos = dsa_query_pointer;

		shared_hash_entry->pgsm_query_id = entry->pgsm_query_id;
		shared_hash_entry->encoding = entry->encoding;
		shared_hash_entry->counters.info.cmd_type = entry->counters.info.cmd_type;

		snprintf(shared_hash_entry->datname, sizeof(shared_hash_entry->datname), "%s", entry->datname);
		snprintf(shared_hash_entry->username, sizeof(shared_hash_entry->username), "%s", entry->username);
	}

	pgsm_update_entry(shared_hash_entry,	/* entry */
					  query,	/* query */
					  comments, /* comments */
					  comments_len, /* comments length */
					  &entry->counters.planinfo,	/* PlanInfo */
					  &entry->counters.sysinfo, /* SysInfo */
					  &entry->counters.error,	/* ErrorInfo */
					  entry->counters.plantime.total_time,	/* plan_total_time */
					  entry->counters.time.total_time,	/* exec_total_time */
					  entry->counters.calls.rows,	/* rows */
					  &bufusage,	/* bufusage */
					  &walusage,	/* walusage */
					  &jitusage,	/* jitusage */
					  reset,	/* reset */
					  PGSM_STORE);

	memset(&entry->counters, 0, sizeof(entry->counters));
	LWLockRelease(pgsm->lock);
}

/*
 * Reset all statement statistics.
 */
Datum
pg_stat_monitor_reset(PG_FUNCTION_ARGS)
{
	pgsmSharedState *pgsm;

	/* Safety check... */
	if (!IsSystemInitialized())
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("pg_stat_monitor: must be loaded via shared_preload_libraries")));

	pgsm = pgsm_get_ss();
	LWLockAcquire(pgsm->lock, LW_EXCLUSIVE);
	hash_entry_dealloc(-1, -1, NULL);

	LWLockRelease(pgsm->lock);
	PG_RETURN_VOID();
}

Datum
pg_stat_monitor_1_0(PG_FUNCTION_ARGS)
{
	pg_stat_monitor_internal(fcinfo, PGSM_V1_0, true);
	return (Datum) 0;
}

Datum
pg_stat_monitor_2_0(PG_FUNCTION_ARGS)
{
	pg_stat_monitor_internal(fcinfo, PGSM_V2_0, true);
	return (Datum) 0;
}

/*
  * Legacy entry point for pg_stat_monitor() API versions 1.0
  */
Datum
pg_stat_monitor(PG_FUNCTION_ARGS)
{
	pg_stat_monitor_internal(fcinfo, PGSM_V1_0, true);
	return (Datum) 0;
}

static bool
IsBucketValid(uint64 bucketid)
{
	long		secs;
	int			microsecs;
	TimestampTz current_tz = GetCurrentTimestamp();
	pgsmSharedState *pgsm = pgsm_get_ss();

	TimestampDifference(pgsm->bucket_start_time[bucketid], current_tz, &secs, &microsecs);

	if (secs > (pgsm_bucket_time * pgsm_max_buckets))
		return false;
	return true;
}

/* Common code for all versions of pg_stat_monitor() */
static void
pg_stat_monitor_internal(FunctionCallInfo fcinfo,
						 pgsmVersion api_version,
						 bool showtext)
{
	ReturnSetInfo *rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
	TupleDesc	tupdesc;
	Tuplestorestate *tupstore;
	MemoryContext per_query_ctx;
	MemoryContext oldcontext;
	PGSM_HASH_SEQ_STATUS hstat;
	pgsmEntry  *entry;
	pgsmSharedState *pgsm;
	int			expected_columns = (api_version >= PGSM_V2_0) ? PG_STAT_MONITOR_COLS_V2_0 : PG_STAT_MONITOR_COLS_V1_0;

	/* Disallow old api usage */
	if (api_version < PGSM_V2_0)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("[pg_stat_monitor] pg_stat_monitor_internal: API version not supported."),
				 errhint("Upgrade pg_stat_monitor extension")));
	/* Safety check... */
	if (!IsSystemInitialized())
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("[pg_stat_monitor] pg_stat_monitor_internal: Must be loaded via shared_preload_libraries.")));

	/* Out of memory? */
	if (IsSystemOOM())
		ereport(WARNING,
				(errcode(ERRCODE_OUT_OF_MEMORY),
				 errmsg("[pg_stat_monitor] pg_stat_monitor_internal: Hash table is out of memory and can no longer store queries!"),
				 errdetail("You may reset the view or when the buckets are deallocated, pg_stat_monitor will resume saving " \
						   "queries. Alternatively, try increasing the value of pg_stat_monitor.pgsm_max.")));

	/* check to see if caller supports us returning a tuplestore */
	if (rsinfo == NULL || !IsA(rsinfo, ReturnSetInfo))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("[pg_stat_monitor] pg_stat_monitor_internal: Set-valued function called in context that cannot accept a set.")));
	if (!(rsinfo->allowedModes & SFRM_Materialize))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("[pg_stat_monitor] pg_stat_monitor_internal: Materialize mode required, but it is not " \
						"allowed in this context.")));

	/* Switch into long-lived context to construct returned data structures */
	per_query_ctx = rsinfo->econtext->ecxt_per_query_memory;
	oldcontext = MemoryContextSwitchTo(per_query_ctx);

	/* Build a tuple descriptor for our result type */
	if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
		elog(ERROR, "[pg_stat_monitor] pg_stat_monitor_internal: Return type must be a row type.");

	if (tupdesc->natts != expected_columns)
		elog(ERROR, "[pg_stat_monitor] pg_stat_monitor_internal: Incorrect number of output arguments, received %d, required %d.", tupdesc->natts, expected_columns);

	tupstore = tuplestore_begin_heap(true, false, work_mem);
	rsinfo->returnMode = SFRM_Materialize;
	rsinfo->setResult = tupstore;
	rsinfo->setDesc = tupdesc;

	MemoryContextSwitchTo(oldcontext);

	pgsm = pgsm_get_ss();
	LWLockAcquire(pgsm->lock, LW_SHARED);
	pgsm_hash_seq_init(&hstat, get_pgsmHash(), false);

	while ((entry = pgsm_hash_seq_next(&hstat)) != NULL)
	{
		Datum		values[PG_STAT_MONITOR_COLS] = {0};
		bool		nulls[PG_STAT_MONITOR_COLS] = {0};
		int			i = 0;
		Counters	tmp;
		double		stddev;
		uint64		queryid = entry->key.queryid;
		int64		bucketid = entry->key.bucket_id;
		Oid			dbid = entry->key.dbid;
		Oid			userid = entry->key.userid;
		uint64		ip = (uint64) entry->key.ip;
		uint64		planid = entry->key.planid;
		uint64		pgsm_query_id = entry->pgsm_query_id;
		dsa_area   *query_dsa_area;
		char	   *query_ptr;
		char	   *query_txt = NULL;
		char	   *parent_query_txt = NULL;

		bool		toplevel = entry->key.toplevel;
#if PG_VERSION_NUM < 140000
		bool		is_allowed_role = is_member_of_role(GetUserId(), DEFAULT_ROLE_READ_ALL_STATS);
#else
		bool		is_allowed_role = is_member_of_role(GetUserId(), ROLE_PG_READ_ALL_STATS);
#endif
		/* Load the query text from dsa area */
		if (DsaPointerIsValid(entry->query_text.query_pos))
		{
			query_dsa_area = get_dsa_area_for_query_text();
			query_ptr = dsa_get_address(query_dsa_area, entry->query_text.query_pos);
			query_txt = pstrdup(query_ptr);
		}
		else
			query_txt = pstrdup("Query string not available");	/* Should never happen.
																 * Just a safty check */

		/* copy counters to a local variable to keep locking time short */
		{
			volatile	pgsmEntry *e = (volatile pgsmEntry *) entry;

			SpinLockAcquire(&e->mutex);
			tmp = e->counters;
			SpinLockRelease(&e->mutex);
		}

		/*
		 * In case that query plan is enabled, there is no need to show 0
		 * planid query
		 */
		if (tmp.info.cmd_type == CMD_SELECT && pgsm_enable_query_plan && planid == 0)
			continue;

		if (!IsBucketValid(bucketid))
		{
			continue;
		}

		/* read the parent query text if any */
		if (tmp.info.parentid != UINT64CONST(0))
		{
			if (DsaPointerIsValid(tmp.info.parent_query))
			{
				query_dsa_area = get_dsa_area_for_query_text();
				query_ptr = dsa_get_address(query_dsa_area, tmp.info.parent_query);
				parent_query_txt = pstrdup(query_ptr);
			}
			else
				parent_query_txt = pstrdup("parent query text not available");
		}
		/* bucketid at column number 0 */
		values[i++] = Int64GetDatumFast(bucketid);

		/* userid at column number 1 */
		values[i++] = ObjectIdGetDatum(userid);

		/* userid at column number 1 */
		values[i++] = CStringGetTextDatum(entry->username);

		/* dbid at column number 2 */
		values[i++] = ObjectIdGetDatum(dbid);

		/* userid at column number 1 */
		values[i++] = CStringGetTextDatum(entry->datname);

		/*
		 * ip address at column number 3, Superusers or members of
		 * pg_read_all_stats members are allowed
		 */
		if (is_allowed_role || userid == GetUserId())
			values[i++] = UInt32GetDatum(ip);
		else
			nulls[i++] = true;

		/* queryid at column number 4 */
		values[i++] = UInt64GetDatum(queryid);

		/* planid at column number 5 */
		if (planid)
		{
			values[i++] = UInt64GetDatum(planid);
		}
		else
		{
			nulls[i++] = true;
		}
		if (is_allowed_role || userid == GetUserId())
		{
			if (showtext)
			{
				char	   *enc;

				/* query at column number 6 */
				enc = pg_any_to_server(query_txt, strlen(query_txt), GetDatabaseEncoding());
				values[i++] = CStringGetTextDatum(enc);
				if (enc != query_txt)
					pfree(enc);
				/* plan at column number 7 */
				if (planid && tmp.planinfo.plan_text[0])
					values[i++] = CStringGetTextDatum(tmp.planinfo.plan_text);
				else
					nulls[i++] = true;
			}
			else
			{
				/* query at column number 6 */
				nulls[i++] = true;
				/* plan at column number 7 */
				nulls[i++] = true;
			}
		}
		else
		{
			/* query text at column number 6 */
			values[i++] = CStringGetTextDatum("<insufficient privilege>");
			values[i++] = CStringGetTextDatum("<insufficient privilege>");
		}

		if (pgsm_query_id)
			values[i++] = UInt64GetDatum(pgsm_query_id);
		else
			nulls[i++] = true;

		/* parentid at column number 9 */
		if (tmp.info.parentid != UINT64CONST(0))
		{
			values[i++] = UInt64GetDatum(tmp.info.parentid);
			values[i++] = CStringGetTextDatum(parent_query_txt);
		}
		else
		{
			nulls[i++] = true;
			nulls[i++] = true;
		}

		/* application_name at column number 10 */
		if (strlen(tmp.info.application_name) > 0)
			values[i++] = CStringGetTextDatum(tmp.info.application_name);
		else
			nulls[i++] = true;

		/* relations at column number 10 */
		if (tmp.info.num_relations > 0)
		{
			int			j;
			char	   *text_str = palloc0(TOTAL_RELS_LENGTH);
			char	   *tmp_str = palloc0(TOTAL_RELS_LENGTH);
			bool		first = true;

			/*
			 * Need to calculate the actual size, and avoid unnessary memory
			 * usage
			 */
			for (j = 0; j < tmp.info.num_relations; j++)
			{
				if (first)
				{
					snprintf(text_str, 1024, "%s", tmp.info.relations[j]);
					first = false;
					continue;
				}
				snprintf(tmp_str, 1024, "%s,%s", text_str, tmp.info.relations[j]);
				snprintf(text_str, 1024, "%s", tmp_str);
			}
			pfree(tmp_str);
			values[i++] = CStringGetTextDatum(text_str);
		}
		else
			nulls[i++] = true;

		/* cmd_type at column number 11 */
		if (tmp.info.cmd_type == CMD_NOTHING)
			nulls[i++] = true;
		else
			values[i++] = Int64GetDatumFast((int64) tmp.info.cmd_type);

		/* elevel at column number 12 */
		values[i++] = Int64GetDatumFast(tmp.error.elevel);

		/* sqlcode at column number 13 */
		if (strlen(tmp.error.sqlcode) == 0)
			nulls[i++] = true;
		else
			values[i++] = CStringGetTextDatum(tmp.error.sqlcode);

		/* message at column number 14 */
		if (strlen(tmp.error.message) == 0)
			nulls[i++] = true;
		else
			values[i++] = CStringGetTextDatum(tmp.error.message);

		/* bucket_start_time at column number 15 */
		values[i++] = TimestampTzGetDatum(pgsm->bucket_start_time[entry->key.bucket_id]);

		if (tmp.calls.calls == 0)
		{
			/* Query of pg_stat_monitor itslef started from zero count */
			tmp.calls.calls++;
			tmp.resp_calls[0]++;
		}

		/* calls at column number 16 */
		values[i++] = Int64GetDatumFast(tmp.calls.calls);

		/* total_time at column number 17 */
		values[i++] = Float8GetDatumFast(tmp.time.total_time);

		/* min_time at column number 18 */
		values[i++] = Float8GetDatumFast(tmp.time.min_time);

		/* max_time at column number 19 */
		values[i++] = Float8GetDatumFast(tmp.time.max_time);

		/* mean_time at column number 20 */
		values[i++] = Float8GetDatumFast(tmp.time.mean_time);
		if (tmp.calls.calls > 1)
			stddev = sqrt(tmp.time.sum_var_time / tmp.calls.calls);
		else
			stddev = 0.0;

		/* calls at column number 21 */
		values[i++] = Float8GetDatumFast(stddev);

		/* calls at column number 22 */
		values[i++] = Int64GetDatumFast(tmp.calls.rows);

		if (tmp.calls.calls == 0)
		{
			/* Query of pg_stat_monitor itslef started from zero count */
			tmp.calls.calls++;
			tmp.resp_calls[0]++;
		}

		/* calls at column number 23 */
		values[i++] = Int64GetDatumFast(tmp.plancalls.calls);

		/* total_time at column number 24 */
		values[i++] = Float8GetDatumFast(tmp.plantime.total_time);

		/* min_time at column number 25 */
		values[i++] = Float8GetDatumFast(tmp.plantime.min_time);

		/* max_time at column number 26 */
		values[i++] = Float8GetDatumFast(tmp.plantime.max_time);

		/* mean_time at column number 27 */
		values[i++] = Float8GetDatumFast(tmp.plantime.mean_time);
		if (tmp.plancalls.calls > 1)
			stddev = sqrt(tmp.plantime.sum_var_time / tmp.plancalls.calls);
		else
			stddev = 0.0;

		/* calls at column number 28 */
		values[i++] = Float8GetDatumFast(stddev);

		/* blocks are from column number 29 - 40 */
		values[i++] = Int64GetDatumFast(tmp.blocks.shared_blks_hit);
		values[i++] = Int64GetDatumFast(tmp.blocks.shared_blks_read);
		values[i++] = Int64GetDatumFast(tmp.blocks.shared_blks_dirtied);
		values[i++] = Int64GetDatumFast(tmp.blocks.shared_blks_written);
		values[i++] = Int64GetDatumFast(tmp.blocks.local_blks_hit);
		values[i++] = Int64GetDatumFast(tmp.blocks.local_blks_read);
		values[i++] = Int64GetDatumFast(tmp.blocks.local_blks_dirtied);
		values[i++] = Int64GetDatumFast(tmp.blocks.local_blks_written);
		values[i++] = Int64GetDatumFast(tmp.blocks.temp_blks_read);
		values[i++] = Int64GetDatumFast(tmp.blocks.temp_blks_written);
		values[i++] = Float8GetDatumFast(tmp.blocks.blk_read_time);
		values[i++] = Float8GetDatumFast(tmp.blocks.blk_write_time);
		values[i++] = Float8GetDatumFast(tmp.blocks.temp_blk_read_time);
		values[i++] = Float8GetDatumFast(tmp.blocks.temp_blk_write_time);

		/* resp_calls at column number 41 */
		values[i++] = IntArrayGetTextDatum(tmp.resp_calls, hist_bucket_count_total);

		/* utime at column number 42 */
		values[i++] = Float8GetDatumFast(tmp.sysinfo.utime);

		/* stime at column number 43 */
		values[i++] = Float8GetDatumFast(tmp.sysinfo.stime);
		{
			char		buf[256];
			Datum		wal_bytes;

			/* wal_records at column number 44 */
			values[i++] = Int64GetDatumFast(tmp.walusage.wal_records);

			/* wal_fpi at column number 45 */
			values[i++] = Int64GetDatumFast(tmp.walusage.wal_fpi);

			snprintf(buf, sizeof buf, UINT64_FORMAT, tmp.walusage.wal_bytes);

			/* Convert to numeric */
			wal_bytes = DirectFunctionCall3(numeric_in,
											CStringGetDatum(buf),
											ObjectIdGetDatum(0),
											Int32GetDatum(-1));
			/* wal_bytes at column number 46 */
			values[i++] = wal_bytes;

			/* application_name at column number 47 */
			if (strlen(tmp.info.comments) > 0)
				values[i++] = CStringGetTextDatum(tmp.info.comments);
			else
				nulls[i++] = true;

			values[i++] = Int64GetDatumFast(tmp.jitinfo.jit_functions);
			values[i++] = Float8GetDatumFast(tmp.jitinfo.jit_generation_time);
			values[i++] = Int64GetDatumFast(tmp.jitinfo.jit_inlining_count);
			values[i++] = Float8GetDatumFast(tmp.jitinfo.jit_inlining_time);
			values[i++] = Int64GetDatumFast(tmp.jitinfo.jit_optimization_count);
			values[i++] = Float8GetDatumFast(tmp.jitinfo.jit_optimization_time);
			values[i++] = Int64GetDatumFast(tmp.jitinfo.jit_emission_count);
			values[i++] = Float8GetDatumFast(tmp.jitinfo.jit_emission_time);
		}
		values[i++] = BoolGetDatum(toplevel);
		values[i++] = BoolGetDatum(pg_atomic_read_u64(&pgsm->current_wbucket) != bucketid);

		/* clean up and return the tuplestore */
		tuplestore_putvalues(tupstore, tupdesc, values, nulls);

		if (query_txt)
			pfree(query_txt);
		if (parent_query_txt)
			pfree(parent_query_txt);
	}
	/* clean up and return the tuplestore */
	pgsm_hash_seq_term(&hstat);
	LWLockRelease(pgsm->lock);


	tuplestore_donestoring(tupstore);
}

static uint64
get_next_wbucket(pgsmSharedState * pgsm)
{
	struct timeval tv;
	uint64		current_bucket_sec;
	uint64		new_bucket_id;
	uint64		prev_bucket_id;
	struct tm;
	bool		update_bucket = false;

	gettimeofday(&tv, NULL);
	current_bucket_sec = pg_atomic_read_u64(&pgsm->prev_bucket_sec);

	/*
	 * If current bucket expired we loop attempting to update prev_bucket_sec.
	 *
	 * pg_atomic_compare_exchange_u64 may fail in two possible ways: 1.
	 * Another thread/process updated the variable before us. 2. A spurious
	 * failure / hardware event.
	 *
	 * In both failure cases we read prev_bucket_sec from memory again, if it
	 * was a spurious failure then the value of prev_bucket_sec must be the
	 * same as before, which will cause the while loop to execute again.
	 *
	 * If another thread updated prev_bucket_sec, then its current value will
	 * definitely make the while condition to fail, we can stop the loop as
	 * another thread has already updated prev_bucket_sec.
	 */
	while ((tv.tv_sec - (uint) current_bucket_sec) >= ((uint) pgsm_bucket_time))
	{
		if (pg_atomic_compare_exchange_u64(&pgsm->prev_bucket_sec, &current_bucket_sec, (uint64) tv.tv_sec))
		{
			update_bucket = true;
			break;
		}

		current_bucket_sec = pg_atomic_read_u64(&pgsm->prev_bucket_sec);
	}

	if (update_bucket)
	{

		new_bucket_id = (tv.tv_sec / pgsm_bucket_time) % pgsm_max_buckets;

		/* Update bucket id and retrieve the previous one. */
		prev_bucket_id = pg_atomic_exchange_u64(&pgsm->current_wbucket, new_bucket_id);

		LWLockAcquire(pgsm->lock, LW_EXCLUSIVE);
		hash_entry_dealloc(new_bucket_id, prev_bucket_id, NULL);

		LWLockRelease(pgsm->lock);

		/* Allign the value in prev_bucket_sec to the bucket start time */
		tv.tv_sec = (tv.tv_sec) - (tv.tv_sec % pgsm_bucket_time);

		pg_atomic_exchange_u64(&pgsm->prev_bucket_sec, (uint64) tv.tv_sec);

		pgsm->bucket_start_time[new_bucket_id] = (TimestampTz) tv.tv_sec -
			((POSTGRES_EPOCH_JDATE - UNIX_EPOCH_JDATE) * SECS_PER_DAY);
		pgsm->bucket_start_time[new_bucket_id] = pgsm->bucket_start_time[new_bucket_id] * USECS_PER_SEC;
		return new_bucket_id;
	}

	return pg_atomic_read_u64(&pgsm->current_wbucket);
}

/*
 * This function expects a NORMALIZED query as the input.
 * It iterates oer the normalized query skipping comments and
 * multiple spaces. All spaces are converted to ' ' so that we
 * the calculation is independent of the space type whether
 * newline, tab, or any other type. Trailing and leading spaces
 * are also removed before calculating the hash.
 */
uint64
get_pgsm_query_id_hash(const char *norm_query, int norm_len)
{
	char	   *query;
	char	   *q_iter;
	char	   *norm_q_iter = (char *) norm_query;
	uint64		pgsm_query_id = 0;

	if (!pgsm_enable_pgsm_query_id)
		return 0;

	query = palloc(norm_len + 1);
	q_iter = query;

	while (norm_q_iter && *norm_q_iter && norm_q_iter < (norm_query + norm_len))
	{
		/*
		 * Skip multiline comments, + 1 is safe even if we've reach end of
		 * string
		 */
		if (*norm_q_iter == '/' && *(norm_q_iter + 1) == '*')
		{
			while (*norm_q_iter && *norm_q_iter != '*' && *(norm_q_iter + 1) != '/')
				norm_q_iter++;

			/* Skip the '/' if the current character is valid. */
			if (*norm_q_iter)
				norm_q_iter++;
		}

		/*
		 * Skip single line comments, + 1 is safe even if we've reach end of
		 * string
		 */
		if (*norm_q_iter == '-' && *(norm_q_iter + 1) == '-')
		{
			while (*norm_q_iter && *norm_q_iter != '\n')
				norm_q_iter++;
		}

		/* Skip white spaces */
		if (scanner_isspace(*norm_q_iter))
		{
			while (scanner_isspace(*++norm_q_iter));

			/*
			 * Let's replace it with a simple space. -1 is safe as we are
			 * making sure we are not at the start of the string.
			 */
			if (q_iter != query && !scanner_isspace(*(q_iter - 1)))
				*q_iter++ = ' ';

			continue;
		}

		*q_iter++ = *norm_q_iter++;
	}

	/* Ensure we have a terminating zero at the end */
	*q_iter = '\0';

	/* Get rid of trailing spaces */
	while (q_iter > query && *q_iter == '\0')
	{
		q_iter--;

		/* Continue reducing the string size if space is found. */
		if (scanner_isspace(*q_iter))
			*q_iter = '\0';
	}

	/* Calcuate the hash. */
	pgsm_query_id = pgsm_hash_string(query, strlen(query));

	pfree(query);
	return pgsm_query_id;
}

#if PG_VERSION_NUM < 140000
/*
 * AppendJumble: Append a value that is substantive in a given query to
 * the current jumble.
 */
static void
AppendJumble(JumbleState *jstate, const unsigned char *item, Size size)
{
	unsigned char *jumble = jstate->jumble;
	Size		jumble_len = jstate->jumble_len;

	/*
	 * Whenever the jumble buffer is full, we hash the current contents and
	 * reset the buffer to contain just that hash value, thus relying on the
	 * hash to summarize everything so far.
	 */
	while (size > 0)
	{
		Size		part_size;

		if (jumble_len >= JUMBLE_SIZE)
		{
			uint64		start_hash;

			start_hash = pgsm_hash_string((char *) jumble, JUMBLE_SIZE);
			memcpy(jumble, &start_hash, sizeof(start_hash));
			jumble_len = sizeof(start_hash);
		}
		part_size = Min(size, JUMBLE_SIZE - jumble_len);
		memcpy(jumble + jumble_len, item, part_size);
		jumble_len += part_size;
		item += part_size;
		size -= part_size;
	}
	jstate->jumble_len = jumble_len;
}

/*
 * Wrappers around AppendJumble to encapsulate details of serialization
 * of individual local variable elements.
 */
#define APP_JUMB(item) \
	AppendJumble(jstate, (const unsigned char *) &(item), sizeof(item))
#define APP_JUMB_STRING(str) \
	AppendJumble(jstate, (const unsigned char *) (str), strlen(str) + 1)

/*
 * JumbleQuery: Selectively serialize the query tree, appending significant
 * data to the "query jumble" while ignoring nonsignificant data.
 *
 * Rule of thumb for what to include is that we should ignore anything not
 * semantically significant (such as alias names) as well as anything that can
 * be deduced from child nodes (else we'd just be double-hashing that piece
 * of information).
 */
static void
JumbleQuery(JumbleState *jstate, Query *query)
{
	Assert(IsA(query, Query));
	Assert(query->utilityStmt == NULL);

	APP_JUMB(query->commandType);
	/* resultRelation is usually predictable from commandType */
	JumbleExpr(jstate, (Node *) query->cteList);

	JumbleRangeTable(jstate, query->rtable, query->commandType);
	JumbleExpr(jstate, (Node *) query->jointree);
	JumbleExpr(jstate, (Node *) query->targetList);
	JumbleExpr(jstate, (Node *) query->onConflict);
	JumbleExpr(jstate, (Node *) query->returningList);
	JumbleExpr(jstate, (Node *) query->groupClause);
	JumbleExpr(jstate, (Node *) query->groupingSets);
	JumbleExpr(jstate, query->havingQual);
	JumbleExpr(jstate, (Node *) query->windowClause);
	JumbleExpr(jstate, (Node *) query->distinctClause);
	JumbleExpr(jstate, (Node *) query->sortClause);
	JumbleExpr(jstate, query->limitOffset);
	JumbleExpr(jstate, query->limitCount);
	/* we ignore rowMarks */
	JumbleExpr(jstate, query->setOperations);
}

/*
 * Jumble a range table
 */
static void
JumbleRangeTable(JumbleState *jstate, List *rtable, CmdType cmd_type)
{
	ListCell   *lc = NULL;

	foreach(lc, rtable)
	{
		RangeTblEntry *rte = lfirst_node(RangeTblEntry, lc);

		if (rte->rtekind != RTE_RELATION && cmd_type == CMD_INSERT)
			continue;

		APP_JUMB(rte->rtekind);
		switch (rte->rtekind)
		{
			case RTE_RELATION:
				APP_JUMB(rte->relid);
				JumbleExpr(jstate, (Node *) rte->tablesample);
				break;
			case RTE_SUBQUERY:
				JumbleQuery(jstate, rte->subquery);
				break;
			case RTE_JOIN:
				APP_JUMB(rte->jointype);
				break;
			case RTE_FUNCTION:
				JumbleExpr(jstate, (Node *) rte->functions);
				break;
			case RTE_TABLEFUNC:
				JumbleExpr(jstate, (Node *) rte->tablefunc);
				break;
			case RTE_VALUES:
				JumbleExpr(jstate, (Node *) rte->values_lists);
				break;
			case RTE_CTE:

				/*
				 * Depending on the CTE name here isn't ideal, but it's the
				 * only info we have to identify the referenced WITH item.
				 */
				APP_JUMB_STRING(rte->ctename);
				APP_JUMB(rte->ctelevelsup);
				break;
			case RTE_NAMEDTUPLESTORE:
				APP_JUMB_STRING(rte->enrname);
				break;
			default:
				elog(ERROR, "[pg_stat_monitor] JumbleRangeTable: unrecognized RTE kind: %d.", (int) rte->rtekind);
				break;
		}
	}
}

/*
 * Jumble an expression tree
 *
 * In general this function should handle all the same node types that
 * expression_tree_walker() does, and therefore it's coded to be as parallel
 * to that function as possible.  However, since we are only invoked on
 * queries immediately post-parse-analysis, we need not handle node types
 * that only appear in planning.
 *
 * Note: the reason we don't simply use expression_tree_walker() is that the
 * point of that function is to support tree walkers that don't care about
 * most tree node types, but here we care about all types.  We should complain
 * about any unrecognized node type.
 */
static void
JumbleExpr(JumbleState *jstate, Node *node)
{
	ListCell   *temp;

	if (node == NULL)
		return;

	/* Guard against stack overflow due to overly complex expressions */
	check_stack_depth();

	/*
	 * We always emit the node's NodeTag, then any additional fields that are
	 * considered significant, and then we recurse to any child nodes.
	 */
	APP_JUMB(node->type);

	switch (nodeTag(node))
	{
		case T_Var:
			{
				Var		   *var = (Var *) node;

				APP_JUMB(var->varno);
				APP_JUMB(var->varattno);
				APP_JUMB(var->varlevelsup);
			}
			break;
		case T_Const:
			{
				Const	   *c = (Const *) node;

				/* We jumble only the constant's type, not its value */
				APP_JUMB(c->consttype);
				/* Also, record its parse location for query normalization */
				RecordConstLocation(jstate, c->location);
			}
			break;
		case T_Param:
			{
				Param	   *p = (Param *) node;

				APP_JUMB(p->paramkind);
				APP_JUMB(p->paramid);
				APP_JUMB(p->paramtype);
				/* Also, track the highest external Param id */
				if (p->paramkind == PARAM_EXTERN &&
					p->paramid > jstate->highest_extern_param_id)
					jstate->highest_extern_param_id = p->paramid;
			}
			break;
		case T_Aggref:
			{
				Aggref	   *expr = (Aggref *) node;

				APP_JUMB(expr->aggfnoid);
				JumbleExpr(jstate, (Node *) expr->aggdirectargs);
				JumbleExpr(jstate, (Node *) expr->args);
				JumbleExpr(jstate, (Node *) expr->aggorder);
				JumbleExpr(jstate, (Node *) expr->aggdistinct);
				JumbleExpr(jstate, (Node *) expr->aggfilter);
			}
			break;
		case T_GroupingFunc:
			{
				GroupingFunc *grpnode = (GroupingFunc *) node;

				JumbleExpr(jstate, (Node *) grpnode->refs);
			}
			break;
		case T_WindowFunc:
			{
				WindowFunc *expr = (WindowFunc *) node;

				APP_JUMB(expr->winfnoid);
				APP_JUMB(expr->winref);
				JumbleExpr(jstate, (Node *) expr->args);
				JumbleExpr(jstate, (Node *) expr->aggfilter);
			}
			break;
#if PG_VERSION_NUM >= 120000
		case T_SubscriptingRef:
			{
				SubscriptingRef *sbsref = (SubscriptingRef *) node;

				JumbleExpr(jstate, (Node *) sbsref->refupperindexpr);
				JumbleExpr(jstate, (Node *) sbsref->reflowerindexpr);
				JumbleExpr(jstate, (Node *) sbsref->refexpr);
				JumbleExpr(jstate, (Node *) sbsref->refassgnexpr);
			}
			break;
#else
		case T_ArrayRef:
			{
				ArrayRef   *aref = (ArrayRef *) node;

				JumbleExpr(jstate, (Node *) aref->refupperindexpr);
				JumbleExpr(jstate, (Node *) aref->reflowerindexpr);
				JumbleExpr(jstate, (Node *) aref->refexpr);
				JumbleExpr(jstate, (Node *) aref->refassgnexpr);
			}
			break;
#endif
		case T_FuncExpr:
			{
				FuncExpr   *expr = (FuncExpr *) node;

				APP_JUMB(expr->funcid);
				JumbleExpr(jstate, (Node *) expr->args);
			}
			break;
		case T_NamedArgExpr:
			{
				NamedArgExpr *nae = (NamedArgExpr *) node;

				APP_JUMB(nae->argnumber);
				JumbleExpr(jstate, (Node *) nae->arg);
			}
			break;
		case T_OpExpr:
		case T_DistinctExpr:	/* struct-equivalent to OpExpr */
		case T_NullIfExpr:		/* struct-equivalent to OpExpr */
			{
				OpExpr	   *expr = (OpExpr *) node;

				APP_JUMB(expr->opno);
				JumbleExpr(jstate, (Node *) expr->args);
			}
			break;
		case T_ScalarArrayOpExpr:
			{
				ScalarArrayOpExpr *expr = (ScalarArrayOpExpr *) node;

				APP_JUMB(expr->opno);
				APP_JUMB(expr->useOr);
				JumbleExpr(jstate, (Node *) expr->args);
			}
			break;
		case T_BoolExpr:
			{
				BoolExpr   *expr = (BoolExpr *) node;

				APP_JUMB(expr->boolop);
				JumbleExpr(jstate, (Node *) expr->args);
			}
			break;
		case T_SubLink:
			{
				SubLink    *sublink = (SubLink *) node;

				APP_JUMB(sublink->subLinkType);
				APP_JUMB(sublink->subLinkId);
				JumbleExpr(jstate, (Node *) sublink->testexpr);
				JumbleQuery(jstate, castNode(Query, sublink->subselect));
			}
			break;
		case T_FieldSelect:
			{
				FieldSelect *fs = (FieldSelect *) node;

				APP_JUMB(fs->fieldnum);
				JumbleExpr(jstate, (Node *) fs->arg);
			}
			break;
		case T_FieldStore:
			{
				FieldStore *fstore = (FieldStore *) node;

				JumbleExpr(jstate, (Node *) fstore->arg);
				JumbleExpr(jstate, (Node *) fstore->newvals);
			}
			break;
		case T_RelabelType:
			{
				RelabelType *rt = (RelabelType *) node;

				APP_JUMB(rt->resulttype);
				JumbleExpr(jstate, (Node *) rt->arg);
			}
			break;
		case T_CoerceViaIO:
			{
				CoerceViaIO *cio = (CoerceViaIO *) node;

				APP_JUMB(cio->resulttype);
				JumbleExpr(jstate, (Node *) cio->arg);
			}
			break;
		case T_ArrayCoerceExpr:
			{
				ArrayCoerceExpr *acexpr = (ArrayCoerceExpr *) node;

				APP_JUMB(acexpr->resulttype);
				JumbleExpr(jstate, (Node *) acexpr->arg);
				JumbleExpr(jstate, (Node *) acexpr->elemexpr);
			}
			break;
		case T_ConvertRowtypeExpr:
			{
				ConvertRowtypeExpr *crexpr = (ConvertRowtypeExpr *) node;

				APP_JUMB(crexpr->resulttype);
				JumbleExpr(jstate, (Node *) crexpr->arg);
			}
			break;
		case T_CollateExpr:
			{
				CollateExpr *ce = (CollateExpr *) node;

				APP_JUMB(ce->collOid);
				JumbleExpr(jstate, (Node *) ce->arg);
			}
			break;
		case T_CaseExpr:
			{
				CaseExpr   *caseexpr = (CaseExpr *) node;

				JumbleExpr(jstate, (Node *) caseexpr->arg);
				foreach(temp, caseexpr->args)
				{
					CaseWhen   *when = lfirst_node(CaseWhen, temp);

					JumbleExpr(jstate, (Node *) when->expr);
					JumbleExpr(jstate, (Node *) when->result);
				}
				JumbleExpr(jstate, (Node *) caseexpr->defresult);
			}
			break;
		case T_CaseTestExpr:
			{
				CaseTestExpr *ct = (CaseTestExpr *) node;

				APP_JUMB(ct->typeId);
			}
			break;
		case T_ArrayExpr:
			JumbleExpr(jstate, (Node *) ((ArrayExpr *) node)->elements);
			break;
		case T_RowExpr:
			JumbleExpr(jstate, (Node *) ((RowExpr *) node)->args);
			break;
		case T_RowCompareExpr:
			{
				RowCompareExpr *rcexpr = (RowCompareExpr *) node;

				APP_JUMB(rcexpr->rctype);
				JumbleExpr(jstate, (Node *) rcexpr->largs);
				JumbleExpr(jstate, (Node *) rcexpr->rargs);
			}
			break;
		case T_CoalesceExpr:
			JumbleExpr(jstate, (Node *) ((CoalesceExpr *) node)->args);
			break;
		case T_MinMaxExpr:
			{
				MinMaxExpr *mmexpr = (MinMaxExpr *) node;

				APP_JUMB(mmexpr->op);
				JumbleExpr(jstate, (Node *) mmexpr->args);
			}
			break;
		case T_SQLValueFunction:
			{
				SQLValueFunction *svf = (SQLValueFunction *) node;

				APP_JUMB(svf->op);
				/* type is fully determined by op */
				APP_JUMB(svf->typmod);
			}
			break;
		case T_XmlExpr:
			{
				XmlExpr    *xexpr = (XmlExpr *) node;

				APP_JUMB(xexpr->op);
				JumbleExpr(jstate, (Node *) xexpr->named_args);
				JumbleExpr(jstate, (Node *) xexpr->args);
			}
			break;
		case T_NullTest:
			{
				NullTest   *nt = (NullTest *) node;

				APP_JUMB(nt->nulltesttype);
				JumbleExpr(jstate, (Node *) nt->arg);
			}
			break;
		case T_BooleanTest:
			{
				BooleanTest *bt = (BooleanTest *) node;

				APP_JUMB(bt->booltesttype);
				JumbleExpr(jstate, (Node *) bt->arg);
			}
			break;
		case T_CoerceToDomain:
			{
				CoerceToDomain *cd = (CoerceToDomain *) node;

				APP_JUMB(cd->resulttype);
				JumbleExpr(jstate, (Node *) cd->arg);
			}
			break;
		case T_CoerceToDomainValue:
			{
				CoerceToDomainValue *cdv = (CoerceToDomainValue *) node;

				APP_JUMB(cdv->typeId);
			}
			break;
		case T_SetToDefault:
			{
				SetToDefault *sd = (SetToDefault *) node;

				APP_JUMB(sd->typeId);
			}
			break;
		case T_CurrentOfExpr:
			{
				CurrentOfExpr *ce = (CurrentOfExpr *) node;

				APP_JUMB(ce->cvarno);
				if (ce->cursor_name)
					APP_JUMB_STRING(ce->cursor_name);
				APP_JUMB(ce->cursor_param);
			}
			break;
		case T_NextValueExpr:
			{
				NextValueExpr *nve = (NextValueExpr *) node;

				APP_JUMB(nve->seqid);
				APP_JUMB(nve->typeId);
			}
			break;
		case T_InferenceElem:
			{
				InferenceElem *ie = (InferenceElem *) node;

				APP_JUMB(ie->infercollid);
				APP_JUMB(ie->inferopclass);
				JumbleExpr(jstate, ie->expr);
			}
			break;
		case T_TargetEntry:
			{
				TargetEntry *tle = (TargetEntry *) node;

				APP_JUMB(tle->resno);
				APP_JUMB(tle->ressortgroupref);
				JumbleExpr(jstate, (Node *) tle->expr);
			}
			break;
		case T_RangeTblRef:
			{
				RangeTblRef *rtr = (RangeTblRef *) node;

				APP_JUMB(rtr->rtindex);
			}
			break;
		case T_JoinExpr:
			{
				JoinExpr   *join = (JoinExpr *) node;

				APP_JUMB(join->jointype);
				APP_JUMB(join->isNatural);
				APP_JUMB(join->rtindex);
				JumbleExpr(jstate, join->larg);
				JumbleExpr(jstate, join->rarg);
				JumbleExpr(jstate, join->quals);
			}
			break;
		case T_FromExpr:
			{
				FromExpr   *from = (FromExpr *) node;

				JumbleExpr(jstate, (Node *) from->fromlist);
				JumbleExpr(jstate, from->quals);
			}
			break;
		case T_OnConflictExpr:
			{
				OnConflictExpr *conf = (OnConflictExpr *) node;

				APP_JUMB(conf->action);
				JumbleExpr(jstate, (Node *) conf->arbiterElems);
				JumbleExpr(jstate, conf->arbiterWhere);
				JumbleExpr(jstate, (Node *) conf->onConflictSet);
				JumbleExpr(jstate, conf->onConflictWhere);
				APP_JUMB(conf->constraint);
				APP_JUMB(conf->exclRelIndex);
				JumbleExpr(jstate, (Node *) conf->exclRelTlist);
			}
			break;
		case T_List:
			foreach(temp, (List *) node)
			{
				JumbleExpr(jstate, (Node *) lfirst(temp));
			}
			break;
		case T_IntList:
			foreach(temp, (List *) node)
			{
				APP_JUMB(lfirst_int(temp));
			}
			break;
		case T_SortGroupClause:
			{
				SortGroupClause *sgc = (SortGroupClause *) node;

				APP_JUMB(sgc->tleSortGroupRef);
				APP_JUMB(sgc->eqop);
				APP_JUMB(sgc->sortop);
				APP_JUMB(sgc->nulls_first);
			}
			break;
		case T_GroupingSet:
			{
				GroupingSet *gsnode = (GroupingSet *) node;

				JumbleExpr(jstate, (Node *) gsnode->content);
			}
			break;
		case T_WindowClause:
			{
				WindowClause *wc = (WindowClause *) node;

				APP_JUMB(wc->winref);
				APP_JUMB(wc->frameOptions);
				JumbleExpr(jstate, (Node *) wc->partitionClause);
				JumbleExpr(jstate, (Node *) wc->orderClause);
				JumbleExpr(jstate, wc->startOffset);
				JumbleExpr(jstate, wc->endOffset);
			}
			break;
		case T_CommonTableExpr:
			{
				CommonTableExpr *cte = (CommonTableExpr *) node;

				/* we store the string name because RTE_CTE RTEs need it */
				APP_JUMB_STRING(cte->ctename);
				JumbleQuery(jstate, castNode(Query, cte->ctequery));
			}
			break;
		case T_SetOperationStmt:
			{
				SetOperationStmt *setop = (SetOperationStmt *) node;

				APP_JUMB(setop->op);
				APP_JUMB(setop->all);
				JumbleExpr(jstate, setop->larg);
				JumbleExpr(jstate, setop->rarg);
			}
			break;
		case T_RangeTblFunction:
			{
				RangeTblFunction *rtfunc = (RangeTblFunction *) node;

				JumbleExpr(jstate, rtfunc->funcexpr);
			}
			break;
		case T_TableFunc:
			{
				TableFunc  *tablefunc = (TableFunc *) node;

				JumbleExpr(jstate, tablefunc->docexpr);
				JumbleExpr(jstate, tablefunc->rowexpr);
				JumbleExpr(jstate, (Node *) tablefunc->colexprs);
			}
			break;
		case T_TableSampleClause:
			{
				TableSampleClause *tsc = (TableSampleClause *) node;

				APP_JUMB(tsc->tsmhandler);
				JumbleExpr(jstate, (Node *) tsc->args);
				JumbleExpr(jstate, (Node *) tsc->repeatable);
			}
			break;
		default:
			/* Only a warning, since we can stumble along anyway */
			elog(INFO, "[pg_stat_monitor] JumbleExpr: unrecognized node type: %d.",
				 (int) nodeTag(node));
			break;
	}
}

/*
 * Record location of constant within query string of query tree
 * that is currently being walked.
 */
static void
RecordConstLocation(JumbleState *jstate, int location)
{
	/* -1 indicates unknown or undefined location */
	if (location >= 0)
	{
		/* enlarge array if needed */
		if (jstate->clocations_count >= jstate->clocations_buf_size)
		{
			jstate->clocations_buf_size *= 2;
			jstate->clocations = (LocationLen *)
				repalloc(jstate->clocations,
						 jstate->clocations_buf_size *
						 sizeof(LocationLen));
		}
		jstate->clocations[jstate->clocations_count].location = location;
		/* initialize lengths to -1 to simplify fill_in_constant_lengths */
		jstate->clocations[jstate->clocations_count].length = -1;
		jstate->clocations_count++;
	}
}

static const char *
CleanQuerytext(const char *query, int *location, int *len)
{
	int			query_location = *location;
	int			query_len = *len;

	/* First apply starting offset, unless it's -1 (unknown). */
	if (query_location >= 0)
	{
		Assert(query_location <= strlen(query));
		query += query_location;
		/* Length of 0 (or -1) means "rest of string" */
		if (query_len <= 0)
			query_len = strlen(query);
		else
			Assert(query_len <= strlen(query));
	}
	else
	{
		/* If query location is unknown, distrust query_len as well */
		query_location = 0;
		query_len = strlen(query);
	}

	/*
	 * Discard leading and trailing whitespace, too.  Use scanner_isspace()
	 * not libc's isspace(), because we want to match the lexer's behavior.
	 */
	while (query_len > 0 && scanner_isspace(query[0]))
		query++, query_location++, query_len--;
	while (query_len > 0 && scanner_isspace(query[query_len - 1]))
		query_len--;

	*location = query_location;
	*len = query_len;

	return query;
}
#endif

/*
 * Generate a normalized version of the query string that will be used to
 * represent all similar queries.
 *
 * Note that the normalized representation may well vary depending on
 * just which "equivalent" query is used to create the hashtable entry.
 * We assume this is OK.
 *
 * If query_loc > 0, then "query" has been advanced by that much compared to
 * the original string start, so we need to translate the provided locations
 * to compensate.  (This lets us avoid re-scanning statements before the one
 * of interest, so it's worth doing.)
 *
 * *query_len_p contains the input string length, and is updated with
 * the result string length on exit.  The resulting string might be longer
 * or shorter depending on what happens with replacement of constants.
 *
 * Returns a palloc'd string.
 */
static char *
generate_normalized_query(JumbleState *jstate, const char *query,
						  int query_loc, int *query_len_p, int encoding)
{
	char	   *norm_query;
	int			query_len = *query_len_p;
	int			i,
				norm_query_buflen,	/* Space allowed for norm_query */
				len_to_wrt,		/* Length (in bytes) to write */
				quer_loc = 0,	/* Source query byte location */
				n_quer_loc = 0, /* Normalized query byte location */
				last_off = 0,	/* Offset from start for previous tok */
				last_tok_len = 0;	/* Length (in bytes) of that tok */

	/*
	 * Get constants' lengths (core system only gives us locations).  Note
	 * this also ensures the items are sorted by location.
	 */
	fill_in_constant_lengths(jstate, query, query_loc);

	/*
	 * Allow for $n symbols to be longer than the constants they replace.
	 * Constants must take at least one byte in text form, while a $n symbol
	 * certainly isn't more than 11 bytes, even if n reaches INT_MAX.  We
	 * could refine that limit based on the max value of n for the current
	 * query, but it hardly seems worth any extra effort to do so.
	 */
	norm_query_buflen = query_len + jstate->clocations_count * 10;

	/* Allocate result buffer */
	norm_query = palloc(norm_query_buflen + 1);

	for (i = 0; i < jstate->clocations_count; i++)
	{
		int			off,		/* Offset from start for cur tok */
					tok_len;	/* Length (in bytes) of that tok */

		off = jstate->clocations[i].location;
		/* Adjust recorded location if we're dealing with partial string */
		off -= query_loc;

		tok_len = jstate->clocations[i].length;

		if (tok_len < 0)
			continue;			/* ignore any duplicates */

		/* Copy next chunk (what precedes the next constant) */
		len_to_wrt = off - last_off;
		len_to_wrt -= last_tok_len;

		Assert(len_to_wrt >= 0);
		memcpy(norm_query + n_quer_loc, query + quer_loc, len_to_wrt);
		n_quer_loc += len_to_wrt;

		/* And insert a param symbol in place of the constant token */
		n_quer_loc += sprintf(norm_query + n_quer_loc, "$%d",
							  i + 1 + jstate->highest_extern_param_id);

		quer_loc = off + tok_len;
		last_off = off;
		last_tok_len = tok_len;
	}

	/*
	 * We've copied up until the last ignorable constant.  Copy over the
	 * remaining bytes of the original query string.
	 */
	len_to_wrt = query_len - quer_loc;

	Assert(len_to_wrt >= 0);
	memcpy(norm_query + n_quer_loc, query + quer_loc, len_to_wrt);
	n_quer_loc += len_to_wrt;

	Assert(n_quer_loc <= norm_query_buflen);
	norm_query[n_quer_loc] = '\0';

	*query_len_p = n_quer_loc;
	return norm_query;
}

/*
 * Given a valid SQL string and an array of constant-location records,
 * fill in the textual lengths of those constants.
 *
 * The constants may use any allowed constant syntax, such as float literals,
 * bit-strings, single-quoted strings and dollar-quoted strings.  This is
 * accomplished by using the public API for the core scanner.
 *
 * It is the caller's job to ensure that the string is a valid SQL statement
 * with constants at the indicated locations.  Since in practice the string
 * has already been parsed, and the locations that the caller provides will
 * have originated from within the authoritative parser, this should not be
 * a problem.
 *
 * Duplicate constant pointers are possible, and will have their lengths
 * marked as '-1', so that they are later ignored.  (Actually, we assume the
 * lengths were initialized as -1 to start with, and don't change them here.)
 *
 * If query_loc > 0, then "query" has been advanced by that much compared to
 * the original string start, so we need to translate the provided locations
 * to compensate.  (This lets us avoid re-scanning statements before the one
 * of interest, so it's worth doing.)
 *
 * N.B. There is an assumption that a '-' character at a Const location begins
 * a negative numeric constant.  This precludes there ever being another
 * reason for a constant to start with a '-'.
 */
static void
fill_in_constant_lengths(JumbleState *jstate, const char *query,
						 int query_loc)
{
	LocationLen *locs;
	core_yyscan_t yyscanner;
	core_yy_extra_type yyextra;
	core_YYSTYPE yylval;
	YYLTYPE		yylloc;
	int			last_loc = -1;
	int			i;

	/*
	 * Sort the records by location so that we can process them in order while
	 * scanning the query text.
	 */
	if (jstate->clocations_count > 1)
		qsort(jstate->clocations, jstate->clocations_count,
			  sizeof(LocationLen), comp_location);
	locs = jstate->clocations;

	/* initialize the flex scanner --- should match raw_parser() */
	yyscanner = scanner_init(query,
							 &yyextra,
#if PG_VERSION_NUM >= 120000
							 &ScanKeywords,
							 ScanKeywordTokens);
#else
							 ScanKeywords,
							 NumScanKeywords);
#endif
	/* we don't want to re-emit any escape string warnings */
	yyextra.escape_string_warning = false;

	/* Search for each constant, in sequence */
	for (i = 0; i < jstate->clocations_count; i++)
	{
		int			loc = locs[i].location;
		int			tok;

		/* Adjust recorded location if we're dealing with partial string */
		loc -= query_loc;

		Assert(loc >= 0);

		if (loc <= last_loc)
			continue;			/* Duplicate constant, ignore */

		/* Lex tokens until we find the desired constant */
		for (;;)
		{
			tok = core_yylex(&yylval, &yylloc, yyscanner);

			/* We should not hit end-of-string, but if we do, behave sanely */
			if (tok == 0)
				break;			/* out of inner for-loop */

			/*
			 * We should find the token position exactly, but if we somehow
			 * run past it, work with that.
			 */
			if (yylloc >= loc)
			{
				if (query[loc] == '-')
				{
					/*
					 * It's a negative value - this is the one and only case
					 * where we replace more than a single token.
					 *
					 * Do not compensate for the core system's special-case
					 * adjustment of location to that of the leading '-'
					 * operator in the event of a negative constant.  It is
					 * also useful for our purposes to start from the minus
					 * symbol.  In this way, queries like "select * from foo
					 * where bar = 1" and "select * from foo where bar = -2"
					 * will have identical normalized query strings.
					 */
					tok = core_yylex(&yylval, &yylloc, yyscanner);
					if (tok == 0)
						break;	/* out of inner for-loop */
				}

				/*
				 * We now rely on the assumption that flex has placed a zero
				 * byte after the text of the current token in scanbuf.
				 */
				locs[i].length = strlen(yyextra.scanbuf + loc);
				break;			/* out of inner for-loop */
			}
		}

		/* If we hit end-of-string, give up, leaving remaining lengths -1 */
		if (tok == 0)
			break;

		last_loc = loc;
	}

	scanner_finish(yyscanner);
}

/*
 * comp_location: comparator for qsorting LocationLen structs by location
 */
static int
comp_location(const void *a, const void *b)
{
	int			l = ((const LocationLen *) a)->location;
	int			r = ((const LocationLen *) b)->location;

	if (l < r)
		return -1;
	else if (l > r)
		return +1;
	else
		return 0;
}

#define MAX_STRING_LEN	1024
/* Convert array into Text dataum */
static Datum
intarray_get_datum(int32 arr[], int len)
{
	int			j;
	char		str[1024];
	char		tmp[10];

	str[0] = '\0';

	/* Need to calculate the actual size, and avoid unnessary memory usage */
	for (j = 0; j < len; j++)
	{
		if (!str[0])
		{
			snprintf(tmp, 10, "%d", arr[j]);
			strcat(str, tmp);
			continue;
		}
		snprintf(tmp, 10, ",%d", arr[j]);
		strcat(str, tmp);
	}
	return CStringGetTextDatum(str);

}


Datum
pg_stat_monitor_hook_stats(PG_FUNCTION_ARGS)
{
	return (Datum) 0;
}


void
pgsm_emit_log_hook(ErrorData *edata)
{
	if (!IsSystemInitialized() || edata == NULL)
		goto exit;

	if (IsParallelWorker())
		goto exit;

	/* Check if PostgreSQL has finished its own bootstraping code. */
	if (MyProc == NULL)
		goto exit;

	/* Do not store */
	if (PGSM_ERROR_CAPTURE_ENABLED && edata->elevel >= WARNING && IsSystemOOM() == false)
	{
		pgsm_store_error(debug_query_string ? debug_query_string : "",
						 edata);
	}
exit:
	if (prev_emit_log_hook)
		prev_emit_log_hook(edata);
}

bool
IsSystemInitialized(void)
{
	return (system_init && IsHashInitialize());
}

static double
time_diff(struct timeval end, struct timeval start)
{
	double		mstart;
	double		mend;

	mend = ((double) end.tv_sec * 1000.0 + (double) end.tv_usec / 1000.0);
	mstart = ((double) start.tv_sec * 1000.0 + (double) start.tv_usec / 1000.0);
	return mend - mstart;
}

char *
unpack_sql_state(int sql_state)
{
	static char buf[12];
	int			i;

	for (i = 0; i < 5; i++)
	{
		buf[i] = PGUNSIXBIT(sql_state);
		sql_state >>= 6;
	}

	buf[i] = '\0';
	return buf;
}

/* Validate histogram values and find the max number of histogram buckets that can be created */
static void
set_histogram_bucket_timings(void)
{
	double		b2_start;
	double		b2_end;
	int			b_count;

	hist_bucket_min = pgsm_histogram_min;
	hist_bucket_max = pgsm_histogram_max;
	hist_bucket_count_user = pgsm_histogram_buckets;
	b_count = hist_bucket_count_user;

	if (pgsm_histogram_buckets >= 2)
	{
		for (; hist_bucket_count_user > 0; hist_bucket_count_user--)
		{
			histogram_bucket_timings(2, &b2_start, &b2_end);

			/*
			 * The first bucket size will always be one or greater as we're
			 * doing min value + e^0; and e^0 = 1. Checking if histograms
			 * buckets overlap. That can only happen if the second bucket size
			 * is zero as we using exponential bucket sizes. Therefore, if the
			 * second bucket size is greater than 1, we'll never have
			 * overlapping buckets.
			 */
			if (b2_start != b2_end)
			{
				break;
			}
		}

		if (b_count != hist_bucket_count_user)
			ereport(WARNING,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("pg_stat_monitor: Histogram buckets are overlapping."),
					 errdetail("Histogram bucket size is set to %d [not including outlier buckets].", hist_bucket_count_user)));
	}

	/*
	 * Important that we keep user bucket count separate for calculations, but
	 * must add 1 for max outlier queries. However, for min, bucket should
	 * only be added if the minimum value provided by user is greater than 0
	 */
	hist_bucket_count_total = (hist_bucket_count_user + (int) (hist_bucket_max < HISTOGRAM_MAX_TIME) + (int) (hist_bucket_min > 0));

	for (b_count = 0; b_count < hist_bucket_count_total; b_count++)
	{
		histogram_bucket_timings(b_count, &hist_bucket_timings[b_count][HISTOGRAM_START], &hist_bucket_timings[b_count][HISTOGRAM_END]);
	}
}

/*
 * Given an index, return the histogram start and end times.
 */
static void
histogram_bucket_timings(int index, double *b_start, double *b_end)
{
	double		q_min = hist_bucket_min;
	double		q_max = hist_bucket_max;
	int			b_count = hist_bucket_count_total;
	int			b_count_user = hist_bucket_count_user;
	double		bucket_size;

	/*
	 * We must not skip any queries that fall outside the user defined
	 * histogram buckets. So capturing min/max outliers.
	 */
	if (index == 0 && q_min > 0)
	{
		*b_start = 0;
		*b_end = q_min;
		return;
	}
	else if (index == (b_count - 1) && q_max < HISTOGRAM_MAX_TIME)
	{
		*b_start = q_max;
		*b_end = -1;
		return;
	}

	/*
	 * Equisized logrithmic values will yield exponential values as required.
	 * For calculating logrithmic value, we MUST use the number of bucket
	 * provided by the user.
	 */
	bucket_size = log(q_max - q_min) / (double) b_count_user;

	/*
	 * Can't do exp(0) as that returns 1. So handling the case of first entry
	 * specifically
	 */
	*b_start = q_min + ((index == 0 || (index == 1 && q_min > 0)) ? 0 : exp(bucket_size * (index - 1 + (q_min == 0))));
	*b_end = q_min + exp(bucket_size * (index + (q_min == 0)));
}

/*
 * Get the histogram bucket index for a given query time.
 */
static int
get_histogram_bucket(double q_time)
{
	int			index = 0;
	double		exec_time = q_time;

	for (index = 0; index < hist_bucket_count_total; index++)
	{
		if (exec_time >= hist_bucket_timings[index][HISTOGRAM_START] && exec_time <= hist_bucket_timings[index][HISTOGRAM_END])
			return index;
	}

	/*
	 * So haven't found a histogram bucket for this query. That's only
	 * possible for the last bucket as its end time is less than 0.
	 */
	return (hist_bucket_count_total - 1);
}

/*
 * Get the timings of the histogram as a single string. The last bucket
 * has ellipses as the end value indication infinity.
 */
Datum
get_histogram_timings(PG_FUNCTION_ARGS)
{
	double		b_start;
	double		b_end;
	int			b_count = hist_bucket_count_total;
	int			index = 0;
	char	   *tmp_str = palloc0(MAX_STRING_LEN);
	char	   *text_str = palloc0(MAX_STRING_LEN);

	for (index = 0; index < b_count; index++)
	{
		histogram_bucket_timings(index, &b_start, &b_end);

		if (index == 0)
		{
			snprintf(text_str, MAX_STRING_LEN, "{{%.3lf - %.3lf}", b_start, b_end);
		}
		else if (index == (b_count - 1))
		{
			snprintf(tmp_str, MAX_STRING_LEN, "%s, (%.3lf - ...}}", text_str, b_start);
			snprintf(text_str, MAX_STRING_LEN, "%s", tmp_str);
		}
		else
		{
			snprintf(tmp_str, MAX_STRING_LEN, "%s, (%.3lf - %.3lf}", text_str, b_start, b_end);
			snprintf(text_str, MAX_STRING_LEN, "%s", tmp_str);
		}
	}

	pfree(tmp_str);

	return CStringGetTextDatum(text_str);
}

static void
extract_query_comments(const char *query, char *comments, size_t max_len)
{
	int			rc;
	size_t		nmatch = 1;
	regmatch_t	pmatch;
	regoff_t	comment_len,
				total_len = 0;
	const char *s = query;

	while (total_len < max_len)
	{
		rc = regexec(&preg_query_comments, s, nmatch, &pmatch, 0);
		if (rc != 0)
			break;

		comment_len = pmatch.rm_eo - pmatch.rm_so;

		if (total_len + comment_len > max_len)
			break;				/* TODO: log error in error view, insufficient
								 * space for comment. */

		total_len += comment_len;

		/* Not 1st iteration, append ", " before next comment. */
		if (s != query)
		{
			if (total_len + 2 > max_len)
				break;			/* TODO: log error in error view, insufficient
								 * space for ", " + comment. */

			memcpy(comments, ", ", 2);
			comments += 2;
			total_len += 2;
		}

		memcpy(comments, s + pmatch.rm_so, comment_len);
		comments += comment_len;
		s += pmatch.rm_eo;
	}
}

#if PG_VERSION_NUM < 140000
static uint64
get_query_id(JumbleState *jstate, Query *query)
{
	uint64		queryid;

	/* Set up workspace for query jumbling */
	jstate->jumble = (unsigned char *) palloc(JUMBLE_SIZE);
	jstate->jumble_len = 0;
	jstate->clocations_buf_size = 32;
	jstate->clocations = (LocationLen *) palloc(jstate->clocations_buf_size * sizeof(LocationLen));
	jstate->clocations_count = 0;
	jstate->highest_extern_param_id = 0;

	/* Compute query ID and mark the Query node with it */
	JumbleQuery(jstate, query);
	queryid = pgsm_hash_string((const char *) jstate->jumble, jstate->jumble_len);
	return queryid;
}
#endif
