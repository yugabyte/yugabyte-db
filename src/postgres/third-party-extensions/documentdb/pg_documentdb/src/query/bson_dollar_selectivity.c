/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/query/selectivity.c
 *
 * Implementation of selectivity functions for BSON operators.
 *
 *-------------------------------------------------------------------------
 */
#include <postgres.h>
#include <fmgr.h>
#include <utils/lsyscache.h>

#include <nodes/pathnodes.h>
#include <utils/selfuncs.h>
#include <metadata/metadata_cache.h>
#include <planner/mongo_query_operator.h>

extern bool EnableNewOperatorSelectivityMode;


static double GetStatisticsNoStatsData(List *args, Oid selectivityOpExpr);

static double GetDisableStatisticSelectivity(List *args);

/* The default selectivity Postgres applies for matching clauses. */
static const double DefaultSelectivity = 0.5;

/* The low selectivity - based on prior guess. */
static const double LowSelectivity = 0.01;

/* Selectivity when most of the table is accessed (Selectivity max is 1) */
static const double HighSelectivity = 0.9;

PG_FUNCTION_INFO_V1(bson_dollar_selectivity);


/*
 * bson_operator_selectivity returns the selectivity of a BSON operator
 * on a relation.
 */
Datum
bson_dollar_selectivity(PG_FUNCTION_ARGS)
{
	PlannerInfo *planner = (PlannerInfo *) PG_GETARG_POINTER(0);
	Oid selectivityOpExpr = PG_GETARG_OID(1);
	List *args = (List *) PG_GETARG_POINTER(2);
	int varRelId = PG_GETARG_INT32(3);
	Oid collation = PG_GET_COLLATION();

	if (!EnableNewOperatorSelectivityMode)
	{
		PG_RETURN_FLOAT8(GetDisableStatisticSelectivity(args));
	}

	double defaultInputSelectivity = GetStatisticsNoStatsData(args, selectivityOpExpr);

	/*
	 * This is Postgres's default selectivity implementation that looks at statistics
	 * and gets the Most common values/ histograms and gets the overall selectivity
	 * from the raw table.
	 */
	double selectivity = generic_restriction_selectivity(
		planner, selectivityOpExpr, collation, args, varRelId, defaultInputSelectivity);

	PG_RETURN_FLOAT8(selectivity);
}


/*
 * Legacy function for compat to restore prior value to
 * implementing selectivity.
 */
static double
GetStatisticsNoStatsData(List *args, Oid selectivityOpExpr)
{
	if (list_length(args) != 2)
	{
		/* this is not one of the default operators - return Postgres's default values */
		return DefaultSelectivity;
	}

	Node *secondNode = lsecond(args);
	if (!IsA(secondNode, Const))
	{
		/* Can't determine anything here */
		return DefaultSelectivity;
	}

	Const *secondConst = (Const *) secondNode;
	const MongoIndexOperatorInfo *indexOp;
	if (secondConst->consttype == BsonQueryTypeId())
	{
		Oid selectFuncId = get_opcode(selectivityOpExpr);
		indexOp = GetMongoIndexOperatorInfoByPostgresFuncId(selectFuncId);
	}
	else
	{
		/* This is an index pushdown operator */
		indexOp = GetMongoIndexOperatorByPostgresOperatorId(selectivityOpExpr);
	}

	if (indexOp->indexStrategy == BSON_INDEX_STRATEGY_INVALID)
	{
		/* Unknown - thunk to PG value */
		return DefaultSelectivity;
	}

	pgbsonelement dollarElement;
	PgbsonToSinglePgbsonElement(
		DatumGetPgBson(secondConst->constvalue), &dollarElement);

	switch (indexOp->indexStrategy)
	{
		case BSON_INDEX_STRATEGY_DOLLAR_EQUAL:
		{
			if (dollarElement.bsonValue.value_type == BSON_TYPE_NULL)
			{
				/* $eq: null matches paths that don't exist: presume normal selectivity */
				return DefaultSelectivity;
			}

			/* Use prior value - assume $eq supports lower selectivity */
			return LowSelectivity;
		}

		case BSON_INDEX_STRATEGY_DOLLAR_NOT_EQUAL:
		case BSON_INDEX_STRATEGY_DOLLAR_EXISTS:
		{
			/* Inverse selectivity of $eq or general exists check
			 * so assume high selectivity
			 */
			return HighSelectivity;
		}

		case BSON_INDEX_STRATEGY_DOLLAR_IN:
		{
			if (dollarElement.bsonValue.value_type == BSON_TYPE_ARRAY)
			{
				int inElements = BsonDocumentValueCountKeys(&dollarElement.bsonValue);

				/* $in is basically N $eq - selectivity is multiplied */
				return Min(inElements * LowSelectivity, HighSelectivity);
			}

			return DefaultSelectivity;
		}

		case BSON_INDEX_STRATEGY_DOLLAR_RANGE:
		{
			/* Since $range does a $gt/$lt together, assume that it gives you
			 * half the selectivity of each $gt/$lt.
			 */
			return DefaultSelectivity / 2;
		}

		default:
		{
			return DefaultSelectivity;
		}
	}
}


/*
 * Legacy function for compat to restore prior value to
 * implementing selectivity.
 */
static double
GetDisableStatisticSelectivity(List *args)
{
	if (list_length(args) != 2)
	{
		/* this is not one of the default operators - return Postgres's default values */
		return DefaultSelectivity;
	}

	Node *secondNode = lsecond(args);
	if (!IsA(secondNode, Const))
	{
		/* Can't determine anything here */
		return DefaultSelectivity;
	}

	Const *secondConst = (Const *) secondNode;

	/* dumbest possible implementation: assume 1% of rows are returned */
	if (secondConst->consttype == BsonQueryTypeId())
	{
		/* These didn't have a restrict info so they were using the PG default*/
		return DefaultSelectivity;
	}
	else
	{
		/* These were the default Selectivity value for $operators */
		return LowSelectivity;
	}
}
