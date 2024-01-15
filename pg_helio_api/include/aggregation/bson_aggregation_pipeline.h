/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/planner/bson_aggregation_pipeline.h
 *
 * Exports for the bson_aggregation_pipeline definition
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_AGGREGATION_PIPELINE_H
#define BSON_AGGREGATION_PIPELINE_H


#include "utils/mongo_errors.h"

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

	/*
	 * The namespaceName associated with the query.
	 */
	const char *namespaceName;

	/*
	 * Whether or not the query can be done as a streamable
	 * query.
	 */
	bool isStreamableCursor;

	/*
	 * Whether or not it's a single batch query.
	 */
	bool isSingleBatch;

	/*
	 * The requested batchSize in the query request.
	 */
	int32_t batchSize;
} QueryData;


Query * GenerateFindQuery(Datum database, pgbson *findSpec, QueryData *queryData, bool
						  addCursorParams);
Query * GenerateCountQuery(Datum database, pgbson *countSpec);
Query * GenerateDistinctQuery(Datum database, pgbson *distinctSpec);
Query * GenerateListCollectionsQuery(Datum database, pgbson *listCollectionsSpec,
									 QueryData *queryData,
									 bool addCursorParams);
Query * GenerateListIndexesQuery(Datum database, pgbson *listIndexesSpec,
								 QueryData *queryData,
								 bool addCursorParams);

Query * GenerateAggregationQuery(Datum database, pgbson *aggregationSpec,
								 QueryData *queryData, bool addCursorParams);

Query * ExpandAggregationFunction(Query *node, ParamListInfo boundParams);

int64_t ParseGetMore(text *databaseName, pgbson *getMoreSpec, QueryData *queryData);

void ValidateAggregationPipeline(Datum databaseDatum, const StringView *baseCollection,
								 const bson_value_t *pipelineValue);

void LookupExtractCollectionAndPipeline(const bson_value_t *lookupValue,
										StringView *collection, bson_value_t *pipeline);

#endif
