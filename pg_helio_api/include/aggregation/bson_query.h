/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/bson/bson_query.h
 *
 * Common declarations of functions for handling bson queries.
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_QUERY_H
#define BSON_QUERY_H


#include "io/helio_bson_core.h"

/* The function that is called when dealing with a querySpec on a leaf
 * query filter (e.g. "a.b" : <value>)
 */
typedef void (*ProcessQueryValueFunc)(void *context, const char *path, const
									  bson_value_t *value);

bool TraverseQueryDocumentAndGetId(bson_iter_t *queryDocument,
								   bson_value_t *idValue, bool errorOnConflict);

void TraverseQueryDocumentAndProcess(bson_iter_t *queryDocument, void *context,
									 ProcessQueryValueFunc processValueFunc,
									 bool isUpsert);

#endif
