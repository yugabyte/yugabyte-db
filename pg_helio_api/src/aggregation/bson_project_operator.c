/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bson/bson_project_operator.c
 *
 * Handler Functions for find projection operators
 *
 *-------------------------------------------------------------------------
 */
#include "aggregation/bson_project_operator.h"
#include "utils/helio_errors.h"
#include "types/decimal128.h"


typedef enum FindProjectionOperators
{
	FindProjectionOperators_Unspecified = 0,

	FindProjectionOperators_ElemMatch = 0x1,

	FindProjectionOperators_Positional = 0x2,

	FindProjectionOperators_Slice = 0x4,
} FindProjectionOperators;

/* Macros for operator flag handling */
#define IsElemMatchIncluded(x) ((x & FindProjectionOperators_ElemMatch) == \
								FindProjectionOperators_ElemMatch)
#define IsPositionalIncluded(x) ((x & FindProjectionOperators_Positional) == \
								 FindProjectionOperators_Positional)
#define IsSliceIncluded(x) ((x & FindProjectionOperators_Slice) == \
							FindProjectionOperators_Slice)
#define SetOperatorFlag(flag, operator) (flag = flag | operator)

/*
 * Metadata for the $elemMatch projection function
 */
typedef struct ElemMatchProjectionData
{
	/* Compiled elemMatch expression */
	ExprEvalState *elemMatchExprState;

	/* projection order index of $elemMatch Projection */
	uint32_t index;

	/* Whether or not the query is empty */
	bool isQueryEmpty;
} ElemMatchProjectionData;


/*
 * Metadata for the $slice projection function
 */
typedef struct SliceProjectionData
{
	/* number of element to be skipped from 0th index in array */
	int numToSkip;

	/* number of element to be return after skipping elements */
	int numToReturn;

	/* Whether any of the arguments inside array has null or undefined value. */
	bool isArgumentNullOrUndefined;
} SliceProjectionData;


/*
 * Metadata for the $ positional projection function
 */
typedef struct PositionalQueryData
{
	/* Opaque positional data wrapper to be used in execution */
	BsonPositionalQueryData *positionalQueryData;

	/* Whether or not the query is empty */
	bool isQueryEmpty;
} PositionalQueryData;


/*
 * An additional state to maintain the metadata for find projection
 */
typedef struct ProjectFindBsonPathTreeState
{
	/* Referrence to the only positional leaf we can have for $ projection
	 * Used to validate path collisions
	 */
	BsonPathNode *positionalLeaf;

	/* Whether or not current projection is positional projection */
	bool isPositional;

	/* query Spec for positional projection */
	pgbson *querySpec;

	/* Projection Operator flags that declare whether or not a projection operator is specified */
	FindProjectionOperators operatorExistenceFlag;

	/*
	 * Find projection operator state needed to evaluate the operator
	 * State is specific to these 3 operators => $elemMatch, $(positional), $slice
	 */
	PositionalQueryData *positionalOperatorState;

	/*
	 * Number of $elemMatch Projection
	 */
	uint32_t totalElemMatchProjections;
} ProjectFindBsonPathTreeState;

/*
 * Intermediate node for positional projection operators
 */
typedef struct BsonProjectionIntermediateNode
{
	/* Base intermediate node */
	BsonIntermediatePathNode base;

	/* Whehther or not the node has positional child */
	bool hasPositionalChild;

	/* If the node has positional child then this gets a referrence to the quals */
	/* This is stored in order to save traverse time to find the state */
	void *state;
} BsonProjectionIntermediateNode;


/*
 * State for pending projections that will be projected at the end
 */
typedef struct PendingProjectionState
{
	/*
	 * The context specific collection of pending projection
	 * e.g. $elemMatch maintains an array of projection based on projection spec
	 */
	pgbson_heap_writer **heapWriters;

	/* Total number of such pending projections */
	uint32_t totalPendingProjections;
} PendingProjectionState;


static void PositionalHandlerFunc(const bson_value_t *sourceValue,
								  const StringView *path,
								  pgbson_writer *writer,
								  ProjectDocumentState *projectDocState,
								  void *state, bool isInNestedArray);

static void ElemMatchHandlerFunc(const bson_value_t *sourceValue,
								 const StringView *path,
								 pgbson_writer *writer,
								 ProjectDocumentState *projectDocState,
								 void *state, bool isInNestedArray);
static void SliceHandlerFunc(const bson_value_t *sourceValue,
							 const StringView *path, pgbson_writer *writer,
							 ProjectDocumentState *projectDocState,
							 void *state, bool isInNestedArray);
static void WriteNewArrayForDollarSlice(int numToSkip,
										int numToReturn,
										const StringView *path,
										pgbson_writer *writer,
										bson_iter_t *srcArrayIter);


static void ValidatePositionalCollisions(BsonPathNode *positionalLeaf,
										 BsonPathNode *childNode,
										 bool isPositional,
										 const StringView *relativePath);
static void ValidateFindProjectionSpecAndSetNodeContext(BsonLeafPathNode *child,
														ProjectFindBsonPathTreeState *
														context,
														bool *isExclusionIfNoInclusion,
														bool *hasFieldsForIntermediate);

static void MatchPositionalAndAdvanceArrayIter(bson_iter_t *arrIter,
											   const pgbson *document,
											   const PositionalQueryData *state);

static void HandleSliceInputData(const bson_value_t *sliceOperatorValue,
								 SliceProjectionData *sliceData);
static BsonLeafPathNode * BsonCreateLeafNodeWithContext(const StringView *fieldPath,
														const char *relativePath,
														void *state);
static StringView PreProcessPathForFind(const StringView *path, const
										bson_value_t *pathValue,
										bool isOperator, bool *isFindOperator,
										void *state,
										bool *skipInclusionExclusionValidation);
static void ProcessAlreadyExistsNodeForFind(void *state, const StringView *path,
											BsonPathNode *node);
static void PostProcessLeafNodeForFind(void *state, const StringView *path,
									   BsonPathNode *node,
									   bool *isExclusionIfNoInclusion, bool *hasField);
static BsonIntermediatePathNode * BsonCreateIntermediateNodeForFind(const
																	StringView *fieldPath,
																	const char *
																	relativePath,
																	void *state);
static bool TryAdvanceArrayIteratorForFind(const BsonIntermediatePathNode *node,
										   ProjectDocumentState *state,
										   bson_iter_t *sourceValue);
static void WritePendingElemMatchProjections(pgbson_writer *writer,
											 void *pendingProjections);
static void * InitListForElemMatchProjections(uint32_t totalPendingProjections);

BuildBsonPathTreeFunctions FindPathTreeFunctions =
{
	.createLeafNodeFunc = BsonCreateLeafNodeWithContext,
	.createIntermediateNodeFunc = BsonCreateIntermediateNodeForFind,
	.postProcessLeafNodeFunc = PostProcessLeafNodeForFind,
	.preprocessPathFunc = PreProcessPathForFind,
	.validateAlreadyExistsNodeFunc = ProcessAlreadyExistsNodeForFind
};


void *
GetPathTreeStateForFind(pgbson *querySpec)
{
	ProjectFindBsonPathTreeState *findTreeState = palloc0(
		sizeof(ProjectFindBsonPathTreeState));
	findTreeState->querySpec = querySpec;
	return findTreeState;
}


int
PostProcessStateForFind(BsonProjectDocumentFunctions *projectDocumentFuncs,
						BuildBsonPathTreeContext *context)
{
	projectDocumentFuncs->tryMoveArrayIteratorFunc = TryAdvanceArrayIteratorForFind;

	ProjectFindBsonPathTreeState *findTreeState = context->pathTreeState;
	int totalEndProjections = findTreeState->totalElemMatchProjections;
	if (findTreeState->totalElemMatchProjections > 0 && !context->hasExclusion)
	{
		/*
		 * $elemMatch projection order seems to have
		 * multiple behaviors based on the projection context
		 *
		 * Source document => {a: [1,2,3], b: 1, c: [4,5,6], d: [7,8,9]}
		 *
		 * For inclusion projection:
		 *    1- elemMatch projections are projected at the end after all the other
		 *       inclusions
		 *       e.g.
		 *       Projection => {a: {$elemMatch: {$eq: 1}}, c: 1, b: 1}
		 *       Result => {b: 1, c: [4,5,6], a: [1]}
		 *
		 *    2- if multiple elemMatch projections are specified they will also be
		 *       projected at the end, but elemMatch projections should follow projection
		 *       spec order not the document order
		 *       e.g.
		 *       Projection => {
		 *          c: {$elemMatch: {$eq: 4}},
		 *          b: 1,
		 *          d: {$elemMatch: {$eq: 8}},
		 *          a: {$elemMatch: {$eq: 3}}
		 *       }
		 *       Result => {b: 1, c: [4], d: [8], a: [3]}
		 *
		 * For exclusion projection:
		 *     $elemMatch always follows the document order in this case
		 *     e.g.
		 *     Projection => {d: {$elemMatch: {$eq: 9}}, b: 0, a: {$elemMatch: {$eq: 1}}}
		 *     Result => {a: [1], c: [4,5,6], d: [9]}
		 *
		 * So we maintain a list in case of inclusion projection for the ordering
		 * & for exclusion projection list is not used and the projection is
		 * directly written to inorder writer
		 */
		projectDocumentFuncs->initializePendingProjectionFunc =
			InitListForElemMatchProjections;
		projectDocumentFuncs->writePendingProjectionFunc =
			WritePendingElemMatchProjections;
	}

	pfree(context->pathTreeState);
	return totalEndProjections;
}


/*
 * Tree Leaf creation method for Find Projection operators
 * i.e for $elemMatch, $positional & $slice
 */
static BsonLeafPathNode *
BsonCreateLeafNodeWithContext(const StringView *fieldPath, const char *relativePath,
							  void *state)
{
	/* Return a Find projection operator specific leaf path node with context */
	BsonLeafNodeWithContext *leafNode = palloc0(sizeof(BsonLeafNodeWithContext));
	leafNode->context = (ProjectionOpHandlerContext *) state;
	return &leafNode->base;
}


/*
 * Helper to cast a node as Intermediate Positional Projection node
 */
inline static const BsonProjectionIntermediateNode *
CastAsProjectionIntermediateNode(const BsonIntermediatePathNode *toCast)
{
	return (const BsonProjectionIntermediateNode *) toCast;
}


static inline void
pg_attribute_noreturn()
ThrowPositionalNotMatchedError()
{
	ereport(ERROR, (errcode(ERRCODE_HELIO_LOCATION51246),
					errmsg(
						"Executor error during find command :: caused by :: positional operator "
						"'.$' couldn't find a matching element in the array")));
}


static inline void
pg_attribute_noreturn()
ThrowPositionalMismatchedMatchedError()
{
	ereport(ERROR, (errcode(ERRCODE_HELIO_LOCATION51247),
					errmsg(
						"Executor error during find command :: caused by :: positional operator "
						"'.$' element mismatch")));
}


/*
 * Checks if the current path of spec is a positional projection operator
 */
static
StringView
PreProcessPathForFind(const StringView *path, const bson_value_t *pathValue,
					  bool isOperator, bool *isFindOperator, void *state,
					  bool *skipInclusionExclusionValidation)
{
	ProjectFindBsonPathTreeState *treeState = (ProjectFindBsonPathTreeState *) state;
	*skipInclusionExclusionValidation = false;
	if (StringViewEndsWithString(path, ".$"))
	{
		treeState->isPositional = true;
		*isFindOperator = true;
		return (StringView) {
				   .string = path->string, .length = path->length - 2
		};
	}
	else if (isOperator)
	{
		bson_iter_t valueIterator;
		BsonValueInitIterator(pathValue, &valueIterator);
		if (bson_iter_next(&valueIterator))
		{
			StringView key = bson_iter_key_string_view(&valueIterator);
			*isFindOperator =
				(StringViewEqualsCString(&key, "$slice") ||
				 StringViewEqualsCString(&key, "$elemMatch"));
			*skipInclusionExclusionValidation = *isFindOperator;
		}
	}

	treeState->isPositional = false;
	return *path;
}


/*
 * If a leaf node in the path spec tree already exists then we check for path collisions
 *
 * This method checks:
 * - If an existing regular path is overlapped with a positional leaf path, e.g. {a.b.c: 1, a.b.c.$: 1}
 * - If an exisitng positional path is overlapped with a regulat path, e.g. {a.b.c.$: 1, a.b.c: 1}
 */
static void
ProcessAlreadyExistsNodeForFind(void *state, const StringView *path, BsonPathNode *node)
{
	ProjectFindBsonPathTreeState *treeState = (ProjectFindBsonPathTreeState *) state;
	ValidatePositionalCollisions(treeState->positionalLeaf, node, treeState->isPositional,
								 path);
}


/*
 * Creates the intermediate nodes for positional leaf node
 * These nodes store the referrence to positional query quals which are used by the first array along the path
 */
BsonIntermediatePathNode *
BsonCreateIntermediateNodeForFind(const StringView *fieldPath, const
								  char *relativePath,
								  void *state)
{
	BsonProjectionIntermediateNode *node = palloc0(
		sizeof(BsonProjectionIntermediateNode));
	return &node->base;
}


/*
 * Try Advancing the existing array iterator to the matched index of positional quals
 */
static bool
TryAdvanceArrayIteratorForFind(const BsonIntermediatePathNode *node,
							   ProjectDocumentState *projectDocState,
							   bson_iter_t *sourceValue)
{
	const BsonProjectionIntermediateNode *intermediateNode =
		CastAsProjectionIntermediateNode(node);
	if (!intermediateNode->hasPositionalChild ||
		projectDocState->isPositionalAlreadyEvaluated)
	{
		return false;
	}

	MatchPositionalAndAdvanceArrayIter(sourceValue, projectDocState->parentDocument,
									   intermediateNode->state);
	projectDocState->isPositionalAlreadyEvaluated = true;
	return true;
}


/*
 * Initialized an array with all elemMatch Projection which holds
 * pgbson_writer for a particular elemMatch projection
 * based on projection spec order in an inclusion projection
 */
void *
InitListForElemMatchProjections(uint32_t totalPendingProjections)
{
	if (totalPendingProjections == 0)
	{
		return NULL;
	}

	PendingProjectionState *pendingProjectionState = palloc0(
		sizeof(PendingProjectionState));
	pendingProjectionState->heapWriters = palloc0(totalPendingProjections *
												  sizeof(pgbson_heap_writer *));
	pendingProjectionState->totalPendingProjections = totalPendingProjections;
	return pendingProjectionState;
}


/*
 * Concats the pending elemMatch projections at the end to main projection writer if not empty
 */
void
WritePendingElemMatchProjections(pgbson_writer *writer,
								 void *pendingProjectionState)
{
	PendingProjectionState *pendingProjections =
		(PendingProjectionState *) pendingProjectionState;
	pgbson_heap_writer **pendingProjectionList = pendingProjections->heapWriters;
	pgbson_heap_writer *pendingWriter;
	for (uint32_t index = 0; index < pendingProjections->totalPendingProjections; index++)
	{
		/* This already has the projected value if not empty ans also in the right order
		 * just concatenate and free */
		pendingWriter = (pgbson_heap_writer *) pendingProjectionList[index];

		if (pendingWriter == NULL)
		{
			continue;
		}

		if (!IsPgbsonHeapWriterEmptyDocument(pendingWriter))
		{
			/* Concat projections if not empty */
			PgbsonWriterConcatHeapWriter(writer, pendingWriter);

			/* Free writer */
			PgbsonHeapWriterFree(pendingWriter);
		}
		pfree(pendingWriter);
	}
}


/*
 * Post process the positional leaf node
 * - Compiles the query spec and generate the quals
 * - Updates the referrence of quals in all intermediate nodes till the positional leaf node
 * - Updates hasPositional flag in all intermediate nodes till the positional leaf node
 * - Sets hasExpressionFieldsInChildren to false in all intermediate nodes of positional leaf
 */
void
PostProcessLeafNodeForFind(void *state, const StringView *path, BsonPathNode *childNode,
						   bool *isExclusionIfNoInclusion, bool *hasFieldsForIntermediate)
{
	void *leafPositionalOperatorState = NULL;
	bool isLeafPositional = false;
	if (childNode->nodeType == NodeType_LeafFieldWithContext)
	{
		BsonLeafPathNode *leafPathNode = (BsonLeafPathNode *) childNode;
		ProjectFindBsonPathTreeState *treeState = (ProjectFindBsonPathTreeState *) state;

		if (treeState->isPositional &&
			treeState->positionalOperatorState == NULL)
		{
			treeState->positionalOperatorState = palloc(sizeof(PositionalQueryData));
			if (treeState->querySpec == NULL)
			{
				treeState->positionalOperatorState->isQueryEmpty = true;
			}
			else
			{
				treeState->positionalOperatorState->isQueryEmpty = IsPgbsonEmptyDocument(
					treeState->querySpec);
				treeState->positionalOperatorState->positionalQueryData =
					GetPositionalQueryData(treeState->querySpec);
			}
		}

		ValidateFindProjectionSpecAndSetNodeContext(
			leafPathNode, treeState, isExclusionIfNoInclusion, hasFieldsForIntermediate);
		if (treeState->isPositional)
		{
			if (treeState->positionalLeaf == NULL)
			{
				treeState->positionalLeaf = childNode;
			}
			isLeafPositional = true;
			leafPositionalOperatorState = treeState->positionalOperatorState;
		}

		/*
		 * Update the positional node state to all the intermediate nodes (if any)
		 * This is done for mainly for 2 reasons:
		 *  1) Any intermediate node can use positional quals if it is an array
		 *  2) If there is a need for an intermediate node to use the quals then we don't
		 *     want to traverse all the children and their children to look for the positional
		 *     state to use
		 *
		 * Also we update the count of LeafField Children count in order to handle array unresolved fields
		 * e.g:
		 * Source => a.b.c: [1,2,3]
		 * Projection => a.b.c.d.$: 1, a.b.c.d.e : {$isArray: $a.b.c}
		 *
		 * Requirement => d should only be written if there are any LeafFields, if there are none then it should not
		 * be written in final projection
		 */
		BsonProjectionIntermediateNode *node =
			(BsonProjectionIntermediateNode *) childNode->parent;
		while (node->base.baseNode.nodeType != NodeType_None)
		{
			if (NodeType_IsLeaf(node->base.baseNode.nodeType))
			{
				ereport(ERROR, (errmsg("unexpectedly got leaf node")));
			}

			node->state = leafPositionalOperatorState;
			node->hasPositionalChild = isLeafPositional;
			node = (BsonProjectionIntermediateNode *) node->base.baseNode.parent;
		}
	}
}


/*
 * Finds the first index based on the positional queryData
 * And then moves the current iterator to the matched index.
 *
 * This method also throws error if in case
 * - If the positional query quals doesn't match any index
 * - If the existing array size is less than the matched index
 */
static void
MatchPositionalAndAdvanceArrayIter(bson_iter_t *arrIter,
								   const pgbson *document,
								   const PositionalQueryData *state)
{
	if (state->isQueryEmpty)
	{
		ThrowPositionalNotMatchedError();
	}
	int32_t matchedIndex = MatchPositionalQueryAgainstDocument(state->positionalQueryData,
															   document);
	if (matchedIndex < 0)
	{
		ThrowPositionalNotMatchedError();
	}
	int32_t index = -1;

	while (index < matchedIndex && bson_iter_next(arrIter))
	{
		index++;
	}
	if (index < matchedIndex)
	{
		/* Index matched is greater than existing array size */
		ThrowPositionalMismatchedMatchedError();
	}
}


/*
 * This method is used to validate the projection spec for find operators $slice, $elemMatch, positional
 * If any of these operators are found then ProjectionOpHandlerContext is filled for the operators which
 * includes the handler for these operators. Also the finalNodeType is set to `LeafWithContext` for these operators
 *
 * Update isExclusionIfNoInclusion to true in case of $slice operator beacuse:
 * If projectionSpec has only $slice it will work as an exclusion projection &
 * it depends on the projection type of other fields in case multiple projection fields are specified
 * e.g, {a: {$slice: 2}} => This is exclusion projection
 * {a: {$slice: 2}, b: 0} => This is also exclusion projection
 * {a: {$slice: 2}, b: 1} => This is now inclusion projection
 * For other operators that can end up here e.g '$literal' Context is NULL and it is a no op here in this method
 */
static void
ValidateFindProjectionSpecAndSetNodeContext(BsonLeafPathNode *child,
											ProjectFindBsonPathTreeState *treeState,
											bool *isExclusionIfNoInclusion,
											bool *hasFieldsForIntermediate)
{
	BsonLeafNodeWithContext *leafNodeWithContext = (BsonLeafNodeWithContext *) child;
	leafNodeWithContext->context = NULL;

	if (child->fieldData.kind != AggregationExpressionKind_Constant)
	{
		/* If the node is not a constant, it can't be a projection/find operator. */
		return;
	}

	const bson_value_t *pathSpecValue = &child->fieldData.value;
	bson_iter_t operatorSpecIter;
	if (treeState->isPositional)
	{
		/* Positional operator */
		if (IsPositionalIncluded(treeState->operatorExistenceFlag))
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_LOCATION31276),
							errmsg(
								"Cannot specify more than one positional projection per query.")));
		}
		if (pathSpecValue->value_type == BSON_TYPE_DOCUMENT)
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_LOCATION31271),
							errmsg("positional projection cannot be used with "
								   "an expression or sub object")));
		}
		if (!BsonValueIsNumberOrBool(pathSpecValue))
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_LOCATION31308),
							errmsg(
								"positional projection cannot be used with a literal")));
		}
		else if (BsonValueAsDouble(pathSpecValue) == 0)
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_LOCATION31395),
							errmsg(
								"positional projection cannot be used with exclusion")));
		}
		else if (IsElemMatchIncluded(treeState->operatorExistenceFlag))
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_LOCATION31256),
							errmsg(
								"Cannot specify positional operator and $elemMatch.")));
		}

		ProjectionOpHandlerContext *projectionOpHandlerContext = palloc0(
			sizeof(ProjectionOpHandlerContext));

		/* Get the positional query state */
		projectionOpHandlerContext->projectionOpHandlerFunc = PositionalHandlerFunc;
		SetOperatorFlag(treeState->operatorExistenceFlag,
						FindProjectionOperators_Positional);

		projectionOpHandlerContext->state = treeState->positionalOperatorState;
		leafNodeWithContext->context = (void *) projectionOpHandlerContext;

		/* ElemMatch doesn't count towards field calculation for intermrediate nodes */
		*hasFieldsForIntermediate = false;
	}
	else if (pathSpecValue->value_type == BSON_TYPE_DOCUMENT)
	{
		BsonValueInitIterator(pathSpecValue, &operatorSpecIter);

		/* In this case this would be either $slice or $elemMatch */
		bson_iter_next(&operatorSpecIter);
		const char *operatorKey = bson_iter_key(&operatorSpecIter);
		const bson_value_t *operatorValue = bson_iter_value(&operatorSpecIter);
		if (strcmp(operatorKey, "$elemMatch") == 0)
		{
			if (child->baseNode.parent != NULL &&
				child->baseNode.parent->baseNode.parent != NULL)
			{
				ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE),
								errmsg(
									"Cannot use $elemMatch projection on a nested field.")));
			}
			else if (operatorValue->value_type != BSON_TYPE_DOCUMENT)
			{
				ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE),
								errmsg(
									"elemMatch: Invalid argument, object required, but got %s",
									BsonTypeName(operatorValue->value_type))));
			}
			else if (IsPositionalIncluded(treeState->operatorExistenceFlag))
			{
				ereport(ERROR, (errcode(ERRCODE_HELIO_LOCATION31256),
								errmsg(
									"Cannot specify positional operator and $elemMatch.")));
			}

			bson_iter_t elemMatchValueIter, jsonSchemaIter;
			if (bson_iter_recurse(&operatorSpecIter, &elemMatchValueIter) &&
				bson_iter_find_descendant(&elemMatchValueIter, "$jsonSchema",
										  &jsonSchemaIter))
			{
				/*
				 * $jsonSchema can't be given in a $elemMatch projection operation context
				 */
				ereport(ERROR, (errcode(ERRCODE_HELIO_QUERYFEATURENOTALLOWED),
								errmsg(
									"$jsonSchema is not allowed in this context")));
			}

			ProjectionOpHandlerContext *projectionOpHandlerContext = palloc0(
				sizeof(ProjectionOpHandlerContext));
			projectionOpHandlerContext->projectionOpHandlerFunc = ElemMatchHandlerFunc;

			ElemMatchProjectionData *elemMatchState = palloc0(
				sizeof(ElemMatchProjectionData));

			elemMatchState->isQueryEmpty = IsBsonValueEmptyDocument(
				operatorValue);

			/* ElemMatch state to hold the compiled expression as well as record the index in projection spec */
			elemMatchState->index = treeState->totalElemMatchProjections++;
			elemMatchState->elemMatchExprState = GetExpressionEvalState(
				operatorValue,
				CurrentMemoryContext);
			SetOperatorFlag(treeState->operatorExistenceFlag,
							FindProjectionOperators_ElemMatch);
			projectionOpHandlerContext->state = elemMatchState;

			leafNodeWithContext->context = (void *) projectionOpHandlerContext;

			/* ElemMatch doesn't count towards field calculation for intermrediate nodes */
			*hasFieldsForIntermediate = false;
		}
		else if (strcmp(operatorKey, "$slice") == 0)
		{
			ProjectionOpHandlerContext *projectionOpHandlerContext = palloc0(
				sizeof(ProjectionOpHandlerContext));

			SliceProjectionData *sliceData = palloc0(sizeof(SliceProjectionData));

			projectionOpHandlerContext->projectionOpHandlerFunc = SliceHandlerFunc;
			projectionOpHandlerContext->state = sliceData;

			SetOperatorFlag(treeState->operatorExistenceFlag,
							FindProjectionOperators_Slice);
			leafNodeWithContext->context = (void *) projectionOpHandlerContext;
			*isExclusionIfNoInclusion = true;

			HandleSliceInputData(operatorValue, sliceData);

			/* ElemMatch doesn't count towards field calculation for intermrediate nodes */
			*hasFieldsForIntermediate = false;
		}
	}
}


/*
 * $ positional handler function
 */
static void
PositionalHandlerFunc(const bson_value_t *sourceValue,
					  const StringView *path,
					  pgbson_writer *writer,
					  ProjectDocumentState *projectDocState,
					  void *state, bool isInNestedArray)
{
	/*
	 * Project the original value if:
	 *  - Source value is not array
	 *  - Positional query is already used by parent array
	 */
	if (sourceValue->value_type != BSON_TYPE_ARRAY ||
		projectDocState->isPositionalAlreadyEvaluated)
	{
		PgbsonWriterAppendValue(writer, path->string, path->length, sourceValue);
		return;
	}
	bson_iter_t sourceArrIter;
	BsonValueInitIterator(sourceValue, &sourceArrIter);
	MatchPositionalAndAdvanceArrayIter(&sourceArrIter, projectDocState->parentDocument,
									   state);

	/* Write the iterator value as a single element in array */
	PgbsonWriterAppendBsonValueAsArray(writer, path->string,
									   path->length,
									   bson_iter_value(&sourceArrIter));
}


/*
 * $elemMatch projection op handler function
 *
 * In an exclusion projection the matched elements are written to main `writer` which follows
 * document order.
 * Otherwise a new pgbson_writer is allocated and stored in a list in the same order of projection spec
 * to match native mongo behavior
 */
static void
ElemMatchHandlerFunc(const bson_value_t *sourceValue,
					 const StringView *path,
					 pgbson_writer *writer,
					 ProjectDocumentState *projectDocState,
					 void *state, bool isInNestedArray)
{
	ElemMatchProjectionData *nodeState = (ElemMatchProjectionData *) state;
	bool shouldWriteAtEnd = !projectDocState->hasExclusion &&
							projectDocState->pendingProjectionState != NULL;
	pgbson_heap_writer *heapWriter = NULL;
	if (shouldWriteAtEnd)
	{
		heapWriter = PgbsonHeapWriterInit();
	}

	if (sourceValue->value_type == BSON_TYPE_ARRAY)
	{
		if (nodeState->isQueryEmpty)
		{
			/* An empty elemMatch only matches documents and arrays */
			bson_iter_t arrayIter;
			BsonValueInitIterator(sourceValue, &arrayIter);
			while (bson_iter_next(&arrayIter))
			{
				if (bson_iter_type(&arrayIter) == BSON_TYPE_DOCUMENT ||
					bson_iter_type(&arrayIter) == BSON_TYPE_ARRAY)
				{
					if (shouldWriteAtEnd)
					{
						PgbsonHeapWriterAppendBsonValueAsArray(heapWriter,
															   path->string,
															   path->length,
															   bson_iter_value(
																   &arrayIter));
					}
					else
					{
						PgbsonWriterAppendBsonValueAsArray(writer,
														   path->string,
														   path->length,
														   bson_iter_value(&arrayIter));
					}
					break;
				}
			}
		}
		else
		{
			bson_value_t matchedValue = EvalExpressionAgainstArrayGetFirstMatch(
				nodeState->elemMatchExprState,
				sourceValue);
			if (matchedValue.value_type != BSON_TYPE_EOD)
			{
				if (shouldWriteAtEnd)
				{
					PgbsonHeapWriterAppendBsonValueAsArray(heapWriter,
														   path->string,
														   path->length,
														   &matchedValue);
				}
				else
				{
					PgbsonWriterAppendBsonValueAsArray(writer,
													   path->string,
													   path->length,
													   &matchedValue);
				}
			}
		}
	}

	/* Save the writer in the list if projections are saved for end */
	if (shouldWriteAtEnd)
	{
		PendingProjectionState *pendingState = projectDocState->pendingProjectionState;
		uint32_t index = nodeState->index;
		pendingState->heapWriters[index] = heapWriter;
	}
}


/*
 * Validate if the new node which is added has not collided with positional
 * There are 2 cases to check, when positiona is first or last:
 * 1- {...a: 1, ...a.$: 1}
 * 2- {...a.$: 1, ...a: 1}
 *
 * It iterates over the children of positionalLeaf->parent and validates if
 * postional leaf is still part of the list because in case of leaf collision this is replaced
 */
static void
ValidatePositionalCollisions(BsonPathNode *positionalLeaf,
							 BsonPathNode *childNode,
							 bool isPositional,
							 const StringView *relativePath)
{
	if (isPositional)
	{
		/* If the current postional leaf node already exists, error */
		ereport(ERROR, (errcode(ERRCODE_HELIO_LOCATION31250),
						errmsg("Path collision at %.*s", relativePath->length,
							   relativePath->string)));
	}

	if (positionalLeaf != NULL)
	{
		const BsonPathNode *child;
		foreach_child(child, positionalLeaf->parent)
		{
			if (child == positionalLeaf)
			{
				return;
			}
		}

		/* Exisiting leaf node is replaced: error */
		ereport(ERROR, (errcode(ERRCODE_HELIO_LOCATION31250),
						errmsg("Path collision at %.*s", relativePath->length,
							   relativePath->string)));
	}
}


/*
 * $slice projection op handler function.
 * @sourceValue : source value on which $slice need to apply.
 * @parentDocument : Unused in this function, Needed for PositionalHandlerFunc.
 * @path : path to project.
 * @writer: pgbson_writer to write sliced array.
 * @endProjectioWriters : Unused in this function, Needed for ElemMatchHandlerFunc.
 * @state : holds sliceOperator value.
 */
static void
SliceHandlerFunc(const bson_value_t *sourceValue,
				 const StringView *path, pgbson_writer *writer,
				 ProjectDocumentState *projectDocState,
				 void *state, bool isInNestedArray)
{
	SliceProjectionData *nodeState = (SliceProjectionData *) state;

	/* if one of the argument of slice is null or undefined - the output is null.*/
	if (nodeState->isArgumentNullOrUndefined)
	{
		PgbsonWriterAppendNull(writer, path->string, path->length);
		return;
	}

	if (isInNestedArray || sourceValue->value_type != BSON_TYPE_ARRAY)
	{
		PgbsonWriterAppendValue(writer, path->string, path->length, sourceValue);
		return;
	}

	int numToSkip = nodeState->numToSkip;
	int numToReturn = nodeState->numToReturn;
	bson_iter_t arrayIter;

	BsonValueInitIterator(sourceValue, &arrayIter);
	int sourceArrayLength = BsonDocumentValueCountKeys(sourceValue);

	if (numToReturn < 0)
	{
		numToReturn = (-numToReturn);
		numToSkip = sourceArrayLength > numToReturn ? sourceArrayLength - numToReturn : 0;
	}
	else if (numToSkip < 0)
	{
		numToSkip = sourceArrayLength > abs(numToSkip) ? sourceArrayLength + numToSkip :
					0;
	}

	if (numToSkip < sourceArrayLength)
	{
		WriteNewArrayForDollarSlice(numToSkip, numToReturn,
									path, writer,
									&arrayIter);
	}
	else
	{
		PgbsonWriterAppendEmptyArray(writer, path->string, path->length);
	}
}


/* write a new array to pgbson_element_writer from numToSkip to numToReturn */
static void
WriteNewArrayForDollarSlice(int numToSkip, int numToReturn,
							const StringView *path, pgbson_writer *writer,
							bson_iter_t *srcArrayIter)
{
	pgbson_element_writer objectElementWriter;
	PgbsonInitObjectElementWriter(writer, &objectElementWriter,
								  path->string,
								  path->length);

	pgbson_array_writer arrayElementwriter;
	pgbson_element_writer innerWriter;
	PgbsonElementWriterStartArray(&objectElementWriter, &arrayElementwriter);
	PgbsonInitArrayElementWriter(&arrayElementwriter, &innerWriter);

	while (numToSkip > 0 && bson_iter_next(srcArrayIter))
	{
		numToSkip--;
	}

	while (bson_iter_next(srcArrayIter) && numToReturn > 0)
	{
		const bson_value_t *tmpVal = bson_iter_value(srcArrayIter);
		PgbsonElementWriterWriteValue(&innerWriter, tmpVal);
		numToReturn--;
	}

	PgbsonElementWriterEndArray(&objectElementWriter, &arrayElementwriter);
}


/* read $slice input and fill the SliceProjectionData so that can be used into SliceHandler function  */
static void
HandleSliceInputData(const bson_value_t *sliceOperatorValue,
					 SliceProjectionData *sliceData)
{
	bool checkFixedInteger = false;

	int *numToSkip = &sliceData->numToSkip;
	int *numToReturn = &sliceData->numToReturn;

	if (BsonValueIsNumber(sliceOperatorValue))
	{
		if (IsBsonValueNaN(sliceOperatorValue))
		{
			*numToSkip = INT32_MAX;
			*numToReturn = 0;
			return;
		}

		if (IsBsonValueInfinity(sliceOperatorValue))
		{
			*numToSkip = 0;
			*numToReturn = INT32_MAX;
			return;
		}

		if (!IsBsonValue32BitInteger(sliceOperatorValue, checkFixedInteger))
		{
			*numToReturn = INT32_MAX;
		}
		else
		{
			*numToReturn = BsonValueAsInt32(sliceOperatorValue);
		}

		*numToSkip = 0;
	}
	else if (sliceOperatorValue->value_type == BSON_TYPE_ARRAY)
	{
		bson_iter_t operatorValueIter;
		BsonValueInitIterator(sliceOperatorValue, &operatorValueIter);
		int sliceArrayLength = BsonDocumentValueCountKeys(sliceOperatorValue);
		*numToReturn = 0;

		if (sliceArrayLength != 2)
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_RANGEARGUMENTEXPRESSIONARGSOUTOFRANGE),
							errmsg("$slice takes 2 arguments, but %d were passed in.",
								   sliceArrayLength)));
		}

		if (bson_iter_next(&operatorValueIter))
		{
			const bson_value_t *numToSkipBsonVal;
			numToSkipBsonVal = bson_iter_value(&operatorValueIter);

			if (numToSkipBsonVal->value_type == BSON_TYPE_NULL ||
				numToSkipBsonVal->value_type == BSON_TYPE_UNDEFINED)
			{
				sliceData->isArgumentNullOrUndefined = true;
				return;
			}

			if (!BsonValueIsNumber(numToSkipBsonVal))
			{
				ereport(ERROR, (errcode(ERRCODE_HELIO_DOLLARSLICEINVALIDINPUT), errmsg(
									"First argument to $slice must be an number, but is of type: %s",
									BsonTypeName(numToSkipBsonVal->value_type))));
			}

			int bsonValPosOrNegInfinity = IsBsonValueInfinity(numToSkipBsonVal);
			if (IsBsonValueNaN(numToSkipBsonVal) || bsonValPosOrNegInfinity == -1)
			{
				*numToSkip = 0;
			}
			else if (bsonValPosOrNegInfinity == 1 ||
					 !IsBsonValue32BitInteger(numToSkipBsonVal, checkFixedInteger))
			{
				*numToSkip = INT32_MAX;
			}
			else
			{
				*numToSkip = BsonValueAsInt32(numToSkipBsonVal);
			}
		}

		if (bson_iter_next(&operatorValueIter))
		{
			const bson_value_t *numToReturnBsonVal;
			numToReturnBsonVal = bson_iter_value(&operatorValueIter);

			if (numToReturnBsonVal->value_type == BSON_TYPE_NULL ||
				numToReturnBsonVal->value_type == BSON_TYPE_UNDEFINED)
			{
				sliceData->isArgumentNullOrUndefined = true;
				return;
			}

			int bsonValPosOrNegInfinity = IsBsonValueInfinity(numToReturnBsonVal);

			if (!BsonValueIsNumber(numToReturnBsonVal) || IsBsonValueNaN(
					numToReturnBsonVal) || bsonValPosOrNegInfinity == -1)
			{
				ereport(ERROR, (errcode(ERRCODE_HELIO_DOLLARSLICEINVALIDINPUT), errmsg(
									"Second argument to $slice must be a positive number, but is of type: %s",
									BsonTypeName(numToReturnBsonVal->value_type))));
			}

			if (bsonValPosOrNegInfinity == 1 ||
				!IsBsonValue32BitInteger(numToReturnBsonVal, checkFixedInteger))
			{
				*numToReturn = INT32_MAX;
			}
			else
			{
				*numToReturn = BsonValueAsInt32(numToReturnBsonVal);
			}

			if (*numToReturn <= 0)
			{
				ereport(ERROR, (errcode(ERRCODE_HELIO_DOLLARSLICEINVALIDINPUT), errmsg(
									"Second argument to $slice must be a positive number")));
			}
		}
	}
	else
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_RANGEARGUMENTEXPRESSIONARGSOUTOFRANGE),
						errmsg(
							"First argument to $slice must be an number, but is of type: %s",
							BsonTypeName(sliceOperatorValue->value_type))));
	}
}
