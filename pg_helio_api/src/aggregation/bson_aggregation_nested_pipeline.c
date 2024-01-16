/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/planner/bson_aggregation_nested_pipeline.c
 *
 * Implementation of the backend query generation for pipelines that have
 * nested pipelines (such as $lookup, $facet).
 *
 *-------------------------------------------------------------------------
 */


#include <postgres.h>
#include <float.h>
#include <fmgr.h>
#include <miscadmin.h>

#include <optimizer/planner.h>
#include <nodes/nodes.h>
#include <nodes/makefuncs.h>
#include <nodes/nodeFuncs.h>
#include <parser/parser.h>
#include <parser/parse_agg.h>
#include <parser/parse_clause.h>
#include <parser/parse_param.h>
#include <parser/analyze.h>
#include <parser/parse_oper.h>
#include <utils/ruleutils.h>
#include <utils/builtins.h>
#include <catalog/pg_aggregate.h>
#include <catalog/pg_class.h>

#include "io/helio_bson_core.h"
#include "metadata/metadata_cache.h"
#include "query/query_operator.h"
#include "planner/helio_planner.h"
#include "aggregation/bson_aggregation_pipeline.h"
#include "commands/parse_error.h"
#include "commands/commands_common.h"
#include "utils/feature_counter.h"
#include "operators/bson_expression.h"

#include "aggregation/bson_aggregation_pipeline_private.h"

const int MaximumLookupPipelineDepth = 20;

/*
 * Struct having parsed view of the
 * arguments to Lookup.
 */
typedef struct
{
	/*
	 * The name of the collection to lookup
	 */
	StringView from;

	/*
	 * The alias where the results of the lookup are placed
	 */
	StringView lookupAs;

	/*
	 * The remote field to match (if applicable)
	 */
	StringView foreignField;

	/*
	 * The local field to match (if applicable)
	 */
	StringView localField;

	/*
	 * A pipeline to execute (for uncorrelated lookups)
	 */
	bson_value_t pipeline;

	/*
	 * has a join (foreign/local field).
	 */
	bool hasLookupMatch;
} LookupArgs;

static bool CanInlineInnerLookupPipeline(LookupArgs *lookupArgs);
static int ValidateFacet(const bson_value_t *facetValue);
static Query * BuildFacetUnionAllQuery(int numStages, const bson_value_t *facetValue,
									   CommonTableExpr *baseCte, QuerySource querySource,
									   Datum databaseNameDatum, const
									   bson_value_t *sortSpec);
static Query * AddBsonArrayAggFunction(Query *baseQuery,
									   AggregationPipelineBuildContext *context,
									   ParseState *parseState, const char *fieldPath,
									   uint32_t fieldPathLength, bool migrateToSubQuery);
static Query * AddBsonObjectAggFunction(Query *baseQuery,
										AggregationPipelineBuildContext *context);
static void ParseLookupStage(const bson_value_t *existingValue, LookupArgs *args);
static Query * CreateCteSelectQuery(CommonTableExpr *baseCte, const char *prefix,
									int stageNum, int levelsUp);
static Query * ProcessLookupCore(Query *query, AggregationPipelineBuildContext *context,
								 LookupArgs *lookupArgs);
static void ValidateUnionWithPipeline(const bson_value_t *pipeline);


/*
 * Helper function that creates a UNION ALL Set operation statement
 * that returns a single BSON field.
 */
inline static SetOperationStmt *
MakeBsonSetOpStatement(void)
{
	SetOperationStmt *setOpStatement = makeNode(SetOperationStmt);
	setOpStatement->all = true;
	setOpStatement->op = SETOP_UNION;
	setOpStatement->colCollations = list_make1_oid(InvalidOid);
	setOpStatement->colTypes = list_make1_oid(BsonTypeId());
	setOpStatement->colTypmods = list_make1_int(-1);
	return setOpStatement;
}


/*
 * Validates and returns a given pipeline stage: Used in validations for facet/lookup/unionWith
 */
inline static pgbsonelement
GetPipelineStage(bson_iter_t *pipelineIter, const char *parentStage, const
				 char *pipelineKey)
{
	const bson_value_t *pipelineStage = bson_iter_value(pipelineIter);
	if (!BSON_ITER_HOLDS_DOCUMENT(pipelineIter))
	{
		ereport(ERROR, (errcode(MongoBadValue),
						errmsg(
							"Pipeline stage for %s %s must be a document",
							parentStage, pipelineKey)));
	}

	pgbsonelement stageElement;
	if (!TryGetBsonValueToPgbsonElement(pipelineStage, &stageElement))
	{
		ereport(ERROR, (errcode(MongoLocation40323),
						errmsg(
							"A pipeline stage specification object must contain exactly one field.")));
	}

	return stageElement;
}


/*
 * Processes the $_internalInhibitOptimization Pipeine stage.
 * Injects a CTE into the pipeline with the MaterializeAlways flag
 * generating a break in the pipeline.
 */
Query *
HandleInternalInhibitOptimization(const bson_value_t *existingValue, Query *query,
								  AggregationPipelineBuildContext *context)
{
	/* First step, move the current query into a CTE */
	CommonTableExpr *baseCte = makeNode(CommonTableExpr);
	baseCte->ctename = "internalinhibitoptimization";
	baseCte->ctequery = (Node *) query;

	/* Mark it as materialize always */
	baseCte->ctematerialized = CTEMaterializeAlways;
	int levelsUp = 0;
	query = CreateCteSelectQuery(baseCte, "inhibit", context->stageNum, levelsUp);
	query->cteList = list_make1(baseCte);
	return query;
}


/*
 * Processes the $facet Pipeine stage.
 * Injects a CTE for the current query.
 * Then builds a UNION ALL query that has the N pipelines
 * selecting from the common CTE.
 * Finally aggregates all of it into a BSON_OBJECT_AGG
 */
Query *
HandleFacet(const bson_value_t *existingValue, Query *query,
			AggregationPipelineBuildContext *context)
{
	ReportFeatureUsage(FEATURE_STAGE_FACET);
	if (context->nestedPipelineLevel > 0)
	{
		ereport(ERROR, (errmsg(
							"Unexpected, facet should not be called in a nested pipeline")));
	}

	if (list_length(query->targetList) > 1)
	{
		/* if we have multiple projectors, push to a subquery (Facet needs 1 projector) */
		/* TODO: Can we get rid of this Subquery? */
		query = MigrateQueryToSubQuery(query, context);
	}

	int numStages = ValidateFacet(existingValue);

	/* First step, move the current query into a CTE */
	CommonTableExpr *baseCte = makeNode(CommonTableExpr);
	baseCte->ctename = "facet_base";
	baseCte->ctequery = (Node *) query;

	/* Second step: Build UNION ALL query */
	Query *finalQuery = BuildFacetUnionAllQuery(numStages, existingValue, baseCte,
												query->querySource,
												context->databaseNameDatum,
												&context->sortSpec);

	/* Finally, add bson_object_agg */
	finalQuery = AddBsonObjectAggFunction(finalQuery, context);

	finalQuery->cteList = list_make1(baseCte);
	return finalQuery;
}


/*
 * Top level method handling processing of the lookup stage.
 */
Query *
HandleLookup(const bson_value_t *existingValue, Query *query,
			 AggregationPipelineBuildContext *context)
{
	ReportFeatureUsage(FEATURE_STAGE_LOOKUP);
	LookupArgs lookupArgs;
	memset(&lookupArgs, 0, sizeof(LookupArgs));

	if (context->nestedPipelineLevel > MaximumLookupPipelineDepth)
	{
		ereport(ERROR, (errcode(MongoMaxSubPipelineDepthExceeded),
						errmsg(
							"Maximum number of nested sub-pipelines exceeded. Limit is %d",
							MaximumLookupPipelineDepth)));
	}

	ParseLookupStage(existingValue, &lookupArgs);

	/* Now build the base query for the lookup */
	return ProcessLookupCore(query, context, &lookupArgs);
}


/*
 * Top level method handling processing of the $documents stage.
 */
Query *
HandleDocumentsStage(const bson_value_t *existingValue, Query *query,
					 AggregationPipelineBuildContext *context)
{
	ReportFeatureUsage(FEATURE_STAGE_DOCUMENTS);

	if (list_length(query->rtable) != 0 || context->stageNum != 0)
	{
		ereport(ERROR, (errcode(MongoBadValue),
						errmsg(
							"$documents is only valid as the first stage in a pipeline")));
	}

	/* Documents is an expression */
	pgbsonelement documentsElement =
	{
		.path = "$documents",
		.pathLength = 10,
		.bsonValue = *existingValue
	};
	pgbson_writer writer;
	PgbsonWriterInit(&writer);

	ExpressionVariableContext *variableContext = NULL;
	bool isNullOnEmpty = false;
	EvaluateExpressionToWriter(PgbsonInitEmpty(), &documentsElement, &writer,
							   variableContext, isNullOnEmpty);

	/* Validate documents */
	pgbson *targetBson = PgbsonWriterGetPgbson(&writer);
	pgbsonelement fetchedElement = { 0 };
	if (!TryGetSinglePgbsonElementFromPgbson(targetBson, &fetchedElement) ||
		fetchedElement.bsonValue.value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(MongoLocation5858203),
						errmsg(
							"error during aggregation :: caused by :: an array is expected")));
	}

	/* Create a distinct unwind - to expand arrays and such */
	Const *unwindValue = MakeTextConst(documentsElement.path,
									   documentsElement.pathLength);
	List *args = list_make2(MakeBsonConst(targetBson), unwindValue);
	FuncExpr *resultExpr = makeFuncExpr(
		BsonLookupUnwindFunctionOid(), BsonTypeId(), args, InvalidOid,
		InvalidOid, COERCE_EXPLICIT_CALL);
	resultExpr->funcretset = true;

	RangeTblFunction *tblFunction = makeNode(RangeTblFunction);
	tblFunction->funcexpr = (Node *) resultExpr;
	tblFunction->funccolcount = 2;
	tblFunction->funccoltypes = list_make2_oid(BsonTypeId(), TEXTOID);
	tblFunction->funccolcollations = list_make2_oid(InvalidOid, InvalidOid);
	tblFunction->funccoltypmods = list_make2_int(-1, -1);

	RangeTblEntry *rte = makeNode(RangeTblEntry);
	rte->rtekind = RTE_FUNCTION;
	rte->self_reference = false;
	rte->lateral = false;
	rte->inh = false;
	rte->inFromCl = true;
	rte->functions = list_make1(tblFunction);

	List *colnames = list_make1(makeString("documents"));
	rte->alias = makeAlias("documents", NIL);
	rte->eref = makeAlias("documents", colnames);

	Var *queryOutput = makeVar(1, 1, BsonTypeId(),
							   -1, InvalidOid, 0);
	bool resJunk = false;
	TargetEntry *upperEntry = makeTargetEntry((Expr *) queryOutput, 1,
											  "documents_aggregate",
											  resJunk);
	query->targetList = list_make1(upperEntry);
	query->rtable = list_make1(rte);

	RangeTblRef *rtr = makeNode(RangeTblRef);
	rtr->rtindex = 1;
	query->jointree = makeFromExpr(list_make1(rtr), NULL);

	context->requiresSubQuery = true;
	return query;
}


/*
 * Top level method handling processing of the $unionWith stage.
 */
Query *
HandleUnionWith(const bson_value_t *existingValue, Query *query,
				AggregationPipelineBuildContext *context)
{
	ReportFeatureUsage(FEATURE_STAGE_UNIONWITH);

	Query *leftQuery = query;
	Query *rightQuery = NULL;
	if (existingValue->value_type == BSON_TYPE_UTF8)
	{
		AggregationPipelineBuildContext subPipelineContext = { 0 };
		subPipelineContext.nestedPipelineLevel = context->nestedPipelineLevel + 1;
		subPipelineContext.databaseNameDatum = context->databaseNameDatum;

		/* This is unionWith on the base collection */
		StringView collectionFrom =
		{
			.length = existingValue->value.v_utf8.len,
			.string = existingValue->value.v_utf8.str
		};
		rightQuery = GenerateBaseTableQuery(context->databaseNameDatum,
											&collectionFrom, &subPipelineContext);
	}
	else if (existingValue->value_type == BSON_TYPE_DOCUMENT)
	{
		bson_iter_t pipelineIterator;
		BsonValueInitIterator(existingValue, &pipelineIterator);

		StringView collectionName = { 0 };
		bson_value_t pipelineValue = { 0 };
		while (bson_iter_next(&pipelineIterator))
		{
			const char *key = bson_iter_key(&pipelineIterator);
			const bson_value_t *value = bson_iter_value(&pipelineIterator);
			if (strcmp(key, "coll") == 0)
			{
				EnsureTopLevelFieldType("$unionWith.coll", &pipelineIterator,
										BSON_TYPE_UTF8);
				collectionName.length = value->value.v_utf8.len;
				collectionName.string = value->value.v_utf8.str;
			}
			else if (strcmp(key, "pipeline") == 0)
			{
				EnsureTopLevelFieldType("$unionWith.pipeline", &pipelineIterator,
										BSON_TYPE_ARRAY);
				pipelineValue = *value;
			}
			else
			{
				ereport(ERROR, (errcode(MongoUnknownBsonField),
								errmsg("BSON field '$unionWith.%s' is an unknown field.",
									   key),
								errhint("BSON field '$unionWith.%s' is an unknown field.",
										key)));
			}
		}

		if (collectionName.length == 0)
		{
			/* TODO: Technically this can be supported for agnostic queries but for now bail */
			ereport(ERROR, (errcode(MongoCommandNotSupported),
							errmsg(
								"$unionWith without explicit collection not supported yet"),
							errhint(
								"$unionWith without explicit collection not supported yet")));
		}

		AggregationPipelineBuildContext subPipelineContext = { 0 };
		subPipelineContext.nestedPipelineLevel = context->nestedPipelineLevel + 1;
		subPipelineContext.databaseNameDatum = context->databaseNameDatum;
		rightQuery = GenerateBaseTableQuery(context->databaseNameDatum,
											&collectionName, &subPipelineContext);

		if (pipelineValue.value_type != BSON_TYPE_EOD)
		{
			ValidateUnionWithPipeline(&pipelineValue);
			rightQuery = MutateQueryWithPipeline(rightQuery, &pipelineValue,
												 &subPipelineContext);
		}
	}
	else
	{
		ereport(ERROR, (errcode(MongoFailedToParse),
						errmsg(
							"the $unionWith stage specification must be an object or string, but found %s",
							BsonTypeName(existingValue->value_type)),
						errhint(
							"the $unionWith stage specification must be an object or string, but found %s",
							BsonTypeName(existingValue->value_type))));
	}

	bool includeAllColumns = false;
	RangeTblEntry *leftRte = MakeSubQueryRte(leftQuery, context->stageNum, 0,
											 "unionLeft",
											 includeAllColumns);
	RangeTblEntry *rightRte = MakeSubQueryRte(rightQuery, context->stageNum, 0,
											  "unionRight",
											  includeAllColumns);

	Query *modifiedQuery = makeNode(Query);
	modifiedQuery->commandType = CMD_SELECT;
	modifiedQuery->querySource = query->querySource;
	modifiedQuery->canSetTag = true;
	modifiedQuery->jointree = makeNode(FromExpr);

	modifiedQuery->rtable = list_make2(leftRte, rightRte);
	RangeTblRef *leftReference = makeNode(RangeTblRef);
	leftReference->rtindex = 1;
	RangeTblRef *rightReference = makeNode(RangeTblRef);
	rightReference->rtindex = 2;

	SetOperationStmt *setOpStatement = MakeBsonSetOpStatement();
	setOpStatement->larg = (Node *) leftReference;
	setOpStatement->rarg = (Node *) rightReference;

	/* Update the query with the SetOp statement */
	modifiedQuery->setOperations = (Node *) setOpStatement;

	/* Result column node */
	TargetEntry *leftMostTargetEntry = linitial(leftQuery->targetList);
	Var *var = makeVar(1, leftMostTargetEntry->resno,
					   BsonTypeId(), -1, InvalidOid, 0);
	TargetEntry *restle = makeTargetEntry((Expr *) var,
										  leftMostTargetEntry->resno,
										  leftMostTargetEntry->resname,
										  false);
	modifiedQuery->targetList = list_make1(restle);
	modifiedQuery = MigrateQueryToSubQuery(modifiedQuery, context);

	return modifiedQuery;
}


/*
 * Validates the facet pipeline definition.
 */
static int
ValidateFacet(const bson_value_t *facetValue)
{
	if (facetValue->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(MongoLocation15947),
						errmsg("a facet's fields must be specified in an object")));
	}

	bson_iter_t facetIter;
	BsonValueInitIterator(facetValue, &facetIter);

	int numStages = 0;
	while (bson_iter_next(&facetIter))
	{
		const char *key = bson_iter_key(&facetIter);
		const bson_value_t *pipeline = bson_iter_value(&facetIter);
		EnsureTopLevelFieldValueType("$facet.pipeline", pipeline, BSON_TYPE_ARRAY);

		numStages++;

		bson_iter_t pipelineArray;
		BsonValueInitIterator(pipeline, &pipelineArray);

		/* These stages are not allowed when executing $facet */
		while (bson_iter_next(&pipelineArray))
		{
			pgbsonelement stageElement = GetPipelineStage(&pipelineArray, "facet", key);
			const char *nestedPipelineStage = stageElement.path;
			if (strcmp(nestedPipelineStage, "$collStats") == 0 ||
				strcmp(nestedPipelineStage, "$facet") == 0 ||
				strcmp(nestedPipelineStage, "$geoNear") == 0 ||
				strcmp(nestedPipelineStage, "$indexStats") == 0 ||
				strcmp(nestedPipelineStage, "$out") == 0 ||
				strcmp(nestedPipelineStage, "$merge") == 0 ||
				strcmp(nestedPipelineStage, "$planCacheStats") == 0 ||
				strcmp(nestedPipelineStage, "$search") == 0)
			{
				ereport(ERROR, (errcode(MongoLocation40600),
								errmsg(
									"%s is not allowed to be used within a $facet stage",
									nestedPipelineStage),
								errhint(
									"%s is not allowed to be used within a $facet stage",
									nestedPipelineStage)));
			}
		}
	}

	if (numStages == 0)
	{
		ereport(ERROR, (errcode(MongoLocation40169),
						errmsg("the $facet specification must be a non-empty object")));
	}

	return numStages;
}


/*
 * Given a specific facet value that corresponds to an object with
 * one or more fields containing pipelines, creates a query that has
 * the UNION ALL SELECT operators of all those pipelines.
 */
static Query *
BuildFacetUnionAllQuery(int numStages, const bson_value_t *facetValue,
						CommonTableExpr *baseCte, QuerySource querySource,
						Datum databaseNameDatum, const bson_value_t *sortSpec)
{
	Query *modifiedQuery = NULL;

	ParseState *parseState = make_parsestate(NULL);
	parseState->p_expr_kind = EXPR_KIND_SELECT_TARGET;
	parseState->p_next_resno = 1;

	/* Special case, if there's only 1 facet stage, we can just treat it as a SELECT from the CTE
	 * with a Subquery and skip all the UNION ALL processing.
	 */
	if (numStages == 1)
	{
		pgbsonelement singleElement = { 0 };
		BsonValueToPgbsonElement(facetValue, &singleElement);

		/* Creates a CTE that selects from the base CTE */

		/* Levels up is unused as we reset it after we build the aggregation and the final levelsup
		 * is determined. */
		int levelsUpUnused = 0;
		Query *baseQuery = CreateCteSelectQuery(baseCte, "facetsub", numStages,
												levelsUpUnused);

		/* Mutate the Query to add the aggregation pipeline */
		AggregationPipelineBuildContext nestedContext = { 0 };
		nestedContext.nestedPipelineLevel = 1;
		nestedContext.databaseNameDatum = databaseNameDatum;
		nestedContext.sortSpec = *sortSpec;

		modifiedQuery = MutateQueryWithPipeline(baseQuery, &singleElement.bsonValue,
												&nestedContext);

		/* Add bson_array_agg to the output */
		bool migrateToSubQuery = true;
		modifiedQuery = AddBsonArrayAggFunction(modifiedQuery, &nestedContext, parseState,
												singleElement.path,
												singleElement.pathLength,
												migrateToSubQuery);

		/* Track that the CTE is actually N levels up from the inner-most query
		 * Since we attach the CTE to the query with the obj_agg, we add one more level
		 * to the CTE levels up.
		 */
		RangeTblEntry *origTableEntry = linitial(baseQuery->rtable);
		origTableEntry->ctelevelsup = nestedContext.numNestedLevels + 1;
	}
	else
	{
		/* We need yet another layer thats going to hold the Set UNION ALL queries */
		modifiedQuery = makeNode(Query);
		modifiedQuery->commandType = CMD_SELECT;
		modifiedQuery->querySource = querySource;
		modifiedQuery->canSetTag = true;
		modifiedQuery->jointree = makeNode(FromExpr);
		modifiedQuery->hasAggs = true;

		List *rangeTableReferences = NIL;
		bson_iter_t facetIterator;
		BsonValueInitIterator(facetValue, &facetIterator);
		int nestedStage = 1;

		Query *firstInnerQuery = NULL;
		Index firstRangeTableIndex = 0;
		while (bson_iter_next(&facetIterator))
		{
			StringView pathView = bson_iter_key_string_view(&facetIterator);

			/* Same drill - create a CTE Scan */

			/* Note that levelsUp is overridden after the query is built.
			 * In this path because we create a wrapper for the SetOperation, the
			 * prior stage query is at least 1 level up from each nested pipeline query.
			 * Since we attach the CTE to the query with the obj_agg, we add one more level
			 * to the CTE levels up.
			 */
			int baseLevelsUp = 2;
			Query *baseQuery = CreateCteSelectQuery(baseCte, "facetsub", nestedStage,
													baseLevelsUp);

			/* Modify the pipeline */
			AggregationPipelineBuildContext nestedContext = { 0 };
			nestedContext.nestedPipelineLevel = 1;
			nestedContext.databaseNameDatum = databaseNameDatum;
			nestedContext.sortSpec = *sortSpec;

			Query *innerQuery = MutateQueryWithPipeline(baseQuery, bson_iter_value(
															&facetIterator),
														&nestedContext);

			/* Add the BSON_ARRAY_AGG function */
			bool migrateToSubQuery = true;
			innerQuery = AddBsonArrayAggFunction(innerQuery, &nestedContext, parseState,
												 pathView.string, pathView.length,
												 migrateToSubQuery);

			/* Notify the base table that it got pushed down. */
			RangeTblEntry *origTableEntry = linitial(baseQuery->rtable);
			origTableEntry->ctelevelsup = nestedContext.numNestedLevels + baseLevelsUp;

			/* Build the RTE for the output and add it to the SetOperation wrapper */
			bool includeAllColumns = false;
			RangeTblEntry *entry = MakeSubQueryRte(innerQuery, nestedStage, 0,
												   "facetinput",
												   includeAllColumns);
			modifiedQuery->rtable = lappend(modifiedQuery->rtable, entry);

			/* Track the RangeTableRef to this RTE (Will be used later) */
			RangeTblRef *innerRangeTableRef = makeNode(RangeTblRef);
			innerRangeTableRef->rtindex = list_length(modifiedQuery->rtable);
			if (firstInnerQuery == NULL)
			{
				firstInnerQuery = innerQuery;
				firstRangeTableIndex = innerRangeTableRef->rtindex;
			}

			rangeTableReferences = lappend(rangeTableReferences, innerRangeTableRef);
			nestedStage++;
		}


		/* now build the SetOperationStmt tree: This is the UNION ALL */
		int i = 0;
		SetOperationStmt *setOpStatement = MakeBsonSetOpStatement();
		bool insertLeft = false;
		for (i = 0; i < list_length(rangeTableReferences); i++)
		{
			if (setOpStatement->larg == NULL)
			{
				setOpStatement->larg = (Node *) list_nth(rangeTableReferences, i);
			}
			else if (setOpStatement->rarg == NULL)
			{
				setOpStatement->rarg = (Node *) list_nth(rangeTableReferences, i);
			}
			else if (insertLeft)
			{
				/* Both left and right are full and we want to add a node
				 * Flip flop between inserting on the left and right
				 */
				SetOperationStmt *newStatement = MakeBsonSetOpStatement();
				newStatement->larg = setOpStatement->larg;
				newStatement->rarg = (Node *) list_nth(rangeTableReferences, i);
				setOpStatement->larg = (Node *) newStatement;
				insertLeft = false;
			}
			else
			{
				SetOperationStmt *newStatement = MakeBsonSetOpStatement();
				newStatement->larg = setOpStatement->rarg;
				newStatement->rarg = (Node *) list_nth(rangeTableReferences, i);
				setOpStatement->rarg = (Node *) newStatement;
				insertLeft = true;
			}
		}

		/* Update the query with the SetOp statement */
		modifiedQuery->setOperations = (Node *) setOpStatement;

		/* Result column node */
		TargetEntry *leftMostTargetEntry = linitial(firstInnerQuery->targetList);
		Var *var = makeVar(firstRangeTableIndex,
						   leftMostTargetEntry->resno,
						   BsonTypeId(), -1, InvalidOid, 0);
		TargetEntry *restle = makeTargetEntry((Expr *) var,
											  leftMostTargetEntry->resno,
											  leftMostTargetEntry->resname,
											  false);
		modifiedQuery->targetList = list_make1(restle);
	}

	pfree(parseState);
	return modifiedQuery;
}


/*
 * Creates the COALESCE(Expr, '{ "field": [] }'::bson)
 * Expression for an arbitrary expression.
 */
static Expr *
GetArrayAggCoalesce(Expr *innerExpr, const char *fieldPath, uint32_t fieldPathLength)
{
	pgbson_writer defaultValueWriter;
	PgbsonWriterInit(&defaultValueWriter);
	PgbsonWriterAppendEmptyArray(&defaultValueWriter, fieldPath, fieldPathLength);

	pgbson *bson = PgbsonWriterGetPgbson(&defaultValueWriter);

	/* Add COALESCE operator */
	CoalesceExpr *coalesce = makeNode(CoalesceExpr);
	coalesce->coalescetype = BsonTypeId();
	coalesce->coalescecollid = InvalidOid;
	coalesce->args = list_make2(innerExpr, MakeBsonConst(bson));

	return (Expr *) coalesce;
}


/*
 * Adds the BSON_ARRAY_AGG function to a given query. Also migrates the existing query
 * to a subquery if required.
 */
static Query *
AddBsonArrayAggFunction(Query *baseQuery, AggregationPipelineBuildContext *context,
						ParseState *parseState, const char *fieldPath,
						uint32_t fieldPathLength, bool migrateToSubQuery)
{
	/* Now add the bson_array_agg function */
	Query *modifiedQuery = migrateToSubQuery ? MigrateQueryToSubQuery(baseQuery,
																	  context) :
						   baseQuery;

	/* The first projector is the document */
	TargetEntry *firstEntry = linitial(modifiedQuery->targetList);

	List *aggregateArgs = list_make2(
		firstEntry->expr,
		MakeTextConst(fieldPath, fieldPathLength));
	List *argTypesList = list_make2_oid(BsonTypeId(), TEXTOID);
	Aggref *aggref = CreateMultiArgAggregate(BsonArrayAggregateFunctionOid(),
											 aggregateArgs,
											 argTypesList, parseState);

	firstEntry->expr = GetArrayAggCoalesce((Expr *) aggref, fieldPath, fieldPathLength);
	modifiedQuery->hasAggs = true;
	return modifiedQuery;
}


/*
 * Adds the BSON_OBJECT_AGG function to a given query.
 */
static Query *
AddBsonObjectAggFunction(Query *baseQuery, AggregationPipelineBuildContext *context)
{
	/* We replace the targetEntry so we're fine to take the resno 1 slot */
	ParseState *parseState = make_parsestate(NULL);
	parseState->p_expr_kind = EXPR_KIND_SELECT_TARGET;
	parseState->p_next_resno = 1;

	/* Better safe than sorry (Move it to a subquery) */
	Query *modifiedQuery = MigrateQueryToSubQuery(baseQuery, context);

	/* The first projector is the document */
	TargetEntry *firstEntry = linitial(modifiedQuery->targetList);

	/* Now add the bson_object_agg function */
	List *aggregateArgs = list_make1(firstEntry->expr);
	List *argTypesList = list_make1_oid(BsonTypeId());
	Aggref *aggref = CreateMultiArgAggregate(BsonObjectAggregateFunctionOid(),
											 aggregateArgs,
											 argTypesList, parseState);
	firstEntry->expr = (Expr *) aggref;
	modifiedQuery->hasAggs = true;

	pfree(parseState);
	return modifiedQuery;
}


static RangeTblEntry *
CreateCteRte(CommonTableExpr *baseCte, const char *prefix, int stageNum, int levelsUp)
{
	StringInfo s = makeStringInfo();
	appendStringInfo(s, "%s_stage_%d", prefix, stageNum);

	RangeTblEntry *rte = makeNode(RangeTblEntry);
	rte->rtekind = RTE_CTE;
	rte->ctename = baseCte->ctename;
	rte->self_reference = false;
	rte->lateral = false;
	rte->inh = false;
	rte->inFromCl = true;
	rte->ctelevelsup = levelsUp;
	baseCte->cterefcount++;

	List *colnames = NIL;
	ListCell *cell;
	Query *baseQuery = (Query *) baseCte->ctequery;
	foreach(cell, baseQuery->targetList)
	{
		TargetEntry *tle = (TargetEntry *) lfirst(cell);
		colnames = lappend(colnames, makeString(tle->resname ? tle->resname : ""));
	}

	rte->alias = makeAlias(s->data, NIL);
	rte->eref = makeAlias(s->data, colnames);

	return rte;
}


/*
 * Creates a base query that selects from a given FuncExpr.
 */
static Query *
CreateFunctionSelectorQuery(FuncExpr *funcExpr, const char *prefix,
							int subStageNum, QuerySource querySource)
{
	StringInfo s = makeStringInfo();
	appendStringInfo(s, "%s_substage_%d", prefix, subStageNum);

	RangeTblFunction *tblFunction = makeNode(RangeTblFunction);
	tblFunction->funcexpr = (Node *) funcExpr;
	tblFunction->funccolcount = 2;
	tblFunction->funccoltypes = list_make2_oid(BsonTypeId(), TEXTOID);
	tblFunction->funccolcollations = list_make2_oid(InvalidOid, InvalidOid);
	tblFunction->funccoltypmods = list_make2_int(-1, -1);

	RangeTblEntry *rte = makeNode(RangeTblEntry);
	rte->rtekind = RTE_FUNCTION;
	rte->self_reference = false;
	rte->lateral = false;
	rte->inh = false;
	rte->inFromCl = true;
	rte->functions = list_make1(tblFunction);

	List *colnames = list_make1(makeString("lookup_unwind"));
	rte->alias = makeAlias(s->data, NIL);
	rte->eref = makeAlias(s->data, colnames);

	Var *queryOutput = makeVar(1, 1, funcExpr->funcresulttype,
							   -1, InvalidOid, 0);
	bool resJunk = false;
	TargetEntry *upperEntry = makeTargetEntry((Expr *) queryOutput, 1,
											  "funcName",
											  resJunk);

	Query *newquery = makeNode(Query);
	newquery->commandType = CMD_SELECT;
	newquery->querySource = querySource;
	newquery->canSetTag = true;
	newquery->targetList = list_make1(upperEntry);
	newquery->rtable = list_make1(rte);

	RangeTblRef *rtr = makeNode(RangeTblRef);
	rtr->rtindex = 1;
	newquery->jointree = makeFromExpr(list_make1(rtr), NULL);
	return newquery;
}


/*
 * Creates a base query that selects from a given CTE.
 */
static Query *
CreateCteSelectQuery(CommonTableExpr *baseCte, const char *prefix, int stageNum,
					 int levelsUp)
{
	Assert(baseCte->ctequery != NULL);
	Query *baseQuery = (Query *) baseCte->ctequery;

	RangeTblEntry *rte = CreateCteRte(baseCte, prefix, stageNum, levelsUp);

	List *upperTargetList = NIL;
	Index rtIndex = 1;
	ListCell *cell;
	foreach(cell, baseQuery->targetList)
	{
		TargetEntry *tle = (TargetEntry *) lfirst(cell);
		Var *newQueryOutput = makeVar(rtIndex, tle->resno, BsonTypeId(), -1,
									  InvalidOid, 0);
		bool resJunk = false;
		TargetEntry *upperEntry = makeTargetEntry((Expr *) newQueryOutput, 1,
												  tle->resname,
												  resJunk);
		upperTargetList = lappend(upperTargetList, upperEntry);
	}

	Query *newquery = makeNode(Query);
	newquery->commandType = CMD_SELECT;
	newquery->querySource = baseQuery->querySource;
	newquery->canSetTag = true;
	newquery->targetList = upperTargetList;
	newquery->rtable = list_make1(rte);

	RangeTblRef *rtr = makeNode(RangeTblRef);
	rtr->rtindex = 1;
	newquery->jointree = makeFromExpr(list_make1(rtr), NULL);

	return newquery;
}


/*
 * Parses the lookup aggregation value and extracts the from collection and pipeline.
 */
void
LookupExtractCollectionAndPipeline(const bson_value_t *lookupValue,
								   StringView *collection, bson_value_t *pipeline)
{
	LookupArgs args;
	memset(&args, 0, sizeof(LookupArgs));
	ParseLookupStage(lookupValue, &args);
	*collection = args.from;
	*pipeline = args.pipeline;
}


/*
 * Parses & validates the input lookup spec.
 * Parsed outputs are placed in the LookupArgs struct.
 */
static void
ParseLookupStage(const bson_value_t *existingValue, LookupArgs *args)
{
	if (existingValue->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(MongoLocation40319),
						errmsg(
							"the $lookup stage specification must be an object, but found %s",
							BsonTypeName(existingValue->value_type))));
	}

	bson_iter_t lookupIter;
	BsonValueInitIterator(existingValue, &lookupIter);

	while (bson_iter_next(&lookupIter))
	{
		const char *key = bson_iter_key(&lookupIter);
		const bson_value_t *value = bson_iter_value(&lookupIter);
		if (strcmp(key, "as") == 0)
		{
			if (value->value_type != BSON_TYPE_UTF8)
			{
				ereport(ERROR, (errcode(MongoFailedToParse),
								errmsg(
									"lookup argument 'as' must be a string, is type %s",
									BsonTypeName(value->value_type))));
			}

			args->lookupAs = (StringView) {
				.length = value->value.v_utf8.len,
				.string = value->value.v_utf8.str
			};
		}
		else if (strcmp(key, "foreignField") == 0)
		{
			if (value->value_type != BSON_TYPE_UTF8)
			{
				ereport(ERROR, (errcode(MongoFailedToParse),
								errmsg(
									"lookup argument 'foreignField' must be a string, is type %s",
									BsonTypeName(value->value_type))));
			}

			args->foreignField = (StringView) {
				.length = value->value.v_utf8.len,
				.string = value->value.v_utf8.str
			};
		}
		else if (strcmp(key, "from") == 0)
		{
			if (value->value_type != BSON_TYPE_UTF8)
			{
				ereport(ERROR, (errcode(MongoLocation40321),
								errmsg(
									"lookup argument 'from' must be a string, is type %s",
									BsonTypeName(value->value_type))));
			}

			args->from = (StringView) {
				.length = value->value.v_utf8.len,
				.string = value->value.v_utf8.str
			};
		}
		else if (strcmp(key, "let") == 0)
		{
			ereport(ERROR, (errcode(MongoCommandNotSupported),
							errmsg("let not supported")));
		}
		else if (strcmp(key, "localField") == 0)
		{
			if (value->value_type != BSON_TYPE_UTF8)
			{
				ereport(ERROR, (errcode(MongoFailedToParse),
								errmsg(
									"lookup argument 'localField' must be a string, is type %s",
									BsonTypeName(value->value_type))));
			}

			args->localField = (StringView) {
				.length = value->value.v_utf8.len,
				.string = value->value.v_utf8.str
			};
		}
		else if (strcmp(key, "pipeline") == 0)
		{
			EnsureTopLevelFieldValueType("$lookup.pipeline", value, BSON_TYPE_ARRAY);
			args->pipeline = *value;

			bson_iter_t pipelineArray;
			BsonValueInitIterator(value, &pipelineArray);

			/* These stages are not allowed when executing $lookup */
			while (bson_iter_next(&pipelineArray))
			{
				pgbsonelement stageElement = GetPipelineStage(&pipelineArray, "lookup",
															  "pipeline");
				const char *nestedPipelineStage = stageElement.path;
				if (strcmp(nestedPipelineStage, "$out") == 0 ||
					strcmp(nestedPipelineStage, "$merge") == 0)
				{
					ereport(ERROR, (errcode(MongoLocation51047),
									errmsg(
										"%s is not allowed to be used within a $lookup stage",
										nestedPipelineStage)));
				}
			}
		}
		else
		{
			ereport(ERROR, (errcode(MongoFailedToParse),
							errmsg("unknown argument to $lookup: %s", key)));
		}
	}

	if (args->lookupAs.length == 0)
	{
		ereport(ERROR, (errcode(MongoFailedToParse),
						errmsg("must specify 'as' field for a $lookup")));
	}

	bool isPipelineLookup = args->pipeline.value_type != BSON_TYPE_EOD;
	if (args->from.length == 0 && !isPipelineLookup)
	{
		ereport(ERROR, (errcode(MongoFailedToParse),
						errmsg("must specify 'from' field for a $lookup")));
	}

	if (!isPipelineLookup &&
		(args->foreignField.length == 0 || args->localField.length == 0))
	{
		ereport(ERROR, (errcode(MongoFailedToParse),
						errmsg(
							"$lookup requires either 'pipeline' or both 'localField' and 'foreignField' to be specified")));
	}

	if ((args->foreignField.length == 0) ^ (args->localField.length == 0))
	{
		ereport(ERROR, (errcode(MongoFailedToParse),
						errmsg(
							"$lookup requires both or neither of 'localField' and 'foreignField' to be specified")));
	}

	if (args->foreignField.length != 0)
	{
		args->hasLookupMatch = true;
	}
}


/*
 * Helper method to create Lookup's JOIN RTE - this is the entry in the RTE
 * That goes in the Lookup's FROM clause and ties the two tables together.
 */
inline static RangeTblEntry *
MakeLookupJoinRte(List *joinVars, List *colNames, List *joinLeftCols, List *joinRightCols)
{
	/* Add an RTE for the JoinExpr */
	RangeTblEntry *joinRte = makeNode(RangeTblEntry);

	joinRte->rtekind = RTE_JOIN;
	joinRte->relid = InvalidOid;
	joinRte->subquery = NULL;
	joinRte->jointype = JOIN_LEFT;
	joinRte->joinmergedcols = 0; /* No using clause */
	joinRte->joinaliasvars = joinVars;
	joinRte->joinleftcols = joinLeftCols;
	joinRte->joinrightcols = joinRightCols;
	joinRte->join_using_alias = NULL;
	joinRte->alias = makeAlias("lookup_join", colNames);
	joinRte->eref = joinRte->alias;
	joinRte->inh = false;           /* never true for joins */
	joinRte->inFromCl = true;

	joinRte->requiredPerms = 0;
	joinRte->checkAsUser = InvalidOid;
	joinRte->selectedCols = NULL;
	joinRte->insertedCols = NULL;
	joinRte->updatedCols = NULL;
	joinRte->extraUpdatedCols = NULL;
	return joinRte;
}


/*
 * Given a query in a specified RTE index that returns only BSON values, updates 3 lists based on the query:
 * 1) OutputVars are going to be Var nodes that point to the (RTE, Output) position
 * 2) The names of the output columns across the query
 * 3) The integer result numbers of the Vars.
 * Used to build the JoinRTE for lookup
 */
inline static void
MakeBsonJoinVarsFromQuery(Index queryIndex, Query *query, List **outputVars,
						  List **outputColNames, List **joinCols)
{
	ListCell *cell;
	foreach(cell, query->targetList)
	{
		TargetEntry *entry = lfirst(cell);

		Var *outputVar = makeVar(queryIndex, entry->resno, BsonTypeId(), -1, InvalidOid,
								 0);
		*outputVars = lappend(*outputVars, outputVar);
		*outputColNames = lappend(*outputColNames, makeString(entry->resname));
		*joinCols = lappend_int(*joinCols, (int) entry->resno);
	}
}


/*
 * Core JOIN building logic for $lookup. See comments within on logic
 * of each step within.
 */
static Query *
ProcessLookupCore(Query *query, AggregationPipelineBuildContext *context,
				  LookupArgs *lookupArgs)
{
	if (list_length(query->targetList) > 1)
	{
		/* if we have multiple projectors, push to a subquery (Lookup needs 1 projector) */
		/* TODO: Can we do away with this */
		query = MigrateQueryToSubQuery(query, context);
	}

	/* Generate the lookup query */
	/* Start with a fresh query */
	Query *lookupQuery = makeNode(Query);
	lookupQuery->commandType = CMD_SELECT;
	lookupQuery->querySource = query->querySource;
	lookupQuery->canSetTag = true;
	lookupQuery->jointree = makeNode(FromExpr);

	/* Mark that we're adding a nesting level */
	context->numNestedLevels++;

	/* The left query is just the base query */
	const Index leftQueryRteIndex = 1;
	const Index rightQueryRteIndex = 2;
	const Index joinQueryRteIndex = 3;
	Query *leftQuery = query;

	AggregationPipelineBuildContext subPipelineContext = { 0 };
	subPipelineContext.nestedPipelineLevel = context->nestedPipelineLevel + 1;
	subPipelineContext.databaseNameDatum = context->databaseNameDatum;

	/* For the right query, generate a base table query for the right collection */
	bool isRightQueryAgnostic = lookupArgs->from.length == 0;
	Query *rightQuery = isRightQueryAgnostic ?
						GenerateBaseAgnosticQuery(context->databaseNameDatum,
												  &subPipelineContext) :
						GenerateBaseTableQuery(context->databaseNameDatum,
											   &lookupArgs->from, &subPipelineContext);

	/* Create a parse_state for this session */
	ParseState *parseState = make_parsestate(NULL);
	parseState->p_expr_kind = EXPR_KIND_SELECT_TARGET;
	parseState->p_next_resno = 1;

	/* Check if the pipeline can be pushed to the inner query (right collection)
	 * If it can, then it's inlined. If not, we apply the pipeline post-join.
	 */
	bool canInlineInnerPipeline = CanInlineInnerLookupPipeline(lookupArgs);

	if (!lookupArgs->hasLookupMatch)
	{
		/* If lookup is purely a pipeline (uncorrelated subquery) then
		 * modify the pipeline. */
		rightQuery = MutateQueryWithPipeline(rightQuery, &lookupArgs->pipeline,
											 &subPipelineContext);
		rightQuery = AddBsonArrayAggFunction(rightQuery, &subPipelineContext, parseState,
											 lookupArgs->lookupAs.string,
											 lookupArgs->lookupAs.length,
											 subPipelineContext.requiresSubQuery);
	}
	else
	{
		/* For sharded lookup queries, we need the base table to be a CTE otherwise citus complains */
		if (isRightQueryAgnostic)
		{
			rightQuery = MutateQueryWithPipeline(rightQuery, &lookupArgs->pipeline,
												 &subPipelineContext);
		}

		CommonTableExpr *rightTableExpr = makeNode(CommonTableExpr);
		rightTableExpr->ctename = "lookup_right_query";
		rightTableExpr->ctequery = (Node *) rightQuery;

		rightQuery = CreateCteSelectQuery(rightTableExpr, "lookup_right_query",
										  context->nestedPipelineLevel, 0);
		RangeTblEntry *rightRteCte = linitial(rightQuery->rtable);

		/* If the nested pipeline can be sent down to the nested right query */
		if (canInlineInnerPipeline && !isRightQueryAgnostic)
		{
			rightQuery = MutateQueryWithPipeline(rightQuery, &lookupArgs->pipeline,
												 &subPipelineContext);
			if (subPipelineContext.requiresSubQuery)
			{
				rightQuery = MigrateQueryToSubQuery(rightQuery, &subPipelineContext);
			}
		}

		/* It's a join on a field, first add a TargetEntry for left for bson_dollar_lookup_extract_filter_expression */
		TargetEntry *currentEntry = linitial(leftQuery->targetList);

		/* The extract query right arg is a simple bson of the form { "remoteField": "localField" } */
		pgbson_writer filterWriter;
		PgbsonWriterInit(&filterWriter);
		PgbsonWriterAppendUtf8(&filterWriter, lookupArgs->foreignField.string,
							   lookupArgs->foreignField.length,
							   lookupArgs->localField.string);
		pgbson *filterBson = PgbsonWriterGetPgbson(&filterWriter);

		/* Create the bson_dollar_lookup_extract_filter_expression(document, 'filter') */
		List *extractFilterArgs = list_make2(currentEntry->expr, MakeBsonConst(
												 filterBson));
		FuncExpr *projectorFunc = makeFuncExpr(
			BsonLookupExtractFilterExpressionFunctionOid(), BsonTypeId(),
			extractFilterArgs,
			InvalidOid, InvalidOid, COERCE_EXPLICIT_CALL);

		/* Mark it as an SRF */
		projectorFunc->funcretset = true;
		AttrNumber newProjectorAttrNum = list_length(leftQuery->targetList) + 1;
		TargetEntry *extractFilterProjector = makeTargetEntry((Expr *) projectorFunc,
															  newProjectorAttrNum,
															  "lookup_filter", false);
		leftQuery->targetList = lappend(leftQuery->targetList, extractFilterProjector);
		leftQuery->hasTargetSRFs = true;

		/* now on the right query, add a filter referencing this projector */
		List *rightQuals = NIL;
		if (rightQuery->jointree->quals != NULL)
		{
			rightQuals = make_ands_implicit((Expr *) rightQuery->jointree->quals);
		}

		/* add the WHERE bson_dollar_in(t2.document, t1.match) */
		TargetEntry *currentRightEntry = linitial(rightQuery->targetList);

		int levelsUp = 1;
		Var *matchVar = makeVar(leftQueryRteIndex, newProjectorAttrNum, BsonTypeId(), -1,
								InvalidOid, levelsUp);

		Var *rightVar = (Var *) currentRightEntry->expr;

		List *inArgs = list_make2(copyObject(rightVar), matchVar);
		FuncExpr *inClause = makeFuncExpr(BsonInMatchFunctionId(), BOOLOID, inArgs,
										  InvalidOid, InvalidOid, COERCE_EXPLICIT_CALL);
		if (rightQuals == NIL)
		{
			rightQuery->jointree->quals = (Node *) inClause;
		}
		else
		{
			rightQuals = lappend(rightQuals, inClause);
			rightQuery->jointree->quals = (Node *) make_ands_explicit(rightQuals);
		}

		/* Add the bson_array_agg function */
		bool requiresSubQuery = false;
		rightQuery = AddBsonArrayAggFunction(rightQuery, &subPipelineContext, parseState,
											 lookupArgs->lookupAs.string,
											 lookupArgs->lookupAs.length,
											 requiresSubQuery);

		rightQuery->cteList = list_make1(rightTableExpr);
		rightRteCte->ctelevelsup += subPipelineContext.numNestedLevels;
	}

	/* Make the input a sub-query RTE (The left part of the join) */

	/* Due to citus query_pushdown_planning.JoinTreeContainsSubqueryWalker
	 * we can't just use SubQueries (however, CTEs work). So move the left
	 * query is pushed to a CTE which seems to work (Similar to what the GW)
	 * does.
	 */
	StringInfo cteStr = makeStringInfo();
	appendStringInfo(cteStr, "lookupLeftCte_%d", context->nestedPipelineLevel);
	CommonTableExpr *cteExpr = makeNode(CommonTableExpr);
	cteExpr->ctename = cteStr->data;
	cteExpr->ctequery = (Node *) leftQuery;
	lookupQuery->cteList = lappend(lookupQuery->cteList, cteExpr);

	int stageNum = 1;
	RangeTblEntry *leftTree = CreateCteRte(cteExpr, "lookup", stageNum, 0);

	/* The "from collection" becomes another RTE */
	bool includeAllColumns = true;
	RangeTblEntry *rightTree = MakeSubQueryRte(rightQuery, 1,
											   context->nestedPipelineLevel,
											   "lookupRight", includeAllColumns);

	/* Mark the Right RTE as a lateral join*/
	rightTree->lateral = true;

	/* Build the JOIN RTE joining the left and right RTEs */
	List *outputVars = NIL;
	List *outputColNames = NIL;
	List *leftJoinCols = NIL;
	List *rightJoinCols = NIL;
	MakeBsonJoinVarsFromQuery(leftQueryRteIndex, leftQuery, &outputVars, &outputColNames,
							  &leftJoinCols);
	MakeBsonJoinVarsFromQuery(rightQueryRteIndex, rightQuery, &outputVars,
							  &outputColNames, &rightJoinCols);
	RangeTblEntry *joinRte = MakeLookupJoinRte(outputVars, outputColNames, leftJoinCols,
											   rightJoinCols);


	lookupQuery->rtable = list_make3(leftTree, rightTree, joinRte);

	/* Now specify the "From" as a join */
	/* The query has a single 'FROM' which is a Join */
	JoinExpr *joinExpr = makeNode(JoinExpr);
	joinExpr->jointype = joinRte->jointype;
	joinExpr->rtindex = joinQueryRteIndex;

	/* Create RangeTblRef's to point to the left & right RTEs */
	RangeTblRef *leftRef = makeNode(RangeTblRef);
	leftRef->rtindex = leftQueryRteIndex;
	RangeTblRef *rightRef = makeNode(RangeTblRef);
	rightRef->rtindex = rightQueryRteIndex;
	joinExpr->larg = (Node *) leftRef;
	joinExpr->rarg = (Node *) rightRef;

	/* Join ON TRUE */
	joinExpr->quals = (Node *) makeConst(BOOLOID, -1, InvalidOid, 1, BoolGetDatum(true),
										 false, true);

	lookupQuery->jointree->fromlist = list_make1(joinExpr);

	/* Now add the lookup projector */
	/* The lookup projector is an addFields on the left doc */
	/* And the rightArg will be an addFieldsSpec already under a bson_array_agg(value, 'asField') */
	Var *leftOutput = makeVar(leftRef->rtindex, (AttrNumber) 1, BsonTypeId(), -1,
							  InvalidOid, 0);
	Var *rightOutput = makeVar(rightRef->rtindex, (AttrNumber) 1, BsonTypeId(), -1,
							   InvalidOid, 0);

	Expr *rightOutputExpr = (Expr *) rightOutput;

	/* If there's a pipeline, run it here as a subquery only if we
	 * couldn't inline it in the top level query.
	 * We do it here since it's easier to deal with the pipeline
	 * post join (fewer queries to think about and manage).
	 */
	if (lookupArgs->pipeline.value_type != BSON_TYPE_EOD &&
		!canInlineInnerPipeline)
	{
		List *unwindArgs = list_make2(rightOutputExpr,
									  MakeTextConst(lookupArgs->lookupAs.string,
													lookupArgs->lookupAs.length));
		FuncExpr *funcExpr = makeFuncExpr(BsonLookupUnwindFunctionOid(), BsonTypeId(),
										  unwindArgs, InvalidOid, InvalidOid,
										  COERCE_EXPLICIT_CALL);
		funcExpr->funcretset = true;
		Query *subSelectQuery = CreateFunctionSelectorQuery(funcExpr,
															"lookup_subpipeline",
															context->
															nestedPipelineLevel,
															query->querySource);

		AggregationPipelineBuildContext projectorQueryContext = { 0 };
		projectorQueryContext.nestedPipelineLevel = context->nestedPipelineLevel + 1;
		projectorQueryContext.databaseNameDatum =
			subPipelineContext.databaseNameDatum;
		projectorQueryContext.mongoCollection = subPipelineContext.mongoCollection;

		subSelectQuery = MutateQueryWithPipeline(subSelectQuery,
												 &lookupArgs->pipeline,
												 &projectorQueryContext);

		/* Readd the aggregate */
		subSelectQuery = AddBsonArrayAggFunction(subSelectQuery,
												 &projectorQueryContext, parseState,
												 lookupArgs->lookupAs.string,
												 lookupArgs->lookupAs.length,
												 projectorQueryContext.
												 requiresSubQuery);

		rightOutput->varlevelsup += projectorQueryContext.numNestedLevels + 1;
		SubLink *subLink = makeNode(SubLink);
		subLink->subLinkType = EXPR_SUBLINK;
		subLink->subLinkId = 0;
		subLink->subselect = (Node *) subSelectQuery;
		rightOutputExpr = (Expr *) subLink;
		lookupQuery->hasSubLinks = true;
	}

	List *addFieldArgs = list_make2(leftOutput, rightOutputExpr);
	FuncExpr *addFields = makeFuncExpr(BsonDollarAddFieldsFunctionOid(), BsonTypeId(),
									   addFieldArgs, InvalidOid, InvalidOid,
									   COERCE_EXPLICIT_CALL);

	TargetEntry *topTargetEntry = makeTargetEntry((Expr *) addFields, 1, "document",
												  false);

	lookupQuery->targetList = list_make1(topTargetEntry);

	pfree(parseState);

	/* TODO: is this needed? */
	context->requiresSubQuery = true;
	return lookupQuery;
}


/*
 * Helper for a nested $lookup stage on whether it can be inlined for a $lookup.
 * This is valid as long as the lookupAs does not intersect with the local field.
 */
bool
CanInlineLookupStageLookup(const bson_value_t *lookupStage,
						   const StringView *lookupPath)
{
	/* A lookup can be inlined if */
	LookupArgs nestedLookupArgs;
	memset(&nestedLookupArgs, 0, sizeof(LookupArgs));
	ParseLookupStage(lookupStage, &nestedLookupArgs);

	if (StringViewStartsWithStringView(&nestedLookupArgs.lookupAs, lookupPath) ||
		StringViewStartsWithStringView(lookupPath, &nestedLookupArgs.lookupAs))
	{
		return false;
	}

	return true;
}


/*
 * Helper for Checking if a lookup pipeline should be inlined.
 * Today this is a binary decision - all of the pipeline is inlined or none of it is.
 * It is possible to do a mixed mode for this (TODO for later).
 */
static bool
CanInlineInnerLookupPipeline(LookupArgs *lookupArgs)
{
	if (lookupArgs->from.length == 0)
	{
		/* Agnostic pipelines can always be inlined */
		return true;
	}

	if (!lookupArgs->hasLookupMatch)
	{
		/* Uncorrelated lookup can always be inlined */
		return true;
	}

	if (lookupArgs->pipeline.value_type == BSON_TYPE_EOD)
	{
		return false;
	}


	/* The lookup can be inlined if the nested pipeline stage does not
	 * modify or write to the local field. This is because the local field
	 * is what's going to be in the $in clause later.
	 */
	StringView localFieldValue = lookupArgs->localField;

	StringView prefix = StringViewFindPrefix(&localFieldValue, '.');
	if (prefix.length != 0)
	{
		localFieldValue = prefix;
	}

	return CanInlineLookupPipeline(&lookupArgs->pipeline, &localFieldValue);
}


/*
 * Validates the pipeline for a $unionwith query.
 */
static void
ValidateUnionWithPipeline(const bson_value_t *pipeline)
{
	bson_iter_t pipelineIter;
	BsonValueInitIterator(pipeline, &pipelineIter);

	while (bson_iter_next(&pipelineIter))
	{
		pgbsonelement stageElement = GetPipelineStage(&pipelineIter, "unionWith",
													  "pipeline");
		if (strcmp(stageElement.path, "$out") == 0)
		{
			ereport(ERROR, (errcode(MongoLocation31441),
							errmsg(
								"$out is not allowed within a $unionWith's sub-pipeline")));
		}
		else if (strcmp(stageElement.path, "$merge") == 0)
		{
			ereport(ERROR, (errcode(MongoLocation31441),
							errmsg(
								"$merge is not allowed within a $unionWith's sub-pipeline")));
		}
	}
}
