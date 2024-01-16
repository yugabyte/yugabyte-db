/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/bson/bson_project.h
 *
 * Common declarations of functions for handling bson projection.
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_PROJECT_H
#define BSON_PROJECT_H


#include "io/helio_bson_core.h"
#include "aggregation/bson_projection_tree.h"

extern const StringView IdFieldStringView;

/* Forward declare the pointer type ( no need to expose the struct layout) */
typedef struct BsonProjectionQueryState BsonProjectionQueryState;
typedef struct BsonProjectDocumentFunctions BsonProjectDocumentFunctions;

typedef struct ProjectDocumentState ProjectDocumentState;

typedef bool (*TryHandleIntermediateArrayNodeFunc)(const BsonIntermediatePathNode *node,
												   ProjectDocumentState *state,
												   bson_iter_t *sourceValue);

typedef void *(*InitializePendingProjectionStateFunc)(uint32_t totalPendingProjections);

typedef void (*WritePendingProjectionFunc)(pgbson_writer *writer,
										   void *pendingProjections);

typedef bool (*SkipUnresolvedIntermediateFieldsFunc)(const
													 BsonIntermediatePathNode *tree);


/*
 * Context for Projection
 */
typedef struct BsonProjectionContext
{
	/* Whether or not Id should be forcily projected */
	bool forceProjectId;

	/* Whether or not inclusion and exclusion is allowed simultaneously */
	bool allowInclusionExclusion;

	/* Iterator on path spec */
	bson_iter_t *pathSpecIter;

	/* Query spec for positional projection. It all cases this is null when `isFindProjection` is false */
	pgbson *querySpec;
} BsonProjectionContext;

/*
 * Common function pointers hooks uses while project wrting stage
 */
typedef struct BsonProjectDocumentFunctions
{
	/*
	 * Used to handle the intermediate array fields in a projection
	 * e.g. $ projection use the first intermediate array field and applies
	 * the query to find a matching index and projects it
	 */
	TryHandleIntermediateArrayNodeFunc tryMoveArrayIteratorFunc;

	/*
	 * Used in case when some of the projection needs to be written
	 * at the end, this would initialize the state needed to meet the requirement
	 * of such cases
	 * e.g. $elemMatch Projection
	 */
	InitializePendingProjectionStateFunc initializePendingProjectionFunc;

	/*
	 * Used to write the pendingProjection to the main writer at the end
	 */
	WritePendingProjectionFunc writePendingProjectionFunc;
} BsonProjectDocumentFunctions;

/* Per document projection state */
typedef struct ProjectDocumentState
{
	/* Matched document for projection */
	pgbson *parentDocument;

	/*
	 * Whether or not for a $ projection the query is used to evaluate the matching index
	 * This can only be used once by the outermost array for a positional path spec
	 * e.g. {a.b.c.d.$: 1} => If `b` & `d` are both array fields then positional projeciton
	 * is applied on `b`
	 */
	bool isPositionalAlreadyEvaluated;

	/* Whether the projection is exclusion projection */
	bool hasExclusion;

	/* Optional: Bson Project Document stage function hooks */
	BsonProjectDocumentFunctions projectDocumentFuncs;

	/* Pending projections for the document as a whole */
	void *pendingProjectionState;
} ProjectDocumentState;

const BsonProjectionQueryState * GetProjectionStateForBsonProject(
	bson_iter_t *projectionSpecIter,
	bool forceProjectId,
	bool
	allowInclusionExclusion);

const BsonProjectionQueryState * GetProjectionStateForBsonAddFields(
	bson_iter_t *projectionSpecIter);
const BsonProjectionQueryState * GetProjectionStateForBsonUnset(const bson_value_t *
																unsetValue,
																bool forceProjectId);
void GetBsonValueForReplaceRoot(bson_iter_t *replaceRootIterator, bson_value_t *value);


pgbson * ProjectDocumentWithState(pgbson *sourceDocument,
								  const BsonProjectionQueryState *state);
pgbson * ProjectReplaceRootDocument(pgbson *document,
									const AggregationExpressionData *replaceRootExpression,
									bool forceProjectId);

/* projection writer functions */
void TraverseObjectAndAppendToWriter(bson_iter_t *parentIterator,
									 const BsonIntermediatePathNode *pathSpecTree,
									 pgbson_writer *writer,
									 bool projectNonMatchingFields,
									 ProjectDocumentState *projectDocState,
									 bool isInNestedArray);

#endif
