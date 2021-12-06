/*-------------------------------------------------------------------------
 *
 * pgsm_errors.c
 *		Track pg_stat_monitor internal error messages.
 *
 * Copyright © 2021, Percona LLC and/or its affiliates
 *
 * Portions Copyright (c) 1996-2020, PostgreSQL Global Development Group
 *
 * Portions Copyright (c) 1994, The Regents of the University of California
 *
 * IDENTIFICATION
 *	  contrib/pg_stat_monitor/pgsm_errors.c
 *
 *-------------------------------------------------------------------------
 */

#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include <postgres.h>
#include <access/hash.h>
#include <storage/shmem.h>
#include <utils/hsearch.h>

#include "pg_stat_monitor.h"
#include "pgsm_errors.h"


PG_FUNCTION_INFO_V1(pg_stat_monitor_errors);
PG_FUNCTION_INFO_V1(pg_stat_monitor_reset_errors);


/*
 * Maximum number of error messages tracked.
 * This should be set to a sensible value in order to track
 * the different type of errors that pg_stat_monitor may
 * report, e.g. out of memory.
 */
#define PSGM_ERRORS_MAX 128

static HTAB *pgsm_errors_ht = NULL;

void psgm_errors_init(void)
{
	HASHCTL info;
#if PG_VERSION_NUM >= 140000
	int flags = HASH_ELEM | HASH_STRINGS;
#else
	int flags = HASH_ELEM | HASH_BLOBS;
#endif


	memset(&info, 0, sizeof(info));
	info.keysize = ERROR_MSG_MAX_LEN;
	info.entrysize = sizeof(ErrorEntry);
	pgsm_errors_ht = ShmemInitHash("pg_stat_monitor: errors hashtable",
									PSGM_ERRORS_MAX,  /* initial size */
									PSGM_ERRORS_MAX,  /* maximum size */
									&info,
									flags);
}

size_t pgsm_errors_size(void)
{
    return hash_estimate_size(PSGM_ERRORS_MAX, sizeof(ErrorEntry));
}

void pgsm_log(PgsmLogSeverity severity, const char *format, ...)
{
	char key[ERROR_MSG_MAX_LEN];
	ErrorEntry *entry;
	bool found = false;
	va_list ap;
	int n;
	struct timeval tv;
	struct tm *lt;
	pgssSharedState *pgss;

	va_start(ap, format);
	n = vsnprintf(key, ERROR_MSG_MAX_LEN, format, ap);
	va_end(ap);

	if (n < 0)
		return;

	pgss = pgsm_get_ss();
	LWLockAcquire(pgss->errors_lock, LW_EXCLUSIVE);

	entry = (ErrorEntry *) hash_search(pgsm_errors_ht, key, HASH_ENTER_NULL, &found);
	if (!entry)
	{
		LWLockRelease(pgss->errors_lock);
		/* 
		* We're out of memory, can't track this error message.
		*/
		return;
	}

	if (!found)
	{
		entry->severity = severity;
		entry->calls = 0;
	}

	/* Update message timestamp. */
	gettimeofday(&tv, NULL);
	lt = localtime(&tv.tv_sec);
	snprintf(entry->time, sizeof(entry->time),
			"%04d-%02d-%02d %02d:%02d:%02d",
			lt->tm_year + 1900,
			lt->tm_mon + 1,
			lt->tm_mday,
			lt->tm_hour,
			lt->tm_min,
			lt->tm_sec);

	entry->calls++;

	LWLockRelease(pgss->errors_lock);
}

/*
 * Clear all entries from the hash table.
 */
Datum
pg_stat_monitor_reset_errors(PG_FUNCTION_ARGS)
{
	HASH_SEQ_STATUS 	hash_seq;
	ErrorEntry         *entry;
	pgssSharedState     *pgss = pgsm_get_ss();

	/* Safety check... */
	if (!IsSystemInitialized())
		ereport(ERROR,
			(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
			errmsg("pg_stat_monitor: must be loaded via shared_preload_libraries")));

	LWLockAcquire(pgss->errors_lock, LW_EXCLUSIVE);

	hash_seq_init(&hash_seq, pgsm_errors_ht);
	while ((entry = hash_seq_search(&hash_seq)) != NULL)
		entry = hash_search(pgsm_errors_ht, &entry->message, HASH_REMOVE, NULL);

	LWLockRelease(pgss->errors_lock);
	PG_RETURN_VOID();
}

/*
 * Invoked when users query the view pg_stat_monitor_errors.
 * This function creates tuples with error messages from data present in
 * the hash table, then return the dataset to the caller.
 */
Datum
pg_stat_monitor_errors(PG_FUNCTION_ARGS)
{
	ReturnSetInfo		*rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
	TupleDesc			tupdesc;
	Tuplestorestate		*tupstore;
	MemoryContext		per_query_ctx;
	MemoryContext		oldcontext;
	HASH_SEQ_STATUS     hash_seq;
	ErrorEntry          *error_entry;
	pgssSharedState     *pgss = pgsm_get_ss();

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

	if (tupdesc->natts != 4)
		elog(ERROR, "pg_stat_monitor: incorrect number of output arguments, required 3, found %d", tupdesc->natts);

	tupstore = tuplestore_begin_heap(true, false, work_mem);
	rsinfo->returnMode = SFRM_Materialize;
	rsinfo->setResult = tupstore;
	rsinfo->setDesc = tupdesc;

	MemoryContextSwitchTo(oldcontext);

	LWLockAcquire(pgss->errors_lock, LW_SHARED);

	hash_seq_init(&hash_seq, pgsm_errors_ht);
	while ((error_entry = hash_seq_search(&hash_seq)) != NULL)
	{
		Datum		values[4];
		bool		nulls[4];
		int			i = 0;
		memset(values, 0, sizeof(values));
		memset(nulls, 0, sizeof(nulls));

		values[i++] = Int64GetDatumFast(error_entry->severity);
		values[i++] = CStringGetTextDatum(error_entry->message);
		values[i++] = CStringGetTextDatum(error_entry->time);
		values[i++] = Int64GetDatumFast(error_entry->calls);

		tuplestore_putvalues(tupstore, tupdesc, values, nulls);
	}

	LWLockRelease(pgss->errors_lock);
	/* clean up and return the tuplestore */
	tuplestore_donestoring(tupstore);

	return (Datum)0;
}