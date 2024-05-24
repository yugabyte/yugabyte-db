/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bson/bson_expression_object_operators.c
 *
 * Object Operator expression implementations of BSON.
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>

#include "io/helio_bson_core.h"
#include "operators/bson_expression.h"
#include "operators/bson_expression_operators.h"
#include "aggregation/bson_tree.h"
#include "aggregation/bson_tree_write.h"
#include "utils/mongo_errors.h"

/* Struct that represents the parsed arguments to a $setField expression. */
typedef struct DollarSetFieldArguments
{
	/* The array input to the $filter expression. */
	AggregationExpressionData input;

	/* The field condition to evaluate against every element in the input array. */
	AggregationExpressionData field;

	/* Optional: The variable value (for unsetField) but required for setField */
	AggregationExpressionData value;
} DollarSetFieldArguments;

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */
static void AppendDocumentForMergeObjects(pgbson *sourceDocument, const
										  bson_value_t *value,
										  BsonIntermediatePathNode *tree,
										  ExpressionResult *parent);
static void TraverseTreeAndWrite(const BsonIntermediatePathNode *parentNode,
								 pgbson_writer *writer, pgbson *parentDocument);
static bool IsAggregationExpressionEvaluatesToNull(
	AggregationExpressionData *expressionData);
static AggregationExpressionData * PerformConstantFolding(
	AggregationExpressionData *expressionData, const bson_value_t *value);

/*
 * Evaluates the output of an $mergeObjects expression.
 * Since $mergeObjects is expressed as { "$mergeObjects": [ <expression1>, <expression2>, ... ] }
 * We evaluate the inner expressions and then return the merged object.
 * If multiple objects have the same field define, the last one wins.
 * Null evaluates to empty document.
 */
void
HandleDollarMergeObjects(pgbson *doc, const bson_value_t *operatorValue,
						 ExpressionResult *expressionResult)
{
	pgbson_writer childWriter;
	BsonIntermediatePathNode *tree = MakeRootNode();
	if (operatorValue->value_type == BSON_TYPE_ARRAY)
	{
		bson_iter_t arrayIterator;
		bson_iter_init_from_data(&arrayIterator, operatorValue->value.v_doc.data,
								 operatorValue->value.v_doc.data_len);
		while (bson_iter_next(&arrayIterator))
		{
			const bson_value_t *arrayValue = bson_iter_value(&arrayIterator);
			AppendDocumentForMergeObjects(doc, arrayValue, tree, expressionResult);
		}
	}
	else
	{
		AppendDocumentForMergeObjects(doc, operatorValue, tree, expressionResult);
	}

	pgbson_element_writer *elementWriter = ExpressionResultGetElementWriter(
		expressionResult);
	PgbsonElementWriterStartDocument(elementWriter, &childWriter);

	if (tree->childData.numChildren > 0)
	{
		TraverseTreeAndWrite(tree, &childWriter, doc);
	}

	PgbsonElementWriterEndDocument(elementWriter, &childWriter);
	ExpressionResultSetValueFromWriter(expressionResult);

	FreeTree(tree);
}


/*
 * Appends a current value that holds a expression to the given tree
 * if the expression evaluates to a document.
 * If the expression evaluates to null or undefined, it is a noop.
 * If the expression is not a document, null or undefined, an error is emitted.
 */
static void
AppendDocumentForMergeObjects(pgbson *sourceDocument, const bson_value_t *value,
							  BsonIntermediatePathNode *tree,
							  ExpressionResult *parentExpression)
{
	bool isNullOnEmpty = false;

	bson_value_t evaluatedResult = EvaluateExpressionAndGetValue(sourceDocument, value,
																 parentExpression,
																 isNullOnEmpty);
	bson_type_t evaluatedValueType = evaluatedResult.value_type;

	/* if the value is null or undefined it is a noop
	 * so we don't need to do anything for it. */
	if (evaluatedValueType != BSON_TYPE_DOCUMENT &&
		!IsExpressionResultNullOrUndefined(&evaluatedResult))
	{
		ereport(ERROR,
				errcode(MongoDollarMergeObjectsInvalidType),
				errmsg("$mergeObjects requires object inputs, but input %s is of type %s",
					   BsonValueToJsonForLogging(&evaluatedResult),
					   BsonTypeName(evaluatedValueType)),
				errhint("$mergeObjects requires object inputs, but input is of type %s",
						BsonTypeName(evaluatedValueType)));
	}
	else if (evaluatedValueType == BSON_TYPE_DOCUMENT)
	{
		bson_iter_t docIter;
		bson_iter_init_from_data(&docIter,
								 evaluatedResult.value.v_doc.data,
								 evaluatedResult.value.v_doc.data_len);

		/* Expressions are already evaluated (this will change once we move this to the new framework. )*/
		bool treatLeafDataAsConstant = true;

		while (bson_iter_next(&docIter))
		{
			StringView pathView = bson_iter_key_string_view(&docIter);

			const bson_value_t *docValue = bson_iter_value(&docIter);
			bool nodeCreated = false;
			const BsonLeafPathNode *treeNode = TraverseDottedPathAndGetOrAddLeafFieldNode(
				&pathView, docValue,
				tree, BsonDefaultCreateLeafNode,
				treatLeafDataAsConstant, &nodeCreated);

			/* if the node already exists we need to update the value
			 * as $mergeObjects has the behavior that the last path spec
			 * found if duplicates wins */
			if (!nodeCreated)
			{
				ResetNodeWithField(treeNode, NULL, docValue, BsonDefaultCreateLeafNode,
								   treatLeafDataAsConstant);
			}
		}
	}
}


/* Writes the given tree to a writer with no filter */
static void
TraverseTreeAndWrite(const BsonIntermediatePathNode *parentNode,
					 pgbson_writer *writer, pgbson *parentDocument)
{
	WriteTreeContext context =
	{
		.state = NULL,
		.filterNodeFunc = NULL,
		.isNullOnEmpty = false,
	};

	ExpressionVariableContext *variableContext = NULL;
	TraverseTreeAndWriteFieldsToWriter(parentNode, writer, parentDocument, &context,
									   variableContext);
}


/* Function verifies if the input to $setField expression, is such that the expression result will
 * short circuit to null by definitions. */
static bool
IsAggregationExpressionEvaluatesToNull(AggregationExpressionData *expressionData)
{
	switch (expressionData->kind)
	{
		case AggregationExpressionKind_Operator:
		case AggregationExpressionKind_Array:
		{
			return false;
		}

		case AggregationExpressionKind_SystemVariable:
		{
			if (expressionData->systemVariable.kind ==
				AggregationExpressionSystemVariableKind_Root)
			{
				if (expressionData->value.value_type == BSON_TYPE_DOCUMENT)
				{
					return false;
				}

				if (expressionData->value.value_type == BSON_TYPE_NULL)
				{
					return true;
				}

				return false;
			}
		}

		/* paths and variables are not supported for const folding */
		case AggregationExpressionKind_Variable:
		case AggregationExpressionKind_Path:
		{
			return false;
		}

		case AggregationExpressionKind_Document:
		{
			/* here we want to eval the input given to setfield using an agnostic env of root {} */
			/* ExpressionResult expressionResult = { 0 }; results in gcc bug on centos */
			ExpressionResult expressionResult;
			memset(&expressionResult, 0, sizeof(ExpressionResult));
			ExpressionResult childExpression = ExpressionResultCreateChild(
				&expressionResult);
			bool isNullOnEmpty = true;
			pgbson doc = *(PgbsonInitEmpty());

			EvaluateAggregationExpressionData(expressionData, &doc, &childExpression,
											  isNullOnEmpty);
			return IsExpressionResultNullOrUndefined(&childExpression.value);
		}

		case AggregationExpressionKind_Constant:
		{
			bson_value_t *value = &expressionData->value;
			return IsExpressionResultNullOrUndefined(value);
		}

		default:
		{
			ereport(ERROR, (errmsg(
								"IsAggregationExpressionEvaluatesToNull: Unexpected aggregation expression kind %d",
								expressionData->kind)));
		}
	}

	return false;
}


/*
 * Evaluates the output of a $setField expression.
 * $setField is expressed as:
 * $setField { "field": <const expression>, "input": <document> can also be "$$ROOT", "value": <expression> can also be "$$REMOVE" } }
 * We evalute the value and add the field/value into the "input" document.  If the value is a special term "$$REMOVE", we remove instead.
 */
void
HandlePreParsedDollarSetField(pgbson *doc, void *arguments,
							  ExpressionResult *expressionResult)
{
	DollarSetFieldArguments *setFieldArguments = (DollarSetFieldArguments *) arguments;

	bool isNullOnEmpty = false;

	/* When input.value is "$$REMOVE", we remove the entry in the input document.
	 * As an implementation note:  we eval $$REMOVE to EOD  and that is handled below as remove.
	 * And if the value is a constant and 'value.value_type == BSON_TYPE_EOD' that means the $$REMOVE,
	 * as when we eval $$REMOVE in some other construct, it will eval to its default state for free.
	 * TODO NOTE: if any other special variable uses 'value_type == BSON_TYPE_EOD' as a default value, we
	 * will need to differentiate which is $$REMOVE.
	 */
	bool removeOperation = false;

	/* if value is a string provide a simple alias viewport */
	StringView fieldAsString = {
		.length = 0,
		.string = ""
	};

	ExpressionResult fieldExpression = ExpressionResultCreateChild(expressionResult);

	EvaluateAggregationExpressionData(&setFieldArguments->field, doc, &fieldExpression,
									  isNullOnEmpty);
	bson_value_t evaluatedFieldArg = fieldExpression.value;

	ExpressionResult inputExpression = ExpressionResultCreateChild(expressionResult);
	EvaluateAggregationExpressionData(&setFieldArguments->input, doc, &inputExpression,
									  isNullOnEmpty);
	bson_value_t evaluatedInputArg = inputExpression.value;

	ExpressionResult valueExpression = ExpressionResultCreateChild(expressionResult);
	EvaluateAggregationExpressionData(&setFieldArguments->value, doc,
									  &valueExpression,
									  isNullOnEmpty);
	bson_value_t evaluatedValue = valueExpression.value;

	if (IsExpressionResultNullOrUndefined(&evaluatedInputArg))
	{
		/* return null rewrite, BSON_TYPE_NULL as a generated constant */
		bson_value_t value = (bson_value_t) {
			.value_type = BSON_TYPE_NULL
		};
		ExpressionResultSetValue(expressionResult, &value);
		return;
	}

	if (evaluatedInputArg.value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(MongoDollarSetFieldRequiresObject), errmsg(
							"$setField requires 'input' to evaluate to type Object")));
	}

	/* When $$REMOVE is evaluated in the current expression resolution process the
	 * system variable will resolve to EOD which we take advantage of as in mongo
	 * if you had a "value" field that was null, then we are to act as remove requested.
	 * So both are able to be processed by compare with EOD. */
	if (evaluatedValue.value_type == BSON_TYPE_EOD)
	{
		removeOperation = true;  /* mongodb will have a unknown/null $var remove the value */
	}

	/* If field is a variable reference like $foo.bar we should give error:
	 * Location4161108, error code 4161108 with message of:
	 * field path must not be a path reference, did you mean... or
	 * $foo.bar a field path reference which is not allowed in this context.
	 * Did you mean {$literal: $foo.bar} but to support that error we need
	 * to catch it during the expression eval which happens before any
	 * operator handling is done.
	 * Mongodb's parser knows the context.  When helioapi parses we ignore context.
	 */
	fieldAsString.length = evaluatedFieldArg.value.v_utf8.len;
	fieldAsString.string = evaluatedFieldArg.value.v_utf8.str;

	pgbson_element_writer *elementWriter =
		ExpressionResultGetElementWriter(expressionResult);

	pgbson_writer childObjectWriter;
	PgbsonElementWriterStartDocument(elementWriter,
									 &childObjectWriter);

	bson_iter_t inputIter;    /* We loop over the "input" document... */
	BsonValueInitIterator(&evaluatedInputArg, &inputIter);

	bool fieldAlreadyPresentInInput = false;
	while (bson_iter_next(&inputIter))
	{
		const char *key = bson_iter_key(&inputIter);
		const bson_value_t *val = bson_iter_value(&inputIter);
		if (strcmp(key, fieldAsString.string) == 0)
		{
			if (removeOperation)
			{
				continue;
			}
			else
			{
				fieldAlreadyPresentInInput = true;
				val = &evaluatedValue;    /* use the supplied "value" to overwrite field */
			}
		}

		PgbsonWriterAppendValue(&childObjectWriter, key, -1,
								val);
	}

	if (!removeOperation && !fieldAlreadyPresentInInput)
	{
		PgbsonWriterAppendValue(&childObjectWriter, fieldAsString.string, -1,
								&evaluatedValue);
	}

	PgbsonElementWriterEndDocument(elementWriter, &childObjectWriter);
	ExpressionResultSetValueFromWriter(expressionResult);
}


/* Parses the $setField expression specified in the bson_value_t and stores it in the data argument.
 * $setField { "field": <const expression>, "input": <document> can also be "$$ROOT", "value": <expression> can also be "$$REMOVE" } }
 */
void
ParseDollarSetField(const bson_value_t *argument, AggregationExpressionData *data)
{
	bson_value_t input = { 0 };
	bson_value_t field = { 0 };
	bson_value_t value = { 0 };

	bool expressionHasNullArgument = false;

	data->operator.returnType = BSON_TYPE_DOCUMENT;

	if (argument->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(MongoDollarSetFieldRequiresObject), errmsg(
							"$setField only supports an object as its argument")));
	}

	bson_iter_t docIter;
	BsonValueInitIterator(argument, &docIter);

	while (bson_iter_next(&docIter))
	{
		const char *key = bson_iter_key(&docIter);
		if (strcmp(key, "input") == 0)
		{
			input = *bson_iter_value(&docIter);
		}
		else if (strcmp(key, "field") == 0)
		{
			field = *bson_iter_value(&docIter);
		}
		else if (strcmp(key, "value") == 0)
		{
			value = *bson_iter_value(&docIter);
		}
		else
		{
			ereport(ERROR, (errcode(MongoDollarSetFieldUnknownArgument), errmsg(
								"$setField found an unknown argument: %s", key)));
		}
	}

	/* field.value_type == BSON_TYPE_NULL || field.value_type == BSON_TYPE_BOOL; in these cases mongodb will give different err */
	if (field.value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(MongoLocation4161102),
						errmsg(
							"$setField requires 'field' to be specified")));
	}

	/* TODO value for $unsetField would be missing, so might be optional, later
	 * we will refactor into a common parsing function that knows if value
	 * should be required
	 */
	if (value.value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(MongoLocation4161103),
						errmsg(
							"$setField requires 'value' to be specified")));
	}

	if (input.value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(MongoLocation4161109),
						errmsg(
							"$setField requires 'input' to be specified")));
	}

	if (input.value_type == BSON_TYPE_NULL)
	{
		expressionHasNullArgument = true;
	}

	/* If field is a variable reference like $foo.bar we should give error:
	 * Location4161108, error code 4161108 with message of:
	 * field path must not be a path reference, did you me
	 * $foo.bar a field path reference which is not allowed in this context.
	 * Did you mean {$literal: $foo.bar} but to support that error we need
	 * to catch it during the expression eval which happens before any
	 * operator handling is done
	 */

	DollarSetFieldArguments *arguments = palloc0(sizeof(DollarSetFieldArguments));

	/* Optimize, if input, field and value are constants, we can calculate the result at this parse phase,
	 * and have resolved already if the input was null to a return NULL expression as a rewrite. */

	ParseAggregationExpressionData(&arguments->field, &field);
	ParseAggregationExpressionData(&arguments->value, &value);

	/* The following will Constant Fold expressions... */
	PerformConstantFolding(&arguments->value, &value);
	PerformConstantFolding(&arguments->field, &field);

	ParseAggregationExpressionData(&arguments->input, &input);

	/* The following will optimize out NULL as Constant Fold expressions... */
	if (expressionHasNullArgument ||
		IsAggregationExpressionEvaluatesToNull(&arguments->input))
	{
		data->value = (bson_value_t) {
			.value_type = BSON_TYPE_NULL
		};

		data->kind = AggregationExpressionKind_Constant;
		return;
	}

	data->operator.arguments = arguments;
	data->operator.argumentsKind = AggregationExpressionArgumentsKind_Palloc;

	/* Here we expect param field's ultimate data value to be .value_type == BSON_TYPE_UTF8
	 * handle path variable (path-var  ref: "$$id")  "$a"   "$$a" */
	if (!(IsAggregationExpressionConstant(&arguments->field) ||
		  arguments->field.kind == AggregationExpressionKind_Operator))
	{
		/* Mongo error codes check for a few datatypes, they also check for non-constant arg to some
		 * operator parameters.
		 * If not a kind var => let it pass
		 *         if system variable : fail w/ error code 4161108, and instead if,
		 *         path expression: fail w/ different 4161108.
		 */
		ereport(ERROR, (errcode(
							MongoLocation4161106),
						errmsg(
							"$setField requires 'field' to evaluate to a constant, but got a non-constant argument %s",
							BsonTypeName(arguments->field.value.value_type)),
						errhint(
							"$setField requires 'field' to evaluate to a constant, but got a non-constant argument %s",
							BsonTypeName(arguments->field.value.value_type))));
	}
	else if (arguments->field.kind == AggregationExpressionKind_Operator)
	{
		/* continue, as the operator expression may generate a string */
	}
	else if (arguments->field.value.value_type != BSON_TYPE_UTF8)
	{
		/* Anything other than a string, is an error */

		if (arguments->field.value.value_type == BSON_TYPE_DOCUMENT)
		{
			ereport(ERROR, (errcode(
								MongoLocation4161106),
							errmsg(
								"$setField requires 'field' to evaluate to type String, but got %s",
								BsonTypeName(
									arguments->field.value.value_type)),
							errhint(
								"$setField requires 'field' to evaluate to type String, but got %s",
								BsonTypeName(
									arguments->field.value.value_type))));
		}

		ereport(ERROR, (errcode(
							MongoLocation4161107),
						errmsg(
							"$setField requires 'field' to evaluate to type String, but got %s",
							BsonTypeName(arguments->field.value.value_type)),
						errhint(
							"$setField requires 'field' to evaluate to type String, but got %s",
							BsonTypeName(arguments->field.value.value_type))));
	}

	if (arguments->input.kind == AggregationExpressionKind_Constant &&
		arguments->input.value.value_type != BSON_TYPE_DOCUMENT)
	{
		if (arguments->input.value.value_type == BSON_TYPE_NULL)
		{
			/* BSON_TYPE_NULL as a generated constant */
			data->value = (bson_value_t) {
				.value_type = BSON_TYPE_NULL
			};

			data->kind = AggregationExpressionKind_Constant;
			return;
		}

		/* If a system variable, it is most likely $$ROOT, we process $$ROOT when we evalute on a specific document, otherwise
		 * we expect a document as the 'input' value which we operate upon. */
		ereport(ERROR, (errcode(
							MongoLocation4161105),
						errmsg(
							"$setField requires 'input' to evaluate to type Object")));
	}
}


/* Function verifies if we can simplify expressions by constant folding, especially during parse time
 * so that later when we handle the pre-parsed tree during document procession, we make it as fast
 * as possible. Some notes: to make the input to $setField expression, is such that the expression result will
 * short circuit to null by definitions; for field and value we look for constants to fold. */
static AggregationExpressionData *
PerformConstantFolding(AggregationExpressionData *expressionData,
					   const bson_value_t *value)
{
	switch (expressionData->kind)
	{
		case AggregationExpressionKind_Array:
		{
			return expressionData;
		}

		case AggregationExpressionKind_Operator:
		{
			/* ExpressionResult expressionResult = { 0 }; results in gcc bug on centos */
			ExpressionResult expressionResult;
			memset(&expressionResult, 0, sizeof(ExpressionResult));
			ExpressionResult childExpression = ExpressionResultCreateChild(
				&expressionResult);
			bool isNullOnEmpty = true;
			pgbson *doc = PgbsonInitEmpty();

			ExpressionResultReset(&childExpression);
			EvaluateAggregationExpressionData(expressionData, doc, &childExpression,
											  isNullOnEmpty);
			bson_value_t evaluatedInputArg = childExpression.value;

			if (IsExpressionResultNullOrUndefined(&evaluatedInputArg))
			{
				bson_value_t valueLiteral = (bson_value_t) {
					.value_type = BSON_TYPE_NULL
				};

				ParseAggregationExpressionData(expressionData, &valueLiteral);
			}
			else
			{
				ParseAggregationExpressionData(expressionData, &evaluatedInputArg);
			}

			return expressionData;
		}

		case AggregationExpressionKind_SystemVariable:
		{
			if (expressionData->systemVariable.kind ==
				AggregationExpressionSystemVariableKind_Root)
			{
				if (expressionData->value.value_type == BSON_TYPE_DOCUMENT)
				{
					return expressionData;
				}

				if (expressionData->value.value_type == BSON_TYPE_NULL)
				{
					return expressionData;
				}

				return expressionData;
			}

			return expressionData;
		}

		/* paths and variables are not supported for const folding */
		case AggregationExpressionKind_Variable:
		case AggregationExpressionKind_Path:
		case AggregationExpressionKind_Document:
		{
			return expressionData;
		}

		case AggregationExpressionKind_Constant:
		{
			bson_value_t *value = &expressionData->value;
			bool isNull = value->value_type == BSON_TYPE_NULL || value->value_type ==
						  BSON_TYPE_UNDEFINED || value->value_type == BSON_TYPE_EOD;
			if (isNull)
			{
				return expressionData;
			}
			break;
		}

		default:
		{
			ereport(ERROR, (errmsg(
								"PerformConstantFolding: Unexpected aggregation expression kind %d",
								expressionData->kind)),
					(errhint(
						 "PerformConstantFolding: Unexpected aggregation expression kind %d",
						 expressionData->kind)));
		}
	}

	return expressionData;
}
