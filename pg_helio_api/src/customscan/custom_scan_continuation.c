/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/customscan/custom_scan_continuation.c
 *
 * Implementation and Definitions for a custom scan for extension that handles cursors.
 *
 * For more details see /docs/indexing/cursors.md
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <fmgr.h>
#include <utils/lsyscache.h>
#include <nodes/extensible.h>
#include <nodes/makefuncs.h>
#include <nodes/nodeFuncs.h>
#include <optimizer/pathnode.h>
#include <optimizer/optimizer.h>
#include <parser/parse_relation.h>
#include <utils/rel.h>
#include <access/detoast.h>
#include <miscadmin.h>
#include <catalog/pg_operator.h>
#include <optimizer/restrictinfo.h>

#include "io/helio_bson_core.h"
#include "customscan/helio_custom_scan.h"
#include "customscan/custom_scan_registrations.h"
#include "metadata/metadata_cache.h"
#include "query/query_operator.h"
#include "catalog/pg_am.h"
#include "commands/cursor_common.h"


#if (PG_VERSION_NUM >= 150000)

/* require_col_privs = true by default */
#define expandNSItemAttrs_compat(pstate, nsitem, sublevels_up, location) \
	expandNSItemAttrs(pstate, nsitem, sublevels_up, true, location)
#else
#define expandNSItemAttrs_compat(pstate, nsitem, sublevels_up, location) \
	expandNSItemAttrs(pstate, nsitem, sublevels_up, location)
#endif


/* --------------------------------------------------------- */
/* Data-types */
/* --------------------------------------------------------- */

/*
 * The input continuation data parsed out during query planning.
 */
typedef struct InputContinuation
{
	/* Must be the first field */
	ExtensibleNode extensible;

	/* The user provided continuation in bson format */
	pgbson *continuation;

	/* The query specified table ID determined at plan time */
	Oid queryTableId;

	/* The query specified table Name that the OID above points to */
	const char *queryTableName;
} InputContinuation;

/*
 * The current query's continuation state. This is mutable
 * and is modified as the current query progresses and
 * enumerates.
 */
typedef struct ContinuationState
{
	/* How many tuples have been enumerated so far */
	uint64_t currentTupleCount;

	/* The size of the tuples enumerated */
	uint64_t currentEnumeratedSize;

	/* The current table ID (Copied from input continuation) */
	Oid currentTableId;

	/* The current table Name (Copied from input continuation) */
	const char *currentTableName;

	/* The current tuple that was just enumerated */
	ItemPointerData currentTuple;

	/* Whether or not the current Tuple is usable and valid */
	bool currentTupleValid;
} ContinuationState;

/*
 * The custom Scan State for the HelioApiScan.
 */
typedef struct ExtensionScanState
{
	/* must be first field */
	CustomScanState custom_scanstate;

	/* The execution state of the inner path */
	ScanState *innerScanState;

	/* The planning state of the inner path */
	Plan *innerPlan;

	/* Extension scan custom fields */

	/* The user requested page size for this query (default 0) */
	uint64_t batchCount;

	/* The total size of the page to fetch (this is not a guarantee but a hint) */
	uint64_t batchSizeHintBytes;

	/* The attribute number of the continuation function */
	AttrNumber contentTrackAttributeNumber;

	/* The continuation state passed in by the user */
	ItemPointerData userContinuationState;

	/* Whether or not to consume the user continuation state */
	bool hasUserContinuationState;

	/* The raw user continuation for explain */
	bson_value_t rawUsercontinuation;

	/* The continuation state tracked for
	 * the current query */
	ContinuationState queryState;
} ExtensionScanState;

/* Continuation state of the currently active query */
static ContinuationState *CurrentQueryState = NULL;

/* Constants used in serialization of cursor state */
const char CursorContinuationTableName[11] = "table_name";
const uint32_t CursorContinuationTableNameLength = 10;
const char CursorContinuationValue[6] = "value";
const uint32_t CursorContinuationValueLength = 5;

extern bool EnableRumIndexScan;

#define InputContinuationNodeName "ExtensionScanInputContinuation"

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */
static Plan * ExtensionScanPlanCustomPath(PlannerInfo *root,
										  RelOptInfo *rel,
										  struct CustomPath *best_path,
										  List *tlist,
										  List *clauses,
										  List *custom_plans);
static Node * ExtensionScanCreateCustomScanState(CustomScan *cscan);
static void ExtensionScanBeginCustomScan(CustomScanState *node, EState *estate,
										 int eflags);
static TupleTableSlot * ExtensionScanExecCustomScan(CustomScanState *node);
static void ExtensionScanEndCustomScan(CustomScanState *node);
static void ExtensionScanReScanCustomScan(CustomScanState *node);
static void ExtensionScanExplainCustomScan(CustomScanState *node, List *ancestors,
										   ExplainState *es);

static void ParseContinuationState(ExtensionScanState *scanState,
								   InputContinuation *continuation);
static TupleTableSlot * ExtensionScanNext(CustomScanState *node);
static TupleTableSlot * SkipWithUserContinuation(ExtensionScanState *state,
												 bool *shouldContinue);
static bool ExtensionScanNextRecheck(ScanState *state, TupleTableSlot *slot);
static void PostProcessSlot(ExtensionScanState *extensionScanState, TupleTableSlot *slot);
static PathTarget * BuildBaseRelPathTarget(Relation tableRel, Index relIdIndex);

static void CopyNodeInputContinuation(ExtensibleNode *target_node, const
									  ExtensibleNode *source_node);
static void OutInputContinuation(StringInfo str, const struct ExtensibleNode *raw_node);
static void ReadUnsupportedExtensionScanNode(struct ExtensibleNode *node);
static bool EqualUnsupportedExtensionScanNode(const struct ExtensibleNode *a,
											  const struct ExtensibleNode *b);
static Node * ReplaceCursorParamValuesMutator(Node *node, ParamListInfo boundParams);


/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */

/* Declaration of extensibility paths for query processing (See extensible.h) */
static const struct CustomPathMethods ExtensionScanPathMethods = {
	.CustomName = "HelioApiScan",
	.PlanCustomPath = ExtensionScanPlanCustomPath,
};

static const struct CustomScanMethods ExtensionScanMethods = {
	.CustomName = "HelioApiScan",
	.CreateCustomScanState = ExtensionScanCreateCustomScanState
};

static const struct CustomExecMethods ExtensionScanExecuteMethods = {
	.CustomName = "HelioApiScan",
	.BeginCustomScan = ExtensionScanBeginCustomScan,
	.ExecCustomScan = ExtensionScanExecCustomScan,
	.EndCustomScan = ExtensionScanEndCustomScan,
	.ReScanCustomScan = ExtensionScanReScanCustomScan,
	.ExplainCustomScan = ExtensionScanExplainCustomScan,
};

static const ExtensibleNodeMethods InputContinuationMethods =
{
	InputContinuationNodeName,
	sizeof(InputContinuation),
	CopyNodeInputContinuation,
	EqualUnsupportedExtensionScanNode,
	OutInputContinuation,
	ReadUnsupportedExtensionScanNode
};

PG_FUNCTION_INFO_V1(command_cursor_state);
PG_FUNCTION_INFO_V1(command_current_cursor_state);

/*
 * Dummy function used to send cursor state to the planner.
 */
Datum
command_cursor_state(PG_FUNCTION_ARGS)
{
	if (CurrentQueryState == NULL)
	{
		ereport(ERROR, (errmsg("This method should not be called directly")));
	}
	else
	{
		PG_RETURN_BOOL(true);
	}
}


/* Serializes the current query's continuation state as
 * a Projection. This can be passed back to resume a
 * query.
 */
Datum
command_current_cursor_state(PG_FUNCTION_ARGS)
{
	if (CurrentQueryState == NULL)
	{
		PG_RETURN_NULL();
	}

	if (!CurrentQueryState->currentTupleValid)
	{
		PG_RETURN_NULL();
	}

	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	PgbsonWriterAppendUtf8(&writer, CursorContinuationTableName,
						   CursorContinuationTableNameLength,
						   CurrentQueryState->currentTableName);

	bson_value_t binaryValue;
	binaryValue.value_type = BSON_TYPE_BINARY;
	binaryValue.value.v_binary.subtype = BSON_SUBTYPE_BINARY;
	binaryValue.value.v_binary.data = (uint8_t *) &CurrentQueryState->currentTuple;
	binaryValue.value.v_binary.data_len = sizeof(ItemPointerData);
	PgbsonWriterAppendValue(&writer, CursorContinuationValue,
							CursorContinuationValueLength,
							&binaryValue);

	PG_RETURN_POINTER(PgbsonWriterGetPgbson(&writer));
}


void
UpdatePathsToForceRumIndexScanToBitmapHeapScan(PlannerInfo *root, RelOptInfo *rel)
{
	ListCell *cell;

	bool allowIndexScans = false;
	if (EnableRumIndexScan)
	{
		/*
		 * Check if we can allow base index scans these can be allowed with
		 * scenarios that have skip/limit:
		 * Let postgres deal with whether a Bitmap path or index path is better
		 * for high limits.
		 */
		allowIndexScans = root->limit_tuples > 0;
	}

	bool hasIndexPaths = false;
	foreach(cell, rel->pathlist)
	{
		Path *inputPath = lfirst(cell);

		if (inputPath->pathtype == T_IndexScan && !allowIndexScans)
		{
			IndexPath *indexPath = (IndexPath *) inputPath;

			/*
			 *  Convert any IndexScan on Rum index to BitmapHeapScan,
			 *  unless BitmapHeapScan is turned off. Rum Index is optimized
			 *  for text search, hence Rum IndexScan always does a sorting of
			 *  the tuples after getting the tuples from the index. This is slow
			 *  if the query needs to fetch a lot of rows. On the contrary, Bitmap
			 *  Heap Scan performs a BitmapIndexScan on the index to create a
			 *  bitmap of the index pages it needs to visit and then hits them
			 *  sequentially. This is faster for large number of rows as the
			 *  index pages are expected to contain multiple matching rows.
			 *
			 *  Once, we have selectivity estimates we can improve on this by
			 *  taking the BitmapHeapScan path only when the selectivity is low
			 *  (more rows), and using IndexScan when selectivity is high (few rows).
			 */
			if (indexPath->indexinfo->relam == RumIndexAmId())
			{
				inputPath = (Path *) create_bitmap_heap_path(root, rel,
															 inputPath,
															 rel->lateral_relids, 1.0,
															 0);

				cell->ptr_value = inputPath;
			}
		}

		if (inputPath->pathtype == T_BitmapHeapScan ||
			inputPath->pathtype == T_IndexScan)
		{
			hasIndexPaths = true;
			break;
		}
	}

	if (hasIndexPaths)
	{
		/* If we have index paths, then trim any parallel seqscans:
		 * Since there's LIMIT and our selectivity today returns low values for
		 * say $eq that match lots of documents, a parallel seqscan can easily
		 * win over index paths. Consequently trim seqscan in the case of index winning.
		 * TODO: Revisit this with selectivity/analyze
		 */
		foreach(cell, rel->partial_pathlist)
		{
			Path *inputPath = lfirst(cell);
			if (inputPath->pathtype == T_SeqScan)
			{
				rel->partial_pathlist = foreach_delete_current(rel->partial_pathlist,
															   cell);
			}
		}
	}
}


/*
 * UpdatePathsWithExtensionCustomPlans walks the built paths for a given query
 * and extracts the continuation state for that path.
 * If there is a continuation state, then builds a custom ExtensionPath that
 * wraps the inner path using that continuation state.
 */
bool
UpdatePathsWithExtensionCustomPlans(PlannerInfo *root, RelOptInfo *rel,
									RangeTblEntry *rte)
{
	/*
	 *  Check if we have a non volatile sort key (aka order by random()).
	 *  Cursor is not supported for non-volatile sort key.
	 *  Currently streaming cursor is also not supported for Table sample.
	 */
	bool hasNonVolatileSortKey = root->sort_pathkeys != NIL;
	bool isTableSample = false;
	if (root->sort_pathkeys != NIL && rte->tablesample != NULL)
	{
		ListCell *lc;
		foreach(lc, root->sort_pathkeys)
		{
			PathKey *pathKey = (PathKey *) lfirst(lc);
			EquivalenceClass *cls = pathKey->pk_eclass;
			if (!cls->ec_has_volatile)
			{
				/* Blocking table sample to be used with sort key other than random() for extension */
				ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
								errmsg(
									"Table Sample can't have any other sort key than random()")));
			}
			else
			{
				hasNonVolatileSortKey = false;
			}
		}

		isTableSample = true;
	}

	if (list_length(rel->baserestrictinfo) < 1)
	{
		return false;
	}

	/* first look for a continuation function in the base quals */
	pgbson *continuation = NULL;
	bool hasContinuation = false;
	ListCell *cell;

	foreach(cell, rel->baserestrictinfo)
	{
		RestrictInfo *rinfo = lfirst_node(RestrictInfo, cell);
		if (IsA(rinfo->clause, FuncExpr))
		{
			FuncExpr *expr = (FuncExpr *) rinfo->clause;
			if (expr->funcid == ApiCursorStateFunctionId())
			{
				if (hasContinuation)
				{
					ereport(ERROR, (errmsg(
										"More than one continuation provided. this is unsupported")));
				}

				if (list_length(expr->args) != 2)
				{
					ereport(ERROR, (errmsg(
										"Invalid cursor state provided - must have 2 arguments.")));
				}

				Node *secondArg = lsecond(expr->args);
				if (IsA(secondArg, Param))
				{
					/*
					 * The only reason why parameters would not be resolved at this stage
					 * is if we are dealing with a generic plan.
					 *
					 * Instead of throwing an error, stop and give the planner another
					 * chance to generate a plan with bound parameters.
					 */
					return false;
				}

				if (!IsA(secondArg, Const))
				{
					ereport(ERROR, (errmsg(
										"Invalid cursor state provided - must be a const value. found: %d",
										secondArg->type)));
				}

				Const *constValue = (Const *) secondArg;
				continuation = (pgbson *) constValue->constvalue;
				hasContinuation = true;
			}
		}
	}

	/* No continuation found. We can skip. */
	if (!hasContinuation)
	{
		return false;
	}

	bool isEmptyTableScan = false;
	if (rte->rtekind == RTE_FUNCTION)
	{
		/* validate if it's the empty table scenario. */
		RangeTblFunction *rangeTblFunc = (RangeTblFunction *) linitial(rte->functions);
		if (IsA(rangeTblFunc->funcexpr, FuncExpr))
		{
			FuncExpr *expr = (FuncExpr *) rangeTblFunc->funcexpr;
			isEmptyTableScan = expr->funcid == BsonEmptyDataTableFunctionId();
		}
	}

	bool validTableFunction = rte->rtekind == RTE_RELATION ||
							  isEmptyTableScan;

	/*
	 *  If a continuation is provided, ensure that the plan paths are valid.
	 */
	if (root->hasJoinRTEs || root->hasRecursion || root->hasLateralRTEs ||
		root->group_pathkeys != NIL ||
		hasNonVolatileSortKey ||
		isTableSample ||
		root->agginfos != NIL || root->hasAlternativeSubPlans ||
		rel->reloptkind != RELOPT_BASEREL || !validTableFunction)
	{
		ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						errmsg(
							"Having continuations not supported for this type of query")));
	}

	if (isEmptyTableScan)
	{
		/* Special case, if it's an empty table scan, just strip the continuation and return */
		foreach(cell, rel->baserestrictinfo)
		{
			RestrictInfo *rinfo = lfirst_node(RestrictInfo, cell);
			if (IsA(rinfo->clause, FuncExpr))
			{
				FuncExpr *expr = (FuncExpr *) rinfo->clause;
				if (expr->funcid == ApiCursorStateFunctionId())
				{
					expr->funcid = BsonTrueFunctionId();
					expr->args = list_make1(linitial(expr->args));
				}
			}
		}

		return false;
	}

	/* Walk the existing paths and wrap them in a custom scan */
	List *customPlanPaths = NIL;
	foreach(cell, rel->pathlist)
	{
		Path *inputPath = lfirst(cell);

		if (inputPath->pathtype == T_IndexScan)
		{
			IndexPath *indexPath = (IndexPath *) inputPath;
			if (indexPath->indexinfo->amhasgetbitmap)
			{
				inputPath = (Path *) create_bitmap_heap_path(root, rel,
															 inputPath,
															 rel->lateral_relids, 1.0, 0);
			}
		}

		Const *tidLowerBoundConst = NULL;
		ItemPointer tidLowerPointPointer = NULL;
		if (inputPath->pathtype == T_SeqScan)
		{
			/* Convert a seqscan to a TidScan */
			if ((rel->amflags & AMFLAG_HAS_TID_RANGE) != 0)
			{
				tidLowerPointPointer = palloc0(sizeof(ItemPointerData));
				tidLowerBoundConst = makeConst(TIDOID, -1, InvalidOid,
											   sizeof(ItemPointerData), PointerGetDatum(
												   tidLowerPointPointer), false,
											   false);
				OpExpr *tidLowerBoundScan = (OpExpr *) make_opclause(
					TIDGreaterEqOperator, BOOLOID, false,
					(Expr *) makeVar(rel->relid, SelfItemPointerAttributeNumber, TIDOID,
									 -1, InvalidOid, 0),
					(Expr *) tidLowerBoundConst, InvalidOid, InvalidOid);
				RestrictInfo *rinfo = make_simple_restrictinfo(root,
															   (Expr *) tidLowerBoundScan);
				inputPath = (Path *) create_tidrangescan_path(root, rel, list_make1(
																  rinfo),
															  rel->lateral_relids);
			}
		}

		if (inputPath->pathtype != T_BitmapHeapScan &&
			inputPath->pathtype != T_TidScan &&
			inputPath->pathtype != T_TidRangeScan)
		{
			/* For now just break if it's not a seq scan or bitmap scan */
			elog(INFO, "Skipping unsupported path type %d", inputPath->pathtype);
			continue;
		}

		/* wrap the path in a custom path */
		CustomPath *customPath = makeNode(CustomPath);
		customPath->methods = &ExtensionScanPathMethods;

		Path *path = &customPath->path;
		path->pathtype = T_CustomScan;

		/* copy the parameters from the inner path */
		Assert(inputPath->parent == rel);
		path->parent = rel;

		/* we don't support lateral joins here so required outer is 0 */
		Relids requiredOuter = 0;
		path->param_info = get_baserel_parampathinfo(root, rel, requiredOuter);

		/* Copy scalar values in from the inner path */
		path->rows = rel->rows;
		path->startup_cost = inputPath->startup_cost;
		path->total_cost = inputPath->total_cost;

		/* For now the custom path is not parallel safe */
		path->parallel_safe = false;

		/* move the 'projection' from the path to the custom path. */
		path->pathtarget = inputPath->pathtarget;

		/* Extract the base rel for the query */
		Relation tableRel = RelationIdGetRelation(rte->relid);

		/* Extract the table name (used to recognize continuation) */
		const char *tableName = pstrdup(NameStr(tableRel->rd_rel->relname));

		/* Point the nested scan's projection to the base table's projection */
		inputPath->pathtarget = BuildBaseRelPathTarget(tableRel, rel->relid);

		/* Ensure you close the rel */
		RelationClose(tableRel);

		customPath->custom_paths = list_make1(inputPath);

#if (PG_VERSION_NUM >= 150000)

		/* necessary to avoid extra Result node in PG15 */
		customPath->flags = CUSTOMPATH_SUPPORT_PROJECTION;
#endif

		/* store the continuation data */
		InputContinuation *inputContinuation = palloc0(sizeof(InputContinuation));
		inputContinuation->extensible.type = T_ExtensibleNode;
		inputContinuation->extensible.extnodename = InputContinuationNodeName;
		inputContinuation->continuation = continuation;
		inputContinuation->queryTableId = rte->relid;
		inputContinuation->queryTableName = tableName;

		if (tidLowerBoundConst != NULL)
		{
			ExtensionScanState scanState;
			memset(&scanState, 0, sizeof(ExtensionScanState));
			ParseContinuationState(&scanState, inputContinuation);
			if (scanState.hasUserContinuationState)
			{
				*tidLowerPointPointer = scanState.userContinuationState;
				tidLowerBoundConst->constvalue = PointerGetDatum(tidLowerPointPointer);
			}
		}

		/* Store the input continuation to be used later, as well as the inner projection
		 * target List
		 * NOTE: Anything added here must be of type ExtensibleNode and must be registered
		 * with the RegisterNodes method below.
		 */
		customPath->custom_private = list_make1(inputContinuation);
		customPlanPaths = lappend(customPlanPaths, customPath);
	}

	if (customPlanPaths == NIL)
	{
		Path *firstPath = (Path *) linitial(rel->pathlist);

		ereport(ERROR,
				(errmsg(
					 "Unsupported scan paths detected. Cursors cannot be run with these paths. First Type %d",
					 firstPath->pathtype)));
	}

	/* Don't need to handle parallel paths since custom_scan function is not parallel safe */
	rel->pathlist = customPlanPaths;

	/* If we got here, we need ordering on CTID, disable parallel scan
	 * This is because streaming cursors need monotonically increasing order for
	 * tuples and we can't allow parallel scan to reorder tuples.
	 */
	rel->partial_pathlist = NIL;
	return true;
}


/*
 * Registers any custom nodes that the Extension Scan produces.
 * This is for any items present in the custom_private field.
 */
void
RegisterScanNodes(void)
{
	RegisterExtensibleNodeMethods(&InputContinuationMethods);
}


/*
 * When streaming cursors are enabled, we only expect the root rel
 * based plan, or a limit plan with the inner statement being a cursor.
 * This is because a streaming cursor only allows immutable statements
 * currently, and that should be inlined into 1 baserel query.
 */
void
ValidateCursorCustomScanPlan(Plan *plan)
{
	CHECK_FOR_INTERRUPTS();
	switch (plan->type)
	{
		case T_CustomScan:
		{
			CustomScan *scan = castNode(CustomScan, plan);

			/* Custom scans today are Citus and HelioApi - if it's not HelioApi - just check the subtree */
			if (scan->methods != &ExtensionScanMethods)
			{
				if (scan->scan.plan.lefttree != NULL)
				{
					ValidateCursorCustomScanPlan(scan->scan.plan.lefttree);
				}

				if (scan->scan.plan.righttree != NULL)
				{
					ValidateCursorCustomScanPlan(scan->scan.plan.righttree);
				}
			}

			return;
		}

		case T_Limit:
		{
			Limit *limit = castNode(Limit, plan);
			if (limit->limitOffset != NULL)
			{
				ereport(ERROR,
						(errmsg(
							 "Found unsupported limit for stream cursors with offset")));
			}

			ValidateCursorCustomScanPlan(limit->plan.lefttree);
			return;
		}

		case T_FunctionScan:
		{
			FunctionScan *scan = castNode(FunctionScan, plan);
			if (list_length(scan->functions) != 1)
			{
				ereport(ERROR,
						(errmsg(
							 "Found unsupported function scan path for cursors with %d functions",
							 list_length(scan->functions))));
			}

			RangeTblFunction *rtfunc = (RangeTblFunction *) linitial(scan->functions);
			if (IsA(rtfunc->funcexpr, FuncExpr))
			{
				FuncExpr *funcexpr = (FuncExpr *) rtfunc->funcexpr;
				Oid funcid = funcexpr->funcid;

				if (funcid != BsonEmptyDataTableFunctionId())
				{
					char *objectname = get_func_name(funcid);
					ereport(ERROR,
							(errmsg("Found unsupported cursor function scan: %s",
									objectname)));
				}
			}
			else
			{
				elog(NOTICE, "Unexpected entry for cursor functional scan: %d",
					 plan->type);
				ereport(ERROR, (errmsg("Unexpected entry for cursor functional scan")));
			}

			return;
		}

		case T_Result:
		{
			/* Queries that can evaluate to a const (e.g. a filter of $alwaysFalse) can be made into a Result. */
			Result *result = castNode(Result, plan);
			if (result->plan.lefttree != NULL || result->plan.righttree != NULL ||
				result->resconstantqual == NULL)
			{
				elog(LOG,
					 "Unsupported combination of query with streaming cursors, found result with leftPlan %d, rightPlan %d, const %d",
					 result->plan.lefttree != NULL ?
					 result->plan.lefttree->type : 0,
					 result->plan.righttree != NULL ?
					 result->plan.righttree->type : 0,
					 result->resconstantqual != NULL);

				/* Raise the error without the enum (to avoid cross PG version values). */
				ereport(ERROR, (errmsg(
									"Unsupported combination of query with streaming cursors")));
			}

			return;
		}

		default:
		{
			/* Log the notice in server/client logs */
			elog(LOG,
				 "Unsupported combination of query with streaming cursors, found %d",
				 plan->type);

			/* Raise the error without the enum (to avoid cross PG version values). */
			ereport(ERROR, (errmsg(
								"Unsupported combination of query with streaming cursors")));
		}
	}
}


/*
 * When doing Explain Analyze, the parameter values aren't available in the worker.
 * To avoid this issue, we apply the same hack that is in the helio planner to
 * replace the param value with the replaced const, and use the bson_true_function
 * on the param to ensure it gets sent to the worker.
 * One of the tracking bugs: https://github.com/citusdata/citus/issues/5787
 */
Query *
ReplaceCursorParamValues(Query *query, ParamListInfo boundParams)
{
	if (boundParams == NULL)
	{
		return query;
	}

	return (Query *) ReplaceCursorParamValuesMutator((Node *) query, boundParams);
}


/* --------------------------------------------------------- */
/* Helper methods exports */
/* --------------------------------------------------------- */

/*
 * ReplaceCursorParamValuesMutator is a mutator that replaces all occurrences
 * of parameter values for the cursor state function
 * with the actual value for the cursor state function.
 */
static Node *
ReplaceCursorParamValuesMutator(Node *node, ParamListInfo boundParams)
{
	if (node == NULL)
	{
		return NULL;
	}

	if (IsA(node, FuncExpr))
	{
		FuncExpr *funcExpr = (FuncExpr *) node;

		if (funcExpr->funcid == ApiCursorStateFunctionId())
		{
			/* operator always has 2 arguments */
			Assert(list_length(funcExpr->args) == 2);

			Node *queryNode = lsecond(funcExpr->args);
			if (IsA(queryNode, Param))
			{
				Node *modifiedNode = EvaluateBoundParameters(queryNode, boundParams);
				funcExpr->args = list_make2(linitial(funcExpr->args), modifiedNode);

				FuncExpr *trueFunction = makeFuncExpr(
					BsonTrueFunctionId(), BOOLOID, list_make1(queryNode), InvalidOid,
					InvalidOid, COERCE_EXPLICIT_CALL);

				List *andQuals = list_make2(funcExpr, trueFunction);
				return (Node *) make_ands_explicit(andQuals);
			}
		}

		return node;
	}
	else if (IsA(node, Query))
	{
		Query *currentQuery = (Query *) node;

		/* also descend into subqueries */
		Query *result = query_tree_mutator(currentQuery, ReplaceCursorParamValuesMutator,
										   boundParams, 0);
		return (Node *) result;
	}

	return expression_tree_mutator(node, ReplaceCursorParamValuesMutator, boundParams);
}


/*
 * Given a scan path for the extension path, generates a
 * Custom Plan for the path. Note that the inner path
 * is already planned since it is listed as an inner_path
 * in the custom path above.
 */
static Plan *
ExtensionScanPlanCustomPath(PlannerInfo *root,
							RelOptInfo *rel,
							struct CustomPath *best_path,
							List *tlist,
							List *clauses,
							List *custom_plans)
{
	CustomScan *cscan = makeNode(CustomScan);

	/* Initialize and copy necessary data */
	cscan->methods = &ExtensionScanMethods;

	/* The first item is the continuation - we propagate it forward */
	cscan->custom_private = best_path->custom_private;
	cscan->custom_plans = custom_plans;

	Plan *nestedPlan = linitial(custom_plans);

	/* TODO: clear the filters in the nested plan (so we don't load the document in the nested plan) */
	/* This is the output of the scan */
	if (tlist != NIL)
	{
		cscan->scan.plan.targetlist = tlist;
	}
	else
	{
		cscan->scan.plan.targetlist = root->processed_tlist;
	}

	/* This is the input to the custom scan */
	cscan->custom_scan_tlist = nestedPlan->targetlist;

#if (PG_VERSION_NUM >= 150000)

	/* necessary to avoid extra Result node in PG15 */
	cscan->flags = CUSTOMPATH_SUPPORT_PROJECTION;
#endif

	return (Plan *) cscan;
}


/*
 * Given a custom scan generated during the plan phase
 * Creates a Custom ScanState that is used during the
 * execution of the plan.
 * This is called at the beginning of query execution
 * by the executor.
 */
static Node *
ExtensionScanCreateCustomScanState(CustomScan *cscan)
{
	ExtensionScanState *extensionScanState = (ExtensionScanState *) newNode(
		sizeof(ExtensionScanState), T_CustomScanState);

	CustomScanState *cscanstate = &extensionScanState->custom_scanstate;
	cscanstate->methods = &ExtensionScanExecuteMethods;

	/* Here we don't store the custom plan inside the custom_ps of the custom scan state yet
	 * This is done as part of BeginCustomScan */
	Plan *innerPlan = (Plan *) linitial(cscan->custom_plans);
	extensionScanState->innerPlan = innerPlan;
	extensionScanState->contentTrackAttributeNumber = InvalidAttrNumber;

	/* Parse and store continuation state */
	InputContinuation *continuation = (InputContinuation *) linitial(
		cscan->custom_private);
	if (continuation != NULL)
	{
		ParseContinuationState(extensionScanState, continuation);
	}

	if ((extensionScanState->batchSizeHintBytes > 0) ^
		(extensionScanState->contentTrackAttributeNumber > 0))
	{
		ereport(ERROR, (errmsg(
							"both batchSizeHint and batchSizeAttr must be set - or neither")));
	}

	/* The attrnumber must be in the tlist */
	if (extensionScanState->contentTrackAttributeNumber > list_length(
			cscan->scan.plan.targetlist))
	{
		ereport(ERROR, (errmsg(
							"content track attribute must be within the projected targetlist")));
	}

	return (Node *) cscanstate;
}


static void
ExtensionScanBeginCustomScan(CustomScanState *node, EState *estate,
							 int eflags)
{
	/* Initialize the actual state of the plan */
	ExtensionScanState *extensionScanState = (ExtensionScanState *) node;
	extensionScanState->innerScanState = (ScanState *) ExecInitNode(
		extensionScanState->innerPlan, estate, eflags);

	/* Store the inner state here so that EXPLAIN works */
	extensionScanState->custom_scanstate.custom_ps = list_make1(
		extensionScanState->innerScanState);

	/* Set the currently tracked state for projections */
	CurrentQueryState = &extensionScanState->queryState;
}


static void
ExtensionScanEndCustomScan(CustomScanState *node)
{
	ExtensionScanState *extensionScanState = (ExtensionScanState *) node;

	/* reset any scanstate state here */
	CurrentQueryState = NULL;

	ExecEndNode((PlanState *) extensionScanState->innerScanState);
}


static void
ExtensionScanReScanCustomScan(CustomScanState *node)
{
	ExtensionScanState *extensionScanState = (ExtensionScanState *) node;

	/* reset any scanstate state here */
	extensionScanState->queryState.currentTupleCount = 0;
	extensionScanState->queryState.currentTupleValid = false;

	ExecReScan((PlanState *) extensionScanState->innerScanState);
}


static void
ExtensionScanExplainCustomScan(CustomScanState *node, List *ancestors,
							   ExplainState *es)
{
	ExtensionScanState *extensionScanState = (ExtensionScanState *) node;

	/* Explain any extension specific state */
	if (extensionScanState->batchCount > 0)
	{
		ExplainPropertyInteger("Page Row Count", "rows", extensionScanState->batchCount,
							   es);
	}

	if (extensionScanState->batchSizeHintBytes > 0)
	{
		ExplainPropertyInteger("Page Size Hint", "bytes",
							   extensionScanState->batchSizeHintBytes, es);
	}

	if (extensionScanState->rawUsercontinuation.value_type != BSON_TYPE_EOD)
	{
		ExplainPropertyText("Continuation", BsonValueToJsonForLogging(
								&extensionScanState->rawUsercontinuation), es);
	}
}


static TupleTableSlot *
ExtensionScanExecCustomScan(CustomScanState *pstate)
{
	ExtensionScanState *node = (ExtensionScanState *) pstate;

	/*
	 * Call ExecScan with the next/recheck methods. This handles
	 * Post-processing for projections, custom filters etc.
	 */
	TupleTableSlot *returnSlot = ExecScan(&node->custom_scanstate.ss,
										  (ExecScanAccessMtd) ExtensionScanNext,
										  (ExecScanRecheckMtd) ExtensionScanNextRecheck);

	if (!TupIsNull(returnSlot) && node->contentTrackAttributeNumber > InvalidAttrNumber)
	{
		if (returnSlot->tts_nvalid < node->contentTrackAttributeNumber)
		{
			/* Ensure we've got some valid attributes */
			returnSlot->tts_ops->getsomeattrs(returnSlot,
											  returnSlot->tts_tupleDescriptor->natts);
		}

		if (node->contentTrackAttributeNumber <= returnSlot->tts_tupleDescriptor->natts)
		{
			/* attribute numbers are 1 based */
			int index = node->contentTrackAttributeNumber - 1;
			Oid currentTypeId = returnSlot->tts_tupleDescriptor->attrs[index].atttypid;
			if (currentTypeId == BsonTypeId() && !returnSlot->tts_isnull[index])
			{
				/*
				 * Track all bsons being returned - We skip the continuation but track all others.
				 * This also means the filtering returns one extra row to the caller, but that's also okay since the caller
				 * handles the filtering down to the actual page size.
				 */
				Size bsonSize = toast_raw_datum_size(returnSlot->tts_values[index]) -
								VARHDRSZ;
				CurrentQueryState->currentEnumeratedSize += bsonSize;
			}
		}
	}

	return returnSlot;
}


/*
 * Gets the actual underlying tuple stable slot for the scan.
 * This points to the actual CTID executed (For SeqScans the heap slot,
 * and for bitmap scans, the slot from the index).
 */
inline static TupleTableSlot *
GetOriginalSlot(ScanState *state, TupleTableSlot *slot)
{
	if (state->ps.ps_ExprContext->ecxt_scantuple != NULL)
	{
		return state->ps.ps_ExprContext->ecxt_scantuple;
	}

	return slot;
}


/* Post process the slot that we get from the inner scan and ensure that
 * we set any continuation state data.
 */
static void
PostProcessSlot(ExtensionScanState *extensionScanState, TupleTableSlot *slot)
{
	/* Increment the tuples we've seen and return the slot we just got */
	extensionScanState->queryState.currentTupleCount++;

	/* Store the actual slot visited */
	TupleTableSlot *originalSlot = GetOriginalSlot(extensionScanState->innerScanState,
												   slot);
	if (originalSlot->tts_tableOid == extensionScanState->queryState.currentTableId)
	{
		extensionScanState->queryState.currentTuple = originalSlot->tts_tid;
		extensionScanState->queryState.currentTupleValid = true;
	}
	else
	{
		extensionScanState->queryState.currentTupleValid = false;
	}
}


/*
 * Executes the inner scan and gets the next available Tuple for the query.
 */
static TupleTableSlot *
ExtensionScanNext(CustomScanState *node)
{
	ExtensionScanState *extensionScanState = (ExtensionScanState *) node;

	TupleTableSlot *slot;
	if (extensionScanState->hasUserContinuationState)
	{
		bool shouldContinue = false;
		slot = SkipWithUserContinuation(extensionScanState, &shouldContinue);
		extensionScanState->hasUserContinuationState = false;
		if (slot != NULL)
		{
			PostProcessSlot(extensionScanState, slot);
			return slot;
		}
		else if (!shouldContinue)
		{
			return slot;
		}
	}

	/* Fetch a tuple from the underlying scan */
	slot = extensionScanState->innerScanState->ps.ExecProcNode(
		(PlanState *) extensionScanState->innerScanState);

	/* We're done scanning, so return NULL */

	if (TupIsNull(slot))
	{
		extensionScanState->queryState.currentTupleValid = false;
		return slot;
	}

	/* Check that we're under the page size. If we already exhausted the page size, return NULL */
	if (extensionScanState->batchCount > 0 &&
		extensionScanState->queryState.currentTupleCount >=
		extensionScanState->batchCount)
	{
		extensionScanState->queryState.currentTupleValid = false;
		return NULL;
	}

	if (extensionScanState->batchSizeHintBytes > 0 &&
		extensionScanState->queryState.currentEnumeratedSize >=
		extensionScanState->batchSizeHintBytes)
	{
		extensionScanState->queryState.currentTupleValid = false;
		return NULL;
	}

	/* Copy the slot onto our own query state for projection */
	PostProcessSlot(extensionScanState, slot);
	TupleTableSlot *ourSlot = node->ss.ss_ScanTupleSlot;
	return ExecCopySlot(ourSlot, slot);
}


/*
 * Runs the "recheck" flow for any tuples marked for recheck.
 * This is noop for the extension scan since the recheck is done by the inner scan
 * at this point.
 */
static bool
ExtensionScanNextRecheck(ScanState *state, TupleTableSlot *slot)
{
	/* The underlying scan takes care of recheck since we call ExecProcNode directly. We shouldn't need recheck */
	ereport(ERROR, (errmsg("Recheck is unexpected on Custom Scan")));
}


/*
 * Parses the incoming continuation to build the continuation state
 * For the current query.
 */
static void
ParseContinuationState(ExtensionScanState *extensionScanState,
					   InputContinuation *continuation)
{
	extensionScanState->queryState.currentTableId = continuation->queryTableId;
	extensionScanState->queryState.currentTableName = continuation->queryTableName;

	bson_iter_t continuationIterator;
	PgbsonInitIterator(continuation->continuation, &continuationIterator);
	while (bson_iter_next(&continuationIterator))
	{
		const char *currentField = bson_iter_key(&continuationIterator);
		if (strcmp(currentField, "getpage_batchCount") == 0)
		{
			if (!BSON_ITER_HOLDS_NUMBER(&continuationIterator))
			{
				ereport(ERROR, (errmsg("batchCount must be a number.")));
			}
			else if (extensionScanState->batchCount > 0)
			{
				ereport(ERROR, (errmsg("batchCount cannot be specified twice.")));
			}

			extensionScanState->batchCount = BsonValueAsInt64(bson_iter_value(
																  &continuationIterator));
		}
		else if (strcmp(currentField, "getpage_batchSizeAttr") == 0)
		{
			if (!BSON_ITER_HOLDS_NUMBER(&continuationIterator))
			{
				ereport(ERROR, (errmsg("batchSizeAttr must be a number.")));
			}
			else if (extensionScanState->contentTrackAttributeNumber > 0)
			{
				ereport(ERROR, (errmsg("batchSizeAttr cannot be specified twice.")));
			}

			int32_t batchSizeAttribute = BsonValueAsInt32(bson_iter_value(
															  &continuationIterator));
			extensionScanState->contentTrackAttributeNumber =
				(AttrNumber) batchSizeAttribute;
		}
		else if (strcmp(currentField, "getpage_batchSizeHint") == 0)
		{
			if (!BSON_ITER_HOLDS_NUMBER(&continuationIterator))
			{
				ereport(ERROR, (errmsg("batchSizeHint must be a number.")));
			}
			else if (extensionScanState->batchSizeHintBytes > 0)
			{
				ereport(ERROR, (errmsg("batchSizeHint cannot be specified twice.")));
			}

			int32_t batchSizeHint = BsonValueAsInt32(bson_iter_value(
														 &continuationIterator));
			extensionScanState->batchSizeHintBytes = (uint64_t) batchSizeHint;
		}
		else if (strcmp(currentField, "continuation") == 0)
		{
			bson_iter_t continuationArray;
			if (!BSON_ITER_HOLDS_ARRAY(&continuationIterator) ||
				!bson_iter_recurse(&continuationIterator, &continuationArray))
			{
				ereport(ERROR, (errmsg("continuation must be an array.")));
			}

			while (bson_iter_next(&continuationArray))
			{
				bson_iter_t singleContinuationDoc;
				if (!BSON_ITER_HOLDS_DOCUMENT(&continuationArray) ||
					!bson_iter_recurse(&continuationArray, &singleContinuationDoc))
				{
					ereport(ERROR, (errmsg("continuation element must be a document.")));
				}
				const bson_value_t *currentValue = bson_iter_value(&continuationArray);
				const char *tableName = NULL;
				bson_value_t continuationBinaryValue = { 0 };
				while (bson_iter_next(&singleContinuationDoc))
				{
					if (strcmp(bson_iter_key(&singleContinuationDoc),
							   CursorContinuationTableName) == 0)
					{
						if (!BSON_ITER_HOLDS_UTF8(&singleContinuationDoc))
						{
							ereport(ERROR, (errmsg("Expecting string value for %s",
												   CursorContinuationTableName)));
						}

						tableName = bson_iter_utf8(&singleContinuationDoc, NULL);
					}
					else if (strcmp(bson_iter_key(&singleContinuationDoc),
									CursorContinuationValue) == 0)
					{
						continuationBinaryValue = *bson_iter_value(
							&singleContinuationDoc);
					}
				}

				if (tableName == NULL || strcmp(tableName,
												continuation->queryTableName) != 0)
				{
					continue;
				}

				if (continuationBinaryValue.value_type != BSON_TYPE_BINARY)
				{
					ereport(ERROR, (errmsg("Expecting binary value for %s",
										   CursorContinuationValue)));
				}

				if (continuationBinaryValue.value.v_binary.data_len !=
					sizeof(ItemPointerData))
				{
					ereport(ERROR, (errmsg(
										"Invalid length for binary value %d, expecting %d",
										continuationBinaryValue.value.v_binary.data_len,
										(int) sizeof(ItemPointerData))));
				}

				extensionScanState->userContinuationState =
					*(ItemPointerData *) continuationBinaryValue.value.v_binary.data;
				extensionScanState->rawUsercontinuation = *currentValue;
				extensionScanState->hasUserContinuationState = true;
			}
		}
		else
		{
			ereport(ERROR, (errmsg("Unknown continuation field %s", currentField)));
		}
	}
}


/*
 * Skips enumerating rows until the specified continuation is hit.
 * If the enumeration lands *after* the given continuation, returns the tuple
 * If the enumeration ends before the continuation is hit, returns NULL and shouldContinue = false.
 * If the enumeration ends at the continuation point, returns NULL and sets shouldContinue = true.
 */
static TupleTableSlot *
SkipWithUserContinuation(ExtensionScanState *state, bool *shouldContinue)
{
	*shouldContinue = false;
	while (true)
	{
		TupleTableSlot *slot = state->innerScanState->ps.ExecProcNode(
			(PlanState *) state->innerScanState);
		if (TupIsNull(slot))
		{
			return slot;
		}

		/* With SeqScans, the slots are stored in the exct. So we retrieve it from there
		 * For bitmap heap scans, that field is null and so we retrieve the slot directly
		 * Note that there is an implicit dependency that the slot is returned in ascending
		 * slot order. This does present a small problem with vacuum and autovacuum. These
		 * would need to be reconciled as we build more scans on top of this.
		 * TODO: Consider impact of VACUUM on the cursor state execution and skip
		 */
		TupleTableSlot *originalSlot = GetOriginalSlot(state->innerScanState, slot);
		if (ItemPointerCompare(&originalSlot->tts_tid, &state->userContinuationState) ==
			0)
		{
			*shouldContinue = true;
			return NULL;
		}

		if (ItemPointerCompare(&originalSlot->tts_tid, &state->userContinuationState) > 0)
		{
			/* already found a slot after the continuation. return. */
			return slot;
		}
	}
}


/*
 * Builds a PathTarget that is valid for a base table Relation.
 */
static PathTarget *
BuildBaseRelPathTarget(Relation tableRel, Index relIdIndex)
{
	PathTarget *pathTarget = makeNode(PathTarget);
	pathTarget->cost.per_tuple = 0;
	pathTarget->cost.startup = 0;
	pathTarget->has_volatile_expr = VOLATILITY_UNKNOWN;
	pathTarget->sortgrouprefs = 0;

	/* make the inner path project the base projection */
	ParseState *pstate = make_parsestate(NULL);

	/*
	 * Follow the logic for SELECT * - see parse_target.c
	 * We construct a ParseNameItem, and expand the rels into
	 * vars. This is passed to the inner path so we don't apply
	 * projections in the inner path.
	 * From it we construct a pathTarget as if we're applying a
	 * SELECT * and
	 */
	ParseNamespaceItem *item = addRangeTableEntryForRelation(pstate,
															 tableRel,
															 AccessShareLock,
															 NULL,
															 false,
															 false);
	List *tlist = expandNSItemAttrs_compat(pstate, item, 0, 0);

	/* Now set the actual vars into the PathTarget */
	List *exprs = NIL;
	ListCell *targetEntryCell;
	foreach(targetEntryCell, tlist)
	{
		TargetEntry *entry = (TargetEntry *) lfirst(targetEntryCell);
		if (IsA(entry->expr, Var))
		{
			Var *var = (Var *) entry->expr;
			var->varno = relIdIndex;
		}

		exprs = lappend(exprs, entry->expr);
	}

	pathTarget->exprs = exprs;
	pathTarget->width = get_rel_data_width(tableRel, NULL);
	return pathTarget;
}


/*
 * Function for reading HelioApiScan node (unsupported)
 */
static void
ReadUnsupportedExtensionScanNode(struct ExtensibleNode *node)
{
	ereport(ERROR, (errmsg("Read for node type not implemented")));
}


/*
 * Support for comparing two Scan extensible nodes
 * Currently insupported.
 */
static bool
EqualUnsupportedExtensionScanNode(const struct ExtensibleNode *a,
								  const struct ExtensibleNode *b)
{
	ereport(ERROR, (errmsg("Equal for node type not implemented")));
}


/*
 * Support for Copying the InputContinuation node
 */
static void
CopyNodeInputContinuation(struct ExtensibleNode *target_node, const struct
						  ExtensibleNode *source_node)
{
	InputContinuation *from = (InputContinuation *) source_node;

	InputContinuation *newNode = (InputContinuation *) target_node;
	newNode->extensible.type = T_ExtensibleNode;
	newNode->extensible.extnodename = InputContinuationNodeName;
	newNode->continuation = PgbsonCloneFromPgbson(from->continuation);
	newNode->queryTableId = from->queryTableId;
	newNode->queryTableName = pstrdup(from->queryTableName);
}


/*
 * Support for Outputing the InputContinuation node
 */
static void
OutInputContinuation(StringInfo str, const struct ExtensibleNode *raw_node)
{
	/* TODO: This doesn't seem needed */
}
