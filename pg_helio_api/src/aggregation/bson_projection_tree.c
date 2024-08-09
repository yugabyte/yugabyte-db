/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bson/bson_tree.c
 *
 * Functions to create, modify and query bson projection path trees.
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"
#include "miscadmin.h"

#define BSON_TREE_PRIVATE
#include "aggregation/bson_tree_private.h"
#undef BSON_TREE_PRIVATE

#include <aggregation/bson_projection_tree.h>
#include <aggregation/bson_project_operator.h>


static const char *Inclusion_String = "inclusion";
static const char *Exclusion_String = "exclusion";

static bool BuildBsonPathTreeCore(bson_iter_t *pathSpecification,
								  BsonIntermediatePathNode *tree,
								  BuildBsonPathTreeContext *context,
								  bool forceLeafExpression);
static NodeType DetermineChildNodeTypeWithContext(const bson_value_t *value,
												  bool forceLeafExpression,
												  bool updateInclusionExclusion,
												  BuildBsonPathTreeContext *context);
static inline void EnsureValidInclusionExclusion(const BuildBsonPathTreeContext *context,
												 const StringView *path);

static void DefaultPostProcessLeafNode(void *state, const StringView *path,
									   BsonPathNode *node, bool *isExclusionIfNoInclusion,
									   bool *hasField);
static StringView DefaultPreprocessProcessPath(const StringView *path, const
											   bson_value_t *value,
											   bool isOperator, bool *isFindOperator,
											   void *state,
											   bool *skipInclusionExclusionValidation);
static void DefaultValidateAlreadyExistsNode(void *state, const StringView *path,
											 BsonPathNode *node);

BuildBsonPathTreeFunctions DefaultPathTreeFuncs =
{
	.createLeafNodeFunc = BsonDefaultCreateLeafNode,
	.createIntermediateNodeFunc = BsonDefaultCreateIntermediateNode,
	.postProcessLeafNodeFunc = DefaultPostProcessLeafNode,
	.preprocessPathFunc = DefaultPreprocessProcessPath,
	.validateAlreadyExistsNodeFunc = DefaultValidateAlreadyExistsNode
};

/* Walks the bson document that specifies the projection specification and builds a tree
 * with the projection data.
 * e.g. "a.b.c" and "a.d.e" will produce a -> b,d; b->c; d->e
 * This will allow us to walk the documents later and produce a single projection spec.
 * This also returns if *any* of the paths were inclusions or exclusions
 * A given query can have only all inclusions or all exclusions (which is validated by the caller).
 *
 * Example pathSpecs and the node types of the spec tree:
 *      { "a.b.c" : 1}  // c.node_type : LeafIncluded ; b.node_type = Intermediate ; b.node_type = Intermediate
 *      { "a.b" : { "c" : 0}  // c.node_type : LeafExcluded,b.node_type = Intermediate ; b.node_type = Intermediate
 *      { "a.c" : "1"} // c.node_type : LeafField ; b.node_type = Intermediate with hasExpressionFieldsInChildren = true
 *		{ "a.b" : { "$isArray" : "$a.b"}} // b.node_type : LeafField ; a.node_type = Intermediate with hasExpressionFieldsInChildren = true
 *		{ "a.b.c" : 1,  "a" : { "b" : {"d" :"1" }}} // a,b.node_type = Intermediate with hasExpressionFieldsInChildren = true
 *      { "a.b.c" : "1",  "a" : { "b" : {"d" :1 }}} // a,b.node_type = Intermediate with hasExpressionFieldsInChildren = true
 *      { "a.b" : { "c" : [1, 2 , "$a.b"]}}	// a,b.node_type = Intermediate with hasExpressionFieldsInChildren = true
 *
 *
 *  forceLeafExpression -> Number fields are treated as values rather than inclusion or exclusion (e.g., for addFields)
 */
BsonIntermediatePathNode *
BuildBsonPathTree(bson_iter_t *pathSpecification,
				  BuildBsonPathTreeContext *context,
				  bool forceLeafExpression,
				  bool *hasFields)
{
	BsonIntermediatePathNode *root =
		context->buildPathTreeFuncs->createIntermediateNodeFunc(NULL,
																NULL,
																NULL);
	*hasFields = BuildBsonPathTreeCore(pathSpecification, root, context,
									   forceLeafExpression);
	return root;
}


/*
 * Given a pre-built tree, merges another iterator into the tree.
 */
void
MergeBsonPathTree(BsonIntermediatePathNode *root,
				  bson_iter_t *pathSpecification,
				  BuildBsonPathTreeContext *context,
				  bool forceLeafExpression,
				  bool *hasFields)
{
	*hasFields = BuildBsonPathTreeCore(pathSpecification, root, context,
									   forceLeafExpression);
}


/*
 * Helper method for BuildBsonPathTree.
 * See method comment for details.
 */
static bool
BuildBsonPathTreeCore(bson_iter_t *pathSpecification, BsonIntermediatePathNode *tree,
					  BuildBsonPathTreeContext *context, bool forceLeafExpression)
{
	check_stack_depth();
	CHECK_FOR_INTERRUPTS();

	bool hasFields = false;
	bool isExclusionIfNoInclusion = false;
	bool hasAggregationExpressions = false;

	/* If a leaf node conflicts with another leaf, we replace it with the new one */
	bool replaceExistingNodes = true;
	while (bson_iter_next(pathSpecification))
	{
		const StringView originalPath = bson_iter_key_string_view(pathSpecification);
		const bson_value_t *value = bson_iter_value(pathSpecification);
		bson_iter_t childIter;

		/*
		 *  Build a tree from the path (e.g., "a.b.c" or "a") and get the leaf tree node (e.g., "c" and
		 *  "a" correspondingly). The next step is to set the Node data. If the current spec value is an operator
		 *  or a non-document value, the tree ends there by setting the node data and marking the node as a Leaf Node.
		 *
		 *  If the specField has a document, we will need to continue to build the tree by recursively inspecting
		 *  that document.
		 */

		/* if the field is an operator */
		bool isOperator = (bson_iter_recurse(pathSpecification, &childIter) &&
						   bson_iter_next(&childIter) &&
						   bson_iter_key_len(&childIter) > 1 &&
						   bson_iter_key(&childIter)[0] == '$');

		/* current knowledge about whether the spec has a leaf field (aka terminal field) */
		bool skipInclusionExclusionValidation = false;
		bool isFindOperator = false;
		StringView path = context->buildPathTreeFuncs->preprocessPathFunc(&originalPath,
																		  value,
																		  isOperator,
																		  &isFindOperator,
																		  context->
																		  pathTreeState,
																		  &
																		  skipInclusionExclusionValidation);
		EnsureValidFieldPathString(&path);
		NodeType finalNodeType = NodeType_Intermediate;

		/* If the value of the path is a non-document value or an operator, the tree ends */
		if (value->value_type != BSON_TYPE_DOCUMENT || isOperator || isFindOperator)
		{
			if (StringViewEquals(&path, &IdFieldStringView) &&
				tree->baseNode.parent == NULL)
			{
				/*
				 *  ignore setting inclusion/exclusion, as top level _id  (e.g.,  { "_id" : 1, "a" : 1 })
				 *  does not participate in the check for inclusion and exclusion. However, lower level _id
				 *  fields (e.g., { "b" : {"_id" : 0} : 1, "a.b._id : 1"} )  behaves like normal properties.
				 */
				finalNodeType = DetermineChildNodeType(value,
													   forceLeafExpression);
			}
			else
			{
				bool updateInclusionExclusion = !skipInclusionExclusionValidation;
				finalNodeType = DetermineChildNodeTypeWithContext(value,
																  forceLeafExpression ||
																  isOperator ||
																  isFindOperator,
																  updateInclusionExclusion,
																  context);
			}
		}


		if (!skipInclusionExclusionValidation)
		{
			EnsureValidInclusionExclusion(context, &path);
		}

		BsonPathNode *childNode;
		bool alreadyExists = false;
		void *nodeCreationState = NULL;
		bool hasFieldForIntermediateNode = forceLeafExpression;
		childNode = TraverseDottedPathAndGetNode(&path,
												 tree,
												 hasFieldForIntermediateNode,
												 context->buildPathTreeFuncs->
												 createLeafNodeFunc,
												 context->buildPathTreeFuncs->
												 createIntermediateNodeFunc,
												 finalNodeType,
												 replaceExistingNodes,
												 nodeCreationState,
												 &alreadyExists);

		if (alreadyExists)
		{
			if (context->skipIfAlreadyExists)
			{
				continue;
			}

			context->buildPathTreeFuncs->validateAlreadyExistsNodeFunc(
				context->pathTreeState, &path,
				childNode);
		}

		bool childHasField = false;
		if (finalNodeType != NodeType_Intermediate)
		{
			bool treatLeafDataAsConstant = isFindOperator ||
										   context->skipParseAggregationExpressions;
			childHasField = ValidateAndSetLeafNodeData(childNode, value,
													   &path, treatLeafDataAsConstant,
													   &context->parseAggregationContext);

			context->buildPathTreeFuncs->postProcessLeafNodeFunc(
				context->pathTreeState, &path, childNode,
				&isExclusionIfNoInclusion,
				&childHasField);

			if (childHasField)
			{
				const BsonLeafPathNode *leafNode = CastAsLeafNode(childNode);
				hasAggregationExpressions = hasAggregationExpressions ||
											leafNode->fieldData.kind !=
											AggregationExpressionKind_Constant;
			}
		}
		else
		{
			Assert(!NodeType_IsLeaf(childNode->nodeType));

			/* else, it's a nested field spec ("{ a : { b : { "c" : "1"} }"), and build the remaining tree recursively */
			if (bson_iter_recurse(pathSpecification, &childIter))
			{
				BsonIntermediatePathNode *intermediateNode =
					(BsonIntermediatePathNode *) childNode;
				childHasField = BuildBsonPathTreeCore(&childIter,
													  intermediateNode,
													  context,
													  forceLeafExpression);

				hasAggregationExpressions = hasAggregationExpressions ||
											context->hasAggregationExpressions;
			}

			if (!TrySetIntermediateNodeData(childNode, &path,
											childHasField))
			{
				ThrowErrorOnIntermediateMismatch(childNode, &path);
			}
		}

		/*
		 *  if the path is "a.b.c" and the spec value was { "d" : "1" }, after we determine that {"d" : "1" }
		 *  has a terminal field we will need to update the parent chain of node(c) with that information.
		 *  Note that Node(c) has already been marked with hasExpressionFieldsInChildren. We mark Node(a) and
		 *  Node(b) by traversing the tree (c)->(b)->(a) until we reach the root.
		 */
		if (childHasField)
		{
			BsonIntermediatePathNode *node = childNode->parent;

			while (node->baseNode.nodeType != NodeType_None &&
				   !node->hasExpressionFieldsInChildren)
			{
				if (NodeType_IsLeaf(node->baseNode.nodeType))
				{
					ereport(ERROR, (errmsg("unexpectedly got leaf node")));
				}

				node->hasExpressionFieldsInChildren = true;
				node = node->baseNode.parent;
			}
		}

		hasFields = childHasField || hasFields;
	}

	/*
	 * If projectionSpec has only $slice it will project sliced array and non matching fields too.
	 * Setting hasExclusion to true when $slice is alone will help to project non matching fields also.
	 */
	context->hasExclusion = context->hasExclusion || (isExclusionIfNoInclusion &&
													  !context->hasInclusion);
	context->hasAggregationExpressions = hasAggregationExpressions;
	return hasFields;
}


/*
 * Given a value at the end of a dotted path (e.g. a.b.c.d: <value>)
 * Determines the type of leaf that will be derived from the value.
 * And also sets BuildBsonPathTreeContext context based on the type
 */
static NodeType
DetermineChildNodeTypeWithContext(const bson_value_t *value,
								  bool forceLeafExpression,
								  bool updateInclusionExclusion,
								  BuildBsonPathTreeContext *context)
{
	NodeType childNodeType = DetermineChildNodeType(value, forceLeafExpression);
	if (childNodeType == NodeType_LeafField &&
		context->pathTreeState != NULL &&
		!context->skipParseAggregationExpressions)
	{
		childNodeType = NodeType_LeafFieldWithContext;
	}

	if (updateInclusionExclusion)
	{
		context->hasInclusion = context->hasInclusion || childNodeType !=
								NodeType_LeafExcluded;
		context->hasExclusion = context->hasExclusion || childNodeType ==
								NodeType_LeafExcluded;
	}

	return childNodeType;
}


/*
 * Ensures the validity of Inclusion / Exclusion and throw appropriate error
 */
static inline void
EnsureValidInclusionExclusion(const BuildBsonPathTreeContext *context,
							  const StringView *path)
{
	if (!context->allowInclusionExclusion && context->hasExclusion &&
		context->hasInclusion)
	{
		MongoErrorEreportCode code = context->hasInclusion ? MongoLocation31254 :
									 MongoLocation31253;
		const char *from = context->hasInclusion ? Inclusion_String :
						   Exclusion_String;
		const char *to = context->hasInclusion ? Exclusion_String :
						 Inclusion_String;
		ereport(ERROR, (errcode(code),
						errmsg("Cannot do %s on field %.*s in %s projection.",
							   to, path->length, path->string, from)));
	}
}


/*
 * Default implementations for projection tree functions.
 */
static void
DefaultPostProcessLeafNode(void *state, const StringView *path,
						   BsonPathNode *node, bool *isExclusionIfNoInclusion,
						   bool *hasField)
{
	*isExclusionIfNoInclusion = false;
}


static StringView
DefaultPreprocessProcessPath(const StringView *path, const bson_value_t *value,
							 bool isOperator, bool *isFindOperator, void *state,
							 bool *skipInclusionExclusionValidation)
{
	return *path;
}


static void
DefaultValidateAlreadyExistsNode(void *state, const StringView *path,
								 BsonPathNode *node)
{ }
