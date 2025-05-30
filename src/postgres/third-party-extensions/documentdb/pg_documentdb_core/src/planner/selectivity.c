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
	/* dumbest possible implementation: assume 1% of rows are returned */
	PG_RETURN_FLOAT8(0.01);
}
