/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/planner/mongo_query_operator.c
 *
 * Implementation and Definitions for Mongo Query Operator Handling
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <fmgr.h>
#include <catalog/pg_type.h>
#include <catalog/namespace.h>
#include <utils/lsyscache.h>
#include <nodes/makefuncs.h>

#include "planner/mongo_query_operator.h"
#include "metadata/metadata_cache.h"

/* --------------------------------------------------------- */
/* Top level declarations */
/* --------------------------------------------------------- */

static Oid InvalidQueryOperatorFuncOid(void);

/*
 * Wrapper that tracks the various types of information
 * about mongo query operators in different contexts
 * (index, runtime, expressions etc).
 */
typedef struct
{
	/*
	 * Basic and runtime information for operators
	 * and functions taking BSON as input
	 */
	MongoQueryOperator bsonQueryOperator;

	/*
	 * Basic and runtime information for operators
	 * and functions taking bson values (internal) as input
	 */
	MongoQueryOperator bsonValueQueryOperator;

	/*
	 * Index strategy and operator information for
	 * mongo operators.
	 */
	MongoIndexOperatorInfo indexQueryOperator;

	/*
	 * Whether or not this operator is valid for the public API contract
	 * internal operators will set this to false.
	 */
	bool isPublicOperator;
} MongoOperatorInfo;


static const MongoQueryOperator UnknownOperator = {
	NULL, QUERY_OPERATOR_UNKNOWN, NULL, NULL, NULL, NULL,
	INVALID_QUERY_OPERATOR_FEATURE_TYPE
};
static const MongoIndexOperatorInfo UnknownIndexOperator = {
	NULL, BSON_INDEX_STRATEGY_INVALID, false
};

/*
 * known Mongo query operators, should match order of values in MongoQueryOperatorType
 * CODESYNC: If you're updating operators here, please ensure to update any places that
 * reference this for explain().
 */
static const MongoOperatorInfo QueryOperators[] = {
	/* comparison */
	{
		{ "$eq", QUERY_OPERATOR_EQ, GetClusterBsonQueryTypeId,
		  BsonEqualMatchRuntimeFunctionId,
		  BsonEqualMatchRuntimeOperatorId, BsonEqualMatchIndexFunctionId,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "$eq", QUERY_OPERATOR_EQ, BsonTypeId, BsonValueEqualMatchFunctionId, NULL, NULL,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "@=", BSON_INDEX_STRATEGY_DOLLAR_EQUAL, false },
		true,
	},
	{
		{ "$gt", QUERY_OPERATOR_GT, GetClusterBsonQueryTypeId,
		  BsonGreaterThanMatchRuntimeFunctionId,
		  BsonGreaterThanMatchRuntimeOperatorId, BsonGreaterThanMatchIndexFunctionId,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "$gt", QUERY_OPERATOR_GT, BsonTypeId, BsonValueGreaterThanMatchFunctionId, NULL,
		  NULL,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "@>", BSON_INDEX_STRATEGY_DOLLAR_GREATER, false },
		true,
	},
	{
		{ "$gte", QUERY_OPERATOR_GTE, GetClusterBsonQueryTypeId,
		  BsonGreaterThanEqualMatchRuntimeFunctionId,
		  BsonGreaterThanEqualMatchRuntimeOperatorId,
		  BsonGreaterThanEqualMatchIndexFunctionId,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "$gte", QUERY_OPERATOR_GTE, BsonTypeId,
		  BsonValueGreaterThanEqualMatchFunctionId, NULL, NULL,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "@>=", BSON_INDEX_STRATEGY_DOLLAR_GREATER_EQUAL, false },
		true,
	},
	{
		{ "$lt", QUERY_OPERATOR_LT, GetClusterBsonQueryTypeId,
		  BsonLessThanMatchRuntimeFunctionId,
		  BsonLessThanMatchRuntimeOperatorId, BsonLessThanMatchIndexFunctionId,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "$lt", QUERY_OPERATOR_LT, BsonTypeId, BsonValueLessThanMatchFunctionId, NULL,
		  NULL,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "@<", BSON_INDEX_STRATEGY_DOLLAR_LESS, false },
		true,
	},
	{
		{ "$lte", QUERY_OPERATOR_LTE, GetClusterBsonQueryTypeId,
		  BsonLessThanEqualMatchRuntimeFunctionId,
		  BsonLessThanEqualMatchRuntimeOperatorId, BsonLessThanEqualMatchIndexFunctionId,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "$lte", QUERY_OPERATOR_LTE, BsonTypeId, BsonValueLessThanEqualMatchFunctionId,
		  NULL, NULL, INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "@<=", BSON_INDEX_STRATEGY_DOLLAR_LESS_EQUAL, false },
		true,
	},
	{
		{ "$ne", QUERY_OPERATOR_NE, BsonTypeId, BsonNotEqualMatchFunctionId, NULL,
		  BsonNotEqualMatchFunctionId,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "$ne", QUERY_OPERATOR_NE, BsonTypeId, BsonValueNotEqualMatchFunctionId, NULL,
		  BsonValueNotEqualMatchFunctionId,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "@!=", BSON_INDEX_STRATEGY_DOLLAR_NOT_EQUAL, false },
		true,
	},
	{
		{ "$in", QUERY_OPERATOR_IN, BsonTypeId, BsonInMatchFunctionId, NULL,
		  BsonInMatchFunctionId,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "$in", QUERY_OPERATOR_IN, BsonTypeId, BsonValueInMatchFunctionId, NULL,
		  BsonValueInMatchFunctionId,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "@*=", BSON_INDEX_STRATEGY_DOLLAR_IN, false },
		true,
	},
	{
		{ "$nin", QUERY_OPERATOR_NIN, BsonTypeId, BsonNinMatchFunctionId, NULL,
		  BsonNinMatchFunctionId,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "$nin", QUERY_OPERATOR_NIN, BsonTypeId, BsonValueNinMatchFunctionId, NULL,
		  BsonValueNinMatchFunctionId,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "@!*=", BSON_INDEX_STRATEGY_DOLLAR_NOT_IN, false },
		true,
	},
	{
		{ "$all", QUERY_OPERATOR_ALL, BsonTypeId, BsonAllMatchFunctionId, NULL,
		  BsonAllMatchFunctionId,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "$all", QUERY_OPERATOR_ALL, BsonTypeId, BsonValueAllMatchFunctionId, NULL,
		  BsonValueAllMatchFunctionId,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "@&=", BSON_INDEX_STRATEGY_DOLLAR_ALL, false },
		true,
	},

	/* logical */
	{
		{ "$and", QUERY_OPERATOR_AND, BsonTypeId, InvalidQueryOperatorFuncOid, NULL,
		  InvalidQueryOperatorFuncOid,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "$and", QUERY_OPERATOR_AND, BsonTypeId, InvalidQueryOperatorFuncOid, NULL,
		  InvalidQueryOperatorFuncOid,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ NULL, BSON_INDEX_STRATEGY_INVALID, false },
		true,
	},
	{
		{ "$or", QUERY_OPERATOR_OR, BsonTypeId, InvalidQueryOperatorFuncOid, NULL,
		  InvalidQueryOperatorFuncOid,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "$or", QUERY_OPERATOR_OR, BsonTypeId, InvalidQueryOperatorFuncOid, NULL,
		  InvalidQueryOperatorFuncOid,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ NULL, BSON_INDEX_STRATEGY_INVALID, false },
		true,
	},
	{
		{ "$not", QUERY_OPERATOR_NOT, BsonTypeId, InvalidQueryOperatorFuncOid, NULL,
		  InvalidQueryOperatorFuncOid,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "$not", QUERY_OPERATOR_NOT, BsonTypeId, InvalidQueryOperatorFuncOid, NULL,
		  InvalidQueryOperatorFuncOid,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ NULL, BSON_INDEX_STRATEGY_INVALID, false },
		true,
	},
	{
		{ "$nor", QUERY_OPERATOR_NOR, BsonTypeId, InvalidQueryOperatorFuncOid, NULL,
		  InvalidQueryOperatorFuncOid,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "$nor", QUERY_OPERATOR_NOR, BsonTypeId, InvalidQueryOperatorFuncOid, NULL,
		  InvalidQueryOperatorFuncOid,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ NULL, BSON_INDEX_STRATEGY_INVALID, false },
		true,
	},
	{
		{ "$alwaysTrue", QUERY_OPERATOR_ALWAYS_TRUE, BsonTypeId,
		  InvalidQueryOperatorFuncOid, NULL,
		  InvalidQueryOperatorFuncOid,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "$alwaysTrue", QUERY_OPERATOR_ALWAYS_TRUE, BsonTypeId,
		  InvalidQueryOperatorFuncOid, NULL,
		  InvalidQueryOperatorFuncOid,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ NULL, BSON_INDEX_STRATEGY_INVALID, false },
		true,
	},
	{
		{ "$alwaysFalse", QUERY_OPERATOR_ALWAYS_FALSE, BsonTypeId,
		  InvalidQueryOperatorFuncOid, NULL,
		  InvalidQueryOperatorFuncOid,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "$alwaysFalse", QUERY_OPERATOR_ALWAYS_FALSE, BsonTypeId,
		  InvalidQueryOperatorFuncOid, NULL,
		  InvalidQueryOperatorFuncOid,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ NULL, BSON_INDEX_STRATEGY_INVALID, false },
		true,
	},

	/* element */
	{
		{ "$exists", QUERY_OPERATOR_EXISTS, BsonTypeId, BsonExistsMatchFunctionId, NULL,
		  BsonExistsMatchFunctionId,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "$exists", QUERY_OPERATOR_EXISTS, BsonTypeId, BsonValueExistsMatchFunctionId,
		  NULL, BsonValueExistsMatchFunctionId, INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "@?", BSON_INDEX_STRATEGY_DOLLAR_EXISTS, false },
		true,
	},
	{
		{ "$type", QUERY_OPERATOR_TYPE, BsonTypeId, BsonTypeMatchFunctionId, NULL,
		  BsonTypeMatchFunctionId,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "$type", QUERY_OPERATOR_TYPE, BsonTypeId, BsonValueTypeMatchFunctionId, NULL,
		  BsonValueTypeMatchFunctionId,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "@#", BSON_INDEX_STRATEGY_DOLLAR_TYPE, false },
		true,
	},
	{
		{ "$size", QUERY_OPERATOR_SIZE, BsonTypeId, BsonSizeMatchFunctionId, NULL,
		  BsonSizeMatchFunctionId,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "$size", QUERY_OPERATOR_SIZE, BsonTypeId, BsonValueSizeMatchFunctionId, NULL,
		  BsonValueSizeMatchFunctionId,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "@@#", BSON_INDEX_STRATEGY_DOLLAR_SIZE, false },
		true,
	},
	{
		{ "$elemMatch", QUERY_OPERATOR_ELEMMATCH, BsonTypeId,
		  BsonElemMatchMatchFunctionId, NULL, BsonElemMatchMatchFunctionId,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "$elemMatch", QUERY_OPERATOR_ELEMMATCH, BsonTypeId,
		  BsonValueElemMatchMatchFunctionId,
		  NULL, BsonValueElemMatchMatchFunctionId, INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "@#?", BSON_INDEX_STRATEGY_DOLLAR_ELEMMATCH, false },
		true,
	},

	/* evaluation */
	{
		{ "$regex", QUERY_OPERATOR_REGEX, BsonTypeId, BsonRegexMatchFunctionId, NULL,
		  BsonRegexMatchFunctionId,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "$regex", QUERY_OPERATOR_REGEX, BsonTypeId, BsonValueRegexMatchFunctionId, NULL,
		  BsonValueRegexMatchFunctionId,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "@~", BSON_INDEX_STRATEGY_DOLLAR_REGEX, false },
		true,
	},
	{
		{ "$mod", QUERY_OPERATOR_MOD, BsonTypeId, BsonModMatchFunctionId, NULL,
		  BsonModMatchFunctionId,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "$mod", QUERY_OPERATOR_MOD, BsonTypeId, BsonValueModMatchFunctionId, NULL,
		  BsonValueModMatchFunctionId,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "@%", BSON_INDEX_STRATEGY_DOLLAR_MOD, false },
		true,
	},
	{
		{ "$text", QUERY_OPERATOR_TEXT, BsonTypeId, BsonTextFunctionId, NULL,
		  BsonTextFunctionId,
		  FEATURE_QUERY_OPERATOR_TEXT },
		{ "$text", QUERY_OPERATOR_TEXT, BsonTypeId, InvalidQueryOperatorFuncOid, NULL,
		  InvalidQueryOperatorFuncOid,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "@#%", BSON_INDEX_STRATEGY_DOLLAR_TEXT, false },
		true,
	},
	{
		{ "$expr", QUERY_OPERATOR_EXPR, BsonTypeId, BsonExprFunctionId, NULL,
		  BsonExprFunctionId,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "$expr", QUERY_OPERATOR_EXPR, BsonTypeId, InvalidQueryOperatorFuncOid, NULL,
		  InvalidQueryOperatorFuncOid,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ NULL, BSON_INDEX_STRATEGY_INVALID, false },
		true,
	},


	/* bitwise */
	{
		{ "$bitsAllClear", QUERY_OPERATOR_BITS_ALL_CLEAR, BsonTypeId,
		  BsonBitsAllClearFunctionId,
		  NULL, BsonBitsAllClearFunctionId, INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "$bitsAllClear", QUERY_OPERATOR_BITS_ALL_CLEAR,
		  BsonTypeId, BsonValueBitsAllClearFunctionId, NULL,
		  BsonValueBitsAllClearFunctionId,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "@!&", BSON_INDEX_STRATEGY_DOLLAR_BITS_ALL_CLEAR, false },
		true,
	},

	/* bitwise */
	{
		{ "$bitsAnyClear", QUERY_OPERATOR_BITS_ANY_CLEAR, BsonTypeId,
		  BsonBitsAnyClearFunctionId,
		  NULL, BsonBitsAnyClearFunctionId, INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "$bitsAnyClear", QUERY_OPERATOR_BITS_ANY_CLEAR,
		  BsonTypeId, BsonValueBitsAnyClearFunctionId, NULL,
		  BsonValueBitsAnyClearFunctionId,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "@!|", BSON_INDEX_STRATEGY_DOLLAR_BITS_ANY_CLEAR, false },
		true,
	},

	/* bitwise */
	{
		{ "$bitsAllSet", QUERY_OPERATOR_BITS_ALL_SET, BsonTypeId,
		  BsonBitsAllSetFunctionId, NULL, BsonBitsAllSetFunctionId,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "$bitsAllSet", QUERY_OPERATOR_BITS_ALL_SET,
		  BsonTypeId, BsonValueBitsAllSetFunctionId, NULL, BsonValueBitsAllSetFunctionId,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "@&", BSON_INDEX_STRATEGY_DOLLAR_BITS_ALL_SET, false },
		true,
	},

	/* bitwise */
	{
		{ "$bitsAnySet", QUERY_OPERATOR_BITS_ANY_SET, BsonTypeId,
		  BsonBitsAnySetFunctionId, NULL, BsonBitsAnySetFunctionId,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "$bitsAnySet", QUERY_OPERATOR_BITS_ANY_SET,
		  BsonTypeId, BsonValueBitsAnySetFunctionId, NULL, BsonValueBitsAnySetFunctionId,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "@|", BSON_INDEX_STRATEGY_DOLLAR_BITS_ANY_SET, false },
		true,
	},

	/*
	 * Geospatial query operators
	 *
	 * For these operators we have quals of this form to match to the sparse 2d index
	 *
	 * bson_validate(document, 'path') @|-| 'geowithin query'
	 *
	 * Note: $within is deprecated in 2.4 and is replaced with $geoWithin but because this is heavily used in jstests
	 * we are bound support this and it is just another name for $geoWithin for us.
	 */
	{
		{ "$within", QUERY_OPERATOR_WITHIN, BsonTypeId, BsonDollarGeowithinFunctionOid,
		  NULL, BsonDollarGeowithinFunctionOid, FEATURE_QUERY_OPERATOR_GEOWITHIN },
		{ "$within", QUERY_OPERATOR_WITHIN, BsonTypeId, InvalidQueryOperatorFuncOid,
		  NULL, InvalidQueryOperatorFuncOid, INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "@|-|", BSON_INDEX_STRATEGY_DOLLAR_GEOWITHIN, false },
		true,
	},

	{
		{ "$geoWithin", QUERY_OPERATOR_GEOWITHIN, BsonTypeId,
		  BsonDollarGeowithinFunctionOid, NULL, BsonDollarGeowithinFunctionOid,
		  FEATURE_QUERY_OPERATOR_GEOWITHIN },
		{ "$geoWithin", QUERY_OPERATOR_GEOWITHIN, BsonTypeId, InvalidQueryOperatorFuncOid,
		  NULL, InvalidQueryOperatorFuncOid, INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "@|-|", BSON_INDEX_STRATEGY_DOLLAR_GEOWITHIN, false },
		true,
	},

	{
		{ "$geoIntersects", QUERY_OPERATOR_GEOINTERSECTS,
		  BsonTypeId, BsonDollarGeoIntersectsFunctionOid,
		  NULL, BsonDollarGeoIntersectsFunctionOid,
		  FEATURE_QUERY_OPERATOR_GEOINTERSECTS },
		{ "$geoIntersects", QUERY_OPERATOR_GEOINTERSECTS, BsonTypeId,
		  InvalidQueryOperatorFuncOid,
		  NULL, InvalidQueryOperatorFuncOid, INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "@|#|", BSON_INDEX_STRATEGY_DOLLAR_GEOINTERSECTS, false },
		true,
	},

	/*
	 * $negator operators
	 */
	{
		{ "$ngt", QUERY_OPERATOR_NOT_GT, BsonTypeId, BsonNotGreaterThanFunctionId, NULL,
		  BsonNotGreaterThanFunctionId,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "$ngt", QUERY_OPERATOR_NOT_GT, BsonTypeId, InvalidQueryOperatorFuncOid, NULL,
		  InvalidQueryOperatorFuncOid,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "@!>", BSON_INDEX_STRATEGY_DOLLAR_NOT_GT, true },
		false,
	},
	{
		{ "$ngte", QUERY_OPERATOR_NOT_GTE, BsonTypeId, BsonNotGreaterThanEqualFunctionId,
		  NULL,
		  BsonNotGreaterThanEqualFunctionId,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "$ngte", QUERY_OPERATOR_NOT_GTE, BsonTypeId, InvalidQueryOperatorFuncOid, NULL,
		  InvalidQueryOperatorFuncOid,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "@!>=", BSON_INDEX_STRATEGY_DOLLAR_NOT_GTE, true },
		false,
	},
	{
		{ "$nlt", QUERY_OPERATOR_NOT_LT, BsonTypeId, BsonNotLessThanFunctionId, NULL,
		  BsonNotLessThanFunctionId,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "$nlt", QUERY_OPERATOR_NOT_LT, BsonTypeId, InvalidQueryOperatorFuncOid, NULL,
		  InvalidQueryOperatorFuncOid,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "@!<", BSON_INDEX_STRATEGY_DOLLAR_NOT_LT, true },
		false,
	},
	{
		{ "$nlte", QUERY_OPERATOR_NOT_LTE, BsonTypeId, BsonNotLessThanEqualFunctionId,
		  NULL,
		  BsonNotLessThanEqualFunctionId,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "$nlte", QUERY_OPERATOR_NOT_LTE, BsonTypeId, InvalidQueryOperatorFuncOid, NULL,
		  InvalidQueryOperatorFuncOid,
		  INVALID_QUERY_OPERATOR_FEATURE_TYPE },
		{ "@!<=", BSON_INDEX_STRATEGY_DOLLAR_NOT_LTE, true },
		false,
	}
};

static const int QueryOperatorSize = sizeof(QueryOperators) / sizeof(MongoOperatorInfo);


/*
 * Inline function that takes a MongoOperatorInfo and returns the appropriate
 * MongoQueryOperator based on the input (whether it's bson or bson_value)
 */
inline static const MongoQueryOperator *
GetQueryOperatorCore(const MongoOperatorInfo *info, MongoQueryOperatorInputType inputType)
{
	switch (inputType)
	{
		case MongoQueryOperatorInputType_Bson:
		{
			return &info->bsonQueryOperator;
		}

		case MongoQueryOperatorInputType_BsonValue:
		{
			return &info->bsonValueQueryOperator;
		}

		default:
		{
			ereport(ERROR, (errmsg("Invalid mongo operator input type %d", inputType)));
		}
	}
}


/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */


/* Given a function Oid, returns the BSON Gin index strategy for that function
 * If no function matches, returns the invalid strategy */
BsonIndexStrategy
GetBsonStrategyForFuncId(Oid functionOid)
{
	for (int operatorIndex = 0; operatorIndex < QueryOperatorSize; operatorIndex++)
	{
		if (QueryOperators[operatorIndex].bsonQueryOperator.
			postgresRuntimeFunctionOidLookup() == functionOid)
		{
			return QueryOperators[operatorIndex].indexQueryOperator.indexStrategy;
		}
	}

	return BSON_INDEX_STRATEGY_INVALID;
}


/*
 * GetMongoQueryOperatorOid returns oid of given MongoQueryOperator.
 *
 * Returns InvalidOid if it's an operator of type QUERY_OPERATOR_UNKNOWN or
 * cache lookup fails to find oid of the operator (e.g.: it doesn't exist).
 */
Oid
GetMongoQueryOperatorOid(const MongoIndexOperatorInfo *operator)
{
	if (operator->indexStrategy == BSON_INDEX_STRATEGY_INVALID)
	{
		return InvalidOid;
	}

	Oid rightHandSideOid = BsonTypeId();
	if (operator->indexStrategy == BSON_INDEX_STRATEGY_DOLLAR_TEXT)
	{
		rightHandSideOid = TSQUERYOID;
	}

	char *schemaName = operator->isApiInternalSchema ? "helio_api_internal" :
					   ApiCatalogSchemaName;
	List *qualifiedOperatorName = list_make2(makeString(schemaName),
											 makeString(operator->postgresOperatorName));
	return OpernameGetOprid(qualifiedOperatorName, BsonTypeId(), rightHandSideOid);
}


/*
 * Returns a MongoQueryOperator whose postgres function id is equal to given function id.
 *
 * If the operator is unknown, a MongoQueryOperator with operatorType
 * QUERY_OPERATOR_UNKNOWN is returned.
 */
const MongoQueryOperator *
GetMongoQueryOperatorByPostgresFuncId(Oid functionId)
{
	for (int operatorIndex = 0; operatorIndex < QueryOperatorSize; operatorIndex++)
	{
		const MongoOperatorInfo *operator = &(QueryOperators[operatorIndex]);
		if (operator->indexQueryOperator.postgresOperatorName &&
			functionId == operator->bsonQueryOperator.postgresRuntimeFunctionOidLookup())
		{
			return &operator->bsonQueryOperator;
		}
	}

	return &UnknownOperator;
}


const MongoIndexOperatorInfo *
GetMongoIndexOperatorByPostgresOperatorId(Oid operatorId)
{
	Oid functionId = get_opcode(operatorId);
	for (int operatorIndex = 0; operatorIndex < QueryOperatorSize; operatorIndex++)
	{
		const MongoOperatorInfo *operator = &(QueryOperators[operatorIndex]);
		if (operator->indexQueryOperator.postgresOperatorName &&
			(functionId ==
			 operator->bsonQueryOperator.postgresRuntimeFunctionOidLookup() ||
			 functionId == operator->bsonQueryOperator.postgresIndexFunctionOidLookup()))
		{
			return &operator->indexQueryOperator;
		}
	}

	return &UnknownIndexOperator;
}


/*
 * Returns a MongoIndexOperatorInfo whose postgres function id is equal to given function id.
 *
 * If the operator is unknown, a MongoIndexOperatorInfo with operatorType
 * QUERY_OPERATOR_UNKNOWN is returned.
 */
const MongoIndexOperatorInfo *
GetMongoIndexOperatorInfoByPostgresFuncId(Oid functionId)
{
	for (int operatorIndex = 0; operatorIndex < QueryOperatorSize; operatorIndex++)
	{
		const MongoOperatorInfo *operator = &(QueryOperators[operatorIndex]);
		if (operator->indexQueryOperator.postgresOperatorName &&
			(functionId ==
			 operator->bsonQueryOperator.postgresRuntimeFunctionOidLookup() ||
			 functionId == operator->bsonQueryOperator.postgresIndexFunctionOidLookup()))
		{
			return &operator->indexQueryOperator;
		}
	}

	return &UnknownIndexOperator;
}


/*
 * Returns a MongoQueryOperator based on the query operator
 *
 * If the operator is unknown, a MongoQueryOperator with operatorType
 * QUERY_OPERATOR_UNKNOWN is returned.
 */
const MongoQueryOperator *
GetMongoQueryOperatorByQueryOperatorType(MongoQueryOperatorType type,
										 MongoQueryOperatorInputType inputType)
{
	return GetQueryOperatorCore(&QueryOperators[type], inputType);
}


/*
 * GetMongoQueryOperatorByMongoOpName converts a path to a MongoQueryOperator.
 * If the operator is unknown, a MongoQueryOperator with operatorType
 * QUERY_OPERATOR_UNKNOWN is returned.
 */
const MongoQueryOperator *
GetMongoQueryOperatorByMongoOpName(const char *key, MongoQueryOperatorInputType inputType)
{
	for (int operatorIndex = 0; operatorIndex < QueryOperatorSize; operatorIndex++)
	{
		const MongoOperatorInfo *operator = &(QueryOperators[operatorIndex]);

		if (strcmp(key, operator->bsonQueryOperator.mongoOperatorName) == 0 &&
			operator->isPublicOperator)
		{
			return GetQueryOperatorCore(operator, inputType);
		}
	}

	return &UnknownOperator;
}


inline static Oid
GetFuncIdFromExpr(Node *expr, List **args)
{
	if (IsA(expr, FuncExpr))
	{
		FuncExpr *function = (FuncExpr *) expr;
		*args = function->args;
		return function->funcid;
	}
	else if (IsA(expr, OpExpr))
	{
		OpExpr *opExpr = (OpExpr *) expr;
		*args = opExpr->args;
		return opExpr->opfuncid;
	}

	*args = NIL;
	return InvalidOid;
}


/*
 * Retrieves the MongoQueryOperator from known Nodes.
 * Also returns the args, and the FuncId associated with the Nodes.
 */
const MongoQueryOperator *
GetMongoQueryOperatorFromExpr(Node *expr, List **args)
{
	Oid funcId = GetFuncIdFromExpr(expr, args);
	if (funcId != InvalidOid)
	{
		return GetMongoQueryOperatorByPostgresFuncId(funcId);
	}

	*args = NIL;
	return &UnknownOperator;
}


/*
 * Retrieves the MongoIndexOperatorInfo from known Nodes.
 * Also returns the args, and the FuncId associated with the Nodes.
 */
const MongoIndexOperatorInfo *
GetMongoIndexQueryOperatorFromNode(Node *expr, List **args)
{
	Oid funcId = GetFuncIdFromExpr(expr, args);
	if (funcId != InvalidOid)
	{
		return GetMongoIndexOperatorInfoByPostgresFuncId(funcId);
	}

	*args = NIL;
	return &UnknownIndexOperator;
}


/*
 * Dummy function that returns InvalidOid for operators that do not map
 * to a valid bson_dollar_<op> function.
 */
static Oid
InvalidQueryOperatorFuncOid()
{
	return InvalidOid;
}
