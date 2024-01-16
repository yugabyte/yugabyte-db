/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bson/bson_update_operators_workflow.c
 *
 * Implementation of the update operation top level workflow for update operators.
 * This is intended as a staging ground for the new update pipeline.
 * When all update operators are supported here, then we can move this into
 * bson_update.c
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <miscadmin.h>
#include <math.h>
#include <utils/builtins.h>
#include <common/hashfn.h>
#include <nodes/bitmapset.h>

#include "update/bson_update_common.h"
#include "query/helio_bson_compare.h"
#include "utils/mongo_errors.h"
#include "aggregation/bson_tree.h"
#include "operators/bson_expr_eval.h"
#include "update/bson_update_operators.h"
#include "aggregation/bson_positional_query.h"
#include "utils/hashset_utils.h"
#include "commands/commands_common.h"

#include "api_hooks_def.h"

/* --------------------------------------------------------- */
/* Data types */
/* --------------------------------------------------------- */

static const StringView PositionalType_AllString = { .string = "$[]", .length = 3 };
static const StringView PositionalFilterString = { .string = "$", .length = 1 };

/* WriteUpdatedValuesFunc function takes an existing value in the current document,
 * applies the update mutation pertinent to that operator, and writes the updated
 * value to the writer
 */
typedef void (*WriteUpdatedValuesFunc)(const bson_value_t *existingValue,
									   UpdateOperatorWriter *writer,
									   const bson_value_t *updateValue,
									   void *updateNodeContext,
									   const UpdateSetValueState *setValueState,
									   const CurrentDocumentState *state);

/*
 * TREE DATA STRUCTURES
 * The following data structures are used to build the Update tree.
 * Each intermediate node in the tree is of type
 * BsonUpdateIntermediatePathNode, which stores positional data, the
 * update children and any precomputed values to make traversing the
 * document simpler (e.g. whether it has renames, unsets, etc).
 * Each leaf node is of type BsonUpdateLeafNode and contains a function
 * pointer to the function that will mutate an existing value in the document
 * at the dotted path and produce the mutated value. Leaf nodes also contain
 * positional data since positional can occur at any place.
 * PositionalData tracks any pertinent information needed to apply a positional
 * filter. For PositionalType_None & PositionalType_All - it has nothing.
 * For PositionalType_Query/ArrayFilter it will contain the compiled query
 * expression to be evaluated at every path.
 *
 */


/* The type of positional operator a node has */
typedef enum PositionalType
{
	/* It is not a positional update */
	PositionalType_None = 0,

	/* Corresponds to the $[] path */
	PositionalType_All,

	/* Corresponds to the $ path */
	PositionalType_QueryFilter,

	/* Corresponds to the $[identifier] path */
	PositionalType_ArrayFilter
} PositionalType;

/* Metadata about positional nodes (if any) */
typedef struct PositionalData
{
	/* The type of positional operator */
	PositionalType type;

	union
	{
		/*
		 * The query expression pertinent to this
		 * positional expression. This is null
		 * if the positional node doesn't need
		 * a query expression
		 */
		ExprEvalState *expression;

		/*
		 * QueryData pertinent to the positional $ operator
		 * if any.
		 */
		BsonPositionalQueryData *positionalQueryData;
	};
} PositionalData;


/* Data for an update leaf node in the tree */
typedef struct BsonUpdateLeafNode
{
	/* The base node for this path */
	BsonLeafPathNode base;

	/* The relative dotted path from the root to this leaf node */
	const char *relativePath;

	/* Specific leaf writer function pertinent to this node */
	WriteUpdatedValuesFunc writeFunc;

	/* Any optional context specific state needed for the node */
	void *updateNodeContext;

	/* The positional update data for this node */
	PositionalData positionalData;
} BsonUpdateLeafNode;


/* Data for an intermediate positional path node in the tree */
typedef struct BsonUpdateIntermediatePathNode
{
	/* The base node for this path */
	BsonIntermediatePathNode base;

	/* The relative dotted path from the root to this intermediate
	 * node */
	const char *relativePath;

	/* The positional update data for this node */
	PositionalData positionalData;

	/* Whether or not the direct children of this node are positional */
	bool hasPositionalChildren;

	/* Whether or not the children tree has children that include values */
	bool HasIntermediateIncludeChildren;

	/* In case of $rename operator: for source node, link target node and for target node link source node
	 * Other than $rename operator this value will be NULL
	 */
	const BsonPathNode *sourceOrTargetNodeForRenameOP;
} BsonUpdateIntermediatePathNode;

/*
 * TREE BUILDING DATA STRUCTURES
 * These are temporary stack allocated data structures used to pass information
 * to any extension methods used in building the tree. PositionalUpdateSpec,
 * and BsonUpdateTreeState are constructed before the call to
 * TraverseDottedPath* to get the tree node. As the bson_tree visits any
 * intermediate or leaf nodes, the callbacks that generate each node are provided
 * this state so that each node in the tree can be constructed with the appropriate
 * state.
 */

/*
 * The array filter declaration. This contains the element
 * key and the aggregated query filter expression for the
 * identifier that can be used for positional evaluation.
 */
typedef struct QueryFilterPathAndValue
{
	/* The identifier for the queryFilter - the key (must be the first value) */
	StringView identifier;

	/* The value for the query */
	bson_value_t queryValue;

	/* The top level bson that the queryValue points to */
	pgbson *queryBson;

	/* Whether or not the filter was used */
	bool filterUsed;
} QueryFilterPathAndValue;


/* Any positional metadata available during building the update spec for
 * target documents */
typedef struct PositionalUpdateSpec
{
	/* The input query spec - used when evaluating $ positional operators */
	pgbson *querySpec;

	/* hashmap of char* to bson_value_t* */
	HTAB *arrayFilters;

	/* The processed positional query data from the original querySpec */
	BsonPositionalQueryData *processedQuerySpec;
} PositionalUpdateSpec;

/*
 * State passed during the tree building phase to allow for the tree-nodes
 * to be constructed correctly.
 */
typedef struct BsonUpdateTreeState
{
	/* The function that the update operator will apply on the target document */
	WriteUpdatedValuesFunc updateFunc;

	/* Any associated state with the update tree */
	void *updateFuncState;

	const PositionalUpdateSpec *positionalSpec;
} BsonUpdateTreeState;

/* OPERATOR DEFINITIONS
 * These types specify how to build operators and define all supported
 * update operators.
 */

/*
 * An optional function to retrieve operator specific state given a specific
 * updateSpec value.
 */
typedef void *(*UpdateOperatorGetFuncState)(const bson_value_t *tree);

/*
 * HandleUpdateOperatorUpdateBsonTree takes a specific update operator document
 * and constructs the update tree. The function is also given a pointer to the
 * function that will apply the update on the target document - this can be cached
 * into the tree so we don't need to look up this data again.
 */
typedef void (*HandleUpdateOperatorUpdateBsonTree)(BsonIntermediatePathNode *tree,
												   bson_iter_t *updateSpec,
												   WriteUpdatedValuesFunc updateFunc,
												   UpdateOperatorGetFuncState stateFunc,
												   const PositionalUpdateSpec *
												   positionalSpec);

/* The declaration of the Mongo update operators */
typedef struct
{
	/* The name of the update operator e.g. $set */
	const char *operatorName;

	/* Function that handles parsing the update Spec for the operator
	 * and updates the tree with the set of paths being updated
	 */
	HandleUpdateOperatorUpdateBsonTree updateTreeFunc;

	/* Function that writes the updated values into the target writer
	 * for a given document
	 */
	WriteUpdatedValuesFunc updateWriterFunc;

	/* An optional function for retreiving state pertinent to the
	 * update node value */
	UpdateOperatorGetFuncState updateWriterGetState;
} MongoUpdateOperatorSpec;


/* OPERATOR WRITER TYPES:
 * These data types are stack allocated data types that are
 * passed to update operators to write state into the
 * target document during the application of the update.
 * UpdateOperatorWriter is the top level field writer (contains a
 * pgbson_element_writer) that writes to the target document;
 * It also contains a pointer to the BsonUpdateTracker that will
 * update the changestream specification. For array operators,
 * the UpdateArrayWriter will handle propagating array updates
 * to the target writer.
 */


/* A wrapper that allows update operators
 * to write out an array into the target
 * document, but also tracks state necessary
 * for handling modifications and change-streams
 */
typedef struct UpdateArrayWriter
{
	/* the actual array writer */
	pgbson_array_writer writer;

	/* Whether or not the array writer is valid */
	bool isValid;

	/* Whether or not the array writer modified something */
	bool modified;
} UpdateArrayWriter;


/* A wrapper that allows update operators
 * to write out an results into the target
 * document, but also tracks state necessary
 * for handling modifications and change-streams
 */
typedef struct UpdateOperatorWriter
{
	/* The actual element writer to write to */
	pgbson_element_writer *writer;

	/* An optional update tracker to dispatch updates to */
	BsonUpdateTracker *updateTracker;

	/* the relative path to the node being written */
	const char *relativePath;

	/* whether or not the value was modified */
	bool modified;

	/* the underlying array writer if one is requested */
	UpdateArrayWriter updateArrayWriter;
} UpdateOperatorWriter;

/* --------------------------------------------------------- */
/* Global hooks */
/* --------------------------------------------------------- */
NotifyUpdatedField_HookType notify_updated_field_hook = NULL;
NotifyUpdatedFieldPathView_HookType notify_updated_field_path_view_hook = NULL;
NotifyRemovedField_HookType notify_remove_field_hook = NULL;

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */

static void ReadUpdateSpecAndUpdateTree(bson_iter_t *updateIterator,
										BsonIntermediatePathNode *root,
										const PositionalUpdateSpec *positionalSpec);

/* Tree building functions */
static void ValidateSpecPathForUpdateTree(const StringView *updatePath);
static bool UpdateParentDataInTree(const BsonPathNode *node, bool markAsIncludeAncestor,
								   const BsonPathNode *sourceOrTargetNodeForRenameOP);
static void HandleBasicUpdateTree(BsonIntermediatePathNode *tree, bson_iter_t *updateSpec,
								  WriteUpdatedValuesFunc valuesFunc,
								  UpdateOperatorGetFuncState stateFunc,
								  const PositionalUpdateSpec *positionalSpec);
static void HandleRenameUpdateTree(BsonIntermediatePathNode *tree,
								   bson_iter_t *updateSpec,
								   WriteUpdatedValuesFunc valuesFunc,
								   UpdateOperatorGetFuncState stateFunc,
								   const PositionalUpdateSpec *positionalSpec);
static void HandleUnsetUpdateTree(BsonIntermediatePathNode *tree,
								  bson_iter_t *updateSpec,
								  WriteUpdatedValuesFunc valuesFunc,
								  UpdateOperatorGetFuncState stateFunc,
								  const PositionalUpdateSpec *positionalSpec);
static BsonIntermediatePathNode * CreateUpdateIntermediateNode(const StringView *path,
															   const char *relativePath,
															   void *state);
static BsonLeafPathNode * CreateUpdateLeafNode(const StringView *path, const
											   char *relativePath,
											   void *state);
static const BsonPathNode * HandleNodeExistsInTree(const BsonPathNode *existing,
												   const char *updatePath,
												   const bson_value_t *value,
												   BsonUpdateTreeState *updateTreeState);
static void ValidateNodePathInTree(const StringView *path, const char *relativePath);
static PositionalData GetNodePositionalDataFromPath(const StringView *path,
													const BsonUpdateTreeState *state);

/* Positional filter expression functions */
static uint32_t ArrayFiltersKeyHash(const void *key, Size keysize);
static int ArrayFiltersKeyCompare(const void *obj1, const void *obj2, Size objsize);
static void WriteCurrentArrayFilterValue(pgbson_writer *writer, bson_value_t *entryValue);
static HTAB * BuildExpressionForArrayFilters(pgbson *arrayFilters);
static void PostValidateArrayFilters(HTAB *arrayFiltersHash, pgbson *updateSpec);

/* Value writer functions */
static bool HandleUpdateDocumentId(pgbson_writer *writer,
								   const BsonUpdateIntermediatePathNode *root,
								   const CurrentDocumentState *state);
static bool TraverseDocumentAndApplyUpdate(bson_iter_t *sourceDocIterator,
										   pgbson_writer *writer,
										   const BsonUpdateIntermediatePathNode *tree,
										   bool isRootLevel,
										   const CurrentDocumentState *state,
										   BsonUpdateTracker *tracker,
										   bool hasArrayAncestors);
static bool TraverseArrayAndApplyUpdate(bson_iter_t *sourceDocIterator,
										pgbson_array_writer *writer,
										const BsonUpdateIntermediatePathNode *tree,
										const CurrentDocumentState *state,
										BsonUpdateTracker *tracker);
static bool HandleCurrentIteratorPosition(bson_iter_t *documentIterator,
										  const BsonUpdateIntermediatePathNode *tree,
										  pgbson_element_writer *writer,
										  Bitmapset **fieldHandledBitmapSet,
										  const CurrentDocumentState *state,
										  BsonUpdateTracker *tracker,
										  bool isArray, bool hasArrayAncestors,
										  StringView *fieldPath);
static bool IsNodeMatchForIteratorPath(const BsonPathNode *node,
									   const StringView *fieldPath,
									   bool isArray,
									   const CurrentDocumentState *state,
									   const bson_value_t *currentValue);
static bool HandleUnresolvedDocumentFields(const BsonUpdateIntermediatePathNode *tree,
										   Bitmapset *fieldBitMapSet,
										   pgbson_writer *writer,
										   bool isRootLevel,
										   const CurrentDocumentState *state,
										   BsonUpdateTracker *tracker,
										   bool hasArrayAncestors);
static bool HandleUnresolvedArrayFields(const BsonUpdateIntermediatePathNode *tree,
										int currentMaxIndex,
										pgbson_element_writer *arrayElementWriter,
										const CurrentDocumentState *state,
										BsonUpdateTracker *tracker);

/* Operator specific writer state functions */
static void * HandlePullWriterGetState(const bson_value_t *tree);

/* Operator writer functions */

static MongoUpdateOperatorSpec MongoUpdateOperators[] =
{
	{ "$set", HandleBasicUpdateTree, HandleUpdateDollarSet, NULL },
	{ "$inc", HandleBasicUpdateTree, HandleUpdateDollarInc, NULL },
	{ "$min", HandleBasicUpdateTree, HandleUpdateDollarMin, NULL },
	{ "$max", HandleBasicUpdateTree, HandleUpdateDollarMax, NULL },
	{ "$unset", HandleUnsetUpdateTree, HandleUpdateDollarUnset, NULL },
	{ "$push", HandleBasicUpdateTree, HandleUpdateDollarPush, NULL },
	{ "$pop", HandleBasicUpdateTree, HandleUpdateDollarPop, NULL },
	{ "$rename", HandleRenameUpdateTree, HandleUpdateDollarRename, NULL },
	{ "$setOnInsert", HandleBasicUpdateTree, HandleUpdateDollarSetOnInsert, NULL },
	{ "$pullAll", HandleBasicUpdateTree, HandleUpdateDollarPullAll, NULL },
	{ "$addToSet", HandleBasicUpdateTree, HandleUpdateDollarAddToSet, NULL },
	{ "$mul", HandleBasicUpdateTree, HandleUpdateDollarMul, NULL },
	{ "$bit", HandleBasicUpdateTree, HandleUpdateDollarBit, NULL },
	{ "$currentDate", HandleBasicUpdateTree, HandleUpdateDollarCurrentDate, NULL },
	{ "$pull", HandleBasicUpdateTree, HandleUpdateDollarPull, HandlePullWriterGetState },
	{ NULL, NULL, NULL, NULL },
};


/*
 * Helper function to cast a node as an update intermediate node.
 */
inline static const BsonUpdateIntermediatePathNode *
CastAsUpdateIntermediateNode(const BsonPathNode *node)
{
	Assert(IsIntermediateNode(node));
	return (const BsonUpdateIntermediatePathNode *) node;
}


/*
 * Helper function to cast a node as an update intermediate node.
 */
inline static const BsonUpdateLeafNode *
CastAsUpdateLeafNode(const BsonPathNode *node)
{
	Assert(!IsIntermediateNode(node));
	return (const BsonUpdateLeafNode *) node;
}


/*
 * Helper function to get the relative path form an Update node (used for logging purposes)
 */
inline static const char *
GetRelativePathFromNode(const BsonPathNode *node)
{
	if (IsIntermediateNode(node))
	{
		const BsonUpdateIntermediatePathNode *updateNode =
			CastAsUpdateIntermediateNode(node);
		return updateNode->relativePath;
	}
	else
	{
		const BsonUpdateLeafNode *updateNode = CastAsUpdateLeafNode(node);
		return updateNode->relativePath;
	}
}


/*
 * Helper function to extract positional data from an Update node.
 */
inline static const PositionalData *
GetPositionalData(const BsonPathNode *node)
{
	if (IsIntermediateNode(node))
	{
		const BsonUpdateIntermediatePathNode *updateNode =
			CastAsUpdateIntermediateNode(node);
		return &updateNode->positionalData;
	}
	else
	{
		const BsonUpdateLeafNode *updateNode = CastAsUpdateLeafNode(node);
		return &updateNode->positionalData;
	}
}


/* Helper method to write the current leaf node into the writer with common parameters */
inline static bool
WriteCurrentNode(const BsonUpdateLeafNode *leaf, const bson_value_t *currentValue,
				 pgbson_element_writer *writer, const CurrentDocumentState *state,
				 BsonUpdateTracker *tracker, bool isArray, bool hasArrayAncestors,
				 const StringView *fieldPath)
{
	UpdateSetValueState setValueState =
	{
		.fieldPath = fieldPath,
		.relativePath = leaf->relativePath,
		.isArray = isArray,
		.hasArrayAncestors = hasArrayAncestors
	};

	UpdateOperatorWriter updateWriter;
	memset(&updateWriter, 0, sizeof(UpdateOperatorWriter));
	updateWriter.writer = writer;
	updateWriter.updateTracker = tracker;
	updateWriter.relativePath = leaf->relativePath;

	Assert(leaf->base.fieldData.kind == AggregationExpressionKind_Constant);
	leaf->writeFunc(currentValue, &updateWriter,
					&leaf->base.fieldData.value,
					leaf->updateNodeContext, &setValueState,
					state);

	/* If the update operator did not modify the document
	 * and there is a value to be written,
	 * write out the original value.
	 */
	if (!updateWriter.modified &&
		currentValue->value_type != BSON_TYPE_EOD)
	{
		PgbsonElementWriterWriteValue(writer, currentValue);
	}

	return updateWriter.modified;
}


inline static void
pg_attribute_noreturn()
ThrowDollarPathNotAllowedError(const BsonPathNode * node)
{
	const char *relativePath = GetRelativePathFromNode(node);
	ereport(ERROR, (errcode(MongoBadValue),
					errmsg(
						"The dollar ($) prefixed field '%.*s' in '%s' is not allowed in the context of"
						" an update's replacement document. Consider using an aggregation pipeline with $replaceWith.",
						node->field.length, node->field.string, relativePath)));
}


/*
 * Gets the relative path from the source path until the field provided.
 * e.g. if there is a relativePath a.b.c.d.e for the field 'c',
 * will return a StringView that is 'a.b'
 */
inline static StringView
GetRelativePathUntilField(const BsonPathNode *node)
{
	const char *relativePath = GetRelativePathFromNode(node);

	/* If they're the same pointer, then this is the root field */
	if (relativePath == node->field.string)
	{
		return node->field;
	}

	/* Calculate offset of the path until that field */
	size_t length = node->field.string - relativePath;
	Assert(length <= strlen(relativePath));

	/* Trim any '.' in the path */
	if (relativePath[length - 1] == '.')
	{
		length--;
	}

	return (StringView) {
			   .length = (uint32_t) length,
			   .string = relativePath
	};
}


/* In case of $rename update operator returns true if source node exist in source document,
 * For any other operator sourceOrTargetNodeForRenameOP will be NULL and return false,
 * if sourceOrTargetNodeForRenameOP is target node do not check for existance in source return true.
 *
 * e.g, assume sourceDoc : {"a" : 1, "f": 1}
 * case 1: updateSpec : {$rename : {"a", "f.x"}} => a is leaf exclude and present in source so returns true
 * case 2: updateSpec : {$rename : {"f.x", "a"}} => a is leaf include so we will not go inside below if condition returns true
 * case 3: updateSpec : {$rename : {"g", "f.x"}} => g is leaf exclude but not present in source , so return false as it is No-Op
 */
inline static bool
IsAnErrorForIntermediateNodeOfDollarRenameOp(const BsonPathNode *
											 sourceOrTargetNodeForRenameOP,
											 pgbson *sourceDoc)
{
	/* for any operator other than $rename, below value will be NULL and return false */
	if (sourceOrTargetNodeForRenameOP == NULL)
	{
		return false;
	}

	/* if sourceOrTargetNodeForRenameOP is source node and present in soruce document return true */
	if (sourceOrTargetNodeForRenameOP->nodeType == NodeType_LeafExcluded)
	{
		BsonUpdateLeafNode *leaf =
			(BsonUpdateLeafNode *) sourceOrTargetNodeForRenameOP;
		bson_iter_t descendant, sourceIter;
		PgbsonInitIterator(sourceDoc, &sourceIter);
		return bson_iter_find_descendant(&sourceIter, leaf->relativePath, &descendant);
	}

	/* return true if sourceOrTargetNodeForRenameOP is a $rename value node (NodeType_LeafInclude) */
	return true;
}


/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */

/*
 * Builds an immutable update tree that is constructed once per query
 * and can be reused in processing document updates.
 */
const BsonIntermediatePathNode *
GetOperatorUpdateState(pgbson *updateSpec, pgbson *querySpec, pgbson *arrayFilters)
{
	bson_iter_t updateIterator;
	PgbsonInitIteratorAtPath(updateSpec, "", &updateIterator);
	if (!BSON_ITER_HOLDS_DOCUMENT(&updateIterator) ||
		!bson_iter_recurse(&updateIterator, &updateIterator))
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg(
							"Update should be a document")));
	}

	HTAB *arrayFilterHash = BuildExpressionForArrayFilters(arrayFilters);
	PositionalUpdateSpec positionalSpec =
	{
		.querySpec = querySpec,
		.arrayFilters = arrayFilterHash,
		.processedQuerySpec = NULL
	};

	BsonUpdateIntermediatePathNode *root = palloc0(
		sizeof(BsonUpdateIntermediatePathNode));
	ReadUpdateSpecAndUpdateTree(&updateIterator, &root->base,
								&positionalSpec);

	PostValidateArrayFilters(arrayFilterHash, updateSpec);
	hash_destroy(arrayFilterHash);

	return &root->base;
}


/*
 * Given a previously built immutable update tree, and a source document,
 * applies the mutations in the update state and produces a new document.
 * Additionally, if requested, also builds a new update description for the
 * update.
 *
 * Returns NULL if update was a no-op.
 */
pgbson *
ProcessUpdateOperatorWithState(pgbson *sourceDoc,
							   const BsonIntermediatePathNode *
							   updateState,
							   bool isUpsert,
							   BsonUpdateTracker *updateTracker)
{
	bson_iter_t docIterator;
	bson_value_t documentId = { 0 };
	if (PgbsonInitIteratorAtPath(sourceDoc, "_id", &docIterator))
	{
		documentId = *bson_iter_value(&docIterator);
	}

	CurrentDocumentState currentDocState =
	{
		.documentId = documentId,
		.isUpsert = isUpsert,
		.sourceDocument = sourceDoc
	};

	PgbsonInitIterator(sourceDoc, &docIterator);

	/* Do the update */
	pgbson_writer writer;
	PgbsonWriterInit(&writer);

	const BsonUpdateIntermediatePathNode *updateRoot =
		(const BsonUpdateIntermediatePathNode *) updateState;

	/* Step 1 - write the _id */
	bool updated = HandleUpdateDocumentId(&writer, updateRoot, &currentDocState);

	bool isRootLevel = true;
	bool hasArrayAncestors = false;
	bool documentUpdated = TraverseDocumentAndApplyUpdate(&docIterator, &writer,
														  updateRoot, isRootLevel,
														  &currentDocState,
														  updateTracker,
														  hasArrayAncestors);
	updated = updated || documentUpdated;
	if (!updated && !isUpsert)
	{
		return NULL;
	}

	pgbson *finalDoc = PgbsonWriterGetPgbson(&writer);

	/* Validate the _id */
	bson_value_t newIdValue = { 0 };
	if (PgbsonInitIteratorAtPath(finalDoc, "_id", &docIterator))
	{
		newIdValue = *bson_iter_value(&docIterator);
	}

	if (newIdValue.value_type == BSON_TYPE_EOD ||
		(currentDocState.documentId.value_type != BSON_TYPE_EOD &&
		 !BsonValueEquals(&newIdValue, &currentDocState.documentId)))
	{
		ThrowIdPathModifiedErrorForOperatorUpdate();
	}

	/* one last validation for the BSON length and structure, on the final document. */
	PgbsonValidateInputBson(finalDoc, BSON_VALIDATE_NONE);

	/* Validate that the document ID is correct */
	ValidateIdField(&newIdValue);
	return finalDoc;
}


/*
 * Writes a modified value to the target pgbson_element_writer and marks
 * the update as modifying the original document.
 */
void
UpdateWriterWriteModifiedValue(UpdateOperatorWriter *writer, const bson_value_t *value)
{
	PgbsonElementWriterWriteValue(writer->writer, value);
	writer->modified = true;

	if (notify_updated_field_hook != NULL)
	{
		notify_updated_field_hook(writer->updateTracker, writer->relativePath, value);
	}
}


/*
 * Skips writing a value to the target pgbson_element_writer and marks
 * the update as modifying the original document.
 */
void
UpdateWriterSkipValue(UpdateOperatorWriter *writer)
{
	writer->modified = true;
	if (notify_remove_field_hook != NULL)
	{
		notify_remove_field_hook(writer->updateTracker, writer->relativePath);
	}
}


/*
 * Gets an array writer that writes to the target pgbson_element_writer.
 */
UpdateArrayWriter *
UpdateWriterGetArrayWriter(UpdateOperatorWriter *writer)
{
	if (!writer->updateArrayWriter.isValid)
	{
		PgbsonElementWriterStartArray(writer->writer, &writer->updateArrayWriter.writer);
		writer->updateArrayWriter.isValid = true;
		writer->updateArrayWriter.modified = false;
	}

	return &writer->updateArrayWriter;
}


/*
 * Writes a value from the original document to the arrayWriter
 * This does not mark the document as modified.
 */
void
UpdateArrayWriterWriteOriginalValue(UpdateArrayWriter *writer, const bson_value_t *value)
{
	PgbsonArrayWriterWriteValue(&writer->writer, value);
}


/*
 * Writes a modified value to the arrayWriter
 * This will also mark the document as modified.
 */
void
UpdateArrayWriterWriteModifiedValue(UpdateArrayWriter *writer, const bson_value_t *value)
{
	PgbsonArrayWriterWriteValue(&writer->writer, value);
	writer->modified = true;
}


/*
 * Skips writing a value to the arrayWriter.
 * This will also mark the document as modified.
 */
void
UpdateArrayWriterSkipValue(UpdateArrayWriter *writer)
{
	writer->modified = true;
}


/*
 * Closes the array writer and propagates the appropriate state to the parent
 * updateOperatorWriter.
 */
void
UpdateArrayWriterFinalize(UpdateOperatorWriter *writer, UpdateArrayWriter *arrayWriter)
{
	PgbsonElementWriterEndArray(writer->writer, &arrayWriter->writer);

	if (writer->updateTracker != NULL &&
		arrayWriter->modified)
	{
		bson_value_t value = PgbsonElementWriterGetValue(writer->writer);

		if (notify_updated_field_hook != NULL)
		{
			notify_updated_field_hook(writer->updateTracker, writer->relativePath,
									  &value);
		}
	}

	writer->updateArrayWriter.isValid = false;
	writer->modified = writer->modified || arrayWriter->modified;
}


/* --------------------------------------------------------- */
/* Private helper methods */
/* --------------------------------------------------------- */

static void
ReadUpdateSpecAndUpdateTree(bson_iter_t *updateIterator,
							BsonIntermediatePathNode *root,
							const PositionalUpdateSpec *positionalSpec)
{
	/* Add the _id as the first node with leaf excluded */
	bson_value_t excludeValue;
	excludeValue.value_type = BSON_TYPE_INT32;
	excludeValue.value.v_int32 = 0;
	bool treatLeafDataAsConstant = true;
	BsonUpdateTreeState updateTreeState = { 0 };
	TraverseDottedPathAndGetOrAddValue(&IdFieldStringView, &excludeValue, root,
									   CreateUpdateIntermediateNode, CreateUpdateLeafNode,
									   treatLeafDataAsConstant, &updateTreeState, NULL);

	while (bson_iter_next(updateIterator))
	{
		const char *updateOperator = bson_iter_key(updateIterator);
		int i = 0;
		bool operatorFound = false;
		while (!operatorFound && MongoUpdateOperators[i].operatorName != NULL)
		{
			/* find a matching operator for the update operator. */
			if (strcmp(MongoUpdateOperators[i].operatorName, updateOperator) != 0)
			{
				i++;
				continue;
			}

			bson_iter_t operatorIterator;
			if (!BSON_ITER_HOLDS_DOCUMENT(updateIterator) ||
				!bson_iter_recurse(updateIterator, &operatorIterator))
			{
				const bson_value_t *updateItrVal = bson_iter_value(updateIterator);
				ereport(ERROR, (errcode(MongoFailedToParse),
								errmsg(
									"Modifiers operate on fields but we found type %s instead. "
									"For example: {$mod: {<field>: ...}} not {%s: %s}",
									BsonTypeName(updateItrVal->value_type),
									MongoUpdateOperators[i].operatorName,
									BsonValueToJsonForLogging(
										updateItrVal)),
								errhint(
									"Modifiers operate on fields but we found type %s instead "
									"for operator name: %s",
									BsonTypeName(updateItrVal->value_type),
									MongoUpdateOperators[i].operatorName)));
			}

			MongoUpdateOperators[i].updateTreeFunc(root, &operatorIterator,
												   MongoUpdateOperators[i].
												   updateWriterFunc,
												   MongoUpdateOperators[i].
												   updateWriterGetState,
												   positionalSpec);

			operatorFound = true;
		}

		if (!operatorFound)
		{
			ereport(ERROR, (errcode(MongoFailedToParse),
							errmsg(
								"Unknown modifier: %s. Expected a valid update modifier or "
								"pipeline-style update specified as an array",
								updateOperator)));
		}
	}
}


/*
 * Helper function that sets the parent nodes for a given node,
 * and sets the hasPositionalChildren based on the positional data of the node.
 * this is used in the walking the documents to validate positional data on a per
 * node basis per document. For instance if there's a path
 * 'a.0' and 'a.[].[]' we need to validate when we traverse at a.0 that there were
 * positionalChildren and there may be conflicts.
 * This also sets the HasIntermediateIncludeChildren if requested.
 * This function also sets sourceOrTargetNodeForRenameOP
 * In case of $rename operator for source node set sourceOrTargetNodeForRenameOP to target node and vice versa.
 * For any other operator : sourceOrTargetNodeForRenameOP will set to NULL
 */
static bool
UpdateParentDataInTree(const BsonPathNode *node, bool markAsIncludeAncestor,
					   const BsonPathNode *sourceOrTargetNodeForRenameOP)
{
	bool hasPositionalInAncestors = false;
	while (node->parent != NULL)
	{
		BsonUpdateIntermediatePathNode *parent =
			(BsonUpdateIntermediatePathNode *) node->parent;

		parent->sourceOrTargetNodeForRenameOP = sourceOrTargetNodeForRenameOP;

		/* First update any relevant positional data */
		const PositionalData *positionalData = GetPositionalData(node);
		if (positionalData->type != PositionalType_None)
		{
			parent->hasPositionalChildren = true;
			hasPositionalInAncestors = true;
		}

		if (parent->hasPositionalChildren)
		{
			/* if the parent has positional children, ensure all children
			 * are positional - otherwise it fails
			 */
			const BsonPathNode *child;
			foreach_child(child, (&parent->base))
			{
				positionalData = GetPositionalData(child);
				if (positionalData->type == PositionalType_None)
				{
					const char *updatePath = GetRelativePathFromNode(node);
					StringView pathUntilFieldView = GetRelativePathUntilField(node);
					const char *conflictPath = pnstrdup(pathUntilFieldView.string,
														pathUntilFieldView.length);
					ThrowPathConflictError(updatePath, conflictPath);
				}
			}
		}

		if (markAsIncludeAncestor)
		{
			parent->HasIntermediateIncludeChildren = true;
		}

		node = (BsonPathNode *) node->parent;
	}

	return hasPositionalInAncestors;
}


/*
 * For simple update operators that do not require complex parsing of the
 * update spec to create the tree, HandleBasicUpdateTree walks the updateSpec
 * and for each path in the operator spec, creates a node in the tree representing
 * that operator.
 */
static void
HandleBasicUpdateTree(BsonIntermediatePathNode *tree, bson_iter_t *updateSpec,
					  WriteUpdatedValuesFunc valuesFunc,
					  UpdateOperatorGetFuncState stateFunc,
					  const PositionalUpdateSpec *positionalSpec)
{
	while (bson_iter_next(updateSpec))
	{
		/* each entry is a single $operator. */
		StringView updatePathView = bson_iter_key_string_view(updateSpec);
		const bson_value_t *value = bson_iter_value(updateSpec);

		/* do path validation. */
		ValidateSpecPathForUpdateTree(&updatePathView);

		BsonUpdateTreeState updateTreeState =
		{
			.updateFunc = valuesFunc,
			.updateFuncState = NULL,
			.positionalSpec = positionalSpec
		};

		if (stateFunc != NULL)
		{
			updateTreeState.updateFuncState = stateFunc(value);
		}

		/* add a leaf field node into the bson tree. */
		bool nodeCreated = false;

		/* Update operators are parsed later, so we should just treat them as constant when creating the leaf nodes. */
		bool treatLeafDataAsConstant = true;
		const BsonPathNode *node = TraverseDottedPathAndGetOrAddField(&updatePathView,
																	  value,
																	  tree,
																	  CreateUpdateIntermediateNode,
																	  CreateUpdateLeafNode,
																	  treatLeafDataAsConstant,
																	  &updateTreeState,
																	  &nodeCreated);
		if (!nodeCreated)
		{
			/* validate scenarios where the path we're trying to update already has a conflict */
			/* in the bson path tree. */
			node = HandleNodeExistsInTree(node, updatePathView.string, value,
										  &updateTreeState);
		}

		bool markAsIncludeAncestor = false;

		BsonPathNode *ignoreAsItIsNotDollarRename = NULL;
		UpdateParentDataInTree(node, markAsIncludeAncestor, ignoreAsItIsNotDollarRename);
	}
}


/*
 * For rename, we walk the $rename spec, and create 2 nodes in the tree.
 * For the rename source, we simply add a leaf_excluded node that will
 * be used to validate the rename source path and skip writing it.
 * For the rename target, we add a node that will parse the rename source
 * and write it into the rename target writer with the
 * rename source value.
 */
static void
HandleRenameUpdateTree(BsonIntermediatePathNode *tree, bson_iter_t *updateSpec,
					   WriteUpdatedValuesFunc valuesFunc,
					   UpdateOperatorGetFuncState stateFunc,
					   const PositionalUpdateSpec *positionalSpec)
{
	bool treatLeafDataAsConstant = true;
	while (bson_iter_next(updateSpec))
	{
		StringView updatePathView = bson_iter_key_string_view(updateSpec);
		const bson_value_t *value = bson_iter_value(updateSpec);

		/* do rename source path validation. */
		ValidateSpecPathForUpdateTree(&updatePathView);

		bool nodeCreated = false;
		BsonUpdateTreeState updateTreeState =
		{
			.updateFunc = HandleUpdateDollarRenameSource,
			.updateFuncState = NULL,
			.positionalSpec = positionalSpec
		};
		bson_value_t excludeValue = {
			.value_type = BSON_TYPE_INT32,
			.value.v_int32 = 0
		};
		const BsonPathNode *sourceNode =
			TraverseDottedPathAndGetOrAddValue(&updatePathView,
											   &excludeValue,
											   tree,
											   CreateUpdateIntermediateNode,
											   CreateUpdateLeafNode,
											   treatLeafDataAsConstant,
											   &updateTreeState,
											   &nodeCreated);
		if (!nodeCreated)
		{
			sourceNode = HandleNodeExistsInTree(sourceNode, updatePathView.string,
												&excludeValue,
												&updateTreeState);
		}


		if (value->value_type != BSON_TYPE_UTF8)
		{
			ereport(ERROR, (errcode(MongoBadValue), errmsg(
								"Rename target should be a string")));
		}

		/* For the rename target, we map the "value" to the path of the rename source */
		StringView renamePath = {
			.string = value->value.v_utf8.str, .length = value->value.v_utf8.len
		};

		BsonUpdateTreeState renameNodeState =
		{
			.updateFunc = valuesFunc,
			.updateFuncState = (void *) updatePathView.string,
			.positionalSpec = positionalSpec
		};

		const bson_value_t renameTargetValue =
		{
			.value_type = BSON_TYPE_INT32,
			.value.v_int32 = 1
		};

		/* do rename target path validation. */
		ValidateSpecPathForUpdateTree(&renamePath);

		nodeCreated = false;
		const BsonPathNode *targetNode =
			TraverseDottedPathAndGetOrAddValue(&renamePath,
											   &renameTargetValue,
											   tree,
											   CreateUpdateIntermediateNode,
											   CreateUpdateLeafNode,
											   treatLeafDataAsConstant,
											   &renameNodeState,
											   &nodeCreated);
		if (!nodeCreated)
		{
			targetNode = HandleNodeExistsInTree(targetNode, renamePath.string, value,
												&updateTreeState);
		}

		bool markAsIncludeAncestor = false;
		bool hasPositionalInAncestors = UpdateParentDataInTree(sourceNode,
															   markAsIncludeAncestor,
															   targetNode);

		if (hasPositionalInAncestors)
		{
			ereport(ERROR, (errcode(MongoBadValue),
							errmsg("The source field for $rename may not be dynamic: %s",
								   updatePathView.string)));
		}

		markAsIncludeAncestor = true;
		hasPositionalInAncestors = UpdateParentDataInTree(targetNode,
														  markAsIncludeAncestor,
														  sourceNode);
		if (hasPositionalInAncestors)
		{
			ereport(ERROR, (errcode(MongoBadValue),
							errmsg(
								"The destination field for $rename may not be dynamic: %s",
								renamePath.string)));
		}
	}
}


/*
 * For unset, we walk the $unset spec, and create a leaf excluded node
 * into the tree.
 */
static void
HandleUnsetUpdateTree(BsonIntermediatePathNode *tree,
					  bson_iter_t *updateSpec,
					  WriteUpdatedValuesFunc valuesFunc,
					  UpdateOperatorGetFuncState stateFunc,
					  const PositionalUpdateSpec *positionalSpec)
{
	bool treatLeafDataAsConstant = true;
	while (bson_iter_next(updateSpec))
	{
		StringView updatePathView = bson_iter_key_string_view(updateSpec);
		const bson_value_t *value = bson_iter_value(updateSpec);
		BsonUpdateTreeState updateTreeState =
		{
			.updateFunc = valuesFunc,
			.updateFuncState = valuesFunc,
			.positionalSpec = positionalSpec
		};

		bson_value_t unsetValue = { 0 };
		unsetValue.value_type = BSON_TYPE_INT32;
		unsetValue.value.v_int32 = 0;
		bool nodeCreated = false;
		const BsonPathNode *node = TraverseDottedPathAndGetOrAddValue(&updatePathView,
																	  &unsetValue, tree,
																	  CreateUpdateIntermediateNode,
																	  CreateUpdateLeafNode,
																	  treatLeafDataAsConstant,
																	  &updateTreeState,
																	  &nodeCreated);
		if (!nodeCreated)
		{
			HandleNodeExistsInTree(node, updatePathView.string, value, &updateTreeState);
		}
	}
}


/*
 * Performs top level validation of update paths in the update spec
 */
static void
ValidateSpecPathForUpdateTree(const StringView *updatePath)
{
	if (updatePath->length == 0)
	{
		ereport(ERROR, (errcode(MongoEmptyFieldName), errmsg(
							"An empty update path is not valid")));
	}

	if (updatePath->string[updatePath->length - 1] == '.')
	{
		ereport(ERROR, (errcode(MongoEmptyFieldName),
						errmsg(
							"An update path '%s' contains an empty field name, which is not allowed.",
							updatePath->string)));
	}

	if (updatePath->string[0] == '$')
	{
		StringView path = StringViewFindPrefix(updatePath, '.');
		if (path.string == NULL)
		{
			path = *updatePath;
		}

		if (StringViewEquals(&path, &PositionalType_AllString))
		{
			ereport(ERROR, (errcode(MongoBadValue),
							errmsg(
								"Cannot have array filter identifier (i.e. '$[<id>]') element in the first position in path '%s'",
								updatePath->string)));
		}
		else if (StringViewEquals(&path, &PositionalFilterString))
		{
			ereport(ERROR, (errcode(MongoBadValue),
							errmsg(
								"Cannot have positional (i.e. '$') element in the first position in path '%s'",
								updatePath->string)));
		}
	}
}


/*
 * Handles scenarios where there's a conflict due to a node existing in the tree
 * already. Throws errors in cases where it is unexpected, or replaces the node
 * in the tree with a new value if needed.
 */
static const BsonPathNode *
HandleNodeExistsInTree(const BsonPathNode *existing,
					   const char *updatePath,
					   const bson_value_t *value,
					   BsonUpdateTreeState *updateTreeState)
{
	const char *relativePath = GetRelativePathFromNode(existing);
	if (strcmp(updatePath, "_id") != 0 || IsIntermediateNode(existing))
	{
		/* if two update operations update the same path, it's an error. */
		/* the one exception is for _id. */
		if (IsIntermediateNode(existing))
		{
			/* Existing is a deep node (e.g. a.b.c.d.e) and we're asking for
			 * a prefix (a.b.c) - the conflict is at the update a.b.c.
			 */
			relativePath = updatePath;
		}

		ThrowPathConflictError(updatePath, relativePath);
	}
	else
	{
		bool treatLeafDataAsConstant = true;

		/* replace the node in the tree. */
		return ResetNodeWithFieldAndState((const BsonLeafPathNode *) existing, updatePath,
										  value,
										  CreateUpdateLeafNode, treatLeafDataAsConstant,
										  updateTreeState);
	}
}


/*
 * Helper method to validate a specific field path when building the
 * tree for updates
 */
static void
ValidateNodePathInTree(const StringView *path, const char *relativePath)
{
	if (path->length == 0 || path->string[path->length - 1] == '.')
	{
		ereport(ERROR, (errcode(MongoBadValue),
						errmsg(
							"An empty path '%s' contains an empty field name, which is not allowed.",
							relativePath)));
	}
}


/*
 * Gets the appropriate positional data for the node based on parsing the field path
 * and the global update tree state.
 */
static PositionalData
GetNodePositionalDataFromPath(const StringView *path,
							  const BsonUpdateTreeState *state)
{
	if (StringViewEquals(path, &PositionalType_AllString))
	{
		return (PositionalData) {
				   .expression = NULL, .type = PositionalType_All
		};
	}
	else if (StringViewEquals(path, &PositionalFilterString))
	{
		if (state->positionalSpec->processedQuerySpec == NULL)
		{
			PositionalUpdateSpec *spec = (PositionalUpdateSpec *) state->positionalSpec;
			spec->processedQuerySpec = GetPositionalQueryData(spec->querySpec);
		}

		return (PositionalData) {
				   .type = PositionalType_QueryFilter,
				   .positionalQueryData = state->positionalSpec->processedQuerySpec
		};
	}
	else if (path->length > 3 &&
			 strncmp(path->string, "$[", 2) == 0 &&
			 path->string[path->length - 1] == ']')
	{
		/* Trim the leading $[ */
		StringView identifier = StringViewSubstring(path, 2);

		/* Trim the trailing ] */
		identifier.length--;

		/* now look up the array filters */
		bool found = false;
		QueryFilterPathAndValue *hashEntry = NULL;
		if (state->positionalSpec->arrayFilters != NULL)
		{
			hashEntry = (QueryFilterPathAndValue *) hash_search(
				state->positionalSpec->arrayFilters,
				&identifier,
				HASH_FIND, &found);
		}

		if (!found || hashEntry == NULL)
		{
			ereport(ERROR, (errcode(MongoBadValue),
							errmsg("No array filter found for identifier %.*s",
								   identifier.length, identifier.string)));
		}

		/* Mark the filter as used in the query */
		hashEntry->filterUsed = true;

		ExprEvalState *expr = GetExpressionEvalState(&hashEntry->queryValue,
													 CurrentMemoryContext);
		return (PositionalData) {
				   .expression = expr,
				   .type = PositionalType_ArrayFilter
		};
	}

	return (PositionalData) {
			   .expression = NULL, .type = PositionalType_None
	};
}


/*
 * Extension function to create an leaf path node in the bson tree.
 */
static BsonLeafPathNode *
CreateUpdateLeafNode(const StringView *path, const char *relativePath, void *state)
{
	ValidateNodePathInTree(path, relativePath);
	BsonUpdateTreeState *updateState = (BsonUpdateTreeState *) state;
	BsonUpdateLeafNode *node = palloc0(sizeof(BsonUpdateLeafNode));
	node->relativePath = relativePath;
	node->positionalData = GetNodePositionalDataFromPath(path, updateState);
	node->writeFunc = updateState->updateFunc;
	node->updateNodeContext = updateState->updateFuncState;

	return &node->base;
}


/*
 * Extension function to create an intermediate path node in the bson tree.
 */
static BsonIntermediatePathNode *
CreateUpdateIntermediateNode(const StringView *path,
							 const char *relativePath,
							 void *state)
{
	ValidateNodePathInTree(path, relativePath);
	BsonUpdateTreeState *updateState = (BsonUpdateTreeState *) state;
	BsonUpdateIntermediatePathNode *node = palloc0(
		sizeof(BsonUpdateIntermediatePathNode));
	node->relativePath = relativePath;
	node->positionalData = GetNodePositionalDataFromPath(path, updateState);

	return &node->base;
}


/*
 * Writes the Document ID into the target pgbson_writer.
 * This is used to write the _id first into the target document.
 */
static bool
HandleUpdateDocumentId(pgbson_writer *writer,
					   const BsonUpdateIntermediatePathNode *root,
					   const CurrentDocumentState *state)
{
	/* First walk the root and validate no operator is acting on it. */
	const BsonPathNode *node;
	bool foundId = false;
	foreach_child(node, (&root->base))
	{
		if (StringViewEquals(&node->field, &IdFieldStringView))
		{
			foundId = true;
			break;
		}
	}

	if (!foundId)
	{
		ereport(ERROR, (errmsg("Unexpected _id was removed from the update tree")));
	}

	pgbson_element_writer elementWriter;
	PgbsonInitObjectElementWriter(writer, &elementWriter, IdFieldStringView.string,
								  IdFieldStringView.length);
	switch (node->nodeType)
	{
		case NodeType_LeafExcluded:
		{
			/* No updates happening to _id - just write or create a value */
			if (state->documentId.value_type == BSON_TYPE_EOD)
			{
				/* value doesn't exist in update doc. */
				bson_value_t newIdValue;
				newIdValue.value_type = BSON_TYPE_OID;
				bson_oid_init(&(newIdValue.value.v_oid), NULL);
				PgbsonElementWriterWriteValue(&elementWriter, &newIdValue);
				return true;
			}
			else
			{
				PgbsonElementWriterWriteValue(&elementWriter, &state->documentId);
				return false;
			}
		}

		case NodeType_Intermediate:
		{
			ereport(ERROR, (errcode(MongoBadValue), errmsg(
								"_id cannot be an object/array")));
		}

		case NodeType_LeafIncluded:
		case NodeType_LeafField:
		{
			/* There is an update action on _id. apply the update. We will do
			 * post-validation to see if it's modified
			 */
			const BsonUpdateLeafNode *updateNode = CastAsUpdateLeafNode(node);
			bool isArray = false;
			bool hasArrayAncestors = false;

			/* We don't track documentId in updates */
			BsonUpdateTracker *tracker = NULL;
			return WriteCurrentNode(updateNode, &state->documentId, &elementWriter,
									state, tracker, isArray, hasArrayAncestors,
									&updateNode->base.baseNode.field);
		}

		default:
		{
			ereport(ERROR, (errmsg("Unexpected node type for _id: %d", node->nodeType)));
		}
	}
}


/*
 * Traverses the document with the sourceDocIterator, walks the update tree
 * and applies the update from the tree and writes the resultant modified
 * document into the target writer.
 *
 * Returns true if the document was modified.
 */
static bool
TraverseDocumentAndApplyUpdate(bson_iter_t *sourceDocIterator,
							   pgbson_writer *writer,
							   const BsonUpdateIntermediatePathNode *tree,
							   bool isRootLevel,
							   const CurrentDocumentState *state,
							   BsonUpdateTracker *tracker,
							   bool hasArrayAncestors)
{
	check_stack_depth();
	CHECK_FOR_INTERRUPTS();

	/* fieldHandledBitmapSet is used determine the number of bits for a bitmask on the children.
	 * When traversing the document, each field handled is marked in this bitmask. If the paths in
	 * the tree aren't handled, HandleUnresolved*Fields will take care of making sure the updates are
	 * processed.
	 */
	Bitmapset *fieldHandledBitmapSet = NULL;
	bool modified = false;
	while (bson_iter_next(sourceDocIterator))
	{
		/* for documents, if a nested field matches, then project it in and move forward. */
		pgbson_element_writer elementWriter;

		bool isArray = false;
		StringView fieldPath = bson_iter_key_string_view(sourceDocIterator);

		/* at the top level - if asked to skip _id - move on. */
		if (isRootLevel && strcmp(fieldPath.string, "_id") == 0)
		{
			continue;
		}

		/* Process the current field in the document */
		PgbsonInitObjectElementWriter(writer, &elementWriter, fieldPath.string,
									  fieldPath.length);
		bool fieldModified = HandleCurrentIteratorPosition(sourceDocIterator, tree,
														   &elementWriter,
														   &fieldHandledBitmapSet, state,
														   tracker, isArray,
														   hasArrayAncestors,
														   &fieldPath);
		modified = modified || fieldModified;
	}

	/* add any unresolved field nodes that needed to be added. */
	bool unresolvedModified = HandleUnresolvedDocumentFields(tree, fieldHandledBitmapSet,
															 writer, isRootLevel,
															 state, tracker,
															 hasArrayAncestors);
	modified = modified || unresolvedModified;
	if (fieldHandledBitmapSet != NULL)
	{
		bms_free(fieldHandledBitmapSet);
	}

	return modified;
}


/*
 * Traverses the array with the sourceDocIterator, walks the update tree
 * and applies the update from the tree and writes the resultant modified
 * array into the target writer.
 *
 * Returns true if the array was modified.
 */
static bool
TraverseArrayAndApplyUpdate(bson_iter_t *sourceDocIterator,
							pgbson_array_writer *writer,
							const BsonUpdateIntermediatePathNode *tree,
							const CurrentDocumentState *state,
							BsonUpdateTracker *tracker)
{
	check_stack_depth();
	CHECK_FOR_INTERRUPTS();

	/*
	 * first project all existing array fields to the target document.
	 * This considers overrides due to updates.
	 */
	Bitmapset *fieldHandledBitmapSet = NULL;
	pgbson_element_writer arrayElementWriter;
	PgbsonInitArrayElementWriter(writer, &arrayElementWriter);

	/* Special case, if there's positional children, then the tracker only tracks
	 * updates to the array as a whole
	 * See use of trackArrayValue.
	 */
	BsonUpdateTracker *trackerInner = tracker;
	bool trackArrayValue = false;
	if (tree->hasPositionalChildren)
	{
		trackerInner = NULL;
		trackArrayValue = true;
	}

	int32_t index = 0;
	bool modified = false;
	while (bson_iter_next(sourceDocIterator))
	{
		bool isArray = true;
		bool hasArrayAncestors = true;
		StringView fieldPath = bson_iter_key_string_view(sourceDocIterator);
		bool fieldModified = HandleCurrentIteratorPosition(sourceDocIterator, tree,
														   &arrayElementWriter,
														   &fieldHandledBitmapSet, state,
														   trackerInner, isArray,
														   hasArrayAncestors,
														   &fieldPath);
		index++;
		modified = modified || fieldModified;
	}

	/* Handle any fields that aren't in the original document */
	bool unresolvedModified = HandleUnresolvedArrayFields(tree, index,
														  &arrayElementWriter, state,
														  tracker);
	modified = unresolvedModified || modified;
	if (fieldHandledBitmapSet != NULL)
	{
		bms_free(fieldHandledBitmapSet);
	}

	if (modified && trackArrayValue && notify_updated_field_hook != NULL)
	{
		bson_value_t value = PgbsonArrayWriterGetValue(writer);

		/* The relative path reported here is the relative path until the array */
		StringView relativePathToNode = GetRelativePathUntilField((const
																   BsonPathNode *) tree);
		notify_updated_field_path_view_hook(tracker, &relativePathToNode, &value);
	}

	return modified;
}


/*
 * Given a specific position in the iterator (field),
 * walks the children of the node pointed to by the tree and processes updates
 * in that tree if there is a match for the existing field.
 * If there's no updates to the tree, then writes the original value.
 * returns true if the field was modified.
 */
static bool
HandleCurrentIteratorPosition(bson_iter_t *documentIterator,
							  const BsonUpdateIntermediatePathNode *tree,
							  pgbson_element_writer *writer,
							  Bitmapset **fieldHandledBitmapSet,
							  const CurrentDocumentState *state,
							  BsonUpdateTracker *tracker,
							  bool isArray,
							  bool hasArrayAncestors,
							  StringView *fieldPath)
{
	const StringView path = bson_iter_key_string_view(documentIterator);

	const BsonPathNode *child;
	int index = 0;
	const bson_value_t *currentValue = bson_iter_value(documentIterator);
	foreach_child(child, (&tree->base))
	{
		if (!IsNodeMatchForIteratorPath(child, &path, isArray, state, currentValue))
		{
			index++;
			continue;
		}

		/* field is a match. */
		switch (child->nodeType)
		{
			case NodeType_LeafIncluded:
			case NodeType_LeafField:
			case NodeType_LeafExcluded:
			{
				/* It's an update node */
				const BsonUpdateLeafNode *node = CastAsUpdateLeafNode(child);
				bool modified = WriteCurrentNode(node, currentValue, writer, state,
												 tracker, isArray, hasArrayAncestors,
												 fieldPath);
				*fieldHandledBitmapSet = bms_add_member(*fieldHandledBitmapSet,
														index);
				return modified;
			}

			case NodeType_Intermediate:
			{
				const BsonUpdateIntermediatePathNode *intermediate =
					CastAsUpdateIntermediateNode(child);
				bool modified = false;

				/* the update is an intermediate document, write the nested */
				/* document. */
				if (BSON_ITER_HOLDS_DOCUMENT(documentIterator))
				{
					pgbson_writer childWriter;
					bson_iter_t childIter;
					PgbsonElementWriterStartDocument(writer, &childWriter);
					if (bson_iter_recurse(documentIterator, &childIter))
					{
						bool skipDocumentIdInner = false;
						modified = TraverseDocumentAndApplyUpdate(&childIter,
																  &childWriter,
																  intermediate,
																  skipDocumentIdInner,
																  state, tracker,
																  hasArrayAncestors);
					}
					PgbsonElementWriterEndDocument(writer, &childWriter);
				}
				else if (BSON_ITER_HOLDS_ARRAY(documentIterator))
				{
					/* the update is an intermediate array, write the nested */
					/* array. */
					bson_iter_t childIter;
					pgbson_array_writer childWriter;
					PgbsonElementWriterStartArray(writer, &childWriter);
					if (bson_iter_recurse(documentIterator, &childIter))
					{
						modified = TraverseArrayAndApplyUpdate(&childIter,
															   &childWriter,
															   intermediate,
															   state, tracker);
					}
					PgbsonElementWriterEndArray(writer, &childWriter);
				}
				else if (!IsIntermediateNodeWithField(child))
				{
					/* if it's an intermediate node that has no fields (e.g.)
					 * $unset scenarios, treat it appropriately. This is the scenario
					 * where we have say $unset: { "a.b": 0 } and the document is
					 * { "a": 1 } - in this case we just write the value.
					 */
					if (isArray)
					{
						ereport(ERROR, (errcode(MongoBadValue), errmsg(
											"Cannot create field '%s' in element {%s : %s}",
											path.string, path.string,
											BsonValueToJsonForLogging(bson_iter_value(
																		  documentIterator)))));
					}
					else if (IsAnErrorForIntermediateNodeOfDollarRenameOp(
								 intermediate->sourceOrTargetNodeForRenameOP,
								 state->sourceDocument))
					{
						ereport(ERROR, (errcode(MongoPathNotViable),
										errmsg(
											"Cannot create field '%s' in element {%s : %s}",
											path.string, path.string,
											BsonValueToJsonForLogging(bson_iter_value(
																		  documentIterator)))));
					}
					else
					{
						PgbsonElementWriterWriteValue(writer, bson_iter_value(
														  documentIterator));
					}
				}
				else
				{
					ereport(ERROR, (errcode(MongoBadValue),
									errmsg(
										"Cannot create field '%s' in element {%s : %s}",
										path.string, path.string,
										BsonValueToJsonForLogging(bson_iter_value(
																	  documentIterator)))));
				}

				*fieldHandledBitmapSet = bms_add_member(*fieldHandledBitmapSet,
														index);
				return modified;
			}

			default:
			{
				ereport(ERROR, (errmsg("Updating document - unexpected nodeType %d",
									   child->nodeType)));
			}
		}
	}

	/* no updates for this field in the document - write it to the target. */
	PgbsonElementWriterWriteValue(writer, bson_iter_value(documentIterator));
	return false;
}


/*
 * Throws the error for having a positional path on a non-array field.
 */
inline static void
pg_attribute_noreturn()
ThrowPositionalOnNonArrayPathError(const BsonPathNode * node,
								   const StringView * fieldPath,
								   const bson_value_t * currentValue)
{
	const char *elementValue = BsonValueToJsonForLogging(currentValue);
	ereport(ERROR, (errcode(MongoBadValue), errmsg(
						"Cannot apply array updates to non-array element %.*s: { %.*s: %s }",
						node->parent->baseNode.field.length,
						node->parent->baseNode.field.string,
						fieldPath->length,
						fieldPath->string,
						elementValue)));
}


/*
 * Validates if a given node is a match for the fieldPath in the iterator.
 */
static bool
IsNodeMatchForIteratorPath(const BsonPathNode *node,
						   const StringView *fieldPath,
						   bool isArray,
						   const CurrentDocumentState *state,
						   const bson_value_t *currentValue)
{
	const PositionalData *positionalData = GetPositionalData(node);
	switch (positionalData->type)
	{
		case PositionalType_QueryFilter:
		{
			int32_t matchedIndex = MatchPositionalQueryAgainstDocument(
				positionalData->positionalQueryData, state->sourceDocument);
			if (matchedIndex < 0)
			{
				ereport(ERROR, (errcode(MongoBadValue),
								errmsg(
									"The positional operator did not find the match needed from the query.")));
			}

			if (isArray)
			{
				char buffer[UINT32_MAX_STR_LEN];
				const char *key;
				uint32_t keyLength = bson_uint32_to_string((uint32_t) matchedIndex, &key,
														   buffer,
														   sizeof buffer);
				StringView keyView = { .string = key, .length = keyLength };
				return StringViewEquals(&keyView, fieldPath);
			}
			else
			{
				ThrowPositionalOnNonArrayPathError(node, fieldPath, currentValue);
			}
		}

		case PositionalType_ArrayFilter:
		{
			if (isArray)
			{
				bool shouldRecurseIfArray = true;
				return EvalBooleanExpressionAgainstValue(positionalData->expression,
														 currentValue,
														 shouldRecurseIfArray);
			}
			else
			{
				ThrowPositionalOnNonArrayPathError(node, fieldPath, currentValue);
			}
		}

		case PositionalType_All:
		{
			if (isArray)
			{
				/* PositionalType_All matches all array paths */
				return true;
			}
			else
			{
				ThrowPositionalOnNonArrayPathError(node, fieldPath, currentValue);
			}
		}

		case PositionalType_None:
		{
			if (isArray)
			{
				int32_t arrayIndex = StringViewToPositiveInteger(&node->field);
				if (arrayIndex < 0)
				{
					ereport(ERROR, (errcode(MongoBadValue),
									errmsg("Invalid array index path %.*s",
										   node->field.length,
										   node->field.string)));
				}
			}

			/* Standard fields match based on field path. */
			if (!StringViewEquals(fieldPath, &node->field))
			{
				return false;
			}

			return true;
		}

		default:
		{
			ereport(ERROR, (errmsg("Unsupported positional mode %d",
								   positionalData->type)));
		}
	}
}


/*
 * Walks the tree's children for a given tree node, and for every node
 * that isn't in the original document, applies the update if necessary
 * and writes the modified value into the target writer.
 */
static bool
HandleUnresolvedDocumentFields(const BsonUpdateIntermediatePathNode *tree,
							   Bitmapset *fieldBitMapSet,
							   pgbson_writer *writer,
							   bool isRootLevel,
							   const CurrentDocumentState *state,
							   BsonUpdateTracker *tracker,
							   bool hasArrayAncestors)
{
	check_stack_depth();
	CHECK_FOR_INTERRUPTS();

	if (tree->hasPositionalChildren)
	{
		ereport(ERROR, (errcode(MongoBadValue),
						errmsg(
							"The path '%.*s' must exist in the document in order to apply array updates.",
							tree->base.baseNode.field.length,
							tree->base.baseNode.field.string)));
	}

	int index = 0;
	const BsonPathNode *node;
	bool modified = false;
	foreach_child(node, (&tree->base))
	{
		/* skip documentId if requested. */
		if (isRootLevel && StringViewEquals(&node->field, &IdFieldStringView))
		{
			index++;
			continue;
		}

		/* if the field is already handled, skip */
		if (bms_is_member(index, fieldBitMapSet))
		{
			index++;
			continue;
		}

		/* Is a match - if we're adding a dollar prefixed root field, then fail
		 * if it's not an upsert */
		if (isRootLevel &&
			node->field.length > 0 && node->field.string[0] == '$' &&
			!state->isUpsert)
		{
			ThrowDollarPathNotAllowedError(node);
		}

		switch (node->nodeType)
		{
			case NodeType_LeafIncluded:
			case NodeType_LeafField:
			{
				const BsonUpdateLeafNode *leaf = CastAsUpdateLeafNode(node);
				bson_value_t currentValue = { 0 };
				currentValue.value_type = BSON_TYPE_EOD;
				pgbson_element_writer elementWriter;
				PgbsonInitObjectElementWriter(writer, &elementWriter,
											  node->field.string,
											  node->field.length);
				bool isArray = false;
				bool innerModified = WriteCurrentNode(leaf, &currentValue,
													  &elementWriter,
													  state, tracker, isArray,
													  hasArrayAncestors,
													  &leaf->base.baseNode.field);
				modified = modified || innerModified;
				break;
			}

			case NodeType_LeafExcluded:
			{
				break;
			}

			case NodeType_Intermediate:
			{
				const BsonUpdateIntermediatePathNode *intermediate =
					CastAsUpdateIntermediateNode(node);
				if (IsIntermediateNodeWithField(node) ||
					intermediate->HasIntermediateIncludeChildren)
				{
					/* write tree as nested objects. */
					pgbson_writer childWriter;
					PgbsonWriterStartDocument(writer,
											  node->field.string,
											  node->field.length,
											  &childWriter);
					bool isRootLevelInner = false;
					bool innerModified = HandleUnresolvedDocumentFields(intermediate,
																		NULL,
																		&childWriter,
																		isRootLevelInner,
																		state,
																		tracker,
																		hasArrayAncestors);
					PgbsonWriterEndDocument(writer, &childWriter);
					modified = modified || innerModified;
				}

				break;
			}

			default:
			{
				ereport(ERROR, (errmsg("Updating document - unexpected nodeType %d",
									   node->nodeType)));
			}
		}

		index++;
	}

	return modified;
}


/*
 * Walks the tree's children for a given tree node, and for every node
 * that isn't in the original array, applies the update if necessary
 * and writes the modified value into the target writer.
 */
static bool
HandleUnresolvedArrayFields(const BsonUpdateIntermediatePathNode *tree,
							int currentMaxIndex,
							pgbson_element_writer *arrayElementWriter,
							const CurrentDocumentState *state,
							BsonUpdateTracker *tracker)
{
	/* add any unresolved field nodes that needed to be added. */
	/* unresolved fields for arrays are handled differently. */
	/* we need to walk the arrays in index order and handle each field that way. */
	/* to do that, we need the maximal array index we need to handle. */
	const BsonPathNode *child;
	int32_t maxIndex = 0;
	bool modified = false;

	/* First walk the tree and figure out the max index that needs to be written */
	foreach_child(child, (&tree->base))
	{
		const PositionalData *positionalData = GetPositionalData(child);

		/* If it's not a field node, then skip it - this includes things like
		 * $unset - they shouldn't contribute to growing the array if necessary
		 */
		if (child->nodeType != NodeType_LeafField &&
			!IsIntermediateNodeWithField(child))
		{
			continue;
		}

		int32_t pathIndex = StringViewToPositiveInteger(
			&child->field);

		/* track the highest index that is requested. */
		if (pathIndex >= 0)
		{
			maxIndex = pathIndex > maxIndex ? pathIndex : maxIndex;
		}
		else if (positionalData->type == PositionalType_None)
		{
			ereport(ERROR, (errcode(MongoBadValue),
							errmsg(
								"The destination field is an invalid array element")));
		}
	}


	/* now that we know the max index, we walk the array in index order and handle each field. */
	bson_value_t nullValue;
	nullValue.value_type = BSON_TYPE_NULL;

	if (maxIndex - currentMaxIndex > 1500000)
	{
		ereport(ERROR, (errcode(MongoCannotBackfillArray), errmsg(
							"can't backfill more than 1500000 elements")));
	}

	for (int index = currentMaxIndex; index <= maxIndex; index++)
	{
		/* these are the indexes that aren't handled yet since they were not a part of the array */
		bool pathFound = false;
		foreach_child(child, (&tree->base))
		{
			if (pathFound)
			{
				break;
			}

			int32_t pathIndex = StringViewToPositiveInteger(
				&child->field);
			if (pathIndex != index)
			{
				continue;
			}

			pathFound = true;
			switch (child->nodeType)
			{
				case NodeType_Intermediate:
				{
					if (IsIntermediateNodeWithField(child))
					{
						const BsonUpdateIntermediatePathNode *intermediate =
							CastAsUpdateIntermediateNode(child);
						pgbson_writer childWriter;
						PgbsonElementWriterStartDocument(arrayElementWriter,
														 &childWriter);
						bool hasArrayAncestors = true;
						bool isRootLevelInner = false;
						bool innerModified = HandleUnresolvedDocumentFields(intermediate,
																			NULL,
																			&childWriter,
																			isRootLevelInner,
																			state,
																			tracker,
																			hasArrayAncestors);
						PgbsonElementWriterEndDocument(arrayElementWriter,
													   &childWriter);
						modified = modified || innerModified;
					}
					break;
				}

				case NodeType_LeafField:
				case NodeType_LeafExcluded:
				case NodeType_LeafIncluded:
				{
					const BsonUpdateLeafNode *node = CastAsUpdateLeafNode(child);
					bson_value_t currentValue = { 0 };
					currentValue.value_type = BSON_TYPE_EOD;
					bool isArray = true;
					bool hasArrayAncestors = true;
					bool innerModified = WriteCurrentNode(node, &currentValue,
														  arrayElementWriter, state,
														  tracker, isArray,
														  hasArrayAncestors,
														  &node->base.baseNode.field);
					modified = modified || innerModified;
					break;
				}

				default:
				{
					ereport(ERROR, (errmsg(
										"Unexpected node type in array field handling %d",
										child->nodeType)));
				}
			}
		}

		if (!pathFound)
		{
			/* write null; */
			PgbsonElementWriterWriteValue(arrayElementWriter, &nullValue);
			modified = true;
		}
	}

	return modified;
}


/*
 * This utility method compiles the given expression for $pull update operator
 * & stores it as update state for the operator.
 *
 * RETURNS: BsonUpdateDollarPullState
 */
static void *
HandlePullWriterGetState(const bson_value_t *updateValue)
{
	MemoryContext memCtxt = CurrentMemoryContext;

	BsonUpdateDollarPullState *pullState = palloc0(sizeof(BsonUpdateDollarPullState));

	/* Validations of $pull spec */
	if (updateValue->value_type != BSON_TYPE_DOCUMENT)
	{
		/*
		 * Convert the value to a `$eq` expression document, because expression engine expects the value to be a document
		 * e.g: {a: 2} => {a: {$eq: 2}}
		 */
		pgbson_writer docWriter;
		PgbsonWriterInit(&docWriter);
		PgbsonWriterAppendValue(&docWriter, "$eq", 3, updateValue);
		const bson_value_t finalUpdateValue = ConvertPgbsonToBsonValue(
			PgbsonWriterGetPgbson(&docWriter));
		pullState->isValue = true;
		pullState->evalState = GetExpressionEvalState(&finalUpdateValue, memCtxt);
	}
	else
	{
		/*
		 * Validating $pull spec of one field,
		 * If the $pull spec is a mixed spec containing operators and normal fields e.g: {$eq: 2, a: 2}, error
		 */
		bool hasDollarField = false, hasPlainField = false;
		bson_iter_t updateSpecItr;
		BsonValueInitIterator(updateValue, &updateSpecItr);
		while (bson_iter_next(&updateSpecItr))
		{
			const char *field = bson_iter_key(&updateSpecItr);
			if (field[0] == '$')
			{
				hasDollarField = true;
				if (hasPlainField)
				{
					ereport(ERROR, (errcode(MongoBadValue), errmsg(
										"unknown top level operator: %s. If you have a field name that starts with a '$' symbol, consider using $getField or $setField.",
										field)));
				}
			}
			else
			{
				hasPlainField = true;
				if (hasDollarField)
				{
					ereport(ERROR, (errcode(MongoBadValue), errmsg(
										"unknown operator: %s", field)));
				}
			}
		}

		/* At this point we know that the update spec is not a mixed type spec containing operators and plain field */
		pullState->isValue = hasPlainField;
		pullState->evalState = GetExpressionEvalState(updateValue, memCtxt);
	}
	return pullState;
}


/*
 * Hashes the StringView path used in the hash for array filters.
 */
static uint32
ArrayFiltersKeyHash(const void *key, Size keysize)
{
	const StringView *stringView = (const StringView *) key;
	return HashStringView(stringView);
}


/*
 * Compares two stringView keys for array filters in the hash for the match.
 */
static int
ArrayFiltersKeyCompare(const void *obj1, const void *obj2, Size objsize)
{
	const StringView *hashEntry1 = obj1;
	const StringView *hashEntry2 = obj2;

	return CompareStringView(hashEntry1, hashEntry2);
}


/*
 * Walks the arrayFilters array and builds a hashmap of query values associated
 * with the given identifiers. The key will be the identifier, and the value is
 * a QueryFilterPathAndValue.
 */
static HTAB *
BuildExpressionForArrayFilters(pgbson *arrayFilters)
{
	if (arrayFilters == NULL)
	{
		return NULL;
	}

	HASHCTL hashBuilderInfo = CreateExtensionHashCTL(
		sizeof(StringView),
		sizeof(QueryFilterPathAndValue),
		ArrayFiltersKeyCompare,
		ArrayFiltersKeyHash);
	HTAB *arrayFiltersBuilderSet =
		hash_create("Bson array filter hash table", 32, &hashBuilderInfo,
					DefaultExtensionHashFlags);

	pgbsonelement arrayFiltersElement;
	bson_iter_t arrayIterator;
	PgbsonToSinglePgbsonElement(arrayFilters, &arrayFiltersElement);

	BsonValueInitIterator(&arrayFiltersElement.bsonValue, &arrayIterator);

	while (bson_iter_next(&arrayIterator))
	{
		bson_iter_t documentIterator;
		pgbsonelement singleElement;
		if (!BSON_ITER_HOLDS_DOCUMENT(&arrayIterator))
		{
			ereport(ERROR, (errcode(MongoTypeMismatch),
							errmsg("BSONField updates.update.arrayFilters.%s"
								   " is the wrong type %s. expected type object",
								   bson_iter_key(&arrayIterator),
								   BsonTypeName(bson_iter_type(&arrayIterator)))));
		}

		if (!bson_iter_recurse(&arrayIterator, &documentIterator))
		{
			ereport(ERROR, (errcode(MongoFailedToParse),
							errmsg(
								"Cannot use an expression without a top-level field name in"
								" arrayFilters")));
		}

		pgbson_writer writer;
		PgbsonWriterInit(&writer);

		bool hasElements = false;
		StringView topLevelKey = { 0 };
		while (bson_iter_next(&documentIterator))
		{
			hasElements = true;
			BsonIterToPgbsonElement(&documentIterator, &singleElement);
			StringView keyView = {
				.length = singleElement.pathLength, .string = singleElement.path
			};

			if (keyView.length == 0 || !isalnum(keyView.string[0]))
			{
				ereport(ERROR, (errcode(MongoBadValue),
								errmsg(
									"The top level field name must be alphanumeric string. Found '%.*s'",
									keyView.length, keyView.string)));
			}

			StringView fieldPath = StringViewFindPrefix(&keyView, '.');

			StringView suffix = { 0 };
			if (fieldPath.length == 0)
			{
				/* No dots in path */
				fieldPath = keyView;
			}
			else
			{
				suffix = StringViewSubstring(&keyView, fieldPath.length + 1);
			}

			if (topLevelKey.length > 0 && !StringViewEquals(&fieldPath, &topLevelKey))
			{
				ereport(ERROR, (errcode(MongoFailedToParse),
								errmsg(
									"Error parsing array filter :: caused by :: "
									"Expected a single top-level field name, found %.*s and %.*s",
									topLevelKey.length, topLevelKey.string,
									fieldPath.length, fieldPath.string)));
			}

			topLevelKey = fieldPath;

			if (suffix.length == 0)
			{
				WriteCurrentArrayFilterValue(&writer, &singleElement.bsonValue);
			}
			else
			{
				pgbson_writer childWriter;
				PgbsonWriterStartDocument(&writer, suffix.string, suffix.length,
										  &childWriter);
				WriteCurrentArrayFilterValue(&childWriter, &singleElement.bsonValue);
				PgbsonWriterEndDocument(&writer, &childWriter);
			}
		}

		if (!hasElements)
		{
			ereport(ERROR, (errcode(MongoFailedToParse),
							errmsg(
								"Cannot use an expression without a top-level field name in"
								" arrayFilters")));
		}

		bool found = false;
		QueryFilterPathAndValue *entry = hash_search(arrayFiltersBuilderSet, &topLevelKey,
													 HASH_ENTER, &found);
		if (found)
		{
			ereport(ERROR, (errcode(MongoFailedToParse),
							errmsg(
								"Found multiple array filters with the same top-level field name %.*s",
								topLevelKey.length, topLevelKey.string)));
		}

		pgbson *document = PgbsonWriterGetPgbson(&writer);
		entry->queryValue = ConvertPgbsonToBsonValue(document);
		entry->queryBson = document;
		entry->filterUsed = false;
	}

	return arrayFiltersBuilderSet;
}


/*
 * For a given field value in the array filter appends it to the bson writer
 * as a qualifier based on the value of the filter.
 */
static void
WriteCurrentArrayFilterValue(pgbson_writer *writer, bson_value_t *entryValue)
{
	if (entryValue->value_type != BSON_TYPE_DOCUMENT)
	{
		/* It's a pure value - convert to $eq */
		PgbsonWriterAppendValue(writer, "$eq", 3, entryValue);
	}
	else
	{
		bson_iter_t valueIterator;
		BsonValueInitIterator(entryValue, &valueIterator);

		bool isOperator = false;
		while (bson_iter_next(&valueIterator))
		{
			const char *key = bson_iter_key(&valueIterator);
			if (key[0] == '$')
			{
				isOperator = true;
				break;
			}
		}

		if (!isOperator)
		{
			PgbsonWriterAppendValue(writer, "$eq", 3, entryValue);
		}
		else
		{
			/* Add the operators to the target document */
			BsonValueInitIterator(entryValue, &valueIterator);
			while (bson_iter_next(&valueIterator))
			{
				StringView key = bson_iter_key_string_view(&valueIterator);
				PgbsonWriterAppendValue(writer, key.string, key.length, bson_iter_value(
											&valueIterator));
			}
		}
	}
}


/*
 * Walks the set of entries in the hash and ensures that the array filters were used
 * by at least 1 update entry.
 */
static void
PostValidateArrayFilters(HTAB *arrayFiltersHash, pgbson *updateSpec)
{
	if (arrayFiltersHash == NULL)
	{
		return;
	}

	HASH_SEQ_STATUS hashStatus;
	QueryFilterPathAndValue *entry;

	hash_seq_init(&hashStatus, arrayFiltersHash);
	while ((entry = (QueryFilterPathAndValue *) hash_seq_search(&hashStatus)) != NULL)
	{
		if (!entry->filterUsed)
		{
			bson_iter_t updateSpecIter;
			PgbsonInitIteratorAtPath(updateSpec, "", &updateSpecIter);
			const bson_value_t *bsonValue = bson_iter_value(&updateSpecIter);
			const char *updateSpecStr = FormatBsonValueForShellLogging(bsonValue);
			ereport(ERROR, (errcode(MongoFailedToParse),
							errmsg(
								"The array filter for identifier \'%.*s\' was not used in the update %s",
								entry->identifier.length, entry->identifier.string,
								updateSpecStr)));
		}

		/* Free the bson - not used anymore since we already have the expression */
		pfree(entry->queryBson);
	}
}
