/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/bson/bson_project_operator.h
 *
 * Common declarations of functions for handling projection operators in find queries
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_PROJECT_OPERATOR_H
#define BSON_PROJECT_OPERATOR_H

#include "postgres.h"

#include "operators/bson_expr_eval.h"
#include "aggregation/bson_project.h"

/*
 * Function Pointer for handlers of Projection Operators
 */
typedef void (*ProjectionOpHandlerFunc)(const bson_value_t *sourceValue,
										const StringView *path,
										pgbson_writer *writer,
										ProjectDocumentState *projectDocState,
										void *state, bool isInNestedArray);

/*
 * Projection Operator Handler Context
 */
typedef struct ProjectionOpHandlerContext
{
	/* Projection Operator specific handler function */
	ProjectionOpHandlerFunc projectionOpHandlerFunc;

	void *state;
} ProjectionOpHandlerContext;


/*
 * Implementation for functions overriding behavior of
 * tree traversal for Projection for $find.
 */
extern BuildBsonPathTreeFunctions FindPathTreeFunctions;


void * GetPathTreeStateForFind(pgbson *querySpec);
int PostProcessStateForFind(BsonProjectDocumentFunctions *projectDocumentFuncs,
							BuildBsonPathTreeContext *context);

#endif
