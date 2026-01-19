/*-------------------------------------------------------------------------
 *
 * yb_query_diagnostics.c
 *    Utilities for Query Diagnostics/Yugabyte (Postgres layer) integration
 *    that have to be defined on the PostgreSQL side.
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License. You may obtain
 * a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied.  See the License for the specific language governing
 * permissions and limitations under the License.
 *
 * IDENTIFICATION
 *	  src/backend/utils/misc/yb_query_diagnostics.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include <math.h>
#include <unistd.h>

#include "access/hash.h"
#include "access/htup_details.h"
#include "access/xact.h"
#include "catalog/indexing.h"
#include "catalog/pg_collation.h"
#include "catalog/pg_constraint.h"
#include "catalog/pg_namespace_d.h"
#include "catalog/yb_catalog_version.h"
#include "commands/dbcommands.h"
#include "commands/explain.h"
#include "commands/tablespace.h"
#include "common/fe_memutils.h"
#include "common/file_perm.h"
#include "common/pg_prng.h"
#include "common/pg_yb_common.h"
#include "executor/spi.h"
#include "foreign/foreign.h"
#include "funcapi.h"
#include "parser/analyze.h"
#include "pg_yb_utils.h"
#include "pgstat.h"
#include "postmaster/bgworker.h"
#include "postmaster/interrupt.h"
#include "storage/ipc.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/jsonb.h"
#include "utils/lsyscache.h"
#include "utils/syscache.h"
#include "yb_file_utils.h"
#include "yb_query_diagnostics.h"

static const char *constants_and_bind_variables_file = "constants_and_bind_variables.csv";
static const char *pgss_file = "pg_stat_statements.csv";
static const char *ash_file = "active_session_history.csv";
static const char *explain_plan_file = "explain_plan.txt";
static const char *schema_details_file = "schema_details.txt";
static const int32 status_view_file_format_id = 0x20250325;
static const char *status_view_file_name = "yb_query_diagnostics_status.txt";
static const char *status_view_tmp_file_name = "yb_query_diagnostics_status.tmp";
static const char *statistics_file = "statistics.json";
static const char *extended_statistics_file = "statistics_ext.json";

/* Separates schema details for different tables by 60 '=' */
const char *schema_details_separator = "============================================================";

/* Maximum number of entries in the hash table */
static const int max_bundles_in_progress = 100;

/* Constants used for yb_query_diagnostics_status view */
#define YB_QUERY_DIAGNOSTICS_STATUS_COLS 8

typedef struct YbBundleInfo
{
	/*
	 * 0 - Success; 1 - In Progress; 2 - Error; 3 - Cancelled; 4 - Postmaster
	 * Shutdown
	 */
	YbQueryDiagnosticsStatusType status;
	YbQueryDiagnosticsMetadata metadata;	/* stores bundle's metadata */
	char		description[YB_QD_DESCRIPTION_LEN]; /* stores error
													 * description */
} YbBundleInfo;

typedef struct YbQueryDiagnosticsBundles
{
	int			index;			/* index to insert new buffer entry */
	int			max_entries;	/* maximum # of entries in the buffer */
	LWLock		lock;			/* protects circular buffer from
								 * search/modification */
	YbBundleInfo bundles[FLEXIBLE_ARRAY_MEMBER];	/* circular buffer to
													 * store info about
													 * bundles */
} YbQueryDiagnosticsBundles;

typedef struct
{
	/*
	 * Identifies the starting position of the query within the source text,
	 * equivalent to Query->stmt_location
	 */
	int			stmt_location;

	/* Number of constants in the query */
	int			count;

	/* Holds the locations of constants in the query */
	LocationLen locations[YB_QD_MAX_CONSTANTS];
} YbQueryConstantsMetadata;

typedef struct YbDatabaseConnectionWorkerInfo
{
	/*
	 * Create a deep copy of the data to prevent race conditions. The original
	 * entry in the hash table may be deleted by yb_query_diagnostics_bgworker
	 * while the database connection worker is still processing it. This ensures data
	 * consistency between background workers.
	 */
	YbQueryDiagnosticsEntry entry;	/* Copy of the entry we're processing */
	bool		initialized;	/* Flag to check if the worker is initialized */
} YbDatabaseConnectionWorkerInfo;

/* GUC variables */
bool		yb_enable_query_diagnostics;
int			yb_query_diagnostics_bg_worker_interval_ms;
int			yb_query_diagnostics_circular_buffer_size;
bool		yb_query_diagnostics_disable_database_connection_bgworker;

/* Saved hook value in case of unload */
static post_parse_analyze_hook_type prev_post_parse_analyze_hook = NULL;
static ExecutorStart_hook_type prev_ExecutorStart = NULL;
static ExecutorEnd_hook_type prev_ExecutorEnd = NULL;

/* Function pointers */
YbGetNormalizedQueryFuncPtr yb_get_normalized_query = NULL;
TimestampTz *yb_pgss_last_reset_time;
YbPgssFillInConstantLengths yb_qd_fill_in_constant_lengths = NULL;

/* session variables */
static YbQueryConstantsMetadata query_constants = {
	.stmt_location = 0,
	.count = 0,
	.locations = {{0, 0}}
};

typedef struct YbBackgroundWorkerHandle
{
	int			slot;
	uint64		generation;
} YbBackgroundWorkerHandle;

enum QueryOutputFormat
{
	YB_QD_TABULAR = 0,
	YB_QD_CSV
};

/* shared variables */
static HTAB *bundles_in_progress = NULL;
static LWLock *bundles_in_progress_lock;	/* protects bundles_in_progress
											 * hash table */
static YbQueryDiagnosticsBundles *bundles_completed = NULL;
static bool current_query_sampled = false;
static YbBackgroundWorkerHandle *bg_worker_handle = NULL;
static YbDatabaseConnectionWorkerInfo *database_connection_worker_info = NULL;
static bool *bg_worker_should_be_active = NULL;

static void YbQueryDiagnostics_post_parse_analyze(ParseState *pstate, Query *query,
												  JumbleState *jstate);
static void YbQueryDiagnostics_ExecutorStart(QueryDesc *queryDesc, int eflags);
static void YbQueryDiagnostics_ExecutorEnd(QueryDesc *queryDesc);

static bool IsBgWorkerStopped();
static void StartBgWorkerIfStopped();
static void InsertNewBundleInfo(YbQueryDiagnosticsMetadata *metadata);
static void FetchParams(YbQueryDiagnosticsParams *params, FunctionCallInfo fcinfo);
static void ConstructDiagnosticsPath(YbQueryDiagnosticsMetadata *metadata);
static void FormatBindVariables(StringInfo buf, const ParamListInfo params);
static void FormatConstants(StringInfo constants, const char *query, int constants_count,
							int query_location, LocationLen *constant_locations);
static int	DumpToFile(const char *path, const char *file_name, const char *data, char *description);
static void FlushAndCleanBundles();
static void AccumulateExplain(QueryDesc *queryDesc, YbQueryDiagnosticsEntry *entry,
							  bool explain_analyze, bool explain_dist, double totaltime_ms);
static inline TimestampTz BundleEndTime(const YbQueryDiagnosticsEntry *entry);
static int	YbQueryDiagnosticsBundlesShmemSize(void);
static Datum CreateJsonb(const YbQueryDiagnosticsParams *params);
static void CreateJsonbInt(JsonbParseState *state, char *key, int64 value);
static void CreateJsonbBool(JsonbParseState *state, char *key, bool value);
static void InsertBundlesIntoView(const YbQueryDiagnosticsMetadata *metadata,
								  YbQueryDiagnosticsStatusType status, const char *description);
static void OutputBundle(const YbQueryDiagnosticsMetadata metadata, const char *description,
						 const char *status, Tuplestorestate *tupstore, TupleDesc tupdesc);
static void ProcessActiveBundles(Tuplestorestate *tupstore, TupleDesc tupdesc);
static void ProcessCompletedBundles(Tuplestorestate *tupstore, TupleDesc tupdesc);
static inline int CircularBufferMaxEntries(void);
static void RemoveBundleInfo(int64 query_id);
static int	MakeDirectory(const char *path, char *description);
static void FinishBundleProcessing(YbQueryDiagnosticsMetadata *metadata,
								   YbQueryDiagnosticsStatusType status, const char *description);
static int	DumpBufferIfHalfFull(char *buffer, size_t max_len, const char *file_name,
								 const char *folder_path, char *description, slock_t *mutex);
static int	DumpActiveSessionHistory(const YbQueryDiagnosticsEntry *entry, char *description);
static void GetResultsInCsvFormat(StringInfo output_buffer, int num_cols);
static void GetResultsInTabularFormat(StringInfo output_buffer, int num_cols);
static void RegisterDatabaseConnectionBgWorker(const YbQueryDiagnosticsEntry *entry);
static void YbSaveQueryDiagnosticsStatusView(int code, Datum arg);
static void YbRestoreQueryDiagnosticsStatusView();
static char *StatusToString(YbQueryDiagnosticsStatusType status);

/* Function used in gathering bind_variables/constants data */
static void AccumulateBindVariablesOrConstants(YbQueryDiagnosticsEntry *entry, StringInfo params);

/* Functions used in gathering pg_stat_statements */
static void PgssToString(int64 query_id, char *pgss_str, YbQueryDiagnosticsPgss pgss,
						 const char *queryString);

/* Functions used in gathering schema details */
static void FetchSchemaOids(List *rtable, Oid *schema_oids);
static bool ExecuteQuery(StringInfo output_buffer, const char *query, int output_format);
static int	DescribeOneTable(Oid oid, const char *db_name, StringInfo schema_details,
							 char *description);
static void PrintTableAndDatabaseName(Oid oid, const char *db_name, StringInfo schema_details);
static int	DumpSchemaDetails(const YbQueryDiagnosticsEntry *entry, char *description);

/* Functions used in gathering cbo_stat_dump */
static int	DumpStatistics(const YbQueryDiagnosticsEntry *entry, char *description);
static int	DumpExtendedStatistics(const YbQueryDiagnosticsEntry *entry, char *description);

void
YbQueryDiagnosticsInstallHook(void)
{
	prev_post_parse_analyze_hook = post_parse_analyze_hook;
	post_parse_analyze_hook = YbQueryDiagnostics_post_parse_analyze;

	prev_ExecutorStart = ExecutorStart_hook;
	ExecutorStart_hook = YbQueryDiagnostics_ExecutorStart;

	prev_ExecutorEnd = ExecutorEnd_hook;
	ExecutorEnd_hook = YbQueryDiagnostics_ExecutorEnd;
}

/*
 * YbQueryDiagnosticsBundlesShmemSize
 *		Compute space needed for yb_query_diagnostics_status view related shared memory
 */
static int
YbQueryDiagnosticsBundlesShmemSize(void)
{
	Size		size;

	size = offsetof(YbQueryDiagnosticsBundles, bundles);
	size = add_size(size, mul_size(CircularBufferMaxEntries(), sizeof(YbBundleInfo)));

	return size;
}

/*
 * YbQueryDiagnosticsShmemSize
 *		Compute space needed for QueryDiagnostics-related shared memory
 */
Size
YbQueryDiagnosticsShmemSize(void)
{
	Size		size;

	size = MAXALIGN(sizeof(LWLock));
	size = add_size(size, hash_estimate_size(max_bundles_in_progress,
											 sizeof(YbQueryDiagnosticsEntry)));
	size = add_size(size, YbQueryDiagnosticsBundlesShmemSize());
	size = add_size(size, sizeof(TimestampTz));
	size = add_size(size, sizeof(YbBackgroundWorkerHandle));	/* bg_worker_handle */
	size = add_size(size, sizeof(YbDatabaseConnectionWorkerInfo));	/* database_connection_worker_info */
	size = add_size(size, sizeof(bool));	/* bg_worker_should_be_active */

	return size;
}

/*
 * YbQueryDiagnosticsShmemInit
 *		Allocate and initialize QueryDiagnostics-related shared memory
 */
void
YbQueryDiagnosticsShmemInit(void)
{
	HASHCTL		ctl;
	bool		found;

	bundles_in_progress = NULL;
	/* Initialize the hash table control structure */
	MemSet(&ctl, 0, sizeof(ctl));

	/* Set the key size and entry size */
	ctl.keysize = sizeof(int64);
	ctl.entrysize = sizeof(YbQueryDiagnosticsEntry);

	/* Create the hash table in shared memory */
	bundles_in_progress_lock = (LWLock *) ShmemInitStruct("YbQueryDiagnostics Lock",
														  sizeof(LWLock), &found);

	if (!found)
	{
		/* First time through ... */
		LWLockInitialize(bundles_in_progress_lock,
						 LWTRANCHE_YB_QUERY_DIAGNOSTICS);
	}

	bundles_in_progress = ShmemInitHash("YbQueryDiagnostics shared hash table",
										max_bundles_in_progress,
										max_bundles_in_progress,
										&ctl,
										HASH_ELEM | HASH_BLOBS);

	bundles_completed =
		(YbQueryDiagnosticsBundles *) ShmemInitStruct("YbQueryDiagnostics Status",
													  YbQueryDiagnosticsBundlesShmemSize(),
													  &found);

	if (!found)
	{
		/* First time through ... */
		bundles_completed->index = 0;
		bundles_completed->max_entries = CircularBufferMaxEntries();

		MemSet(bundles_completed->bundles, 0, sizeof(YbBundleInfo) * bundles_completed->max_entries);

		LWLockInitialize(&bundles_completed->lock,
						 LWTRANCHE_YB_QUERY_DIAGNOSTICS_CIRCULAR_BUFFER);

		YbRestoreQueryDiagnosticsStatusView();
	}

	yb_pgss_last_reset_time = (TimestampTz *) ShmemAlloc(sizeof(TimestampTz));
	(*yb_pgss_last_reset_time) = 0;

	/* Initialize the background worker handle */
	bg_worker_handle = (YbBackgroundWorkerHandle *) ShmemAlloc(sizeof(YbBackgroundWorkerHandle));
	bg_worker_handle->slot = -1;
	bg_worker_handle->generation = -1;

	/* Initialize the database connection worker info */
	database_connection_worker_info = (YbDatabaseConnectionWorkerInfo *)
		ShmemAlloc(sizeof(YbDatabaseConnectionWorkerInfo));
	database_connection_worker_info->initialized = false;

	/* Initialize bg_worker_should_be_active */
	bg_worker_should_be_active = (bool *) ShmemAlloc(sizeof(bool));

	/*
	 * Initialize background worker as inactive until
	 * explicitly triggered by a diagnostics request.
	 */
	(*bg_worker_should_be_active) = false;

	if (!IsUnderPostmaster)
		on_shmem_exit(YbSaveQueryDiagnosticsStatusView, (Datum) 0);
}

static inline int
CircularBufferMaxEntries(void)
{
	return yb_query_diagnostics_circular_buffer_size * 1024 / sizeof(YbBundleInfo);
}

/*
 * InsertBundlesIntoView
 * 		Add a query diagnostics entry to the circular buffer.
 */
static void
InsertBundlesIntoView(const YbQueryDiagnosticsMetadata *metadata,
					  YbQueryDiagnosticsStatusType status, const char *description)
{
	YbBundleInfo *sample;

	LWLockAcquire(&bundles_completed->lock, LW_EXCLUSIVE);

	sample = &bundles_completed->bundles[bundles_completed->index];
	sample->status = status;
	memcpy(&sample->metadata, metadata, sizeof(YbQueryDiagnosticsMetadata));
	memcpy(sample->description, description, strlen(description));

	/* Advance the index, wrapping around if necessary */
	if (++bundles_completed->index == bundles_completed->max_entries)
		bundles_completed->index = 0;

	LWLockRelease(&bundles_completed->lock);
}

static void
CreateJsonbInt(JsonbParseState *state, char *key, int64 value)
{
	JsonbValue	json_key;
	JsonbValue	json_value;

	json_key.type = jbvString;
	json_key.val.string.len = strlen(key);
	json_key.val.string.val = key;

	json_value.type = jbvNumeric;
	json_value.val.numeric = DatumGetNumeric(DirectFunctionCall1(int8_numeric, value));

	pushJsonbValue(&state, WJB_KEY, &json_key);
	pushJsonbValue(&state, WJB_VALUE, &json_value);
}

static void
CreateJsonbBool(JsonbParseState *state, char *key, bool value)
{
	JsonbValue	json_key;
	JsonbValue	json_value;

	json_key.type = jbvString;
	json_key.val.string.len = strlen(key);
	json_key.val.string.val = key;

	json_value.type = jbvBool;
	json_value.val.boolean = value;

	pushJsonbValue(&state, WJB_KEY, &json_key);
	pushJsonbValue(&state, WJB_VALUE, &json_value);
}

/*
 * CreateJsonb
 * 		Create a JSONB representation of the explain parameters given as input
 * 		while starting query diagnostics.
 */
static Datum
CreateJsonb(const YbQueryDiagnosticsParams *params)
{
	JsonbParseState *state = NULL;
	JsonbValue *result;

	Assert(params != NULL);

	pushJsonbValue(&state, WJB_BEGIN_OBJECT, NULL);

	CreateJsonbInt(state, "explain_sample_rate", params->explain_sample_rate);
	CreateJsonbBool(state, "explain_analyze", params->explain_analyze);
	CreateJsonbBool(state, "explain_dist", params->explain_dist);
	CreateJsonbBool(state, "explain_debug", params->explain_debug);

	result = pushJsonbValue(&state, WJB_END_OBJECT, NULL);

	PG_RETURN_POINTER(JsonbValueToJsonb(result));
}

static char *
StatusToString(YbQueryDiagnosticsStatusType status)
{
	switch (status)
	{
		case YB_DIAGNOSTICS_SUCCESS:
			return "Success";

		case YB_DIAGNOSTICS_IN_PROGRESS:
			return "In Progress";

		case YB_DIAGNOSTICS_ERROR:
			return "Error";

		case YB_DIAGNOSTICS_CANCELLED:
			return "Cancelled";

		case YB_DIAGNOSTICS_POSTMASTER_SHUTDOWN:
			return "Postmaster Shutdown";
	}

	/* alma8-gcc11-fastdebug fails to compile without a return statement */
	Assert(false);
	return NULL;				/* Should never reach here. */
}

static void
YbSaveQueryDiagnosticsStatusView(int code, Datum arg)
{
	/*
	 * Completed query diagnostics bundles are within bundles_completed circular buffer and
	 * bundles which are still in progress are in bundles_in_progress hash table.
	 * We add the bundles in progress also into bundles_completed and dump it to a file.
	 */
	HASH_SEQ_STATUS status;
	YbQueryDiagnosticsEntry *entry;
	char	   *status_view_tmp_file_path = psprintf("%s/%s/%s", DataDir, "query-diagnostics",
													 status_view_tmp_file_name);
	char	   *status_view_file_path = psprintf("%s/%s/%s", DataDir, "query-diagnostics",
												 status_view_file_name);

	LWLockAcquire(bundles_in_progress_lock, LW_SHARED);

	hash_seq_init(&status, bundles_in_progress);

	while ((entry = hash_seq_search(&status)) != NULL)
		InsertBundlesIntoView(&entry->metadata, YB_DIAGNOSTICS_POSTMASTER_SHUTDOWN, "");

	LWLockRelease(bundles_in_progress_lock);

	yb_write_struct_to_file(status_view_tmp_file_path, status_view_file_path,
							bundles_completed, YbQueryDiagnosticsBundlesShmemSize(),
							status_view_file_format_id);
	pfree(status_view_tmp_file_path);
	pfree(status_view_file_path);
}

static void
YbRestoreQueryDiagnosticsStatusView()
{
	char	   *status_view_file_path = psprintf("%s/%s/%s", DataDir, "query-diagnostics",
												 status_view_file_name);

	yb_read_struct_from_file(status_view_file_path,
							 bundles_completed, YbQueryDiagnosticsBundlesShmemSize(),
							 status_view_file_format_id);

	pfree(status_view_file_path);
}

/*
 * yb_get_query_diagnostics_status
 *		This function returns a set of rows containing information about active, successful and
 *		errored out query diagnostic bundles.
 *		It's designed to be displayed as yb_query_diagnostics_status view.
 */
Datum
yb_get_query_diagnostics_status(PG_FUNCTION_ARGS)
{
	TupleDesc	tupdesc;
	Tuplestorestate *tupstore;
	ReturnSetInfo *rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
	MemoryContext per_query_ctx;
	MemoryContext oldcontext;

	/* Ensure that query diagnostics is enabled */
	if (!yb_enable_query_diagnostics)
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("ysql_yb_enable_query_diagnostics gflag must be true")));

	/* check to see if caller supports us returning a tuplestore */
	if (rsinfo == NULL || !IsA(rsinfo, ReturnSetInfo))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("set-valued function called in context that cannot accept a set")));

	if (!(rsinfo->allowedModes & SFRM_Materialize))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("materialize mode required, but it is not " \
						"allowed in this context")));

	/* Switch context to construct returned data structures */
	per_query_ctx = rsinfo->econtext->ecxt_per_query_memory;
	oldcontext = MemoryContextSwitchTo(per_query_ctx);

	/* Build a tuple descriptor */
	if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
		ereport(ERROR,
				(errmsg_internal("return type must be a row type")));

	tupstore = tuplestore_begin_heap(true, false, work_mem);
	rsinfo->returnMode = SFRM_Materialize;
	rsinfo->setResult = tupstore;
	rsinfo->setDesc = tupdesc;

	MemoryContextSwitchTo(oldcontext);

	ProcessActiveBundles(tupstore, tupdesc);
	ProcessCompletedBundles(tupstore, tupdesc);

	/* clean up and return the tuplestore */
	tuplestore_donestoring(tupstore);

	return (Datum) 0;
}

static void
OutputBundle(const YbQueryDiagnosticsMetadata metadata, const char *description,
			 const char *status, Tuplestorestate *tupstore, TupleDesc tupdesc)
{
	/* Arrays to hold the values and null flags for each column in a row */
	Datum		values[YB_QUERY_DIAGNOSTICS_STATUS_COLS];
	bool		nulls[YB_QUERY_DIAGNOSTICS_STATUS_COLS];
	int			j = 0;

	MemSet(values, 0, sizeof(values));
	MemSet(nulls, 0, sizeof(nulls));

	values[j++] = CStringGetTextDatum(status);
	values[j++] = CStringGetTextDatum(description);
	values[j++] = Int64GetDatum(metadata.params.query_id);
	values[j++] = TimestampTzGetDatum(metadata.start_time);
	values[j++] = Int64GetDatum(metadata.params.diagnostics_interval_sec);
	values[j++] = Int64GetDatum(metadata.params.bind_var_query_min_duration_ms);
	values[j++] = CreateJsonb(&metadata.params);
	values[j++] = CStringGetTextDatum(metadata.path);

	tuplestore_putvalues(tupstore, tupdesc, values, nulls);
}

/*
 * ProcessActiveBundles
 *		Process and store information about active query diagnostics bundles
 *
 * This function iterates through the shared hash table, retrieves the information for each entry,
 * and stores it in the tuplestore in proper format.
 */
static void
ProcessActiveBundles(Tuplestorestate *tupstore, TupleDesc tupdesc)
{
	HASH_SEQ_STATUS status;
	YbQueryDiagnosticsEntry *entry;

	LWLockAcquire(bundles_in_progress_lock, LW_SHARED);

	hash_seq_init(&status, bundles_in_progress);

	while ((entry = hash_seq_search(&status)) != NULL)
		OutputBundle(entry->metadata, "",
					 StatusToString(YB_DIAGNOSTICS_IN_PROGRESS), tupstore, tupdesc);

	LWLockRelease(bundles_in_progress_lock);
}

/*
 * ProcessCompletedBundles
 *		Process and store information about successful and errored out query diagnostic bundles
 *
 * This function iterates through the circular buffer of query diagnostic bundles,
 * formats the information for each valid entry, and stores it in the tuplestore.
 */
static void
ProcessCompletedBundles(Tuplestorestate *tupstore, TupleDesc tupdesc)
{
	LWLockAcquire(&bundles_completed->lock, LW_SHARED);

	for (int i = 0; i < bundles_completed->max_entries; ++i)
	{
		YbBundleInfo *sample = &bundles_completed->bundles[i];

		if (sample->metadata.params.query_id != 0)
			OutputBundle(sample->metadata, sample->description,
						 StatusToString(sample->status), tupstore, tupdesc);
	}

	LWLockRelease(&bundles_completed->lock);
}

/*
 * BgWorkerRegister
 *		Dynamically register the background worker.
 *
 * Background worker is required to periodically check for expired entries
 * within the shared hash table and stop the query diagnostics for them.
 */
static void
BgWorkerRegister(void)
{
	pid_t		pid;
	BackgroundWorker worker;
	BackgroundWorkerHandle *handle;
	BgwHandleStatus status;

	MemSet(&worker, 0, sizeof(worker));
	sprintf(worker.bgw_name, "yb_query_diagnostics bgworker");
	sprintf(worker.bgw_type, "yb_query_diagnostics bgworker");
	worker.bgw_flags = BGWORKER_SHMEM_ACCESS |
		BGWORKER_BACKEND_DATABASE_CONNECTION;
	worker.bgw_start_time = BgWorkerStart_RecoveryFinished;
	worker.bgw_restart_time = BGW_DEFAULT_RESTART_INTERVAL;
	sprintf(worker.bgw_library_name, "postgres");
	sprintf(worker.bgw_function_name, "YbQueryDiagnosticsMain");
	worker.bgw_main_arg = (Datum) 0;
	/* set bgw_notify_pid so that we can use WaitForBackgroundWorkerStartup */
	worker.bgw_notify_pid = MyProcPid;
	if (!RegisterDynamicBackgroundWorker(&worker, &handle))
		ereport(WARNING,
				(errcode(ERRCODE_CONFIGURATION_LIMIT_EXCEEDED),
				 errmsg("due to being out of background worker slots, "
						"yb_query_diagnostics bgworker cannot be started"),
				 errhint("You might need to increase max_worker_processes")));

	status = WaitForBackgroundWorkerStartup(handle, &pid);

	if (status == BGWH_STOPPED)
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_RESOURCES),
				 errmsg("yb_query_diagnostics bgworker cannot be started"),
				 errhint("More details may be available in the server log")));

	if (status == BGWH_POSTMASTER_DIED)
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_RESOURCES),
				 errmsg("yb_query_diagnostics bgworker cannot be started without postmaster"),
				 errhint("Kill all remaining database processes and restart the database")));

	Assert(status == BGWH_STARTED);

	/* Save the handle for future use */
	memcpy(bg_worker_handle, handle, sizeof(YbBackgroundWorkerHandle));

	/* Set the bg_worker_should_be_active to true */
	(*bg_worker_should_be_active) = true;
}

/*
 * RegisterDatabaseConnectionBgWorker
 *		Register the background worker for schema details
 *
 * A separate bg worker is required because the database with which we need to initialize the bg worker
 * can be different for each bundle.
 */
static void
RegisterDatabaseConnectionBgWorker(const YbQueryDiagnosticsEntry *entry)
{
	Assert(database_connection_worker_info != NULL && !database_connection_worker_info->initialized);

	ereport(DEBUG1,
			(errmsg("registering background worker to dump schema details")));

	database_connection_worker_info->initialized = true;

	BackgroundWorker worker;
	BackgroundWorkerHandle *handle;

	/* Copy data to shared memory */
	memcpy(&database_connection_worker_info->entry, entry, sizeof(YbQueryDiagnosticsEntry));

	/* Initialize worker struct */
	memset(&worker, 0, sizeof(worker));
	sprintf(worker.bgw_type, "yb_query_diagnostics database connection bgworker");
	sprintf(worker.bgw_name, "yb_query_diagnostics database connection bgworker");
	worker.bgw_flags = BGWORKER_SHMEM_ACCESS |
		BGWORKER_BACKEND_DATABASE_CONNECTION;
	worker.bgw_start_time = BgWorkerStart_ConsistentState;
	worker.bgw_restart_time = BGW_NEVER_RESTART;
	sprintf(worker.bgw_library_name, "postgres");
	sprintf(worker.bgw_function_name, "YbQueryDiagnosticsDatabaseConnectionWorkerMain");
	worker.bgw_main_arg = (Datum) 0;
	worker.bgw_notify_pid = MyProcPid;

	/* Register the worker */
	if (!RegisterDynamicBackgroundWorker(&worker, &handle))
		ereport(LOG,
				(errcode(ERRCODE_INSUFFICIENT_RESOURCES),
				 errmsg("could not register query diagnostics database connection worker")));
}

/*
 * YbSetPgssNormalizedQueryText
 *    This function updates the hash table with the offset and length of the normalized query text.
 *	  The normalized query text is generated by pg_stat_statements.c and stored in the
 *	  pgss_query_texts.stat file. This metadata allows retrieval of query text while dumping to disk.
 */
void
YbSetPgssNormalizedQueryText(int64 query_id, const Size query_offset, int query_len)
{
	YbQueryDiagnosticsEntry *entry;

	LWLockAcquire(bundles_in_progress_lock, LW_SHARED);

	/*
	 * This can slow down the query execution, even if the query is not being bundled.
	 * Worst case : O(max_bundles_in_progress)
	 */
	entry = (YbQueryDiagnosticsEntry *) hash_search(bundles_in_progress,
													&query_id, HASH_FIND,
													NULL);

	if (entry)
	{
		SpinLockAcquire(&entry->mutex);
		entry->pgss.query_offset = query_offset;
		entry->pgss.query_len = query_len;
		SpinLockRelease(&entry->mutex);
	}

	LWLockRelease(bundles_in_progress_lock);
}

/*
 * FormatBindVariables
 *		Iterates over params and prints all of the bind variables in CSV fromat.
 */
static void
FormatBindVariables(StringInfo buf, const ParamListInfo params)
{
	Oid			typoutput;
	bool		typIsVarlena;
	char	   *val;
	bool		is_text;

	MemoryContext oldcxt = CurrentMemoryContext;
	MemoryContext cxt = AllocSetContextCreate(CurrentMemoryContext,
											  "FormatBindVariables temporary context",
											  ALLOCSET_DEFAULT_SIZES);

	MemoryContextSwitchTo(cxt);
	for (int i = 0; i < params->numParams; ++i)
	{
		if (params->params[i].isnull)
			appendStringInfo(buf, "NULL");
		else
		{
			getTypeOutputInfo(params->params[i].ptype,
							  &typoutput, &typIsVarlena);
			val = OidOutputFunctionCall(typoutput, params->params[i].value);

			/* Check if the type is text-like (char, varchar, text) */
			is_text = (params->params[i].ptype == TEXTOID ||
					   params->params[i].ptype == VARCHAROID ||
					   params->params[i].ptype == BPCHAROID);

			if (i > 0)
				appendStringInfoChar(buf, ',');

			/* Append the value with quotes if it is a text-like type */
			appendStringInfo(buf, is_text ? "'%s'" : "%s", val);

			pfree(val);
		}
	}

	MemoryContextSwitchTo(oldcxt);
	MemoryContextDelete(cxt);
}

/*
 * FormatConstants
 *		Formats and copies the constants from the query into the constants buffer.
 */
static void
FormatConstants(StringInfo constants, const char *query, int constants_count,
				int query_location, LocationLen *constant_locations)
{
	int			i;

	/* Offset from the start of the query string for the current constant */
	int			offset;

	/* Length (in bytes) of the current constant */
	int			constant_len;

	for (i = 0; i < constants_count; i++)
	{
		offset = constant_locations[i].location;
		constant_len = constant_locations[i].length;

		if (constant_len < 0)
			continue;

		/* Adjust recorded location if we're dealing with partial string */
		offset -= query_location;

		/* Copy the constant */
		if (i > 0)
			appendStringInfoChar(constants, ',');

		appendBinaryStringInfo(constants, query + offset, constant_len);
	}
}

static void
YbQueryDiagnostics_post_parse_analyze(ParseState *pstate, Query *query, JumbleState *jstate)
{
	if (prev_post_parse_analyze_hook)
		prev_post_parse_analyze_hook(pstate, query, jstate);

	int64		query_id = query->queryId;
	YbQueryDiagnosticsEntry *entry;

	LWLockAcquire(bundles_in_progress_lock, LW_SHARED);

	/*
	 * This can slow down the query execution, even if the query is not being bundled.
	 */
	entry = (YbQueryDiagnosticsEntry *) hash_search(bundles_in_progress,
													&query_id, HASH_FIND,
													NULL);
	if (entry)
	{
		if (jstate && jstate->clocations_count > 0)
		{
			int			query_location = query->stmt_location;

			/*
			 * Get constants' lengths (Postgres infrastructure only gives us locations).
			 * Note this also ensures the items are sorted by location.
			 */
			yb_qd_fill_in_constant_lengths(jstate, pstate->p_sourcetext, query_location);

			query_constants.stmt_location = query_location;
			query_constants.count = Min(jstate->clocations_count, YB_QD_MAX_CONSTANTS);
			memcpy(query_constants.locations, jstate->clocations,
				   sizeof(LocationLen) * query_constants.count);
		}
	}

	LWLockRelease(bundles_in_progress_lock);
}

static void
YbQueryDiagnostics_ExecutorStart(QueryDesc *queryDesc, int eflags)
{
	int64		query_id = queryDesc->plannedstmt->queryId;
	YbQueryDiagnosticsEntry *entry;

	LWLockAcquire(bundles_in_progress_lock, LW_SHARED);

	/*
	 * This can slow down the query execution, even if the query is not being bundled.
	 */
	entry = (YbQueryDiagnosticsEntry *) hash_search(bundles_in_progress,
													&query_id, HASH_FIND,
													NULL);

	if (entry)
		current_query_sampled = (pg_prng_double(&pg_global_prng_state) <
								 entry->metadata.params.explain_sample_rate / 100.0);
	else
		current_query_sampled = false;

	/* Enable per-node instrumentation iff explain_analyze is required. */
	if (current_query_sampled &&
		(entry->metadata.params.explain_analyze && (eflags & EXEC_FLAG_EXPLAIN_ONLY) == 0))
		queryDesc->instrument_options |= INSTRUMENT_ALL;

	LWLockRelease(bundles_in_progress_lock);

	if (prev_ExecutorStart)
		prev_ExecutorStart(queryDesc, eflags);
	else
		standard_ExecutorStart(queryDesc, eflags);
}

static void
YbQueryDiagnostics_ExecutorEnd(QueryDesc *queryDesc)
{
	int64		query_id = queryDesc->plannedstmt->queryId;
	YbQueryDiagnosticsEntry *entry;

	LWLockAcquire(bundles_in_progress_lock, LW_SHARED);

	/*
	 * This can slow down the query execution, even if the query is not being bundled.
	 * Worst case : O(max_bundles_in_progress)
	 */
	entry = (YbQueryDiagnosticsEntry *) hash_search(bundles_in_progress,
													&query_id, HASH_FIND,
													NULL);

	if (entry)
	{
		/*
		 * Make sure stats accumulation is done.  (Note: it's okay if several
		 * levels of hook all do this.)
		 */
		InstrEndLoop(queryDesc->totaltime);
		double		totaltime_ms = queryDesc->totaltime->total * 1000.0;

		if (entry->metadata.params.bind_var_query_min_duration_ms <= totaltime_ms &&
			(queryDesc->params || query_constants.count > 0))
		{
			StringInfoData buf;

			initStringInfo(&buf);

			/* For prepared statements, format bind variables in a CSV format */
			if (queryDesc->params)
				FormatBindVariables(&buf, queryDesc->params);

			/* For non-prepared statements, format constants in a CSV format. */
			else if (query_constants.count > 0)
			{
				FormatConstants(&buf, queryDesc->sourceText,
								query_constants.count,
								query_constants.stmt_location,
								query_constants.locations);

				/* Reset the constants metadata */
				query_constants.stmt_location = 0;
				query_constants.count = 0;
				MemSet(query_constants.locations, 0, sizeof(LocationLen) * YB_QD_MAX_CONSTANTS);
			}

			if (buf.len > 0)
			{
				appendStringInfo(&buf, ",%lf ms\n", totaltime_ms);
				AccumulateBindVariablesOrConstants(entry, &buf);
			}

			pfree(buf.data);
		}

		if (current_query_sampled)
			AccumulateExplain(queryDesc, entry,
							  entry->metadata.params.explain_analyze,
							  entry->metadata.params.explain_dist, totaltime_ms);

		/* Fetch schema details only if it is not already fetched */
		if (entry->schema_oids[0] == InvalidOid)
			FetchSchemaOids(queryDesc->plannedstmt->rtable,
							entry->schema_oids);
	}

	LWLockRelease(bundles_in_progress_lock);

	if (prev_ExecutorEnd)
		prev_ExecutorEnd(queryDesc);
	else
		standard_ExecutorEnd(queryDesc);
}

/*
 * AccumulateBindVariablesOrConstants
 *		Appends current query's bind variables/constants to the entry.
 */
static void
AccumulateBindVariablesOrConstants(YbQueryDiagnosticsEntry *entry, StringInfo params)
{
	SpinLockAcquire(&entry->mutex);

	size_t		current_len = strlen(entry->bind_vars);

	if (current_len + params->len <= YB_QD_MAX_BIND_VARS_LEN - 1)
	{
		memcpy(entry->bind_vars + current_len, params->data, params->len);
		entry->bind_vars[current_len + params->len] = '\0'; /* Ensure null
															 * termination */
	}

	SpinLockRelease(&entry->mutex);
}

void
YbQueryDiagnosticsAccumulatePgss(int64 query_id, YbQdPgssStoreKind kind,
								 double total_time, uint64 rows,
								 const BufferUsage *bufusage,
								 const WalUsage *walusage,
								 const struct JitInstrumentation *jitusage)
{
	YbQueryDiagnosticsEntry *entry;

	LWLockAcquire(bundles_in_progress_lock, LW_SHARED);
	/*
	 * This can slow down the query execution, even if the query is not being bundled.
	 * Worst case : O(max_bundles_in_progress)
	 */
	entry = (YbQueryDiagnosticsEntry *) hash_search(bundles_in_progress,
													&query_id, HASH_FIND,
													NULL);

	if (entry)
	{
		SpinLockAcquire(&entry->mutex);
		entry->pgss.counters.calls[kind] += 1;
		entry->pgss.counters.total_time[kind] += total_time;
		entry->pgss.counters.rows += rows;

		if (entry->pgss.counters.calls[kind] == 1)
		{
			entry->pgss.counters.min_time[kind] = total_time;
			entry->pgss.counters.max_time[kind] = total_time;
			entry->pgss.counters.mean_time[kind] = total_time;
		}
		else
		{
			double		old_mean = entry->pgss.counters.mean_time[kind];

			/*
			 * 'calls' cannot be 0 here because
			 * it is initialized to 0 and incremented by calls++ above
			 */
			entry->pgss.counters.mean_time[kind] +=
				(total_time - old_mean) / entry->pgss.counters.calls[kind];
			entry->pgss.counters.sum_var_time[kind] +=
				(total_time - old_mean) * (total_time - entry->pgss.counters.mean_time[kind]);
			if (entry->pgss.counters.min_time[kind] > total_time)
				entry->pgss.counters.min_time[kind] = total_time;
			if (entry->pgss.counters.max_time[kind] < total_time)
				entry->pgss.counters.max_time[kind] = total_time;
		}

		entry->pgss.counters.rows += rows;
		entry->pgss.counters.shared_blks_hit += bufusage->shared_blks_hit;
		entry->pgss.counters.shared_blks_read += bufusage->shared_blks_read;
		entry->pgss.counters.shared_blks_dirtied += bufusage->shared_blks_dirtied;
		entry->pgss.counters.shared_blks_written += bufusage->shared_blks_written;
		entry->pgss.counters.local_blks_hit += bufusage->local_blks_hit;
		entry->pgss.counters.local_blks_read += bufusage->local_blks_read;
		entry->pgss.counters.local_blks_dirtied += bufusage->local_blks_dirtied;
		entry->pgss.counters.local_blks_written += bufusage->local_blks_written;
		entry->pgss.counters.temp_blks_read += bufusage->temp_blks_read;
		entry->pgss.counters.temp_blks_written += bufusage->temp_blks_written;
		entry->pgss.counters.blk_read_time += INSTR_TIME_GET_MILLISEC(bufusage->blk_read_time);
		entry->pgss.counters.blk_write_time += INSTR_TIME_GET_MILLISEC(bufusage->blk_write_time);
		entry->pgss.counters.wal_records += walusage->wal_records;
		entry->pgss.counters.wal_fpi += walusage->wal_fpi;
		entry->pgss.counters.wal_bytes += walusage->wal_bytes;

		if (jitusage)
		{
			entry->pgss.counters.jit_functions += jitusage->created_functions;
			entry->pgss.counters.jit_generation_time += INSTR_TIME_GET_MILLISEC(jitusage->generation_counter);

			if (INSTR_TIME_GET_MILLISEC(jitusage->inlining_counter))
				entry->pgss.counters.jit_inlining_count++;
			entry->pgss.counters.jit_inlining_time += INSTR_TIME_GET_MILLISEC(jitusage->inlining_counter);

			if (INSTR_TIME_GET_MILLISEC(jitusage->optimization_counter))
				entry->pgss.counters.jit_optimization_count++;
			entry->pgss.counters.jit_optimization_time += INSTR_TIME_GET_MILLISEC(jitusage->optimization_counter);

			if (INSTR_TIME_GET_MILLISEC(jitusage->emission_counter))
				entry->pgss.counters.jit_emission_count++;
			entry->pgss.counters.jit_emission_time += INSTR_TIME_GET_MILLISEC(jitusage->emission_counter);
		}

		SpinLockRelease(&entry->mutex);
	}

	LWLockRelease(bundles_in_progress_lock);
}

static double
CalculateStandardDeviation(int64 calls, double sum_var_time)
{
	return (calls > 1) ? (sqrt(sum_var_time / calls)) : 0.0;
}


/*
 * PgssToString
 *		Converts the pg_stat_statements data to a CSV string, and stores it in pgss_str.
 */
static void
PgssToString(int64 query_id, char *pgss_str, YbQueryDiagnosticsPgss pgss, const char *query_str)
{
	if (!query_str)
		query_str = "";

	snprintf(pgss_str, YB_QD_MAX_PGSS_LEN,
			 "query_id,query,calls,total_plan_time,total_exec_time,"
			 "min_plan_time,min_exec_time,max_plan_time,max_exec_time,"
			 "mean_plan_time,mean_exec_time,stddev_plan_time,stddev_exec_time,"
			 "rows,shared_blks_hit,shared_blks_read,shared_blks_dirtied,shared_blks_written,"
			 "local_blks_hit,local_blks_read,local_blks_dirtied,local_blks_written,"
			 "temp_blks_read,temp_blks_written,blk_read_time,blk_write_time,"
			 "temp_blk_read_time,temp_blk_write_time,wal_records,wal_fpi,wal_bytes,"
			 "jit_functions,jit_generation_time,jit_inlining_count,jit_inlining_time,"
			 "jit_optimization_count,jit_optimization_time,jit_emission_count,jit_emission_time\n"
			 "%ld,\"%s\",%ld,%lf,%lf,%lf,%lf,"
			 "%lf,%lf,%lf,%lf,%lf,%lf,"
			 "%ld,%ld,%ld,%ld,%ld,"
			 "%ld,%ld,%ld,%ld,"
			 "%ld,%ld,%lf,%lf,"
			 "%lf,%lf,%ld,%ld,%ld,"
			 "%ld,%lf,%ld,%lf,"
			 "%ld,%lf,%ld,%lf\n",
			 query_id, query_str,
			 pgss.counters.calls[YB_QD_PGSS_EXEC],
			 pgss.counters.total_time[YB_QD_PGSS_PLAN],
			 pgss.counters.total_time[YB_QD_PGSS_EXEC],
			 pgss.counters.min_time[YB_QD_PGSS_PLAN],
			 pgss.counters.min_time[YB_QD_PGSS_EXEC],
			 pgss.counters.max_time[YB_QD_PGSS_PLAN],
			 pgss.counters.max_time[YB_QD_PGSS_EXEC],
			 pgss.counters.mean_time[YB_QD_PGSS_PLAN],
			 pgss.counters.mean_time[YB_QD_PGSS_EXEC],
			 CalculateStandardDeviation(pgss.counters.calls[YB_QD_PGSS_PLAN],
										pgss.counters.sum_var_time[YB_QD_PGSS_PLAN]),
			 CalculateStandardDeviation(pgss.counters.calls[YB_QD_PGSS_EXEC],
										pgss.counters.sum_var_time[YB_QD_PGSS_EXEC]),
			 pgss.counters.rows,
			 pgss.counters.shared_blks_hit,
			 pgss.counters.shared_blks_read,
			 pgss.counters.shared_blks_dirtied,
			 pgss.counters.shared_blks_written,
			 pgss.counters.local_blks_hit,
			 pgss.counters.local_blks_read,
			 pgss.counters.local_blks_dirtied,
			 pgss.counters.local_blks_written,
			 pgss.counters.temp_blks_read,
			 pgss.counters.temp_blks_written,
			 pgss.counters.blk_read_time,
			 pgss.counters.blk_write_time,
			 pgss.counters.temp_blk_read_time,
			 pgss.counters.temp_blk_write_time,
			 pgss.counters.wal_records,
			 pgss.counters.wal_fpi,
			 pgss.counters.wal_bytes,
			 pgss.counters.jit_functions,
			 pgss.counters.jit_generation_time,
			 pgss.counters.jit_inlining_count,
			 pgss.counters.jit_inlining_time,
			 pgss.counters.jit_optimization_count,
			 pgss.counters.jit_optimization_time,
			 pgss.counters.jit_emission_count,
			 pgss.counters.jit_emission_time);
}

static void
AccumulateExplain(QueryDesc *queryDesc, YbQueryDiagnosticsEntry *entry, bool explain_analyze,
				  bool explain_dist, double totaltime_ms)
{
	ExplainState *es = NewExplainState();

	es->analyze = (queryDesc->instrument_options && explain_analyze);
	es->verbose = false;
	es->buffers = es->analyze;
	es->timing = es->analyze;
	es->summary = es->analyze;
	es->format = EXPLAIN_FORMAT_JSON;
	es->rpc = (es->analyze && explain_dist);

	/* Produce JSON-formatted explain output */
	ExplainBeginOutput(es);
	ExplainPrintPlan(es, queryDesc);
	if (es->analyze)
		ExplainPrintTriggers(es, queryDesc);
	if (es->costs)
		ExplainPrintJITSummary(es, queryDesc);
	ExplainPropertyFloat("Execution Time", "ms", totaltime_ms, 3, es);
	ExplainEndOutput(es);

	/*
	 * Removes the trailing newline that might be introduced by
	 * ExplainPrintJITSummary
	 */
	if (es->str->len > 0 && es->str->data[es->str->len - 1] == '\n')
		es->str->data[--es->str->len] = '\0';

	/* Fix JSON to output an object */
	es->str->data[0] = '{';
	es->str->data[es->str->len - 1] = '}';

	SpinLockAcquire(&entry->mutex);

	/* TODO(GH#23720): Add support for handling oversized explain plans */
	int			remaining_space = sizeof(entry->explain_plan) - strlen(entry->explain_plan) - 1;

	if (remaining_space > 0)
		snprintf(entry->explain_plan + strlen(entry->explain_plan), remaining_space,
				"%s\n", es->str->data);

	SpinLockRelease(&entry->mutex);

	pfree(es->str->data);
	pfree(es->str);
	pfree(es);
}

static bool
IsBgWorkerStopped()
{
	pid_t		pid;

	return (GetBackgroundWorkerPid((BackgroundWorkerHandle *) bg_worker_handle,
								   &pid) != BGWH_STARTED);
}

/*
 * StartBgWorkerIfStopped()
 *		Starts the background worker if it is not already running.
 *
 * This function manages the background worker lifecycle with the following logic:
 * 1.	If no worker has ever been initialized (invalid slot and generation),
 *		registers a new worker immediately.
 * 2.	If a worker is marked as inactive and not running, registers a new worker.
 * 3.	If a worker is marked as inactive but still running (in termination process),
 *		waits for the worker to fully terminate before registering a new one.
 *		If the wait exceeds the timeout (5 seconds), raises an ERROR to prevent
 *		indefinite waiting.
 */
static void
StartBgWorkerIfStopped()
{
	int			max_attempts = 50;
	int			attempts = 0;

	/* Worker was never initialized (invalid slot and generation) */
	if (bg_worker_handle->slot == -1 && bg_worker_handle->generation == -1)
		BgWorkerRegister();

	/* Worker was initialized but not currently running */
	else if ((*bg_worker_should_be_active) == false)
	{
		while (attempts < max_attempts)
		{
			if (IsBgWorkerStopped())
			{
				BgWorkerRegister();
				break;
			}

			pg_usleep(100000);	/* Sleep for 100ms */
			attempts++;
		}

		if (attempts >= max_attempts)
			ereport(ERROR,
					(errcode(ERRCODE_INSUFFICIENT_RESOURCES),
					 errmsg("timed out after waiting 5 seconds for "
							"query diagnostics background worker to terminate")));
	}
}

/*
 * InsertNewBundleInfo
 *		Adds the entry into bundles_in_progress hash table.
 *		Entry is inserted only if it is not already present,
 *		otherwise an error is raised. This function also starts
 *		yb_query_diagnostics bgworker if it is not already running.
 */
static void
InsertNewBundleInfo(YbQueryDiagnosticsMetadata *metadata)
{
	int64		key = metadata->params.query_id;
	bool		found;
	YbQueryDiagnosticsEntry *entry;

	LWLockAcquire(bundles_in_progress_lock, LW_EXCLUSIVE);

	/*
	 * Note that we need not worry about concurrent registration attempts
	 * from different sessions as we are within an EXCLUSIVE lock.
	 */
	StartBgWorkerIfStopped();

	entry = (YbQueryDiagnosticsEntry *) hash_search(bundles_in_progress, &key,
													HASH_ENTER, &found);

	if (!found)
	{
		entry->metadata = *metadata;
		entry->metadata.directory_created = false;
		entry->metadata.flush_only_schema_details = false;
		MemSet(entry->bind_vars, 0, YB_QD_MAX_BIND_VARS_LEN);
		MemSet(entry->explain_plan, 0, YB_QD_MAX_EXPLAIN_PLAN_LEN);
		MemSet(entry->schema_oids, 0, sizeof(Oid) * YB_QD_MAX_SCHEMA_OIDS);
		SpinLockInit(&entry->mutex);
		entry->pgss = (YbQueryDiagnosticsPgss)
		{
			.counters =
			{
				{
					0
				}
			},
				.query_offset = 0,
				.query_len = 0,
		};
	}

	LWLockRelease(bundles_in_progress_lock);

	if (found)
		ereport(ERROR,
				(errcode(ERRCODE_DUPLICATE_OBJECT),
				 errmsg("query diagnostics for %ld is already being generated",
						metadata->params.query_id)));
}

/*
 * BundleEndTime
 * 		Returns the time when the given bundle will expire.
 * 		note: since TimestampTz is equivalent to microsecond,
 * 		diagnostics_interval is converted to microseconds before adding to start_time.
 */
static inline TimestampTz
BundleEndTime(const YbQueryDiagnosticsEntry *entry)
{
	return (entry->metadata.start_time +
			(entry->metadata.params.diagnostics_interval_sec * USECS_PER_SEC));
}

void
YbQueryDiagnosticsAppendToDescription(char *description, const char *format,...)
{
	int			current_len = strlen(description);
	va_list		args;
	int			remaining_len = YB_QD_DESCRIPTION_LEN - current_len - 1;	/* -1 for '\0' */

	if (remaining_len <= 0)
		return;

	va_start(args, format);
	vsnprintf(description + current_len, remaining_len, format, args);
	va_end(args);
}

/*
 * FlushAndCleanBundles
 * 		This function is invoked every yb_query_diagnostics_bg_worker_interval_ms and is
 * 		responsible for cleaning up expired query diagnostic bundles and flushing their
 * 		data to disk.
 *
 * The function performs the following tasks:
 * 1. Scans the hash table of in-progress bundles
 * 2. For expired bundles:
 *    - Dumps data to disk
 *    - Removes the bundle from the in-progress table
 * 3. For non-expired bundles:
 *    - Performs intermediate flushing of explain plans and bind variables if they
 *      are filled more than 50%
 * Note:
 * It's safe to acquire the entry-level lock here without holding the hash table lock.
 * This is because only this background worker is responsible for removing entries,
 * so we can be certain that the entry won't be removed while we're accessing it.
 * Other threads may still modify the entry's contents, which is why we need entry level lock.
 */
static void
FlushAndCleanBundles()
{
	/* TODO(GH#22447): Do this in O(1) */
	TimestampTz current_time = GetCurrentTimestamp();
	HASH_SEQ_STATUS status;
	YbQueryDiagnosticsEntry *entry;
	MemoryContext curr_context = CurrentMemoryContext;
	int			expired_entries_index = 0;
	YbQueryDiagnosticsEntry *expired_entries;

	expired_entries = (YbQueryDiagnosticsEntry *)
		palloc0(sizeof(YbQueryDiagnosticsEntry) *
				max_bundles_in_progress);

	LWLockAcquire(bundles_in_progress_lock, LW_SHARED);

	/* Initialize the hash table scan */
	hash_seq_init(&status, bundles_in_progress);

	/*
	 * Scan the hash table for expired entries and store them in an array.
	 * This approach is necessary to prevent holding the hash_seq_search() open
	 * while calling CommitTransactionCommand(), which can lead to resource leaks
	 * and potential inconsistencies in the hash table state.
	 */
	while ((entry = hash_seq_search(&status)) != NULL)
	{
		/* Release the shared lock before flushing to disk */
		LWLockRelease(bundles_in_progress_lock);

		/*
		 * It is fine to read entry's metadata without holding the entry level lock
		 * because that is set only once, when the bundle is created.
		 */
		char	   *folder_path = entry->metadata.path;
		YbQueryDiagnosticsStatusType status = YB_DIAGNOSTICS_SUCCESS;
		bool		has_expired = current_time >= BundleEndTime(entry);

		/* Description of the warnings/errors that occurred */
		char		description[YB_QD_DESCRIPTION_LEN];

		description[0] = '\0';

		/*
		 * Since only the background worker accesses this variable, no lock is
		 * needed
		 */
		if (!entry->metadata.directory_created)
		{
			/* Creates the directory structure recursively for this bundle */
			if (MakeDirectory(folder_path, description) == YB_DIAGNOSTICS_ERROR)
			{
				FinishBundleProcessing(&entry->metadata, YB_DIAGNOSTICS_ERROR, description);
				goto end_of_loop;
			}

			entry->metadata.directory_created = true;
		}

		if (has_expired)
		{
			SpinLockAcquire(&entry->mutex);
			/*
			 * To avoid holding the lock while flushing to disk, we create a copy of the data
			 * that is to be dumped, this protects us from potential overwriting of the entry
			 * during the flushing process.
			 */
			memcpy(&expired_entries[expired_entries_index++], entry, sizeof(YbQueryDiagnosticsEntry));
			SpinLockRelease(&entry->mutex);
		}
		else
		{
			/*
			 * Bundle has not expired, perform intermediate flushing if necessary.
			 *
			 * We dump the explain plan and bind variables if they are filled more than
			 * 50% of the maximum allowed size. This is done to ensure no data is lost
			 * while flushing to disk.
			 */
			status = DumpBufferIfHalfFull(entry->bind_vars, YB_QD_MAX_BIND_VARS_LEN,
										  constants_and_bind_variables_file, folder_path, description,
										  &entry->mutex);

			if (status == YB_DIAGNOSTICS_ERROR)
			{
				/* Expire the bundle as we are not able to dump */
				FinishBundleProcessing(&entry->metadata, status, description);
				goto end_of_loop;
			}

			status = DumpBufferIfHalfFull(entry->explain_plan, YB_QD_MAX_EXPLAIN_PLAN_LEN,
										  explain_plan_file, folder_path, description,
										  &entry->mutex);

			if (status == YB_DIAGNOSTICS_ERROR)
			{
				/* Expire the bundle as we are not able to dump */
				FinishBundleProcessing(&entry->metadata, status, description);
				goto end_of_loop;
			}
		}

end_of_loop:
		/* Re-acquire the shared lock before continuing the loop */
		LWLockAcquire(bundles_in_progress_lock, LW_SHARED);
	}
	LWLockRelease(bundles_in_progress_lock);

	/* Handle expired entries */
	for (int i = 0; i < expired_entries_index; i++)
	{
		YbQueryDiagnosticsEntry *entry = &expired_entries[i];

		char		description[YB_QD_DESCRIPTION_LEN];
		YbQueryDiagnosticsStatusType status = YB_DIAGNOSTICS_SUCCESS;
		int			query_len = Min(entry->pgss.query_len, YB_QD_MAX_PGSS_QUERY_LEN);
		char		query_str[query_len + 1];

		description[0] = '\0';
		query_str[0] = '\0';

		/* No queries were executed that needed to be bundled */
		if (entry->pgss.query_len == 0)
		{
			YbQueryDiagnosticsAppendToDescription(description, "No query executed;");
			goto remove_entry;
		}

		if (yb_query_diagnostics_disable_database_connection_bgworker)
			goto skip_schema_details;

		/*
		 * Wait for dumping schema details for the future iterations,
		 * if the database connection bgworker is busy.
		 */
		if (database_connection_worker_info->initialized)
		{
			/*
			 * If we have already dumped ash, pgss, bind_var and explain plans
			 * then wait for bgworker to become idle.
			 */
			if (entry->metadata.flush_only_schema_details)
				continue;

			/*
			 * If we haven't yet dumped ash, pgss, bind_var and explain plans
			 * then do it.
			 */
			entry->metadata.flush_only_schema_details = true;
			goto skip_schema_details;
		}

		/* Gather and dump schema details through a separate bg worker */
		RegisterDatabaseConnectionBgWorker(entry);

		/* If rest of the data is already dumped then we can remove the entry. */
		if (entry->metadata.flush_only_schema_details)
			goto remove_entry;

skip_schema_details:

		/* Dump bind variables */
		status = DumpToFile(entry->metadata.path, constants_and_bind_variables_file,
							entry->bind_vars, description);

		if (status == YB_DIAGNOSTICS_ERROR)
			goto remove_entry;

		/*
		 * Ensure that pg_stat_statements was not reset in the bundle duration
		 *
		 * Note that there are separate checks for pg_stat_statements reset and
		 * pgss_copy.query_len == 0. This is because resetting pgss does not automatically
		 * set pgss_copy.query_len to 0. The query_len is only updated when a new query
		 * is executed. Therefore, even after a pgss reset, pgss_copy.query_len
		 * may still hold a non-zero garbage value until a new query is run
		 * and query_len is updated.
		 */
		if (*yb_pgss_last_reset_time >= entry->metadata.start_time)
			YbQueryDiagnosticsAppendToDescription(description,
												  "pg_stat_statements was reset, "
												  "query string not available;");
		else
		{
			PG_TRY();
			{
				if (yb_get_normalized_query)
				{
					/* Extract query string from pgss_query_texts.stat file */
					status = yb_get_normalized_query(entry->pgss.query_offset,
													 entry->pgss.query_len,
													 query_str);

					if (query_str[0] == '\0')
						YbQueryDiagnosticsAppendToDescription(description,
															  "Error fetching "
															  "pg_stat_statements "
															  "normalized query "
															  "string;");
				}
			}
			PG_CATCH();
			{
				/*
				 * Not setting status = YB_DIAGNOSTICS_ERROR,
				 * so that rest of the data can be printed
				 */
				ErrorData  *edata;

				/* save error info */
				MemoryContextSwitchTo(curr_context);
				edata = CopyErrorData();
				FlushErrorState();

				YbQueryDiagnosticsAppendToDescription(description,
													  "Fetching pg_stat_statements normalized "
													  "query string "
													  "errored out, with the following message: "
													  "%s;",
													  edata->message);

				ereport(LOG,
						(errmsg("error while fetching normalized query string; "
								"%s",
								edata->message)),
						(errhint("%s", edata->hint)));

				FreeErrorData(edata);
			}
			PG_END_TRY();
		}

		/* Convert pg_stat_statements data to string format */
		char		pgss_str[YB_QD_MAX_PGSS_LEN];

		PgssToString(entry->metadata.params.query_id, pgss_str, entry->pgss, query_str);

		/* Dump pg_stat_statements */
		status = DumpToFile(entry->metadata.path, pgss_file, pgss_str, description);

		if (status == YB_DIAGNOSTICS_ERROR)
			goto remove_entry;

		/* Dump explain plan */
		status = DumpToFile(entry->metadata.path, explain_plan_file,
							entry->explain_plan, description);

		if (status == YB_DIAGNOSTICS_ERROR)
			goto remove_entry;

		/* Keep bundle in hash table as dumping schema details is pending */
		if (!yb_query_diagnostics_disable_database_connection_bgworker &&
			entry->metadata.flush_only_schema_details)
			continue;

remove_entry:
		FinishBundleProcessing(&entry->metadata, status, description);
	}

	pfree(expired_entries);
}

static int
DumpSchemaDetails(const YbQueryDiagnosticsEntry *entry, char *description)
{
	StringInfoData schema_details;
	MemoryContext curr_context = CurrentMemoryContext;
	YbQueryDiagnosticsStatusType status = YB_DIAGNOSTICS_SUCCESS;

	initStringInfo(&schema_details);

	PG_TRY();
	{
		/* Fetch schema details for each schema oid */
		for (int i = 0; i < YB_QD_MAX_SCHEMA_OIDS; i++)
		{
			if (entry->schema_oids[i] == InvalidOid)
				break;

			status = DescribeOneTable(entry->schema_oids[i],
									  entry->metadata.db_name, &schema_details,
									  description);

			if (status == YB_DIAGNOSTICS_ERROR)
			{
				ereport(ERROR,
						(errcode(ERRCODE_INTERNAL_ERROR),
						 errmsg("error while dumping schema details %s", description)));
			}

			appendStringInfo(&schema_details, "\n%s\n\n",
							 schema_details_separator);
		}

		/* Dump schema details */
		status = DumpToFile(entry->metadata.path, schema_details_file,
							schema_details.data, description);

		pfree(schema_details.data);
	}
	PG_CATCH();
	{
		ErrorData  *edata;

		/* save error info */
		MemoryContextSwitchTo(curr_context);
		edata = CopyErrorData();
		FlushErrorState();

		YbQueryDiagnosticsAppendToDescription(description,
											  "Fetching schema details errored "
											  "out "
											  "with the following message: %s",
											  edata->message);

		ereport(LOG,
				(errmsg("error while dumping schema details %s", edata->message)),
				(errhint("%s", edata->hint)));

		FreeErrorData(edata);
	}
	PG_END_TRY();

	database_connection_worker_info->initialized = false;

	return status;
}

/*
 * DumpActiveSessionHistory
 *		Gathers ASH data using SPI and dumps it to a file.
 */
static int
DumpActiveSessionHistory(const YbQueryDiagnosticsEntry *entry, char *description)
{
	char		query[256];
	StringInfoData ash_buffer;
	TimestampTz end_time = BundleEndTime(entry);
	YbQueryDiagnosticsStatusType status = YB_DIAGNOSTICS_SUCCESS;

	Assert(entry->metadata.start_time < end_time);

	char	   *start_time_str = pstrdup(timestamptz_to_str(entry->metadata.start_time));
	char	   *end_time_str = pstrdup(timestamptz_to_str(end_time));

	initStringInfo(&ash_buffer);
	snprintf(query, sizeof(query),
			 "SELECT * FROM yb_active_session_history "
			 "WHERE sample_time >= '%s' AND sample_time <= '%s'",
			 start_time_str, end_time_str);

	PG_TRY();
	{
		/* Execute the query */
		if (!ExecuteQuery(&ash_buffer, query, YB_QD_CSV))
			YbQueryDiagnosticsAppendToDescription(description, "Fetching ASH data failed; ");
		else
			DumpToFile(entry->metadata.path, ash_file,
					   ash_buffer.data, description);
	}
	PG_CATCH();
	{
		status = YB_DIAGNOSTICS_ERROR;
	}
	PG_END_TRY();

	pfree(start_time_str);
	pfree(end_time_str);
	pfree(ash_buffer.data);

	return status;
}

/*
 * DumpBufferIfHalfFull
 *      Checks if the buffer is at least half full, and if so, dumps it to a file.
 *
 * Returns:
 * YB_DIAGNOSTICS_SUCCESS if successful, YB_DIAGNOSTICS_ERROR otherwise
 */
static int
DumpBufferIfHalfFull(char *buffer, size_t max_len, const char *file_name, const char *folder_path,
					 char *description, slock_t *mutex)
{
	YbQueryDiagnosticsStatusType status = YB_DIAGNOSTICS_SUCCESS;

	SpinLockAcquire(mutex);

	int			buffer_len = strlen(buffer);

	if (buffer_len >= max_len / 2)
	{
		char	   *buffer_copy = palloc(buffer_len + 1);

		memcpy(buffer_copy, buffer, buffer_len + 1);
		buffer[0] = '\0';		/* Reset the buffer */

		SpinLockRelease(mutex);

		status = DumpToFile(folder_path, file_name, buffer_copy, description);

		pfree(buffer_copy);
	}
	else
		SpinLockRelease(mutex);

	return status;
}

/*
 * FinishBundleProcessing
 *		Stops the bundle processing by removing the entry from the bundles_in_progress hash table,
 *		and inserting the completed bundle info into the bundles_completed circular buffer.
 */
static void
FinishBundleProcessing(YbQueryDiagnosticsMetadata *metadata,
					   YbQueryDiagnosticsStatusType status, const char *description)
{
	/*
	 * Insert the completed bundle info into the bundles_completed circular
	 * buffer
	 */
	InsertBundlesIntoView(metadata, status, description);

	/* Remove the entry from the bundles_in_progress hash table */
	RemoveBundleInfo(metadata->params.query_id);
}

/*
 * RemoveBundleInfo
 *		Removes the entry from the bundles_in_progress hash table.
 */
static void
RemoveBundleInfo(int64 query_id)
{
	/*
	 * Acquire the exclusive lock on the bundles_in_progress hash table.
	 * This is necessary to prevent other background workers from
	 * accessing the hash table while we are removing the entry.
	 */
	LWLockAcquire(bundles_in_progress_lock, LW_EXCLUSIVE);

	hash_search(bundles_in_progress, &query_id, HASH_REMOVE, NULL);

	LWLockRelease(bundles_in_progress_lock);
}

/*
 * MakeDirectory
 *		Creates the directory structure recursively for storing the diagnostics data.
 *
 * Returns:
 * YB_DIAGNOSTICS_SUCCESS if successful, YB_DIAGNOSTICS_ERROR otherwise
 */
static int
MakeDirectory(const char *path, char *description)
{
	/*
	 * pg_mkdir_p modifies the path in case of failure, so we create a copy of the path
	 * and pass it to pg_mkdir_p.
	 */
	char		path_copy[MAXPGPATH];

	memcpy(path_copy, path, MAXPGPATH);

	if (pg_mkdir_p((char *) path_copy, pg_dir_create_mode) == -1)
	{
		snprintf(description, YB_QD_DESCRIPTION_LEN,
				 "Failed to create query diagnostics directory, %s;", strerror(errno));
		return YB_DIAGNOSTICS_ERROR;
	}
	return YB_DIAGNOSTICS_SUCCESS;
}

/*
 * DescribeOneTable
 * 		This function prints table details for the given table oid into schema_details.
 *		Note: This function is a modified version of DescribeOneTableDetails in describe.c
 *
 *		Returns YB_DIAGNOSTICS_SUCCESS if the function executed successfully,
 *		YB_DIAGNOSTICS_ERROR in case of an error.
 *		In case of an error, the function copies the error message to description.
 */
static int
DescribeOneTable(Oid oid, const char *db_name, StringInfo schema_details,
				 char *description)
{
	const char *queries[] = {
		/* Table info, tablegroup, and colocated status */
		"SELECT pg_catalog.quote_ident(n.nspname) || '.' || "
		"\n  pg_catalog.quote_ident(c.relname) AS \"Table Name\", "
		"\n  gr.grpname AS \"Table Groupname\", "
		"\n  CASE WHEN p.is_colocated THEN 'true' ELSE 'false' END AS \"Colocated\" "
		"\nFROM pg_catalog.pg_class c "
		"\nJOIN pg_catalog.pg_namespace n ON "
		"\n  c.relnamespace = n.oid "
		"\nLEFT JOIN pg_catalog.yb_table_properties(c.oid) p ON "
		"\n  true "
		"\nLEFT JOIN pg_catalog.pg_yb_tablegroup gr ON "
		"\n  gr.oid = p.tablegroup_oid "
		"\nWHERE c.oid = %u;",

		/* Sequence details */
		"SELECT pg_catalog.format_type(seqtypid, NULL) AS \"Type\", "
		"\n  seqstart AS \"Start\", "
		"\n  seqmin AS \"Minimum\", "
		"\n  seqmax AS \"Maximum\", "
		"\n  seqincrement AS \"Increment\", "
		"\n  CASE WHEN seqcycle THEN 'yes' ELSE 'no' END AS \"Cycles?\", "
		"\n  seqcache AS \"Cache\" "
		"\nFROM pg_catalog.pg_sequence WHERE seqrelid = %u;",

		/* Column that owns this sequence */
		"SELECT pg_catalog.quote_ident(nspname) || '.' || "
		"\n  pg_catalog.quote_ident(relname) || '.' || "
		"\n  pg_catalog.quote_ident(attname) AS \"Owned By\" "
		"\nFROM pg_catalog.pg_class c "
		"\nINNER JOIN pg_catalog.pg_depend d ON "
		"\n  c.oid=d.refobjid "
		"\nINNER JOIN pg_catalog.pg_namespace n ON"
		"\n  n.oid=c.relnamespace "
		"\nINNER JOIN pg_catalog.pg_attribute a ON ("
		"\n  a.attrelid=c.oid AND "
		"\n  a.attnum=d.refobjsubid) "
		"\nWHERE d.classid='pg_catalog.pg_class'::pg_catalog.regclass AND "
		"\n  d.refclassid='pg_catalog.pg_class'::pg_catalog.regclass AND "
		"\n  d.objid=%u AND d.deptype IN ('a', 'i');",

		/* Per-column info */
		"SELECT a.attname AS \"Column\", "
		"\n  pg_catalog.format_type(a.atttypid, a.atttypmod) AS \"Type\", "
		"\n  COALESCE((SELECT c.collname "
		"\n    FROM pg_catalog.pg_collation c, pg_catalog.pg_type t "
		"\n    WHERE c.oid = a.attcollation AND "
		"\n      t.oid = a.atttypid AND "
		"\n      a.attcollation <> t.typcollation), '') AS \"Collation\", "
		"\n  CASE WHEN a.attnotnull THEN 'not null' ELSE '' END AS \"Nullable\", "
		"\n  (SELECT substring(pg_catalog.pg_get_expr(d.adbin, d.adrelid) for 128) "
		"\n   FROM pg_catalog.pg_attrdef d "
		"\n   WHERE d.adrelid = a.attrelid AND "
		"\n   d.adnum = a.attnum AND "
		"\n   a.atthasdef) AS \"Default\", "
		"\n  CASE a.attstorage "
		"\n    WHEN 'p' THEN 'plain' "
		"\n    WHEN 'x' THEN 'extended' "
		"\n    WHEN 'm' THEN 'main' "
		"\n    ELSE '' "
		"\n  END AS \"Storage\", "
		"\n  CASE WHEN a.attstattarget=-1 THEN '' ELSE a.attstattarget::text END AS \"Stats Target\", "
		"\n  COALESCE(pg_catalog.col_description(a.attrelid, a.attnum), '') AS \"Description\" "
		"\nFROM pg_catalog.pg_attribute a "
		"\nWHERE a.attrelid = %u AND "
		"\n  a.attnum > 0 AND "
		"\n  NOT a.attisdropped "
		"\nORDER BY a.attnum;",

		/* Footers */
		"SELECT inhparent::pg_catalog.regclass, "
		"\n  pg_catalog.pg_get_expr(c.relpartbound, inhrelid), "
		"\n  pg_catalog.pg_get_partition_constraintdef(inhrelid) "
		"\nFROM pg_catalog.pg_class c "
		"\nJOIN pg_catalog.pg_inherits i ON "
		"\n  c.oid = inhrelid "
		"\nWHERE c.oid = %u AND "
		"\n  c.relispartition;",

		/* Indexes */
		"SELECT c2.relname AS \"Name\", "
		"\n  pg_catalog.pg_get_indexdef(i.indexrelid, 0, true) AS \"Index Definition\", "
		"\n  pg_catalog.pg_get_constraintdef(con.oid, true) AS \"Constraint Definition\" "
		"\nFROM pg_catalog.pg_class c, "
		"\n  pg_catalog.pg_class c2, "
		"\n  pg_catalog.pg_index i "
		"\nLEFT JOIN pg_catalog.pg_constraint con ON "
		"\n  (conrelid = i.indrelid AND "
		"\n  conindid = i.indexrelid AND "
		"\n  contype IN ('p','u','x')) "
		"\nWHERE c.oid = %u AND "
		"\n  c.oid = i.indrelid AND "
		"\n  i.indexrelid = c2.oid "
		"\nORDER BY i.indisprimary DESC, i.indisunique DESC, c2.relname;",

		/* Check constraints */
		"SELECT r.conname AS \"Name\", "
		"\n  pg_catalog.pg_get_constraintdef(r.oid, true) AS \"Constraint Definition\" "
		"\nFROM pg_catalog.pg_constraint r "
		"\nWHERE r.conrelid = %u AND "
		"\n  r.contype = 'c' "
		"\nORDER BY 1;",

		/* Foreign-key constraints */
		"SELECT conname AS \"Name\", "
		"\n  pg_catalog.pg_get_constraintdef(r.oid, true) AS \"Constraint Definition\" "
		"\nFROM pg_catalog.pg_constraint r "
		"\nWHERE r.conrelid = %u AND "
		"\n  r.contype = 'f' "
		"\nORDER BY 1;",

		/* Incoming foreign-key references */
		"SELECT conname AS \"Name\", "
		"\n  conrelid::pg_catalog.regclass as \"Relation Name\", "
		"\n  pg_catalog.pg_get_constraintdef(c.oid, true) AS \"Constraint Definition\" "
		"\nFROM pg_catalog.pg_constraint c "
		"\nWHERE c.confrelid = %u AND "
		"\n  c.contype = 'f' "
		"\nORDER BY 1;",

		/* Row-level policies */
		"SELECT pol.polname AS \"Name\", "
		"\n  CASE WHEN pol.polpermissive "
		"\n    THEN 'Permissive' ELSE 'Restrictive' END AS \"Type\", "
		"\n  CASE WHEN pol.polroles = '{0}' "
		"\n    THEN 'ALL ROLES' "
		"\n    ELSE pg_catalog.array_to_string(ARRAY(SELECT rolname FROM pg_catalog.pg_roles "
		"\n      WHERE oid = ANY (pol.polroles) ORDER BY 1), ',') "
		"\n  END AS \"Applicable Roles\", "
		"\n  pg_catalog.pg_get_expr(pol.polqual, pol.polrelid) AS \"USING Expression\", "
		"\n  pg_catalog.pg_get_expr(pol.polwithcheck, pol.polrelid) AS \"With CHECK Expression\", "
		"\n  CASE pol.polcmd "
		"\n    WHEN 'r' THEN 'SELECT' "
		"\n    WHEN 'a' THEN 'INSERT' "
		"\n    WHEN 'w' THEN 'UPDATE' "
		"\n    WHEN 'd' THEN 'DELETE' "
		"\n    ELSE 'ALL' "
		"\n  END AS \"Applicable Command\" "
		"\nFROM pg_catalog.pg_policy pol "
		"\nWHERE pol.polrelid = %u "
		"\nORDER BY 1;",

		/* Extended statistics */
		"SELECT oid AS \"OID\", "
		"\n  stxrelid::pg_catalog.regclass AS \"TABLE\", "
		"\n  stxnamespace::pg_catalog.regnamespace AS \"Namespace\", "
		"\n  stxname AS \"Statistics Name\", "
		"\n  (SELECT pg_catalog.string_agg(pg_catalog.quote_ident(attname),', ') "
		"\n    FROM pg_catalog.unnest(stxkeys) s(attnum) "
		"\n    JOIN pg_catalog.pg_attribute a ON "
		"\n      (stxrelid = a.attrelid "
		"\n      AND a.attnum = s.attnum "
		"\n      AND NOT attisdropped)) AS \"Columns\", "
		"\n  CASE WHEN 'd' = ANY(stxkind) THEN 'Yes' ELSE 'No' END AS \"N-Dinstinct Enabled\", "
		"\n  CASE WHEN 'f' = ANY(stxkind) THEN 'Yes' ELSE 'No' END AS \"Functional Dependencies Enabled\" "
		"\nFROM pg_catalog.pg_statistic_ext stat "
		"\nWHERE stxrelid = %u "
		"\nORDER BY 1;",

		/* Rules */
		"SELECT r.rulename AS \"Name\", "
		"\n  regexp_replace(trim(trailing ';' from pg_catalog.pg_get_ruledef(r.oid, true)), E'\\n', ' ', 'g') AS \"Rule Definition\" "
		"\nFROM pg_catalog.pg_rewrite r "
		"\nWHERE r.ev_class = %u AND "
		"\n  r.rulename != '_RETURN' "
		"\nORDER BY 1;",

		/* Publications */
		"SELECT pubname AS \"Name\" "
		"\nFROM pg_catalog.pg_publication p "
		"\nJOIN pg_catalog.pg_publication_rel pr ON "
		"\n  p.oid = pr.prpubid "
		"\nWHERE pr.prrelid = %u "
		"\nUNION ALL "
		"\nSELECT pubname "
		"\nFROM pg_catalog.pg_publication p "
		"\nWHERE p.puballtables AND "
		"\n  pg_catalog.pg_relation_is_publishable(%u) "
		"\nORDER BY 1;",

		/* Triggers */
		"SELECT t.tgname AS \"Name\", "
		"\n  pg_catalog.pg_get_triggerdef(t.oid, true) AS \"Trigger Definition\", "
		"\n  CASE t.tgenabled "
		"\n    WHEN 'O' THEN 'Enabled' "
		"\n    WHEN 'D' THEN 'Disabled' "
		"\n    WHEN 'R' THEN 'Replica' "
		"\n    WHEN 'A' THEN 'Always' "
		"\n    ELSE 'Unknown' "
		"\n  END AS \"Trigger Status\", "
		"\n  CASE WHEN t.tgisinternal THEN 'Yes' ELSE 'No' END AS \"Is Internal\" "
		"\nFROM pg_catalog.pg_trigger t "
		"\nWHERE t.tgrelid = %u AND "
		"\n  (NOT t.tgisinternal OR (t.tgisinternal AND t.tgenabled = 'D') OR "
		"\n  EXISTS (SELECT 1 FROM pg_catalog.pg_depend WHERE objid = t.oid AND "
		"\n    refclassid = 'pg_catalog.pg_trigger'::regclass)) "
		"\nORDER BY 1;",

		/* View definition */
		"SELECT pg_catalog.pg_get_viewdef(%u::pg_catalog.oid, true) AS \"View Definition\";",

		/* Foreign table information */
		"SELECT s.srvname AS \"Server Name\", "
		"\n  pg_catalog.array_to_string(ARRAY( "
		"\n  SELECT pg_catalog.quote_ident(option_name) || ' ' || pg_catalog.quote_literal(option_value) "
		"\n  FROM pg_catalog.pg_options_to_table(ftoptions)), ', ') AS \"FDW Options\" "
		"\nFROM pg_catalog.pg_foreign_table f, "
		"\n  pg_catalog.pg_foreign_server s "
		"\nWHERE f.ftrelid = %u AND "
		"\n  s.oid = f.ftserver;",
	};

	/* Titles corresponding to each query */
	const char *titles[] = {
		"Table information",
		"Sequence details",
		"Sequence owner",
		"Columns",
		"Partition information",
		"Indexes",
		"Check constraints",
		"Foreign-key constraints",
		"Referenced by",
		"Policies",
		"Statistics",
		"Rules",
		"Publications",
		"Triggers",
		"View definition",
		"Foreign table information",
	};

	int			num_of_queries = sizeof(queries) / sizeof(queries[0]);
	char		formatted_query[BLCKSZ];

	PrintTableAndDatabaseName(oid, db_name, schema_details);

	for (int i = 0; i < num_of_queries; i++)
	{
		snprintf(formatted_query, sizeof(formatted_query), queries[i], oid);

		/* Initialize a temporary buffer to hold the query result. */
		StringInfoData temp_buffer;

		initStringInfo(&temp_buffer);

		/* Execute the query and format the output in tabular view. */
		if (!ExecuteQuery(&temp_buffer, formatted_query, YB_QD_TABULAR))
		{
			YbQueryDiagnosticsAppendToDescription(description,
												  "Fetching schema details failed;");
			pfree(temp_buffer.data);
			return YB_DIAGNOSTICS_ERROR;
		}

		/* If the query returned any data, append it with a title header. */
		if (temp_buffer.len > 0)
			appendStringInfo(schema_details, "- %s:\n%s\n", titles[i], temp_buffer.data);

		pfree(temp_buffer.data);
	}

	return YB_DIAGNOSTICS_SUCCESS;
}

static void
PrintTableAndDatabaseName(Oid oid, const char *db_name,
						  StringInfo schema_details)
{
	StartTransactionCommand();

	char	   *table_name = get_rel_name(oid);

	if (table_name)
	{
		appendStringInfo(schema_details,
						 "- Table name: %s\n- Database name: %s\n", table_name,
						 db_name);
		pfree(table_name);
	}

	CommitTransactionCommand();
}

/*
 * ExecuteQuery
 *		Uses SPI framework to execute the given query and appends the results to
 *		the output_buffer. The results are formatted based on the output_format.
 *		- YB_QD_CSV: Comma-separated values
 *		- YB_QD_TABULAR: Tabular format
 *		Note: title is only added for YB_QD_TABULAR format to maintain consistency
 *			  with csv formatting.
 *
 *		Returns true if the query was executed successfully, false in case of an error.
 *		Error is copied to description.
 */
static bool
ExecuteQuery(StringInfo output_buffer, const char *query, int output_format)
{
	int			ret;
	int			num_cols;

	/*
	 * Start a transaction on which we can run queries. Note that each StartTransactionCommand()
	 * call should be preceded by a SetCurrentStatementStartTimestamp() call,
	 * which sets the transaction start time.
	 * Also, each other query sent to SPI should be preceded by
	 * SetCurrentStatementStartTimestamp(), so that statement start time is always up to date.
	 */
	SetCurrentStatementStartTimestamp();
	StartTransactionCommand();

	/* Lets us run queries through the SPI manager */
	ret = SPI_connect();
	if (ret != SPI_OK_CONNECT)
	{
		ereport(LOG,
				(errmsg("SPI_connect failed: %s, while executing: %s",
						SPI_result_code_string(ret), query)));
		pgstat_report_activity(STATE_IDLE, NULL);
		return false;
	}

	/*
	 * Creates an "active" snapshot which is necessary for queries to have
	 * MVCC data to work on
	 */
	PushActiveSnapshot(GetTransactionSnapshot());

	/* Make our activity visible through the pgstat views */
	pgstat_report_activity(STATE_RUNNING, query);

	/* Execute the query and get all of the rows from the result set */
	ret = SPI_execute(query,
					  true,		/* read_only */
					  0);		/* tcount (0 = unlimited rows) */
	if (ret != SPI_OK_SELECT)
	{
		ereport(LOG,
				(errmsg("SPI_execute failed: %s, while executing: %s",
						SPI_result_code_string(ret), query)));
		pgstat_report_activity(STATE_IDLE, NULL);
		return false;
	}

	num_cols = SPI_tuptable->tupdesc->natts;

	switch (output_format)
	{
		case YB_QD_CSV:
			{
				GetResultsInCsvFormat(output_buffer, num_cols);
				break;
			}

		case YB_QD_TABULAR:
			{
				GetResultsInTabularFormat(output_buffer, num_cols);
				break;
			}

		default:
			Assert(false);
	}

	/* Close the SPI connection and clean up the transaction context */
	ret = SPI_finish();
	if (ret != SPI_OK_FINISH)
	{
		ereport(LOG,
				(errmsg("SPI_finish failed: %s, while executing: %s",
						SPI_result_code_string(ret), query)));
		pgstat_report_activity(STATE_IDLE, NULL);
		return false;
	}

	PopActiveSnapshot();
	CommitTransactionCommand();
	pgstat_report_stat(true);
	pgstat_report_activity(STATE_IDLE, NULL);
	return true;
}

static void
GetResultsInCsvFormat(StringInfo output_buffer, int num_cols)
{
	/*
	 * If there are no rows within output then its better to not create the file
	 * than to output only the header.
	 */
	if (SPI_processed == 0)
		return;

	/* Build CSV header */
	for (int i = 0; i < num_cols; i++)
	{
		Form_pg_attribute attr = TupleDescAttr(SPI_tuptable->tupdesc, i);

		appendStringInfo(output_buffer, "%s%s", (i == 0 ? "" : ","),
						 NameStr(attr->attname));
	}
	appendStringInfoChar(output_buffer, '\n');
	/* Build rows in CSV format */
	for (uint64 j = 0; j < SPI_processed; j++)
	{
		HeapTuple	tuple = SPI_tuptable->vals[j];

		for (int i = 0; i < num_cols; i++)
		{
			char	   *val = SPI_getvalue(tuple, SPI_tuptable->tupdesc, i + 1);

			appendStringInfo(output_buffer, "%s%s", (i == 0 ? "" : ","),
							 val ? val : "");
			if (val)
				pfree(val);
		}
		appendStringInfoChar(output_buffer, '\n');
	}
}

static void
GetResultsInTabularFormat(StringInfo output_buffer, int num_cols)
{
	/*
	 * If there are no rows within output then its better to not create the file
	 * than to output only the header.
	 */
	if (SPI_processed == 0)
		return;

	int		   *col_widths = (int *) palloc0(num_cols * sizeof(int));
	int			initial_len = output_buffer->len;
	bool		is_it_all_null = true;

	/* Initialize column widths with column name lengths */
	for (int i = 0; i < num_cols; i++)
	{
		Form_pg_attribute attr = TupleDescAttr(SPI_tuptable->tupdesc, i);

		col_widths[i] = strlen(NameStr(attr->attname));
	}

	/* Adjust column widths based on data */
	for (uint64 j = 0; j < SPI_processed; j++)
	{
		HeapTuple	tuple = SPI_tuptable->vals[j];

		for (int i = 0; i < num_cols; i++)
		{
			char	   *val = SPI_getvalue(tuple, SPI_tuptable->tupdesc, i + 1);
			int			val_len = val ? strlen(val) : 0;	/* Empty string for NULL */

			col_widths[i] = Max(col_widths[i], val_len);

			if (val)
				pfree(val);
		}
	}

	/* Format column names */
	appendStringInfoChar(output_buffer, '|');
	for (int i = 0; i < num_cols; i++)
	{
		Form_pg_attribute attr = TupleDescAttr(SPI_tuptable->tupdesc, i);

		appendStringInfo(output_buffer, "%-*s |", col_widths[i],
						 NameStr(attr->attname));
	}

	appendStringInfoChar(output_buffer, '\n');

	/* Add separator line */
	appendStringInfoChar(output_buffer, '+');
	for (int i = 0; i < num_cols; i++)
	{
		for (int j = 0; j <= col_widths[i]; j++)
			appendStringInfoChar(output_buffer, '-');

		appendStringInfoChar(output_buffer, '+');
	}

	appendStringInfoChar(output_buffer, '\n');

	/* Format rows */
	for (uint64 j = 0; j < SPI_processed; j++)
	{
		HeapTuple	tuple = SPI_tuptable->vals[j];

		appendStringInfoChar(output_buffer, '|');
		for (int i = 0; i < num_cols; i++)
		{
			char	   *val = SPI_getvalue(tuple, SPI_tuptable->tupdesc, i + 1);

			appendStringInfo(output_buffer, "%-*s |", col_widths[i], val ? val : "");
			if (val)
			{
				is_it_all_null = false;
				pfree(val);
			}
		}
		appendStringInfoChar(output_buffer, '\n');
	}

	/*
	 * Reset output_buffer to the original state if the result is empty to avoid
	 * only printing headers.
	 */
	if (is_it_all_null)
	{
		output_buffer->len = initial_len;
		Assert(output_buffer->data);
		output_buffer->data[initial_len] = '\0';
	}

	if (col_widths)
		pfree(col_widths);
}

/*
 * DumpToFile
 *		Creates the file (/folder_path/file_name) and writes the data to it.
 * Returns:
 *		YB_DIAGNOSTICS_SUCCESS if the file was successfully written, YB_DIAGNOSTICS_ERROR otherwise.
 */
static int
DumpToFile(const char *folder_path, const char *file_name, const char *data,
		   char *description)
{
	bool		ok = false;
	File		file = 0;
	const int	file_path_len = MAXPGPATH + strlen(file_name) + 1;
	char		file_path[file_path_len];
	MemoryContext curr_context = CurrentMemoryContext;

	/* No data to write */
	if (data[0] == '\0')
		return YB_DIAGNOSTICS_SUCCESS;

#ifdef WIN32
	snprintf(file_path, file_path_len, "%s\\%s", path, file_name);
#else
	snprintf(file_path, file_path_len, "%s/%s", folder_path, file_name);
#endif

	/*
	 * We use PG_TRY to handle any function returning an error. This ensures that the entry
	 * can be safely removed from the hash table even if the file writing fails.
	 */
	PG_TRY();
	{
		if ((file = PathNameOpenFile(file_path,
									 O_RDWR | O_CREAT | O_APPEND)) < 0)
			snprintf(description, YB_QD_DESCRIPTION_LEN,
					 "out of file descriptors: %s; release and retry", strerror(errno));
		else if (FileWrite(file, (char *) data, strlen(data), FileSize(file),
						   WAIT_EVENT_DATA_FILE_WRITE) < 0)
			snprintf(description, YB_QD_DESCRIPTION_LEN, "Error writing to file; %s",
					 strerror(errno));

		else
			ok = true;
	}
	PG_CATCH();
	{
		ErrorData  *edata;

		/* save the error data */
		MemoryContextSwitchTo(curr_context);
		edata = CopyErrorData();
		FlushErrorState();

		snprintf(description, YB_QD_DESCRIPTION_LEN, "%s", edata->message);

		ereport(LOG,
				(errmsg("error while dumping to file %s", edata->message)),
				(errhint("%s", edata->hint)));

		FreeErrorData(edata);
	}
	PG_END_TRY();

	if (file > 0)
		FileClose(file);

	return ok ? YB_DIAGNOSTICS_SUCCESS : YB_DIAGNOSTICS_ERROR;
}

/*
 * YbQueryDiagnosticsMain
 *		Background worker for yb_query_diagnostics
 *
 * Scans and removes expired entries within the shared hash table.
 * The worker sleeps for yb_query_diagnostics_bg_worker_interval_ms seconds
 * before scanning the hash table again.
 */
void
YbQueryDiagnosticsMain(Datum main_arg)
{
	ereport(LOG,
			(errmsg("starting bgworker for yb_query_diagnostics with time interval of %dms",
					yb_query_diagnostics_bg_worker_interval_ms)));

	/* Register functions for SIGTERM/SIGHUP management */
	pqsignal(SIGHUP, SignalHandlerForConfigReload);
	pqsignal(SIGTERM, SignalHandlerForShutdownRequest);
	pqsignal(SIGINT, SignalHandlerForShutdownRequest);
	pqsignal(SIGQUIT, SignalHandlerForCrashExit);

	/* Initialize the worker process */
	BackgroundWorkerUnblockSignals();

	pgstat_report_appname("yb_query_diagnostics bgworker");

	while (true)
	{
		int			rc;

		/* Wait necessary amount of time */
		rc = WaitLatch(MyLatch,
					   WL_LATCH_SET | WL_TIMEOUT | WL_POSTMASTER_DEATH,
					   yb_query_diagnostics_bg_worker_interval_ms,
					   WAIT_EVENT_YB_QUERY_DIAGNOSTICS_MAIN);
		ResetLatch(MyLatch);

		/* Bailout if postmaster has died */
		if (rc & WL_POSTMASTER_DEATH)
			proc_exit(1);

		HandleMainLoopInterrupts();

		/* Check for expired entries within the shared hash table */
		FlushAndCleanBundles();

		/*
		 * Acquire the exclusive lock on the bundles_in_progress hash table.
		 * This is necessary to prevent other sessions from
		 * removing entries while we are checking.
		 */
		LWLockAcquire(bundles_in_progress_lock, LW_EXCLUSIVE);

		/* If there are no bundles being diagnosed, switch off the bgworker */
		if (hash_get_num_entries(bundles_in_progress) == 0)
			(*bg_worker_should_be_active) = false;

		LWLockRelease(bundles_in_progress_lock);

		/*
		 * Do this outside the lock to ensure we release the lock before
		 * exiting.
		 */
		if (!(*bg_worker_should_be_active))
		{
			if (YBQueryDiagnosticsTestRaceCondition())
				pg_usleep(10000000L);	/* 10 seconds */

			/* Kill the bgworker */
			ereport(LOG,
					(errmsg("stopping query diagnostics background worker "
							"- no active bundles")));
			proc_exit(0);
		}
	}
}

/*
 * BuildRelationFilter
 *      Builds a SQL filter condition for relations based on the relation OIDs.
 *
 */
static void
BuildRelationFilter(const YbQueryDiagnosticsEntry *entry, StringInfo filter_string)
{
	StringInfoData oid_list;
	int			oid_count = 0;

	initStringInfo(&oid_list);

	for (int i = 0; i < YB_QD_MAX_SCHEMA_OIDS; i++)
	{
		if (entry->schema_oids[i] == InvalidOid)
			break;

		if (oid_count > 0)
			appendStringInfoString(&oid_list, ", ");

		appendStringInfo(&oid_list, "%u", entry->schema_oids[i]);
		oid_count++;
	}

	appendStringInfo(filter_string,
					 "c.oid in (%s) or c.oid in (select "
					 "indexrelid from pg_index where indrelid in (%s))",
					 oid_list.data, oid_list.data);

	pfree(oid_list.data);
}

/*
 * AppendJsonArrayFromBuffer
 *      Takes a raw buffer containing JSON objects (one per line after a
 *      header) and appends it as a formatted JSON array under the given key
 *      to the target buffer.
 */
static void
AppendJsonArrayFromBuffer(StringInfo target_buffer,
						  const StringInfo source_buffer,
						  const char *json_key)
{
	char	   *line;
	char	   *saveptr = NULL;
	bool		first_item = true;
	const char *header_line = "row_to_json";

	/* Add the key and start the array */
	appendStringInfo(target_buffer, ",\n    %s: [\n", json_key);

	for (line = strtok_r(source_buffer->data, "\n", &saveptr); line != NULL;
		 line = strtok_r(NULL, "\n", &saveptr))
	{
		/* Skip the header line and empty lines */
		if (strcmp(line, header_line) == 0 || strlen(line) == 0)
			continue;

		if (!first_item)
			appendStringInfoString(target_buffer, ",\n");

		/* Add indentation and the JSON line */
		appendStringInfo(target_buffer, "        %s", line);
		first_item = false;
	}

	/* Close the array (add newline only if items were added) */
	if (!first_item)
		appendStringInfoChar(target_buffer, '\n');

	appendStringInfoString(target_buffer, "    ]");
}

/*
 * DumpAndFormatStatistics
 *      Common function to dump statistics data into a JSON file.
 *      Takes an array of queries and their corresponding JSON keys.
 */
static int
DumpAndFormatStatistics(const YbQueryDiagnosticsEntry *entry, char *description,
						const char *file_name, const char **queries, const char **json_key_names,
						int query_count, bool *needs_relation_filter)
{
	StringInfoData relation_names_filter;
	StringInfoData query;
	StringInfoData buffer;
	StringInfoData final_json_buffer;
	YbQueryDiagnosticsStatusType status = YB_DIAGNOSTICS_SUCCESS;

	/* We need not dump anything if no relations are provided */
	if (entry->schema_oids[0] == InvalidOid)
	{
		YbQueryDiagnosticsAppendToDescription(description,
											  "No relation OIDs provided for statistics dump;");

		return YB_DIAGNOSTICS_SUCCESS;
	}

	initStringInfo(&relation_names_filter);
	initStringInfo(&final_json_buffer);
	initStringInfo(&query);
	initStringInfo(&buffer);

	BuildRelationFilter(entry, &relation_names_filter);

	/* Start the final JSON structure */
	appendStringInfoString(&final_json_buffer, "{\n    \"version\": \"1.0.0\"");

	/* Process each query */
	for (int i = 0; i < query_count; i++)
	{
		/*
		 * Format the query with relation filter if needed, otherwise use it
		 * as-is
		 */
		if (needs_relation_filter[i])
			appendStringInfo(&query, queries[i], relation_names_filter.data);
		else
			appendStringInfoString(&query, queries[i]);

		/* Execute query into its temporary buffer */
		if (!ExecuteQuery(&buffer, query.data, YB_QD_CSV))
		{

			YbQueryDiagnosticsAppendToDescription("Failed to execute query for %s",
												  json_key_names[i] + 1);	/* Skip the leading
																			 * quote */
			status = YB_DIAGNOSTICS_ERROR;

			goto cleanup;
		}

		/* Append formatted data to the final buffer */
		AppendJsonArrayFromBuffer(&final_json_buffer, &buffer,
								  json_key_names[i]);

		resetStringInfo(&query);
		resetStringInfo(&buffer);
	}

	appendStringInfoString(&final_json_buffer, "\n}");

	status = DumpToFile(entry->metadata.path, file_name,
						final_json_buffer.data, description);

cleanup:
	pfree(query.data);
	pfree(buffer.data);
	pfree(relation_names_filter.data);
	pfree(final_json_buffer.data);

	return status;
}

/*
 * DumpStatistics
 *		Dumps statistics data from pg_statistic and pg_class into a JSON file.
 *		%s within queries is later replaced with the relation filter condition.
 */
static int
DumpStatistics(const YbQueryDiagnosticsEntry *entry, char *description)
{
	const char *queries[] = {
		"SELECT row_to_json(t) FROM "
		"(SELECT c.relname, c.relpages, c.reltuples, c.relallvisible, n.nspname "
		"FROM pg_class c "
		"JOIN pg_namespace n ON c.relnamespace = n.oid "
		"AND n.nspname NOT IN ('pg_catalog', 'pg_toast', 'information_schema') "
		"and (%s) ORDER BY n.nspname, c.relname) t",

		"SELECT row_to_json(t) FROM "
		"(SELECT n.nspname AS nspname, "
		"c.relname AS relname, "
		"a.attname AS attname, "
		"(SELECT nspname FROM pg_namespace WHERE oid = t.typnamespace) AS typnspname, "
		"t.typname AS typname, "
		"s.stainherit, s.stanullfrac, s.stawidth, s.stadistinct, "
		"s.stakind1, s.stakind2, s.stakind3, s.stakind4, s.stakind5, "
		"s.staop1, s.staop2, s.staop3, s.staop4, s.staop5, "
		"s.stanumbers1, s.stanumbers2, s.stanumbers3, s.stanumbers4, s.stanumbers5, "
		"s.stacoll1, s.stacoll2, s.stacoll3, s.stacoll4, s.stacoll5, "
		"s.stavalues1, s.stavalues2, s.stavalues3, s.stavalues4, s.stavalues5 "
		"FROM pg_class c "
		"JOIN pg_namespace n ON c.relnamespace = n.oid "
		"AND n.nspname NOT IN ('pg_catalog', 'pg_toast', 'information_schema') and (%s) "
		"JOIN pg_statistic s ON s.starelid = c.oid "
		"JOIN pg_attribute a ON c.oid = a.attrelid AND s.staattnum = a.attnum "
		"JOIN pg_type t ON a.atttypid = t.oid "
		"ORDER BY n.nspname, c.relname, a.attname) t"
	};

	const char *json_key_names[] = {"\"pg_class\"", "\"pg_statistic\""};
	bool		needs_relation_filter[] = {true, true};

	return DumpAndFormatStatistics(entry, description, statistics_file,
								   queries, json_key_names, 2 /* query_count */ ,
								   needs_relation_filter);
}

/*
 * DumpExtendedStatistics
 *		Dumps extended statistics data from pg_statistic_ext and
 *		pg_statistic_ext_data into a JSON file.
 *		%s within queries is later replaced with the relation filter condition.
 */
static int
DumpExtendedStatistics(const YbQueryDiagnosticsEntry *entry, char *description)
{
	const char *queries[] = {
		"SELECT row_to_json(t) FROM "
		"(SELECT c.relname, s.stxname, n.nspname, s.stxowner, s.stxstattarget, "
		"string_agg(a.attname, ',') AS stxkeys, s.stxkind, s.stxexprs "
		"FROM pg_class c "
		"JOIN pg_statistic_ext s ON c.oid = s.stxrelid "
		"JOIN pg_attribute a ON c.oid = a.attrelid AND a.attnum = ANY(s.stxkeys) "
		"JOIN pg_namespace n ON c.relnamespace = n.oid "
		"AND n.nspname NOT IN ('pg_catalog', 'pg_toast', 'information_schema') and (%s) "
		"GROUP BY c.relname, s.stxname, n.nspname, s.stxowner, s.stxstattarget, s.stxkind, s.stxexprs "
		"ORDER BY n.nspname, c.relname, s.stxname) t",

		"SELECT row_to_json(t) FROM "
		"(SELECT s.stxname, d.stxdinherit, d.stxdndistinct::bytea, "
		"d.stxddependencies::bytea, d.stxdmcv::bytea, d.stxdexpr "
		"FROM pg_statistic_ext s "
		"JOIN pg_statistic_ext_data d ON s.oid = d.stxoid "
		"ORDER BY s.stxname) t"
	};

	const char *json_key_names[] = {"\"pg_statistic_ext\"", "\"pg_statistic_ext_data\""};
	bool		needs_relation_filter[] = {true, false};

	return DumpAndFormatStatistics(entry, description, extended_statistics_file,
								   queries, json_key_names, 2 /* query_count */ ,
								   needs_relation_filter);
}

/*
 * DumpDataAndLogError
 *    Dump data by executing the DumpFunction and log any error.
 *
 * This helper consolidates error handling for multiple dump operations in the
 * database connection background worker.
 */
static void
DumpDataAndLogError(const char *data_identifier,
					int (*DumpFunction) (const YbQueryDiagnosticsEntry *, char *),
					YbQueryDiagnosticsEntry *entry, char *description)
{
	YbQueryDiagnosticsStatusType status = DumpFunction(entry, description);

	if (status == YB_DIAGNOSTICS_ERROR)
		ereport(LOG,
				(errmsg("QueryDiagnostics: database connection bgworker "
						"failed to dump %s", data_identifier)));
}

/*
 * YbQueryDiagnosticsDatabaseConnectionWorkerMain
 * 		Background worker for dumping schema details, active session history.
 *		Since these operations require query execution, a separate bgworker is
 *		required which can initialize connection to database.
 */
void
YbQueryDiagnosticsDatabaseConnectionWorkerMain(Datum main_arg)
{
	ereport(LOG,
			(errmsg("starting a background worker to dump schema details " \
					"and active session history")));

	YbQueryDiagnosticsEntry *entry = &database_connection_worker_info->entry;
	const char *database_name = entry->metadata.db_name;
	char	   *description = palloc0(YB_QD_DESCRIPTION_LEN);

	if (!database_name || !entry)
	{
		ereport(ERROR,
				(errcode(ERRCODE_INTERNAL_ERROR),
				 errmsg("invalid worker info passed to " \
						"YbQueryDiagnosticsDatabaseConnectionWorkerMain")));
		proc_exit(1);
	}

	/* Connect to the database */
	BackgroundWorkerInitializeConnection(database_name, NULL, 0);

	if (yb_enable_ash)
		DumpDataAndLogError("active session history", DumpActiveSessionHistory,
							entry, description);

	DumpDataAndLogError("schema details", DumpSchemaDetails,
						entry, description);
	DumpDataAndLogError("statistics", DumpStatistics,
						entry, description);
	DumpDataAndLogError("extended statistics", DumpExtendedStatistics,
						entry, description);

	ereport(DEBUG1,
			(errmsg("QueryDiagnostics: database connection bgworker description: %s",
					description)));

	pfree(description);
	proc_exit(0);
}

/*
 * FetchSchemaOids
 *		Fetches unique schema oids from the given rtable and stores them in schema_oids.
 */
static void
FetchSchemaOids(List *rtable, Oid *schema_oids)
{
	ListCell   *lc;
	int			i = 0;

	foreach(lc, rtable)
	{
		if (i >= YB_QD_MAX_SCHEMA_OIDS)
			break;

		RangeTblEntry *rte = (RangeTblEntry *) lfirst(lc);
		bool		already_exist = false;

		/*
		 * Using a bitmapset (bitmapset.h) for schema_oids would make insertion cleaner,
		 * but take up more memory and make iteration messier.
		 * Since YB_QD_MAX_SCHEMA_OIDS is small, we stick to an array.
		 */
		if (rte->relid != InvalidOid)
		{
			/* Check if the schema oid is already present */
			for (int j = 0; j < i; j++)
			{
				if (schema_oids[j] == rte->relid)
				{
					already_exist = true;
					break;
				}
			}

			if (!already_exist)
				schema_oids[i++] = rte->relid;
		}
	}
}

/*
 * ConstructDiagnosticsPath
 *		Creates the directory structure for storing the diagnostics data.
 *		Directory structure: pg_data/query-diagnostics/queryid/random_number/
 *
 * Errors out in case the path is too long.
 */
static void
ConstructDiagnosticsPath(YbQueryDiagnosticsMetadata *metadata)
{
	uint32		rand_num = DatumGetUInt32(hash_any((unsigned char *) &metadata->start_time,
												   sizeof(metadata->start_time)));
#ifdef WIN32
	const char *format = "%s\\%s\\%ld\\%u\\";
#else
	const char *format = "%s/%s/%ld/%u/";
#endif
	if (snprintf(metadata->path, MAXPGPATH, format,
				 DataDir, "query-diagnostics", metadata->params.query_id, rand_num) >= MAXPGPATH)
		ereport(ERROR,
				(errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
				 errmsg("path to pg_data is too long"),
				 errhint("Move the data directory to a shorter path.")));
}

/*
 * FetchParams
 *		Fetches the parameters from the yb_query_diagnostics function call and validates them.
 */
static void
FetchParams(YbQueryDiagnosticsParams *params, FunctionCallInfo fcinfo)
{
	params->query_id = PG_GETARG_INT64(0);
	params->diagnostics_interval_sec = PG_GETARG_INT64(1);
	params->explain_sample_rate = PG_GETARG_INT64(2);
	params->explain_analyze = PG_GETARG_BOOL(3);
	params->explain_dist = PG_GETARG_BOOL(4);
	params->explain_debug = PG_GETARG_BOOL(5);
	params->bind_var_query_min_duration_ms = PG_GETARG_INT64(6);

	if (params->query_id == 0)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("there cannot be a query with query_id 0")));

	if (params->diagnostics_interval_sec <= 0)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("diagnostics_interval_sec should be greater than 0")));

	if (params->explain_sample_rate < 0 || params->explain_sample_rate > 100)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("explain_sample_rate should be between 0 and 100")));

	if (params->bind_var_query_min_duration_ms < 0)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("bind_var_query_min_duration_ms cannot be less than 0")));

	if (params->explain_dist && !params->explain_analyze)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("explain_dist cannot be true without explain_analyze")));
}

/*
 * yb_query_diagnostics
 *		Enable query diagnostics for the given query ID.
 *	returns:
 * 		path to the diagnostics bundle is returned if the diagnostics started successfully,
 *		otherwise raises an ereport(ERROR).
 */
Datum
yb_query_diagnostics(PG_FUNCTION_ARGS)
{
	if (!yb_enable_query_diagnostics)
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("query diagnostics is not enabled"),
				 errhint("Set ysql_yb_enable_query_diagnostics gflag to true")));

	YbQueryDiagnosticsMetadata metadata;

	metadata.start_time = GetCurrentTimestamp();

	FetchParams(&metadata.params, fcinfo);

	ConstructDiagnosticsPath(&metadata);

	snprintf(metadata.db_name, NAMEDATALEN, "%s", YBCGetDatabaseName(MyDatabaseId));

	InsertNewBundleInfo(&metadata);

	PG_RETURN_TEXT_P(cstring_to_text(metadata.path));
}

/*
 * yb_cancel_query_diagnostics
 *		Stops query diagnostics for the given query ID, and no data is dumped.
 *	returns:
 * 		Nothing is returned if the diagnostics stopped successfully,
 *		otherwise raises an ereport(ERROR).
 */
Datum
yb_cancel_query_diagnostics(PG_FUNCTION_ARGS)
{
	if (!yb_enable_query_diagnostics)
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("query diagnostics is not enabled"),
				 errhint("Set ysql_yb_enable_query_diagnostics gflag to true")));

	int64		query_id = PG_GETARG_INT64(0);
	YbQueryDiagnosticsEntry *entry;

	if (query_id == 0)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("there cannot be a query with query_id 0")));

	LWLockAcquire(bundles_in_progress_lock, LW_SHARED);

	entry = (YbQueryDiagnosticsEntry *) hash_search(bundles_in_progress,
													&query_id, HASH_FIND,
													NULL);
	if (entry)
	{
		InsertBundlesIntoView(&entry->metadata, YB_DIAGNOSTICS_CANCELLED,
							  "Bundle was cancelled");

		LWLockRelease(bundles_in_progress_lock);
		RemoveBundleInfo(query_id);
	}
	else
	{
		LWLockRelease(bundles_in_progress_lock);
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("query diagnostics is not enabled for query_id %ld", query_id)));
	}

	PG_RETURN_VOID();
}
