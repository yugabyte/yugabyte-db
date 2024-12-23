/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/bson/bson_tree_common.h
 *
 * Common declarations of functions for handling bson path trees.
 *
 *-------------------------------------------------------------------------
 */
#include "io/helio_bson_core.h"
#include "utils/string_view.h"

#ifndef BSON_TREE_COMMON_H
#define BSON_TREE_COMMON_H

#define LEAF_NODE_BASE 0x80

/*
 * Node types for various BsonPathNodes
 * Categories:
 * 0x80+ are all leaf nodes (not intermediate)
 */
typedef enum NodeType
{
	/* Default value for NodeType */
	NodeType_None = 0,

	/* a non-leaf tree node
	 * in a projection a.b.c.d, 'a.b.c' are considered
	 * intermediate nodes
	 */
	NodeType_Intermediate = 0x1,

	/* A leaf tree that contains a
	 * path inclusion (if the path exists, add it to
	 * the target document): e.g. 'a.b': 1
	 */
	NodeType_LeafIncluded = LEAF_NODE_BASE,

	/* A leaf tree that contains a
	 * path exclusion (if the path exists, actively do not add it to
	 * the target document): e.g. 'a.b': 0
	 */
	NodeType_LeafExcluded = LEAF_NODE_BASE + 0x1,

	/* A leaf tree that contains an
	 * expression which can be a static field, or a path, or operator
	 * expression : e.g. 'a.b': '$c.1'
	 */
	NodeType_LeafField = LEAF_NODE_BASE + 0x2,

	/*
	 * An effective leaf node, in a sense that once we are at this node,
	 * we can stop traversing and start writing values. However,
	 * the values are a set of array elements that are stored as child nodes.
	 */
	NodeType_LeafWithArrayField = LEAF_NODE_BASE + 0x3,

	/*
	 * A leaf node with context, that can help in storing misc operators states for narrow cases.
	 * e.g. $ positional, $slice and $elemMatch projection operators etc.
	 */
	NodeType_LeafFieldWithContext = LEAF_NODE_BASE + 0x4,
} NodeType;

#define NodeType_IsLeaf(x) ((x & LEAF_NODE_BASE) != 0)

#endif
