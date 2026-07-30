
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
 * Given ereport error code belongs to a DocumentDB error ?
 */
#define EreportCodeIsDocumentDBError(documentdbErrorEreportCode) \
	(PGUNSIXBIT(documentdbErrorEreportCode) == 'M')

/*
 * This is an PG aligned error code for Internal errors category to represent
 * that write operation was detected with a lost path in the index.
 * For more info see rum/src/rumbtree.c
 */
#define ERRCODE_INDEX_LOSTPATH MAKE_SQLSTATE('X', 'X', '0', '0', '3')

/* Specific error code that tracks unredacted log messages in the server logs */
#define UNREDACTED_LOG_CODE MAKE_SQLSTATE('M', 'Z', 'Z', 'Z', 'Z')

typedef int (*format_log_hook)(const char *fmt, ...) pg_attribute_printf (1, 2);
extern format_log_hook unredacted_log_emit_hook;

#define errmsg_unredacted(...) \
	(unredacted_log_emit_hook ? \
	 (*unredacted_log_emit_hook)(__VA_ARGS__) : \
	 errmsg_internal(__VA_ARGS__))


/* Macro to log messages that are generally PII Safe in server logs */
#define elog_unredacted(...) \
	ereport(LOG_SERVER_ONLY, (errcode(UNREDACTED_LOG_CODE), errhidecontext(true), \
							  errhidestmt(true), errmsg_unredacted(__VA_ARGS__)))

/* Helper method that gets the error data from the current
 * memory context and flushes the error state. */
static inline ErrorData *
CopyErrorDataAndFlush()
{
	ErrorData *errorData = CopyErrorData();
	FlushErrorState();
	return errorData;
}


/* Whether or not the error code is an operator intervention error
 * class (class 57) that should not resume the query.
 */
inline static bool
IsOperatorInterventionError(ErrorData *errorData)
{
	switch (errorData->sqlerrcode)
	{
		case ERRCODE_QUERY_CANCELED:
		case ERRCODE_ADMIN_SHUTDOWN:
		case ERRCODE_CRASH_SHUTDOWN:
		{
			/* Explicit background notification of cancellation */
			return true;
		}

		case ERRCODE_T_R_SERIALIZATION_FAILURE:
		{
			/*
			 * if there's a conflict with recovery, there's no point in continuing
			 * might as well bail and retry the overall query.
			 */
			return true;
		}

		default:
			return false;
	}
}


/*
 * Prepend error messages of DocumentDB errors within a PG_CATCH() block.
 * Example usage:
 *
 *   MemoryContext savedMemoryContext = CurrentMemoryContext;
 *   PG_TRY();
 *   {
 *     // perform the stuff that could result in throwing a DocumentDB error
 *   }
 *   PG_CATCH();
 *   {
 *     // Make sure to switch back to original memory context before
 *     // re-throwing the error.
 *     MemoryContextSwitchTo(savedMemoryContext);
 *
 *     RethrowPrependDocumentDBError(errorPrefix);
 *   }
 *   PG_END_TRY();
 */
static inline void
RethrowPrependDocumentDBError(char *errorPrefix)
{
	ErrorData *errorData = CopyErrorDataAndFlush();

	if (EreportCodeIsDocumentDBError(errorData->sqlerrcode))
	{
		StringInfo newErrorMessageStr = makeStringInfo();
		appendStringInfo(newErrorMessageStr, "%s%s", errorPrefix,
						 errorData->message);
		errorData->message = newErrorMessageStr->data;
	}

	ThrowErrorData(errorData);
}
