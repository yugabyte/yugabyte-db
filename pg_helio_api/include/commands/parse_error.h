/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/commands/parse_error.h
 *
 * Errors thrown for common parse errors.
 *
 *-------------------------------------------------------------------------
 */
#include <postgres.h>

#include "io/helio_bson_core.h"
#include "utils/mongo_errors.h"


#ifndef PARSE_ERROR_H
#define PARSE_ERROR_H


static inline void
ThrowTopLevelTypeMismatchError(const char *fieldName, const char *fieldTypeName,
							   const char *expectedTypeName)
{
	ereport(ERROR, (errcode(MongoTypeMismatch),
					errmsg("BSON field '%s' is the wrong type '%s', "
						   "expected type '%s'",
						   fieldName, fieldTypeName, expectedTypeName),
					errhint("BSON field '%s' is the wrong type '%s', "
							"expected type '%s'",
							fieldName, fieldTypeName, expectedTypeName)));
}


/*
 * Throw an error if type of the value that given iterator holds doesn't
 * match the expected one.
 */
static inline void
EnsureTopLevelFieldType(const char *fieldName, const bson_iter_t *iter,
						bson_type_t expectedType)
{
	bson_type_t fieldType = bson_iter_type(iter);
	if (fieldType != expectedType)
	{
		ThrowTopLevelTypeMismatchError(fieldName, BsonTypeName(fieldType),
									   BsonTypeName(expectedType));
	}
}


/*
 * Variant of the above with values.
 */
static inline void
EnsureTopLevelFieldValueType(const char *fieldName, const bson_value_t *value,
							 bson_type_t expectedType)
{
	if (value->value_type != expectedType)
	{
		ThrowTopLevelTypeMismatchError(fieldName, BsonTypeName(value->value_type),
									   BsonTypeName(expectedType));
	}
}


/*
 * Similar to EnsureTopLevelFieldType, but null value is also ok even if
 * expectedType is not "null".
 *
 * That means;
 *  - Returns true if type of the value that it holds matches the expected
 *    one.
 *  - Otherwise, returns false if iterator holds null value else throws an
 *    error.
 *
 * Mostly useful when given field being set to null implies using the default
 * setting for that spec option.
 */
static inline bool
EnsureTopLevelFieldTypeNullOk(const char *fieldName, const bson_iter_t *iter,
							  bson_type_t expectedType)
{
	if (BSON_ITER_HOLDS_NULL(iter) && expectedType != BSON_TYPE_NULL)
	{
		return false;
	}

	EnsureTopLevelFieldType(fieldName, iter, expectedType);
	return true;
}


/*
 * Similar to EnsureTopLevelFieldType, but null value is also ok even if
 * expectedType is not "null" or "undefined".
 *
 * That means;
 *  - Returns true if type of the value that it holds matches the expected
 *    one.
 *  - Otherwise, returns false if iterator holds null value else throws an
 *    error.
 *
 * Mostly useful when given field being set to null implies using the default
 * setting for that spec option.
 */
static inline bool
EnsureTopLevelFieldTypeNullOkUndefinedOK(const char *fieldName, const bson_iter_t *iter,
										 bson_type_t expectedType)
{
	if ((BSON_ITER_HOLDS_NULL(iter) && expectedType != BSON_TYPE_NULL) ||
		(BSON_ITER_HOLDS_UNDEFINED(iter) && expectedType != BSON_TYPE_UNDEFINED))
	{
		return false;
	}

	EnsureTopLevelFieldType(fieldName, iter, expectedType);
	return true;
}


/*
 * Throw an error if type of the value that given iterator holds cannot be
 * interpreted as a boolean.
 */
static inline void
EnsureTopLevelFieldIsBooleanLike(const char *fieldName, const bson_iter_t *iter)
{
	if (!BsonTypeIsNumberOrBool(bson_iter_type(iter)))
	{
		ereport(ERROR, (errcode(MongoTypeMismatch),
						errmsg("BSON field '%s' is the wrong type '%s', "
							   "expected types '[bool, long, int, decimal, "
							   "double']",
							   fieldName, BsonIterTypeName(iter)),
						errhint("BSON field '%s' is the wrong type '%s', "
								"expected types '[bool, long, int, decimal, "
								"double']",
								fieldName, BsonIterTypeName(iter))));
	}
}


/*
 * Similar to EnsureTopLevelFieldIsBooleanLike, but null value is also ok.
 *
 * That means;
 *  - Returns true if type of the value that it holds can be interpreted as
 *    a boolean.
 *  - Otherwise, returns false if iterator holds null value else throws an
 *    error.
 *
 * Mostly useful when given field being set to null implies using the default
 * setting for that spec option.
 */
static inline bool
EnsureTopLevelFieldIsBooleanLikeNullOk(const char *fieldName, const bson_iter_t *iter)
{
	if (BSON_ITER_HOLDS_NULL(iter))
	{
		return false;
	}

	EnsureTopLevelFieldIsBooleanLike(fieldName, iter);
	return true;
}


static inline void
ThrowTopLevelMissingFieldError(const char *fieldName)
{
	ereport(ERROR, (errcode(MongoBadValue),
					errmsg("BSON field '%s' is missing but a required field",
						   fieldName)));
}


#endif
