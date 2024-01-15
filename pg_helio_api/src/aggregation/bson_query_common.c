/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/aggregation/bson_query_common.c
 *
 * Implementation of the common BSON query utility.
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>

#include "io/bson_core.h"
#include "aggregation/bson_query_common.h"


DollarRangeParams *
ParseQueryDollarRange(pgbson *rangeFilter)
{
	pgbsonelement filterElement;
	PgbsonToSinglePgbsonElement(rangeFilter, &filterElement);


	DollarRangeParams *rangeParams = palloc0(sizeof(DollarRangeParams));

	bson_iter_t rangeIter;
	BsonValueInitIterator(&filterElement.bsonValue, &rangeIter);
	while (bson_iter_next(&rangeIter))
	{
		const char *key = bson_iter_key(&rangeIter);
		if (strcmp(key, "min") == 0)
		{
			rangeParams->minValue = *bson_iter_value(&rangeIter);
		}
		else if (strcmp(key, "max") == 0)
		{
			rangeParams->maxValue = *bson_iter_value(&rangeIter);
		}
		else if (strcmp(key, "minInclusive") == 0)
		{
			rangeParams->isMinInclusive = bson_iter_bool(&rangeIter);
		}
		else if (strcmp(key, "maxInclusive") == 0)
		{
			rangeParams->isMaxInclusive = bson_iter_bool(&rangeIter);
		}
		else
		{
			ereport(ERROR, (errmsg("Unsupported range predicate: %s", key), errhint(
								"Unsupported range predicate: %s", key)));
		}
	}

	return rangeParams;
}
