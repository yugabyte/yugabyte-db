/*-------------------------------------------------------------------------
 *
 * pg_explain.cpp
 *	  Explain query execution plans
 *
 * Portions Copyright (c) 1996-2024, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994-5, Regents of the University of California
 *
 *-------------------------------------------------------------------------
 */
extern "C" {
#include "postgres.h"

#include "optimizer/planner.h"
#include "tcop/tcopprot.h"

#include "pgduckdb/vendor/pg_explain.hpp"

#if PG_VERSION_NUM < 170000

/*
 * standard_ExplainOneQuery -
 *	  print out the execution plan for one Query, without calling a hook.
 *
 * This is a PG16 version of the standard ExplainOneQuery function that was
 * introduced in PG17.
 */
void
standard_ExplainOneQuery(Query *query, int cursorOptions, IntoClause *into, ExplainState *es, const char *queryString,
                         ParamListInfo params, QueryEnvironment *queryEnv) {
	PlannedStmt *plan;
	instr_time planstart, planduration;
	BufferUsage bufusage_start, bufusage;

	if (es->buffers)
		bufusage_start = pgBufferUsage;
	INSTR_TIME_SET_CURRENT(planstart);

	/* plan the query */
	plan = pg_plan_query(query, queryString, cursorOptions, params);

	INSTR_TIME_SET_CURRENT(planduration);
	INSTR_TIME_SUBTRACT(planduration, planstart);

	/* calc differences of buffer counters. */
	if (es->buffers) {
		memset(&bufusage, 0, sizeof(BufferUsage));
		BufferUsageAccumDiff(&bufusage, &pgBufferUsage, &bufusage_start);
	}

	/* run it (if needed) and produce output */
	ExplainOnePlan(plan, into, es, queryString, params, queryEnv, &planduration, (es->buffers ? &bufusage : NULL));
}
#endif
}
