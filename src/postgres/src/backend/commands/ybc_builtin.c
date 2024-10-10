/*--------------------------------------------------------------------------------------------------
 *
 * ybc_builtin.c
 *        Commands to call YugaByte builtin functions.
 *
 * Copyright (c) YugaByte, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied.  See the License for the specific language governing permissions and limitations
 * under the License.
 *
 * IDENTIFICATION
 *        src/backend/commands/ybc_builtin.c
 *
 *------------------------------------------------------------------------------
 */

#include "postgres.h"

#include <limits.h>
#include <math.h>
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif
#ifdef HAVE_SYS_RESOURCE_H
#include <sys/time.h>
#include <sys/resource.h>
#endif
#ifndef HAVE_GETRUSAGE
#include "rusagestub.h"
#endif

#include "access/htup_details.h"
#include "catalog/pg_type.h"
#include "catalog/pg_type_d.h"
#include "utils/builtins.h"

#include "funcapi.h"

#include "yb/yql/pggate/ybc_pggate.h"
#include "pg_yb_utils.h"

// Scale the ru_maxrss value according to the platform.
// On Linux, the maxrss is in kilobytes.
// On OSX, the maxrss is in bytes and scale it to kilobytes.
// https://www.manpagez.com/man/2/getrusage/osx-10.12.3.php
static long
scale_rss_to_kb(long maxrss)
{
#ifdef __APPLE__
	maxrss = maxrss / 1024;
#endif
	return maxrss;
}
/*
 * Dump the current connection heap stats, including TCMalloc, PG, and PgGate.
 * The exact definition for the output columns are as followed:
 * total_heap_usage                	-> TCMalloc physical usage
 * total_heap_allocation           	-> TCMalloc current allocated bytes
 * total_heap_requested            	-> TCMalloc heap size
 * cached_free_memory              	-> TCMalloc freed bytes
 * total_heap_released             	-> TCMalloc unmapped bytes
 * PostgreSQL_memory_usage         	-> PG current total bytes
 * PostgreSQL_storage_gateway_usage	-> PgGate current total bytes
 *
 * Example usage:
 * SELECT * FROM yb_heap_stats();
 */
Datum
yb_heap_stats(PG_FUNCTION_ARGS)
{
	const static size_t kRetArgNum = 7;

	Datum		values[kRetArgNum];
	bool		isnull[kRetArgNum];
	TupleDesc	tupdesc = CreateTemplateTupleDesc(kRetArgNum);

	TupleDescInitEntry(tupdesc, (AttrNumber) 1, "TCMalloc heap_size_bytes",
					   INT8OID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 2, "TCMalloc total_physical_bytes",
					   INT8OID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 3,
					   "TCMalloc current_allocated_size", INT8OID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 4, "TCMalloc pageheap_free_bytes",
					   INT8OID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 5,
					   "TCMalloc pageheap_unmapped_bytes", INT8OID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 6,
					   "Postgres current allocated bytes", INT8OID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 7,
					   "PGGate current allocated bytes", INT8OID, -1, 0);
	BlessTupleDesc(tupdesc);

	if (yb_enable_memory_tracking)
	{
		YbTcmallocStats tcmallocStats;
		YBCGetHeapConsumption(&tcmallocStats);

		values[0] = Int64GetDatum(tcmallocStats.heap_size_bytes);
		values[1] = Int64GetDatum(tcmallocStats.total_physical_bytes);
		values[2] = Int64GetDatum(tcmallocStats.current_allocated_bytes);
		values[3] = Int64GetDatum(tcmallocStats.pageheap_free_bytes);
		values[4] = Int64GetDatum(tcmallocStats.pageheap_unmapped_bytes);
		values[5] = Int64GetDatum(PgMemTracker.pg_cur_mem_bytes);
		values[6] = Int64GetDatum(tcmallocStats.current_allocated_bytes -
					PgMemTracker.pg_cur_mem_bytes);
	}

	memset(isnull, !yb_enable_memory_tracking, sizeof(isnull));

	// Return tuple.
	return HeapTupleGetDatum(heap_form_tuple(tupdesc, values, isnull));
}

/*
 * Get memory usage of the current session.
 * - The return value is a ROW of unix getrusage().
 * - User command:
 *     SELECT yb_getrusage();
 */
Datum
yb_getrusage(PG_FUNCTION_ARGS)
{
	const int arg_count = 16;

	TupleDesc	tupdesc;
	Datum		values[arg_count];
	bool		isnull[arg_count];
	struct rusage r;

	// Get usage.
	getrusage(RUSAGE_SELF, &r);

	// Create tuple descriptor.
	tupdesc = CreateTemplateTupleDesc(arg_count);
	TupleDescInitEntry(tupdesc, (AttrNumber) 1, "user cpu", INT8OID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 2, "system cpu", INT8OID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 3, "maxrss", INT8OID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 4, "ixrss", INT8OID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 5, "idrss", INT8OID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 6, "isrss", INT8OID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 7, "minflt", INT8OID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 8, "majflt", INT8OID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 9, "nswap", INT8OID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 10, "inblock", INT8OID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 11, "oublock", INT8OID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 12, "msgsnd", INT8OID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 13, "msgrcv", INT8OID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 14, "nsignals", INT8OID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 15, "nvcsw", INT8OID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 16, "nivcsw", INT8OID, -1, 0);
	BlessTupleDesc(tupdesc);

	// Fill in values.
	// TODO() To evaluate CPU percentage, a start-time must be defined. An interface for users to
	// set start-time is needed. It could be the start of a page, a statement, a transaction, or
	// the entire process. Leave this work till later as it is not need it now.
	//   user_cpu % = NULL / NAN
	//   system_cpu % = NULL / NAN
	memset(isnull, 0, sizeof(isnull));
	isnull[0] = true;
	values[0] = Int64GetDatum(NAN);
	isnull[1] = true;
	values[1] = Int64GetDatum(NAN);
	values[2] = Int64GetDatum(r.ru_maxrss);
	values[3] = Int64GetDatum(r.ru_ixrss);
	values[4] = Int64GetDatum(r.ru_idrss);
	values[5] = Int64GetDatum(r.ru_isrss);
	values[6] = Int64GetDatum(r.ru_minflt);
	values[7] = Int64GetDatum(r.ru_majflt);
	values[8] = Int64GetDatum(r.ru_nswap);
	values[9] = Int64GetDatum(r.ru_inblock);
	values[10] = Int64GetDatum(r.ru_oublock);
	values[11] = Int64GetDatum(r.ru_msgsnd);
	values[12] = Int64GetDatum(r.ru_msgrcv);
	values[13] = Int64GetDatum(r.ru_nsignals);
	values[14] = Int64GetDatum(r.ru_nvcsw);
	values[15] = Int64GetDatum(r.ru_nivcsw);

	// Return tuple.
	return HeapTupleGetDatum(heap_form_tuple(tupdesc, values, isnull));
}

/*
 * Get memory usage of the current session
 * - The return value RSS value from getrusage().
 * - User command:
 *     SELECT yb_mem_usage_kb();
 */
Datum
yb_mem_usage(PG_FUNCTION_ARGS)
{
	struct rusage r;
	char a[1024];

	// Get usage.
	getrusage(RUSAGE_SELF, &r);
	sprintf(a, "Session memory usage = %ld kbs", scale_rss_to_kb(r.ru_maxrss));
	PG_RETURN_TEXT_P(cstring_to_text(a));
}

Datum
yb_mem_usage_kb(PG_FUNCTION_ARGS)
{
	struct rusage r;
	getrusage(RUSAGE_SELF, &r);
	PG_RETURN_INT64(scale_rss_to_kb(r.ru_maxrss));
}

/*
 * SQL Layer Only: Get memory usage of the current session.
 * - The return value is size of SQL::MemoryContext.
 * - User command:
 *     SELECT yb_mem_usage_sql();
 */
Datum
yb_mem_usage_sql(PG_FUNCTION_ARGS)
{
	char s[1024];
	int64 usage = MemoryContextStatsUsage(TopMemoryContext, 100);
	sprintf(s, "SQL layer memory usage = %ld bytes", usage);
	PG_RETURN_TEXT_P(cstring_to_text(s));
}

Datum
yb_mem_usage_sql_b(PG_FUNCTION_ARGS)
{
	int64 usage = MemoryContextStatsUsage(TopMemoryContext, 100);
	PG_RETURN_INT64(usage);
}

Datum
yb_mem_usage_sql_kb(PG_FUNCTION_ARGS)
{
	int64 usage = MemoryContextStatsUsage(TopMemoryContext, 100)/1000;
	PG_RETURN_INT64(usage);
}
