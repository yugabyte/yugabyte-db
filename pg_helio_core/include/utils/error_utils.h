
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


/* Helper method that gets the error data from the current
 * memory context and flushes the error state. */
static inline ErrorData *
CopyErrorDataAndFlush()
{
	ErrorData *errorData = CopyErrorData();
	FlushErrorState();
	return errorData;
}
