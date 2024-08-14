/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bson_query.h
 *
 * Common declarations for BSON query functions.
 *
 *-------------------------------------------------------------------------
 */

#ifndef QUERY_OPERATOR_H
#define QUERY_OPERATOR_H

#include <nodes/params.h>
#include <nodes/parsenodes.h>

#include "metadata/collection.h"
#include "io/helio_bson_core.h"
#include "opclass/helio_gin_common.h"
#include "planner/mongo_query_operator.h"

/* BsonElemMatchContext is passed down while expanding BSON query expressions to handle $elemMatch operator */
typedef struct BsonElemMatchContext
{
	/* Depth in case of nested elemMatch while traversing the query */
	int currentDepth;

	/* Max Depth in case of nested elemMatch */
	int maxDepth;

	/* List of quals to be applied as outermost quals in elemMatch query tree
	 * Example: { a: {$elemMatch: {d: {$elemMatch: {e: {$gte: 1}}}, b: {$elemMatch: { $gte: 80, $lt: 85 }}}} }
	 * The outermost quals will be {"a.d.e" : {"$gte" : 1}, "a.b" : {"$gte" : 80}, "a.b" : {"$lt" : 85} }
	 */
	List *outerMostExpressionQuals;

	/* It is set when bson query expression results in SUBLINK creation in query tree */
	bool hasSublink;

	/* It is set when bson query expression contains number field inside $elemMatch
	 * Example: {"a" : { "$elemMatch": { "0" : 100 } } }
	 */
	bool hasNumberField;

	/* It is set when there is expression inside $elemMatch for $ne, $not */
	bool hasNegationOp;

	/* isCmpOpInsideElemMatch : it will be set to true when there is a cmp operator inside $elemMatch i.e. for calling CreateOpExprFromOperatorDocIterator.
	 * {path : {$elemMatch : {$op : {...}}}}. we are interested in $op being another $elemMatch for array of array scenario.
	 */
	bool isCmpOpInsideElemMatch;
} BsonElemMatchContext;

/*
 * BsonQueryOperatorContext is passed down while expanding BSON query expressions
 * of the form <document> @@ <query>
 */
typedef struct BsonQueryOperatorContext
{
	Expr *documentExpr;

	/* The input variable type for functions. Note that this should
	 *  match the data type expectations of documentExpr above.
	 *  If documentExpr is bson -> this is bson (otherwise it's bsonValue)
	 */
	MongoQueryOperatorInputType inputType;

	/*
	 * Whether or not to treat a simple $or as $in.
	 */
	bool simplifyOperators;

	/*
	 * Coerce expressions to runtime OpExpr if available
	 */
	bool coerceOperatorExprIfApplicable;

	/* Required path names for vector search
	 * it is set only when the filter of vector search is specified
	 */
	HTAB *requiredFilterPathNameHashSet;

	/* List of sort clauses, if any query operator adds them
	 * e.g. $near, $nearSphere etc, will be NULL for most of
	 * the query operators.
	 *
	 * Please note that the `ressortgroupref` is needed to be updated
	 * based on the overall query structure later
	 */
	List *sortClauses;

	/* List of Target entries for these sort clauses
	 *
	 * Please note that the `resno` is needed to be updated
	 * based on the overall query structure later
	 */
	List *targetEntries;

	/* Any let variable context for this query operator */
	Expr *variableContext;
} BsonQueryOperatorContext;

Var * MakeSimpleDocumentVar(void);
Node * ReplaceBsonQueryOperators(Query *node, ParamListInfo boundParams);
void ValidateQueryDocument(pgbson *queryDocument);
bool QueryDocumentsAreEquivalent(const pgbson *leftQueryDocument,
								 const pgbson *rightQueryDocument);
List * CreateQualsFromQueryDocIterator(bson_iter_t *queryDocIterator,
									   BsonQueryOperatorContext *context);
Node * EvaluateBoundParameters(Node *expression, ParamListInfo boundParams);

List * CreateQualsForBsonValueTopLevelQuery(const pgbson *query);
Expr * CreateQualForBsonValueExpression(const bson_value_t *expression);
Expr * CreateQualForBsonValueArrayExpression(const bson_value_t *expression);
Expr * CreateQualForBsonExpression(const bson_value_t *expression, const char *queryPath);

Expr * CreateNonShardedShardKeyValueFilter(int collectionVarNo, const
										   MongoCollection *collection);
Expr * CreateShardKeyFiltersForQuery(const bson_value_t *queryDocument, pgbson *shardKey,
									 uint64_t collectionId,
									 Index collectionVarno);
Expr * CreateIdFilterForQuery(List *existingQuals,
							  Index collectionVarno);

bool ValidateOrderbyExpressionAndGetIsAscending(pgbson *orderby);

/* Checks the validity of value for $in and $nin ops */
bool IsValidBsonDocumentForDollarInOrNinOp(const bson_value_t *value);
void UpdateQueryOperatorContextSortList(Query *query, List *sortClauses,
										List *targetEntries);

#endif
