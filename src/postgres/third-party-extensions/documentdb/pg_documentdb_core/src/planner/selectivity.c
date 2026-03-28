/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/planner/selectivity.c
 *
 * Implementation of selectivity functions for BSON operators.
 *
 *-------------------------------------------------------------------------
 */
#include <postgres.h>
#include <fmgr.h>


PG_FUNCTION_INFO_V1(bson_operator_selectivity);


/*
 * bson_operator_selectivity returns the selectivity of a BSON operator
 * on a relation.
 */
Datum
bson_operator_selectivity(PG_FUNCTION_ARGS)
{
	/*
	 * Note: This method is superceded by pg_documentdb/src/query/bson_dollar_selectivity.c
	 * for the core operators. This is left for back-compatibility for the schema.
	 * dumbest possible implementation: assume 1% of rows are returned */
	PG_RETURN_FLOAT8(0.01);
}
