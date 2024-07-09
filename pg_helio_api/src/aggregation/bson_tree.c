/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bson/bson_tree.c
 *
 * Functions to create, modify and query bson path trees.
 *
 *-------------------------------------------------------------------------
 */
#include <postgres.h>
#include <lib/stringinfo.h>
#include <miscadmin.h>

#define BSON_TREE_PRIVATE
#include "aggregation/bson_tree.h"
#include "aggregation/bson_tree_private.h"
#undef BSON_TREE_PRIVATE

#include "io/helio_bson_core.h"

#include "aggregation/bson_projection_tree.h"

static BsonLeafPathNode * GetOrAddLeafArrayChildNode(BsonLeafArrayWithFieldPathNode *tree,
													 const char *relativePath,
													 const StringView *fieldPath,
													 CreateLeafNodeFunc createFunc,
													 NodeType childNodeType);
static BsonLeafPathNode * CreateLeafNodeWithArrayField(const StringView *fieldPath,
													   const char *relativePath,
													   void *state);
static const BsonPathNode * ResetNodeWithValueOrField(const
													  BsonLeafPathNode *baseLeafNode,
													  const char *relativePath, const
													  bson_value_t *value,
													  CreateLeafNodeFunc createFunc,
													  bool forceLeafExpression,
													  bool treatLeafDataAsConstant,
													  void *nodeCreationState);
static BsonPathNode * TraverseDottedPathAndGetOrAddNodeCore(const StringView *path,
															const bson_value_t *value,
															BsonIntermediatePathNode *tree,
															CreateLeafNodeFunc createFunc,
															CreateIntermediateNodeFunc
															createIntermediateFunc,
															bool forceLeafExpression,
															bool treatNodeDataAsConstant,
															void *nodeCreationState,
															bool *nodeCreated);
static void FreeBsonPathNode(BsonPathNode *node);
static void FreeLeafWithArrayFieldNodes(BsonLeafArrayWithFieldPathNode *leafArrayNode);

/*
 * This is a wrapper function to help usage of bson_uint32_to_string() func.
 * If input num < 1000, the bson_uint32_to_string() func, returns
 * pointer to a statically allocated memory.
 * When num >= 1000, bson_uint32_to_string() func uses the char buffer
 * provided to it, to store the converted string.
 * This function is defined as "inline" becauze it's often used within a loop,
 * for all indexes of array.
 */
inline static StringView
Uint32ToStringView(uint32_t num)
{
	char *buffer = NULL;
	size_t bufferSize = 0;
	if (num >= 1000)
	{
		bufferSize = UINT32_MAX_STR_LEN * sizeof(char);
		buffer = (char *) palloc(bufferSize);
	}

	StringView s;
	s.length = bson_uint32_to_string(num, &(s.string), buffer,
									 bufferSize);

	return s;
}


/*
 * Checks if a string view contains DbRefs fields like $id, $ref, $db.
 */
inline static bool
StringViewContainsDbRefsField(const StringView *source)
{
	return StringViewEqualsCString(source, "$id") ||
		   StringViewEqualsCString(source, "$ref") ||
		   StringViewEqualsCString(source, "$db");
}


/*
 * Walks the dotted path in the path variable and creates the necessary
 * Intermediate nodes until it reaches the leaf node specified by the path.
 * If a node exists, returns the node. If the node does not exist,
 * creates a node at that path.
 * Returns the existing or created node.
 */
const BsonLeafPathNode *
TraverseDottedPathAndGetOrAddLeafFieldNode(const StringView *path,
										   const bson_value_t *value,
										   BsonIntermediatePathNode *tree,
										   CreateLeafNodeFunc createFunc,
										   bool treatLeafDataAsConstant,
										   bool *nodeCreated)
{
	bool forceLeafExpression = true;
	void *nodeCreationState = NULL;
	BsonPathNode *childNode = TraverseDottedPathAndGetOrAddNodeCore(path, value, tree,
																	createFunc,
																	BsonDefaultCreateIntermediateNode,
																	forceLeafExpression,
																	treatLeafDataAsConstant,
																	nodeCreationState,
																	nodeCreated);
	return CastAsLeafNode(childNode);
}


const BsonPathNode *
TraverseDottedPathAndGetOrAddField(const StringView *path, const bson_value_t *value,
								   BsonIntermediatePathNode *tree,
								   CreateIntermediateNodeFunc
								   createIntermediateFunc,
								   CreateLeafNodeFunc createLeafFunc,
								   bool treatLeafNodeAsConstant,
								   void *nodeCreationState, bool *nodeCreated)
{
	bool forceLeafExpression = true;
	return TraverseDottedPathAndGetOrAddNodeCore(path, value, tree,
												 createLeafFunc,
												 createIntermediateFunc,
												 forceLeafExpression,
												 treatLeafNodeAsConstant,
												 nodeCreationState,
												 nodeCreated);
}


const BsonPathNode *
TraverseDottedPathAndGetOrAddValue(const StringView *path, const bson_value_t *value,
								   BsonIntermediatePathNode *tree,
								   CreateIntermediateNodeFunc
								   createIntermediateFunc,
								   CreateLeafNodeFunc createLeafFunc,
								   bool treatLeafNodeAsConstant,
								   void *nodeCreationState, bool *nodeCreated)
{
	bool forceLeafExpression = false;
	return TraverseDottedPathAndGetOrAddNodeCore(path, value, tree,
												 createLeafFunc,
												 createIntermediateFunc,
												 forceLeafExpression,
												 treatLeafNodeAsConstant,
												 nodeCreationState,
												 nodeCreated);
}


/*
 * Walks the dotted path in the path variable and creates the necessary
 * Intermediate nodes until it produces a LeafField node that has the
 * value specified by the bson value.
 * Returns the created node.
 */
const BsonLeafPathNode *
TraverseDottedPathAndAddLeafFieldNode(const StringView *path,
									  const bson_value_t *value,
									  BsonIntermediatePathNode *tree,
									  CreateLeafNodeFunc createFunc,
									  bool treatLeafDataAsConstant)
{
	bool hasField = true;
	NodeType leafNodeType = NodeType_LeafField;
	bool replaceExistingNodes = true;
	bool alreadyExistsIgnore = false;
	void *nodeCreationState = NULL;
	BsonPathNode *childNode = TraverseDottedPathAndGetNode(path, tree, hasField,
														   createFunc,
														   BsonDefaultCreateIntermediateNode,
														   leafNodeType,
														   replaceExistingNodes,
														   nodeCreationState,
														   &alreadyExistsIgnore);
	ValidateAndSetLeafNodeData(childNode, value, path, treatLeafDataAsConstant);
	return CastAsLeafNode(childNode);
}


/*
 * Walks the dotted path in the path variable and creates the necessary
 * Intermediate nodes until it produces a leaf node that has the
 * value specified by the bson value.
 * Returns the created node.
 */
const BsonLeafPathNode *
TraverseDottedPathAndAddLeafValueNode(const StringView *path,
									  const bson_value_t *value,
									  BsonIntermediatePathNode *tree,
									  CreateLeafNodeFunc createFunc,
									  CreateIntermediateNodeFunc intermediateFunc,
									  bool treatLeafDataAsConstant)
{
	bool hasField = false;
	bool forceLeafExpression = false;
	bool replaceExistingNodes = true;
	bool alreadyExistsIgnore = false;
	void *nodeCreationState = NULL;
	NodeType leafNodeType = DetermineChildNodeType(value, forceLeafExpression);
	BsonPathNode *childNode = TraverseDottedPathAndGetNode(path, tree, hasField,
														   createFunc,
														   intermediateFunc,
														   leafNodeType,
														   replaceExistingNodes,
														   nodeCreationState,
														   &alreadyExistsIgnore);
	ValidateAndSetLeafNodeData(childNode, value, path, treatLeafDataAsConstant);
	return CastAsLeafNode(childNode);
}


/*
 * Walks the dotted path in the path variable and creates the necessary
 * Intermediate nodes until it produces a leaf node with array children.
 * Returns the created node.
 */
BsonLeafArrayWithFieldPathNode *
TraverseDottedPathAndAddLeafArrayNode(const StringView *path,
									  BsonIntermediatePathNode *tree,
									  CreateIntermediateNodeFunc intermediateFunc,
									  bool treatLeafDataAsConstant)
{
	bson_value_t includeValue = { 0 };
	includeValue.value_type = BSON_TYPE_INT32;
	includeValue.value.v_int32 = 1;

	bool hasField = true;
	NodeType leafNodeType = NodeType_LeafWithArrayField;
	bool replaceExistingNodes = true;
	bool alreadyExistsIgnore = false;
	void *nodeCreationState = NULL;
	BsonPathNode *childNode = TraverseDottedPathAndGetNode(path, tree, hasField,
														   CreateLeafNodeWithArrayField,
														   intermediateFunc,
														   leafNodeType,
														   replaceExistingNodes,
														   nodeCreationState,
														   &alreadyExistsIgnore);

	ValidateAndSetLeafNodeData(childNode, &includeValue, path, treatLeafDataAsConstant);
	return (BsonLeafArrayWithFieldPathNode *) childNode;
}


/*
 * Adds the specified leaf field at the given index to a leaf array path node
 * as a child.
 */
const BsonLeafPathNode *
AddValueNodeToLeafArrayWithField(BsonLeafArrayWithFieldPathNode *leafArrayField, const
								 char *relativePath, int index, const
								 bson_value_t *leafValue,
								 CreateLeafNodeFunc createFunc,
								 bool treatLeafDataAsConstant)
{
	NodeType leafNodeType = NodeType_LeafField;
	StringView indexFieldPath = Uint32ToStringView(index);
	BsonLeafPathNode *childNode = GetOrAddLeafArrayChildNode(leafArrayField, relativePath,
															 &indexFieldPath, createFunc,
															 leafNodeType);
	ValidateAndSetLeafNodeData(&childNode->baseNode, leafValue, &indexFieldPath,
							   treatLeafDataAsConstant);
	return childNode;
}


/*
 * Given a base node that is already in the tree, resets the node to the new field value
 * provided and replaces that node in the tree.
 */
void
ResetNodeWithField(const BsonLeafPathNode *baseLeafNode, const char *relativePath, const
				   bson_value_t *value, CreateLeafNodeFunc createFunc, bool
				   treatLeafDataAsConstant)
{
	bool forceLeafExpression = true;
	void *nodeCreationState = NULL;
	ResetNodeWithValueOrField(baseLeafNode, relativePath, value, createFunc,
							  forceLeafExpression, treatLeafDataAsConstant,
							  nodeCreationState);
}


/*
 * Given a base node that is already in the tree, resets the node to the new value
 * provided and replaces that node in the tree.
 */
void
ResetNodeWithValue(const BsonLeafPathNode *baseLeafNode, const char *relativePath, const
				   bson_value_t *value, CreateLeafNodeFunc createFunc, bool
				   treatLeafDataAsConstant)
{
	bool forceLeafExpression = false;
	void *nodeCreationState = NULL;
	ResetNodeWithValueOrField(baseLeafNode, relativePath, value, createFunc,
							  forceLeafExpression, treatLeafDataAsConstant,
							  nodeCreationState);
}


/*
 * Given a base node that is already in the tree, resets the node to the new value
 * provided as a field and replaces that node in the tree - with the appropriate state passed
 * to the node creation function
 */
const BsonPathNode *
ResetNodeWithFieldAndState(const BsonLeafPathNode *baseLeafNode,
						   const char *relativePath,
						   const bson_value_t *value,
						   CreateLeafNodeFunc createFunc,
						   bool treatLeafDataAsConstant,
						   void *leafState)
{
	bool forceLeafExpression = true;
	return ResetNodeWithValueOrField(baseLeafNode, relativePath, value, createFunc,
									 forceLeafExpression, treatLeafDataAsConstant,
									 leafState);
}


/*
 * Given a base node that is already in the tree, resets the node to the new value
 * provided and replaces that node in the tree - with the appropriate state passed
 * to the node creation function
 */
const BsonPathNode *
ResetNodeWithValueAndState(const BsonLeafPathNode *baseLeafNode,
						   const char *relativePath,
						   const bson_value_t *value,
						   CreateLeafNodeFunc createFunc,
						   bool treatLeafDataAsConstant,
						   void *leafState)
{
	bool forceLeafExpression = false;
	return ResetNodeWithValueOrField(baseLeafNode, relativePath, value, createFunc,
									 forceLeafExpression, treatLeafDataAsConstant,
									 leafState);
}


/*
 * Extensibility point to create default empty leaf nodes.
 * This creates the simple BsonLeafPathNode when its usage does not require
 * any additional information.
 */
BsonLeafPathNode *
BsonDefaultCreateLeafNode(const StringView *fieldPath, const char *relativePath,
						  void *state)
{
	/* Return a default leaf path node */
	return palloc0(sizeof(BsonLeafPathNode));
}


/*
 * Creates the default intermediate path node (used in case intermediate nodes don't need
 * additional state).
 */
BsonIntermediatePathNode *
BsonDefaultCreateIntermediateNode(const StringView *fieldPath, const char *relativePath,
								  void *state)
{
	return palloc0(sizeof(BsonIntermediatePathNode));
}


/*
 * EnsureValidFieldPathString throws an error if given field path string is
 * not valid.
 */
void
EnsureValidFieldPathString(const StringView *fieldPath)
{
	if (fieldPath->length == 0)
	{
		ereport(ERROR, (errcode(MongoFailedToParse),
						errmsg("FieldPath cannot be constructed with empty string.")));
	}
	else if (StringViewEndsWith(fieldPath, '.'))
	{
		ereport(ERROR, (errcode(MongoFailedToParse),
						errmsg("FieldPath must not end with a '.'.")));
	}

	StringView fieldPathView = *fieldPath;
	while (fieldPathView.length > 0)
	{
		StringView currentFieldPath = StringViewFindPrefix(&fieldPathView, '.');

		if (currentFieldPath.string == NULL)
		{
			currentFieldPath = fieldPathView;
			fieldPathView = (StringView) {
				NULL, 0
			};
		}
		else
		{
			fieldPathView = StringViewSubstring(&fieldPathView, currentFieldPath.length +
												1);
		}

		if (currentFieldPath.length == 0)
		{
			ereport(ERROR, (errcode(MongoFailedToParse),
							errmsg("FieldPath field names may not be empty strings.")));
		}
		else if (StringViewStartsWith(&currentFieldPath, '$') &&
				 !StringViewContainsDbRefsField(&currentFieldPath))
		{
			ereport(ERROR, (errcode(MongoFailedToParse),
							errmsg("FieldPath field names may not start with '$'. "
								   "Consider using $getField or $setField")));
		}
	}
}


/*
 * For a given dotted path specification, walks the object and builds the projection node spec.
 * e.g. a.b.c: 1, will walk and generate the tree a-> b -> c
 */
BsonPathNode *
TraverseDottedPathAndGetNode(const StringView *path,
							 BsonIntermediatePathNode *tree,
							 bool hasFieldForIntermediateNode,
							 CreateLeafNodeFunc childNodeCreateFunc,
							 CreateIntermediateNodeFunc intermediateNodeCreateFunc,
							 NodeType leafNodeType,
							 bool replaceExistingNodes,
							 void *nodeCreationState,
							 bool *alreadyExists)
{
	BsonIntermediatePathNode *treeToInspect = tree;
	BsonPathNode *childNode;

	StringView pathView = *path;
	do {
		CHECK_FOR_INTERRUPTS();
		StringView currentField = StringViewFindPrefix(&pathView, '.');
		StringView *subFieldPath;
		if (currentField.string == NULL)
		{
			currentField = pathView;
			pathView = (StringView) {
				NULL, 0
			};
		}
		else
		{
			pathView = StringViewSubstring(&pathView, currentField.length + 1);
		}

		subFieldPath = &currentField;

		NodeType childNodeNodeTypeInternal = NodeType_Intermediate;
		bool *alreadyExistsInternal = NULL;
		if (pathView.length == 0)
		{
			childNodeNodeTypeInternal = leafNodeType;
			alreadyExistsInternal = alreadyExists;
		}

		childNode = GetOrAddChildNode(treeToInspect, path->string,
									  subFieldPath, childNodeCreateFunc,
									  intermediateNodeCreateFunc,
									  childNodeNodeTypeInternal,
									  replaceExistingNodes,
									  nodeCreationState,
									  alreadyExistsInternal);
		if (childNodeNodeTypeInternal == NodeType_Intermediate)
		{
			if (TrySetIntermediateNodeData(childNode, path, hasFieldForIntermediateNode))
			{
				treeToInspect = (BsonIntermediatePathNode *) childNode;
			}
			else if (replaceExistingNodes)
			{
				ThrowErrorOnIntermediateMismatch(childNode, path);
			}
			else
			{
				/* if this is a GetOrAdd scenario, break and return the node
				 * that exists */
				*alreadyExists = true;
				break;
			}
		}
		else
		{
			/* we hit a child node */
			break;
		}
	} while (pathView.length > 0);
	return childNode;
}


/*
 * Sets the data on a child leaf node, whether it is a field or a path projection
 * with include/exclude data.
 *
 * forceLeafExpression -> If a leaf node is forced to be an expression. E.g., $project treats
 * a:1 as inclusion while addFields treats as setting a = 1. If forceLeafExpression= true, that means that it will
 * follow the $addFields semantics.
 *
 * relativePath is the path to childNode within the innermost document.
 *
 * For example, for {"a": {"b.c": {"d.e.f": 1}}}, this would be equal to
 * "d.e.f" for fields "d", "e" and "f".
 */
bool
ValidateAndSetLeafNodeData(BsonPathNode *childNode, const bson_value_t *value,
						   const StringView *relativePath, bool treatAsConstantExpression)
{
	if (childNode->nodeType == NodeType_Intermediate)
	{
		ereport(ERROR, (errcode(MongoLocation31250),
						errmsg("Path collision at %.*s", relativePath->length,
							   relativePath->string)));
	}

	Assert(NodeType_IsLeaf(childNode->nodeType));
	BsonLeafPathNode *leafPathNode = (BsonLeafPathNode *) childNode;

	/* reset field data in case this is a node we're replacing in the tree. */
	memset(&leafPathNode->fieldData, 0, sizeof(AggregationExpressionData));

	if (treatAsConstantExpression)
	{
		leafPathNode->fieldData.kind = AggregationExpressionKind_Constant;
		leafPathNode->fieldData.value = *value;
	}
	else
	{
		ParseAggregationExpressionData(&leafPathNode->fieldData, value);
	}

	return leafPathNode->baseNode.nodeType == NodeType_LeafField ||
		   leafPathNode->baseNode.nodeType == NodeType_LeafFieldWithContext;
}


/*
 *  Update node type after a child has been added to a node.
 *  Additionally detects any path collision when a Leaf Node set by a previous path has a conflicting specification.
 *  e.g., if we have "a.b": 1, "a.b.1" : 1, we report a path collision. Note that, find() had a last writer wins
 *  behavior until Mongo 5.0. Since Mongo 5.0 both project() and find() exhibits the same behavior.
 *
 * relativePath is the path to given node within the innermost document.
 *
 * For example, for {"a": {"b.c": {"d.e.f": 1}}}, this would be equal to
 * "d.e.f" for fields "d", "e" and "f".
 */
bool
TrySetIntermediateNodeData(BsonPathNode *node, const StringView *relativePath,
						   bool hasFields)
{
	if (NodeType_IsLeaf(node->nodeType))
	{
		return false;
	}

	if (node->nodeType == NodeType_Intermediate && hasFields)
	{
		BsonIntermediatePathNode *intermediateNode = (BsonIntermediatePathNode *) node;
		intermediateNode->hasExpressionFieldsInChildren = true;
	}

	return true;
}


/*
 * For a given tree and a field, returns a child projection node if it exists, or creates one.
 *
 * relativePath is the path that contains given field within the innermost
 * document.
 *
 * For example, for {"a": {"b.c": {"d.e.f": 1}}}, this would be equal to
 * "d.e.f" for fields "d", "e" and "f".
 *
 * Also sets alreadyExists (if it's passed to be non-NULL) to indicate whether
 * the node already exists or not.
 */
BsonPathNode *
GetOrAddChildNode(BsonIntermediatePathNode *tree, const char *relativePath,
				  const StringView *fieldPath, CreateLeafNodeFunc createFunc,
				  CreateIntermediateNodeFunc createIntermediateNodeFunc,
				  NodeType childNodeType, bool replaceExistingNodes,
				  void *nodeCreationState, bool *alreadyExists)
{
	BsonPathNode *childNode;
	BsonPathNode *previousChild = NULL;
	bool found = false;
	foreach_child(childNode, tree)
	{
		if (StringViewEquals(&childNode->field, fieldPath))
		{
			found = true;
			break;
		}

		previousChild = childNode;
	}

	if (alreadyExists)
	{
		*alreadyExists = found;
	}

	/* field node not found. Add as child to the current tree. */
	if (!found)
	{
		if (childNodeType != NodeType_Intermediate)
		{
			childNode = (BsonPathNode *) createFunc(fieldPath, relativePath,
													nodeCreationState);
		}
		else
		{
			childNode = (BsonPathNode *) createIntermediateNodeFunc(fieldPath,
																	relativePath,
																	nodeCreationState);
		}

		/* Set base data */
		SetBasePathNodeData(childNode, childNodeType, fieldPath, tree);

		AddChildToTree(&tree->childData, childNode);
	}
	else if (replaceExistingNodes && childNode->nodeType != childNodeType &&
			 NodeType_IsLeaf(childNode->nodeType) &&
			 NodeType_IsLeaf(childNodeType))
	{
		/* There is a mismatch - we have two existing leaves that claim to be the same path
		 * In this case, it's Last writer wins, so we replace the field
		 */
		BsonPathNode *currentChildNode = childNode;
		childNode = (BsonPathNode *) createFunc(fieldPath, relativePath,
												nodeCreationState);
		SetBasePathNodeData(childNode, childNodeType, fieldPath, tree);
		ReplaceTreeInNodeCore(previousChild, currentChildNode, childNode);
	}

	return childNode;
}


/*
 * Given an intermediate node that has a 'ChildNodeData',
 * inserts a child into the specified tree ensuring that the
 * linkedlist relationships are maintained.
 */
void
AddChildToTree(ChildNodeData *childData, BsonPathNode *childNode)
{
	/* fix-up the tree's child */
	if (childData->children == NULL)
	{
		/* Single child - make child point to itself */
		childData->children = childNode;
		childNode->next = childNode;
	}
	else
	{
		/*
		 * Tree has children - child points to the tail
		 * add to the tail
		 */
		BsonPathNode *currentTail = childData->children;

		/* point the child's next to the current head */
		childNode->next = currentTail->next;

		/* Point the current Tail's next to the child */
		currentTail->next = childNode;

		/* Point the tree's child to the new tail */
		childData->children = childNode;
	}

	childData->numChildren++;
}


/*
 * Given an baseNode, and a previousNode that points to baseNode,
 * replaces baseNode in the tree with newNode ensuring that tree
 * pointers are properly managed.
 */
void
ReplaceTreeInNodeCore(BsonPathNode *previousNode, BsonPathNode *baseNode,
					  BsonPathNode *newNode)
{
	/* Now that we have the previous node, there's 2 cases */
	if (previousNode == NULL)
	{
		/* Case 1: previous node is not initialized by the caller
		 * in the foreach_child semantics, this means that the baseNode
		 * was the First node visited in the loop, which implies it is
		 * the head of the linked list: the current node is the first node (parent->child->next)
		 */
		Assert(baseNode == baseNode->parent->childData.children->next);

		if (baseNode->parent->childData.children == baseNode)
		{
			/* Case 1a: There's only 1 child (head == tail) */
			baseNode->parent->childData.children = newNode;
			newNode->next = newNode;
		}
		else
		{
			/* Case 1b: It's the 1st child of many children */
			/* Point the tail to the new node */
			baseNode->parent->childData.children->next = newNode;

			/* Point the new node's next to the base Node */
			newNode->next = baseNode->next;
		}
	}
	else
	{
		/* Case 2: Previous node is not null. This means that the current node is
		 * somewhere in the linked list (parent->child->next) */
		previousNode->next = newNode;
		newNode->next = baseNode->next;

		/* Replace the parent's child if it's the tail */
		if (baseNode == baseNode->parent->childData.children)
		{
			baseNode->parent->childData.children = newNode;
		}
	}
}


/*
 * Given a value at the end of a dotted path (e.g. a.b.c.d: <value>)
 * Determines the type of leaf that will be derived from the value.
 * This is then used when creating the leaf node.
 */
NodeType
DetermineChildNodeType(const bson_value_t *value,
					   bool forceLeafExpression)
{
	if (forceLeafExpression || !BsonValueIsNumberOrBool(value))
	{
		return NodeType_LeafField;
	}
	else
	{
		bool included = BsonValueAsDouble(value) != 0;
		return included ? NodeType_LeafIncluded : NodeType_LeafExcluded;
	}
}


/* Given a BsonIntermediatePathNode it first frees its subtree and then the node itself. */
void
FreeTree(BsonIntermediatePathNode *root)
{
	if (root == NULL)
	{
		return;
	}

	check_stack_depth();
	CHECK_FOR_INTERRUPTS();

	BsonPathNode *node;
	BsonPathNode *previousNode = NULL;

	foreach_child(node, root)
	{
		FreeBsonPathNode(previousNode);

		previousNode = node;
	}

	FreeBsonPathNode(previousNode);
	pfree(root);
}


/* Given a BsonPathNode it determines its type and frees it accordingly depending if it has children or not. */
static void
FreeBsonPathNode(BsonPathNode *node)
{
	check_stack_depth();
	CHECK_FOR_INTERRUPTS();

	if (node == NULL)
	{
		return;
	}

	if (node->nodeType == NodeType_LeafField)
	{
		pfree(node);
	}
	else if (node->nodeType == NodeType_LeafFieldWithContext)
	{
		BsonLeafNodeWithContext *leafNode = (BsonLeafNodeWithContext *) node;
		if (leafNode->context != NULL)
		{
			pfree(leafNode->context);
		}

		pfree(node);
	}
	else if (IsIntermediateNode(node))
	{
		BsonIntermediatePathNode *intermediateNode = (BsonIntermediatePathNode *) node;
		FreeTree(intermediateNode);
	}
	else if (node->nodeType == NodeType_LeafWithArrayField)
	{
		BsonLeafArrayWithFieldPathNode *leafArrayNode =
			(BsonLeafArrayWithFieldPathNode *) node;
		FreeLeafWithArrayFieldNodes(leafArrayNode);
	}
}


/* Given a BsonLeafArrayWithFieldPathNode it frees the node children first and then the node itself. */
static void
FreeLeafWithArrayFieldNodes(BsonLeafArrayWithFieldPathNode *leafArrayNode)
{
	if (leafArrayNode == NULL)
	{
		return;
	}

	check_stack_depth();
	CHECK_FOR_INTERRUPTS();

	BsonLeafPathNode *leafNode;
	BsonLeafPathNode *previousNode = NULL;
	foreach_array_child(leafNode, leafArrayNode)
	{
		if (previousNode != NULL)
		{
			pfree(previousNode);
		}

		previousNode = leafNode;
	}

	if (previousNode != NULL)
	{
		pfree(previousNode);
	}

	pfree(leafArrayNode);
}


/* Helper method to reset a node in the tree with the given value.
 * If forceLeafExpression = true, sets it as a field.
 * If forceLeafExpression = false, treats the value as inclusion/exclusion
 * of the node in the tree.
 * See DetermineChildNodeType for more details.
 *
 * Returns the created node.
 */
static const BsonPathNode *
ResetNodeWithValueOrField(const BsonLeafPathNode *baseLeafNode, const char *relativePath,
						  const bson_value_t *value, CreateLeafNodeFunc createFunc,
						  bool forceLeafExpression, bool treatLeafDataAsConstant,
						  void *nodeCreationState)
{
	NodeType leafNodeType = DetermineChildNodeType(value, forceLeafExpression);

	BsonPathNode *baseNode = (BsonPathNode *) baseLeafNode;
	BsonPathNode *leafPathNode = (BsonPathNode *) createFunc(
		&baseLeafNode->baseNode.field, relativePath,
		nodeCreationState);
	SetBasePathNodeData(leafPathNode, leafNodeType, &baseNode->field, baseNode->parent);
	ValidateAndSetLeafNodeData(leafPathNode, value, &baseNode->field,
							   treatLeafDataAsConstant);

	/* now fix up the child nodes */
	BsonPathNode *previousNode = NULL;
	BsonPathNode *currentNode;

	/* Find the previous node to the node asked for */
	bool found = false;
	foreach_child(currentNode, baseNode->parent)
	{
		if (currentNode == baseNode)
		{
			found = true;
			break;
		}

		previousNode = currentNode;
	}

	if (!found)
	{
		ereport(ERROR, (errcode(MongoInternalError), errmsg(
							"Unable to find base node in projection tree's children")));
	}

	ReplaceTreeInNodeCore(previousNode, baseNode, leafPathNode);
	return leafPathNode;
}


static BsonPathNode *
TraverseDottedPathAndGetOrAddNodeCore(const StringView *path,
									  const bson_value_t *value,
									  BsonIntermediatePathNode *tree,
									  CreateLeafNodeFunc createFunc,
									  CreateIntermediateNodeFunc
									  createIntermediateFunc,
									  bool forceLeafExpression,
									  bool treatNodeDataAsConstant,
									  void *nodeCreationState,
									  bool *nodeCreated)
{
	NodeType leafNodeType = DetermineChildNodeType(value, forceLeafExpression);
	bool replaceExistingNodes = false;
	bool alreadyExists = false;
	bool hasField = leafNodeType == NodeType_LeafField;
	BsonPathNode *childNode = TraverseDottedPathAndGetNode(path, tree, hasField,
														   createFunc,
														   createIntermediateFunc,
														   leafNodeType,
														   replaceExistingNodes,
														   nodeCreationState,
														   &alreadyExists);
	if (!alreadyExists)
	{
		ValidateAndSetLeafNodeData(childNode, value, path, treatNodeDataAsConstant);
	}

	if (nodeCreated)
	{
		*nodeCreated = !alreadyExists;
	}

	return childNode;
}


/*
 * Just like GetOrAddChildNode, GetOrAddLeafArrayChildNode creates a childNode
 * at the given path to a BsonLeafArrayWithFieldPathNode node ensuring that the
 * appropriate parent/child relationships in the tree are held
 */
static BsonLeafPathNode *
GetOrAddLeafArrayChildNode(BsonLeafArrayWithFieldPathNode *tree, const char *relativePath,
						   const StringView *fieldPath, CreateLeafNodeFunc createFunc,
						   NodeType childNodeType)
{
	BsonLeafPathNode *childNode;
	bool found = false;
	foreach_array_child(childNode, tree)
	{
		if (StringViewEquals(&childNode->baseNode.field, fieldPath))
		{
			found = true;
			break;
		}
	}

	if (!found)
	{
		void *nodeState = NULL;
		childNode = createFunc(fieldPath, relativePath, nodeState);
		SetBasePathNodeData(&childNode->baseNode, childNodeType, fieldPath, NULL);

		AddChildToTree(&tree->arrayChild, &childNode->baseNode);
	}

	return childNode;
}


/*
 * Method that creates a BsonLeafArrayWithFieldPathNode node.
 */
static BsonLeafPathNode *
CreateLeafNodeWithArrayField(const StringView *fieldPath, const char *relativePath,
							 void *state)
{
	/* Return a default leaf path node */
	BsonLeafArrayWithFieldPathNode *leafNode = (BsonLeafArrayWithFieldPathNode *) palloc0(
		sizeof(BsonLeafArrayWithFieldPathNode));
	return &leafNode->leafData;
}


/*
 * Builds a BsonIntermediatePathNode tree from a pgbson document.
 *
 * Parameters:
 *  - tree : The root node of the BsonIntermediatePathNode tree.
 *  - document : The pgbson document to build the tree from.
 */
void
BuildTreeFromPgbson(BsonIntermediatePathNode *tree, pgbson *document)
{
	bson_iter_t documentIterator;
	PgbsonInitIterator(document, &documentIterator);

	bool treatLeafDataAsConstant = true;
	while (bson_iter_next(&documentIterator))
	{
		StringView pathView = bson_iter_key_string_view(&documentIterator);
		const bson_value_t *docValue = bson_iter_value(&documentIterator);

		/* we assume the _id has already been added to the writer before calling this function */
		if (strcmp(pathView.string, "_id") == 0)
		{
			continue;
		}

		bool nodeCreated = false;
		TraverseDottedPathAndGetOrAddLeafFieldNode(
			&pathView, docValue,
			tree, BsonDefaultCreateLeafNode,
			treatLeafDataAsConstant, &nodeCreated);
	}
}
