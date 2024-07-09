/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/bson/bson_tree.h
 *
 * Common declarations of functions for handling bson path trees.
 *
 *-------------------------------------------------------------------------
 */
#include "aggregation/bson_tree_common.h"
#include "utils/mongo_errors.h"
#include "operators/bson_expression.h"

#ifndef BSON_TREE_H
#define BSON_TREE_H


/* Forward declaration of data types */
struct BsonPathNode;
struct BsonIntermediatePathNode;
struct BsonLeafPathNode;
struct BsonLeafArrayWithFieldPathNode;


/*
 * Base PathNode that all types derive from.
 * For derived types, this must be the first field.
 */
typedef struct BsonPathNode
{
	/* The type of node for this field: whether it is a path
	 * inclusion/exclusion/field or a tree intermediate node
	 * leading to a field path
	 */
	const NodeType nodeType;

	/* the 'current' field path for projection
	 * This is the non-dotted field path at the current level of
	 * the projection tree.
	 *
	 * For example, for {"a": {"b.c": {"d.e.f": 1}}}, this would be equal to
	 * "d", "e", and "f" for nodes representing "d", "e" and "f".
	 *
	 */
	const StringView field;

	/*
	 * The parent node for this node
	 */
	struct BsonIntermediatePathNode *const parent;

	/*
	 * The siblings of this node in the tree.
	 */
	struct BsonPathNode *next;
} BsonPathNode;


/*
 * Struct that holds data about child nodes
 * for node types that have child nodes.
 * Currently this is LeafWithArray and
 * Intermediate.
 *
 * This is an internal structure and should
 * *NEVER* be accessed directly.
 */
typedef struct ChildNodeData
{
	/* The number of children that this field has */
	uint32_t numChildren;

	/*
	 * children points to the tail of the intermediate node.
	 * Given a tree "a": { "b": 1, "c": 1 }, children will point to b
	 * and b.next will point to c.
	 * The first child is tree->children->next.
	 * DO NOT Enumerate the children directly.
	 * DO NOT add to the children directly.
	 */
	BsonPathNode *children;
} ChildNodeData;

/* Data for an intermediate path node in the tree */
typedef struct BsonIntermediatePathNode
{
	/* The base node for this path */
	BsonPathNode baseNode;

	/* Whether the children of the node has at least 1 expression field */
	bool hasExpressionFieldsInChildren;

	/* Child data for the intermediate node.
	 * Do not touch this directly.
	 * Use foreach_child to enumerate */
	ChildNodeData childData;
} BsonIntermediatePathNode;


/* Data for an intermediate path node in the tree */
typedef struct BsonLeafPathNode
{
	/* The base node for this path */
	BsonPathNode baseNode;

	/* the data of the field (if it is a constant and its value, or an operator, field expression, etc.) */
	AggregationExpressionData fieldData;
} BsonLeafPathNode;


/* Data for a leaf array path node in the tree */
typedef struct BsonLeafArrayWithFieldPathNode
{
	/* The base node for this path */
	BsonLeafPathNode leafData;

	/* Child data for the intermediate node.
	 * Do not touch this directly.
	 * Use foreach_array_child to enumerate */
	ChildNodeData arrayChild;
} BsonLeafArrayWithFieldPathNode;


/*
 * Function that creates a leaf node.
 */
typedef BsonLeafPathNode *(*CreateLeafNodeFunc)(const StringView *path, const
												char *relativePath, void *state);


/*
 * Function that creates an intermediate node.
 */
typedef BsonIntermediatePathNode *(*CreateIntermediateNodeFunc)(const StringView *path,
																const char *relativePath,
																void *state);


const BsonLeafPathNode * TraverseDottedPathAndGetOrAddLeafFieldNode(const
																	StringView *path,
																	const bson_value_t *
																	value,
																	BsonIntermediatePathNode
																	*tree,
																	CreateLeafNodeFunc
																	createFunc,
																	bool
																	treatLeafDataAsConstant,
																	bool *nodeCreated);

const BsonLeafPathNode * TraverseDottedPathAndAddLeafFieldNode(const
															   StringView *path,
															   const bson_value_t *
															   value,
															   BsonIntermediatePathNode
															   *tree,
															   CreateLeafNodeFunc
															   createFunc,
															   bool
															   treatLeafDataAsConstant);

const BsonLeafPathNode * TraverseDottedPathAndAddLeafValueNode(const StringView *path,
															   const bson_value_t *value,
															   BsonIntermediatePathNode *
															   tree,
															   CreateLeafNodeFunc
															   createFunc,
															   CreateIntermediateNodeFunc
															   intermediateFunc,
															   bool
															   treatLeafDataAsConstant);

BsonLeafArrayWithFieldPathNode * TraverseDottedPathAndAddLeafArrayNode(const
																	   StringView *path,
																	   BsonIntermediatePathNode
																	   *tree,
																	   CreateIntermediateNodeFunc
																	   intermediateFunc,
																	   bool
																	   treatLeafDataAsConstant);

const BsonPathNode * TraverseDottedPathAndGetOrAddField(const StringView *path,
														const bson_value_t *value,
														BsonIntermediatePathNode *tree,
														CreateIntermediateNodeFunc
														createIntermediateFunc,
														CreateLeafNodeFunc createLeafFunc,
														bool treatLeafNodeAsConstant,
														void *nodeCreationState,
														bool *nodeCreated);
const BsonPathNode * TraverseDottedPathAndGetOrAddValue(const StringView *path,
														const bson_value_t *value,
														BsonIntermediatePathNode *tree,
														CreateIntermediateNodeFunc
														createIntermediateFunc,
														CreateLeafNodeFunc createLeafFunc,
														bool treatLeafNodeAsConstant,
														void *nodeCreationState,
														bool *nodeCreated);

BsonLeafPathNode * BsonDefaultCreateLeafNode(const StringView *fieldPath, const
											 char *relativePath, void *state);
BsonIntermediatePathNode * BsonDefaultCreateIntermediateNode(const StringView *fieldPath,
															 const char *relativePath,
															 void *state);

void ResetNodeWithField(const BsonLeafPathNode *baseLeafNode, const char *relativePath,
						const bson_value_t *value, CreateLeafNodeFunc createFunc,
						bool treatLeafDataAsConstant);
void ResetNodeWithValue(const BsonLeafPathNode *baseLeafNode, const char *relativePath,
						const bson_value_t *value, CreateLeafNodeFunc createFunc,
						bool treatLeafDataAsConstant);
void FreeTree(BsonIntermediatePathNode *root);

const BsonPathNode * ResetNodeWithFieldAndState(const BsonLeafPathNode *baseLeafNode,
												const char *relativePath,
												const bson_value_t *value,
												CreateLeafNodeFunc createFunc,
												bool treatLeafDataAsConstant,
												void *leafState);
const BsonPathNode * ResetNodeWithValueAndState(const BsonLeafPathNode *baseLeafNode,
												const char *relativePath,
												const bson_value_t *value,
												CreateLeafNodeFunc createFunc,
												bool treatLeafDataAsConstant,
												void *leafState);

const BsonLeafPathNode * AddValueNodeToLeafArrayWithField(
	BsonLeafArrayWithFieldPathNode *leafArrayField,
	const char *relativePath,
	int index,
	const bson_value_t *leafValue,
	CreateLeafNodeFunc createFunc,
	bool treatLeafDataAsConstant);

void BuildTreeFromPgbson(BsonIntermediatePathNode *tree, pgbson *document);

/*
 * Helper function to create the Root node of a Bson Path Tree.
 */
inline static BsonIntermediatePathNode *
MakeRootNode()
{
	return palloc0(sizeof(BsonIntermediatePathNode));
}


/*
 * Returns true if the BsonPathNode is an intermediate node.
 */
inline static bool
IsIntermediateNode(const BsonPathNode *node)
{
	return node->nodeType == NodeType_Intermediate;
}


/*
 * Returns true if the BsonPathNode is an intermediate node
 * and has a field in its children.
 */
inline static bool
IsIntermediateNodeWithField(const BsonPathNode *node)
{
	return IsIntermediateNode(node) &&
		   ((const BsonIntermediatePathNode *) node)->
		   hasExpressionFieldsInChildren;
}


/*
 * Returns true if the BsonPathNode is an intermediate node
 * and has at least 1 child node.
 */
inline static bool
IntermediateNodeHasChildren(const BsonIntermediatePathNode *intermediateNode)
{
	return intermediateNode->childData.numChildren > 0;
}


/*
 * Convenience cast functions to get specific node types
 */
inline static const BsonIntermediatePathNode *
CastAsIntermediateNode(const BsonPathNode *toCast)
{
	Assert(IsIntermediateNode(toCast));
	return (const BsonIntermediatePathNode *) toCast;
}


inline static const BsonLeafPathNode *
CastAsLeafNode(const BsonPathNode *toCast)
{
	Assert(NodeType_IsLeaf(toCast->nodeType));
	return (const BsonLeafPathNode *) toCast;
}


inline static const BsonLeafArrayWithFieldPathNode *
CastAsLeafArrayFieldNode(const BsonPathNode *toCast)
{
	Assert(toCast->nodeType == NodeType_LeafWithArrayField);
	return (const BsonLeafArrayWithFieldPathNode *) toCast;
}


#define foreach_child_common(node, parent, childAccessor, castFunc) node = \
	parent->childAccessor.children == NULL ? NULL : \
	(castFunc) (parent->childAccessor.children->next); \
	for (uint32_t _doNotUseMacroTreeCounter = 0; \
		 node != NULL && (_doNotUseMacroTreeCounter < parent->childAccessor.numChildren); \
		 _doNotUseMacroTreeCounter++, node = (castFunc) (((BsonPathNode *) node)->next))

/*
 * Macros that help enumerate intermediate node's children.
 */
#ifdef BSON_TREE_PRIVATE
#define foreach_child(node, parent) foreach_child_common(node, parent, childData, \
														 BsonPathNode *)
#define foreach_array_child(node, parent) foreach_child_common(node, parent, arrayChild, \
															   BsonLeafPathNode *)
#else
#define foreach_child(node, parent) foreach_child_common(node, parent, childData, const \
														 BsonPathNode *)
#define foreach_array_child(node, parent) foreach_child_common(node, parent, arrayChild, \
															   const BsonLeafPathNode *)
#endif

#endif
