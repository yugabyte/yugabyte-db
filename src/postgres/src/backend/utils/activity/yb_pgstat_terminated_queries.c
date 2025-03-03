/* -------------------------------------------------------------------------
 *
 * yb_pgstat_terminated_queries.c
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
 *	  src/backend/utils/activity/yb_pgstat_terminated_queries.c
 * -------------------------------------------------------------------------
 */

#include "postgres.h"

#include "utils/pgstat_internal.h"

/*
 * Report a terminated query.
 */
void
pgstat_report_query_termination(char *message, int pid)
{
	int			i;
	volatile PgBackendStatus *beentry = getBackendStatusArray();

	if (beentry == NULL)
		return;

	for (i = 1; i <= MaxBackends; i++, beentry++)
	{
		if (beentry->st_procpid == pid)
		{
			PgStat_EntryRef *entry_ref;
			PgStat_YbTerminatedQueriesBuffer *query_buffer;
			PgStat_YbTerminatedQuery *curr_entry;

			entry_ref = pgstat_get_entry_ref_locked(PGSTAT_KIND_YB_TERMINATED_QUERIES,
													InvalidOid, InvalidOid, false);

			query_buffer = &((PgStatShared_YbTerminatedQuery *) entry_ref->shared_stats)->stats;
			curr_entry = &query_buffer->queries[query_buffer->curr%YB_TERMINATED_QUERIES_SIZE];

			/*
			 * Adding the reported query directly to the shared memory as multiple processes reporting
			 * a terminated query at the same time can cause conflicts in the pending entry.
			 */
#define SUB_SET(shfld, befld) curr_entry->shfld = befld
			SUB_SET(activity_start_timestamp, beentry->st_activity_start_timestamp);
			SUB_SET(activity_end_timestamp, GetCurrentTimestamp());
			SUB_SET(backend_pid, beentry->st_procpid);
			SUB_SET(databaseoid, beentry->st_databaseid);
			SUB_SET(userid, beentry->st_userid);
#undef SUB_SET

#define SUB_MOVE(shfld, befld) strncpy(curr_entry->shfld, befld, sizeof(curr_entry->shfld))
			SUB_MOVE(query_string, beentry->st_activity_raw);
			SUB_MOVE(termination_reason, message);
#undef SUB_MOVE

			query_buffer->curr++;
			pgstat_unlock_entry(entry_ref);
			return;
		}
	}

	elog(WARNING, "could not find BEEntry for pid %d to add to yb_terminated_queries", pid);
}

/*
 * Support function for the SQL-callable pgstat* functions. Returns
 * the terminated queries for one database or NULL.
 */
PgStat_YbTerminatedQuery *
pgstat_fetch_yb_terminated_queries(Oid db_oid, size_t *num_queries)
{
	PgStat_YbTerminatedQueriesBuffer *query_buffer;
	query_buffer = (PgStat_YbTerminatedQueriesBuffer *)
											pgstat_fetch_entry(PGSTAT_KIND_YB_TERMINATED_QUERIES,
															  InvalidOid, InvalidOid);

	if (query_buffer == NULL)
	{
		*num_queries = 0;
		return NULL;
	}

	size_t		total_terminated_queries = query_buffer->curr;
	size_t		queries_size = Min(total_terminated_queries, YB_TERMINATED_QUERIES_SIZE);

	if (db_oid == -1)
	{
		*num_queries = queries_size;
		return query_buffer->queries;
	}

	size_t		db_terminated_queries = 0;
	for (size_t idx = 0; idx < queries_size; idx++)
		db_terminated_queries += query_buffer->queries[idx].databaseoid == db_oid ? 1 : 0;

	size_t		total_expected_size = db_terminated_queries * sizeof(PgStat_YbTerminatedQuery);
	PgStat_YbTerminatedQuery *queries = (PgStat_YbTerminatedQuery *) palloc(total_expected_size);

	size_t		counter = 0;
	for (size_t idx = 0; idx < queries_size; idx++)
	{
		if (query_buffer->queries[idx].databaseoid == db_oid)
			queries[counter++] = query_buffer->queries[idx];
	}

	*num_queries = db_terminated_queries;
	return queries;
}
