
/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/utils/error_utils.h
 *
 * Definitions for utilities related to handling errors.
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <lib/stringinfo.h>

/*
 * Given ereport error code belongs to a Helio error ?
 */
#define EreportCodeIsHelioError(helioErrorEreportCode) \
	(PGUNSIXBIT(helioErrorEreportCode) == 'M')

/* Helper method that gets the error data from the current
 * memory context and flushes the error state. */
static inline ErrorData *
CopyErrorDataAndFlush()
{
	ErrorData *errorData = CopyErrorData();
	FlushErrorState();
	return errorData;
}


/*
 * Prepend error messages of Helio errors within a PG_CATCH() block.
 * Example usage:
 *
 *   MemoryContext savedMemoryContext = CurrentMemoryContext;
 *   PG_TRY();
 *   {
 *     // perform the stuff that could result in throwing a Helio error
 *   }
 *   PG_CATCH();
 *   {
 *     // Make sure to switch back to original memory context before
 *     // re-throwing the error.
 *     MemoryContextSwitchTo(savedMemoryContext);
 *
 *     RethrowPrependHelioError(errorPrefix);
 *   }
 *   PG_END_TRY();
 */
static inline void
RethrowPrependHelioError(char *errorPrefix)
{
	ErrorData *errorData = CopyErrorDataAndFlush();

	if (EreportCodeIsHelioError(errorData->sqlerrcode))
	{
		StringInfo newErrorMessageStr = makeStringInfo();
		appendStringInfo(newErrorMessageStr, "%s%s", errorPrefix,
						 errorData->message);
		errorData->message = newErrorMessageStr->data;
	}

	ThrowErrorData(errorData);
}
