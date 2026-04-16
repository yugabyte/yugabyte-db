/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/aggregation/bson_aggregation_vector_search.c
 *
 * Implementation of the backend query generation for pipelines
 * containing vector search stages.
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <float.h>
#include <fmgr.h>
#include <miscadmin.h>

#include <access/reloptions.h>
#include <catalog/pg_operator_d.h>
#include <nodes/nodes.h>
#include <nodes/makefuncs.h>
#include <nodes/nodeFuncs.h>
#include <parser/parser.h>
#include <parser/parse_agg.h>
#include <parser/parse_clause.h>
#include <parser/parse_param.h>
#include <parser/analyze.h>
#include <parser/parse_oper.h>
#include <parser/parsetree.h>
#include <utils/builtins.h>
#include <utils/rel.h>
#include <utils/lsyscache.h>

#include "io/bson_core.h"
#include "metadata/metadata_cache.h"
#include "query/query_operator.h"
#include "commands/parse_error.h"
#include "commands/defrem.h"
#include "opclass/bson_gin_index_mgmt.h"
#include "utils/version_utils.h"

#include "aggregation/bson_aggregation_pipeline.h"
#include "aggregation/bson_aggregation_pipeline_private.h"

#include "utils/feature_counter.h"
#include "utils/hashset_utils.h"
#include "vector/vector_common.h"
#include "vector/vector_spec.h"
#include "vector/vector_utilities.h"
#include "api_hooks.h"

/* --------------------------------------------------------- */
/* Data-types */
/* --------------------------------------------------------- */

typedef enum VectorSearchSpecType
{
	VectorSearchSpecType_Unknown = 0,

	VectorSearchSpecType_CosmosSearch = 1,

	VectorSearchSpecType_KnnBeta = 2,

	VectorSearchSpecType_MongoNative = 3
} VectorSearchSpecType;

/* Context used in replacing the expressions on filtered vector search */
typedef struct
{
	/* The source orderby var to replace */
	Var *sourceExpr;

	/* The target expression to replace it with */
	Expr *targetExpr;
} ReplaceDocumentVarOnSortContext;

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */
static void AddSearchParamFunctionToQuery(Query *query, Oid accessMethodOid,
										  pgbson *searchParamPgbson);

static Expr * AddScoreFieldToDocumentEntry(TargetEntry *documentEntry,
										   Expr *vectorSortExpr,
										   Expr *orderVar);

static TargetEntry * AddCtidToQueryTargetList(Query *query,
											  bool replaceTargetList);

static Query * JoinVectorSearchQueryWithFilterQuery(Query *leftQuery, Query *rightQuery,
													TargetEntry *leftJoinEntry,
													TargetEntry *rightJoinEntry,
													const TargetEntry *sortEntry,
													bool needsReordering);

static void AddPathStringToHashset(List *indexIdList, HTAB *stringHashSet);

static Query * GeneratePrefilteringVectorSearchQuery(Query *searchQuery,
													 AggregationPipelineBuildContext *
													 context,
													 VectorSearchOptions *
													 vectorSearchOptions,
													 TargetEntry *sortEntry,
													 Expr *processedSortExpr,
													 Node *limitCount);

static void AddNullVectorCheckToQuery(Query *query, const Expr *vectorSortExpr);

static TargetEntry * AddSortByToQuery(Query *query, const Expr *vectorSortExpr);

static void ParseAndValidateMongoNativeVectorSearchSpec(const bson_value_t *
														nativeVectorSearchSpec,
														VectorSearchOptions *
														vectorSearchOptions);

static void ParseAndValidateIndexSpecificOptions(
	VectorSearchOptions *vectorSearchOptions);

static Query * HandleVectorSearchCore(Query *query,
									  VectorSearchOptions *vectorSearchOptions,
									  AggregationPipelineBuildContext *context);

static Expr * CheckVectorIndexAndGenerateSortExpr(Query *query,
												  VectorSearchOptions *vectorSearchOptions,
												  AggregationPipelineBuildContext *context);

static void ParseAndValidateKnnBetaQuerySpec(const pgbson *vectorSearchSpecPgbson,
											 VectorSearchOptions *vectorSearchOption);

static void ParseAndValidateCosmosSearchQuerySpec(const pgbson *vectorSearchSpecPgbson,
												  VectorSearchOptions *vectorSearchOptions);

static void ParseAndValidateVectorQuerySpecCore(const pgbson *vectorSearchSpecPgbson,
												VectorSearchOptions *vectorSearchOptions);

static Expr * GenerateScoreExpr(const Expr *orderVar, Oid similarityDistanceOid);

static Query * ReorderQueryResults(Query *joinQuery,
								   AggregationPipelineBuildContext *context);

static Node * ReplaceDocumentVarOnSort(Node *input,
									   ReplaceDocumentVarOnSortContext *context);

static Query * AddFilterToQuery(Query *query,
								AggregationPipelineBuildContext *context,
								VectorSearchOptions *vectorSearchOptions);

/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */
PG_FUNCTION_INFO_V1(command_bson_document_add_score_field);
PG_FUNCTION_INFO_V1(command_bson_search_param);

/*
 * bson_document_add_meta_field adds new fields to the base document.
 * document: { ... , "__cosmos_meta__" : { "score" : { "$numberDouble" : "0.9" } } }
 */
Datum
command_bson_document_add_score_field(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON(0);
	float8 scoreField = PG_GETARG_FLOAT8(1);

	pgbson_writer finalDocWriter;
	PgbsonWriterInit(&finalDocWriter);

	/* Add the document */
	PgbsonWriterConcat(&finalDocWriter, document);

	pgbson_writer nestedWriter;
	PgbsonWriterInit(&nestedWriter);
	PgbsonWriterStartDocument(&finalDocWriter, VECTOR_METADATA_FIELD_NAME,
							  VECTOR_METADATA_FIELD_NAME_STR_LEN, &nestedWriter);

	/* Add the score field */
	PgbsonWriterAppendDouble(&nestedWriter,
							 VECTOR_METADATA_SCORE_FIELD_NAME,
							 VECTOR_METADATA_SCORE_FIELD_NAME_STR_LEN,
							 scoreField);

	PgbsonWriterEndDocument(&finalDocWriter, &nestedWriter);

	PG_RETURN_POINTER(PgbsonWriterGetPgbson(&finalDocWriter));
}


/*
 * Dummy function used to send search parameters to the workers.
 */
Datum
command_bson_search_param(PG_FUNCTION_ARGS)
{
	PG_RETURN_BOOL(true);
}


/*
 * Parses and handles the $search stage in the aggregation pipeline.
 * Converts the query to an
 * ORDER BY bson_extract_vector(document, 'spec') <=> bson_extract_vector(query, 'spec')
 * For additional details see query_operator.c
 */
Query *
HandleSearch(const bson_value_t *existingValue, Query *query,
			 AggregationPipelineBuildContext *context)
{
	RangeTblEntry *rte = linitial(query->rtable);
	if (rte->rtekind != RTE_RELATION || rte->tablesample != NULL ||
		query->limitCount != NULL || context->stageNum != 0)
	{
		/* This is incompatible.vector search needs the base relation. */
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg("$search must be the first stage in the pipeline")));
	}

	ReportFeatureUsage(FEATURE_STAGE_SEARCH);
	EnsureTopLevelFieldValueType("$search", existingValue, BSON_TYPE_DOCUMENT);

	/* The top level $search spec, parsing and validating */
	VectorSearchSpecType searchSpecType = VectorSearchSpecType_Unknown;
	pgbson *vectorSearchSpecPgbson = NULL;

	bson_iter_t searchIterator;
	BsonValueInitIterator(existingValue, &searchIterator);

	while (bson_iter_next(&searchIterator))
	{
		const char *key = bson_iter_key(&searchIterator);
		if (strcmp(key, "cosmosSearch") == 0)
		{
			/* parse search options search */
			EnsureTopLevelFieldType(key, &searchIterator, BSON_TYPE_DOCUMENT);
			vectorSearchSpecPgbson = PgbsonInitFromDocumentBsonValue(bson_iter_value(
																		 &searchIterator));
			searchSpecType = VectorSearchSpecType_CosmosSearch;
		}
		else if (strcmp(key, "knnBeta") == 0)
		{
			/* parse search options search */
			EnsureTopLevelFieldType(key, &searchIterator, BSON_TYPE_DOCUMENT);
			vectorSearchSpecPgbson = PgbsonInitFromDocumentBsonValue(bson_iter_value(
																		 &searchIterator));
			searchSpecType = VectorSearchSpecType_KnnBeta;
		}
		else if (strcmp(key, "index") == 0 ||
				 strcmp(key, "returnStoredSource") == 0)
		{
			/* We ignore these options */
		}
		else
		{
			/* What are these options today? */
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_UNRECOGNIZEDCOMMAND),
							errmsg(
								"Unrecognized $search option: %s, should be one of: cosmosSearch, knnBeta.",
								key)));
		}
	}

	if (searchSpecType == VectorSearchSpecType_Unknown)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
						errmsg(
							"Invalid search spec provided with one or more unsupported options, should be one of: cosmosSearch, knnBeta.")));
	}


	/* The vector search spec, parsing and validating */
	VectorSearchOptions vectorSearchOptions = { 0 };

	vectorSearchOptions.searchSpecPgbson = vectorSearchSpecPgbson;
	vectorSearchOptions.resultCount = -1;
	vectorSearchOptions.queryVectorLength = -1;

	if (searchSpecType == VectorSearchSpecType_CosmosSearch)
	{
		ParseAndValidateCosmosSearchQuerySpec(vectorSearchSpecPgbson,
											  &vectorSearchOptions);
	}
	else if (searchSpecType == VectorSearchSpecType_KnnBeta)
	{
		/* TODO: Track the usage of the knnBeta, if there is no usage, we will remove knnBeta later */
		ReportFeatureUsage(FEATURE_STAGE_VECTOR_SEARCH_KNN);
		ParseAndValidateKnnBetaQuerySpec(vectorSearchSpecPgbson,
										 &vectorSearchOptions);
	}
	else
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
						errmsg(
							"Invalid search spec provided with one or more unsupported options, should be one of: cosmosSearch, knnBeta.")));
	}

	/* Handle the vector search spec */
	return HandleVectorSearchCore(query, &vectorSearchOptions, context);
}


/*
 * Parses and handles the $vectorSearch stage in the aggregation pipeline.
 * Converts the query to an
 * ORDER BY bson_extract_vector(document, 'spec') <=> bson_extract_vector(query, 'spec')
 * For additional details see query_operator.c
 */
Query *
HandleMongoNativeVectorSearch(const bson_value_t *existingValue, Query *query,
							  AggregationPipelineBuildContext *context)
{
	RangeTblEntry *rte = linitial(query->rtable);
	if (rte->rtekind != RTE_RELATION || rte->tablesample != NULL ||
		query->limitCount != NULL || context->stageNum != 0)
	{
		/* This is incompatible.vector search needs the base relation. */
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg("$vectorSearch must be the first stage in the pipeline")));
	}
	ReportFeatureUsage(FEATURE_STAGE_VECTOR_SEARCH_MONGO);

	VectorSearchOptions vectorSearchOptions = { 0 };
	vectorSearchOptions.resultCount = -1;
	vectorSearchOptions.queryVectorLength = -1;

	ParseAndValidateMongoNativeVectorSearchSpec(existingValue,
												&vectorSearchOptions);

	return HandleVectorSearchCore(query, &vectorSearchOptions, context);
}


/* --------------------------------------------------------- */
/* Private methods */
/* --------------------------------------------------------- */


/*
 * Adds a wrapper function(bson_search_param) which includes the search parameters to the query.
 * If the incoming query does not have search parameters,
 * In custom scan, we will dynamically calculate search param depend on the number of vectors in the collection.
 *
 * The example of the search param:
 *      ivfflat: { "nProbes": 4 }
 *      hnsw: { "efSearch": 16 }
 * e.g.
 *      WHERE bson_search_param(document, { "nProbes": 4 }) or WHERE bson_search_param(document, { "efSearch": 16 })
 */
static void
AddSearchParamFunctionToQuery(Query *query, Oid accessMethodOid,
							  pgbson *searchParamPgbson)
{
	if (searchParamPgbson != NULL)
	{
		/* Add the search param function to the query */
		Const *searchParam = MakeBsonConst(searchParamPgbson);
		List *args = list_make2(MakeSimpleDocumentVar(), searchParam);
		FuncExpr *searchQual = makeFuncExpr(ApiBsonSearchParamFunctionId(),
											BOOLOID,
											args, InvalidOid, InvalidOid,
											COERCE_EXPLICIT_CALL);

		if (query->jointree->quals != NULL)
		{
			List *quals = lappend(make_ands_implicit((Expr *) query->jointree->quals),
								  searchQual);
			query->jointree->quals = (Node *) make_ands_explicit(quals);
		}
		else
		{
			query->jointree->quals = (Node *) searchQual;
		}
	}
}


/*
 * Adds ctid to the query target list.
 */
static TargetEntry *
AddCtidToQueryTargetList(Query *query, bool replaceTargetList)
{
	bool resjunk = false;
	Index varno = 1;
	Var *ctidVar = makeVar(varno,
						   SelfItemPointerAttributeNumber,
						   TIDOID,
						   -1,
						   InvalidOid,
						   0);
	const char *ctidname = "ctid";
	TargetEntry *ctid = NULL;

	if (replaceTargetList)
	{
		ctid = makeTargetEntry((Expr *) ctidVar,
							   1,
							   pstrdup(ctidname),
							   resjunk);
		query->targetList = list_make1(ctid);
	}
	else
	{
		ctid = makeTargetEntry((Expr *) ctidVar,
							   list_length(query->targetList) + 1,
							   pstrdup(ctidname),
							   resjunk);
		query->targetList = lappend(query->targetList, ctid);
	}

	return ctid;
}


/*
 * Generates a join query that joins the left and right queries on the leftJoinEntry and rightJoinEntry.
 * Example:
 * JOIN c1 ON c1.ctid = c2.ctid
 *          SELECT
 *              c1.document
 *          FROM c1 JOIN c2 ON c2.ctid = c1.ctid
 *          ORDER BY c1.orderVal
 */
static Query *
JoinVectorSearchQueryWithFilterQuery(Query *leftQuery, Query *rightQuery,
									 TargetEntry *leftJoinEntry,
									 TargetEntry *rightJoinEntry,
									 const TargetEntry *sortEntry,
									 bool needsReordering)
{
	Query *finalQuery = makeNode(Query);
	finalQuery->commandType = CMD_SELECT;
	finalQuery->querySource = leftQuery->querySource;
	finalQuery->canSetTag = true;
	finalQuery->jointree = makeNode(FromExpr);

	const Index leftQueryRteIndex = 1;
	const Index rightQueryRteIndex = 2;
	const Index joinQueryRteIndex = 3;

	RangeTblEntry *leftTree = makeNode(RangeTblEntry);
	leftTree->rtekind = RTE_SUBQUERY;
	leftTree->subquery = leftQuery;
	leftTree->self_reference = false;
	leftTree->lateral = false;
	leftTree->inh = false;
	leftTree->inFromCl = true;

	List *colnames = NIL;
	ListCell *cell;
	foreach(cell, leftQuery->targetList)
	{
		TargetEntry *tle = (TargetEntry *) lfirst(cell);
		colnames = lappend(colnames, makeString(tle->resname));
	}

	leftTree->alias = makeAlias("c1", NIL);
	leftTree->eref = makeAlias("c1", colnames);

	RangeTblEntry *rightTree = makeNode(RangeTblEntry);
	rightTree->rtekind = RTE_SUBQUERY;
	rightTree->subquery = rightQuery;
	rightTree->self_reference = false;
	rightTree->lateral = false;
	rightTree->inh = false;
	rightTree->inFromCl = true;

	colnames = NIL;
	foreach(cell, rightQuery->targetList)
	{
		TargetEntry *tle = (TargetEntry *) lfirst(cell);
		colnames = lappend(colnames, makeString(tle->resname));
	}

	rightTree->alias = makeAlias("c2", NIL);
	rightTree->eref = makeAlias("c2", colnames);

	List *outputVars = NIL;
	List *outputColNames = NIL;
	List *leftJoinCols = NIL;
	foreach(cell, leftQuery->targetList)
	{
		TargetEntry *entry = lfirst(cell);

		Var *outputVar = makeVar(leftQueryRteIndex, entry->resno,
								 ((Var *) entry->expr)->vartype, -1, InvalidOid,
								 0);
		outputVars = lappend(outputVars, outputVar);
		outputColNames = lappend(outputColNames, makeString(entry->resname));
		leftJoinCols = lappend_int(leftJoinCols, entry->resno);
	}

	List *rightJoinCols = NIL;
	foreach(cell, rightQuery->targetList)
	{
		TargetEntry *entry = lfirst(cell);

		Var *outputVar = makeVar(rightQueryRteIndex, entry->resno,
								 ((Var *) entry->expr)->vartype, -1, InvalidOid,
								 0);
		outputVars = lappend(outputVars, outputVar);
		outputColNames = lappend(outputColNames, makeString(entry->resname));
		rightJoinCols = lappend_int(rightJoinCols, entry->resno);
	}

	/* Add an RTE for the JoinExpr */
	RangeTblEntry *joinRte = makeNode(RangeTblEntry);

	joinRte->rtekind = RTE_JOIN;
	joinRte->relid = InvalidOid;
	joinRte->subquery = NULL;
	joinRte->jointype = JOIN_INNER;
	joinRte->joinmergedcols = 0; /* No using clause */
	joinRte->joinaliasvars = outputVars;
	joinRte->joinleftcols = leftJoinCols;
	joinRte->joinrightcols = rightJoinCols;
	joinRte->join_using_alias = NULL;
	joinRte->alias = makeAlias("final_join", NIL);
	joinRte->eref = makeAlias("final_join", outputColNames);
	joinRte->inh = false; /* never true for joins */
	joinRte->inFromCl = true;

#if PG_VERSION_NUM >= 160000
	joinRte->perminfoindex = 0;
#else
	joinRte->requiredPerms = 0;
	joinRte->checkAsUser = InvalidOid;
	joinRte->selectedCols = NULL;
	joinRte->insertedCols = NULL;
	joinRte->updatedCols = NULL;
	joinRte->extraUpdatedCols = NULL;
#endif

	finalQuery->rtable = list_make3(leftTree, rightTree, joinRte);

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

	Var *lCtidVar = makeVar(leftQueryRteIndex,
							leftJoinEntry->resno,
							TIDOID,
							-1,
							InvalidOid,
							0);

	Var *rCtidVar = makeVar(rightQueryRteIndex,
							rightJoinEntry->resno,
							TIDOID,
							-1,
							InvalidOid,
							0);

	joinExpr->quals = (Node *) make_opclause(
		(Oid) TIDEqualOperator, BOOLOID, false,
		(Expr *) rCtidVar, (Expr *) lCtidVar, InvalidOid, InvalidOid);

	finalQuery->jointree->fromlist = list_make1(joinExpr);

	/* Add the document projector for the joined query */
	bool resjunk = false;
	Var *documentVar = makeVar(leftQueryRteIndex,
							   1,
							   BsonTypeId(),
							   DOCUMENT_DATA_TABLE_DOCUMENT_VAR_TYPMOD,
							   DOCUMENT_DATA_TABLE_DOCUMENT_VAR_COLLATION,
							   0);
	TargetEntry *documentEntry = makeTargetEntry((Expr *) documentVar, 1, "document",
												 resjunk);

	finalQuery->targetList = list_make1(documentEntry);

	/* Add the sort clause for the joined query */
	Var *orderVar = makeVar(leftQueryRteIndex,
							2,
							FLOAT8OID,
							-1,
							InvalidOid,
							0);

	ParseState *parseState = make_parsestate(NULL);
	parseState->p_expr_kind = EXPR_KIND_ORDER_BY;

	/* set after what is already taken */
	parseState->p_next_resno = list_length(finalQuery->targetList) + 1;

	SortBy *sortBy = makeNode(SortBy);
	sortBy->location = -1;
	sortBy->sortby_dir = SORTBY_DEFAULT; /* reset later */
	sortBy->node = (Node *) orderVar;

	if (!needsReordering)
	{
		/* Hide the orderVar if we don't need to reorder */
		resjunk = true;
	}

	TargetEntry *topSortEntry = makeTargetEntry((Expr *) orderVar,
												(AttrNumber) parseState->p_next_resno++,
												pstrdup("sortScore"),
												resjunk);
	finalQuery->targetList = lappend(finalQuery->targetList, topSortEntry);
	List *sortlist = addTargetToSortList(parseState, topSortEntry,
										 NIL, finalQuery->targetList, sortBy);

	pfree(parseState);
	finalQuery->sortClause = sortlist;

	/* Add the similarity score field to the metadata in the document */
	Assert(IsA(sortEntry->expr, OpExpr));
	AddScoreFieldToDocumentEntry(documentEntry, sortEntry->expr, (Expr *) orderVar);

	return finalQuery;
}


/* Add the similarity score field to the metadata in the document
 * bson_document_add_score_field(document, similarityScore)
 * Result:  "document": { ... , "$__cosmos_meta__.score": { "score": 0.9 } }
 */
static Expr *
AddScoreFieldToDocumentEntry(TargetEntry *documentEntry, Expr *vectorSortExpr,
							 Expr *orderVar)
{
	Oid similarityDistanceOid = InvalidOid;
	if (IsA(vectorSortExpr, OpExpr))
	{
		OpExpr *orderOpExpr = (OpExpr *) vectorSortExpr;
		similarityDistanceOid = orderOpExpr->opno;
	}
	else if (IsA(vectorSortExpr, FuncExpr))
	{
		FuncExpr *orderFuncExpr = (FuncExpr *) vectorSortExpr;
		similarityDistanceOid = orderFuncExpr->funcid;
	}
	else
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg(
							"unsupported vector search operator/function type")));
	}

	ReplaceDocumentVarOnSortContext sortContext =
	{
		.sourceExpr = MakeSimpleDocumentVar(),
		.targetExpr = documentEntry->expr,
	};

	/* Use expression_tree_mutator so we copy the orderby Expr before changing it */
	Expr *scoreExprInput = (Expr *) expression_tree_mutator((Node *) orderVar,
															ReplaceDocumentVarOnSort,
															&sortContext);

	Expr *scoreExpr = GenerateScoreExpr((Expr *) scoreExprInput, similarityDistanceOid);

	List *args = list_make2(documentEntry->expr, scoreExpr);
	FuncExpr *resultExpr = makeFuncExpr(
		ApiBsonDocumentAddScoreFieldFunctionId(), BsonTypeId(), args, InvalidOid,
		InvalidOid, COERCE_EXPLICIT_CALL);

	documentEntry->expr = (Expr *) resultExpr;

	return scoreExprInput;
}


/*
 * Generates similar score expression based on the similarity search operator.
 * cos: 1.0 - orderScore
 * ip: -1.0 * orderScore
 * l2: orderScore
 */
static Expr *
GenerateScoreExpr(const Expr *orderVar, Oid similarityDistanceOid)
{
	Expr *scoreExpr = NULL;
	if (similarityDistanceOid == VectorCosineSimilaritySearchOperatorId() ||
		similarityDistanceOid == VectorCosineSimilaritySearchFunctionId())
	{
		/* Similarity search score is 1.0 - orderVar */
		Const *oneConst = makeConst(FLOAT8OID, -1, InvalidOid,
									sizeof(float8), Float8GetDatum(1.0),
									false, true);
		scoreExpr = make_opclause(
			Float8MinusOperatorId(), FLOAT8OID, false,
			(Expr *) oneConst, (Expr *) orderVar, InvalidOid, InvalidOid);
	}
	else if (similarityDistanceOid == VectorIPSimilaritySearchOperatorId() ||
			 similarityDistanceOid == VectorIPSimilaritySearchFunctionId())
	{
		/* Similarity search score is -1.0 * orderVar */
		Const *minusOneConst = makeConst(FLOAT8OID, -1, InvalidOid,
										 sizeof(float8), Float8GetDatum(-1.0),
										 false, true);
		scoreExpr = (Expr *) make_opclause(
			Float8MultiplyOperatorId(), FLOAT8OID, false,
			(Expr *) minusOneConst, (Expr *) orderVar, InvalidOid, InvalidOid);
	}
	else if (similarityDistanceOid == VectorL2SimilaritySearchOperatorId() ||
			 similarityDistanceOid == VectorL2SimilaritySearchFunctionId())
	{
		/* Similarity search score is orderVar */
		scoreExpr = (Expr *) orderVar;
	}
	else
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg(
							"unsupported vector search operator type")));
	}

	return (Expr *) scoreExpr;
}


/*
 * Adds a wrapper select query to the join query and re-order by the orderVar.
 * To force the planner to re-order the query, we use orderVar + 0 for order by.
 * e.g.
 *    SELECT document FROM (JOIN c1 ON c1.ctid = c2.ctid limit k) ORDER BY c1.orderVal + 0
 */
static Query *
ReorderQueryResults(Query *joinQuery, AggregationPipelineBuildContext *context)
{
	context->expandTargetList = true;
	Query *wrapperQuery = MigrateQueryToSubQuery(joinQuery, context);

	/* document var is added by the MigrateQueryToSubQuery */
	/* orderScore var is the second target entry of subquery*/
	/* Add the sort clause for the re-order query */
	Var *orderVar = makeVar(1,
							2,
							FLOAT8OID,
							-1,
							InvalidOid,
							0);

	ParseState *parseState = make_parsestate(NULL);
	parseState->p_expr_kind = EXPR_KIND_ORDER_BY;

	/* set after what is already taken */
	parseState->p_next_resno = list_length(wrapperQuery->targetList) + 1;

	SortBy *sortBy = makeNode(SortBy);
	sortBy->location = -1;
	sortBy->sortby_dir = SORTBY_DEFAULT; /* reset later */

	/* order by orderVar + 0 */
	Const *zeroConst = makeConst(FLOAT8OID, -1, InvalidOid,
								 sizeof(float8), Float8GetDatum(0),
								 false, true);
	Expr *scoreExpr = make_opclause(
		Float8PlusOperatorId(), FLOAT8OID, false,
		(Expr *) orderVar, (Expr *) zeroConst, InvalidOid, InvalidOid);

	sortBy->node = (Node *) scoreExpr;

	/* Hide the orderVar*/
	bool resjunk = true;
	TargetEntry *topSortEntry = makeTargetEntry((Expr *) scoreExpr,
												(AttrNumber) parseState->p_next_resno++,
												pstrdup("orderScore"),
												resjunk);
	wrapperQuery->targetList = lappend(wrapperQuery->targetList, topSortEntry);
	List *sortlist = addTargetToSortList(parseState, topSortEntry,
										 NIL, wrapperQuery->targetList, sortBy);

	pfree(parseState);
	wrapperQuery->sortClause = sortlist;

	return wrapperQuery;
}


/*
 * Retrieves all the path strings from the indexIdList and adds it to the hash set.
 */
static void
AddPathStringToHashset(List *indexIdList, HTAB *stringHashSet)
{
	ListCell *indexId;
	foreach(indexId, indexIdList)
	{
		Relation indexRelation = RelationIdGetRelation(lfirst_oid(indexId));

		if (indexRelation->rd_rel->relam == RumIndexAmId())
		{
			int numberOfKeyAttributes = IndexRelationGetNumberOfKeyAttributes(
				indexRelation);
			for (int i = 0; i < numberOfKeyAttributes; i++)
			{
				/* Check if the index is a single path index */
				if (indexRelation->rd_opcoptions[i] != NULL &&
					indexRelation->rd_opfamily[i] == BsonRumSinglePathOperatorFamily())
				{
					bytea *optBytea = indexRelation->rd_opcoptions[i];

					BsonGinSinglePathOptions *indexOption =
						(BsonGinSinglePathOptions *) optBytea;
					uint32_t pathCount = 0;
					const char *pathStr;
					Get_Index_Path_Option(indexOption, path, pathStr, pathCount);

					char *copiedPathStr = palloc(pathCount + 1);
					strcpy(copiedPathStr, pathStr);

					/* Add the index path to the hash set */
					StringView hashEntry = CreateStringViewFromStringWithLength(
						copiedPathStr,
						pathCount);

					bool found = false;
					hash_search(stringHashSet, &hashEntry, HASH_ENTER, &found);
				}
			}
		}
		RelationClose(indexRelation);
	}
}


static Query *
AddFilterToQuery(Query *query, AggregationPipelineBuildContext *context,
				 VectorSearchOptions *vectorSearchOptions)
{
	RangeTblEntry *rte = linitial(query->rtable);

	Relation collectionRelation = RelationIdGetRelation(rte->relid);
	List *indexIdList = RelationGetIndexList(collectionRelation);
	RelationClose(collectionRelation);

	/* Before we can do prefiltering we need to make sure that
	 * the collection has the appropriate index.
	 * Retrieve the path string of SinglePath index path from the collection */
	HTAB *indexPathNameHashSet = CreateStringViewHashSet();
	AddPathStringToHashset(indexIdList, indexPathNameHashSet);
	context->requiredFilterPathNameHashSet = indexPathNameHashSet;

	/* Add a match expression into where clause
	 * checking that the collection has the appropriate index keys
	 * Example: where document @@ '{ "value": {$regex: /^bb/}' */
	Query *filterQuery = HandleMatch(&vectorSearchOptions->filterBson, query, context);

	context->requiredFilterPathNameHashSet = NULL;
	hash_destroy(indexPathNameHashSet);

	return filterQuery;
}


/*
 * Generates a query based on the vector search query and user specified filters.
 * The query is generated as follows:
 *
 * 1. Search query:
 *    SELECT
 *        document,
 *        (public.vector(ApiCatalogSchema.bson_extract_vector(collection.document, 'v'::text), 3, true) OPERATOR(public.<=>) public.vector(ApiCatalogSchema.bson_extract_vector('{ "vector" : [ { "$numberDouble" : "3.0" }, { "$numberDouble" : "4.9000000000000003553" }, { "$numberDouble" : "1.0" } ], "k" : { "$numberInt" : "1" }, "path" : "v" }'::CoreSchema.bson, 'vector'::text), 3, true)) AS orderVal,
 *        ctid
 *    FROM
 *        ApiDataSchemaName.documents_1 collection
 *    WHERE
 *        shard_key_value = 0
 *        AND ((ApiCatalogSchema.bson_extract_vector(collection.document, 'v'::text) IS NOT NULL))
 *    ORDER BY
 *        (public.vector(ApiCatalogSchema.bson_extract_vector(collection.document, 'v'::text), 3, true) OPERATOR(public.<=>) public.vector(ApiCatalogSchema.bson_extract_vector('{ "vector" : [ { "$numberDouble" : "3.0" }, { "$numberDouble" : "4.9000000000000003553" }, { "$numberDouble" : "1.0" } ], "k" : { "$numberInt" : "1" }, "path" : "v" }'::CoreSchema.bson, 'vector'::text), 3, true))
 *
 * 2. Filter query:
 *    SELECT
 *        ctid
 *    FROM
 *        ApiDataSchemaName.documents_1
 *    WHERE
 *        shard_key_value = 0
 *        AND document @@ '{ "a": "some sentence" }'
 *
 * 3. JOIN c1 ON c1.ctid = c2.ctid
 *    SELECT
 *        c1.document
 *    FROM c1 JOIN c2 ON c2.ctid = c1.ctid
 *    ORDER BY c1.orderVal
 */
static Query *
GeneratePrefilteringVectorSearchQuery(Query *searchQuery,
									  AggregationPipelineBuildContext *context,
									  VectorSearchOptions *vectorSearchOptions,
									  TargetEntry *sortEntry,
									  Expr *processedSortExpr,
									  Node *limitCount)
{
	/* choose enn or ann for the filter query */
	if (vectorSearchOptions->exactSearch)
	{
		/*
		 * Add filters to the search query, use filter index scan + runtime sort
		 * e.g.
		 *   SELECT
		 *		API_CATALOG_SCHEMA_NAME.bson_document_add_score_field(document, {order by expr}) AS document
		 *   FROM (
		 *		SELECT document
		 *		FROM API_DATA_SCHEMA_NAME.documents_2_102046 collection
		 *		WHERE ((document OPERATOR(API_CATALOG_SCHEMA_NAME.#=) '{ "attr" : { "$numberInt" : "1000002" } }'::API_CATALOG_SCHEMA_NAME.bsonquery)
		 *			AND (shard_key_value OPERATOR(pg_catalog.=) '2'::bigint)
		 *			AND (API_CATALOG_SCHEMA_NAME.bson_extract_vector(document, 'vect'::text) IS NOT NULL))
		 *      ORDER BY (
		 *          public.cosine_similarity(
		 *              public.vector(API_CATALOG_SCHEMA_NAME.bson_extract_vector(document, 'vect'::text), 1536, true),
		 *              public.vector(API_CATALOG_SCHEMA_NAME.bson_extract_vector('{ "vector" : [...], "k" : 1, "path" : "vect", "filter" : { "attr" : { "$eq" : 1000002 } }, "lSearch" : 40 }'::API_CATALOG_SCHEMA_NAME.bson, 'vector'::text), 1536, true))
		 *      )
		 *      LIMIT '1'::bigint;
		 *   )
		 */

		/* Add filters to the search query */
		searchQuery = AddFilterToQuery(searchQuery, context, vectorSearchOptions);

		/* Add limit before push to subquery */
		searchQuery->limitCount = limitCount;

		/* Wrapper query */
		context->expandTargetList = false;
		Query *wrapperQuery = MigrateQueryToSubQuery(searchQuery, context);

		/* Add sort score to the metadata in the document */
		TargetEntry *documentEntry = linitial(wrapperQuery->targetList);
		AddScoreFieldToDocumentEntry(documentEntry, sortEntry->expr,
									 sortEntry->expr);

		return wrapperQuery;
	}
	else
	{
		/*
		 * 1. construct searchQuery: add sort entry and ctid to targetList
		 */

		/* add the sort entry with the alias "orderVal" to the targetList */
		const char *sortAlias = "orderVal";
		sortEntry->resname = pstrdup(sortAlias);
		sortEntry->resjunk = false;

		/* append ctid to targetList */
		bool replaceTargetList = false;
		TargetEntry *leftCtidEntry = AddCtidToQueryTargetList(searchQuery,
															  replaceTargetList);

		/*
		 * 2. construct filterQuery: add match expression into where clause
		 */
		pg_uuid_t *collectionUuid = NULL;
		Query *filterQuery = GenerateBaseTableQuery(context->databaseNameDatum,
													&context->collectionNameView,
													collectionUuid,
													context);

		/* Add filters to the filter query */
		filterQuery = AddFilterToQuery(filterQuery, context, vectorSearchOptions);

		/* replace targetList with ctid */
		replaceTargetList = true;
		TargetEntry *rightCtidEntry = AddCtidToQueryTargetList(filterQuery,
															   replaceTargetList);

		/* 3. JOIN c1 ON c1.ctid = c2.ctid
		 *  SELECT
		 *      c1.document
		 *  FROM c1 JOIN c2 ON c2.ctid = c1.ctid
		 *  ORDER BY c1.orderVal
		 */
		Query *joinedQuery = JoinVectorSearchQueryWithFilterQuery(searchQuery,
																  filterQuery,
																  leftCtidEntry,
																  rightCtidEntry,
																  sortEntry,
																  vectorSearchOptions->
																  vectorIndexDef->
																  needsReorderAfterFilter);

		/* Add the limit to the query before reordering */
		joinedQuery->limitCount = limitCount;

		if (vectorSearchOptions->vectorIndexDef->needsReorderAfterFilter)
		{
			/* Search result by iterative search may be disordered, so we need to reorder the result */
			joinedQuery = ReorderQueryResults(joinedQuery, context);
		}

		return joinedQuery;
	}
}


/*
 * Create WHERE bson_extract_vector(document, path) IS NOT NULL
 * and add it to the WHERE clause.
 */
static void
AddNullVectorCheckToQuery(Query *query, const Expr *vectorSortExpr)
{
	Assert(IsA(vectorSortExpr, OpExpr) || IsA(vectorSortExpr, FuncExpr));

	FuncExpr *castFunctionExpr = NULL;
	if (IsA(vectorSortExpr, OpExpr))
	{
		castFunctionExpr = (FuncExpr *) linitial(
			((OpExpr *) vectorSortExpr)->args);
	}
	else
	{
		castFunctionExpr = (FuncExpr *) linitial(
			((FuncExpr *) vectorSortExpr)->args);
	}

	Expr *extractVectorFunctionExpr = linitial(castFunctionExpr->args);
	NullTest *vectorNullTest = makeNode(NullTest);
	vectorNullTest->nulltesttype = IS_NOT_NULL;
	vectorNullTest->arg = (Expr *) extractVectorFunctionExpr;
	vectorNullTest->argisrow = false;
	if (query->jointree->quals != NULL)
	{
		List *qualsWithVectorNullTest = lappend(make_ands_implicit(
													(Expr *) query->jointree->quals),
												(Node *) vectorNullTest);
		query->jointree->quals = (Node *) make_ands_explicit(qualsWithVectorNullTest);
	}
	else
	{
		query->jointree->quals = (Node *) vectorNullTest;
	}
}


/*
 * Add vectorSortExpr as target entry to the query target list, resjunk is true.
 * Add sort by to the query, sort by the target entry.
 * returns the target entry that was added.
 */
static TargetEntry *
AddSortByToQuery(Query *query, const Expr *vectorSortExpr)
{
	ParseState *parseState = make_parsestate(NULL);
	parseState->p_expr_kind = EXPR_KIND_ORDER_BY;

	/* set after what is already taken */
	parseState->p_next_resno = list_length(query->targetList) + 1;

	SortBy *sortBy = makeNode(SortBy);
	sortBy->location = -1;
	sortBy->sortby_dir = SORTBY_DEFAULT; /* reset later */
	sortBy->node = (Node *) vectorSortExpr;

	bool resjunk = true;
	TargetEntry *sortEntry = makeTargetEntry((Expr *) vectorSortExpr,
											 (AttrNumber) parseState->p_next_resno++,
											 "?sort?",
											 resjunk);
	query->targetList = lappend(query->targetList, sortEntry);
	List *sortlist = addTargetToSortList(parseState, sortEntry,
										 NIL, query->targetList, sortBy);

	pfree(parseState);
	query->sortClause = sortlist;
	return sortEntry;
}


/**
 * parse the atlas search spec to cosmos search spec
 * And generate equivalent cosmos search query spec from the atlas search spec.
 * Set the vectorSearchOptions with the generated cosmos search query spec.
 */
static void
ParseAndValidateMongoNativeVectorSearchSpec(const bson_value_t *nativeVectorSearchSpec,
											VectorSearchOptions *vectorSearchOptions)
{
	EnsureTopLevelFieldValueType("vectorSearch", nativeVectorSearchSpec,
								 BSON_TYPE_DOCUMENT);
	bson_iter_t nativeVectorSearchIter;
	BsonValueInitIterator(nativeVectorSearchSpec, &nativeVectorSearchIter);

	pgbson_writer writer;
	PgbsonWriterInit(&writer);

	while (bson_iter_next(&nativeVectorSearchIter))
	{
		const char *key = bson_iter_key(&nativeVectorSearchIter);
		const bson_value_t *value = bson_iter_value(&nativeVectorSearchIter);
		if (strcmp(key, "queryVector") == 0)
		{
			if (!BsonValueHoldsNumberArray(value,
										   &vectorSearchOptions->queryVectorLength))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg(
									"$vectorSearch.queryVector must be an array of numbers.")));
			}

			if (vectorSearchOptions->queryVectorLength == 0)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg(
									"$vectorSearch.queryVector cannot be an empty array.")));
			}

			if (vectorSearchOptions->queryVectorLength > VECTOR_MAX_DIMENSIONS)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg(
									"Length of the query vector cannot exceed %d",
									VECTOR_MAX_DIMENSIONS)));
			}
			vectorSearchOptions->queryVectorValue = *value;
			PgbsonWriterAppendValue(&writer, "vector", 6, value);
		}
		else if (strcmp(key, "numCandidates") == 0)
		{
			EnsureTopLevelFieldValueType("numCandidates", value, BSON_TYPE_INT32);
			if (value->value.v_int32 < HNSW_MIN_EF_SEARCH)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg(
									"$vectorSearch.numCandidates must be greater than or equal to %d.",
									HNSW_MIN_EF_SEARCH),
								errdetail_log(
									"$vectorSearch.numCandidates must be greater than or equal to %d.",
									HNSW_MIN_EF_SEARCH)));
			}

			if (value->value.v_int32 > HNSW_MAX_EF_SEARCH)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg(
									"$vectorSearch.numCandidates must be less than or equal to %d.",
									HNSW_MAX_EF_SEARCH),
								errdetail_log(
									"$vectorSearch.numCandidates must be less than or equal to %d.",
									HNSW_MAX_EF_SEARCH)));
			}

			PgbsonWriterAppendInt32(&writer, "efSearch", 8,
									value->value.v_int32);
		}
		else if (strcmp(key, "path") == 0)
		{
			EnsureTopLevelFieldValueType("path", value, BSON_TYPE_UTF8);
			vectorSearchOptions->searchPath = pstrdup(value->value.v_utf8.str);

			if (vectorSearchOptions->searchPath == NULL)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg(
									"$vectorSearch.path cannot be empty.")));
			}
			PgbsonWriterAppendUtf8(&writer, "path", 4,
								   value->value.v_utf8.str);
		}
		else if (strcmp(key, "limit") == 0)
		{
			EnsureTopLevelFieldValueType("limit", value, BSON_TYPE_INT32);
			if (value->value.v_int32 < 1)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg(
									"$vectorSearch.limit must be a positive integer.")));
			}
			PgbsonWriterAppendInt32(&writer, "k", 1,
									value->value.v_int32);
			vectorSearchOptions->resultCount = BsonValueAsInt32(value);
		}
		else if (strcmp(key, "index") == 0)
		{
			/* Specifying index is not yet supported*/
			continue;
		}
		else if (strcmp(key, "filter") == 0)
		{
			if (!EnableVectorPreFilter && !EnableVectorPreFilterV2)
			{
				/* Safe guard against the enableVectorPreFilter GUC */
				ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
								errmsg("$filter is not supported for vector search yet."),
								errdetail_log(
									"vector pre-filter is disabled. Set ApiGucPrefix.enableVectorPreFilter to true to enable vector pre filter.")));
			}
			EnsureTopLevelFieldValueType("filter", value, BSON_TYPE_DOCUMENT);
			PgbsonWriterAppendValue(&writer, "filter", 6, value);
			vectorSearchOptions->filterBson = *value;
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_UNKNOWNBSONFIELD),
							errmsg(
								"BSON field '$vectorSearch.%s' is an unknown field",
								key)));
		}
	}
	if (vectorSearchOptions->searchPath == NULL ||
		vectorSearchOptions->queryVectorValue.value_type == BSON_TYPE_EOD ||
		vectorSearchOptions->resultCount < 0)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg(
							"$path, $queryVector, and $limit are all required fields for using a vector index.")));
	}

	vectorSearchOptions->searchSpecPgbson = PgbsonWriterGetPgbson(&writer);
}


static Expr *
CheckVectorIndexAndGenerateSortExpr(Query *query,
									VectorSearchOptions *vectorSearchOptions,
									AggregationPipelineBuildContext *context)
{
	RangeTblEntry *rte = linitial(query->rtable);

	Relation collectionRelation = RelationIdGetRelation(rte->relid);
	List *indexIdList = RelationGetIndexList(collectionRelation);
	RelationClose(collectionRelation);

	Expr *processedSortExpr = NULL;
	ListCell *indexId;
	Node *queryNode = (Node *) MakeBsonConst(vectorSearchOptions->searchSpecPgbson);

	foreach(indexId, indexIdList)
	{
		FuncExpr *vectorExtractFunc = NULL;
		Relation indexRelation = RelationIdGetRelation(lfirst_oid(indexId));
		if (IsMatchingVectorIndex(indexRelation, vectorSearchOptions->searchPath,
								  &vectorExtractFunc))
		{
			/* Vector search is on the doc even if there's projectors etc. */
			processedSortExpr = GenerateVectorSortExpr(
				vectorSearchOptions->searchPath, vectorExtractFunc, indexRelation,
				(Node *) MakeSimpleDocumentVar(), queryNode,
				vectorSearchOptions->exactSearch);

			vectorSearchOptions->vectorAccessMethodOid = indexRelation->rd_rel->relam;
		}
		RelationClose(indexRelation);
		if (processedSortExpr != NULL)
		{
			break;
		}
	}

	if (processedSortExpr == NULL)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg(
							"Similarity index was not found for a vector similarity search query.")));
	}

	if (vectorSearchOptions->vectorAccessMethodOid == InvalidOid)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg(
							"Similarity index was not found for a vector similarity search query.")));
	}

	return processedSortExpr;
}


static void
ParseAndValidateIndexSpecificOptions(VectorSearchOptions *vectorSearchOptions)
{
	Assert(vectorSearchOptions->vectorAccessMethodOid != InvalidOid);

	/* Parse the index specific options */
	pgbson *searchParamPgbson =
		vectorSearchOptions->vectorIndexDef->parseIndexSearchSpecFunc(
			vectorSearchOptions);
	if (searchParamPgbson != NULL)
	{
		vectorSearchOptions->searchParamPgbson = searchParamPgbson;
	}
}


/*
 * Given a node, if it matches the source expression, then
 * it's replaced with the targetExpression.
 */
static Node *
ReplaceDocumentVarOnSort(Node *input, ReplaceDocumentVarOnSortContext *context)
{
	if (input == NULL)
	{
		return NULL;
	}

	if (IsA(input, Var))
	{
		if (equal(input, context->sourceExpr))
		{
			return (Node *) context->targetExpr;
		}
	}

	return expression_tree_mutator(input, ReplaceDocumentVarOnSort, context);
}


/* core logic for vector search*/
static Query *
HandleVectorSearchCore(Query *query, VectorSearchOptions *vectorSearchOptions,
					   AggregationPipelineBuildContext *context)
{
	/* check vector index and generate sort expr */
	Expr *processedSortExpr = CheckVectorIndexAndGenerateSortExpr(query,
																  vectorSearchOptions,
																  context);

	/* Get the vector index definition */
	const VectorIndexDefinition *definition = GetVectorIndexDefinitionByIndexAmOid(
		vectorSearchOptions->vectorAccessMethodOid);
	if (definition == NULL)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
						errmsg("Unsupported vector index type")));
	}
	vectorSearchOptions->vectorIndexDef = definition;

	/* Parse and validate the index specific options */
	ParseAndValidateIndexSpecificOptions(vectorSearchOptions);

	/* Add the search param wrapper function to the query */
	/* Create the WHERE bson_search_param(document, searchParamPgbson) and add it to the WHERE */
	if (!vectorSearchOptions->exactSearch)
	{
		AddSearchParamFunctionToQuery(query, vectorSearchOptions->vectorAccessMethodOid,
									  vectorSearchOptions->searchParamPgbson);
	}
	else
	{
		ReportFeatureUsage(FEATURE_STAGE_SEARCH_VECTOR_EXACT);
	}

	/* Add the null vector check to the query, so that we don't return documents that don't have the vector field */
	AddNullVectorCheckToQuery(query, processedSortExpr);

	/* Add the sort by to the query */
	TargetEntry *sortEntry = AddSortByToQuery(query, processedSortExpr);

	Node *limitCount = (Node *) makeConst(INT8OID, -1, InvalidOid, sizeof(int64_t),
										  Int64GetDatum(
											  vectorSearchOptions->resultCount),
										  false,
										  true);

	/* If there's a filter, add it to the query */
	if (EnableVectorPreFilterV2 &&
		vectorSearchOptions->filterBson.value_type != BSON_TYPE_EOD &&
		!IsBsonValueEmptyDocument(&vectorSearchOptions->filterBson))
	{
		ReportFeatureUsage(FEATURE_STAGE_SEARCH_VECTOR_PRE_FILTER);

		/* check if the collection is unsharded */
		if (context->mongoCollection != NULL &&
			context->mongoCollection->shardKey != NULL)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
							errmsg(
								"Filter is not supported for vector search on sharded collection.")));
		}

		/* Just add the match directly to the query */
		query = HandleMatch(&vectorSearchOptions->filterBson, query, context);

		/* Add the limit to the query before reordering */
		query->limitCount = limitCount;

		/* After the limit is applied, push to a subquery */
		query = MigrateQueryToSubQuery(query, context);

		/* Now we have the first arg as the document. Add a second projector for
		 * the score. We do this so we only add the score for the subset of docs
		 * matching the filter.
		 */
		TargetEntry *documentEntry = linitial(query->targetList);
		Expr *scoreExpr = AddScoreFieldToDocumentEntry(documentEntry, sortEntry->expr,
													   sortEntry->expr);

		TargetEntry *scoreEntry = makeTargetEntry(scoreExpr, 2, "sortVal", false);
		query->targetList = lappend(query->targetList, scoreEntry);

		/* now reorder to ensure it matches the score order by */
		query = ReorderQueryResults(query, context);
	}
	else if (EnableVectorPreFilter &&
			 vectorSearchOptions->filterBson.value_type != BSON_TYPE_EOD &&
			 !IsBsonValueEmptyDocument(&vectorSearchOptions->filterBson))
	{
		ReportFeatureUsage(FEATURE_STAGE_SEARCH_VECTOR_PRE_FILTER);

		/* check if the collection is unsharded */
		if (context->mongoCollection != NULL &&
			context->mongoCollection->shardKey != NULL)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
							errmsg(
								"Filter is not supported for vector search on sharded collection.")));
		}

		query = GeneratePrefilteringVectorSearchQuery(query, context,
													  vectorSearchOptions,
													  sortEntry,
													  processedSortExpr,
													  limitCount);
	}
	else
	{
		/* Add the limit to the query from k in the search spec */
		query->limitCount = limitCount;

		/* After the limit is applied, push to a subquery */
		query = MigrateQueryToSubQuery(query, context);

		/* Add the score field */
		TargetEntry *documentEntry = linitial(query->targetList);
		AddScoreFieldToDocumentEntry(documentEntry, sortEntry->expr, sortEntry->expr);
	}

	/* Push next stage to a new subquery (since we did a sort) */
	context->requiresSubQueryAfterProject = true;

	return query;
}


/*
 * This method is for common validation of knnBeta and cosmosSearch
 * NULL values are passed for parameters that are not needed to be validated
 */
static void
ParseAndValidateVectorQuerySpecCore(const pgbson *vectorSearchSpecPgbson,
									VectorSearchOptions *vectorSearchOptions)
{
	bson_iter_t specIter;

	PgbsonInitIterator(vectorSearchSpecPgbson, &specIter);
	while (bson_iter_next(&specIter))
	{
		const char *key = bson_iter_key(&specIter);
		const bson_value_t *value = bson_iter_value(&specIter);

		if (strcmp(key, "path") == 0)
		{
			if (value->value_type != BSON_TYPE_UTF8)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg(
									"$path must be a text value")));
			}

			vectorSearchOptions->searchPath = pstrdup(value->value.v_utf8.str);

			if (vectorSearchOptions->searchPath == NULL)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg(
									"$path cannot be empty.")));
			}
		}
		else if (strcmp(key, "vector") == 0)
		{
			vectorSearchOptions->queryVectorValue = *value;
			if (!BsonValueHoldsNumberArray(&vectorSearchOptions->queryVectorValue,
										   &vectorSearchOptions->queryVectorLength))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg(
									"$vector must be an array of numbers.")));
			}

			if (vectorSearchOptions->queryVectorLength == 0)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg(
									"$vector cannot be an empty array.")));
			}

			if (vectorSearchOptions->queryVectorLength > VECTOR_MAX_DIMENSIONS)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg(
									"Length of the query vector cannot exceed %d",
									VECTOR_MAX_DIMENSIONS)));
			}
		}
		else if (strcmp(key, "k") == 0)
		{
			if (!BSON_ITER_HOLDS_NUMBER(&specIter))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg(
									"$k must be an integer value.")));
			}

			vectorSearchOptions->resultCount = BsonValueAsInt32(value);

			if (vectorSearchOptions->resultCount < 1)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg(
									"$k must be a positive integer.")));
			}
		}
		else if (strcmp(key, "filter") == 0)
		{
			if (!EnableVectorPreFilter && !EnableVectorPreFilterV2)
			{
				/* Safe guard against the enableVectorPreFilter GUC */
				ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
								errmsg("$filter is not supported for vector search yet."),
								errdetail_log(
									"vector pre-filter is disabled. Set ApiGucPrefix.enableVectorPreFilter to true to enable vector pre filter.")));
			}

			if (!BSON_ITER_HOLDS_DOCUMENT(&specIter))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg(
									"$filter must be a document value.")));
			}

			vectorSearchOptions->filterBson = *value;
		}
		else if (strcmp(key, "exact") == 0)
		{
			if (!BSON_ITER_HOLDS_BOOL(&specIter))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg(
									"$exact must be a boolean value.")));
			}

			vectorSearchOptions->exactSearch = BsonValueAsBool(value);
		}
		else if (strcmp(key, "score") == 0)
		{
			if (!BSON_ITER_HOLDS_DOCUMENT(&specIter))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg(
									"$score must be a document value.")));
			}

			vectorSearchOptions->scoreBson = *value;
		}
		else
		{
			/* Custom hook for parsing and validating vector query spec */
			TryCustomParseAndValidateVectorQuerySpec(key,
													 value,
													 vectorSearchOptions);
		}
	}

	if (vectorSearchOptions->searchPath == NULL)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg(
							"$path is required field for using a vector index.")));
	}

	if (vectorSearchOptions->queryVectorValue.value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg(
							"$vector is required field for using a vector index.")));
	}

	if (vectorSearchOptions->resultCount < 0)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg(
							"$k is required field for using a vector index.")));
	}
}


/*
 * Validate that a cosmosSearch query with vector index has all the required options with valid datatypes, namely
 *  1. path: a string denoting the path that was indexed.
 *  2. vector: a non-empty number array.
 *  3. k : an integer denoting the number of requested results.
 *  4. nProbes: an integer denoting the number of probes to use for the ivfflat search.
 *  5. efSearch: an integer denoting the number of efSearch to use for the hnsw search.
 *  6. filter: match expression that compares an indexed field with a boolean, number (not decimals), or string to use as a prefilter, which can help narrow down the scope of vector search.
 *  7. exact: a boolean denoting whether to use exact search or ann search.
 *
 *  "cosmosSearch": {
 *    "vector": [<array-of-numbers>],
 *    "path": "<field-to-search>",
 *    "filter": {<filter-specification>},
 *    "k": <number>,
 *    "exact": <boolean>,
 *  }
 *
 * Example query spec of ivfflat index
 *   '{ "path" : "myvector", "vector": [8.0, 1.0, 9.0], "k": 10, "nProbes": 4 }'::ApiCatalogSchemaName.bson
 *
 * Example query spec of hnsw index
 *   '{ "path" : "myvector", "vector": [8.0, 1.0, 9.0], "k": 10, "efSearch": 4 }'::ApiCatalogSchemaName.bson
 *
 * Example filter spec
 *   '{ "path" : "myvector", "vector": [8.0, 1.0, 9.0], "k": 10, "nProbes": 4, "filter": { "meta.value": {$regex: /^bb/} } }'::ApiCatalogSchemaName.bson
 *
 */
static void
ParseAndValidateCosmosSearchQuerySpec(const pgbson *vectorSearchSpecPgbson,
									  VectorSearchOptions *vectorSearchOptions)
{
	ParseAndValidateVectorQuerySpecCore(vectorSearchSpecPgbson,
										vectorSearchOptions);
}


/*
 * Validate that a knnBeta query with vector index has all the required options with valid datatypes, namely
 *  1. path: a string denoting the path that was indexed.
 *  2. vector: a non-empty number array.
 *  3. k : an integer denoting the number of requested results.
 *  4. filter: Not supported
 *  5. score: Not supported
 *
 * "knnBeta": {
 *    "vector": [<array-of-numbers>],
 *    "path": "<field-to-search>",
 *    "filter": {<filter-specification>},
 *    "k": <number>,
 *    "score": {<options>}
 *  }
 *
 * Example query spec: '{ "path" : "myvector", "vector": [8.0, 1.0, 9.0], "k": 10 }'::ApiCatalogSchemaName.bson
 *
 *
 */
static void
ParseAndValidateKnnBetaQuerySpec(const pgbson *vectorSearchSpecPgbson,
								 VectorSearchOptions *vectorSearchOptions)
{
	ParseAndValidateVectorQuerySpecCore(vectorSearchSpecPgbson,
										vectorSearchOptions);

	bson_value_t filterBson = vectorSearchOptions->filterBson;
	if (filterBson.value_type != BSON_TYPE_EOD && !IsBsonValueEmptyDocument(&filterBson))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
						errmsg(
							"$filter is not supported for knnBeta queries.")));
	}

	bson_value_t scoreBson = vectorSearchOptions->scoreBson;
	if (scoreBson.value_type != BSON_TYPE_EOD && !IsBsonValueEmptyDocument(&scoreBson))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
						errmsg(
							"$score is not supported for knnBeta queries.")));
	}
}
