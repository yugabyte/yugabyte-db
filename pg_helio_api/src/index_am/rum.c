/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/index_am/rum.c
 *
 * Rum access method implementations for helioapi.
 * See also: https://www.postgresql.org/docs/current/gin-extensibility.html
 * See also: https://github.com/postgrespro/rum
 *
 *-------------------------------------------------------------------------
 */


#include <postgres.h>
#include <fmgr.h>
#include <utils/index_selfuncs.h>
#include <utils/selfuncs.h>
#include <utils/lsyscache.h>
#include "math.h"

#include "planner/mongo_query_operator.h"
#include "opclass/helio_gin_index_mgmt.h"
#include "metadata/metadata_cache.h"


extern bool ForceUseIndexIfAvailable;

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */

static void extension_rumcostestimate(PlannerInfo *root, IndexPath *path, double
									  loop_count,
									  Cost *indexStartupCost, Cost *indexTotalCost,
									  Selectivity *indexSelectivity,
									  double *indexCorrelation,
									  double *indexPages);
static bool IsIndexIsValidForQuery(IndexPath *path);
static bool MatchClauseWithIndexForFuncExpr(IndexPath *path, int32_t indexcol,
											Oid funcId, List *args);
static bool ValidateMatchForOrderbyQuals(IndexPath *path);

static bool IsTextIndexMatch(IndexPath *path);

/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */
PG_FUNCTION_INFO_V1(extensionrumhandler);

/*
 * Register the access method for RUM as a custom index handler.
 * This allows us to create a 'custom' RUM index in the extension.
 * Today, this is temporary: This is needed until the RUM index supports
 * a custom configuration function proc for index operator classes.
 * By registering it here we maintain compatibility with existing GIN implementations.
 * Once we merge the RUM config changes into the mainline repo, this can be removed.
 */
Datum
extensionrumhandler(PG_FUNCTION_ARGS)
{
	bool missingOk = false;
	void **ignoreLibFileHandle = NULL;
	Datum (*rumhandler) (FunctionCallInfo) =
		load_external_function("$libdir/rum", "rumhandler", !missingOk,
							   ignoreLibFileHandle);
	Datum rumHandlerDatum = rumhandler(fcinfo);
	IndexAmRoutine *indexRoutine = (IndexAmRoutine *) DatumGetPointer(rumHandlerDatum);

	/* add a new proc as a config prog. */
	/* Based on https://github.com/postgrespro/rum/blob/master/src/rumutil.c#L117 */
	/* AMsupport is the index of the largest support function. We point to the options proc */
	uint16 RUMNProcs = indexRoutine->amsupport;
	if (RUMNProcs < 11)
	{
		indexRoutine->amsupport = RUMNProcs + 1;

		/* register the user config proc number. */
		/* based on https://github.com/postgrespro/rum/blob/master/src/rum.h#L837 */
		/* RUMNprocs is the count, and the highest function supported */
		/* We set our config proc to be one above that */
		indexRoutine->amoptsprocnum = RUMNProcs + 1;
	}

	indexRoutine->amcostestimate = extension_rumcostestimate;
	PG_RETURN_POINTER(indexRoutine);
}


/*
 * Custom cost estimation function for RUM.
 * While Function support handles matching against specific indexes
 * and ensuring pushdowns happen properly (see dollar_support),
 * There is one case that is not yet handled.
 * If an index has a predicate (partial index), and the *only* clauses
 * in the query are ones that match the predicate, indxpath.create_index_paths
 * creates quals that exclude the predicate. Consequently we're left with no clauses.
 * Because RUM also sets amoptionalkey to true (the first key in the index is not required
 * to be specified), we will still continue to consider the index (per useful_predicate in
 * build_index_paths). In this case, we need to check that at least one predicate matches the
 * index for the index to be considered.
 */
static void
extension_rumcostestimate(PlannerInfo *root, IndexPath *path, double loop_count,
						  Cost *indexStartupCost, Cost *indexTotalCost,
						  Selectivity *indexSelectivity, double *indexCorrelation,
						  double *indexPages)
{
	if (!IsIndexIsValidForQuery(path))
	{
		/* This index is not a match for the given query paths */
		/* In this code path, we set the total cost to infinity */
		/* As the planner walks through all other plans, one will be less */
		/* than infinity (the SeqScan) which will be picked in the worst case */
		*indexStartupCost = 0;
		*indexTotalCost = INFINITY;
		*indexSelectivity = 0;
		return;
	}

	/* Index is valid - pick the cost estimate for rum (which currently is the gin cost estimate) */
	gincostestimate(root, path, loop_count, indexStartupCost, indexTotalCost,
					indexSelectivity, indexCorrelation, indexPages);

	/* Do a pass to check for text indexes (We force push down with cost == 0) */
	if (ForceUseIndexIfAvailable || IsTextIndexMatch(path))
	{
		*indexTotalCost = 0;
		*indexStartupCost = 0;
	}
}


/*
 * Validates whether an index path descriptor
 * can be satisfied by the current index.
 */
static bool
IsIndexIsValidForQuery(IndexPath *path)
{
	if (IsA(path, IndexOnlyScan))
	{
		/* We don't support index only scans in RUM */
		return false;
	}

	if (path->indexorderbys != NIL &&
		!ValidateMatchForOrderbyQuals(path))
	{
		/* Only return valid cost if the order by present
		 * matches the index fully
		 */
		return false;
	}

	if (list_length(path->indexclauses) >= 1)
	{
		/* if there's at least one other index clause,
		 * then this index is already valid
		 */
		return true;
	}

	if (path->indexinfo->indpred == NIL)
	{
		/*
		 * if the index is not a partial index, the useful_predicate
		 * clause does not apply. If there's no filter clauses, we
		 * can't really use this index (don't wanna do a full index scan)
		 */
		return false;
	}

	if (path->indexinfo->indpred != NIL)
	{
		ListCell *cell;
		foreach(cell, path->indexinfo->indpred)
		{
			Node *predQual = (Node *) lfirst(cell);

			/* walk the index predicates and check if they match the index */
			/* TODO: Do we need a query walk here */
			if (IsA(predQual, OpExpr))
			{
				OpExpr *expr = (OpExpr *) predQual;
				for (int32_t indexCol = 0; indexCol < path->indexinfo->nkeycolumns;
					 indexCol++)
				{
					if (MatchClauseWithIndexForFuncExpr(path, indexCol, expr->opfuncid,
														expr->args))
					{
						return true;
					}
				}
			}
			else if (IsA(predQual, FuncExpr))
			{
				FuncExpr *expr = (FuncExpr *) predQual;
				for (int32_t indexCol = 0; indexCol < path->indexinfo->nkeycolumns;
					 indexCol++)
				{
					if (MatchClauseWithIndexForFuncExpr(path, indexCol, expr->funcid,
														expr->args))
					{
						return true;
					}
				}
			}
		}
	}

	return false;
}


/* Given an operator expression and an index column with an index
 * Validates whether that operator + column is supported in this index */
static bool
MatchClauseWithIndexForFuncExpr(IndexPath *path, int32_t indexcol, Oid funcId, List *args)
{
	Node *operand = (Node *) lsecond(args);

	/* not a const - can't evaluate this here */
	if (!IsA(operand, Const))
	{
		return true;
	}

	/* if no options - thunk to default cost estimation */
	bytea *options = path->indexinfo->opclassoptions[indexcol];
	if (options == NULL)
	{
		return true;
	}

	BsonIndexStrategy strategy = GetBsonStrategyForFuncId(funcId);
	if (strategy == BSON_INDEX_STRATEGY_INVALID)
	{
		return false;
	}

	Datum queryValue = ((Const *) operand)->constvalue;
	return ValidateIndexForQualifierValue(options, queryValue, strategy);
}


/*
 * ValidateMatchForOrderbyQuals walks the order by operator
 * clauses and ensures that every clause is valid for the
 * current index.
 */
static bool
ValidateMatchForOrderbyQuals(IndexPath *path)
{
	ListCell *orderbyCell;
	int index = 0;
	foreach(orderbyCell, path->indexorderbys)
	{
		Expr *orderQual = (Expr *) lfirst(orderbyCell);

		/* Order by on RUM only supports OpExpr clauses */
		if (!IsA(orderQual, OpExpr))
		{
			return false;
		}

		/* Validate that it's a supported operator */
		OpExpr *opQual = (OpExpr *) orderQual;
		if (opQual->opno != BsonOrderByQueryOperatorId())
		{
			return false;
		}

		/* OpExpr for order by always has 2 args */
		Assert(list_length(opQual->args) == 2);
		Expr *secondArg = lsecond(opQual->args);
		if (!IsA(secondArg, Const))
		{
			return false;
		}

		Const *secondConst = (Const *) secondArg;
		int indexColInt = list_nth_int(path->indexorderbycols, index);
		bytea *options = path->indexinfo->opclassoptions[indexColInt];
		if (options == NULL)
		{
			return false;
		}

		/* Validate that the path can be pushed to the index. */
		if (!ValidateIndexForQualifierValue(options, secondConst->constvalue,
											BSON_INDEX_STRATEGY_DOLLAR_ORDERBY))
		{
			return false;
		}

		index++;
	}

	return true;
}


/*
 * Returns true if the IndexPath corresponds to a "text"
 * index. This is used to force the index cost to 0 to make sure
 * we use the text index.
 */
static bool
IsTextIndexMatch(IndexPath *path)
{
	ListCell *cell;
	foreach(cell, path->indexclauses)
	{
		IndexClause *clause = lfirst(cell);
		if (path->indexinfo->opfamily[clause->indexcol] ==
			BsonRumTextPathOperatorFamily())
		{
			return true;
		}
	}

	return false;
}
