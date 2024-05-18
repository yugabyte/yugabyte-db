/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/bson/bson_tree_private.h
 *
 * Private declarations functions used for creating the Bson trees
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_TREE_PRIVATE
#error Do not import this header file. Import bson_tree.h / bson_projection_tree.h instead
#endif

#ifndef BSON_TREE_PRIVATE_H
#define BSON_TREE_PRIVATE_H

#include "aggregation/bson_tree.h"
#include "aggregation/bson_tree_common.h"
#include "utils/string_view.h"

BsonPathNode * GetOrAddChildNode(BsonIntermediatePathNode *tree,
								 const char *relativePath,
								 const StringView *fieldPath,
								 CreateLeafNodeFunc createFunc,
								 CreateIntermediateNodeFunc
								 createIntermediateNodeFunc,
								 NodeType childNodeType,
								 bool replaceExistingNodes,
								 void *nodeCreationState,
								 bool *alreadyExists);
void EnsureValidFieldPathString(const StringView *fieldPath);
BsonPathNode * TraverseDottedPathAndGetNode(const StringView *path,
											BsonIntermediatePathNode *tree,
											bool hasFieldForIntermediateNode,
											CreateLeafNodeFunc createFunc,
											CreateIntermediateNodeFunc
											createIntermediateNodeFunc,
											NodeType leafNodeType,
											bool replaceExistingNodes,
											void *nodeCreationState,
											bool *alreadyExists);
bool ValidateAndSetLeafNodeData(BsonPathNode *childNode, const bson_value_t *value,
								const StringView *relativePath,
								bool treatAsConstantExpression,
								const ExpressionVariableContext *variableContext);
bool TrySetIntermediateNodeData(BsonPathNode *node, const StringView *relativePath,
								bool hasFields);
void AddChildToTree(ChildNodeData *childData, BsonPathNode *childNode);
void ReplaceTreeInNodeCore(BsonPathNode *previousNode, BsonPathNode *baseNode,
						   BsonPathNode *newNode);
NodeType DetermineChildNodeType(const bson_value_t *value,
								bool forceLeafExpression);

/*
 * Given a Node pointed to by childNode, initializes the base data
 * for the base PathNode.
 */
inline static void
SetBasePathNodeData(BsonPathNode *childNode, NodeType finalNodeType,
					const StringView *fieldPath, BsonIntermediatePathNode *tree)
{
	BsonPathNode baseNode =
	{
		.nodeType = finalNodeType,
		.field = *fieldPath,
		.parent = tree,
		.next = NULL
	};

	memcpy(childNode, &baseNode, sizeof(BsonPathNode));
}


/*
 * Helper method that throws the Path collision error on intermediate node mismatch.
 */
inline static void
pg_attribute_noreturn()
ThrowErrorOnIntermediateMismatch(BsonPathNode * node, const StringView * relativePath)
{
	int errorCode = MongoLocation31250;
	StringInfo errorMessageStr = makeStringInfo();
	appendStringInfo(errorMessageStr, "Path collision at %.*s", relativePath->length,
					 relativePath->string);

	if (node->field.length < relativePath->length)
	{
		errorCode = MongoLocation31249;
		StringView substring = StringViewSubstring(relativePath,
												   node->field.length +
												   1);
		appendStringInfo(errorMessageStr, " remaining portion %.*s",
						 substring.length, substring.string);
	}

	ereport(ERROR, (errcode(errorCode),
					errmsg("%s", errorMessageStr->data)));
}

#endif
