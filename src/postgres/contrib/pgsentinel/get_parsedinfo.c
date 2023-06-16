/*
 * get_parsedinfo.c
 *
 * Copyright (c) 2018, PgSentinel
 *
 * IDENTIFICATION:
 * https://github.com/pgsentinel/pgsentinel
 *
 * LICENSE: GNU Affero General Public License v3.0
 */

#include "postgres.h"
#include "pgsentinel.h"
#include "storage/ipc.h"
#include "storage/proc.h"
#include "miscadmin.h"
#include "access/twophase.h"
#include "parser/scansup.h"
#include "parser/analyze.h"
#include "access/hash.h"
#include "funcapi.h"
#include "utils/builtins.h"
#include "commands/extension.h"
#include "pgstat.h"
#include "postmaster/autovacuum.h"
#include "replication/walsender.h"

Datum get_parsedinfo(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(get_parsedinfo);

/* to create queryid in case of utility statements*/
#if PG_VERSION_NUM >= 110000
static uint64 getparsedinfo_hash64_string(const char *str, int len);
#else
static uint32 getparsedinfo_hash32_string(const char *str, int len);
#endif

/* Estimate amount of shared memory needed for proc entry*/
Size
proc_entry_memsize(void)
{
	Size            size;

	/* ProcEntryArray */
	size = mul_size(sizeof(procEntry), get_max_procs_count());
	/* ProEntryQueryBuffer */
	size = add_size(size, mul_size(pgstat_track_activity_query_size,
													get_max_procs_count()));
	/* ProEntryCmdTypeBuffer */
	size = add_size(size, mul_size(NAMEDATALEN, get_max_procs_count()));
	return size;
}

/* to create queryid in case of utility statements*/
#if PG_VERSION_NUM >= 110000
static uint64
getparsedinfo_hash64_string(const char *str, int len)
{
	return DatumGetUInt64(hash_any_extended((const unsigned char *) str,
																	len, 0));
}
#else
static uint32
getparsedinfo_hash32_string(const char *str, int len)
{
	return hash_any((const unsigned char *) str, len);
}
#endif

int
get_max_procs_count(void)
{
	int count = 0;

	count += MaxConnections;
	count += autovacuum_max_workers;
	count += max_worker_processes;
	count += max_wal_senders;
	count += NUM_AUXILIARY_PROCS;
	count += max_prepared_xacts;
	count++;

	return count;
}

void
#if PG_VERSION_NUM < 140000
getparsedinfo_post_parse_analyze(ParseState *pstate, Query *query)
#else
getparsedinfo_post_parse_analyze(ParseState *pstate, Query *query, JumbleState *jstate)
#endif
{

	if (prev_post_parse_analyze_hook)
#if PG_VERSION_NUM < 140000
		prev_post_parse_analyze_hook(pstate, query);
#else
		prev_post_parse_analyze_hook(pstate, query, jstate);
#endif
	if (MyProc)
	{
		int i = MyProc - ProcGlobal->allProcs;
		const char *querytext = pstate->p_sourcetext;
		int minlen;
		int query_len;
#if PG_VERSION_NUM >= 100000
		int query_location = query->stmt_location;
		query_len = query->stmt_len;

		if (query_location >= 0)
		{
			Assert(query_location <= strlen(querytext));
			querytext += query_location;
			/* Length of 0 (or -1) means "rest of string" */
			if (query_len <= 0)
				query_len = strlen(querytext);
			else
				Assert(query_len <= strlen(querytext));
		}
		else
		{
			/* If query location is unknown, distrust query_len as well */
			query_location = 0;
			query_len = strlen(querytext);
		}

		/*
		 * Discard leading and trailing whitespace, too.  Use scanner_isspace()
		 * not libc's isspace(), because we want to match the lexer's behavior.
		 */
		while (query_len > 0 && scanner_isspace(querytext[0]))
			querytext++, query_location++, query_len--;
		while (query_len > 0 && scanner_isspace(querytext[query_len - 1]))
			query_len--;
#else
		query_len = strlen(querytext);
#endif

		minlen = Min(query_len,pgstat_track_activity_query_size-1);
		memcpy(ProcEntryArray[i].query,querytext,minlen);
		ProcEntryArray[i].query[minlen]='\0';
		switch (query->commandType)
		{
			case CMD_SELECT:
				ProcEntryArray[i].cmdtype="SELECT";
				break;
			case CMD_INSERT:
				ProcEntryArray[i].cmdtype="INSERT";
				break;
#if PG_VERSION_NUM >= 150000
			case CMD_MERGE:
				ProcEntryArray[i].cmdtype="MERGE";
				break;
#endif
			case CMD_UPDATE:
				ProcEntryArray[i].cmdtype="UPDATE";
				break;
			case CMD_DELETE:
				ProcEntryArray[i].cmdtype="DELETE";
				break;
			case CMD_UTILITY:
				ProcEntryArray[i].cmdtype="UTILITY";
				break;
			case CMD_UNKNOWN:
				ProcEntryArray[i].cmdtype="UNKNOWN";
				break;
			case CMD_NOTHING:
				ProcEntryArray[i].cmdtype="NOTHING";
				break;
		}
		/*
		 * For utility statements, we just hash the query string to get an ID.
		 */
#if PG_VERSION_NUM >= 110000
		if (query->queryId == UINT64CONST(0)) 	{
			ProcEntryArray[i].queryid = getparsedinfo_hash64_string(querytext,
																	query_len);
#else
			if (query->queryId == 0) {
				ProcEntryArray[i].queryid = getparsedinfo_hash32_string(querytext,
																	query_len);
#endif
			} else {
				ProcEntryArray[i].queryid = query->queryId;
			}
	}
}

Datum
get_parsedinfo(PG_FUNCTION_ARGS)
{
	int i;
	/*procEntry *result;*/
	ReturnSetInfo   *rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
	TupleDesc	    tupdesc;
	Tuplestorestate *tupstore;
	MemoryContext   per_query_ctx;
	MemoryContext   oldcontext;
	Datum           values[4];
	bool            nulls[4] = {0};

	per_query_ctx = rsinfo->econtext->ecxt_per_query_memory;
	oldcontext = MemoryContextSwitchTo(per_query_ctx);
	/* Build a tuple descriptor for our result type */
 	if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
		elog(ERROR, "return type must be a row type");
	tupstore = tuplestore_begin_heap(true, false, work_mem);
	rsinfo->returnMode = SFRM_Materialize;
	rsinfo->setResult = tupstore;
	rsinfo->setDesc = tupdesc;
	MemoryContextSwitchTo(oldcontext);

	for (i = 0; i < ProcGlobal->allProcCount; i++)
	{
		PGPROC  *proc = &ProcGlobal->allProcs[i];
		if (proc != NULL && proc->pid != 0 && (proc->pid == PG_GETARG_INT32(0)
												 || PG_GETARG_INT32(0) == -1))
		{
			values[0] = proc->pid;
			if (Int64GetDatum(ProcEntryArray[i].queryid))
				values[1] = Int64GetDatum(ProcEntryArray[i].queryid);
			else
				nulls[1] = true;
			if (CStringGetTextDatum(ProcEntryArray[i].query))
        			values[2] = CStringGetTextDatum(ProcEntryArray[i].query);
			else
				nulls[2] = true;
			if (CStringGetTextDatum(ProcEntryArray[i].cmdtype))
        			values[3] = CStringGetTextDatum(ProcEntryArray[i].cmdtype);
			else
				nulls[3] = true;
    
        	tuplestore_putvalues(tupstore, tupdesc, values, nulls);
		}
	}
	tuplestore_donestoring(tupstore);
	return (Datum) 0;
}
