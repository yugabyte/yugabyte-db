/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/index/pgmongo_bson_text_gin.h
 *
 * Specific exports that handle indexing for $text operators
 *
 *-------------------------------------------------------------------------
 */

#ifndef PGMONGO_BSON_TEXT_GIN_H
#define PGMONGO_BSON_TEXT_GIN_H

#include "tsearch/ts_type.h"
#include "nodes/primnodes.h"


/*
 * Type that holds data needed for
 * managing basic Text indexing query
 * related data.
 */
typedef struct QueryTextIndexData
{
	/*
	 * The index options of the text index
	 * that this query will be pushed down to
	 */
	bytea *indexOptions;

	/*
	 * The TSQuery Datum that is applied against
	 * this query.
	 */
	Datum query;
} QueryTextIndexData;

Datum BsonTextGenerateTSQuery(const bson_value_t *queryValue, bytea *indexOptions);
void BsonValidateTextQuery(const bson_value_t *queryValue);
Expr * GetFuncExprForTextWithIndexOptions(List *args, bytea *indexOptions,
										  bool doRuntimeScan,
										  QueryTextIndexData *textIndexData);


/* This is currently a hack to get $meta working. Ideally
 * this will be hooked up via the same framework as '$let'
 */
double EvaluateMetaTextScore(pgbson *document);
bool TryCheckMetaScoreOrderBy(const bson_value_t *value);

#endif
