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
#include "commands/explain.h"
#include "pg_stat_monitor.h"

/* YB includes. */
#include "access/transam.h" /* For FirstNormalObjectId */
#include "common/pg_yb_common.h"
#include "pg_yb_utils.h"
#include "yb/yql/pggate/webserver/pgsql_webserver_wrapper.h"

PG_MODULE_MAGIC;

#define BUILD_VERSION                   "devel"
#define PG_STAT_STATEMENTS_COLS         51  /* maximum of above */
#define PGSM_TEXT_FILE                  "/tmp/pg_stat_monitor_query"

#define PGUNSIXBIT(val) (((val) & 0x3F) + '0')

#define _snprintf(_str_dst, _str_src, _len, _max_len)\
do                                                   \
{                                                    \
	int i;                                           \
	for(i = 0; i < _len && i < _max_len; i++)        \
	{\
		_str_dst[i] = _str_src[i];                   \
	}\
}while(0)

#define _snprintf2(_str_dst, _str_src, _len1, _len2)\
do                                                      \
{                                                       \
	int i,j;                                            \
	for(i = 0; i < _len1; i++)                        \
		for(j = 0; j < _len2; j++)        \
		{                                               \
			_str_dst[i][j] = _str_src[i][j];                  \
		}                                               \
}while(0)

/*---- Initicalization Function Declarations ----*/
void _PG_init(void);
void _PG_fini(void);

/*---- Initicalization Function Declarations ----*/
void _PG_init(void);
void _PG_fini(void);

/*---- Local variables ----*/

/* Current nesting depth of ExecutorRun+ProcessUtility calls */
static int	nested_level = 0;
#if PG_VERSION_NUM >= 130000
static int plan_nested_level = 0;
#endif

/* The array to store outer layer query id*/
uint64 *nested_queryids;

FILE *qfile;
static bool system_init = false;
static struct rusage  rusage_start;
static struct rusage  rusage_end;
static unsigned char *pgss_qbuf[MAX_BUCKETS];
static char *pgss_explain(QueryDesc *queryDesc);

static int  get_histogram_bucket(double q_time);
static bool IsSystemInitialized(void);
static void dump_queries_buffer(int bucket_id, unsigned char *buf, int buf_len);
static double time_diff(struct timeval end, struct timeval start);


/* Saved hook values in case of unload */

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
void pgsm_emit_log_hook(ErrorData *edata);
static shmem_startup_hook_type prev_shmem_startup_hook = NULL;
static ExecutorCheckPerms_hook_type prev_ExecutorCheckPerms_hook = NULL;

PG_FUNCTION_INFO_V1(pg_stat_monitor_version);
PG_FUNCTION_INFO_V1(pg_stat_monitor_reset);
PG_FUNCTION_INFO_V1(pg_stat_monitor_1_2);
PG_FUNCTION_INFO_V1(pg_stat_monitor_1_3);
PG_FUNCTION_INFO_V1(pg_stat_monitor);
PG_FUNCTION_INFO_V1(pg_stat_monitor_settings);
PG_FUNCTION_INFO_V1(get_histogram_timings);

static uint pg_get_client_addr(void);
static int pg_get_application_name(char* application_name);
static PgBackendStatus *pg_get_backend_status(void);
static Datum intarray_get_datum(int32 arr[], int len);

static void pgss_post_parse_analyze(ParseState *pstate, Query *query);
static void pgss_ExecutorStart(QueryDesc *queryDesc, int eflags);
static void pgss_ExecutorRun(QueryDesc *queryDesc, ScanDirection direction, uint64 count, bool execute_once);
static void pgss_ExecutorFinish(QueryDesc *queryDesc);
static void pgss_ExecutorEnd(QueryDesc *queryDesc);
static bool pgss_ExecutorCheckPerms(List *rt, bool abort);

#if PG_VERSION_NUM >= 130000
static PlannedStmt * pgss_planner_hook(Query *parse, const char *query_string, int cursorOptions, ParamListInfo boundParams);
static void pgss_ProcessUtility(PlannedStmt *pstmt, const char *queryString,
					ProcessUtilityContext context,
					ParamListInfo params, QueryEnvironment *queryEnv,
					DestReceiver *dest,
					QueryCompletion *qc
					);
#else
static void BufferUsageAccumDiff(BufferUsage* bufusage, BufferUsage* pgBufferUsage, BufferUsage* bufusage_start);
static void pgss_ProcessUtility(PlannedStmt *pstmt, const char *queryString,
                                ProcessUtilityContext context, ParamListInfo params,
                                QueryEnvironment *queryEnv,
                                DestReceiver *dest,
                                char *completionTag);
#endif

static uint64 pgss_hash_string(const char *str, int len);
char *unpack_sql_state(int sql_state);

static void pgss_store_error(uint64 queryid, const char * query, ErrorData *edata);
static pgssQueryEntry *pgss_store_query_info(uint64 bucketid,
					  uint64 queryid,
					  uint64 dbid,
					  uint64 userid,
					  uint64 ip,
					  const char *query,
					  uint64 query_len,
					  pgssStoreKind kind);

static void pgss_store_utility(const char *query,
					double total_time,
					uint64 rows,
					BufferUsage *bufusage,
					WalUsage *walusage);

static void pgss_store(uint64 queryid,
						const char *query,
						PlanInfo *plan_info,
						CmdType cmd_type,
						SysInfo *sys_info,
						ErrorInfo *error_info,
						double total_time,
						uint64 rows,
						BufferUsage *bufusage,
						WalUsage *walusage,
						pgssJumbleState *jstate,
						pgssStoreKind kind);

static void pg_stat_monitor_internal(FunctionCallInfo fcinfo,
							bool showtext);

static void AppendJumble(pgssJumbleState *jstate,
			 const unsigned char *item, Size size);
static void JumbleQuery(pgssJumbleState *jstate, Query *query);
static void JumbleRangeTable(pgssJumbleState *jstate, List *rtable);
static void JumbleExpr(pgssJumbleState *jstate, Node *node);
static void RecordConstLocation(pgssJumbleState *jstate, int location);
static char *generate_normalized_query(pgssJumbleState *jstate, const char *query,
						  int query_loc, int *query_len_p, int encoding);
static void fill_in_constant_lengths(pgssJumbleState *jstate, const char *query, int query_loc);
static int comp_location(const void *a, const void *b);

static uint64 get_next_wbucket(pgssSharedState *pgss);

static void
pgss_store_query(uint64 queryid,
                  const char * query,
				  CmdType cmd_type,
				  int query_location,
				  int query_len,
				  pgssJumbleState *jstate,
				  pgssStoreKind kind);
static uint64 read_query(unsigned char *buf, uint64 bucketid, uint64 queryid, char * query);
int read_query_buffer(int bucket_id, uint64 queryid, char *query_txt);

static uint64 get_query_id(pgssJumbleState *jstate, Query *query);

/*
 * Module load callback
 */
// cppcheck-suppress unusedFunction
void
_PG_init(void)
{
	int i;
	const char *yb_tmp_dir = YbGetTmpDir();

	elog(DEBUG2, "pg_stat_monitor: %s()", __FUNCTION__);
	/*
	 * In order to create our shared memory area, we have to be loaded via
	 * shared_preload_libraries.  If not, fall out without hooking into any of
	 * the main system.  (We don't throw error here because it seems useful to
	 * allow the pg_stat_statements functions to be created even when the
	 * module isn't active.  The functions must protect themselves against
	 * being called then, however.)
	 */
	if (!process_shared_preload_libraries_in_progress)
		return;

	/* Inilize the GUC variables */
	init_guc();

	for (i = 0; i < PGSM_MAX_BUCKETS; i++)
	{
		char file_name[1024];
		if (yb_tmp_dir)
			snprintf(file_name, 1024, "%s/%s.%d", yb_tmp_dir,
					 YB_PGSM_TEXT_FILE_PREFIX, i);
		else
			snprintf(file_name, 1024, "%s.%d", PGSM_TEXT_FILE, i);
		unlink(file_name);
	}

	EmitWarningsOnPlaceholders("pg_stat_monitor");

	/*
	 * Request additional shared resources.  (These are no-ops if we're not in
	 * the postmaster process.)  We'll allocate or attach to the shared
	 * resources in pgss_shmem_startup().
	 */
	RequestAddinShmemSpace(hash_memsize());
	RequestNamedLWLockTranche("pg_stat_monitor", 1);

	/*
	 * Install hooks.
	 */
	prev_shmem_startup_hook 		= shmem_startup_hook;
	shmem_startup_hook 				= pgss_shmem_startup;
	prev_post_parse_analyze_hook 	= post_parse_analyze_hook;
	post_parse_analyze_hook 		= pgss_post_parse_analyze;
	prev_ExecutorStart 				= ExecutorStart_hook;
	ExecutorStart_hook 				= pgss_ExecutorStart;
	prev_ExecutorRun 				= ExecutorRun_hook;
	ExecutorRun_hook 				= pgss_ExecutorRun;
	prev_ExecutorFinish 			= ExecutorFinish_hook;
	ExecutorFinish_hook 			= pgss_ExecutorFinish;
	prev_ExecutorEnd 				= ExecutorEnd_hook;
	ExecutorEnd_hook 				= pgss_ExecutorEnd;
	prev_ProcessUtility 			= ProcessUtility_hook;
	ProcessUtility_hook 			= pgss_ProcessUtility;
#if PG_VERSION_NUM >= 130000
	planner_hook_next       		= planner_hook;
	planner_hook                    = pgss_planner_hook;
#endif
	emit_log_hook                 = pgsm_emit_log_hook;
	prev_ExecutorCheckPerms_hook 	= ExecutorCheckPerms_hook;
	ExecutorCheckPerms_hook		= pgss_ExecutorCheckPerms;

	nested_queryids = (uint64*) malloc(sizeof(uint64) * max_stack_depth);

	system_init = true;
}

/*
 * Module unload callback
 */
// cppcheck-suppress unusedFunction
void
_PG_fini(void)
{
	system_init = false;
	shmem_startup_hook 		= prev_shmem_startup_hook;
	post_parse_analyze_hook = prev_post_parse_analyze_hook;
	ExecutorStart_hook 		= prev_ExecutorStart;
	ExecutorRun_hook 		= prev_ExecutorRun;
	ExecutorFinish_hook 	= prev_ExecutorFinish;
	ExecutorEnd_hook 		= prev_ExecutorEnd;
	ProcessUtility_hook 	= prev_ProcessUtility;

	free(nested_queryids);

	hash_entry_reset();
}

/*
 * shmem_startup hook: allocate or attach to shared memory,
 * then load any pre-existing statistics from file.
 * Also create and load the query-texts file, which is expected to exist
 * (even if empty) while the module is enabled.
 */
void
pgss_shmem_startup(void)
{
	if (prev_shmem_startup_hook)
		prev_shmem_startup_hook();

	pgss_startup();
}

/*
 * Select the version of pg_stat_monitor.
 */
Datum
pg_stat_monitor_version(PG_FUNCTION_ARGS)
{
	PG_RETURN_TEXT_P(cstring_to_text(BUILD_VERSION));
}

/*
 * Post-parse-analysis hook: mark query with a queryId
 */
static void
pgss_post_parse_analyze(ParseState *pstate, Query *query)
{
	pgssJumbleState jstate;
	pgssStoreKind   kind = PGSS_PARSE;

	if (prev_post_parse_analyze_hook)
		prev_post_parse_analyze_hook(pstate, query);

	/* Safety check... */
	if (!IsSystemInitialized())
		return;

	/*
	 * Utility statements get queryId zero.  We do this even in cases where
	 * the statement contains an optimizable statement for which a queryId
	 * could be derived (such as EXPLAIN or DECLARE CURSOR).  For such cases,
	 * runtime control will first go through ProcessUtility and then the
	 * executor, and we don't want the executor hooks to do anything, since we
	 * are already measuring the statement's costs at the utility level.
	 */
	if (query->utilityStmt)
	{
		query->queryId = UINT64CONST(0);
		return;
	}

	query->queryId = get_query_id(&jstate, query);

	/*
	 * If we are unlucky enough to get a hash of zero, use 1 instead, to
	 * prevent confusion with the utility-statement case.
	 */
	if (query->queryId == UINT64CONST(0))
		query->queryId = UINT64CONST(1);

	if (jstate.clocations_count <= 0)
		return;

	pgss_store_query(query->queryId,          /* queryid */
               pstate->p_sourcetext,          /* query */
               query->commandType,            /* CmdType */
			   query->stmt_location,		  /* Query Location */
               query->stmt_len,			  	  /* Query Len */
               &jstate,                       /* pgssJumbleState */
			   kind);						  /*pgssStoreKind */
}

/*
 * ExecutorStart hook: start up tracking if needed
 */
static void
pgss_ExecutorStart(QueryDesc *queryDesc, int eflags)
{
	uint64             queryId       = queryDesc->plannedstmt->queryId;

	if(getrusage(RUSAGE_SELF, &rusage_start) != 0)
		elog(DEBUG1, "pg_stat_monitor: failed to execute getrusage");

	if (prev_ExecutorStart)
		prev_ExecutorStart(queryDesc, eflags);
	else
		standard_ExecutorStart(queryDesc, eflags);

	/*
	 * If query has queryId zero, don't track it.  This prevents double
	 * counting of optimizable statements that are directly contained in
	 * utility statements.
	 */
	if (PGSM_ENABLED && queryDesc->plannedstmt->queryId != UINT64CONST(0))
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
			queryDesc->totaltime = InstrAlloc(1, INSTRUMENT_ALL, false);
			MemoryContextSwitchTo(oldcxt);
		}
		pgss_store(queryId,                                 /* query id */
					queryDesc->sourceText,					/* query text */
					NULL,                                   /* PlanInfo */
					queryDesc->operation,                   /* CmdType */
					NULL,                                   /* SysInfo */
					NULL,									/* ErrorInfo */
					0,                                      /* totaltime */
					0,                                      /* rows */
					NULL,                                   /*  bufusage */
#if PG_VERSION_NUM >= 130000
					NULL,                                   /* walusage */
#else
					NULL,
#endif
					NULL,
					PGSS_EXEC);							   /* pgssStoreKind */
	}
}

/*
 * ExecutorRun hook: all we need do is track nesting depth
 */
static void
pgss_ExecutorRun(QueryDesc *queryDesc, ScanDirection direction, uint64 count,
				 bool execute_once)
{
	if (nested_level >=0 && nested_level < max_stack_depth)
		nested_queryids[nested_level] = queryDesc->plannedstmt->queryId;
	nested_level++;
	PG_TRY();
	{
		if (prev_ExecutorRun)
			prev_ExecutorRun(queryDesc, direction, count, execute_once);
		else
			standard_ExecutorRun(queryDesc, direction, count, execute_once);
		nested_level--;
		if (nested_level >=0 && nested_level < max_stack_depth)
			nested_queryids[nested_level] = UINT64CONST(0);
	}
	PG_CATCH();
	{
		nested_level--;
		if (nested_level >=0 && nested_level < max_stack_depth)
			nested_queryids[nested_level] = UINT64CONST(0);
		PG_RE_THROW();
	}
	PG_END_TRY();
}

/*
 * ExecutorFinish hook: all we need do is track nesting depth
 */
static void
pgss_ExecutorFinish(QueryDesc *queryDesc)
{
	nested_level++;
	PG_TRY();
	{
		if (prev_ExecutorFinish)
			prev_ExecutorFinish(queryDesc);
		else
			standard_ExecutorFinish(queryDesc);
		nested_level--;
	}
	PG_CATCH();
	{
		nested_level--;
		PG_RE_THROW();
	}
	PG_END_TRY();
}

static char *
pgss_explain(QueryDesc *queryDesc)
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
pgss_ExecutorEnd(QueryDesc *queryDesc)
{
	uint64             queryId       = queryDesc->plannedstmt->queryId;
	pgssSharedState    *pgss         = pgsm_get_ss();
    SysInfo            sys_info;
	PlanInfo           plan_info;

	/* Extract the plan information in case of SELECT statment */
	memset(&plan_info, 0, sizeof(PlanInfo));
	if (queryDesc->operation == CMD_SELECT && PGSM_QUERY_PLAN)
	{
		MemoryContext mct = MemoryContextSwitchTo(TopMemoryContext);
		snprintf(plan_info.plan_text, PLAN_TEXT_LEN, "%s", pgss_explain(queryDesc));
		plan_info.planid = DatumGetUInt64(hash_any_extended((const unsigned char*)plan_info.plan_text, strlen(plan_info.plan_text), 0));
		MemoryContextSwitchTo(mct);
	}

	if (queryId != UINT64CONST(0) && queryDesc->totaltime)
	{
		/*
		 * Make sure stats accumulation is done.  (Note: it's okay if several
		 * levels of hook all do this.)
		 */
		InstrEndLoop(queryDesc->totaltime);
		if(getrusage(RUSAGE_SELF, &rusage_end) != 0)
			elog(DEBUG1, "pg_stat_monitor: failed to execute getrusage");

		sys_info.utime = time_diff(rusage_end.ru_utime, rusage_start.ru_utime);
		sys_info.stime = time_diff(rusage_end.ru_stime, rusage_start.ru_stime);

		pgss_store(queryId,                                 /* query id */
					queryDesc->sourceText,					/* query text */
					&plan_info,								/* PlanInfo */
					queryDesc->operation,					/* CmdType */
					&sys_info,								/* SysInfo */
					NULL,									/* ErrorInfo */
					queryDesc->totaltime->total * 1000.0,	/* totaltime */
					queryDesc->estate->es_processed,		/* rows */
					&queryDesc->totaltime->bufusage,		/*  bufusage */
#if PG_VERSION_NUM >= 130000
					&queryDesc->totaltime->walusage,		/* walusage */
#else
					NULL,
#endif
					NULL,
					PGSS_FINISHED); 							/* pgssStoreKind */
	}
	if (prev_ExecutorEnd)
		prev_ExecutorEnd(queryDesc);
	else
		standard_ExecutorEnd(queryDesc);
	pgss->num_relations = 0;
}

static bool
pgss_ExecutorCheckPerms(List *rt, bool abort)
{
	ListCell		*lr = NULL;
	pgssSharedState *pgss = pgsm_get_ss();
	int				i = 0;
	int				j = 0;
	Oid				list_oid[20];

	LWLockAcquire(pgss->lock, LW_EXCLUSIVE);
	pgss->num_relations = 0;

	foreach(lr, rt)
    {
        RangeTblEntry *rte = lfirst(lr);
        if (rte->rtekind != RTE_RELATION)
            continue;

		if (i < REL_LST)
		{
			bool found = false;
			for(j = 0; j < i; j++)
			{
				if (list_oid[j] == rte->relid)
					found = true;
			}

			if (!found)
			{
				char *namespace_name;
				char *relation_name;
				list_oid[j] = rte->relid;
				namespace_name = get_namespace_name(get_rel_namespace(rte->relid));
				relation_name = get_rel_name(rte->relid);
				if (rte->relkind == 'v')
					snprintf(pgss->relations[i++], REL_LEN, "%s.%s*", namespace_name, relation_name);
				else
					snprintf(pgss->relations[i++], REL_LEN, "%s.%s", namespace_name, relation_name);
			}
		}
	}
	pgss->num_relations = i;
	LWLockRelease(pgss->lock);

    if (prev_ExecutorCheckPerms_hook)
        return prev_ExecutorCheckPerms_hook(rt, abort);

    return true;
}

#if PG_VERSION_NUM >= 130000
static PlannedStmt*
pgss_planner_hook(Query *parse, const char *query_string, int cursorOptions, ParamListInfo boundParams)
{
	PlannedStmt			*result;

	if (PGSM_TRACK_PLANNING && query_string && parse->queryId != UINT64CONST(0))
	{
		PlanInfo	plan_info;
		instr_time	start;
		instr_time	duration;
		BufferUsage bufusage_start;
		BufferUsage bufusage;
		WalUsage	walusage_start;
		WalUsage    walusage;

		memset(&plan_info, 0, sizeof(PlanInfo));
		/* We need to track buffer usage as the planner can access them. */
		bufusage_start = pgBufferUsage;

		/*
		 * Similarly the planner could write some WAL records in some cases
		 * (e.g. setting a hint bit with those being WAL-logged)
		 */
		walusage_start = pgWalUsage;
		INSTR_TIME_SET_CURRENT(start);

		plan_nested_level++;
		PG_TRY();
		{
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
		pgss_store(parse->queryId,          		/* query id */
				query_string,						/* query */
				&plan_info,							/* PlanInfo */
				parse->commandType,					/* CmdType */
				NULL,								/* SysInfo */
				NULL,								/* ErrorInfo */
				INSTR_TIME_GET_MILLISEC(duration),	/* totaltime */
				0,									/* rows */
				&bufusage,							/*  bufusage */
				&walusage,							/* walusage */
				NULL,								/* pgssJumbleState */
				PGSS_PLAN); 							/* pgssStoreKind */
	}
	else
	{
		if (planner_hook_next)
			result = planner_hook_next(parse, query_string, cursorOptions, boundParams);
		else
			result = standard_planner(parse, query_string, cursorOptions, boundParams);
	}
	return result;
}
#endif

/*
 * ProcessUtility hook
 */
#if PG_VERSION_NUM >= 130000
static void pgss_ProcessUtility(PlannedStmt *pstmt, const char *queryString,
								ProcessUtilityContext context,
								ParamListInfo params, QueryEnvironment *queryEnv,
								DestReceiver *dest,
								QueryCompletion *qc)
#else
static void pgss_ProcessUtility(PlannedStmt *pstmt, const char *queryString,
                                ProcessUtilityContext context, ParamListInfo params,
                                QueryEnvironment *queryEnv,
                                DestReceiver *dest,
                                char *completionTag)
#endif
{
	Node	   *parsetree = pstmt->utilityStmt;

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
	if (PGSM_TRACK_UTILITY &&
		!IsA(parsetree, ExecuteStmt) &&
		!IsA(parsetree, PrepareStmt) &&
		!IsA(parsetree, DeallocateStmt))
	{
		instr_time	start;
		instr_time	duration;
		uint64		rows;
		BufferUsage bufusage;
		WalUsage    walusage;
		BufferUsage bufusage_start = pgBufferUsage;
#if PG_VERSION_NUM >= 130000
		WalUsage    walusage_start = pgWalUsage;
#endif
		INSTR_TIME_SET_CURRENT(start);
		PG_TRY();
		{
			if (prev_ProcessUtility)
				prev_ProcessUtility(pstmt, queryString,
									context, params, queryEnv,
									dest
#if PG_VERSION_NUM >= 130000
									,qc
#else
									,completionTag
#endif
									);
			else
				standard_ProcessUtility(pstmt, queryString,
										context, params, queryEnv,
										dest
#if PG_VERSION_NUM >= 130000
									,qc
#else
									,completionTag
#endif
									);
		}
		PG_CATCH();
        {
			nested_level--;
			PG_RE_THROW();

		}

		PG_END_TRY();
		INSTR_TIME_SET_CURRENT(duration);
		INSTR_TIME_SUBTRACT(duration, start);

#if PG_VERSION_NUM >= 130000
		rows = (qc && qc->commandTag == CMDTAG_COPY) ? qc->nprocessed : 0;
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
		pgss_store_utility(queryString,								/* query text */
							INSTR_TIME_GET_MILLISEC(duration),		/* totaltime */
							rows,									/* rows */
							&bufusage,								/* bufusage */
							&walusage);								/* walusage */
	}
	else
	{
			if (prev_ProcessUtility)
				prev_ProcessUtility(pstmt, queryString,
									context, params, queryEnv,
									dest
#if PG_VERSION_NUM >= 130000
									,qc
#else
									,completionTag
#endif
									);
			else
				standard_ProcessUtility(pstmt, queryString,
										context, params, queryEnv,
										dest
#if PG_VERSION_NUM >= 130000
									,qc
#else
									,completionTag
#endif
									);
	}
}

#if PG_VERSION_NUM < 130000
static void
BufferUsageAccumDiff(BufferUsage* bufusage, BufferUsage* pgBufferUsage, BufferUsage* bufusage_start)
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
pgss_hash_string(const char *str, int len)
{
	return DatumGetUInt64(hash_any_extended((const unsigned char *) str,
											len, 0));
}

static PgBackendStatus*
pg_get_backend_status(void)
{
	LocalPgBackendStatus *local_beentry;
	int		             num_backends = pgstat_fetch_stat_numbackends();
	int                  i;

	for (i = 1; i <= num_backends; i++)
	{
		PgBackendStatus *beentry;

		local_beentry = pgstat_fetch_stat_local_beentry(i);
		beentry = &local_beentry->backendStatus;

		if (beentry->st_procpid == MyProcPid)
			return beentry;
	}
	return NULL;
}

static int
pg_get_application_name(char *application_name)
{
	PgBackendStatus *beentry = pg_get_backend_status();

	snprintf(application_name, APPLICATIONNAME_LEN, "%s", beentry->st_appname);
	return strlen(application_name);
}

/*
 * Store some statistics for a statement.
 *
 * If queryId is 0 then this is a utility statement and we should compute
 * a suitable queryId internally.
 *
 * If jstate is not NULL then we're trying to create an entry for which
 * we have no statistics as yet; we just want to record the normalized
 */

static uint
pg_get_client_addr(void)
{
	PgBackendStatus *beentry = pg_get_backend_status();
	char	remote_host[NI_MAXHOST];
	int		ret;

	memset(remote_host, 0x0, NI_MAXHOST);
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
pgss_update_entry(pgssEntry *entry,
						int bucketid,
						uint64 queryid,
						const char *query,
						PlanInfo *plan_info,
						CmdType cmd_type,
						SysInfo *sys_info,
						ErrorInfo *error_info,
						double total_time,
						uint64 rows,
						BufferUsage *bufusage,
						WalUsage *walusage,
						bool reset,
						pgssStoreKind kind)
{
	int                 index;
	char			    application_name[APPLICATIONNAME_LEN];
	int				    application_name_len = pg_get_application_name(application_name);
	pgssSharedState     *pgss = pgsm_get_ss();
	double              old_mean;
	int             	message_len = error_info ? strlen (error_info->message) : 0;
	int             	sqlcode_len = error_info ? strlen (error_info->sqlcode) : 0;
	int             	plan_text_len = plan_info ? strlen (plan_info->plan_text) : 0;


	/* volatile block */
	{
		volatile pgssEntry *e = (volatile pgssEntry *) entry;
		SpinLockAcquire(&e->mutex);
		/* Start collecting data for next bucket and reset all counters */
		if (reset)
			memset(&entry->counters, 0, sizeof(Counters));

		e->counters.state = kind;
		if (kind == PGSS_PLAN)
		{
			if (e->counters.plancalls.calls == 0)
				e->counters.plancalls.usage = USAGE_INIT;
			e->counters.plancalls.calls += 1;
			e->counters.plantime.total_time += total_time;

			if (e->counters.plancalls.calls == 1)
			{
				e->counters.plantime.min_time = total_time;
				e->counters.plantime.max_time = total_time;
				e->counters.plantime.mean_time = total_time;
			}

			/* Increment the counts, except when jstate is not NULL */
			old_mean = e->counters.plantime.mean_time;
			e->counters.plantime.mean_time += (total_time - old_mean) / e->counters.plancalls.calls;
			e->counters.plantime.sum_var_time +=(total_time - old_mean) * (total_time - e->counters.plantime.mean_time);

			/* calculate min and max time */
			if (e->counters.plantime.min_time > total_time) e->counters.plantime.min_time = total_time;
			if (e->counters.plantime.max_time < total_time) e->counters.plantime.max_time = total_time;
		}
		else if (kind == PGSS_FINISHED)
		{
			if (e->counters.calls.calls == 0)
				e->counters.calls.usage = USAGE_INIT;
			e->counters.calls.calls += 1;
			e->counters.time.total_time += total_time;

			if (e->counters.calls.calls == 1)
			{
				e->counters.time.min_time = total_time;
				e->counters.time.max_time = total_time;
				e->counters.time.mean_time = total_time;
			}

			/* Increment the counts, except when jstate is not NULL */
			old_mean = e->counters.time.mean_time;
			e->counters.time.mean_time += (total_time - old_mean) / e->counters.calls.calls;
			e->counters.time.sum_var_time +=(total_time - old_mean) * (total_time - e->counters.time.mean_time);

			/* calculate min and max time */
			if (e->counters.time.min_time > total_time) e->counters.time.min_time = total_time;
			if (e->counters.time.max_time < total_time) e->counters.time.max_time = total_time;

			index = get_histogram_bucket(total_time);
			e->counters.resp_calls[index]++;
		}

		_snprintf(e->counters.planinfo.plan_text, plan_info->plan_text, plan_text_len, PLAN_TEXT_LEN);
		_snprintf(e->counters.info.application_name, application_name, application_name_len, APPLICATIONNAME_LEN);

		e->counters.info.num_relations = pgss->num_relations;
		_snprintf2(e->counters.info.relations, pgss->relations, pgss->num_relations,  REL_LEN);

		e->counters.info.cmd_type = cmd_type;

		if(nested_level > 0)
		{
			if (nested_level >=0 && nested_level < max_stack_depth)
				e->counters.info.parentid = nested_queryids[nested_level - 1];
		}
		else
		{
			e->counters.info.parentid = UINT64CONST(0);
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
		}
		e->counters.calls.usage += USAGE_EXEC(total_time);
		e->counters.info.host = pg_get_client_addr();
		if (sys_info)
		{
			e->counters.sysinfo.utime = sys_info->utime;
			e->counters.sysinfo.stime = sys_info->stime;
		}
		if (walusage)
		{
			e->counters.walusage.wal_records += walusage->wal_records;
			e->counters.walusage.wal_fpi += walusage->wal_fpi;
			e->counters.walusage.wal_bytes += walusage->wal_bytes;
		}
		SpinLockRelease(&e->mutex);
	}
}

static pgssEntry*
pgss_get_entry(uint64 bucket_id,
			   uint64 userid,
			   uint64 dbid,
			   uint64 queryid,
			   uint64 ip,
			   uint64 planid)
{
	pgssEntry       *entry;
	pgssHashKey		key;
	HTAB            *pgss_hash = pgsm_get_hash();
	pgssSharedState *pgss = pgsm_get_ss();

	key.bucket_id = bucket_id;
	key.userid = userid;
	key.dbid = MyDatabaseId;
	key.queryid = queryid;
	key.ip = pg_get_client_addr();
	key.planid = planid;

	entry = (pgssEntry *) hash_search(pgss_hash, &key, HASH_FIND, NULL);
	if(!entry)
	{
		 /* OK to create a new hashtable entry */
		entry = hash_entry_alloc(pgss, &key, GetDatabaseEncoding());
		if (entry == NULL)
			return NULL;
	}
	Assert(entry);
	return entry;
}

static void
pgss_store_query(uint64 queryid,
                  const char * query,
				  CmdType cmd_type,
				  int query_location,
				  int query_len,
				  pgssJumbleState *jstate,
				  pgssStoreKind kind)
{
	char *norm_query = NULL;

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

	if (jstate)
		norm_query = generate_normalized_query(jstate, query,
											   query_location,
											   &query_len,
											   GetDatabaseEncoding());
	/*
     * For utility statements, we just hash the query string to get an ID.
     */
	if (queryid == UINT64CONST(0))
		queryid = pgss_hash_string(query, query_len);

	pgss_store(queryid,                 /* query id */
				PGSM_NORMALIZED_QUERY ? (norm_query ? norm_query : query) : query, /* query */
				NULL,					/* PlanInfo */
				cmd_type,				/* CmdType */
				NULL,					/* SysInfo */
				NULL,					/* ErrorInfo */
				0,						/* totaltime */
				0,						/* rows */
				NULL,					/*  bufusage */
				NULL,					/* walusage */
				jstate,					/* pgssJumbleState */
				kind);					/* pgssStoreKind */
}

static void
pgss_store_error(uint64 queryid,
				const char * query,
				ErrorData *edata)
{
	ErrorInfo		error_info;

	error_info.elevel = edata->elevel;
	snprintf(error_info.message, ERROR_MESSAGE_LEN, "%s", edata->message);
	snprintf(error_info.sqlcode, SQLCODE_LEN, "%s", unpack_sql_state(edata->sqlerrcode));

	pgss_store(queryid,                 /* query id */
				query,					/* query text */
				NULL,					/* PlanInfo */
				0,						/* CmdType */
				NULL,					/* SysInfo */
				&error_info, 			/* ErrorInfo */
				0,						/* total_time */
				0,						/* rows */
				NULL,					/* bufusage */
				NULL,					/* walusage */
				NULL,					/* pgssJumbleState */
				PGSS_ERROR);			/* pgssStoreKind */
}

static void
pgss_store_utility(const char *query,
					double total_time,
					uint64 rows,
					BufferUsage *bufusage,
					WalUsage *walusage)
{
	uint64          queryid = pgss_hash_string(query, strlen(query));

	pgss_store(queryid,                 /* query id */
				query,					/* query text */
				NULL,					/* PlanInfo */
				0,						/* CmdType */
				NULL,					/* SysInfo */
				NULL, 					/* ErrorInfo */
				total_time,				/* total_time */
				rows,					/* rows */
				bufusage,				/* bufusage */
				walusage,				/* walusage */
				NULL,					/* pgssJumbleState */
				PGSS_FINISHED);			/* pgssStoreKind */
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
pgss_store(uint64 queryid,
			const char *query,
			PlanInfo *plan_info,
			CmdType cmd_type,
			SysInfo *sys_info,
			ErrorInfo *error_info,
			double total_time,
			uint64 rows,
			BufferUsage *bufusage,
			WalUsage *walusage,
			pgssJumbleState *jstate,
			pgssStoreKind kind)
{
	pgssEntry		*entry;
	pgssSharedState *pgss = pgsm_get_ss();
	bool 			reset = false;
	uint64 			bucketid;
	uint64 			userid = GetUserId();
	uint64 			dbid = MyDatabaseId;
	uint64 			ip = pg_get_client_addr();
	uint64			planid = plan_info ? plan_info->planid: 0;
	const char      *redacted_query;

	/*  Monitoring is disabled */
	if (!PGSM_ENABLED)
		return;

	Assert(query != NULL);

	redacted_query = RedactPasswordIfExists(query);

	/* Safety check... */
	if (!IsSystemInitialized() || !pgss_qbuf[pgss->current_wbucket])
		return;

	bucketid = get_next_wbucket(pgss);
	if (bucketid != pgss->current_wbucket)
	{
		reset = true;
		pgss->current_wbucket = bucketid;
	}

	LWLockAcquire(pgss->lock, LW_EXCLUSIVE);

	switch (kind)
	{
		case PGSS_PARSE:
		case PGSS_PLAN:
		{
			pgssQueryEntry *query_entry;
			query_entry = pgss_store_query_info(bucketid, queryid, dbid, userid, ip, redacted_query, strlen(redacted_query), kind);
			if (query_entry == NULL)
				elog(DEBUG1, "pg_stat_monitor: out of memory");
			break;
		}
		case PGSS_ERROR:
		case PGSS_EXEC:
		case PGSS_FINISHED:
		{
			pgssQueryEntry *query_entry;
			query_entry = pgss_store_query_info(bucketid, queryid, dbid, userid, ip, redacted_query, strlen(redacted_query), kind);
			if (query_entry == NULL)
			{
				elog(DEBUG1, "pg_stat_monitor: out of memory");
				break;
			}
			entry = pgss_get_entry(bucketid, userid, dbid, queryid, ip, planid);
			if (entry == NULL)
			{
				elog(DEBUG1, "pg_stat_monitor: out of memory");
				break;
			}

			if (jstate == NULL)
				pgss_update_entry(entry,		/* entry */
							bucketid,			/* bucketid */
							queryid,			/* queryid */
							redacted_query, 	/* query */
							plan_info,			/* PlanInfo */
							cmd_type,			/* CmdType */
							sys_info,			/* SysInfo */
							error_info,			/* ErrorInfo */
							total_time,			/* total_time */
							rows,				/* rows */
							bufusage,			/* bufusage */
							walusage,			/* walusage */
							reset,				/* reset */
							kind);				/* kind */
		}
		break;
		case PGSS_NUMKIND:
		case PGSS_INVALID:
			break;
	}
	LWLockRelease(pgss->lock);
}
/*
 * Reset all statement statistics.
 */
Datum
pg_stat_monitor_reset(PG_FUNCTION_ARGS)
{
	pgssSharedState     *pgss = pgsm_get_ss();
	/* Safety check... */
	if (!IsSystemInitialized())
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("pg_stat_monitor: must be loaded via shared_preload_libraries")));
	LWLockAcquire(pgss->lock, LW_EXCLUSIVE);
	hash_entry_dealloc(-1);
	hash_query_entry_dealloc(-1);
	LWLockRelease(pgss->lock);
	PG_RETURN_VOID();
}

Datum
pg_stat_monitor(PG_FUNCTION_ARGS)
{
	pg_stat_monitor_internal(fcinfo, true);
	return (Datum) 0;
}

static bool
IsBucketValid(uint64 bucketid)
{
	struct tm     tm;
	time_t        bucket_t,current_t;
	double        diff_t;
	pgssSharedState      *pgss = pgsm_get_ss();

	memset(&tm, 0, sizeof(tm));
	strptime(pgss->bucket_start_time[bucketid],  "%Y-%m-%d %H:%M:%S", &tm);
	bucket_t = mktime(&tm);

	time(&current_t);
	diff_t = difftime(current_t, bucket_t);
	if (diff_t > (PGSM_BUCKET_TIME * PGSM_MAX_BUCKETS))
		return false;
	return true;
}

/* Common code for all versions of pg_stat_statements() */
static void
pg_stat_monitor_internal(FunctionCallInfo fcinfo,
						bool showtext)
{
	ReturnSetInfo	     *rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
	TupleDesc		     tupdesc;
	Tuplestorestate      *tupstore;
	MemoryContext	     per_query_ctx;
	MemoryContext	     oldcontext;
	HASH_SEQ_STATUS      hash_seq;
	pgssEntry		     *entry;
	pgssQueryEntry		 *query_entry;
	char			     parentid_txt[32];
	pgssSharedState      *pgss = pgsm_get_ss();
	HTAB                 *pgss_hash = pgsm_get_hash();
	char 				*query_txt = (char*) malloc(PGSM_QUERY_MAX_LEN);

	/* Safety check... */
	if (!IsSystemInitialized())
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("pg_stat_monitor: must be loaded via shared_preload_libraries")));

	/* check to see if caller supports us returning a tuplestore */
	if (rsinfo == NULL || !IsA(rsinfo, ReturnSetInfo))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("pg_stat_monitor: set-valued function called in context that cannot accept a set")));
	if (!(rsinfo->allowedModes & SFRM_Materialize))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("pg_stat_monitor: materialize mode required, but it is not " \
						"allowed in this context")));

	/* Switch into long-lived context to construct returned data structures */
	per_query_ctx = rsinfo->econtext->ecxt_per_query_memory;
	oldcontext = MemoryContextSwitchTo(per_query_ctx);

	/* Build a tuple descriptor for our result type */
	if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
		elog(ERROR, "pg_stat_monitor: return type must be a row type");

	if (tupdesc->natts != 48)
		elog(ERROR, "pg_stat_monitor: incorrect number of output arguments, required %d", tupdesc->natts);

	tupstore = tuplestore_begin_heap(true, false, work_mem);
	rsinfo->returnMode = SFRM_Materialize;
	rsinfo->setResult = tupstore;
	rsinfo->setDesc = tupdesc;

	MemoryContextSwitchTo(oldcontext);

	LWLockAcquire(pgss->lock, LW_SHARED);

	hash_seq_init(&hash_seq, pgss_hash);
	while ((entry = hash_seq_search(&hash_seq)) != NULL)
	{
		Datum         values[PG_STAT_STATEMENTS_COLS] = {0};
		bool          nulls[PG_STAT_STATEMENTS_COLS] = {0};
		int		      i = 0;
		Counters      tmp;
		double        stddev;
		char          queryid_text[32] = {0};
		char          planid_text[32] = {0};
		uint64        queryid = entry->key.queryid;
		uint64		  bucketid = entry->key.bucket_id;
		uint64		  dbid = entry->key.dbid;
		uint64		  userid = entry->key.userid;
		uint64        ip = entry->key.ip;
		uint64        planid = entry->key.planid;
		unsigned char *buf = pgss_qbuf[bucketid];
		bool 		  is_allowed_role = is_member_of_role(GetUserId(), DEFAULT_ROLE_READ_ALL_STATS);

		if (!IsBucketValid(bucketid))
			continue;

		query_entry = hash_find_query_entry(bucketid, queryid, dbid, userid, ip);
		if (query_entry == NULL)
			continue;

		if (read_query(buf, bucketid, queryid, query_txt) == 0)
		{
			int len;
			len = read_query_buffer(bucketid, queryid, query_txt);
			if (len != MAX_QUERY_BUFFER_BUCKET)
				snprintf(query_txt, 32, "%s", "insufficient disk/shared space");
		}

		/* copy counters to a local variable to keep locking time short */
		{
			volatile pgssEntry *e = (volatile pgssEntry *) entry;
			SpinLockAcquire(&e->mutex);
			tmp = e->counters;
			SpinLockRelease(&e->mutex);
		}
		/* bucketid at column number 0 */
		values[i++] = Int64GetDatumFast(bucketid);

		/* userid at column number 1 */
		values[i++] = ObjectIdGetDatum(userid);

		/* dbid at column number 2 */
		values[i++] = ObjectIdGetDatum(dbid);

		/*
	 	* ip address at column number 3,
	 	* Superusers or members of pg_read_all_stats members
	 	* are allowed
		*/
		if (is_allowed_role || userid == GetUserId())
			values[i++] = Int64GetDatumFast(ip);
		else
			nulls[i++] = true;

		/* queryid at column number 4 */
		snprintf(queryid_text, 32, "%08lX", queryid);
		values[i++] = CStringGetTextDatum(queryid_text);

		/* planid at column number 5 */
		if (planid)
		{
			snprintf(planid_text, 32, "%08lX", planid);
			values[i++] = CStringGetTextDatum(planid_text);
		}
		else
		{
			nulls[i++] = true;
		}
		if (is_allowed_role || userid == GetUserId())
		{
			if (showtext)
			{
				char	*enc;

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


		/* state at column number 8 */
		values[i++] = Int64GetDatumFast(tmp.state);

		/* parentid at column number 9 */
        if (tmp.info.parentid != UINT64CONST(0))
		{
            snprintf(parentid_txt, 32, "%08lX",tmp.info.parentid);
            values[i++] = CStringGetTextDatum(parentid_txt);
        }
        else
        {
            nulls[i++] = true;
        }

		/* application_name at column number 9 */
		if (strlen(tmp.info.application_name) > 0)
			values[i++] = CStringGetTextDatum(tmp.info.application_name);
		else
			nulls[i++] = true;

		/* relations at column number 10 */
		if (tmp.info.num_relations > 0)
		{
			int     j;
			char    *text_str = palloc0(1024);
			char    *tmp_str = palloc0(1024);
			bool    first = true;

			/* Need to calculate the actual size, and avoid unnessary memory usage */
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
		if (tmp.info.cmd_type < 0)
			nulls[i++] = true;
		else
			values[i++] = Int64GetDatumFast(tmp.info.cmd_type);

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
		values[i++] = CStringGetTextDatum(pgss->bucket_start_time[entry->key.bucket_id]);
		if (tmp.calls.calls == 0)
		{
			/*  Query of pg_stat_monitor itslef started from zero count */
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
			/*  Query of pg_stat_monitor itslef started from zero count */
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

		/* resp_calls at column number 41 */
		values[i++] = IntArrayGetTextDatum(tmp.resp_calls, MAX_RESPONSE_BUCKET);

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
		}
		tuplestore_putvalues(tupstore, tupdesc, values, nulls);
	}
	free(query_txt);
	/* clean up and return the tuplestore */
	LWLockRelease(pgss->lock);

	tuplestore_donestoring(tupstore);
}

static uint64
get_next_wbucket(pgssSharedState *pgss)
{
	struct timeval	tv;
	uint64          current_usec;
	uint64          bucket_id;
	struct tm       *lt;
	const  char     *yb_tmp_dir = YbGetTmpDir();

	gettimeofday(&tv,NULL);
	current_usec = (TimestampTz) tv.tv_sec - ((POSTGRES_EPOCH_JDATE - UNIX_EPOCH_JDATE) * SECS_PER_DAY);
	current_usec = (current_usec * USECS_PER_SEC) + tv.tv_usec;

	if ((current_usec - pgss->prev_bucket_usec) > (PGSM_BUCKET_TIME * 1000 * 1000))
	{
		unsigned char *buf;
		char          file_name[1024];
		int            sec = 0;

		bucket_id = (tv.tv_sec / PGSM_BUCKET_TIME) % PGSM_MAX_BUCKETS;
		LWLockAcquire(pgss->lock, LW_EXCLUSIVE);
		buf = pgss_qbuf[bucket_id];
		hash_entry_dealloc(bucket_id);
		hash_query_entry_dealloc(bucket_id);
		if (yb_tmp_dir)
			snprintf(file_name, 1024, "%s/%s.%d", yb_tmp_dir,
					 YB_PGSM_TEXT_FILE_PREFIX, (int) bucket_id);
		else
			snprintf(file_name, 1024, "%s.%d", PGSM_TEXT_FILE, (int)bucket_id);

		unlink(file_name);

		/* reset the query buffer */
		memset(buf, 0, sizeof (uint64));
		LWLockRelease(pgss->lock);
		pgss->prev_bucket_usec = current_usec;
		lt = localtime(&tv.tv_sec);
		sec = lt->tm_sec - (lt->tm_sec % PGSM_BUCKET_TIME);
		if (sec < 0)
			sec = 0;
		snprintf(pgss->bucket_start_time[bucket_id], sizeof(pgss->bucket_start_time[bucket_id]),
				"%04d-%02d-%02d %02d:%02d:%02d", lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday, lt->tm_hour, lt->tm_min, sec);
		return bucket_id;
	}
	return pgss->current_wbucket;
}

/*
 * AppendJumble: Append a value that is substantive in a given query to
 * the current jumble.
 */
static void
AppendJumble(pgssJumbleState *jstate, const unsigned char *item, Size size)
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

			start_hash = DatumGetUInt64(hash_any_extended(jumble,
														  JUMBLE_SIZE, 0));
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
JumbleQuery(pgssJumbleState *jstate, Query *query)
{
	Assert(IsA(query, Query));
	Assert(query->utilityStmt == NULL);

	APP_JUMB(query->commandType);
	/* resultRelation is usually predictable from commandType */
	JumbleExpr(jstate, (Node *) query->cteList);
	JumbleRangeTable(jstate, query->rtable);
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
JumbleRangeTable(pgssJumbleState *jstate, List *rtable)
{
	ListCell   *lc = NULL;

	foreach(lc, rtable)
	{
		RangeTblEntry *rte = lfirst_node(RangeTblEntry, lc);

		APP_JUMB(rte->rtekind);
		switch (rte->rtekind)
		{
			case RTE_RELATION:
				if (IsYugaByteEnabled() && rte->relid >= FirstNormalObjectId)
					APP_JUMB(MyDatabaseId);
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
				elog(ERROR, "unrecognized RTE kind: %d", (int) rte->rtekind);
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
JumbleExpr(pgssJumbleState *jstate, Node *node)
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
		case T_YbBatchedExpr:
			{
				YbBatchedExpr	*bexpr = (YbBatchedExpr *) node;

				JumbleExpr(jstate, (Node *) bexpr->orig_expr);
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
			elog(INFO, "unrecognized node type: %d",
				 (int) nodeTag(node));
			break;
	}
}

/*
 * Record location of constant within query string of query tree
 * that is currently being walked.
 */
static void
RecordConstLocation(pgssJumbleState *jstate, int location)
{
	/* -1 indicates unknown or undefined location */
	if (location >= 0)
	{
		/* enlarge array if needed */
		if (jstate->clocations_count >= jstate->clocations_buf_size)
		{
			jstate->clocations_buf_size *= 2;
			jstate->clocations = (pgssLocationLen *)
				repalloc(jstate->clocations,
						 jstate->clocations_buf_size *
						 sizeof(pgssLocationLen));
		}
		jstate->clocations[jstate->clocations_count].location = location;
		/* initialize lengths to -1 to simplify fill_in_constant_lengths */
		jstate->clocations[jstate->clocations_count].length = -1;
		jstate->clocations_count++;
	}
}

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
generate_normalized_query(pgssJumbleState *jstate, const char *query,
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
fill_in_constant_lengths(pgssJumbleState *jstate, const char *query,
						 int query_loc)
{
	pgssLocationLen		*locs;
	core_yyscan_t		yyscanner;
	core_yy_extra_type	yyextra;
	core_YYSTYPE		yylval;
	YYLTYPE				yylloc;
	int					last_loc = -1;
	int					i;

	/*
	 * Sort the records by location so that we can process them in order while
	 * scanning the query text.
	 */
	if (jstate->clocations_count > 1)
		qsort(jstate->clocations, jstate->clocations_count,
			  sizeof(pgssLocationLen), comp_location);
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
 * comp_location: comparator for qsorting pgssLocationLen structs by location
 */
static int
comp_location(const void *a, const void *b)
{
	int	l = ((const pgssLocationLen *) a)->location;
	int	r = ((const pgssLocationLen *) b)->location;

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
	int     j;
	char    str[1024];
	char    tmp[10];
	bool    first = true;

	memset(str, 0, sizeof(str));

	/* Need to calculate the actual size, and avoid unnessary memory usage */
	for (j = 0; j < len; j++)
	{
		if (first)
		{
			snprintf(tmp, 10, "%d", arr[j]);
			strcat(str,tmp);
			first = false;
			continue;
		}
		snprintf(tmp, 10, ",%d", arr[j]);
		strcat(str,tmp);
	}
	return CStringGetTextDatum(str);

}

static uint64
read_query(unsigned char *buf, uint64 bucketid, uint64 queryid, char * query)
{
	bool found            = false;
	uint64 query_id       = 0;
	uint64 query_len      = 0;
	uint64 rlen           = 0;
	uint64 buf_len        = 0;

	memcpy(&buf_len, buf, sizeof (uint64));
	if (buf_len <= 0)
		goto exit;

	rlen = sizeof (uint64); /* Move forwad to skip length bytes */
	for(;;)
	{
		if (rlen >= buf_len)
			goto exit;

		memcpy(&query_id, &buf[rlen], sizeof (uint64)); /* query id */
		if (query_id == queryid)
			found = true;

		rlen += sizeof (uint64);
		if (buf_len <= rlen)
			continue;

		memcpy(&query_len, &buf[rlen], sizeof (uint64)); /* query len */
		rlen += sizeof (uint64);
		if (buf_len < rlen + query_len)
			goto exit;

		if (found)
		{
			if (query != NULL)
			{
				memcpy(query, &buf[rlen], query_len); /* Actual query */
				query[query_len] = 0;
			}
			return query_id;
		}
		rlen += query_len;
	}
exit:
	if (PGSM_OVERFLOW_TARGET == OVERFLOW_TARGET_NONE)
	{
		sprintf(query, "%s", "<insufficient shared space>");
		return -1;
	}
	return 0;
}

static pgssQueryEntry*
pgss_store_query_info(uint64 bucketid,
					  uint64 queryid,
					  uint64 dbid,
					  uint64 userid,
					  uint64 ip,
					  const char *query,
					  uint64 query_len,
					  pgssStoreKind kind)
{
    uint64          buf_len = 0;
	pgssSharedState *pgss   = pgsm_get_ss();
	unsigned char   *buf    = pgss_qbuf[pgss->current_wbucket];
	pgssQueryEntry	*entry;

	if (query_len > PGSM_QUERY_MAX_LEN)
		query_len = PGSM_QUERY_MAX_LEN;

	/* Already have query in the shared buffer, there
	 * is no need to add that again.
	 */
	entry = hash_find_query_entry(bucketid, queryid, dbid, userid, ip);
	if (entry)
		return entry;

	entry = hash_create_query_entry(bucketid, queryid, dbid, userid, ip);
	if (!entry)
		return NULL;

	memcpy(&buf_len, buf, sizeof (uint64));
	if (buf_len == 0)
		buf_len += sizeof (uint64);

	if (QUERY_BUFFER_OVERFLOW(buf_len, query_len))
	{
		switch(PGSM_OVERFLOW_TARGET)
		{
			case OVERFLOW_TARGET_NONE:
				return NULL;
			case OVERFLOW_TARGET_DISK:
			{
				dump_queries_buffer(bucketid, buf, MAX_QUERY_BUFFER_BUCKET);
				buf_len = sizeof (uint64);
			}
			break;
			default:
				break;
		}
	}

	memcpy(&buf[buf_len], &queryid, sizeof (uint64)); /* query id */
	buf_len += sizeof (uint64);

	memcpy(&buf[buf_len], &query_len, sizeof (uint64)); /* query length */
	buf_len += sizeof (uint64);

	memcpy(&buf[buf_len], query, query_len); /* query */
	buf_len += query_len;
	memcpy(buf, &buf_len, sizeof (uint64));
	return entry;
}

static uint64
get_query_id(pgssJumbleState *jstate, Query *query)
{
	uint64 queryid;

	/* Set up workspace for query jumbling */
	jstate->jumble = (unsigned char *) palloc(JUMBLE_SIZE);
	jstate->jumble_len = 0;
	jstate->clocations_buf_size = 32;
	jstate->clocations = (pgssLocationLen *) palloc(jstate->clocations_buf_size * sizeof(pgssLocationLen));
	jstate->clocations_count = 0;
	jstate->highest_extern_param_id = 0;

	/* Compute query ID and mark the Query node with it */
	JumbleQuery(jstate, query);
	queryid = DatumGetUInt64(hash_any_extended(jstate->jumble, jstate->jumble_len, 0));
	return queryid;
}

Datum
pg_stat_monitor_settings(PG_FUNCTION_ARGS)
{
	ReturnSetInfo		*rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
	TupleDesc			tupdesc;
	Tuplestorestate		*tupstore;
	MemoryContext		per_query_ctx;
	MemoryContext		oldcontext;
	int					i;

	/* Safety check... */
	if (!IsSystemInitialized())
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("pg_stat_monitor: must be loaded via shared_preload_libraries")));

	/* check to see if caller supports us returning a tuplestore */
	if (rsinfo == NULL || !IsA(rsinfo, ReturnSetInfo))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("pg_stat_monitor: set-valued function called in context that cannot accept a set")));

	/* Switch into long-lived context to construct returned data structures */
	per_query_ctx = rsinfo->econtext->ecxt_per_query_memory;
	oldcontext = MemoryContextSwitchTo(per_query_ctx);

	/* Build a tuple descriptor for our result type */
	if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
		elog(ERROR, "pg_stat_monitor: return type must be a row type");

	if (tupdesc->natts != 7)
		elog(ERROR, "pg_stat_monitor: incorrect number of output arguments, required %d", tupdesc->natts);

	tupstore = tuplestore_begin_heap(true, false, work_mem);
	rsinfo->returnMode = SFRM_Materialize;
	rsinfo->setResult = tupstore;
	rsinfo->setDesc = tupdesc;

	MemoryContextSwitchTo(oldcontext);

	for(i = 0; i < MAX_SETTINGS; i++)
	{
		Datum		values[7];
		bool		nulls[7];
		int			j = 0;
		memset(values, 0, sizeof(values));
		memset(nulls, 0, sizeof(nulls));

		values[j++] = CStringGetTextDatum(get_conf(i)->guc_name);
		values[j++] = Int64GetDatumFast(get_conf(i)->guc_variable);
		values[j++] = Int64GetDatumFast(get_conf(i)->guc_default);
		values[j++] = CStringGetTextDatum(get_conf(i)->guc_desc);
		values[j++] = Int64GetDatumFast(get_conf(i)->guc_min);
		values[j++] = Int64GetDatumFast(get_conf(i)->guc_max);
		values[j++] = Int64GetDatumFast(get_conf(i)->guc_restart);
		tuplestore_putvalues(tupstore, tupdesc, values, nulls);
	}
	/* clean up and return the tuplestore */
	tuplestore_donestoring(tupstore);
	return (Datum)0;
}

void
set_qbuf(int i, unsigned char *buf)
{
	pgss_qbuf[i] = buf;
}

void
pgsm_emit_log_hook(ErrorData *edata)
{
	if (!IsSystemInitialized() || edata == NULL)
		goto exit;

	if ((edata->elevel == ERROR || edata->elevel == WARNING || edata->elevel == INFO || edata->elevel == DEBUG1))
	{
		uint64 queryid = 0;

		if (debug_query_string)
			queryid = DatumGetUInt64(hash_any_extended((const unsigned char *)debug_query_string, strlen(debug_query_string), 0));

		pgss_store_error(queryid,
						debug_query_string ? debug_query_string : "",
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

static void
dump_queries_buffer(int bucket_id, unsigned char *buf, int buf_len)
{
    int  fd = 0;
	char file_name[1024];
	const char *yb_tmp_dir = YbGetTmpDir();

	if (yb_tmp_dir)
		snprintf(file_name, 1024, "%s/%s.%d", yb_tmp_dir,
				 YB_PGSM_TEXT_FILE_PREFIX, bucket_id);
	else
		snprintf(file_name, 1024, "%s.%d", PGSM_TEXT_FILE, bucket_id);

	fd = OpenTransientFile(file_name, O_RDWR | O_CREAT | O_APPEND | PG_BINARY);
    if (fd < 0)
		ereport(LOG,
            (errcode_for_file_access(),
             	errmsg("could not write file \"%s\": %m",
                    		file_name)));

    if (write(fd, buf, buf_len) != buf_len)
		ereport(LOG,
            (errcode_for_file_access(),
             	errmsg("could not write file \"%s\": %m",
                    		file_name)));
    if (fd > 0)
		CloseTransientFile(fd);
}

int
read_query_buffer(int bucket_id, uint64 queryid, char *query_txt)
{
    int           fd = 0;
	int           buf_len = 0;
	char          file_name[1024];
	unsigned char *buf = NULL;
	int           off = 0;
	const char	  *yb_tmp_dir = YbGetTmpDir();

	if (yb_tmp_dir)
		snprintf(file_name, 1024, "%s/%s.%d", yb_tmp_dir,
				 YB_PGSM_TEXT_FILE_PREFIX, bucket_id);
	else
		snprintf(file_name, 1024, "%s.%d", PGSM_TEXT_FILE, bucket_id);

    fd = OpenTransientFile(file_name, O_RDONLY | PG_BINARY);
	if (fd < 0)
		goto exit;

	buf = (unsigned char*) palloc(MAX_QUERY_BUFFER_BUCKET);
	for(;;)
	{
		if (lseek(fd, off, SEEK_SET) != off)
			goto exit;

		buf_len = read(fd, buf, MAX_QUERY_BUFFER_BUCKET);
		if (buf_len != MAX_QUERY_BUFFER_BUCKET)
		{
			if (errno != ENOENT)
				goto exit;

			if (buf_len == 0)
				break;
		}
		off += buf_len;
		if (read_query(buf, bucket_id, queryid, query_txt))
			break;
	}
	if (fd > 0)
    	CloseTransientFile(fd);
	if (buf)
		pfree(buf);
	return buf_len;

exit:
	ereport(LOG,
		(errcode_for_file_access(),
			errmsg("could not read file \"%s\": %m",
				file_name)));
	if (fd > 0)
    	CloseTransientFile(fd);
	if (buf)
		pfree(buf);
	return buf_len;
}

static double
time_diff(struct timeval end, struct timeval start)
{
	double mstart;
	double mend;
	mend = ((double) end.tv_sec * 1000.0 + (double) end.tv_usec / 1000.0);
	mstart   = ((double) start.tv_sec * 1000.0 + (double) start.tv_usec / 1000.0);
	return mend - mstart;
}

char *
unpack_sql_state(int sql_state)
{
    static char buf[12];
    int         i;

    for (i = 0; i < 5; i++)
    {
        buf[i] = PGUNSIXBIT(sql_state);
        sql_state >>= 6;
    }

    buf[i] = '\0';
    return buf;
}

static int
get_histogram_bucket(double q_time)
{
	double q_min = PGSM_HISTOGRAM_MIN;
	double q_max = PGSM_HISTOGRAM_MAX;
	int    b_count = PGSM_HISTOGRAM_BUCKETS;
	int    index = 0;
	double b_max;
	double b_min;
	double bucket_size;

	q_time -= q_min;

	b_max = log(q_max - q_min);
	b_min = 0;

	bucket_size = (b_max - b_min) / (double)b_count;

	for(index = 1; index <= b_count; index++)
	{
		int64 b_start = (index == 1)? 0 : exp(bucket_size * (index - 1));
		int64 b_end = exp(bucket_size * index);
		if( (index == 1 && q_time < b_start)
			|| (q_time >= b_start && q_time <= b_end)
			|| (index == b_count && q_time > b_end) )
		{
			return index - 1;
		}
	}
	return 0;
}

Datum
get_histogram_timings(PG_FUNCTION_ARGS)
{
	double q_min = PGSM_HISTOGRAM_MIN;
	double q_max = PGSM_HISTOGRAM_MAX;
	int    b_count = PGSM_HISTOGRAM_BUCKETS;
	int    index = 0;
	double b_max;
	double b_min;
	double bucket_size;
	bool	first = true;
	char    *tmp_str = palloc0(MAX_STRING_LEN);
	char    *text_str = palloc0(MAX_STRING_LEN);

	b_max = log(q_max - q_min);
	b_min = 0;
	bucket_size = (b_max - b_min) / (double)b_count;
	for(index = 1; index <= b_count; index++)
	{
		int64 b_start = (index == 1)? 0 : exp(bucket_size * (index - 1));
		int64 b_end = exp(bucket_size * index);
		if (first)
		{
			snprintf(text_str, MAX_STRING_LEN, "(%ld - %ld)}", b_start, b_end);
			first = false;
		}
		else
		{
			snprintf(tmp_str, MAX_STRING_LEN, "%s, (%ld - %ld)}", text_str, b_start, b_end);
			snprintf(text_str, MAX_STRING_LEN, "%s", tmp_str);
		}
	}
	pfree(tmp_str);
	return CStringGetTextDatum(text_str);
}

