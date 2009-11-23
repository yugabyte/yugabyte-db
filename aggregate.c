#include "postgres.h"
#include "funcapi.h"
#include "orafunc.h"
#include "utils/builtins.h"
#include "builtins.h"

PG_FUNCTION_INFO_V1(orafce_listagg1_transfn);
PG_FUNCTION_INFO_V1(orafce_listagg2_transfn);
PG_FUNCTION_INFO_V1(orafce_listagg_finalfn);

PG_FUNCTION_INFO_V1(orafce_median4_transfn);
PG_FUNCTION_INFO_V1(orafce_median4_finalfn);
PG_FUNCTION_INFO_V1(orafce_median8_transfn);
PG_FUNCTION_INFO_V1(orafce_median8_finalfn);

typedef struct
{
	StringInfo	strInfo;
	bool	is_empty;
	char	separator[1];
} ListAggState;

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
static ListAggState *
accumStringResult(ListAggState *state, text *elem, text *separator, 
						    MemoryContext aggcontext)
{
	MemoryContext	oldcontext;
	
	if (state == NULL)
	{
		if (separator != NULL)
		{
			char *cseparator = text_to_cstring(separator);
			int	len = strlen(cseparator);
			
			oldcontext = MemoryContextSwitchTo(aggcontext);
			state = palloc(sizeof(ListAggState) + len);
			
			/* copy delimiter to state var */
			memcpy(&state->separator, cseparator, len + 1);
		}
		else
		{
			oldcontext = MemoryContextSwitchTo(aggcontext);
			state = palloc(sizeof(ListAggState));
			state->separator[0] = '\0';
		}
		
		/* Initialise StringInfo */
		state->strInfo = makeStringInfo();
		state->is_empty = true;
		
		MemoryContextSwitchTo(oldcontext);
	}
	
	/* only when element isn't null */
	if (elem != NULL)
	{
		char	*cstr = text_to_cstring(elem);

		oldcontext = MemoryContextSwitchTo(aggcontext);
		if (!state->is_empty)
			appendStringInfoString(state->strInfo, state->separator);
		appendStringInfoString(state->strInfo, cstr);
		MemoryContextSwitchTo(oldcontext);
		
		state->is_empty = false;
	}
	
	return state;
}

Datum
orafce_listagg1_transfn(PG_FUNCTION_ARGS)
{
	MemoryContext	aggcontext;
	ListAggState *state;
	text *elem;

	if (fcinfo->context && IsA(fcinfo->context, AggState))
		aggcontext = ((AggState *) fcinfo->context)->aggcontext;
	else if (fcinfo->context && IsA(fcinfo->context, WindowAggState))
		aggcontext = ((WindowAggState *) fcinfo->context)->wincontext;
	else
	{
		/* cannot be called directly because of internal-type argument */
		elog(ERROR, "listagg2_transfn called in non-aggregate context");
		aggcontext = NULL;		/* keep compiler quiet */
	}
	
	state = PG_ARGISNULL(0) ? NULL : (ListAggState *) PG_GETARG_POINTER(0);
	elem = PG_ARGISNULL(1) ? NULL : PG_GETARG_TEXT_P(1);
	
	state = accumStringResult(state,
					    elem,
					    NULL,
					    aggcontext);

	/*
	 * The transition type for listagg() is declared to be "internal", which
	 * is a pass-by-value type the same size as a pointer.	
	 */
	PG_RETURN_POINTER(state);
}


 
Datum 
orafce_listagg2_transfn(PG_FUNCTION_ARGS)
{
	MemoryContext	aggcontext;
	ListAggState *state;
	text *elem;
	text *separator = NULL;

	if (fcinfo->context && IsA(fcinfo->context, AggState))
		aggcontext = ((AggState *) fcinfo->context)->aggcontext;
	else if (fcinfo->context && IsA(fcinfo->context, WindowAggState))
		aggcontext = ((WindowAggState *) fcinfo->context)->wincontext;
	else
	{
		/* cannot be called directly because of internal-type argument */
		elog(ERROR, "listagg2_transfn called in non-aggregate context");
		aggcontext = NULL;		/* keep compiler quiet */
	}
	
	state = PG_ARGISNULL(0) ? NULL : (ListAggState *) PG_GETARG_POINTER(0);
	elem = PG_ARGISNULL(1) ? NULL : PG_GETARG_TEXT_P(1);
	
	if (PG_NARGS() > 2)
		separator = PG_ARGISNULL(2) ? NULL : PG_GETARG_TEXT_P(2);
	
	state = accumStringResult(state,
					    elem,
					    separator,
					    aggcontext);
	/*
	 * The transition type for listagg() is declared to be "internal", which
	 * is a pass-by-value type the same size as a pointer.	
	 */
	PG_RETURN_POINTER(state);	
}

Datum
orafce_listagg_finalfn(PG_FUNCTION_ARGS)
{
	ListAggState *state;

	if (PG_ARGISNULL(0))
		PG_RETURN_NULL();
		
	/* cannot be called directly because of internal-type argument */
	Assert(fcinfo->context &&
		    (IsA(fcinfo->context, AggState) ||
			    IsA(fcinfo->context, WindowAggState)));
	
	state = (ListAggState *) PG_GETARG_POINTER(0);
	if (!state->is_empty)
		PG_RETURN_TEXT_P(cstring_to_text(state->strInfo->data));
	else
		PG_RETURN_NULL();    
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
	MedianState *state;
	float4 elem;

	if (fcinfo->context && IsA(fcinfo->context, AggState))
		aggcontext = ((AggState *) fcinfo->context)->aggcontext;
	else if (fcinfo->context && IsA(fcinfo->context, WindowAggState))
		aggcontext = ((WindowAggState *) fcinfo->context)->wincontext;
	else
	{
		/* cannot be called directly because of internal-type argument */
		elog(ERROR, "median4_transfn called in non-aggregate context");
		aggcontext = NULL;		/* keep compiler quiet */
	}
	
	state = PG_ARGISNULL(0) ? NULL : (MedianState *) PG_GETARG_POINTER(0);
	if (PG_ARGISNULL(1))
		PG_RETURN_POINTER(state);
	
	elem = PG_GETARG_FLOAT4(1);
	state = accumFloat4(state, elem, aggcontext);
	
	PG_RETURN_POINTER(state);	
}

int 
orafce_float4_cmp(const void *a, const void *b)
{
	return *((float4 *) a) - *((float4*) b);
}

Datum
orafce_median4_finalfn(PG_FUNCTION_ARGS)
{
	MedianState *state;
	int	lidx;
	int	hidx;
	float4 result;

	if (PG_ARGISNULL(0))
		PG_RETURN_NULL();
		
	state = (MedianState *) PG_GETARG_POINTER(0);
	qsort(state->d.float4_values, state->nelems, sizeof(float4), orafce_float4_cmp);

	lidx = state->nelems / 2 + 1 - 1;
	hidx = (state->nelems + 1) / 2 - 1;
	
	if (lidx == hidx)
		result = state->d.float4_values[lidx];
	else
		result = (state->d.float4_values[lidx] + state->d.float4_values[hidx]) / 2.0;
		
	PG_RETURN_FLOAT4(result);
}

Datum
orafce_median8_transfn(PG_FUNCTION_ARGS)
{
	MemoryContext	aggcontext;
	MedianState *state;
	float8 elem;

	if (fcinfo->context && IsA(fcinfo->context, AggState))
		aggcontext = ((AggState *) fcinfo->context)->aggcontext;
	else if (fcinfo->context && IsA(fcinfo->context, WindowAggState))
		aggcontext = ((WindowAggState *) fcinfo->context)->wincontext;
	else
	{
		/* cannot be called directly because of internal-type argument */
		elog(ERROR, "median4_transfn called in non-aggregate context");
		aggcontext = NULL;		/* keep compiler quiet */
	}
	
	state = PG_ARGISNULL(0) ? NULL : (MedianState *) PG_GETARG_POINTER(0);
	if (PG_ARGISNULL(1))
		PG_RETURN_POINTER(state);
		
	elem = PG_GETARG_FLOAT8(1);
	state = accumFloat8(state, elem, aggcontext);
	
	PG_RETURN_POINTER(state);	
}

int 
orafce_float8_cmp(const void *a, const void *b)
{
	return *((float8 *) a) - *((float8*) b);
}


Datum
orafce_median8_finalfn(PG_FUNCTION_ARGS)
{
	MedianState *state;
	int	lidx;
	int	hidx;
	float8 result;

	if (PG_ARGISNULL(0))
		PG_RETURN_NULL();


	state = (MedianState *) PG_GETARG_POINTER(0);
	qsort(state->d.float8_values, state->nelems, sizeof(float8), orafce_float8_cmp);

	lidx = state->nelems / 2 + 1 - 1;
	hidx = (state->nelems + 1) / 2 - 1;
	
	if (lidx == hidx)
		result = state->d.float8_values[lidx];
	else
		result = (state->d.float8_values[lidx] + state->d.float8_values[hidx]) / 2.0;
		
	PG_RETURN_FLOAT8(result);
}
