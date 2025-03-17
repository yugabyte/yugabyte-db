/* -------------------------------------------------------------------------
 *
 * yb_terminated_queries.c
 *	  Implementation of YB terminated queries.
 *
 * This file contains the implementation of yb terminated queries. It is kept
 * separate from pgstat.c to enforce the line between the statistics access /
 * storage implementation and the details about individual types of
 * statistics.
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
 *	  src/backend/utils/activity/yb_terminated_queries.c
 * -------------------------------------------------------------------------
 */

#include "yb_terminated_queries.h"

#include "catalog/pg_authid.h"
#include "funcapi.h"
#include "nodes/execnodes.h"
#include "pg_yb_utils.h"
#include "storage/lwlock.h"
#include "storage/shmem.h"
#include "utils/acl.h"
#include "utils/backend_status.h"
#include "utils/builtins.h"

/* Caps the number of queries which can be stored in the array. */
#define YB_TERMINATED_QUERIES_SIZE 1000

#define YB_QUERY_TEXT_SIZE 256
#define YB_QUERY_TERMINATION_SIZE 256

/* Structure defining the format of a terminated query. */
typedef struct YbTerminatedQuery
{
	Oid			userid;
	Oid			databaseoid;
	int32		backend_pid;
	TimestampTz activity_start_timestamp;
	TimestampTz activity_end_timestamp;
	char		query_string[YB_QUERY_TEXT_SIZE];
	char		termination_reason[YB_QUERY_TERMINATION_SIZE];
} YbTerminatedQuery;

/* Structure defining the circular buffer used to store terminated queries in the shared memory. */
typedef struct YbTerminatedQueries {
	size_t		curr;
	YbTerminatedQuery queries[YB_TERMINATED_QUERIES_SIZE];
} YbTerminatedQueriesBuffer;

static LWLock *yb_terminated_queries_lock;
static YbTerminatedQueriesBuffer *yb_terminated_queries;

Size
YbTerminatedQueriesShmemSize(void)
{
	Size		size;

	size = MAXALIGN(sizeof(LWLock));
	size = add_size(size, sizeof(YbTerminatedQueriesBuffer));

	return size;
}

void
YbTerminatedQueriesShmemInit(void)
{
	bool		found;

	yb_terminated_queries_lock = (LWLock *)ShmemInitStruct("YbTerminatedQueries Lock",
														   sizeof(LWLock), &found);

	if (!found)
		LWLockInitialize(yb_terminated_queries_lock,
						 LWTRANCHE_YB_TERMINATED_QUERIES);

	yb_terminated_queries = (YbTerminatedQueriesBuffer *)
									ShmemInitStruct("YbTerminatedQueries",
													sizeof(YbTerminatedQueriesBuffer),
													&found);

	if (!found)
		MemSet(yb_terminated_queries, 0, sizeof(YbTerminatedQueriesBuffer));

	return;
}

/* Reports a terminated query by adding it to the yb_terminated_queries shared circular buffer. */
void
yb_report_query_termination(char *message, int pid)
{
	int			i;
	volatile PgBackendStatus *beentry = getBackendStatusArray();

	if (beentry == NULL)
		return;

	for (i = 1; i <= MaxBackends; i++, beentry++)
	{
		if (beentry->st_procpid == pid)
		{
			size_t		curr_idx;
			YbTerminatedQuery *curr_entry;

			LWLockAcquire(yb_terminated_queries_lock, LW_EXCLUSIVE);

			curr_idx = yb_terminated_queries->curr%YB_TERMINATED_QUERIES_SIZE;
			curr_entry = &yb_terminated_queries->queries[curr_idx];

#define SUB_SET(shfld, befld) curr_entry->shfld = befld
			SUB_SET(activity_start_timestamp, beentry->st_activity_start_timestamp);
			SUB_SET(activity_end_timestamp, GetCurrentTimestamp());
			SUB_SET(backend_pid, beentry->st_procpid);
			SUB_SET(databaseoid, beentry->st_databaseid);
			SUB_SET(userid, beentry->st_userid);
#undef SUB_SET

#define SUB_MOVE(shfld, befld) strncpy(curr_entry->shfld, befld, sizeof(curr_entry->shfld)-1)
			SUB_MOVE(query_string, beentry->st_activity_raw);
			SUB_MOVE(termination_reason, message);
#undef SUB_MOVE

			yb_terminated_queries->curr++;
			LWLockRelease(yb_terminated_queries_lock);
			return;
		}
	}

	elog(WARNING, "could not find BEEntry for pid %d to add to yb_terminated_queries", pid);
}

/*
 * Returns terminated queries filtered by db_oid, else returns all terminated queries if db_oid is
 * not mentioned. Returns NULL if no terminated queries have been reported.
 */
YbTerminatedQuery *
yb_fetch_terminated_queries(Oid db_oid, size_t *num_queries)
{
	LWLockAcquire(yb_terminated_queries_lock, LW_SHARED);

	if (yb_terminated_queries->curr == 0)
	{
		*num_queries = 0;
		LWLockRelease(yb_terminated_queries_lock);
		return NULL;
	}

	size_t		total_terminated_queries = yb_terminated_queries->curr;
	size_t		queries_size = Min(total_terminated_queries, YB_TERMINATED_QUERIES_SIZE);

	if (db_oid == -1)
	{
		*num_queries = queries_size;
		LWLockRelease(yb_terminated_queries_lock);
		return yb_terminated_queries->queries;
	}

	size_t		db_terminated_queries = 0;
	for (size_t idx = 0; idx < queries_size; idx++)
		db_terminated_queries += yb_terminated_queries->queries[idx].databaseoid == db_oid ? 1 : 0;

	size_t		total_expected_size = db_terminated_queries * sizeof(YbTerminatedQuery);
	YbTerminatedQuery *queries = (YbTerminatedQuery *) palloc(total_expected_size);

	size_t		counter = 0;
	for (size_t idx = 0; idx < queries_size; idx++)
	{
		if (yb_terminated_queries->queries[idx].databaseoid == db_oid)
			queries[counter++] = yb_terminated_queries->queries[idx];
	}

	*num_queries = db_terminated_queries;
	LWLockRelease(yb_terminated_queries_lock);

	return queries;
}

/*
 * For this function, there exists a corresponding entry in pg_proc which dictates the input and
 * schema of the output row. This is used in a different manner from other methods like
 * pgstat_get_backend_activity_start which will be called individually in parallel.
 * This method will return the rows in batched format all at once. Note that {i,o,o,o} in pg_proc
 * means that for the corresponding entry at the same index, it is an input if labeled i
 *  but output (represented by o) otherwise.
 */
Datum
yb_pg_stat_get_queries(PG_FUNCTION_ARGS)
{
#define PG_YBSTAT_TERMINATED_QUERIES_COLS 6
	ReturnSetInfo *rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
	TupleDesc	tupdesc;
	Tuplestorestate *tupstore;
	MemoryContext per_query_ctx;
	MemoryContext oldcontext;

	if (rsinfo == NULL || !IsA(rsinfo, ReturnSetInfo))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("set-valued function called in context that cannot accept a set")));
	if (!(rsinfo->allowedModes & SFRM_Materialize))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("materialize mode required, but it is not " \
						"allowed in this context")));

	if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
		elog(ERROR, "return type must be a row type");

	per_query_ctx = rsinfo->econtext->ecxt_per_query_memory;
	oldcontext = MemoryContextSwitchTo(per_query_ctx);

	tupstore = tuplestore_begin_heap(true, false, work_mem);
	rsinfo->returnMode = SFRM_Materialize;
	rsinfo->setResult = tupstore;
	rsinfo->setDesc = tupdesc;

	MemoryContextSwitchTo(oldcontext);

	Oid			db_oid = PG_ARGISNULL(0) ? -1 : PG_GETARG_OID(0);
	size_t		num_queries = 0;
	YbTerminatedQuery *queries = yb_fetch_terminated_queries(db_oid, &num_queries);
	for (size_t i = 0; i < num_queries; i++)
	{
		if (has_privs_of_role(GetUserId(), queries[i].userid) ||
			is_member_of_role(GetUserId(), ROLE_PG_READ_ALL_STATS) ||
			IsYbDbAdminUser(GetUserId()))
		{
			Datum		values[PG_YBSTAT_TERMINATED_QUERIES_COLS];
			bool		nulls[PG_YBSTAT_TERMINATED_QUERIES_COLS];

			MemSet(values, 0, sizeof(values));
			MemSet(nulls, 0, sizeof(nulls));

			values[0] = ObjectIdGetDatum(queries[i].databaseoid);
			values[1] = Int32GetDatum(queries[i].backend_pid);
			values[2] = CStringGetTextDatum(queries[i].query_string);
			values[3] = CStringGetTextDatum(queries[i].termination_reason);
			values[4] = TimestampTzGetDatum(queries[i].activity_start_timestamp);
			values[5] = TimestampTzGetDatum(queries[i].activity_end_timestamp);

			tuplestore_putvalues(tupstore, tupdesc, values, nulls);
		}
		else
			continue;
	}

	tuplestore_donestoring(tupstore);

	return (Datum) 0;
}
