/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/utility/process_utility_hook.h
 *
 * The pg_helio_api utility hook function.
 *
 *-------------------------------------------------------------------------
 */
#ifndef PGMONGO_PROCESS_UTILITY_H
#define PGMONGO_PROCESS_UTILITY_H


#include <tcop/utility.h>


extern ProcessUtility_hook_type PgmongoPreviousProcessUtilityHook;


void PgmongoProcessUtility(PlannedStmt *pstmt, const char *queryString,
						   bool readOnlyTree, ProcessUtilityContext context,
						   ParamListInfo params, struct QueryEnvironment *queryEnv,
						   DestReceiver *dest, QueryCompletion *completionTag);

#endif
