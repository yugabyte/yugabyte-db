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

#include "yb_query_diagnostics.h"

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
#include "commands/tablespace.h"
#include "commands/explain.h"
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
#include "storage/ipc.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/jsonb.h"
#include "utils/lsyscache.h"
#include "utils/syscache.h"

static const char *constants_and_bind_variables_file = "constants_and_bind_variables.csv";
static const char *pgss_file = "pg_stat_statements.csv";
static const char *ash_file = "active_session_history.csv";
static const char *explain_plan_file = "explain_plan.txt";
static const char *schema_details_file = "schema_details.txt";

/* Separates schema details for different tables by 60 '=' */
const char *schema_details_separator = "============================================================";

/* Maximum number of entries in the hash table */
#define QUERY_DIAGNOSTICS_HASH_MAX_SIZE 100

/* Constants used for yb_query_diagnostics_status view */
#define YB_QUERY_DIAGNOSTICS_STATUS_COLS 8

typedef struct BundleInfo
{
	YbQueryDiagnosticsMetadata metadata; /* stores bundle's metadata */
	int			status; /* 0 - Success; 1 - In Progress; 2 - ERROR */
	char		description[YB_QD_DESCRIPTION_LEN]; /* stores error description */
} BundleInfo;

typedef struct YbQueryDiagnosticsBundles
{
	int			index;			/* index to insert new buffer entry */
	int			max_entries;	/* maximum # of entries in the buffer */
	LWLock	 	lock;			/* protects circular buffer from search/modification */
	BundleInfo	bundles[FLEXIBLE_ARRAY_MEMBER]; /* circular buffer to store info about bundles */
} YbQueryDiagnosticsBundles;

/* GUC variables */
int yb_query_diagnostics_bg_worker_interval_ms;
int yb_query_diagnostics_circular_buffer_size;

/* Saved hook value in case of unload */
static post_parse_analyze_hook_type prev_post_parse_analyze_hook = NULL;
static ExecutorStart_hook_type prev_ExecutorStart = NULL;
static ExecutorEnd_hook_type prev_ExecutorEnd = NULL;

/* Flags set by interrupt handlers for later service in the main loop. */
static volatile sig_atomic_t got_sigterm = false;
static volatile sig_atomic_t got_sighup = false;

YbGetNormalizedQueryFuncPtr yb_get_normalized_query = NULL;
TimestampTz *yb_pgss_last_reset_time;
PgssFillInConstantLengths yb_qd_fill_in_constant_lengths = NULL;

static HTAB *bundles_in_progress = NULL;
static LWLock *bundles_in_progress_lock; /* protects bundles_in_progress hash table */
static YbQueryDiagnosticsBundles *bundles_completed = NULL;
static const char *status_msg[] = {"Success", "In Progress", "Error", "Cancelled"};
static bool current_query_sampled = false;

static void YbQueryDiagnostics_post_parse_analyze(ParseState *pstate, Query *query,
												  JumbleState *jstate);
static void YbQueryDiagnostics_ExecutorStart(QueryDesc *queryDesc, int eflags);
static void YbQueryDiagnostics_ExecutorEnd(QueryDesc *queryDesc);

static void InsertNewBundleInfo(YbQueryDiagnosticsMetadata *metadata);
static void FetchParams(YbQueryDiagnosticsParams *params, FunctionCallInfo fcinfo);
static void ConstructDiagnosticsPath(YbQueryDiagnosticsMetadata *metadata);
static void FormatBindVariables(StringInfo buf, const ParamListInfo params);
static void FormatConstants(StringInfo constants, const char *query, int constants_count,
							int query_location, LocationLen *constant_locations);
static int DumpToFile(const char *path, const char *file_name, const char *data, char *description);
static void FlushAndCleanBundles();
static void AccumulateExplain(QueryDesc *queryDesc, YbQueryDiagnosticsEntry *entry,
							  bool explain_analyze, bool explain_dist, double totaltime_ms);
static void YbQueryDiagnosticsBgWorkerSighup(SIGNAL_ARGS);
static void YbQueryDiagnosticsBgWorkerSigterm(SIGNAL_ARGS);
static inline TimestampTz BundleEndTime(const YbQueryDiagnosticsEntry *entry);
static int YbQueryDiagnosticsBundlesShmemSize(void);
static Datum CreateJsonb(const YbQueryDiagnosticsParams *params);
static void CreateJsonbInt(JsonbParseState *state, char *key, int64 value);
static void CreateJsonbBool(JsonbParseState *state, char *key, bool value);
static void InsertBundlesIntoView(const YbQueryDiagnosticsMetadata *metadata, int status,
								  const char *description);
static void OutputBundle(const YbQueryDiagnosticsMetadata metadata, const char *description,
						 const char *status, Tuplestorestate *tupstore, TupleDesc tupdesc);
static void ProcessActiveBundles(Tuplestorestate *tupstore, TupleDesc tupdesc);
static void ProcessCompletedBundles(Tuplestorestate *tupstore, TupleDesc tupdesc);
static inline int CircularBufferMaxEntries(void);
static void RemoveBundleInfo(int64 query_id);
static int MakeDirectory(const char *path, char *description);
static void FinishBundleProcessing(YbQueryDiagnosticsMetadata *metadata,
								  int status, const char *description);
static int DumpBufferIfHalfFull(char *buffer, size_t max_len, const char *file_name,
								 const char *folder_path, char *description, slock_t *mutex);

/* Function used in gathering bind_variables/constants data */
static void AccumulateBindVariablesOrConstants(YbQueryDiagnosticsEntry *entry, StringInfo params);

/* Functions used in gathering pg_stat_statements */
static void PgssToString(int64 query_id, char *pgss_str, YbQueryDiagnosticsPgss pgss,
						 const char *queryString);
static void AccumulatePgss(QueryDesc *queryDesc, YbQueryDiagnosticsEntry *result);

/* Functions used in gathering schema details */
static void FetchSchemaOids(List *rtable, Oid *schema_oids);
static bool ExecuteQuery(StringInfo schema_details, const char *query,
						 const char *title, char *description);
static int DescribeOneTable(Oid oid, StringInfo schema_details, char *description);
static void PrintTableName(Oid oid, StringInfo schema_details);

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
	size = add_size(size, mul_size(CircularBufferMaxEntries(), sizeof(BundleInfo)));

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
	size = add_size(size, hash_estimate_size(QUERY_DIAGNOSTICS_HASH_MAX_SIZE,
													sizeof(YbQueryDiagnosticsEntry)));
	size = add_size(size, YbQueryDiagnosticsBundlesShmemSize());
	size = add_size(size, sizeof(TimestampTz));

	return size;
}

/*
 * YbQueryDiagnosticsShmemInit
 *		Allocate and initialize QueryDiagnostics-related shared memory
 */
void
YbQueryDiagnosticsShmemInit(void)
{
	HASHCTL 	ctl;
	bool 		found;

	bundles_in_progress = NULL;
	/* Initialize the hash table control structure */
	MemSet(&ctl, 0, sizeof(ctl));

	/* Set the key size and entry size */
	ctl.keysize = sizeof(int64);
	ctl.entrysize = sizeof(YbQueryDiagnosticsEntry);

	/* Create the hash table in shared memory */
	bundles_in_progress_lock = (LWLock *)ShmemInitStruct("YbQueryDiagnostics Lock",
														  sizeof(LWLock), &found);

	if (!found)
	{
		/* First time through ... */
		LWLockInitialize(bundles_in_progress_lock,
						 LWTRANCHE_YB_QUERY_DIAGNOSTICS);
	}

	bundles_in_progress = ShmemInitHash("YbQueryDiagnostics shared hash table",
											  QUERY_DIAGNOSTICS_HASH_MAX_SIZE,
											  QUERY_DIAGNOSTICS_HASH_MAX_SIZE,
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

		MemSet(bundles_completed->bundles, 0, sizeof(BundleInfo) * bundles_completed->max_entries);

		LWLockInitialize(&bundles_completed->lock,
						 LWTRANCHE_YB_QUERY_DIAGNOSTICS_CIRCULAR_BUFFER);
	}

	yb_pgss_last_reset_time = (TimestampTz *) ShmemAlloc(sizeof(TimestampTz));
	(*yb_pgss_last_reset_time) = 0;
}

static inline int
CircularBufferMaxEntries(void)
{
	return yb_query_diagnostics_circular_buffer_size * 1024 / sizeof(BundleInfo);
}

/*
 * InsertCompletedBundleInfo
 * 		Add a query diagnostics entry to the circular buffer.
 */
static void
InsertBundlesIntoView(const YbQueryDiagnosticsMetadata *metadata, int status,
					  const char *description)
{
	BundleInfo *sample;

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
	if (!YBIsQueryDiagnosticsEnabled())
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("TEST_yb_enable_query_diagnostics gflag must be true")));

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
	HASH_SEQ_STATUS	status;
	YbQueryDiagnosticsEntry *entry;

	LWLockAcquire(bundles_in_progress_lock, LW_SHARED);

	hash_seq_init(&status, bundles_in_progress);

	while ((entry = hash_seq_search(&status)) != NULL)
		OutputBundle(entry->metadata, "",
					 status_msg[YB_DIAGNOSTICS_IN_PROGRESS], tupstore, tupdesc);

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
		BundleInfo *sample= &bundles_completed->bundles[i];

		if (sample->metadata.params.query_id != 0)
			OutputBundle(sample->metadata, sample->description,
						 status_msg[sample->status], tupstore, tupdesc);
	}

	LWLockRelease(&bundles_completed->lock);
}

static void
YbQueryDiagnosticsBgWorkerSighup(SIGNAL_ARGS)
{
	int			save_errno = errno;

	got_sighup = true;
	SetLatch(MyLatch);

	errno = save_errno;
}

static void
YbQueryDiagnosticsBgWorkerSigterm(SIGNAL_ARGS)
{
	int			save_errno = errno;

	got_sigterm = true;
	SetLatch(MyLatch);

	errno = save_errno;
}

/*
 * YbQueryDiagnosticsBgWorkerRegister
 *		Register the background worker for yb_query_diagnostics
 *
 * Background worker is required to periodically check for expired entries
 * within the shared hash table and stop the query diagnostics for them.
 */
void
YbQueryDiagnosticsBgWorkerRegister(void)
{
	BackgroundWorker worker;
	MemSet(&worker, 0, sizeof(worker));
	sprintf(worker.bgw_name, "yb_query_diagnostics bgworker");
	sprintf(worker.bgw_type, "yb_query_diagnostics bgworker");
	worker.bgw_flags = BGWORKER_SHMEM_ACCESS | BGWORKER_BACKEND_DATABASE_CONNECTION;
	worker.bgw_start_time = BgWorkerStart_RecoveryFinished;
	/* Value of 1 allows the background worker for yb_query_diagnostics to restart */
	worker.bgw_restart_time = 1;
	sprintf(worker.bgw_library_name, "postgres");
	sprintf(worker.bgw_function_name, "YbQueryDiagnosticsMain");
	worker.bgw_main_arg = (Datum) 0;
	worker.bgw_notify_pid = 0;
	RegisterBackgroundWorker(&worker);
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
	 * Worst case : O(QUERY_DIAGNOSTICS_HASH_MAX_SIZE)
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

			SpinLockAcquire(&entry->mutex);

			entry->query_location = query_location;
			entry->constants_count = Min(jstate->clocations_count, YB_QD_MAX_CONSTANTS);
			memcpy(entry->constant_locations, jstate->clocations,
				   sizeof(LocationLen) * entry->constants_count);

			SpinLockRelease(&entry->mutex);
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
	 * Worst case : O(QUERY_DIAGNOSTICS_HASH_MAX_SIZE)
	 */
	entry = (YbQueryDiagnosticsEntry *) hash_search(bundles_in_progress,
													&query_id, HASH_FIND,
													NULL);

	if (entry)
	{
		double 		totaltime_ms;

		totaltime_ms = INSTR_TIME_GET_MILLISEC(queryDesc->totaltime->counter);

		if (entry->metadata.params.bind_var_query_min_duration_ms <= totaltime_ms)
		{
			StringInfoData buf;
			initStringInfo(&buf);

			/* For prepared statements, format bind variables in a CSV format */
			if (queryDesc->params)
				FormatBindVariables(&buf, queryDesc->params);

			/* For non-prepared statements, format constants in a CSV format. */
			if (entry->constants_count > 0)
				FormatConstants(&buf, queryDesc->sourceText,
								entry->constants_count,
								entry->query_location,
								entry->constant_locations);

			appendStringInfo(&buf, ",%lf ms\n", totaltime_ms);
			AccumulateBindVariablesOrConstants(entry, &buf);

			pfree(buf.data);
		}

		AccumulatePgss(queryDesc, entry);

		if (current_query_sampled)
			AccumulateExplain(queryDesc, entry,
							  entry->metadata.params.explain_analyze,
							  entry->metadata.params.explain_dist, totaltime_ms);

		/* Fetch schema details only if it is not already fetched */
		if (entry->schema_oids[0] == 0)
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
		entry->bind_vars[current_len + params->len] = '\0'; /* Ensure null termination */
	}

	SpinLockRelease(&entry->mutex);
}

static void
AccumulatePgss(QueryDesc *queryDesc, YbQueryDiagnosticsEntry *entry)
{
	double totaltime_ms = INSTR_TIME_GET_DOUBLE(queryDesc->totaltime->counter) * 1000;
	int64 rows = queryDesc->estate->es_processed;
	BufferUsage *bufusage = &queryDesc->totaltime->bufusage;

	SpinLockAcquire(&entry->mutex);
	entry->pgss.counters.calls++;
	entry->pgss.counters.total_time += totaltime_ms;
	entry->pgss.counters.rows += queryDesc->estate->es_processed;

	if (entry->pgss.counters.calls == 1)
	{
		entry->pgss.counters.min_time = totaltime_ms;
		entry->pgss.counters.max_time = totaltime_ms;
		entry->pgss.counters.mean_time = totaltime_ms;
	}
	else
	{
		double old_mean = entry->pgss.counters.mean_time;
		/*
		 * 'calls' cannot be 0 here because
		 * it is initialized to 0 and incremented by calls++ above
		 */
		entry->pgss.counters.mean_time += (totaltime_ms - old_mean) / entry->pgss.counters.calls;
		entry->pgss.counters.sum_var_time += (totaltime_ms - old_mean) *
											  (totaltime_ms - entry->pgss.counters.mean_time);
		if (entry->pgss.counters.min_time > totaltime_ms)
			entry->pgss.counters.min_time = totaltime_ms;
		if (entry->pgss.counters.max_time < totaltime_ms)
			entry->pgss.counters.max_time = totaltime_ms;
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
	SpinLockRelease(&entry->mutex);
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
			"queryid,query,calls,total_time,min_time,max_time,mean_time,stddev_time,rows,"
			"shared_blks_hit,shared_blks_read,shared_blks_dirtied,shared_blks_written,"
			"local_blks_hit,local_blks_read,local_blks_dirtied,local_blks_written,"
			"temp_blks_read,temp_blks_written,blk_read_time,blk_write_time\n"
			"%ld,\"%s\",%ld,%lf,%lf,%lf,%lf,%lf,%ld,%ld,%ld,%ld,"
			"%ld,%ld,%ld,%ld,%ld,%ld,%ld,%lf,%lf\n",
			query_id, query_str, pgss.counters.calls,
			pgss.counters.total_time, pgss.counters.min_time, pgss.counters.max_time,
			pgss.counters.mean_time, sqrt(pgss.counters.sum_var_time / pgss.counters.calls),
			pgss.counters.rows, pgss.counters.shared_blks_hit, pgss.counters.shared_blks_read,
			pgss.counters.shared_blks_dirtied, pgss.counters.shared_blks_written,
			pgss.counters.local_blks_hit, pgss.counters.local_blks_read,
			pgss.counters.local_blks_dirtied, pgss.counters.local_blks_written,
			pgss.counters.temp_blks_read, pgss.counters.temp_blks_written,
			pgss.counters.blk_read_time, pgss.counters.blk_write_time);
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
	es->format = EXPLAIN_FORMAT_TEXT;
	es->rpc = (es->analyze && explain_dist);

	/* Note: this part of code comes from auto_explain.c */
	ExplainPrintPlan(es, queryDesc);
	if (es->analyze)
		ExplainPrintTriggers(es, queryDesc);
	if (es->costs)
		ExplainPrintJITSummary(es, queryDesc);

	/* Removes the trailing newline that might be introduced by ExplainPrintJITSummary */
	if (es->str->len > 0 && es->str->data[es->str->len - 1] == '\n')
		es->str->data[--es->str->len] = '\0';

	SpinLockAcquire(&entry->mutex);

	/* TODO(GH#23720): Add support for handling oversized explain plans */
	int remaining_space = sizeof(entry->explain_plan) - strlen(entry->explain_plan) - 1;
	if (remaining_space > 0)
		snprintf(entry->explain_plan + strlen(entry->explain_plan), remaining_space,
				 "duration: %.3f ms\nplan:\n%s\n\n", totaltime_ms, es->str->data);

	SpinLockRelease(&entry->mutex);

	pfree(es->str->data);
	pfree(es->str);
	pfree(es);
}

/*
 * InsertNewBundleInfo
 *		Adds the entry into bundles_in_progress hash table.
 *		Entry is inserted only if it is not already present,
 *		otherwise an error is raised.
 */
static void
InsertNewBundleInfo(YbQueryDiagnosticsMetadata *metadata)
{
	int64		key = metadata->params.query_id;
	bool		found;
	YbQueryDiagnosticsEntry *entry;

	LWLockAcquire(bundles_in_progress_lock, LW_EXCLUSIVE);
	entry = (YbQueryDiagnosticsEntry *) hash_search(bundles_in_progress, &key,
													HASH_ENTER, &found);

	if (!found)
	{
		entry->metadata = *metadata;
		entry->metadata.directory_created = false;
		MemSet(entry->bind_vars, 0, YB_QD_MAX_BIND_VARS_LEN);
		MemSet(entry->explain_plan, 0, YB_QD_MAX_EXPLAIN_PLAN_LEN);
		MemSet(entry->schema_oids, 0, sizeof(Oid) * YB_QD_MAX_SCHEMA_OIDS);
		entry->query_location = 0;
		entry->constants_count = 0;
		MemSet(entry->constant_locations, 0, sizeof(LocationLen) * YB_QD_MAX_CONSTANTS);
		SpinLockInit(&entry->mutex);
		entry->pgss = (YbQueryDiagnosticsPgss) {.counters = {0}, .query_offset = 0, .query_len = 0};
	}

	LWLockRelease(bundles_in_progress_lock);

	if (found)
		ereport(ERROR,
				(errcode(ERRCODE_DUPLICATE_OBJECT),
				 errmsg("Query diagnostics for %ld, is already being generated",
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
	return entry->metadata.start_time +
		   (entry->metadata.params.diagnostics_interval_sec * USECS_PER_SEC);
}

void
AppendToDescription(char *description, const char *format, ...)
{
	int			current_len = strlen(description);
	va_list		args;
	int			remaining_len = YB_QD_DESCRIPTION_LEN - current_len - 1; /* -1 for '\0' */

	if (remaining_len <= 0)
		return;

	va_start(args, format);
	vsnprintf(description + current_len, remaining_len, format, args);
	va_end(args);
}

/*
 * RefreshCache
 *		Refreshes the catalog cache to ensure that the latest catalog version is used.
 */
static void
RefreshCache()
{
	YBCPgResetCatalogReadTime();
	YbUpdateCatalogCacheVersion(YbGetMasterCatalogVersion());
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
	MemoryContext curr_context = GetCurrentMemoryContext();
	int			expired_entries_index = 0;
	YbQueryDiagnosticsEntry *expired_entries = palloc0(
		sizeof(YbQueryDiagnosticsEntry) * QUERY_DIAGNOSTICS_HASH_MAX_SIZE);

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
		int			status = YB_DIAGNOSTICS_SUCCESS;
		bool		has_expired = current_time >= BundleEndTime(entry);

		/* Description of the warnings/errors that occurred */
		char		description[YB_QD_DESCRIPTION_LEN];
		description[0] = '\0';

		/* Since only the background worker accesses this variable, no lock is needed */
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
		int			status = YB_DIAGNOSTICS_SUCCESS;
		int			query_len = Min(entry->pgss.query_len, YB_QD_MAX_PGSS_QUERY_LEN);
		char		query_str[query_len + 1];

		description[0] = '\0';
		query_str[0] = '\0';

		/* No queries were executed that needed to be bundled */
		if (entry->pgss.query_len == 0)
		{
			AppendToDescription(description, "No query executed;");
			goto remove_entry;
		}

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
			AppendToDescription(description, "pg_stat_statements was reset, " \
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
						AppendToDescription(description, "Error fetching "
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
				ErrorData *edata;

				/* save error info */
				MemoryContextSwitchTo(curr_context);
				edata = CopyErrorData();
				FlushErrorState();

				AppendToDescription(description,
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

		/* Collect schema details */
		StringInfoData schema_details;
		initStringInfo(&schema_details);

		PG_TRY();
		{
			/* This prevents `CATALOG MISMATCHED_SCHEMA` error */
			RefreshCache();

			/* Fetch schema details for each schema oid */
			for (int i = 0; i < YB_QD_MAX_SCHEMA_OIDS; i++)
			{
				if (entry->schema_oids[i] == InvalidOid)
					break;

				status = DescribeOneTable(entry->schema_oids[i],
										  &schema_details, description);

				if (status == YB_DIAGNOSTICS_ERROR)
					goto remove_entry;

				appendStringInfo(&schema_details, "\n%s\n\n",
								 schema_details_separator);
			}
		}
		PG_CATCH();
		{
			/*
			 * Not setting status = YB_DIAGNOSTICS_ERROR,
			 * so that rest of the data can be printed
			 */
			ErrorData *edata;

			/* save error info */
			MemoryContextSwitchTo(curr_context);
			edata = CopyErrorData();
			FlushErrorState();

			AppendToDescription(description,
								"Fetching schema details errored out "
								"with the following message: %s",
								edata->message);

			ereport(LOG,
					(errmsg("error while dumping schema details %s\n%s",
							edata->message, YBCGetStackTrace())),
					(errhint("%s", edata->hint)));

			FreeErrorData(edata);
		}
		PG_END_TRY();

		/* Dump schema details */
		status = DumpToFile(entry->metadata.path, schema_details_file,
							schema_details.data, description);

		pfree(schema_details.data);

		if (status == YB_DIAGNOSTICS_ERROR)
			goto remove_entry;

		/* Dump ASH */
		if (yb_enable_ash)
		{
			StringInfoData ash_buffer;
			initStringInfo(&ash_buffer);

			GetAshDataForQueryDiagnosticsBundle(entry->metadata.start_time,
												BundleEndTime(entry),
												entry->metadata.params.query_id,
												&ash_buffer, description);

			status = DumpToFile(entry->metadata.path, ash_file,
								ash_buffer.data, description);

			pfree(ash_buffer.data);

			if (status == YB_DIAGNOSTICS_ERROR)
				goto remove_entry;
		}

remove_entry:
		FinishBundleProcessing(&entry->metadata, status, description);
	}

	pfree(expired_entries);
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
	int			status = YB_DIAGNOSTICS_SUCCESS;

	SpinLockAcquire(mutex);

	int buffer_len = strlen(buffer);
	if (buffer_len >= max_len / 2)
	{
		char	   *buffer_copy = palloc(buffer_len + 1);

		memcpy(buffer_copy, buffer, buffer_len + 1);
		buffer[0] = '\0'; /* Reset the buffer */

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
FinishBundleProcessing(YbQueryDiagnosticsMetadata *metadata, int status, const char *description)
{
	/* Insert the completed bundle info into the bundles_completed circular buffer */
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

	if (pg_mkdir_p((char *)path_copy, pg_dir_create_mode) == -1)
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
DescribeOneTable(Oid oid, StringInfo schema_details, char *description)
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

	PrintTableName(oid, schema_details);

	for (int i = 0; i < num_of_queries; i++)
	{
		snprintf(formatted_query, sizeof(formatted_query), queries[i], oid);

		if (!ExecuteQuery(schema_details, formatted_query, titles[i], description))
			return YB_DIAGNOSTICS_ERROR;
	}

	return YB_DIAGNOSTICS_SUCCESS;
}

static void
PrintTableName(Oid oid, StringInfo schema_details)
{
	StartTransactionCommand();

	char		*table_name = get_rel_name(oid);

	if (table_name)
	{
		appendStringInfo(schema_details, "Table name: %s\n", table_name);
		pfree(table_name);
	}

	CommitTransactionCommand();
}

/*
 * ExecuteQuery
 *		Uses SPI framework to execute the given query and appends the results to schema_details.
 *		The results are formatted as a table with a title.
 *
 *		Returns true if the query was executed successfully, false in case of an error.
 *		Error is copied to description.
 */
static bool
ExecuteQuery(StringInfo schema_details, const char *query, const char *title, char *description)
{
	int			ret;
	int			num_cols;
	int		   *col_widths;
	bool		is_result_empty = true;
	StringInfoData result;
	StringInfoData columns;

	initStringInfo(&result);
	initStringInfo(&columns);

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
		snprintf(description, YB_QD_DESCRIPTION_LEN,
				 "Failed to gather schema details. SPI_connect failed: %s",
				 SPI_result_code_string(ret));
		pgstat_report_activity(STATE_IDLE, NULL);
		return false;
	}

	/* Creates an "active" snapshot which is necessary for queries to have MVCC data to work on */
	PushActiveSnapshot(GetTransactionSnapshot());

	/* Make our activity visible through the pgstat views */
	pgstat_report_activity(STATE_RUNNING, query);

	/* Execute the query and get all of the rows from the result set */
	ret = SPI_execute(query, true /* read_only */, 0 /* tcount (0 = unlimited rows) */);
	if (ret != SPI_OK_SELECT)
	{
		snprintf(description, YB_QD_DESCRIPTION_LEN,
				 "Failed to gather schema details. SPI_execute failed: %s",
				 SPI_result_code_string(ret));
		pgstat_report_activity(STATE_IDLE, NULL);
		return false;
	}

	/* Calculate column widths for formatting */
	num_cols = SPI_tuptable->tupdesc->natts;
	col_widths = (int *) palloc0(num_cols * sizeof(int));

	/* Initialize column widths with column name lengths */
	for (int i = 0; i < num_cols; i++)
	{
		Form_pg_attribute attr = TupleDescAttr(SPI_tuptable->tupdesc, i);
		col_widths[i] = strlen(NameStr(attr->attname));
	}

	/* Adjust column widths based on data */
	for (uint64 j = 0; j < SPI_processed; j++)
	{
		HeapTuple tuple = SPI_tuptable->vals[j];

		for (int i = 0; i < num_cols; i++)
		{
			char	   *val = SPI_getvalue(tuple, SPI_tuptable->tupdesc, i + 1);
			int			val_len = val ? strlen(val) : 0; /* Empty string for NULL */

			col_widths[i] = Max(col_widths[i], val_len);

			if (val)
				pfree(val);
		}
	}

	/* Format column names */
	appendStringInfoChar(&columns, '|');
	for (int i = 0; i < num_cols; i++)
	{
		Form_pg_attribute attr = TupleDescAttr(SPI_tuptable->tupdesc, i);
		appendStringInfo(&columns, "%-*s |", col_widths[i], NameStr(attr->attname));
	}

	appendStringInfoChar(&columns, '\n');

	/* Add separator line */
	appendStringInfoChar(&columns, '+');
	for (int i = 0; i < num_cols; i++)
	{
		for (int j = 0; j <= col_widths[i]; j++)
		{
			appendStringInfoChar(&columns, '-');
		}
		appendStringInfoChar(&columns, '+');
	}

	/* Format rows */
	for (uint64 j = 0; j < SPI_processed; j++)
	{
		HeapTuple tuple = SPI_tuptable->vals[j];

		appendStringInfoChar(&result, '|');
		for (int i = 0; i < num_cols; i++)
		{
			char	   *val = SPI_getvalue(tuple, SPI_tuptable->tupdesc, i + 1);

			appendStringInfo(&result, "%-*s |", col_widths[i], val ? val : "");
			if (val)
			{
				is_result_empty = false;
				pfree(val);
			}
		}
		appendStringInfoChar(&result, '\n');
	}

	/* Append formatted results to schema_details if not empty */
	if (!is_result_empty)
		appendStringInfo(schema_details, "- %s:\n%s\n%s\n", title, columns.data, result.data);

	/* Clean up */
	pfree(result.data);
	pfree(columns.data);
	pfree(col_widths);

	/* Close the SPI connection and clean up the transaction context */
	ret = SPI_finish();
	if (ret != SPI_OK_FINISH)
	{
		snprintf(description, YB_QD_DESCRIPTION_LEN,
				 "Failed to gather schema details. SPI_finish failed: %s",
				 SPI_result_code_string(ret));
		pgstat_report_activity(STATE_IDLE, NULL);
		return false;
	}

	PopActiveSnapshot();
	CommitTransactionCommand();
	pgstat_report_stat(true);
	pgstat_report_activity(STATE_IDLE, NULL);
	return true;
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
	MemoryContext curr_context = GetCurrentMemoryContext();

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

		else if(FileWrite(file, (char *)data, strlen(data), FileSize(file),
						  WAIT_EVENT_DATA_FILE_WRITE) < 0)
			snprintf(description, YB_QD_DESCRIPTION_LEN, "Error writing to file; %s",
					 strerror(errno));

		else
			ok = true;
	}
	PG_CATCH();
	{
		ErrorData *edata;

		/* save the error data */
		MemoryContextSwitchTo(curr_context);
		edata = CopyErrorData();
		FlushErrorState();

		snprintf(description, YB_QD_DESCRIPTION_LEN, "%s", edata->message);

		ereport(LOG, (errmsg("error while dumping to file %s", edata->message)),
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
	/*
	 * TODO(GH#22612): Add support to switch off and on the bgworker as per the need,
	 *			       thereby saving resources
	 */
	ereport(LOG,
			(errmsg("starting bgworker for yb_query_diagnostics with time interval of %dms",
					 yb_query_diagnostics_bg_worker_interval_ms)));

	/* Register functions for SIGTERM/SIGHUP management */
	pqsignal(SIGHUP, YbQueryDiagnosticsBgWorkerSighup);
	pqsignal(SIGTERM, YbQueryDiagnosticsBgWorkerSigterm);

	/* Initialize the worker process */
	BackgroundWorkerUnblockSignals();

	/* Connect to the database */
	BackgroundWorkerInitializeConnection("yugabyte", NULL, 0);

	pgstat_report_appname("yb_query_diagnostics bgworker");

	while (!got_sigterm)
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

		/* Process signals */
		if (got_sighup)
		{
			/* Process config file */
			got_sighup = false;
			ProcessConfigFile(PGC_SIGHUP);
			ereport(LOG,
					(errmsg("bgworker yb_query_diagnostics signal: processed SIGHUP")));
		}

		/* Check for expired entries within the shared hash table */
		FlushAndCleanBundles();
	}

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
	uint32		rand_num =DatumGetUInt32(hash_any((unsigned char*)&metadata->start_time,
										   sizeof(metadata->start_time)));
#ifdef WIN32
	const char *format = "%s\\%s\\%ld\\%ud\\";
#else
	const char *format = "%s/%s/%ld/%ud/";
#endif
	if (snprintf(metadata->path, MAXPGPATH, format,
				 DataDir, "query-diagnostics", metadata->params.query_id, rand_num) >= MAXPGPATH)
		ereport(ERROR,
				(errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
				 errmsg("Path to pg_data is too long"),
				 errhint("Move the data directory to a shorter path")));
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
	if (!YBIsQueryDiagnosticsEnabled())
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("query diagnostics is not enabled"),
				 errhint("Set TEST_yb_enable_query_diagnostics gflag to true")));

	YbQueryDiagnosticsMetadata metadata;
	metadata.start_time = GetCurrentTimestamp();

	FetchParams(&metadata.params, fcinfo);

	ConstructDiagnosticsPath(&metadata);

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
	if (!YBIsQueryDiagnosticsEnabled())
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("query diagnostics is not enabled"),
				 errhint("Set TEST_yb_enable_query_diagnostics gflag to true")));

	int64			query_id = PG_GETARG_INT64(0);
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
