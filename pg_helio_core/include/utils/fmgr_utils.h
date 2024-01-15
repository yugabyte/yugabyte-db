/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/utils/fmgr_utils.h
 *
 * Utilities for interacting with FMGR (Function Manager)
 *
 *-------------------------------------------------------------------------
 */

#ifndef FMGR_UTILS_H
#define FMGR_UTILS_H

#include <fmgr.h>

bool IsSafeToReuseFmgrFunctionExtraMultiArgs(PG_FUNCTION_ARGS, int *argPositions, int
											 numberArgs);


/*
 * Convenience function for dealing with cached function arguments.
 * Takes a stateVariable that is to be populated with a cached state,
 * along with the type of the state variable.
 * Inspects whether the functions arguments specified by argPosition is
 * valid for caching; if it is not valid, sets the stateVariable to NULL.
 * If it is valid, then the first call will generate a new value of the given type,
 * populate its values using the populateStateFunc and cache it in the function args state.
 * For subsequent calls, this will be reused from the cache.
 */
#define SetCachedFunctionStateMultiArgs(stateVariable, type, argPositions, numberArgs, \
										populateStateFunc, ...) \
	if (fcinfo->flinfo->fn_extra != NULL) \
	{ \
		stateVariable = (type *) fcinfo->flinfo->fn_extra; \
	} \
	else if (!IsSafeToReuseFmgrFunctionExtraMultiArgs(fcinfo, argPositions, numberArgs)) \
	{ \
		stateVariable = NULL; \
	} \
	else \
	{ \
		MemoryContext originalContext = MemoryContextSwitchTo(fcinfo->flinfo->fn_mcxt); \
		type *tempState = palloc0(sizeof(type)); \
		populateStateFunc(tempState, __VA_ARGS__); \
		MemoryContextSwitchTo(originalContext); \
		fcinfo->flinfo->fn_extra = tempState; \
		stateVariable = tempState; \
	}

#define SetCachedFunctionState(stateVariable, type, argPosition, populateStateFunc, ...) \
	{ \
		int __doNotUseArgPositions[1] = { 0 }; \
		__doNotUseArgPositions[0] = argPosition; \
		SetCachedFunctionStateMultiArgs(stateVariable, type, __doNotUseArgPositions, 1, \
										populateStateFunc, __VA_ARGS__); \
	}


#endif
