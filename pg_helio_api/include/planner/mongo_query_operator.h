/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/planner/mongo_query_operator.h
 *
 * Common declarations for query operators in Mongo.
 *
 *-------------------------------------------------------------------------
 */

#ifndef MONGO_QUERY_OPERATOR_H
#define MONGO_QUERY_OPERATOR_H

#include "io/helio_bson_core.h"
#include "opclass/helio_gin_common.h"
#include "utils/feature_counter.h"

/* Invalid Feature counter tag for query operators that are not defined */
#define INVALID_QUERY_OPERATOR_FEATURE_TYPE MAX_FEATURE_INDEX

/* type of Mongo query operator
 * Note for every entry here there must be an entry in the mongo_query_operators
 * array that matches the exact index.
 */
typedef enum MongoQueryOperatorType
{
	/* comparison */
	QUERY_OPERATOR_EQ = 0,
	QUERY_OPERATOR_GT,
	QUERY_OPERATOR_GTE,
	QUERY_OPERATOR_LT,
	QUERY_OPERATOR_LTE,
	QUERY_OPERATOR_NE,
	QUERY_OPERATOR_IN,
	QUERY_OPERATOR_NIN,
	QUERY_OPERATOR_ALL,

	/* logical */
	QUERY_OPERATOR_AND,
	QUERY_OPERATOR_OR,
	QUERY_OPERATOR_NOT,
	QUERY_OPERATOR_NOR,
	QUERY_OPERATOR_ALWAYS_TRUE,
	QUERY_OPERATOR_ALWAYS_FALSE,

	/* element */
	QUERY_OPERATOR_EXISTS,
	QUERY_OPERATOR_TYPE,
	QUERY_OPERATOR_SIZE,
	QUERY_OPERATOR_ELEMMATCH,

	/* evaluation */
	QUERY_OPERATOR_REGEX,
	QUERY_OPERATOR_MOD,
	QUERY_OPERATOR_TEXT,
	QUERY_OPERATOR_EXPR,

	/* bit-wise */
	QUERY_OPERATOR_BITS_ALL_CLEAR,
	QUERY_OPERATOR_BITS_ANY_CLEAR,
	QUERY_OPERATOR_BITS_ALL_SET,
	QUERY_OPERATOR_BITS_ANY_SET,

	/* Geospatial operators */
	QUERY_OPERATOR_WITHIN,
	QUERY_OPERATOR_GEOWITHIN,
	QUERY_OPERATOR_GEOINTERSECTS,
	QUERY_OPERATOR_NEAR,
	QUERY_OPERATOR_NEARSPHERE,

	/*
	 * This is different from geonear agg stage.
	 * This operator only appears in mongo jstests
	 * Behaviour is same as nearsphere
	 */
	QUERY_OPERATOR_GEONEAR,

	/* Negation operators */
	QUERY_OPERATOR_NOT_GT,
	QUERY_OPERATOR_NOT_GTE,
	QUERY_OPERATOR_NOT_LT,
	QUERY_OPERATOR_NOT_LTE,

	QUERY_OPERATOR_UNKNOWN
} MongoQueryOperatorType;

/*
 * The type of input for the MongoQueryOperator requested
 */
typedef enum
{
	/* Not a valid input bson type */
	MongoQueryOperatorInputType_Invalid = 0,

	/* Query Operator based on Bson */
	MongoQueryOperatorInputType_Bson,

	/* Query Operator based on Bson values */
	MongoQueryOperatorInputType_BsonValue,
} MongoQueryOperatorInputType;

typedef Oid (*OperatorOidLookupFunc)(void);

/*
 * MongoQueryOperator represents a Mongo query operator
 * Holds  information about the operator and that pertaining
 * to runtime query planning.
 */
typedef struct
{
	/* operator key (e.g. "$eq") */
	char *mongoOperatorName;

	/* operator type number */
	MongoQueryOperatorType operatorType;

	/* Whether the operator operates on bson or bsonquery */
	OperatorOidLookupFunc operandTypeOid;

	/* Function that queries the Oid of bson query function name
	 * (e.g. bson_dollar_eq(<bson>, <bson query>)) if applicable */
	OperatorOidLookupFunc postgresRuntimeFunctionOidLookup;

	/* Function that queries the Oid of the bson query operator name and
	 * for the runtime evaluation of the function (e.g. #>) if applicable.
	 */
	OperatorOidLookupFunc postgresRuntimeOperatorOidLookup;

	/* Function that queries the Oid of the bson function name for the index
	 * (e.g. bson_dollar_eq(<bson>, <bson>)) if applicable .
	 */
	OperatorOidLookupFunc postgresIndexFunctionOidLookup;

	/*
	 * Feature counter type to be used with the query operator
	 */
	FeatureType featureType;
} MongoQueryOperator;

/*
 * MongoIndexOperatorInfo holds information about the mongo operator
 * and the corresponding information for pushing down operators to the index
 */
typedef struct
{
	/* bson query operator name (e.g. "<bson> @= <bson query>") - if applicable */
	char *postgresOperatorName;

	/* The indexing strategy for this operator */
	BsonIndexStrategy indexStrategy;

	/* Whether the operator is in the internal schema */
	bool isApiInternalSchema;
} MongoIndexOperatorInfo;

const MongoQueryOperator * GetMongoQueryOperatorByMongoOpName(const char *key,
															  MongoQueryOperatorInputType
															  inputType);

const MongoQueryOperator * GetMongoQueryOperatorByPostgresFuncId(Oid functionId);
const MongoQueryOperator * GetMongoQueryOperatorByQueryOperatorType(MongoQueryOperatorType
																	operatorType,
																	MongoQueryOperatorInputType
																	inputType);
const MongoIndexOperatorInfo * GetMongoIndexOperatorInfoByPostgresFuncId(Oid
																		 functionId);
BsonIndexStrategy GetBsonStrategyForFuncId(Oid functionOid);
Oid GetMongoQueryOperatorOid(const MongoIndexOperatorInfo *mongoQueryOperator);
const MongoQueryOperator * GetMongoQueryOperatorFromExpr(Node *expr, List **args);
const MongoIndexOperatorInfo * GetMongoIndexQueryOperatorFromNode(Node *expr,
																  List **args);
const MongoIndexOperatorInfo * GetMongoIndexOperatorByPostgresOperatorId(Oid operatorId);

#endif
