/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/planner/bson_aggregation_pipeline_private.h
 *
 * Private helpers for the bson_aggregation_pipeline definition
 *
 *-------------------------------------------------------------------------
 */

#include <catalog/pg_collation.h>
#include <nodes/parsenodes.h>
#include <nodes/makefuncs.h>

#include "metadata/collection.h"
#include <utils/version_utils.h>
#include "collation/collation.h"

#ifndef BSON_AGGREGATION_PIPELINE_PRIVATE_H
#define BSON_AGGREGATION_PIPELINE_PRIVATE_H

typedef struct AggregationStageDefinition AggregationStageDefinition;

/*
 * for nested stage, use this to record its parent stage name
 */
typedef enum ParentStageName
{
	ParentStageName_NOPARENT = 0,
	ParentStageName_LOOKUP,
	ParentStageName_FACET,
	ParentStageName_UNIONWITH,
	ParentStageName_INVERSEMATCH,
} ParentStageName;


/*
 * Enums to represent all kind of aggregations stages.
 * Please keep the list sorted within their groups for easier readability
 */
typedef enum
{
	Stage_Invalid = 0,

	/* Start internal stages Mongo */
	Stage_Internal_InhibitOptimization = 1,

	/* Start Mongo Public stages */
	Stage_AddFields = 10,
	Stage_Bucket,
	Stage_BucketAuto,
	Stage_ChangeStream,
	Stage_CollStats,
	Stage_Count,
	Stage_CurrentOp,
	Stage_Densify,
	Stage_Documents,
	Stage_Facet,
	Stage_Fill,
	Stage_GeoNear,
	Stage_GraphLookup,
	Stage_Group,
	Stage_IndexStats,
	Stage_Limit,
	Stage_ListLocalSessions,
	Stage_ListSessions,
	Stage_Lookup,
	Stage_Match,
	Stage_Merge,
	Stage_Out,
	Stage_Project,
	Stage_Redact,
	Stage_ReplaceRoot,
	Stage_ReplaceWith,
	Stage_Sample,
	Stage_Search,
	Stage_SearchMeta,
	Stage_Set,
	Stage_SetWindowFields,
	Stage_Skip,
	Stage_Sort,
	Stage_SortByCount,
	Stage_UnionWith,
	Stage_Unset,
	Stage_Unwind,
	Stage_VectorSearch,

	/* Start of pg_documentdb Custom or internal stages */
	Stage_InverseMatch = 100,
	Stage_LookupUnwind
} Stage;


/*
 * Shared context during aggregation pipeline build phase.
 */
typedef struct
{
	/* The current stage number (used for tagging stage identifiers) */
	int stageNum;

	/* Whether or not a subquery stage should be injected before the next stage */
	bool requiresSubQuery;

	/* If true, allows 1 project transform, then forces a subquery stage. */
	bool requiresSubQueryAfterProject;

	/* Whether the query should retain an expanded target list*/
	bool expandTargetList;

	/* Whether or not the query requires a persisted cursor */
	bool requiresPersistentCursor;

	/* Whether or not the stage can handle a single batch cursor */
	bool isSingleRowResult;

	/* The namespace 'db.coll' associated with this query */
	const char *namespaceName;

	/* The current parameter count (Note: Increment this before use) */
	int currentParamCount;

	/* The current Mongo collection */
	MongoCollection *mongoCollection;

	/* the level of nested pipeline for stages that have nested pipelines ($facet/$lookup). */
	int nestedPipelineLevel;

	/* The number of nested levels (incremented by MigrateSubQuery) */
	int numNestedLevels;

	/* The database name associated with this request */
	text *databaseNameDatum;

	/* The collection name associated with this request (if applicable) */
	StringView collectionNameView;

	/* The sort specification that precedes it (if available).
	 * If the stage changes the sort order, this is reset.
	 * BSON_TYPE_EOD if not available.
	 */
	bson_value_t sortSpec;

	/* The path name of the collection, used for filtering of vector search
	 * it is set only when the filter of vector search is specified
	 */
	HTAB *requiredFilterPathNameHashSet;

	/* Whether or not the aggregation query allows direct shard delegation
	 * This allows queries to go directly against a local shard *iff* it's available.
	 * This can be done for base streaming queries. TODO: Investigate whether or not
	 * this can be extended to other types of queries.
	 */
	bool allowShardBaseTable;

	/*
	 * The variable spec expression that preceds it.
	 */
	Expr *variableSpec;

	/* Whether or not the query requires a tailable cursor */
	bool requiresTailableCursor;

	/*
	 * String indicating a standard ICU collation. An example string is "und-u-ks-level1-kc-true".
	 * We parse the Mongo collation spec and covert it to an ICU standard collation string.
	 * This string uniquely identify collation-based string comparison logic by postgres.
	 * See: https://www.postgresql.org/docs/current/collation.html
	 */
	const char collationString[MAX_ICU_COLLATION_LENGTH];

	/*
	 * Whether or not to apply the optimization transformation on the stages
	 */
	bool optimizePipelineStages;

	/* Whether or not it's a point read query */
	bool isPointReadQuery;

	/*Parent Stage Name*/
	ParentStageName parentStageName;
} AggregationPipelineBuildContext;


typedef struct
{
	/* The bson value of the pipeline spec */
	bson_value_t stageValue;

	/* Definition of internal handlers */
	AggregationStageDefinition *stageDefinition;
} AggregationStage;


/* Core Infra exports */
Query * MutateQueryWithPipeline(Query *query, List *aggregationStages,
								AggregationPipelineBuildContext *context);
Query * MigrateQueryToSubQuery(Query *parse, AggregationPipelineBuildContext *context);
Aggref * CreateMultiArgAggregate(Oid aggregateFunctionId, List *args, List *argTypes,
								 ParseState *parseState);
List * ExtractAggregationStages(const bson_value_t *pipelineValue,
								AggregationPipelineBuildContext *context);
Query * GenerateBaseTableQuery(text *databaseDatum, const StringView *collectionNameView,
							   pg_uuid_t *collectionUuid, const bson_value_t *indexHint,
							   AggregationPipelineBuildContext *context);
Query * GenerateBaseAgnosticQuery(text *databaseDatum,
								  AggregationPipelineBuildContext *context);
RangeTblEntry * MakeSubQueryRte(Query *subQuery, int stageNum, int pipelineDepth,
								const char *prefix, bool includeAllColumns);

bool CanInlineLookupPipeline(const bson_value_t *pipeline,
							 const StringView *lookupPath,
							 bool hasLet,
							 pgbson **inlinedPipeline,
							 pgbson **nonInlinedPipeline,
							 bool *pipelineIsValid);

void ParseCursorDocument(bson_iter_t *iterator, QueryData *queryData);
const char * CreateNamespaceName(text *databaseName,
								 const StringView *collectionName);

Query * HandleMatch(const bson_value_t *existingValue, Query *query,
					AggregationPipelineBuildContext *context);
Query * HandleSimpleProjectionStage(const bson_value_t *existingValue, Query *query,
									AggregationPipelineBuildContext *context,
									const char *stageName, Oid functionOid,
									Oid (*functionOidWithLet)(void),
									Oid (*functionOidWithLetAndCollation)(void));
Query * HandleGroup(const bson_value_t *existingValue, Query *query,
					AggregationPipelineBuildContext *context);

/* Sub-Pipeline related aggregation stages */
Query * HandleFacet(const bson_value_t *existingValue, Query *query,
					AggregationPipelineBuildContext *context);

Query * HandleLookup(const bson_value_t *existingValue, Query *query,
					 AggregationPipelineBuildContext *context);

Query * HandleLookupUnwind(const bson_value_t *existingValue, Query *query,
						   AggregationPipelineBuildContext *context);

Query * HandleGraphLookup(const bson_value_t *existingValue, Query *query,
						  AggregationPipelineBuildContext *context);

Query * HandleDocumentsStage(const bson_value_t *existingValue, Query *query,
							 AggregationPipelineBuildContext *context);

Query * HandleUnionWith(const bson_value_t *existingValue, Query *query,
						AggregationPipelineBuildContext *context);

Query * HandleInternalInhibitOptimization(const bson_value_t *existingValue, Query *query,
										  AggregationPipelineBuildContext *context);

Query * HandleInverseMatch(const bson_value_t *existingValue, Query *query,
						   AggregationPipelineBuildContext *context);

/* Metadata based query stages */
Query * HandleCollStats(const bson_value_t *existingValue, Query *query,
						AggregationPipelineBuildContext *context);
Query * HandleIndexStats(const bson_value_t *existingValue, Query *query,
						 AggregationPipelineBuildContext *context);
Query * HandleCurrentOp(const bson_value_t *existingValue, Query *query,
						AggregationPipelineBuildContext *context);
Query * HandleChangeStream(const bson_value_t *existingValue, Query *query,
						   AggregationPipelineBuildContext *context);

bool CanInlineLookupStageLookup(const bson_value_t *lookupStage,
								const StringView *lookupPath,
								bool hasLet);
bool CanInlineLookupWithUnwind(const bson_value_t *lookUpStageValue,
							   const bson_value_t *unwindStageValue,
							   bool *isPreserveNullAndEmptyArrays);

/* vector search related aggregation stages */
Query * HandleSearch(const bson_value_t *existingValue, Query *query,
					 AggregationPipelineBuildContext *context);

/* output to collection related aggregation pipeline */
Query * HandleMerge(const bson_value_t *existingValue, Query *query,
					AggregationPipelineBuildContext *context);
Query * HandleOut(const bson_value_t *existingValue, Query *query,
				  AggregationPipelineBuildContext *context);

/* Native vector search related aggregation stages */
Query * HandleNativeVectorSearch(const bson_value_t *existingValue, Query *query,
								 AggregationPipelineBuildContext *context);

/* Metadata based query generators */
Query * GenerateConfigDatabaseQuery(AggregationPipelineBuildContext *context);

bool IsPartitionByFieldsOnShardKey(const pgbson *partitionByFields,
								   const MongoCollection *collection);

Expr * GenerateMultiExpressionRepathExpression(List *repathArgs,
											   bool overrideArrayInProjection);
Stage GetAggregationStageAtPosition(const List *aggregationStages, int position);

/* Helper methods */

inline static Const *
MakeTextConst(const char *cstring, uint32_t stringLength)
{
	text *textValue = cstring_to_text_with_len(cstring, stringLength);
	return makeConst(TEXTOID, -1, DEFAULT_COLLATION_OID, -1, PointerGetDatum(textValue),
					 false,
					 false);
}


inline static Const *
MakeBsonConst(pgbson *pgbson)
{
	return makeConst(BsonTypeId(), -1, InvalidOid, -1, PointerGetDatum(pgbson), false,
					 false);
}


/*
 * Inline method for a bool const specifying the isNull attribute.
 */
inline static Node *
MakeBoolValueConst(bool value)
{
	bool isNull = false;
	return makeBoolConst(value, isNull);
}


inline static Const *
MakeFloat8Const(float8 floatValue)
{
	return makeConst(FLOAT8OID, -1, InvalidOid, sizeof(float8),
					 Float8GetDatum(floatValue), false, true);
}


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


#endif
