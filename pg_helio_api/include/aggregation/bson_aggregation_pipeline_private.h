/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/planner/bson_aggregation_pipeline_private.h
 *
 * Private helpers for the bson_aggregation_pipeline definition
 *
 *-------------------------------------------------------------------------
 */


#include <nodes/parsenodes.h>
#include <nodes/makefuncs.h>

#include "metadata/collection.h"
#include <utils/version_utils.h>

#ifndef BSON_AGGREGATION_PIPELINE_PRIVATE_H
#define BSON_AGGREGATION_PIPELINE_PRIVATE_H


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

	/* Whether or not the query is a streamable cursor */
	bool requiresPersistentCursor;

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

	/* The database associated with this request */
	Datum databaseNameDatum;

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
	pgbson *variableSpec;
} AggregationPipelineBuildContext;


/* Core Infra exports */
Query * MutateQueryWithPipeline(Query *query, const bson_value_t *pipelineValue,
								AggregationPipelineBuildContext *context);
Query * MigrateQueryToSubQuery(Query *parse, AggregationPipelineBuildContext *context);
Aggref * CreateMultiArgAggregate(Oid aggregateFunctionId, List *args, List *argTypes,
								 ParseState *parseState);
Query * GenerateBaseTableQuery(Datum databaseDatum, const StringView *collectionNameView,
							   pg_uuid_t *collectionUuid,
							   AggregationPipelineBuildContext *context);
Query * GenerateBaseAgnosticQuery(Datum databaseDatum,
								  AggregationPipelineBuildContext *context);
RangeTblEntry * MakeSubQueryRte(Query *subQuery, int stageNum, int pipelineDepth,
								const char *prefix, bool includeAllColumns);

bool CanInlineLookupPipeline(const bson_value_t *pipeline,
							 const StringView *lookupPath);

void ParseCursorDocument(bson_iter_t *iterator, QueryData *queryData);
const char * CreateNamespaceName(text *databaseName,
								 const StringView *collectionName);

Query * HandleMatch(const bson_value_t *existingValue, Query *query,
					AggregationPipelineBuildContext *context);
Query * HandleSimpleProjectionStage(const bson_value_t *existingValue, Query *query,
									AggregationPipelineBuildContext *context,
									const char *stageName, Oid functionOid,
									Oid (*functionOidWithLet)(void));
Query * HandleGroup(const bson_value_t *existingValue, Query *query,
					AggregationPipelineBuildContext *context);

/* Sub-Pipeline related aggregation stages */
Query * HandleFacet(const bson_value_t *existingValue, Query *query,
					AggregationPipelineBuildContext *context);

Query * HandleLookup(const bson_value_t *existingValue, Query *query,
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

bool CanInlineLookupStageLookup(const bson_value_t *lookupStage,
								const StringView *lookupPath);

/* vector search related aggregation stages */
Query * HandleSearch(const bson_value_t *existingValue, Query *query,
					 AggregationPipelineBuildContext *context);

/* output to collection related aggregation pipeline */
Query * HandleMerge(const bson_value_t *existingValue, Query *query,
					AggregationPipelineBuildContext *context);

/* atlas vector search related aggregation stages */
Query * HandleMongoNativeVectorSearch(const bson_value_t *existingValue, Query *query,
									  AggregationPipelineBuildContext *context);

/* Metadata based query generators */
Query * GenerateConfigDatabaseQuery(AggregationPipelineBuildContext *context);

/* Helper methods */

inline static Const *
MakeTextConst(const char *cstring, uint32_t stringLength)
{
	text *textValue = cstring_to_text_with_len(cstring, stringLength);
	return makeConst(TEXTOID, -1, InvalidOid, -1, PointerGetDatum(textValue), false,
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


inline static Oid
GetMergeDocumentsFunctionOid(void)
{
	if (IsClusterVersionAtleastThis(1, 18, 0) ||
		IsClusterVersionEqualToAndAtLeastPatch(1, 17, 1) ||
		IsClusterVersionEqualToAndAtLeastPatch(1, 16, 1))
	{
		return BsonDollaMergeDocumentsFunctionOid();
	}
	else
	{
		return BsonDollarAddFieldsFunctionOid();
	}
}


#endif
