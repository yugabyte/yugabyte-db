#include "postgres.h"

#include <math.h>

#include "funcapi.h"
#include "builtins.h"

#include "lib/stringinfo.h"
#include "utils/builtins.h"

#include "orafce.h"

PG_FUNCTION_INFO_V1(orafce_listagg1_transfn);
PG_FUNCTION_INFO_V1(orafce_wm_concat_transfn);
PG_FUNCTION_INFO_V1(orafce_listagg2_transfn);
PG_FUNCTION_INFO_V1(orafce_listagg_finalfn);

PG_FUNCTION_INFO_V1(orafce_median4_transfn);
PG_FUNCTION_INFO_V1(orafce_median4_finalfn);
PG_FUNCTION_INFO_V1(orafce_median8_transfn);
PG_FUNCTION_INFO_V1(orafce_median8_finalfn);

typedef struct
{
	int	alen;		/* allocated length */
	int	nextlen;	/* next allocated length */
	int	nelems;		/* number of valid entries */
	union
	{
		float4	*float4_values;
		float8  *float8_values;
	} d;
} MedianState;

int orafce_float4_cmp(const void *a, const void *b);
int orafce_float8_cmp(const void *a, const void *b);

/****************************************************************
 * listagg
 *
 * Concates values and returns string.
 *
 * Syntax:
 *     FUNCTION listagg(string varchar, delimiter varchar = '')
 *      RETURNS varchar;
 *
 * Note: any NULL value is ignored.
 *
 ****************************************************************/
/* subroutine to initialize state */
static StringInfo
makeStringAggState(FunctionCallInfo fcinfo)
{
	StringInfo	state;
	MemoryContext aggcontext;
	MemoryContext oldcontext;

	if (!AggCheckCallContext(fcinfo, &aggcontext))
	{
		/* cannot be called directly because of internal-type argument */
		elog(ERROR, "listagg_transfn called in non-aggregate context");
	}

	/*
	 * Create state in aggregate context.  It'll stay there across subsequent
	 * calls.
	 */
	oldcontext = MemoryContextSwitchTo(aggcontext);
	state = makeStringInfo();
	MemoryContextSwitchTo(oldcontext);

	return state;
}

static void
appendStringInfoText(StringInfo str, const text *t)
{
	appendBinaryStringInfo(str, VARDATA_ANY(t), VARSIZE_ANY_EXHDR(t));
}

Datum
orafce_listagg1_transfn(PG_FUNCTION_ARGS)
{
	StringInfo	state;

	state = PG_ARGISNULL(0) ? NULL : (StringInfo) PG_GETARG_POINTER(0);

	/* Append the element unless null. */
	if (!PG_ARGISNULL(1))
	{
		if (state == NULL)
			state = makeStringAggState(fcinfo);
		appendStringInfoText(state, PG_GETARG_TEXT_PP(1));		/* value */
	}

	/*
	 * The transition type for string_agg() is declared to be "internal",
	 * which is a pass-by-value type the same size as a pointer.
	 */
	PG_RETURN_POINTER(state);
}

Datum
orafce_wm_concat_transfn(PG_FUNCTION_ARGS)
{
	StringInfo	state;

	state = PG_ARGISNULL(0) ? NULL : (StringInfo) PG_GETARG_POINTER(0);

	/* Append the element unless null. */
	if (!PG_ARGISNULL(1))
	{
		if (state == NULL)
			state = makeStringAggState(fcinfo);
		else
			appendStringInfoChar(state, ',');

		appendStringInfoText(state, PG_GETARG_TEXT_PP(1));		/* value */
	}

	/*
	 * The transition type for string_agg() is declared to be "internal",
	 * which is a pass-by-value type the same size as a pointer.
	 */
	PG_RETURN_POINTER(state);
}


Datum
orafce_listagg2_transfn(PG_FUNCTION_ARGS)
{
	return string_agg_transfn(fcinfo);
}

Datum
orafce_listagg_finalfn(PG_FUNCTION_ARGS)
{
	return string_agg_finalfn(fcinfo);
}

static MedianState *
accumFloat4(MedianState *mstate, float4 value, MemoryContext aggcontext)
{
	MemoryContext oldcontext;

	if (mstate == NULL)
	{
		/* First call - initialize */
		oldcontext = MemoryContextSwitchTo(aggcontext);
		mstate = palloc(sizeof(MedianState));
		mstate->alen = 1024;
		mstate->nextlen = 2 * 1024;
		mstate->nelems = 0;
		mstate->d.float4_values = palloc(mstate->alen * sizeof(float4));
		MemoryContextSwitchTo(oldcontext);
	}
	else
	{
		/* enlarge float4_values if needed */
		if (mstate->nelems >= mstate->alen)
		{
			int	newlen = mstate->nextlen;

			oldcontext = MemoryContextSwitchTo(aggcontext);
			mstate->nextlen += mstate->alen;
			mstate->alen = newlen;
			mstate->d.float4_values = repalloc(mstate->d.float4_values,
									    mstate->alen * sizeof(float4));
			MemoryContextSwitchTo(oldcontext);
		}
	}

	mstate->d.float4_values[mstate->nelems++] = value;

	return mstate;
}

static MedianState *
accumFloat8(MedianState *mstate, float8 value, MemoryContext aggcontext)
{
	MemoryContext oldcontext;

	if (mstate == NULL)
	{
		/* First call - initialize */
		oldcontext = MemoryContextSwitchTo(aggcontext);
		mstate = palloc(sizeof(MedianState));
		mstate->alen = 1024;
		mstate->nextlen = 2 * 1024;
		mstate->nelems = 0;
		mstate->d.float8_values = palloc(mstate->alen * sizeof(float8));
		MemoryContextSwitchTo(oldcontext);
	}
	else
	{
		/* enlarge float4_values if needed */
		if (mstate->nelems >= mstate->alen)
		{
			int	newlen = mstate->nextlen;

			oldcontext = MemoryContextSwitchTo(aggcontext);
			mstate->nextlen += mstate->alen;
			mstate->alen = newlen;
			mstate->d.float8_values = repalloc(mstate->d.float8_values,
									    mstate->alen * sizeof(float8));
			MemoryContextSwitchTo(oldcontext);
		}
	}

	mstate->d.float8_values[mstate->nelems++] = value;

	return mstate;
}

Datum
orafce_median4_transfn(PG_FUNCTION_ARGS)
{
	MemoryContext	aggcontext;
	MedianState *state = NULL;
	float4 elem;

	if (!AggCheckCallContext(fcinfo, &aggcontext))
	{
		/* cannot be called directly because of internal-type argument */
		elog(ERROR, "median4_transfn called in non-aggregate context");
	}

	state = PG_ARGISNULL(0) ? NULL : (MedianState *) PG_GETARG_POINTER(0);
	if (PG_ARGISNULL(1))
		PG_RETURN_POINTER(state);

	elem = PG_GETARG_FLOAT4(1);
	state = accumFloat4(state, elem, aggcontext);

	PG_RETURN_POINTER(state);
}

int
orafce_float4_cmp(const void *_a, const void *_b)
{
	float4 a = *((float4 *) _a);
	float4 b = *((float4 *) _b);

	if (isnan(a))
	{
		if (isnan(b))
			return 0;
		else
			return 1;
	}
	else if (isnan(b))
	{
		return -1;
	}
	else
	{
		if (a > b)
			return 1;
		else if (a < b)
			return -1;
		else
			return 0;
	}
}

Datum
orafce_median4_finalfn(PG_FUNCTION_ARGS)
{
	MedianState *state = NULL;
	int	lidx;
	int	hidx;
	float4 result;

	if (PG_ARGISNULL(0))
		PG_RETURN_NULL();

	state = (MedianState *) PG_GETARG_POINTER(0);

	if (state == NULL)
		PG_RETURN_NULL();

	qsort(state->d.float4_values, state->nelems, sizeof(float4), orafce_float4_cmp);

	lidx = state->nelems / 2 + 1 - 1;
	hidx = (state->nelems + 1) / 2 - 1;

	if (lidx == hidx)
		result = state->d.float4_values[lidx];
	else
		result = (state->d.float4_values[lidx] + state->d.float4_values[hidx]) / 2.0f;

	PG_RETURN_FLOAT4(result);
}

Datum
orafce_median8_transfn(PG_FUNCTION_ARGS)
{
	MemoryContext	aggcontext;
	MedianState *state = NULL;
	float8 elem;

	if (!AggCheckCallContext(fcinfo, &aggcontext))
	{
		/* cannot be called directly because of internal-type argument */
		elog(ERROR, "median4_transfn called in non-aggregate context");
	}

	state = PG_ARGISNULL(0) ? NULL : (MedianState *) PG_GETARG_POINTER(0);
	if (PG_ARGISNULL(1))
		PG_RETURN_POINTER(state);

	elem = PG_GETARG_FLOAT8(1);
	state = accumFloat8(state, elem, aggcontext);

	PG_RETURN_POINTER(state);
}

int
orafce_float8_cmp(const void *_a, const void *_b)
{
	float8 a = *((float8 *) _a);
	float8 b = *((float8 *) _b);

	if (isnan(a))
	{
		if (isnan(b))
			return 0;
		else
			return 1;
	}
	else if (isnan(b))
	{
		return -1;
	}
	else
	{
		if (a > b)
			return 1;
		else if (a < b)
			return -1;
		else
			return 0;
	}
}


Datum
orafce_median8_finalfn(PG_FUNCTION_ARGS)
{
	MedianState *state = NULL;
	int	lidx;
	int	hidx;
	float8 result;

	if (PG_ARGISNULL(0))
		PG_RETURN_NULL();

	state = (MedianState *) PG_GETARG_POINTER(0);

	if (state == NULL)
		PG_RETURN_NULL();

	qsort(state->d.float8_values, state->nelems, sizeof(float8), orafce_float8_cmp);

	lidx = state->nelems / 2 + 1 - 1;
	hidx = (state->nelems + 1) / 2 - 1;

	if (lidx == hidx)
		result = state->d.float8_values[lidx];
	else
		result = (state->d.float8_values[lidx] + state->d.float8_values[hidx]) / 2.0;

	PG_RETURN_FLOAT8(result);
}
