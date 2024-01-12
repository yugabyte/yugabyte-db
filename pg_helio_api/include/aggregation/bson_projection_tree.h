/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/bson/bson_projection_tree.h
 *
 * Common declarations of functions for handling bson path trees for projection.
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_PROJECTION_TREE_H
#define BSON_PROJECTION_TREE_H

#include "aggregation/bson_tree.h"
#include "aggregation/bson_positional_query.h"
#include "utils/string_view.h"


/*
 * Different stage functions hooks while creating path tree
 */
typedef struct BuildBsonPathTreeFunctions
{
	/* The function that creates the child leaf node */
	CreateLeafNodeFunc createLeafNodeFunc;

	/* Function that creates the intermediate nodes */
	CreateIntermediateNodeFunc createIntermediateNodeFunc;

	/* Used for processing the field paths in the tree */
	StringView (*preprocessPathFunc)(const StringView *path, const
									 bson_value_t *pathSpecValue,
									 bool isOperator,
									 bool *isFindOperator,
									 void *state,
									 bool *skipInclusionExclusionValidation);

	/* Used to process existing leaf nodes while building the tree */
	void (*validateAlreadyExistsNodeFunc)(void *state, const StringView *path,
										  BsonPathNode *node);

	/* Used to post process leaf nodes once created */
	void (*postProcessLeafNodeFunc)(void *state, const StringView *path,
									BsonPathNode *node, bool *isExclusionIfNoInclusion,
									bool *hasFieldsForIntermediate);
} BuildBsonPathTreeFunctions;

/*
 * Supporting context object for building the bson Path tree.
 */
typedef struct BuildBsonPathTreeContext
{
	/*  IN: allowInclusionExclusion -> sets if the inclusion and exclusion are allowed together */
	bool allowInclusionExclusion;

	/* IN: This flag indicates if expressions should be parsed or treat as constants, i.e: when building the tree for wildcard index spec. */
	bool skipParseAggregationExpressions;

	/*  OUT: hasExclusion -> sets if the spec has exclusion of a field */
	bool hasExclusion;

	/*  OUT: hasInclusion -> sets if the spec has inclusion of a field */
	bool hasInclusion;

	/* OUT: Whether the tree contains a leaf node with a non constant aggregation expression or not. */
	bool hasAggregationExpressions;

	/* INOUT: the state of the tree which is used/modified for special find query operators. */
	void *pathTreeState;

	/* IN: Useful functions to handle intermediate stages while building the path tree */
	BuildBsonPathTreeFunctions *buildPathTreeFuncs;
} BuildBsonPathTreeContext;


/*
 * Default implementation for functions overriding behavior of
 * tree traversal for Projection.
 */
extern BuildBsonPathTreeFunctions DefaultPathTreeFuncs;

/*
 * Tree Leaf node for Find Projection operators
 * with additional state
 */
typedef struct BsonLeafNodeWithContext
{
	/* The base node for this path */
	BsonLeafPathNode base;

	/* Any additional state for the node */
	void *context;
} BsonLeafNodeWithContext;


BsonIntermediatePathNode * BuildBsonPathTree(bson_iter_t *pathSpecification,
											 BuildBsonPathTreeContext *context,
											 bool forceLeafExpression,
											 bool *hasFields);

/*
 * Helper function to cast a node as an projection leaf node
 */
inline static const BsonLeafNodeWithContext *
CastAsBsonLeafNodeWithContext(const BsonPathNode *toCast)
{
	Assert(NodeType_IsLeaf(toCast->nodeType));
	return (const BsonLeafNodeWithContext *) toCast;
}


#endif
