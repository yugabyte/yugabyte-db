#include "postgres.h"

#include "funcapi.h"
#include "pg_yb_utils.h"
#include "postmaster/syslogger.h"
#include "utils/builtins.h"
#include "yb/yql/pggate/ybc_pggate.h"

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(ycql_stat_statements);

/* In yb_ycql_utils v1.0 the number of columns = 9 */
static const int ycql_stat_statements_num_cols_v1_1 = 10;

Datum
ycql_stat_statements(PG_FUNCTION_ARGS)
{
	ReturnSetInfo *rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
	TupleDesc	tupdesc;
	Tuplestorestate *tupstore;
	MemoryContext per_query_ctx;
	MemoryContext oldcontext;
	int			i = 0;

	InitMaterializedSRF(fcinfo, 0);

	const int	ncols = rsinfo->setDesc->natts;
	Datum		values[ncols];
	bool		nulls[ncols];

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

	YbcYCQLStatementStats *stat_list = NULL;
	size_t		num_stats = 0;

	HandleYBStatus(YBCYcqlStatementStats(&stat_list, &num_stats));

	for (i = 0; i < num_stats; ++i)
	{
		YbcYCQLStatementStats *stats = (YbcYCQLStatementStats *) stat_list + i;

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

		if (ncols >= ycql_stat_statements_num_cols_v1_1)
			values[9] = CStringGetTextDatum(stats->keyspace);

		tuplestore_putvalues(tupstore, tupdesc, values, nulls);
	}

	/* clean up and return the tuplestore */
	tuplestore_donestoring(tupstore);
	if (stat_list)
	{
		pfree(stat_list);
	}
	return (Datum) 0;
}
