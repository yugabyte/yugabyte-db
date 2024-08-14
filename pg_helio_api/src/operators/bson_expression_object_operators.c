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

/* Struct that represents the parsed arguments to a $getField expression. */
typedef struct DollarGetFieldArguments
{
	/* The input object to the $getField expression. */
	AggregationExpressionData input;

	/* The field in the input object for which you want to return a value */
	AggregationExpressionData field;
} DollarGetFieldArguments;

/* Struct that represents the parsed arguments to a $setField expression. */
typedef struct DollarSetFieldArguments
{
	/* The input object to the $setField expression. */
	AggregationExpressionData input;

	/* The field in the input object for which you want to set value */
	AggregationExpressionData field;

	/* The variable value required for $setField (not for $unsetField) */
	AggregationExpressionData value;
} DollarSetFieldArguments;

/* internal defined type represents validation result for field argument in $getField, $setField and $unsetField */
typedef enum
{
	/* valid field argument */
	VALID_ARGUMENT,

	/* field is a path (start with `$`) */
	PATH_IS_NOT_ALLOWED,

	/* field is a document or array, but not evaluated from $const or $literal */
	NON_CONSTANT_ARGUMENT,

	/* field is a constant other than string */
	NON_STRING_CONSTANT
} FieldArgumentValidationCode;

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */
static void AppendDocumentForMergeObjects(pgbson *sourceDocument, const
										  bson_value_t *value,
										  BsonIntermediatePathNode *tree,
										  ExpressionResult *parent);
static void HandlePreParsedDollarSetFieldOrUnsetFieldCore(pgbson *doc, void *arguments,
														  ExpressionResult *
														  expressionResult, bool
														  isSetField);
static bool IsAggregationExpressionEvaluatesToNull(
	AggregationExpressionData *expressionData);
static FieldArgumentValidationCode
ParseFieldExpressionForDollarGetFieldAndSetFieldAndUnsetField(const bson_value_t *field,
															  AggregationExpressionData
															  *
															  fieldExpression,
															  ParseAggregationExpressionContext
															  *
															  context);
static void ParseDollarSetFieldOrUnsetFieldCore(const bson_value_t *argument,
												AggregationExpressionData *data, bool
												isSetField,
												ParseAggregationExpressionContext *context);
static bson_value_t ProcessResultForDollarGetField(bson_value_t field, bson_value_t
												   input);
static bson_value_t ProcessResultForDollarSetFieldOrUnsetField(bson_value_t field,
															   bson_value_t input,
															   bson_value_t value);

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
		ParseAggregationExpressionContext ignoreContext = { 0 };

		while (bson_iter_next(&docIter))
		{
			StringView pathView = bson_iter_key_string_view(&docIter);

			const bson_value_t *docValue = bson_iter_value(&docIter);
			bool nodeCreated = false;
			const BsonLeafPathNode *treeNode = TraverseDottedPathAndGetOrAddLeafFieldNode(
				&pathView, docValue,
				tree, BsonDefaultCreateLeafNode,
				treatLeafDataAsConstant, &nodeCreated, &ignoreContext);

			/* if the node already exists we need to update the value
			 * as $mergeObjects has the behavior that the last path spec
			 * found if duplicates wins */
			if (!nodeCreated)
			{
				ResetNodeWithField(treeNode, NULL, docValue, BsonDefaultCreateLeafNode,
								   treatLeafDataAsConstant, &ignoreContext);
			}
		}
	}
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
	bool isSetField = true;
	HandlePreParsedDollarSetFieldOrUnsetFieldCore(doc, arguments, expressionResult,
												  isSetField);
}


/* Parses the $setField expression specified in the bson_value_t and stores it in the data argument.
 * $setField { "field": <const expression>, "input": <document> can also be "$$ROOT", "value": <expression> can also be "$$REMOVE" } }
 */
void
ParseDollarSetField(const bson_value_t *argument, AggregationExpressionData *data,
					ParseAggregationExpressionContext *context)
{
	bool isSetField = true;
	ParseDollarSetFieldOrUnsetFieldCore(argument, data, isSetField, context);
}


/*
 * Evaluates the output of a $unsetField expression.
 * $unsetField is expressed as:
 * $unsetField { "field": <const expression>, "input": <document> can also be "$$ROOT" } }
 */
void
HandlePreParsedDollarUnsetField(pgbson *doc, void *arguments,
								ExpressionResult *expressionResult)
{
	bool isSetField = false;
	HandlePreParsedDollarSetFieldOrUnsetFieldCore(doc, arguments, expressionResult,
												  isSetField);
}


/* Parses the $unsetField expression specified in the bson_value_t and stores it in the data argument.
 * $unsetField { "field": <const expression>, "input": <document> can also be "$$ROOT" } }
 */
void
ParseDollarUnsetField(const bson_value_t *argument, AggregationExpressionData *data,
					  ParseAggregationExpressionContext *context)
{
	bool isSetField = false;
	ParseDollarSetFieldOrUnsetFieldCore(argument, data, isSetField, context);
}


/*
 * Evaluates the output of a $getField expression.
 * $getField is expressed as:
 * $getField { "field": <const expression>, "input": <document> default to be "$$CURRENT" } }
 * or $getField: <const expression of field> to retirved field from $$CURRENT
 */
void
HandlePreParsedDollarGetField(pgbson *doc, void *arguments,
							  ExpressionResult *expressionResult)
{
	DollarGetFieldArguments *getFieldArguments = (DollarGetFieldArguments *) arguments;

	bool isNullOnEmpty = false;

	ExpressionResult fieldExpression = ExpressionResultCreateChild(expressionResult);
	EvaluateAggregationExpressionData(&getFieldArguments->field, doc, &fieldExpression,
									  isNullOnEmpty);
	bson_value_t evaluatedFieldArg = fieldExpression.value;

	ExpressionResult inputExpression = ExpressionResultCreateChild(expressionResult);
	EvaluateAggregationExpressionData(&getFieldArguments->input, doc, &inputExpression,
									  isNullOnEmpty);
	bson_value_t evaluatedInputArg = inputExpression.value;

	if (evaluatedInputArg.value_type == BSON_TYPE_DOCUMENT || IsExpressionResultNull(
			&evaluatedInputArg))
	{
		bson_value_t result = ProcessResultForDollarGetField(evaluatedFieldArg,
															 evaluatedInputArg);
		if (result.value_type != BSON_TYPE_EOD)
		{
			ExpressionResultSetValue(expressionResult, &result);
		}
	}

	/* If the field is not found, or input is missing or doesn't resolve to an object, do nothing to return missing directly */
	/* which is not the same with mongodb documentation. */
}


/* Parses the $getField expression specified in the bson_value_t and stores it in the data argument.
 * 1. full expression
 * $getField { "field": <const expression>, "input": <document> default to be "$$CURRENT" } }
 *      example: {"$getField": {"field": "a", "input": "$$CURRENT"}}
 * 2. shorthand expression
 * $getField: <const expression of field> to retirved field from $$CURRENT
 *		example: {"$getField": "a"}
 *
 * validation cases:
 * If the argument is a document, traverse the argument, and check the key got by iterator
 * 1. key is "field" or "input", store the value to field or input
 * 2. the first key starts with "$", the expression is a shorthand expression, took argument as field, and "$$CURRENT" as input, validate field argument later
 * 3. if key is unknown, throw error
 * If the argument is not a document, it is a shorthand expression, store the argument to field, and "$$CURRENT" to input
 *
 * Then validate field argument, which is completed in function ParseFieldExpressionForDollarGetField
 * 4. if input is EOD, throw error
 * 5. if input is null, return null
 * 6. if other cases, do nothing to return missing
 */
void
ParseDollarGetField(const bson_value_t *argument, AggregationExpressionData *data,
					ParseAggregationExpressionContext *context)
{
	bson_value_t input = { 0 };
	bson_value_t field = { 0 };

	if (argument->value_type == BSON_TYPE_DOCUMENT)
	{
		/* iterate over docuemnt to get input and field */
		bson_iter_t docIter;
		BsonValueInitIterator(argument, &docIter);
		bool isFirstKey = true;
		while (bson_iter_next(&docIter))
		{
			const char *key = bson_iter_key(&docIter);

			/* if the first key starts with "$", the expression is a shorthand expression */
			/* it may be an operator expression, copy to field and parse in ParseFieldExpressionForDollarGetField */
			if (isFirstKey && key[0] == '$')
			{
				field = *argument;
				input.value_type = BSON_TYPE_UTF8;
				input.value.v_utf8.len = 9;
				input.value.v_utf8.str = "$$CURRENT";
				break;
			}

			isFirstKey = false;
			if (strcmp(key, "input") == 0)
			{
				input = *bson_iter_value(&docIter);
			}
			else if (strcmp(key, "field") == 0)
			{
				field = *bson_iter_value(&docIter);
			}
			else
			{
				ereport(ERROR, (errcode(MongoLocation3041701), errmsg(
									"$getField found an unknown argument: %s",
									key)));
			}
		}
	}
	else
	{
		/* the expression is a shorthand expression */
		field = *argument;
		input.value_type = BSON_TYPE_UTF8;
		input.value.v_utf8.len = 9;
		input.value.v_utf8.str = "$$CURRENT";
	}

	/* check if required key is missing */
	if (field.value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(MongoLocation3041702),
						errmsg(
							"$getField requires 'field' to be specified")));
	}

	if (input.value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(MongoLocation3041703),
						errmsg(
							"$getField requires 'input' to be specified")));
	}

	DollarGetFieldArguments *arguments = palloc0(sizeof(DollarGetFieldArguments));

	data->operator.arguments = arguments;
	data->operator.argumentsKind = AggregationExpressionArgumentsKind_Palloc;

	/* Parse field */
	FieldArgumentValidationCode validationCode =
		ParseFieldExpressionForDollarGetFieldAndSetFieldAndUnsetField(&field,
																	  &arguments->field,
																	  context);
	if (validationCode == PATH_IS_NOT_ALLOWED)
	{
		ereport(ERROR, (errcode(MongoLocation5654600),
						errmsg(
							"A field path reference which is not allowed in this context. Did you mean {$literal: '%s'}?",
							arguments->field.value.value.v_utf8.str)));
	}
	else if (validationCode == NON_CONSTANT_ARGUMENT)
	{
		ereport(ERROR, (errcode(
							MongoLocation5654601),
						errmsg(
							"$getField requires 'field' to evaluate to a constant, but got a non-constant argument")));
	}
	else if (validationCode == NON_STRING_CONSTANT)
	{
		ereport(ERROR, (errcode(
							MongoLocation5654602),
						errmsg(
							"$getField requires 'field' to evaluate to type String, but got %s",
							BsonTypeName(arguments->field.value.value_type)),
						errhint(
							"$getField requires 'field' to evaluate to type String, but got %s",
							BsonTypeName(arguments->field.value.value_type))));
	}

	/* Parse input */
	ParseAggregationExpressionData(&arguments->input, &input, context);

	/* if input is a constant document, we can evaluate the result directly */
	if ((IsAggregationExpressionConstant(&arguments->input) && input.value_type ==
		 BSON_TYPE_DOCUMENT) || IsAggregationExpressionEvaluatesToNull(&arguments->input))
	{
		bson_value_t result = ProcessResultForDollarGetField(field, input);
		if (result.value_type != BSON_TYPE_EOD)
		{
			data->value = result;
			data->kind = AggregationExpressionKind_Constant;
		}
	}

	/* if the value type of input is not an object, do nothing to return missing */
	/* which is not the same with mongodb documentation. */
}


/*
 * Evaluates the output of a $setField and $unsetField expression.
 * $setField is expressed as:
 * $setField { "field": <const expression>, "input": <document> can also be "$$ROOT", "value": <expression> can also be "$$REMOVE" } }
 * We evalute the value and add the field/value into the "input" document.  If the value is a special term "$$REMOVE", we remove instead.
 *
 * $unsetField is expressed as:
 * $unsetField { "field": <const expression>, "input": <document> can also be "$$ROOT" } }
 */
static void
HandlePreParsedDollarSetFieldOrUnsetFieldCore(pgbson *doc, void *arguments,
											  ExpressionResult *expressionResult, bool
											  isSetField)
{
	const char *operatorName = isSetField ? "$setField" : "$unsetField";
	DollarSetFieldArguments *setFieldArguments = (DollarSetFieldArguments *) arguments;

	bool isNullOnEmpty = false;

	ExpressionResult fieldExpression = ExpressionResultCreateChild(expressionResult);

	EvaluateAggregationExpressionData(&setFieldArguments->field, doc, &fieldExpression,
									  isNullOnEmpty);
	bson_value_t evaluatedFieldArg = fieldExpression.value;

	ExpressionResult inputExpression = ExpressionResultCreateChild(expressionResult);
	EvaluateAggregationExpressionData(&setFieldArguments->input, doc, &inputExpression,
									  isNullOnEmpty);
	bson_value_t evaluatedInputArg = inputExpression.value;

	bson_value_t evaluatedValueArg = { 0 };
	if (isSetField)
	{
		ExpressionResult valueExpression = ExpressionResultCreateChild(expressionResult);
		EvaluateAggregationExpressionData(&setFieldArguments->value, doc,
										  &valueExpression, isNullOnEmpty);
		evaluatedValueArg = valueExpression.value;
	}
	else
	{
		evaluatedValueArg.value_type = BSON_TYPE_EOD;
	}

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
							"%s requires 'input' to evaluate to type Object",
							operatorName)));
	}

	bson_value_t result = ProcessResultForDollarSetFieldOrUnsetField(evaluatedFieldArg,
																	 evaluatedInputArg,
																	 evaluatedValueArg);
	ExpressionResultSetValue(expressionResult, &result);
}


/* Parses the $setField and $unsetField expression specified in the bson_value_t and stores it in the data argument.
 * $setField { "field": <const expression>, "input": <document> can also be "$$ROOT", "value": <expression> can also be "$$REMOVE" } }
 * $unsetField { "field": <const expression>, "input": <document> can also be "$$ROOT" } }
 */
static void
ParseDollarSetFieldOrUnsetFieldCore(const bson_value_t *argument,
									AggregationExpressionData *data,
									bool isSetField,
									ParseAggregationExpressionContext *context)
{
	bson_value_t input = { 0 };
	bson_value_t field = { 0 };
	bson_value_t value = { 0 };

	const char *operatorName = isSetField ? "$setField" : "$unsetField";

	data->operator.returnType = BSON_TYPE_DOCUMENT;

	if (argument->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(MongoDollarSetFieldRequiresObject), errmsg(
							"%s only supports an object as its argument", operatorName),
						errhint("%s only supports an object as its argument",
								operatorName)));
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
		else if (isSetField && strcmp(key, "value") == 0)
		{
			value = *bson_iter_value(&docIter);
		}
		else
		{
			ereport(ERROR, (errcode(MongoDollarSetFieldUnknownArgument), errmsg(
								"%s found an unknown argument: %s", operatorName, key)));
		}
	}

	if (field.value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(MongoLocation4161102),
						errmsg(
							"%s requires 'field' to be specified", operatorName)));
	}

	if (isSetField && value.value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(MongoLocation4161103),
						errmsg(
							"$setField requires 'value' to be specified")));
	}

	if (input.value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(MongoLocation4161109),
						errmsg(
							"%s requires 'input' to be specified", operatorName)));
	}

	DollarSetFieldArguments *arguments = palloc0(sizeof(DollarSetFieldArguments));

	/* parse field argument */
	FieldArgumentValidationCode validationCode =
		ParseFieldExpressionForDollarGetFieldAndSetFieldAndUnsetField(&field,
																	  &arguments->field,
																	  context);

	/* throw errors according to field validation result */
	if (validationCode == NON_CONSTANT_ARGUMENT)
	{
		ereport(ERROR, (errcode(
							MongoLocation4161106),
						errmsg(
							"%s requires 'field' to evaluate to a constant, but got a non-constant argument",
							operatorName)));
	}
	else if (validationCode == NON_STRING_CONSTANT)
	{
		ereport(ERROR, (errcode(
							MongoLocation4161107),
						errmsg(
							"%s requires 'field' to evaluate to type String, but got %s",
							operatorName,
							BsonTypeName(arguments->field.value.value_type)),
						errhint(
							"%s requires 'field' to evaluate to type String, but got %s",
							operatorName,
							BsonTypeName(arguments->field.value.value_type))));
	}
	else if (validationCode == PATH_IS_NOT_ALLOWED)
	{
		ereport(ERROR, (errcode(MongoLocation4161108),
						errmsg(
							"A field path reference which is not allowed in this context. Did you mean {$literal: '%s'}?",
							arguments->field.value.value.v_utf8.str)));
	}

	ParseAggregationExpressionData(&arguments->value, &value, context);
	ParseAggregationExpressionData(&arguments->input, &input, context);

	data->operator.arguments = arguments;
	data->operator.argumentsKind = AggregationExpressionArgumentsKind_Palloc;


	/* Optimize, if input, field and value are constants, we can calculate the result at this parse phase,
	 * or if the input was null then return NULL expression. */
	if (IsAggregationExpressionEvaluatesToNull(&arguments->input) ||
		(IsAggregationExpressionConstant(&arguments->input) && (!isSetField ||
																IsAggregationExpressionConstant(
																	&arguments->value))))
	{
		if (IsExpressionResultNull(&arguments->input.value) ||
			arguments->input.value.value_type == BSON_TYPE_DOCUMENT)
		{
			bson_value_t result = ProcessResultForDollarSetFieldOrUnsetField(
				arguments->field.value,
				arguments->
				input.value,
				arguments->
				value.value);
			data->value = result;
			data->kind = AggregationExpressionKind_Constant;
			return;
		}
		else
		{
			ereport(ERROR, (errcode(
								MongoLocation4161105),
							errmsg(
								"%s requires 'input' to evaluate to type Object",
								operatorName)));
		}
	}
}


/* Function parses field argument for $getField, $setField and $unsetField
 * validation cases:
 * 1. if field is EOD, throw error missing required key 'field'
 * 2. if field is a document, check if it is a valid $const or $literal expression, update flag and store evaluated result
 * 3. If field not a $const or $literal expression:
 *      3.1 If it's a path/system variable, throw error PATH_IS_NOT_ALLOWED
 *      3.2 Else if it's a document/array, throw error NON_CONSTANT_ARGUMENT
 *      3.3 Else if it's value type is not utf-8, throw error NON_STRING_CONSTANT
 * 4. Else if the type of evaluated result is not utf-8 throw error NON_STRING_CONSTANT
 * 5. Then it is a valid field argument, and the value has been updated, return VALID_ARGUMENT */
static FieldArgumentValidationCode
ParseFieldExpressionForDollarGetFieldAndSetFieldAndUnsetField(const bson_value_t *field,
															  AggregationExpressionData *
															  fieldExpression,
															  ParseAggregationExpressionContext
															  *context)
{
	bool isConstOrLiteralExpression = false;

	/* Parse the expression first */
	/* If there are more than one key, throw error */
	/*      example: {"$getField": {"$unknown": "a", "b": "c"}} */
	/*      throw error: An object representing an expression must have exactly one field... */
	/* If the first key is not a recognized operator, throw error */
	/*      example: {"$getField": {"$unknown": "a"}} */
	/*      throw error: Unrecognized expression '$unknown' */
	ParseAggregationExpressionData(fieldExpression, field, context);

	/* We should check if the field is a valid $const or $literal expression */
	/* as we need to separate these two cases where fieldExpression->kind are both AggregationExpressionKind_Constant */
	/* and fieldExpression->value.value_type are both BSON_TYPE_ARRAY */
	/* example: */
	/*		{ $getField: { $const: [1,2]}} should throw MongoLocation5654602 */
	/*		{ $getField: { $zip: { inputs: [ [ "a" ], [ "b" ], [ "c" ] ] }}} should throw MongoLocation5654601 */
	if (field->value_type == BSON_TYPE_DOCUMENT)
	{
		bson_iter_t docIter;
		BsonValueInitIterator(field, &docIter);
		if (bson_iter_next(&docIter))
		{
			const char *key = bson_iter_key(&docIter);
			if (strcmp(key, "$const") == 0 || strcmp(key, "$literal") == 0)
			{
				isConstOrLiteralExpression = true;
			}
		}
	}

	if (!isConstOrLiteralExpression)
	{
		if (fieldExpression->kind == AggregationExpressionKind_Path ||
			fieldExpression->kind == AggregationExpressionKind_SystemVariable)
		{
			return PATH_IS_NOT_ALLOWED;
		}
		else if (field->value_type == BSON_TYPE_DOCUMENT ||
				 field->value_type == BSON_TYPE_ARRAY)
		{
			return NON_CONSTANT_ARGUMENT;
		}
	}

	/* if the field is a $const or $literal expression, or any constant value, we should check if the value type is utf-8 */
	if (fieldExpression->value.value_type != BSON_TYPE_UTF8)
	{
		return NON_STRING_CONSTANT;
	}
	return VALID_ARGUMENT;
}


/* Process result for $getField */
static bson_value_t
ProcessResultForDollarGetField(bson_value_t field, bson_value_t input)
{
	bson_value_t result = { 0 };

	/* if input is null, return null */
	if (IsExpressionResultNull(&input))
	{
		result = (bson_value_t) {
			.value_type = BSON_TYPE_NULL
		};
		return result;
	}

	bson_iter_t inputIter;
	BsonValueInitIterator(&input, &inputIter);
	while (bson_iter_next(&inputIter))
	{
		const char *key = bson_iter_key(&inputIter);
		const bson_value_t *val = bson_iter_value(&inputIter);

		if (strcmp(key, field.value.v_utf8.str) == 0)
		{
			result = *val;
		}
	}
	return result;
}


/* Process result for $setField and $unsetField */
static bson_value_t
ProcessResultForDollarSetFieldOrUnsetField(bson_value_t field, bson_value_t input,
										   bson_value_t value)
{
	bson_value_t result = { 0 };

	/* When input.value is "$$REMOVE", we remove the entry in the input document.
	 * As an implementation note:  we eval $$REMOVE to EOD  and that is handled below as remove.
	 * And if the value is a constant and 'value.value_type == BSON_TYPE_EOD' that means the $$REMOVE,
	 * as when we eval $$REMOVE in some other construct, it will eval to its default state for free.
	 * TODO NOTE: if any other special variable uses 'value_type == BSON_TYPE_EOD' as a default value, we
	 * will need to differentiate which is $$REMOVE.
	 */
	bool removeOperation = false;

	/* When $$REMOVE is evaluated in the current expression resolution process the
	 * system variable will resolve to EOD which we take advantage of as in mongo
	 * if you had a "value" field that was null, then we are to act as remove requested.
	 * So both are able to be processed by compare with EOD. */
	if (value.value_type == BSON_TYPE_EOD)
	{
		removeOperation = true;  /* mongodb will have a unknown/null $var remove the value */
	}

	/* if input is null, return null */
	if (IsExpressionResultNull(&input))
	{
		return (bson_value_t) {
				   .value_type = BSON_TYPE_NULL
		};
	}

	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	bson_iter_t inputIter;
	BsonValueInitIterator(&input, &inputIter);

	bool fieldAlreadyPresentInInput = false;
	while (bson_iter_next(&inputIter))
	{
		const char *key = bson_iter_key(&inputIter);
		const bson_value_t *val = bson_iter_value(&inputIter);

		if (strcmp(key, field.value.v_utf8.str) == 0)
		{
			if (removeOperation)
			{
				continue;
			}
			else
			{
				fieldAlreadyPresentInInput = true;
				val = &value;    /* use the supplied "value" to overwrite field */
			}
		}
		PgbsonWriterAppendValue(&writer, key, -1, val);
	}

	if (!removeOperation && !fieldAlreadyPresentInInput)
	{
		PgbsonWriterAppendValue(&writer, field.value.v_utf8.str, -1, &value);
	}

	pgbson *pgbsonResult = PgbsonWriterGetPgbson(&writer);
	result = ConvertPgbsonToBsonValue(pgbsonResult);

	return result;
}
