/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/query/bsonquery_compare.c
 *
 * Implementation of the bsonquery type comparisons.
 *
 * The bsonquery type layout is the same as a bson so we just parse it as a pgbson to do the comparisons.
 *
 * Bson query comparisons are commutative and are used to do comparisons at the query level for planning, index push down and query specific optimizations.
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <fmgr.h>
#include <miscadmin.h>

#include "io/helio_bson_core.h"
#include "query/helio_bson_compare.h"
#include "utils/helio_errors.h"

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */

static int ComparePgbsonQuery(pgbson *leftBson, pgbson *rightBson,
							  bool *isComparisonValid);

/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */

PG_FUNCTION_INFO_V1(bsonquery_compare);
PG_FUNCTION_INFO_V1(bsonquery_equal);
PG_FUNCTION_INFO_V1(bsonquery_not_equal);
PG_FUNCTION_INFO_V1(bsonquery_gt);
PG_FUNCTION_INFO_V1(bsonquery_gte);
PG_FUNCTION_INFO_V1(bsonquery_lt);
PG_FUNCTION_INFO_V1(bsonquery_lte);

extern bool EnableCollation;

Datum
bsonquery_compare(PG_FUNCTION_ARGS)
{
	pgbson *leftBson = PG_GETARG_PGBSON_PACKED(0);
	pgbson *rightBson = PG_GETARG_PGBSON_PACKED(1);

	bool isComparisonValidIgnore;
	int compareResult = ComparePgbsonQuery(leftBson, rightBson, &isComparisonValidIgnore);

	PG_FREE_IF_COPY(leftBson, 0);
	PG_FREE_IF_COPY(rightBson, 1);
	PG_RETURN_INT32(compareResult);
}


Datum
bsonquery_equal(PG_FUNCTION_ARGS)
{
	pgbson *leftBson = PG_GETARG_PGBSON_PACKED(0);
	pgbson *rightBson = PG_GETARG_PGBSON_PACKED(1);

	bool isComparisonValid;
	int compareResult = ComparePgbsonQuery(leftBson, rightBson, &isComparisonValid);

	PG_FREE_IF_COPY(leftBson, 0);
	PG_FREE_IF_COPY(rightBson, 1);
	PG_RETURN_BOOL(isComparisonValid && compareResult == 0);
}


Datum
bsonquery_not_equal(PG_FUNCTION_ARGS)
{
	pgbson *leftBson = PG_GETARG_PGBSON_PACKED(0);
	pgbson *rightBson = PG_GETARG_PGBSON_PACKED(1);

	bool isComparisonValid;
	int compareResult = ComparePgbsonQuery(leftBson, rightBson, &isComparisonValid);

	PG_FREE_IF_COPY(leftBson, 0);
	PG_FREE_IF_COPY(rightBson, 1);
	PG_RETURN_BOOL(isComparisonValid && compareResult != 0);
}


Datum
bsonquery_gt(PG_FUNCTION_ARGS)
{
	pgbson *leftBson = PG_GETARG_PGBSON_PACKED(0);
	pgbson *rightBson = PG_GETARG_PGBSON_PACKED(1);

	bool isComparisonValid;
	int compareResult = ComparePgbsonQuery(leftBson, rightBson, &isComparisonValid);

	PG_FREE_IF_COPY(leftBson, 0);
	PG_FREE_IF_COPY(rightBson, 1);
	PG_RETURN_BOOL(isComparisonValid && compareResult > 0);
}


Datum
bsonquery_gte(PG_FUNCTION_ARGS)
{
	pgbson *leftBson = PG_GETARG_PGBSON_PACKED(0);
	pgbson *rightBson = PG_GETARG_PGBSON_PACKED(1);

	bool isComparisonValid;
	int compareResult = ComparePgbsonQuery(leftBson, rightBson, &isComparisonValid);

	PG_FREE_IF_COPY(leftBson, 0);
	PG_FREE_IF_COPY(rightBson, 1);
	PG_RETURN_BOOL(isComparisonValid && compareResult >= 0);
}


Datum
bsonquery_lt(PG_FUNCTION_ARGS)
{
	pgbson *leftBson = PG_GETARG_PGBSON_PACKED(0);
	pgbson *rightBson = PG_GETARG_PGBSON_PACKED(1);

	bool isComparisonValid;
	int compareResult = ComparePgbsonQuery(leftBson, rightBson, &isComparisonValid);

	PG_FREE_IF_COPY(leftBson, 0);
	PG_FREE_IF_COPY(rightBson, 1);
	PG_RETURN_BOOL(isComparisonValid && compareResult < 0);
}


Datum
bsonquery_lte(PG_FUNCTION_ARGS)
{
	pgbson *leftBson = PG_GETARG_PGBSON_PACKED(0);
	pgbson *rightBson = PG_GETARG_PGBSON_PACKED(1);

	bool isComparisonValid;
	int compareResult = ComparePgbsonQuery(leftBson, rightBson, &isComparisonValid);

	PG_FREE_IF_COPY(leftBson, 0);
	PG_FREE_IF_COPY(rightBson, 1);
	PG_RETURN_BOOL(isComparisonValid && compareResult <= 0);
}


inline static void
pg_attribute_noreturn()
ThrowUnexpectedFieldError(bool leftNext, bool rightNext)
{
	ereport(ERROR, (errcode(ERRCODE_HELIO_INTERNALERROR), errmsg(
						"Unexpected bsonquery %s value had more than one field.",
						leftNext ? "left" :
						"right")));
}

inline static void
CheckCollationType(const bson_value_t *bsonValue)
{
	if (bsonValue->value_type != BSON_TYPE_UTF8)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE),
						errmsg("Collation is of wrong type: '%s', expected: 'String'",
							   BsonTypeName(bsonValue->value_type)),
						errdetail_log(
							"Collation is of wrong type: '%s', expected: 'String'",
							BsonTypeName(bsonValue->value_type))));
	}
}


static int
ComparePgbsonQuery(pgbson *leftBson, pgbson *rightBson, bool *isComparisonValid)
{
	*isComparisonValid = false;
	if (PgbsonEquals(leftBson, rightBson))
	{
		*isComparisonValid = true;
		return 0;
	}

	/* bsonquery is expected to have only one field. */
	bson_iter_t leftIter;
	bson_iter_t rightIter;
	PgbsonInitIterator(leftBson, &leftIter);
	PgbsonInitIterator(rightBson, &rightIter);
	bool leftNext = bson_iter_next(&leftIter);
	bool rightNext = bson_iter_next(&rightIter);

	if (!leftNext && !rightNext)
	{
		/* they are both empty, so they match. */
		*isComparisonValid = true;
		return 0;
	}

	if (!leftNext || !rightNext)
	{
		return leftNext ? 1 : -1;
	}

	pgbsonelement leftElement;
	pgbsonelement rightElement;
	BsonIterToPgbsonElement(&leftIter, &leftElement);
	BsonIterToPgbsonElement(&rightIter, &rightElement);

	leftNext = bson_iter_next(&leftIter);
	rightNext = bson_iter_next(&rightIter);
	const char *collationStringIgnore = NULL;

	if (EnableCollation && (leftNext || rightNext))
	{
		if ((leftNext && strcmp(bson_iter_key(&leftIter), "collation") != 0) ||
			(rightNext && strcmp(bson_iter_key(&rightIter), "collation") != 0))
		{
			ThrowUnexpectedFieldError(leftNext, rightNext);
		}

		/* TODO (workitem: 3423607): Test for the following case when index pushdown is enabled for collation. */
		/* Index with a pfe { "a": { "gte": "aaa" }} and a query of { "a": { "gt": "bbb" }, "collation": "fr" }} */
		/* that query should not use that index. But a query of { "a": { "$gte": "bbb" }} should be able to use it. */
		if (leftNext && rightNext)
		{
			const bson_value_t *left = bson_iter_value(&leftIter);
			const bson_value_t *right = bson_iter_value(&rightIter);

			CheckCollationType(left);
			CheckCollationType(right);

			return CompareStrings(left->value.v_utf8.str, left->value.v_utf8.len,
								  right->value.v_utf8.str, right->value.v_utf8.len,
								  collationStringIgnore);
		}
		else
		{
			return leftNext ? 1 : -1;
		}
	}
	else if (leftNext || rightNext)
	{
		ThrowUnexpectedFieldError(leftNext, rightNext);
	}

	/* next compare field name. Field name needs to be collation insensitive, hence we make sure we perform
	 * collation agnostic comparison by passing a NULL collation string*/
	int cmp = CompareStrings(leftElement.path, leftElement.pathLength, rightElement.path,
							 rightElement.pathLength, collationStringIgnore);
	if (cmp != 0)
	{
		return cmp;
	}

	*isComparisonValid = true;
	bool ignoreIsComparisonValid;
	bson_type_t leftType = leftElement.bsonValue.value_type;
	bson_type_t rightType = rightElement.bsonValue.value_type;

	/* Special case, if we're comparing MINKEY & NULL
	 * MinKey is greater than null since Null happens to include
	 * 'undefined' which is less than Minkey ($exists: false too )
	 */
	if (leftType == BSON_TYPE_MINKEY && rightType == BSON_TYPE_NULL)
	{
		/* MinKey > Null */
		return 1;
	}
	else if (leftType == BSON_TYPE_NULL && rightType == BSON_TYPE_MINKEY)
	{
		/* Null < MinKey */
		return -1;
	}

	if (leftType == BSON_TYPE_MINKEY || leftType == BSON_TYPE_MAXKEY ||
		rightType == BSON_TYPE_MINKEY || rightType == BSON_TYPE_MAXKEY)
	{
		return CompareBsonValueAndType(&leftElement.bsonValue, &rightElement.bsonValue,
									   &ignoreIsComparisonValid);
	}

	cmp = CompareSortOrderType(leftType, rightType);
	if (cmp != 0)
	{
		*isComparisonValid = false;
		return cmp;
	}

	return CompareBsonValueAndType(&leftElement.bsonValue, &rightElement.bsonValue,
								   &ignoreIsComparisonValid);
}
