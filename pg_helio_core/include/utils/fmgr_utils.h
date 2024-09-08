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

/*
 *
 * Convenience function to register a callback to cleanup any state that may be allocated
 * by thrid party (e.g, ucol_open() i.e., not allocated by PG). When the PG tries to clean up
 * the current memory context, it will issue a call to the registered callback to perform
 * any additional clean up.
 *
 * We need to be careful to not register the same call back multiple times. One option is
 * decide when to register the call back by checking if fcinfo->flinfo->fn_extra is set to NULL.
 *
 */
#define SetCachedFunctionStateCleanupCallback(stateVariable, callbackFunction) \
	{ \
		MemoryContextCallback *cb = MemoryContextAlloc(fcinfo->flinfo->fn_mcxt, \
													   sizeof(MemoryContextCallback)); \
		cb->func = callbackFunction; \
		cb->arg = (void *) stateVariable; \
		MemoryContextRegisterResetCallback(fcinfo->flinfo->fn_mcxt, cb); \
	}


#endif
