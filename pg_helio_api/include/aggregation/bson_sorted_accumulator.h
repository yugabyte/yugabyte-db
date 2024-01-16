/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bson/bson_sorted_accumulator.h
 *
 * Common declarations related to custom aggregates for group by
 * accumulator with sort specification:
 * first (implicit)
 * last (implicit)
 * top (explicit)
 * bottom (explicit)
 * firstN (implicit)
 * lastN (implicit)
 * topN (explicit)
 * bottomN (explicit)
 *-------------------------------------------------------------------------
 */
#ifndef BSON_SORTED_ACCUMULATOR_H
#define BSON_SORTED_ACCUMULATOR_H
#include "io/helio_bson_core.h"

/* --------------------------------------------------------- */
/* Data-types */
/* --------------------------------------------------------- */

typedef struct BsonOrderAggValue
{
	/* The actual value being returned for this result*/
	pgbson *value;

	/* An array of bsons of size numSortKeys computed
	 * by applying the sort spec on an input document. These
	 * bsons are value from the sort keys for ordering documents.
	 * Mongo allow a maximum of 32 sort keys.
	 */
	Datum sortKeyValues[32];
} BsonOrderAggValue;

typedef struct BsonOrderAggState
{
	/* The values to be returned as aggregation result.
	 * Array of numAggValues number of elements.
	 */
	BsonOrderAggValue **currentResult;

	/* The number of values to be returned by the aggregation result
	 * 1 for first/last.  N for firstN/lastN.
	 */
	int64 numAggValues;

	/* The number of values currently in results */
	int64 currentCount;

	/* The number of sort keys (length of sortKeys) */
	int numSortKeys; /* up to 32; */

	/* An array of size numSortKeys indicating the
	 * direction of sorting (ASC = true, DESC = false)
	 */
	bool sortDirections[32];
} BsonOrderAggState;

/* Handles serialization of state */
bytea * SerializeOrderState(MemoryContext aggregateContext,
							BsonOrderAggState *state,
							bytea *bytes);
void DeserializeOrderState(bytea *bytes,
						   BsonOrderAggState *state);

/*
 * Transition/Combine/Final functions for accumulators
 * invertSort is 'false' for ascending and 'true' for descending.
 * isSingle is 'false' if there is an N param (i.e. firstN/LastN)
 *      and 'true' if there is no N param (i.e. first/last).
 */
Datum BsonOrderTransition(PG_FUNCTION_ARGS, bool invertSort, bool isSingle);
Datum BsonOrderTransitionOnSorted(PG_FUNCTION_ARGS, bool invertSort, bool isSingle);
Datum BsonOrderCombine(PG_FUNCTION_ARGS, bool invertSort);
Datum BsonOrderFinal(PG_FUNCTION_ARGS, bool isSingle);
Datum BsonOrderFinalOnSorted(PG_FUNCTION_ARGS, bool isSingle);

#endif
