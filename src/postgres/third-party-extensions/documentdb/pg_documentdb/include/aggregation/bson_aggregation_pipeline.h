/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/aggregation/bson_aggregation_pipeline.h
 *
 * Exports for the bson_aggregation_pipeline definition
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_AGGREGATION_PIPELINE_H
#define BSON_AGGREGATION_PIPELINE_H

#include <nodes/params.h>

#include "operators/bson_expression.h"
#include "utils/documentdb_errors.h"

typedef enum QueryCursorType
{
	QueryCursorType_Unspecified = 0,

	/*
	 * Whether or not the query can be done as a streamable
	 * query.
	 */
	QueryCursorType_Streamable = 1,

	/*
	 * Indicates whether this is processed as a single batch query.
	 */
	QueryCursorType_SingleBatch,

	/*
	 * The cursor plan is a point read.
	 */
	QueryCursorType_PointRead,

	/*
	 * Whether or not the query can be done as a tailable
	 * query.
	 */
	QueryCursorType_Tailable,

	/*
	 * By default all queries are persistent cursors.
	 */
	QueryCursorType_Persistent,
} QueryCursorType;

/*
 * Tracks the overall query spec data
 * that can be extracted from the query.
 * Used in cursor management to page and
 * decide what kind of cursor to use for the outer
 * request.
 */
typedef struct
{
	/*
	 * The parameter number used for the cursor
	 * continuation (if it is a streaming cursor)
	 */
	int cursorStateParamNumber;

	/* Optional cursor state const param */
	pgbson *cursorStateConst;

	/*
	 * The namespaceName associated with the query.
	 */
	const char *namespaceName;

	QueryCursorType cursorKind;

	/*
	 * The requested batchSize in the query request.
	 */
	int32_t batchSize;

	/*
	 * The time system variables ($$NOW, $$CLUSTER_TIME).
	 */
	TimeSystemVariables timeSystemVariables;
} QueryData;


Query * GenerateFindQuery(text *database, pgbson *findSpec, QueryData *queryData,
						  bool addCursorParams, bool setStatementTimeout);
Query * GenerateGetMoreQuery(text *database, pgbson *getMoreSpec,
							 pgbson *continuationSpec,
							 QueryData *queryData, bool addCursorParams, bool
							 setStatementTimeout);
Query * BuildAggregationCursorGetMoreQuery(text *database, pgbson *getMoreSpec,
										   pgbson *continuationSpec);
Query * GenerateCountQuery(text *database, pgbson *countSpec, bool setStatementTimeout);
Query * GenerateDistinctQuery(text *database, pgbson *distinctSpec, bool
							  setStatementTimeout);
Query * GenerateListCollectionsQuery(text *database, pgbson *listCollectionsSpec,
									 QueryData *queryData,
									 bool addCursorParams, bool setStatementTimeout);
Query * GenerateListIndexesQuery(text *database, pgbson *listIndexesSpec,
								 QueryData *queryData,
								 bool addCursorParams, bool setStatementTimeout);

Query * GenerateAggregationQuery(text *database, pgbson *aggregationSpec,
								 QueryData *queryData, bool addCursorParams,
								 bool setStatementTimeout);

int64_t ParseGetMore(text **databaseName, pgbson *getMoreSpec, QueryData *queryData, bool
					 setStatementTimeout);

void ValidateAggregationPipeline(text *databaseDatum, const StringView *baseCollection,
								 const bson_value_t *pipelineValue);

void LookupExtractCollectionAndPipeline(const bson_value_t *lookupValue,
										StringView *collection, bson_value_t *pipeline);

void GraphLookupExtractCollection(const bson_value_t *lookupValue,
								  StringView *collection);
void ParseUnionWith(const bson_value_t *existingValue, StringView *collectionFrom,
					bson_value_t *pipeline);
void ParseInputDocumentForTopAndBottom(const bson_value_t *inputDocument,
									   bson_value_t *input,
									   bson_value_t *elementsToFetch,
									   bson_value_t *sortSpec, const char *opName);
void ParseInputDocumentForMedianAndPercentile(const bson_value_t *inputDocument,
											  bson_value_t *input, bson_value_t *p,
											  bson_value_t *method, bool isMedianOp);
void ValidateElementForNGroupAccumulators(bson_value_t *elementsToFetch, const
										  char *opName);
void ParseInputForNGroupAccumulators(const bson_value_t *inputDocument,
									 bson_value_t *input,
									 bson_value_t *elementsToFetch,
									 const char *opName);

extern int DefaultCursorFirstPageBatchSize;

/* Generates a base QueryData used for the first page */
inline static QueryData
GenerateFirstPageQueryData(void)
{
	QueryData queryData = { 0 };
	queryData.batchSize = DefaultCursorFirstPageBatchSize;
	return queryData;
}


#endif
