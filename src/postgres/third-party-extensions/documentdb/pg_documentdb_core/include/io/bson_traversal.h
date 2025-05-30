/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/bson/bsonvalue_utils.h
 *
 * Core helper function declarations for bsonValues.
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_TRAVERSAL_H
#define BSON_TRAVERSAL_H


/*
 * This enum defines the result of a traverse operation to a document
 */
typedef enum TraverseBsonResult
{
	/*
	 * The field was not found on attempting to traverse the document
	 * for a dotted path.
	 */
	TraverseBsonResult_PathNotFound = 0,

	/*
	 * The field had a type or value mismatch, or the parent path
	 * had a value that was incompatible with the dotted path traversal.
	 */
	TraverseBsonResult_TypeMismatch = 1
} TraverseBsonResult;

/*
 * Represents a set of functions that provide execution extension points
 * when traversing a bson document.
 */
typedef struct TraverseBsonExecutionFuncs
{
	/*
	 * Sets the error result defined by TraverseBsonResult for a given state path.
	 * This function is optional.
	 */
	void (*SetTraverseResult)(void *state, TraverseBsonResult compareResult);

	/*
	 * Visits the top level field specified by the path (e.g. if the path was 'a.b.c', the value at a.b.c)
	 * Returns true if comparison execution should continue.
	 */
	bool (*VisitTopLevelField)(pgbsonelement *element, const StringView *traversePath,
							   void *state);

	/*
	 * Visits the array field at a given index specified by the path (e.g. if the path was 'a.b.c' and was an array,
	 * visits the arrayIndex'th index of the array).
	 * This function is optional.
	 * Returns true if comparison execution should continue.
	 */
	bool (*VisitArrayField)(pgbsonelement *element, const StringView *traversePath, int
							arrayIndex, void *state);

	/*
	 * Given an intermediate array in the path, queries whether or not to continue processing the array
	 * with the specified value.
	 * Returns true if comparison execution should continue.
	 */
	bool (*ContinueProcessIntermediateArray)(void *state, const bson_value_t *value);

	/*
	 * An optional function: On an intermediate array visit, marks the array index that is currently
	 * being traversed.
	 */
	void (*SetIntermediateArrayIndex)(void *state, int32_t arrayIndex);
} TraverseBsonExecutionFuncs;

void TraverseBson(bson_iter_t *documentIterator, const char *traversePath,
				  void *state, const TraverseBsonExecutionFuncs *executionFuncs);
void TraverseBsonPathStringView(bson_iter_t *documentIterator,
								const StringView *traversePathView,
								void *state, const
								TraverseBsonExecutionFuncs *executionFuncs);

#endif
