/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/bson/bson_tree_write.h
 *
 * Common declarations of functions to write components of a tree.
 *
 *-------------------------------------------------------------------------
 */
#include "aggregation/bson_tree_common.h"

#ifndef BSON_TREE_WRITE_H
#define BSON_TREE_WRITE_H

/* Forward declaration of data types */

/* Hook function that returns true if the node should be skipped
 * when writing a tree to a writer. */
typedef bool (*FilterNodeForWriteFunc)(void *state, int index);

/* The write context holding the function hooks and current
 * write state that is used to write the tree. */
typedef struct WriteTreeContext
{
	/* Pointer to a state that is passed to the filter function. */
	void *state;

	/* Callback function to determine whether the node should be skipped */
	FilterNodeForWriteFunc filterNodeFunc;

	/* Whether we should write null when a path expression is found and the path is undefined on the source document. */
	bool isNullOnEmpty;
} WriteTreeContext;

void TraverseTreeAndWriteFieldsToWriter(const BsonIntermediatePathNode *parentNode,
										pgbson_writer *writer,
										pgbson *parentDocument,
										WriteTreeContext *context,
										const ExpressionVariableContext *variableContext);
void WriteLeafArrayFieldToWriter(pgbson_writer *writer, const BsonPathNode *child,
								 pgbson *document,
								 const ExpressionVariableContext *variableContext);
void AppendLeafArrayFieldChildrenToWriter(pgbson_array_writer *arrayWriter, const
										  BsonLeafArrayWithFieldPathNode *leafArrayNode,
										  pgbson *document,
										  const ExpressionVariableContext *variableContext);

#endif
