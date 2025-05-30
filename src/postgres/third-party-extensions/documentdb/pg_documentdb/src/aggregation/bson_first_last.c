/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bson/bson_first_last.c
 *
 * Functions related to custom aggregates for group by
 * accumulator first, last.
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <funcapi.h>
#include "aggregation/bson_sorted_accumulator.h"

/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */

PG_FUNCTION_INFO_V1(bson_first_transition);
PG_FUNCTION_INFO_V1(bson_last_transition);
PG_FUNCTION_INFO_V1(bson_first_combine);
PG_FUNCTION_INFO_V1(bson_last_combine);
PG_FUNCTION_INFO_V1(bson_first_last_final);
PG_FUNCTION_INFO_V1(bson_first_transition_on_sorted);
PG_FUNCTION_INFO_V1(bson_last_transition_on_sorted);
PG_FUNCTION_INFO_V1(bson_first_last_final_on_sorted);
PG_FUNCTION_INFO_V1(bson_firstn_transition);
PG_FUNCTION_INFO_V1(bson_lastn_transition);
PG_FUNCTION_INFO_V1(bson_firstn_combine);
PG_FUNCTION_INFO_V1(bson_lastn_combine);
PG_FUNCTION_INFO_V1(bson_firstn_lastn_final);
PG_FUNCTION_INFO_V1(bson_firstn_transition_on_sorted);
PG_FUNCTION_INFO_V1(bson_lastn_transition_on_sorted);
PG_FUNCTION_INFO_V1(bson_firstn_lastn_final_on_sorted);

/*
 * Applies the "state transition" (SFUNC) for first.
 */
Datum
bson_first_transition(PG_FUNCTION_ARGS)
{
	bool isLast = false;
	bool isSingle = true;
	bool storeInputExpression = false;
	return BsonOrderTransition(fcinfo, isLast, isSingle, storeInputExpression);
}


/*
 * Applies the "state transition" (SFUNC) for last.
 */
Datum
bson_last_transition(PG_FUNCTION_ARGS)
{
	/* LAST will invert the transition so that it takes the element opposite of the true comparison result */
	bool isLast = true;
	bool isSingle = true;
	bool storeInputExpression = false;
	return BsonOrderTransition(fcinfo, isLast, isSingle, storeInputExpression);
}


/*
 * Applies the "state transition" (SFUNC) for first.
 */
Datum
bson_first_transition_on_sorted(PG_FUNCTION_ARGS)
{
	bool isLast = false;
	bool isSingle = true;
	return BsonOrderTransitionOnSorted(fcinfo, isLast, isSingle);
}


/*
 * Applies the "state transition" (SFUNC) for last.
 */
Datum
bson_last_transition_on_sorted(PG_FUNCTION_ARGS)
{
	/* LAST will invert the transition so that it takes the element opposite of the true comparison result */
	bool isLast = true;
	bool isSingle = true;
	return BsonOrderTransitionOnSorted(fcinfo, isLast, isSingle);
}


/*
 * Applies the "final" (FINALFUNC) for first and last.
 */
Datum
bson_first_last_final(PG_FUNCTION_ARGS)
{
	bool isSingle = true;
	return BsonOrderFinal(fcinfo, isSingle);
}


/*
 * Applies the "final" (FINALFUNC) for first and last.
 */
Datum
bson_first_last_final_on_sorted(PG_FUNCTION_ARGS)
{
	bool isSingle = true;
	return BsonOrderFinalOnSorted(fcinfo, isSingle);
}


/*
 * Applies the "combine" (COMBINEFUNC) for first and last.
 */
Datum
bson_first_combine(PG_FUNCTION_ARGS)
{
	bool isLast = false;
	return BsonOrderCombine(fcinfo, isLast);
}


/*
 * Applies the "combine" (COMBINEFUNC) for first and last.
 */
Datum
bson_last_combine(PG_FUNCTION_ARGS)
{
	bool isLast = true;
	return BsonOrderCombine(fcinfo, isLast);
}


/*
 * Applies the "state transition" (SFUNC) for firstn.
 */
Datum
bson_firstn_transition(PG_FUNCTION_ARGS)
{
	bool isLast = false;
	bool isSingle = false;
	bool storeInputExpression = false;
	return BsonOrderTransition(fcinfo, isLast, isSingle, storeInputExpression);
}


/*
 * Applies the "state transition" (SFUNC) for lastn.
 */
Datum
bson_lastn_transition(PG_FUNCTION_ARGS)
{
	/* LAST will invert the transition so that it takes the element opposite of the true comparison result */
	bool isLast = true;
	bool isSingle = false;
	bool storeInputExpression = false;
	return BsonOrderTransition(fcinfo, isLast, isSingle, storeInputExpression);
}


/*
 * Applies the "state transition" (SFUNC) for firstn.
 */
Datum
bson_firstn_transition_on_sorted(PG_FUNCTION_ARGS)
{
	bool isLast = false;
	bool isSingle = false;
	return BsonOrderTransitionOnSorted(fcinfo, isLast, isSingle);
}


/*
 * Applies the "state transition" (SFUNC) for lastn.
 */
Datum
bson_lastn_transition_on_sorted(PG_FUNCTION_ARGS)
{
	/* LAST will invert the transition so that it takes the element opposite of the true comparison result */
	bool isLast = true;
	bool isSingle = false;
	return BsonOrderTransitionOnSorted(fcinfo, isLast, isSingle);
}


/*
 * Applies the "final" (FINALFUNC) for firstn and lastn.
 */
Datum
bson_firstn_lastn_final(PG_FUNCTION_ARGS)
{
	bool isSingle = false;
	return BsonOrderFinal(fcinfo, isSingle);
}


/*
 * Applies the "final" (FINALFUNC) for firstn and lastn.
 */
Datum
bson_firstn_lastn_final_on_sorted(PG_FUNCTION_ARGS)
{
	bool isSingle = false;
	return BsonOrderFinalOnSorted(fcinfo, isSingle);
}


/*
 * Applies the "combine" (COMBINEFUNC) for firstn.
 */
Datum
bson_firstn_combine(PG_FUNCTION_ARGS)
{
	bool isLast = false;
	return BsonOrderCombine(fcinfo, isLast);
}


/*
 * Applies the "combine" (COMBINEFUNC) and lastn.
 */
Datum
bson_lastn_combine(PG_FUNCTION_ARGS)
{
	bool isLast = true;
	return BsonOrderCombine(fcinfo, isLast);
}
