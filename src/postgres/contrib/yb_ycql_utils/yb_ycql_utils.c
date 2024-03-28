#include "postgres.h"

#include "funcapi.h"
#include "postmaster/syslogger.h"
#include "pg_yb_utils.h"
#include "utils/builtins.h"
#include "yb/yql/pggate/ybc_pggate.h"

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(ycql_stat_statements);

Datum
ycql_stat_statements(PG_FUNCTION_ARGS)
{
	ReturnSetInfo *rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
	TupleDesc	tupdesc;
	Tuplestorestate *tupstore;
	MemoryContext per_query_ctx;
	MemoryContext oldcontext;
	int	i;
#define YCQL_STAT_STATEMENTS_COLS 9

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

	YCQLStatementStats *stat_list = NULL;
	size_t num_stats = 0;
	HandleYBStatus(YBCYcqlStatementStats(&stat_list, &num_stats));

	for (i = 0; i < num_stats; ++i)
	{
		YCQLStatementStats *stats = (YCQLStatementStats *) stat_list + i;
		Datum values[YCQL_STAT_STATEMENTS_COLS];
		bool nulls[YCQL_STAT_STATEMENTS_COLS];
		memset(values, 0, sizeof(values));
		memset(nulls, 0, sizeof(nulls));

		values[0] = Int64GetDatum(stats->queryid);
		values[1] = CStringGetTextDatum(stats->query);
		values[2] = BoolGetDatum(stats->is_prepared);
		values[3] = Int64GetDatum(stats->calls);
		values[4] = Float8GetDatumFast(stats->total_time);
		values[5] = Float8GetDatumFast(stats->min_time);
		values[6] = Float8GetDatumFast(stats->max_time);
		values[7] = Float8GetDatumFast(stats->mean_time);
		values[8] = Float8GetDatumFast(stats->stddev_time);

		tuplestore_putvalues(tupstore, tupdesc, values, nulls);
	}
#undef YCQL_STAT_STATEMENTS_COLS

	/* clean up and return the tuplestore */
	tuplestore_donestoring(tupstore);
	if (stat_list) {
		pfree(stat_list);
	}
	return (Datum) 0;
}
