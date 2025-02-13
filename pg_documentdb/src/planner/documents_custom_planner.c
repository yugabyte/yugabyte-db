/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/planner/documentdb_custom_planner.c
 *
 * Implementation of the cursor based operations.
 *
 *-------------------------------------------------------------------------
 */
#include <postgres.h>
#include <fmgr.h>
#include <miscadmin.h>
#include <funcapi.h>
#include <utils/portal.h>
#include <utils/varlena.h>
#include <utils/rel.h>
#include <nodes/makefuncs.h>
#include <utils/lsyscache.h>
#include <metadata/metadata_cache.h>
#include <commands/portalcmds.h>
#include "metadata/collection.h"
#include "planner/documents_custom_planner.h"

static bool SetPointReadQualsOnIndexScan(IndexScan *indexScan, Expr *queryQuals);
static List * FormatProjections(List *targetEntries);

/*
 * Does a best effort to create a point read plan on the primary key index.
 * If it's unable to, returns NULL - this will fall back to the default postgres planner.
 */
PlannedStmt *
TryCreatePointReadPlan(Query *query)
{
	if (query->limitOffset != NULL)
	{
		if (IsA(query->limitOffset, Const))
		{
			Const *limitValue = (Const *) query->limitOffset;
			if (!limitValue->constisnull && DatumGetInt64(limitValue->constvalue) != 0)
			{
				/* non-zero offset is not valid for this planner */
				return NULL;
			}
		}
		else
		{
			return NULL;
		}
	}

	if (query->limitCount != NULL)
	{
		if (IsA(query->limitCount, Const))
		{
			Const *limitValue = (Const *) query->limitCount;
			if (!limitValue->constisnull &&
				DatumGetInt64(limitValue->constvalue) < 1)
			{
				/* Limit 0 or less is not valid for this planner */
				return NULL;
			}
		}
		else
		{
			/* Fall back */
			return NULL;
		}
	}

	/* Multi table queries not supported */
	if (list_length(query->rtable) != 1)
	{
		return NULL;
	}

	RangeTblEntry *firstEntry = linitial(query->rtable);

	/* Querying anything but tables not supported */
	if (firstEntry->rtekind != RTE_RELATION)
	{
		return NULL;
	}

	PlannedStmt *stmt = makeNode(PlannedStmt);
	stmt->stmt_len = 0;
	stmt->stmt_location = -1;
	stmt->queryId = query->queryId;
	stmt->commandType = query->commandType;
	stmt->canSetTag = true;

	stmt->rtable = query->rtable;
	stmt->jitFlags = 0;

	/* Create an IndexScan plan on the BTree index */
	IndexScan *indexScan = makeNode(IndexScan);
	indexScan->scan.plan.plan_rows = 1;

	/* Use custom values that hint that it's our plan */
	indexScan->scan.plan.startup_cost = 1.23123e5;
	indexScan->scan.plan.total_cost = 1.23123e6;
	indexScan->scan.plan.lefttree = NULL;
	indexScan->scan.plan.righttree = NULL;
	indexScan->scan.scanrelid = 1;

	/* We don't need order by since it's a point read */
	indexScan->indexorderby = NIL;
	indexScan->indexorderbyorig = NIL;
	indexScan->indexorderbyops = NIL;
	indexScan->indexorderdir = ForwardScanDirection;

	/* Do not add returns before the RelationClose */
	Relation relation = RelationIdGetRelation(firstEntry->relid);
	indexScan->indexid = relation->rd_pkindex;
	if (indexScan->indexid == InvalidOid)
	{
		/* Load the index */
		indexScan->indexid = RelationGetPrimaryKeyIndex(relation);
	}

	RelationClose(relation);

	if (indexScan->indexid == InvalidOid)
	{
		/* We can't detect a PK index - fall back */
		return NULL;
	}

	if (!SetPointReadQualsOnIndexScan(indexScan, (Expr *) query->jointree->quals))
	{
		return NULL;
	}

	/* Finally, filter and set the projections if successful */
	indexScan->scan.plan.targetlist = FormatProjections(query->targetList);
	stmt->planTree = (Plan *) indexScan;

	return stmt;
}


/*
 * Removes unnecessary projections as per the point read plan.
 */
static List *
FormatProjections(List *targetEntries)
{
	if (list_length(targetEntries) == 1)
	{
		return targetEntries;
	}

	ListCell *cell;
	foreach(cell, targetEntries)
	{
		TargetEntry *currEntry = lfirst(cell);
		if (currEntry->resjunk)
		{
			/* unused - remove */
			targetEntries = foreach_delete_current(targetEntries, cell);
		}
	}

	return targetEntries;
}


/*
 * Scan quals and ensure that there's a point read in there.
 * returns false if it couldn't form a point read plan.
 */
static bool
SetPointReadQualsOnIndexScan(IndexScan *indexScan, Expr *queryQuals)
{
	List *runtimeClauses = NIL;
	List *quals = make_ands_implicit(queryQuals);

	ListCell *cell;
	Expr *objectIdExpr = NULL;
	Expr *objectIdOriginalExpr = NULL;
	Expr *shardKeyExpr = NULL;
	Expr *shardKeyOriginalExpr = NULL;
	foreach(cell, quals)
	{
		Expr *expr = (Expr *) lfirst(cell);
		if (IsA(expr, OpExpr))
		{
			OpExpr *opExpr = (OpExpr *) expr;

			if (opExpr->opfuncid == 0)
			{
				/* Set the opFuncId for runtime execution */
				opExpr->opfuncid = get_opcode(opExpr->opno);
			}
			if (list_length(opExpr->args) == 2)
			{
				Expr *firstArg = linitial(opExpr->args);
				if (!IsA(firstArg, Var))
				{
					runtimeClauses = lappend(runtimeClauses, expr);
				}
				else
				{
					Var *firstVar = (Var *) firstArg;
					if (firstVar->varattno ==
						DOCUMENT_DATA_TABLE_OBJECT_ID_VAR_ATTR_NUMBER &&
						opExpr->opno == BsonEqualOperatorId())
					{
						if (objectIdExpr != NULL)
						{
							return NULL;
						}

						OpExpr *newOpExpr = copyObject(opExpr);
						Var *indexVar = makeVar(INDEX_VAR, 2, BsonTypeId(), -1,
												InvalidOid, 0);
						newOpExpr->args = list_make2(indexVar, lsecond(opExpr->args));
						objectIdExpr = (Expr *) newOpExpr;
						objectIdOriginalExpr = (Expr *) opExpr;
					}
					else if (firstVar->varattno ==
							 DOCUMENT_DATA_TABLE_SHARD_KEY_VALUE_VAR_ATTR_NUMBER &&
							 opExpr->opno == BigintEqualOperatorId())
					{
						if (shardKeyExpr != NULL)
						{
							return NULL;
						}

						OpExpr *newOpExpr = copyObject(opExpr);
						Var *indexVar = makeVar(INDEX_VAR, 1, BsonTypeId(), -1,
												InvalidOid, 0);
						newOpExpr->args = list_make2(indexVar, lsecond(opExpr->args));
						shardKeyExpr = (Expr *) newOpExpr;
						shardKeyOriginalExpr = (Expr *) opExpr;
					}
					else
					{
						if (opExpr->opfuncid == BsonEqualMatchRuntimeFunctionId())
						{
							Expr *secondArg = lsecond(opExpr->args);
							pgbsonelement secondElement;
							if (IsA(secondArg, Const) &&
								TryGetSinglePgbsonElementFromPgbson(DatumGetPgBsonPacked(
																		((Const *)
																		 secondArg)->
																		constvalue),
																	&secondElement) &&
								strcmp(secondElement.path, "_id") == 0)
							{
								/* Skip the _id runtime filter: We know we will have
								 * an _id filter by ensuring it below.
								 */
								continue;
							}
						}
						else if (opExpr->opfuncid == BsonTextFunctionId())
						{
							/* Text search does not qualify here */
							return NULL;
						}

						runtimeClauses = lappend(runtimeClauses, expr);
					}
				}
			}
			else
			{
				/* Cannot push to index */
				runtimeClauses = lappend(runtimeClauses, expr);
			}
		}
		else
		{
			/* Cannot push to index */
			if (IsA(expr, FuncExpr))
			{
				FuncExpr *funcExpr = (FuncExpr *) expr;
				if (funcExpr->funcid == BsonTextFunctionId())
				{
					/* Text search does not qualify here */
					return false;
				}
			}

			runtimeClauses = lappend(runtimeClauses, expr);
		}
	}

	/* Not a point read */
	/* Not a point read */
	if (objectIdExpr == NULL || shardKeyExpr == NULL)
	{
		return false;
	}

	indexScan->scan.plan.qual = runtimeClauses;
	indexScan->indexqual = list_make2(shardKeyExpr, objectIdExpr);
	indexScan->indexqualorig = list_make2(shardKeyOriginalExpr, objectIdOriginalExpr);
	return true;
}
