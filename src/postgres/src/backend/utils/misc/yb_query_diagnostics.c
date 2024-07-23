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

#include "access/hash.h"
#include "common/file_perm.h"
#include "common/pg_yb_common.h"
#include "funcapi.h"
#include "pgstat.h"
#include "postmaster/bgworker.h"
#include "storage/ipc.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"

#define QUERY_DIAGNOSTICS_HASH_MAX_SIZE 100	/* Maximum number of entries in the hash table */

/* GUC variables */
int yb_query_diagnostics_bg_worker_interval_ms;

/* Saved hook value in case of unload */
static ExecutorEnd_hook_type prev_ExecutorEnd = NULL;

/* Flags set by interrupt handlers for later service in the main loop. */
static volatile sig_atomic_t got_sigterm = false;
static volatile sig_atomic_t got_sighup = false;

static HTAB *yb_query_diagnostics_hash = NULL;
static LWLock *yb_query_diagnostics_lock; /* protects yb_query_diagnostics_hash */

static void YbQueryDiagnostics_ExecutorEnd(QueryDesc *queryDesc);

static void InsertIntoSharedHashTable(YbQueryDiagnosticsParameters params,
									 TimestampTz start_time, const char *path);
static void FetchParams(YbQueryDiagnosticsParameters *params, FunctionCallInfo fcinfo);
static void ConstructDiagnosticsPath(int64 query_id, TimestampTz current_time, char *path);
static void FormatParams(StringInfo buf, const ParamListInfo params);
static bool DumpToFile(const char *path, const char *file_name, const char *data);
static void RemoveExpiredEntries();
static void AccumulateBindVariables(int64 query_id,	YbQueryDiagnosticsEntry *entry,
									const double totaltime_ms, const ParamListInfo params);
static void YbQueryDiagnosticsBgWorkerSighup(SIGNAL_ARGS);
static void YbQueryDiagnosticsBgWorkerSigterm(SIGNAL_ARGS);
static inline bool HasBundleExpired(const YbQueryDiagnosticsEntry *entry, TimestampTz current_time);

void
YbQueryDiagnosticsInstallHook(void)
{
	prev_ExecutorEnd = ExecutorEnd_hook;
	ExecutorEnd_hook = YbQueryDiagnostics_ExecutorEnd;
}

/*
 * YbQueryDiagnosticsShmemSize
 *		Compute space needed for QueryDiagnostics-related shared memory
 */
Size
YbQueryDiagnosticsShmemSize(void)
{
	Size        size;

	size = MAXALIGN(sizeof(LWLock));
	size = add_size(size, hash_estimate_size(QUERY_DIAGNOSTICS_HASH_MAX_SIZE,
													sizeof(YbQueryDiagnosticsEntry)));

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

	yb_query_diagnostics_hash = NULL;
	/* Initialize the hash table control structure */
	memset(&ctl, 0, sizeof(ctl));

	/* Set the key size and entry size */
	ctl.keysize = sizeof(int64);
	ctl.entrysize = sizeof(YbQueryDiagnosticsEntry);

	/* Create the hash table in shared memory */
	LWLockAcquire(AddinShmemInitLock, LW_EXCLUSIVE);
	yb_query_diagnostics_lock = (LWLock *)ShmemInitStruct("YbQueryDiagnostics Lock",
														  sizeof(LWLock), &found);

	if (!found)
	{
		/* First time through ... */
		LWLockInitialize(yb_query_diagnostics_lock,
						 LWTRANCHE_YB_QUERY_DIAGNOSTICS);
	}
	yb_query_diagnostics_hash = ShmemInitHash("YbQueryDiagnostics shared hash table",
											  QUERY_DIAGNOSTICS_HASH_MAX_SIZE,
											  QUERY_DIAGNOSTICS_HASH_MAX_SIZE,
											  &ctl,
											  HASH_ELEM | HASH_BLOBS);

	LWLockRelease(AddinShmemInitLock);
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
	memset(&worker, 0, sizeof(worker));
	sprintf(worker.bgw_name, "yb_query_diagnostics bgworker");
	sprintf(worker.bgw_type, "yb_query_diagnostics bgworker");
	worker.bgw_flags = BGWORKER_SHMEM_ACCESS;
	worker.bgw_start_time = BgWorkerStart_PostmasterStart;
	/* Value of 1 allows the background worker for yb_query_diagnostics to restart */
	worker.bgw_restart_time = 1;
	sprintf(worker.bgw_library_name, "postgres");
	sprintf(worker.bgw_function_name, "YbQueryDiagnosticsMain");
	worker.bgw_main_arg = (Datum) 0;
	worker.bgw_notify_pid = 0;
	RegisterBackgroundWorker(&worker);
}

static void
YbQueryDiagnostics_ExecutorEnd(QueryDesc *queryDesc)
{
	uint64		query_id = queryDesc->plannedstmt->queryId;
	double 		totaltime_ms;
	YbQueryDiagnosticsEntry *entry;

	LWLockAcquire(yb_query_diagnostics_lock, LW_SHARED);

	/*
	 * This can slow down the query execution, even if the query is not being bundled.
	 */
	entry = (YbQueryDiagnosticsEntry *) hash_search(yb_query_diagnostics_hash,
													&query_id, HASH_FIND,
													NULL);

	if (entry)
	{
		totaltime_ms = INSTR_TIME_GET_MILLISEC(queryDesc->totaltime->counter);

		if (queryDesc->params && entry->params.bind_var_query_min_duration_ms <= totaltime_ms)
			AccumulateBindVariables(query_id, entry, totaltime_ms, queryDesc->params);
	}

	LWLockRelease(yb_query_diagnostics_lock);

	if (prev_ExecutorEnd)
		prev_ExecutorEnd(queryDesc);
	else
		standard_ExecutorEnd(queryDesc);
}

static void
AccumulateBindVariables(int64 query_id,	YbQueryDiagnosticsEntry *entry,
						const double totaltime_ms, const ParamListInfo params)
{
	/* TODO(GH#22153): Handle the case when entry->bind_vars overflows */

	/* Check if the bind_vars is already full */
	SpinLockAcquire(&entry->mutex);
	bool is_full = strlen(entry->bind_vars) == YB_QD_MAX_BIND_VARS_LEN - 1;
	SpinLockRelease(&entry->mutex);

	if (is_full)
		return;

	StringInfoData buf;
	initStringInfo(&buf);
	FormatParams(&buf, params);
	appendStringInfo(&buf, "%lf\n", totaltime_ms);

	SpinLockAcquire(&entry->mutex);
	if (strlen(entry->bind_vars) + buf.len < YB_QD_MAX_BIND_VARS_LEN)
		memcpy(entry->bind_vars + strlen(entry->bind_vars), buf.data, buf.len);
	SpinLockRelease(&entry->mutex);

	pfree(buf.data);
}

/*
 * FormatParams
 *		Iterates over all of the params and prints them in CSV fromat.
 */
static void
FormatParams(StringInfo buf, const ParamListInfo params)
{
	MemoryContext oldcxt = CurrentMemoryContext;
	MemoryContext cxt = AllocSetContextCreate(CurrentMemoryContext,
													 "FormatParams temporary context",
													 ALLOCSET_DEFAULT_SIZES);

	MemoryContextSwitchTo(cxt);
	for (int i = 0; i < params->numParams; ++i)
	{
		if (params->params[i].isnull)
			appendStringInfo(buf, "NULL");
		else
		{
			Oid			typoutput;
			bool		typIsVarlena;
			char	   *val;

			getTypeOutputInfo(params->params[i].ptype,
							  &typoutput, &typIsVarlena);
			val = OidOutputFunctionCall(typoutput, params->params[i].value);

			appendStringInfo(buf, "%s,", val);
		}
	}

	MemoryContextSwitchTo(oldcxt);
	MemoryContextDelete(cxt);
}

/*
 * InsertIntoSharedHashTable
 *		Adds the entry into yb_query_diagnostics_hash.
 *		Entry is inserted only if it is not already present,
 *		otherwise an error is raised.
 */
static void
InsertIntoSharedHashTable(YbQueryDiagnosticsParameters params,
						  TimestampTz start_time, const char *path)
{
	int64		key = params.query_id;
	bool		found;
	YbQueryDiagnosticsEntry *entry;

	LWLockAcquire(yb_query_diagnostics_lock, LW_EXCLUSIVE);
	entry = (YbQueryDiagnosticsEntry *) hash_search(yb_query_diagnostics_hash, &key,
													HASH_ENTER, &found);

	if (!found)
	{
		entry->params = params;
		entry->start_time = start_time;
		memcpy(entry->path, path, MAXPGPATH);
		memset(entry->bind_vars, 0, YB_QD_MAX_BIND_VARS_LEN);
		SpinLockInit(&entry->mutex);
	}

	LWLockRelease(yb_query_diagnostics_lock);

	if (found)
		ereport(ERROR,
				(errcode(ERRCODE_DUPLICATE_OBJECT),
				 errmsg("Query diagnostics for query_id[ %ld ] is already being generated",
						params.query_id)));
}

/*
 * HasBundleExpired
 * 		Checks if the diagnostics bundle has expired.
 * 		note: since TimestampTz is equivalent to microsecond,
 * 		diagnostics_interval is converted to microseconds before adding to start_time.
 */
static inline bool
HasBundleExpired(const YbQueryDiagnosticsEntry *entry, TimestampTz current_time)
{
	return current_time >= entry->start_time +
						   (entry->params.diagnostics_interval_sec * USECS_PER_SEC);
}

static void
RemoveExpiredEntries()
{
	/* TODO(GH#22447): Do this in O(1) */
	TimestampTz current_time = GetCurrentTimestamp();
	HASH_SEQ_STATUS status;
	YbQueryDiagnosticsEntry *entry;

	LWLockAcquire(yb_query_diagnostics_lock, LW_SHARED);
	/* Initialize the hash table scan */
	hash_seq_init(&status, yb_query_diagnostics_hash);

	/* Scan the hash table */
	while ((entry = hash_seq_search(&status)) != NULL)
	{
		if (HasBundleExpired(entry, current_time))
		{
			/*
			 * To avoid holding the lock while flushing to disk, we create a copy of the data
			 * that is to be dumped, this protects us from potential overwriting of the entry
			 * during the flushing process.
			 */
			SpinLockAcquire(&entry->mutex);

			char		bind_var_copy[YB_QD_MAX_BIND_VARS_LEN];
			int64		query_id_copy = entry->params.query_id;
			char		path_copy[MAXPGPATH];
			memcpy(bind_var_copy, entry->bind_vars, YB_QD_MAX_BIND_VARS_LEN);
			memcpy(path_copy, entry->path, MAXPGPATH);

			SpinLockRelease(&entry->mutex);

			/* release the shared lock before flushing to disk */
			LWLockRelease(yb_query_diagnostics_lock);

			if (!DumpToFile(path_copy, "bind_variables.csv", bind_var_copy))
				ereport(LOG,
						(errmsg("Could not dump bind variables for query_id[ %ld ]",
						 		query_id_copy)));

			LWLockAcquire(yb_query_diagnostics_lock, LW_EXCLUSIVE);

			hash_search(yb_query_diagnostics_hash, &query_id_copy,
						HASH_REMOVE, NULL);

			LWLockRelease(yb_query_diagnostics_lock);
			LWLockAcquire(yb_query_diagnostics_lock, LW_SHARED);
		}
	}
	LWLockRelease(yb_query_diagnostics_lock);
}

/*
 * DumpToFile
 *		Creates the file (/path/file_name) and writes the data to it.
 * Returns:
 *		true if no error, false otherwise.
 */
static bool
DumpToFile(const char *path, const char *file_name, const char *data)
{
	bool 		ok = true;
	File 		file = 0;
	const int	file_path_len = MAXPGPATH + strlen(file_name) + 1;
	char		file_path[file_path_len];

	/* No data to write */
	if (data[0] == '\0')
		return false;

	/* creates the directory structure recursively for this bundle */
	if (pg_mkdir_p((char *)path, pg_dir_create_mode) == -1 && errno != EEXIST)
	{
		ereport(LOG,
				(errcode_for_file_access(),
				 errmsg("Failed to create query diagnostics directory : %m")));
		return false;
	}
#ifdef WIN32
	snprintf(file_path, file_path_len, "%s\\%s", path, file_name);
#else
	snprintf(file_path, file_path_len, "%s/%s", path, file_name);
#endif

	/*
	 * We use PG_TRY to handle any function returning an error. This ensures that the entry
	 * can be safely removed from the hash table even if the file writing fails.
	 */
	PG_TRY();
	{
		if ((file = PathNameOpenFile(file_path,
										O_RDWR | O_CREAT | O_TRUNC)) < 0)
		{
			ereport(LOG,
					(errmsg("Error opening file %s: %m", file_path)));
			ok = false;
		}

		else if(FileWrite(file, (char *)data, strlen(data), 0,
							WAIT_EVENT_DATA_FILE_WRITE) < 0)
		{
			ereport(LOG,
					(errmsg("Error writing to file %s: %m", file_path)));
			ok = false;
		}
	}
	PG_CATCH();
	{
		ok = false;
	}
	PG_END_TRY();

	if (file > 0)
		FileClose(file);

	return ok;
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
			(errmsg("starting bgworker for yb_query_diagnostics with time interval of %ds",
					 yb_query_diagnostics_bg_worker_interval_ms)));

	/* Register functions for SIGTERM/SIGHUP management */
	pqsignal(SIGHUP, YbQueryDiagnosticsBgWorkerSighup);
	pqsignal(SIGTERM, YbQueryDiagnosticsBgWorkerSigterm);

	/* Initialize the worker process */
	BackgroundWorkerUnblockSignals();

	pgstat_report_appname("yb_query_diagnostics bgworker");

	while (!got_sigterm)
	{
		int			rc;
		/* Wait necessary amount of time */
		rc = WaitLatch(MyLatch,
					   WL_LATCH_SET | WL_TIMEOUT | WL_POSTMASTER_DEATH,
					   yb_query_diagnostics_bg_worker_interval_ms,
					   YB_WAIT_EVENT_QUERY_DIAGNOSTICS_MAIN);
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
		RemoveExpiredEntries();
	}
	proc_exit(0);
}

/*
 * ConstructDiagnosticsPath
 *		Creates the directory structure for storing the diagnostics data.
 *		Directory structure: pg_data/query-diagnostics/queryid/random_number/
 */
static void
ConstructDiagnosticsPath(int64 query_id, TimestampTz current_time, char *path)
{
	int rand_num = DatumGetUInt32(hash_any((unsigned char*)&current_time,
										   sizeof(current_time)));
#ifdef WIN32
    const char *format = "%s\\%s\\%ld\\%d\\";
#else
    const char *format = "%s/%s/%ld/%d/";
#endif
	if (snprintf(path, MAXPGPATH, format,
				 DataDir, "query-diagnostics", query_id, rand_num) >= MAXPGPATH)
		ereport(ERROR,
				(errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
				 errmsg("Path to pg_data is too long")));
}

/*
 * FetchParams
 *		Fetches the parameters from the yb_query_diagnostics function call and validates them.
 */
static void
FetchParams(YbQueryDiagnosticsParameters *params, FunctionCallInfo fcinfo)
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
				 errhint("set TEST_yb_enable_query_diagnostics gflag to true")));

	TimestampTz current_time = GetCurrentTimestamp();
	char 	 	path[MAXPGPATH] = "";
	YbQueryDiagnosticsParameters params;

	FetchParams(&params, fcinfo);

	ConstructDiagnosticsPath(params.query_id, current_time, path);

	InsertIntoSharedHashTable(params, current_time, path);

	PG_RETURN_TEXT_P(cstring_to_text(path));
}
