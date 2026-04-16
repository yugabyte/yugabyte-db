/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/api_hooks.h
 *
 * Exports related to hooks for the public API surface that enable distribution.
 *
 *-------------------------------------------------------------------------
 */

#ifndef EXTENSION_API_HOOKS_COMMON_H
#define EXTENSION_API_HOOKS_COMMON_H

#include <postgres.h>
#include "io/bson_core.h"

/*
 * Represents a single row result that is
 * executed on a remote node.
 */
typedef struct DistributedRunCommandResult
{
	/* The node that responded for the row */
	int nodeId;

	/* Whether or not the node succeeded in running the command */
	bool success;

	/* the response (string error, or string coerced value) */
	text *response;
} DistributedRunCommandResult;


/* Private: Feature flag for update tracking */
typedef struct BsonUpdateTracker BsonUpdateTracker;
#endif
