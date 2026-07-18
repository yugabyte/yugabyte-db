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

#include "query/bson_dollar_selectivity.h"
#include "aggregation/bson_query_common.h"

extern bool EnableNewOperatorSelectivityMode;
extern bool EnableCompositeIndexPlanner;
extern bool LowSelectivityForLookup;
extern bool SetSelectivityForFullScan;

static bool IsDollarRangeFullScan(List *args);
static double GetStatisticsNoStatsData(List *args, Oid selectivityOpExpr, double
									   defaultExprSelectivity);

static double GetDisableStatisticSelectivity(List *args, double
											 defaultDisabledSelectivity);

PG_FUNCTION_INFO_V1(bson_dollar_selectivity);


static inline bool
IsLookupExtractFuncExpr(Node *expr)
{
	if (!IsA(expr, FuncExpr))
	{
		return false;
	}

	FuncExpr *funcExpr = (FuncExpr *) expr;
	return funcExpr->funcid ==
		   DocumentDBApiInternalBsonLookupExtractFilterExpressionFunctionOid();
}


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

	/* The default selectivity Postgres applies for matching clauses. */
	const double defaultOperatorSelectivity = 0.5;
	double selectivity = GetDollarOperatorSelectivity(
		planner, selectivityOpExpr, args, collation, varRelId,
		defaultOperatorSelectivity);

	PG_RETURN_FLOAT8(selectivity);
}


double
GetDollarOperatorSelectivity(PlannerInfo *planner, Oid selectivityOpExpr,
							 List *args, Oid collation, int varRelId,
							 double defaultExprSelectivity)
{
	/* Special case, check if it's a full scan */
	if (SetSelectivityForFullScan &&
		selectivityOpExpr == BsonRangeMatchOperatorOid() &&
		IsDollarRangeFullScan(args))
	{
		return 1.0;
	}

	if (!EnableNewOperatorSelectivityMode && !EnableCompositeIndexPlanner)
	{
		return GetDisableStatisticSelectivity(args, defaultExprSelectivity);
	}

	double defaultInputSelectivity = GetStatisticsNoStatsData(args, selectivityOpExpr,
															  defaultExprSelectivity);

	/*
	 * This is Postgres's default selectivity implementation that looks at statistics
	 * and gets the Most common values/ histograms and gets the overall selectivity
	 * from the raw table.
	 */
	double selectivity = generic_restriction_selectivity(
		planner, selectivityOpExpr, collation, args, varRelId, defaultInputSelectivity);

	return selectivity;
}


static bool
IsDollarRangeFullScan(List *args)
{
	/* Special case, check if it's a full scan */
	Node *secondNode = lsecond(args);
	if (!IsA(secondNode, Const))
	{
		return false;
	}

	Const *secondConst = (Const *) secondNode;
	pgbsonelement dollarElement;
	PgbsonToSinglePgbsonElement(
		DatumGetPgBson(secondConst->constvalue), &dollarElement);
	DollarRangeParams rangeParams = { 0 };
	InitializeQueryDollarRange(&dollarElement.bsonValue, &rangeParams);
	return rangeParams.isFullScan;
}


/*
 * Legacy function for compat to restore prior value to
 * implementing selectivity.
 */
static double
GetStatisticsNoStatsData(List *args, Oid selectivityOpExpr, double defaultExprSelectivity)
{
	if (list_length(args) != 2)
	{
		/* this is not one of the default operators - return Postgres's default values */
		return defaultExprSelectivity;
	}

	Node *secondNode = lsecond(args);
	if (!IsA(secondNode, Const))
	{
		if (LowSelectivityForLookup &&
			IsLookupExtractFuncExpr(secondNode))
		{
			/* This means a lookup modified index qual, consider low selectivity */
			return LowSelectivity;
		}

		/* Can't determine anything here */
		return defaultExprSelectivity;
	}

	Const *secondConst = (Const *) secondNode;
	BsonIndexStrategy indexStrategy = BSON_INDEX_STRATEGY_INVALID;
	if (secondConst->consttype == BsonQueryTypeId())
	{
		Oid selectFuncId = get_opcode(selectivityOpExpr);
		const MongoIndexOperatorInfo *indexOp = GetMongoIndexOperatorInfoByPostgresFuncId(
			selectFuncId);
		indexStrategy = indexOp->indexStrategy;
	}
	else
	{
		/* This is an index pushdown operator */
		const MongoIndexOperatorInfo *indexOp = GetMongoIndexOperatorByPostgresOperatorId(
			selectivityOpExpr);
		indexStrategy = indexOp->indexStrategy;
	}

	if (indexStrategy == BSON_INDEX_STRATEGY_INVALID)
	{
		if (selectivityOpExpr == BsonRangeMatchOperatorOid())
		{
			indexStrategy = BSON_INDEX_STRATEGY_DOLLAR_RANGE;
		}
		else
		{
			/* Unknown - thunk to PG value */
			return defaultExprSelectivity;
		}
	}

	pgbsonelement dollarElement;
	PgbsonToSinglePgbsonElement(
		DatumGetPgBson(secondConst->constvalue), &dollarElement);

	switch (indexStrategy)
	{
		case BSON_INDEX_STRATEGY_DOLLAR_EQUAL:
		{
			if (dollarElement.bsonValue.value_type == BSON_TYPE_NULL)
			{
				/* $eq: null matches paths that don't exist: presume normal selectivity */
				return defaultExprSelectivity;
			}

			/* Use prior value - assume $eq supports lower selectivity */
			return LowSelectivity;
		}

		case BSON_INDEX_STRATEGY_DOLLAR_NOT_EQUAL:
		{
			return HighSelectivity;
		}

		case BSON_INDEX_STRATEGY_DOLLAR_EXISTS:
		{
			/* Inverse selectivity of $eq or general exists check
			 * so assume high selectivity. Exists false should return the same selectivity as
			 * equals null above.
			 */
			int32_t value = BsonValueAsInt32(&dollarElement.bsonValue);
			return value > 0 ? HighSelectivity : defaultExprSelectivity;
		}

		case BSON_INDEX_STRATEGY_DOLLAR_IN:
		{
			if (dollarElement.bsonValue.value_type == BSON_TYPE_ARRAY)
			{
				int inElements = BsonDocumentValueCountKeys(&dollarElement.bsonValue);

				/* $in is basically N $eq - selectivity is multiplied */
				return Min(inElements * LowSelectivity, HighSelectivity);
			}

			return defaultExprSelectivity;
		}

		case BSON_INDEX_STRATEGY_DOLLAR_RANGE:
		{
			/* Since $range does a $gt/$lt together, assume that it gives you
			 * half the selectivity of each $gt/$lt.
			 */
			DollarRangeParams rangeParams = { 0 };
			InitializeQueryDollarRange(&dollarElement.bsonValue, &rangeParams);
			if (rangeParams.isFullScan)
			{
				return 1.0;
			}

			return defaultExprSelectivity / 2;
		}

		default:
		{
			return defaultExprSelectivity;
		}
	}
}


/*
 * Legacy function for compat to restore prior value to
 * implementing selectivity.
 */
static double
GetDisableStatisticSelectivity(List *args, double defaultExprSelectivity)
{
	if (list_length(args) != 2)
	{
		/* this is not one of the default operators - return Postgres's default values */
		return defaultExprSelectivity;
	}

	Node *secondNode = lsecond(args);
	if (!IsA(secondNode, Const))
	{
		if (LowSelectivityForLookup &&
			IsLookupExtractFuncExpr(secondNode))
		{
			/* This means a lookup modified index qual, consider low selectivity */
			return LowSelectivity;
		}

		/* Can't determine anything here */
		return defaultExprSelectivity;
	}

	Const *secondConst = (Const *) secondNode;
	if (secondConst->consttype == BsonQueryTypeId())
	{
		/* These didn't have a restrict info so they were using the PG default*/
		return defaultExprSelectivity;
	}
	else
	{
		/* These were the default Selectivity value for $operators */
		return LowSelectivity;
	}
}
