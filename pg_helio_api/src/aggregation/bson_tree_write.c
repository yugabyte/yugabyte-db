/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bson/bson_tree_write.c
 *
 * Functions to write components of a tree.
 *
 *-------------------------------------------------------------------------
 */
#include <postgres.h>
#include <miscadmin.h>

#include "aggregation/bson_tree.h"
#include "aggregation/bson_tree_write.h"
#include "operators/bson_expression.h"
#include "aggregation/bson_projection_tree.h"
#include "aggregation/bson_project_operator.h"

static void TraverseTreeAndWriteFieldsToWriterCore(const
												   BsonIntermediatePathNode *parentNode,
												   pgbson_writer *writer,
												   pgbson *parentDocument,
												   WriteTreeContext *context,
												   bool isRecursiveCall);


/*
 * Writes the array elements (as documents) present in the children of an ArrayFieldNode
 * as an array at the current writer location.
 */
void
WriteLeafArrayFieldToWriter(pgbson_writer *writer, const BsonPathNode *child,
							pgbson *document, ExpressionVariableContext *variableContext)
{
	if (child->nodeType == NodeType_LeafWithArrayField)
	{
		const BsonLeafArrayWithFieldPathNode *leafArrayNode = CastAsLeafArrayFieldNode(
			child);
		pgbson_array_writer arrayWriter;

		PgbsonWriterStartArray(writer, child->field.string,
							   child->field.length,
							   &arrayWriter);
		AppendLeafArrayFieldChildrenToWriter(&arrayWriter, leafArrayNode, document,
											 variableContext);
		PgbsonWriterEndArray(writer, &arrayWriter);
	}
}


/* Writes the children of an array leaf to a given array writer evaluating the aggregation expressions if any. */
void
AppendLeafArrayFieldChildrenToWriter(pgbson_array_writer *arrayWriter, const
									 BsonLeafArrayWithFieldPathNode *leafArrayNode,
									 pgbson *document,
									 ExpressionVariableContext *variableContext)
{
	const BsonLeafPathNode *leafPathNode;
	foreach_array_child(leafPathNode, leafArrayNode)
	{
		bson_value_t value;
		if (leafPathNode->fieldData.kind == AggregationExpressionKind_Constant)
		{
			value = leafPathNode->fieldData.value;
		}
		else
		{
			bool isNullOnEmpty = false;
			StringView path = {
				.string = "", .length = 0
			};
			pgbson_writer innerWriter;
			pgbson_element_writer elementWriter;
			PgbsonWriterInit(&innerWriter);
			PgbsonInitObjectElementWriter(&innerWriter, &elementWriter, "", 0);
			EvaluateAggregationExpressionDataToWriter(&leafPathNode->fieldData, document,
													  path, &innerWriter, variableContext,
													  isNullOnEmpty);

			value = PgbsonElementWriterGetValue(&elementWriter);
		}

		/* For expressions nested in an array that evaluate to undefined we should write null. */
		if (value.value_type == BSON_TYPE_EOD ||
			value.value_type == BSON_TYPE_UNDEFINED)
		{
			PgbsonArrayWriterWriteNull(arrayWriter);
		}
		else
		{
			PgbsonArrayWriterWriteValue(arrayWriter, &value);
		}
	}
}


/* Function that writes out a tree to the given writer and calls the callback
 * functions defined in the context passing down the current state as an argument. */
void
TraverseTreeAndWriteFieldsToWriter(const BsonIntermediatePathNode *parentNode,
								   pgbson_writer *writer, pgbson *parentDocument,
								   WriteTreeContext *context)
{
	bool inRecursiveContext = false;
	TraverseTreeAndWriteFieldsToWriterCore(parentNode, writer, parentDocument, context,
										   inRecursiveContext);
}


/* Core function to write the tree to a writer. */
static void
TraverseTreeAndWriteFieldsToWriterCore(const BsonIntermediatePathNode *parentNode,
									   pgbson_writer *writer, pgbson *parentDocument,
									   WriteTreeContext *context,
									   bool inRecursiveContext)
{
	check_stack_depth();
	CHECK_FOR_INTERRUPTS();

	const BsonPathNode *node;
	int index = 0;
	ExpressionVariableContext *variableContext = NULL;
	bool isNullOnEmpty = context->isNullOnEmpty;
	foreach_child(node, parentNode)
	{
		if (!inRecursiveContext && context->filterNodeFunc != NULL &&
			context->filterNodeFunc(context->state, index))
		{
			/* Filter node when we are not in a recursive context (nested documents),
			 * when a filter function is provided and it returns true.
			 */
			index++;
			continue;
		}

		if (node->nodeType == NodeType_LeafField)
		{
			StringView path = {
				.string = node->field.string,
				.length = node->field.length
			};

			const BsonLeafPathNode *leafNode = CastAsLeafNode(node);

			/* When doing operator eval the parentDocument might be "{ }" on empty project */
			EvaluateAggregationExpressionDataToWriter(&leafNode->fieldData,
													  parentDocument, path, writer,
													  variableContext, isNullOnEmpty);
		}
		else if (node->nodeType == NodeType_LeafFieldWithContext)
		{
			const BsonLeafNodeWithContext *leafNode = CastAsBsonLeafNodeWithContext(
				node);
			ProjectionOpHandlerContext *context =
				(ProjectionOpHandlerContext *) leafNode->context;

			/* if context is NULL we should we would treat it as a normal expression and evaluate it */
			if (context == NULL)
			{
				StringView path = {
					.string = node->field.string,
					.length = node->field.length
				};

				const BsonLeafPathNode *leafNode = CastAsLeafNode(node);
				EvaluateAggregationExpressionDataToWriter(&leafNode->fieldData,
														  parentDocument, path, writer,
														  variableContext, isNullOnEmpty);
			}
		}
		else if (IsIntermediateNodeWithField(node))
		{
			/* write tree as nested objects. */
			const BsonIntermediatePathNode *intermediateNode = CastAsIntermediateNode(
				node);
			pgbson_writer childWriter;
			PgbsonWriterStartDocument(writer,
									  node->field.string,
									  node->field.length,
									  &childWriter);
			bool inRecursiveContextInner = true;
			TraverseTreeAndWriteFieldsToWriterCore(intermediateNode, &childWriter,
												   parentDocument, context,
												   inRecursiveContextInner);
			PgbsonWriterEndDocument(writer, &childWriter);
		}
		else if (node->nodeType == NodeType_LeafWithArrayField)
		{
			WriteLeafArrayFieldToWriter(writer, node, parentDocument, variableContext);
		}

		index++;
	}
}
