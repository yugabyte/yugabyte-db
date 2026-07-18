/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bson/bson_expression_set_operators.c
 *
 * Set Operator expression implementations of BSON.
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <math.h>
#include <common/hashfn.h>

#include "io/bson_core.h"
#include "operators/bson_expression.h"
#include "operators/bson_expression_operators.h"
#include "collation/collation.h"
#include "types/decimal128.h"
#include "utils/documentdb_errors.h"
#include "query/bson_compare.h"
#include "utils/hashset_utils.h"

/* --------------------------------------------------------- */
/* Type definitions */
/* --------------------------------------------------------- */
typedef void (*ProcessSetDualOperands)(void *state, const char *collationString,
									   bson_value_t *result);
typedef bool (*ProcessSetVariableOperands)(const bson_value_t *currentValue,
										   void *state,
										   bson_value_t *result, bool
										   isFieldPathExpression);


typedef struct DollarSetOperatorState
{
	/* Number of Total array for intersection */
	int arrayCount;

	/* This boolean variable indicates whether the current set matches the previous set. */
	bool isMatchWithPreviousSet;

	/* Hash Table to store frequency of elements */
	HTAB *arrayElementsHashTable;

	/* collation string for comparison, if any */
	const char *collationString;
} DollarSetOperatorState;

typedef struct VariadicSetOperatorState
{
	List *inputArgumentsList;

	/* collation string for comparison, if any */
	const char *collationString;
} VariadicSetOperatorState;

typedef struct BinarySetOperatorState
{
	AggregationExpressionData *firstValue;
	AggregationExpressionData *secondValue;

	/* collation string for comparison, if any */
	char *collationString;
} BinarySetOperatorState;

/*
 * Struct to store a set element in a collation-aware hash table
 * It extends BsonValueHashEntry in hash_utils.h
 */
typedef struct SetOperatorBsonValueHashEntry
{
	/* key for hash Entry; must be first field */
	bson_value_t bsonValue;

	/* collation string, if applicable; must be second field */
	const char *collationString;

	/* frequency value for hash entry */
	int count;

	/* store the number of array where element has seen last */
	int lastSeenArray;
} SetOperatorBsonValueHashEntry;

/* Size of the count and lastSeenArray fields in SetOperatorBsonValueHashEntry */
#define BsonValueHashEntryExtraDataSize (2 * sizeof(int))

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */

static void ParseSetDualOperands(const bson_value_t *argument,
								 AggregationExpressionData *data, const
								 char *operatorName,
								 ProcessSetDualOperands processOperatorFunc,
								 ParseAggregationExpressionContext *context);
static void HandlePreParsedSetDualOperands(pgbson *doc, void *arguments,
										   ExpressionResult *expressionResult,
										   bson_value_t *result,
										   ProcessSetDualOperands processOperatorFunc);
static void ParseSetVariableOperands(const bson_value_t *argument,
									 AggregationExpressionData *data,
									 DollarSetOperatorState *state,
									 ParseAggregationExpressionContext *context,
									 ProcessSetVariableOperands processOperatorFunc);
static void HandlePreParsedSetVariableOperands(pgbson *doc, void *arguments,
											   void *state, bson_value_t *result,
											   ExpressionResult *expressionResult,
											   ProcessSetVariableOperands
											   processOperatorFunc);
static bool ProcessDollarSetIntersection(const bson_value_t *currentElement,
										 void *state, bson_value_t *result,
										 bool isFieldPathExpression);
static void ProcessDollarSetIntersectionResult(void *state, bson_value_t *result);
static bool ProcessDollarSetUnion(const bson_value_t *currentValue, void *state,
								  bson_value_t *result,
								  bool isFieldPathExpression);
static void ProcessDollarSetUnionResult(void *state, bson_value_t *result);
static bool ProcessDollarSetEqualsElement(const bson_value_t *currentValue, void *state,
										  bson_value_t *result, bool
										  isFieldPathExpression);
static void ProcessDollarSetEqualsResult(void *state, bson_value_t *result);
static void ProcessDollarSetDifference(void *state, const char *collationString,
									   bson_value_t *result);
static void ProcessDollarSetIsSubset(void *state, const char *collationString,
									 bson_value_t *result);
static void ProcessSetElement(const bson_value_t *currentValue,
							  DollarSetOperatorState *state);
static bool ProcessDollarAllOrAnyElementsTrue(const bson_value_t *currentValue,
											  void *state, bson_value_t *result,
											  bool isFieldPathExpression);

/*
 * Parses a $setIntersection expression and sets the parsed data in the data argument.
 * $setIntersection is expressed as { "$setIntersection": [ [<expression1>], [<expression2>], ... ] }
 */
void
ParseDollarSetIntersection(const bson_value_t *argument,
						   AggregationExpressionData *data,
						   ParseAggregationExpressionContext *parseContext)
{
	data->value.value_type = BSON_TYPE_ARRAY;

	DollarSetOperatorState state =
	{
		.arrayCount = 0,
		.isMatchWithPreviousSet = true,
		.arrayElementsHashTable = CreateBsonValueWithCollationHashSet(
			BsonValueHashEntryExtraDataSize),
	};

	if (IsCollationApplicable(parseContext->collationString))
	{
		state.collationString = pstrdup(parseContext->collationString);
	}

	ParseSetVariableOperands(argument, data, &state, parseContext,
							 ProcessDollarSetIntersection);

	if (data->kind == AggregationExpressionKind_Constant)
	{
		ProcessDollarSetIntersectionResult(&state, &data->value);
	}
}


/*
 * Evaluates the output of an $setIntersection expression.
 * Since $setIntersection is expressed as { "$setIntersection": [ [<expression1>], [<expression2>], ... ] }
 * We evaluate the inner expressions and then return the Intersection of them.
 */
void
HandlePreParsedDollarSetIntersection(pgbson *doc, void *arguments,
									 ExpressionResult *expressionResult)
{
	DollarSetOperatorState state =
	{
		.arrayCount = 0,
		.isMatchWithPreviousSet = true,
		.arrayElementsHashTable = CreateBsonValueWithCollationHashSet(
			BsonValueHashEntryExtraDataSize),
	};

	bson_value_t result;
	result.value_type = BSON_TYPE_ARRAY;

	HandlePreParsedSetVariableOperands(doc, arguments, &state, &result, expressionResult,
									   ProcessDollarSetIntersection);

	if (result.value_type != BSON_TYPE_NULL)
	{
		ProcessDollarSetIntersectionResult(&state, &result);
	}

	ExpressionResultSetValue(expressionResult, &result);
}


/*
 * Parses a $setUnion expression and sets the parsed data in the data argument.
 * $setUnion is expressed as { "$setUnion": [ [<expression1>], [<expression2>], ... ] }
 */
void
ParseDollarSetUnion(const bson_value_t *argument,
					AggregationExpressionData *data,
					ParseAggregationExpressionContext *parseContext)
{
	data->value.value_type = BSON_TYPE_ARRAY;

	DollarSetOperatorState state =
	{
		.arrayCount = 0,
		.isMatchWithPreviousSet = true,
		.arrayElementsHashTable = CreateBsonValueWithCollationHashSet(
			BsonValueHashEntryExtraDataSize),
	};

	if (IsCollationApplicable(parseContext->collationString))
	{
		state.collationString = pstrdup(parseContext->collationString);
	}

	ParseSetVariableOperands(argument, data, &state, parseContext, ProcessDollarSetUnion);

	if (data->kind == AggregationExpressionKind_Constant)
	{
		ProcessDollarSetUnionResult(&state, &data->value);
	}
}


/*
 * Evaluates the output of an $setUnion expression.
 * Since $setUnion is expressed as { "$setUnion": [ [<expression1>], [<expression2>], ... ] }
 * We evaluate the inner expressions and then return the Union of them.
 */
void
HandlePreParsedDollarSetUnion(pgbson *doc, void *arguments,
							  ExpressionResult *expressionResult)
{
	DollarSetOperatorState state =
	{
		.arrayCount = 0,
		.isMatchWithPreviousSet = true,
		.arrayElementsHashTable = CreateBsonValueWithCollationHashSet(
			BsonValueHashEntryExtraDataSize),
	};

	bson_value_t result;
	result.value_type = BSON_TYPE_ARRAY;

	HandlePreParsedSetVariableOperands(doc, arguments, &state, &result, expressionResult,
									   ProcessDollarSetUnion);

	if (result.value_type != BSON_TYPE_NULL)
	{
		ProcessDollarSetUnionResult(&state, &result);
	}

	ExpressionResultSetValue(expressionResult, &result);
}


/*
 * Parses a $setEquals expression and sets the parsed data in the data argument.
 * $setEquals is expressed as { "$setEquals": [ [<expression1>], [<expression2>], ... ] }
 */
void
ParseDollarSetEquals(const bson_value_t *argument,
					 AggregationExpressionData *data,
					 ParseAggregationExpressionContext *parseContext)
{
	int numArgs = argument->value_type == BSON_TYPE_ARRAY ?
				  BsonDocumentValueCountKeys(argument) : 1;

	if (numArgs < 2)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION17045), errmsg(
							"$setEquals requires a minimum of two arguments, but received %d",
							numArgs)));
	}

	data->value.value_type = BSON_TYPE_BOOL;
	data->value.value.v_bool = false;

	DollarSetOperatorState state =
	{
		.arrayCount = 0,
		.isMatchWithPreviousSet = true,
		.arrayElementsHashTable = CreateBsonValueWithCollationHashSet(
			BsonValueHashEntryExtraDataSize),
	};

	if (IsCollationApplicable(parseContext->collationString))
	{
		state.collationString = pstrdup(parseContext->collationString);
	}

	ParseSetVariableOperands(argument, data, &state, parseContext,
							 ProcessDollarSetEqualsElement);

	if (data->kind == AggregationExpressionKind_Constant)
	{
		ProcessDollarSetEqualsResult(&state, &data->value);
	}
}


/*
 * Evaluates the output of an $setEquals expression.
 * Since $setEquals is expressed as { "$setEquals": [ [<expression1>], [<expression2>], ... ] }
 * We evaluate the inner expressions and then return true if sets are equal.
 */
void
HandlePreParsedDollarSetEquals(pgbson *doc, void *arguments,
							   ExpressionResult *expressionResult)
{
	DollarSetOperatorState state =
	{
		.arrayCount = 0,
		.isMatchWithPreviousSet = true,
		.arrayElementsHashTable = CreateBsonValueWithCollationHashSet(
			BsonValueHashEntryExtraDataSize),
	};

	bson_value_t result;
	result.value_type = BSON_TYPE_BOOL;
	result.value.v_bool = false;

	HandlePreParsedSetVariableOperands(doc, arguments, &state, &result, expressionResult,
									   ProcessDollarSetEqualsElement);

	ProcessDollarSetEqualsResult(&state, &result);

	ExpressionResultSetValue(expressionResult, &result);
}


/*
 * Parses a $setDifference expression and sets the parsed data in the data argument.
 * $setDifference is expressed as { "$setDifference": [ [<expression1>], [<expression2>] ] }
 */
void
ParseDollarSetDifference(const bson_value_t *argument,
						 AggregationExpressionData *data,
						 ParseAggregationExpressionContext *parseContext)
{
	data->value.value_type = BSON_TYPE_ARRAY;
	ParseSetDualOperands(argument, data, "$setDifference", ProcessDollarSetDifference,
						 parseContext);
}


/*
 * Evaluates the output of an $setDifference expression.
 * Since $setDifference is expressed as { "$setDifference": [ [<expression1>], [<expression2>] ] }
 * We evaluate the inner expressions and then return the difference of them.
 */
void
HandlePreParsedDollarSetDifference(pgbson *doc, void *arguments,
								   ExpressionResult *expressionResult)
{
	bson_value_t result;
	result.value_type = BSON_TYPE_ARRAY;
	HandlePreParsedSetDualOperands(doc, arguments, expressionResult, &result,
								   ProcessDollarSetDifference);
}


/*
 * Parses a $setIsSubset expression and sets the parsed data in the data argument.
 * $setIsSubset is expressed as { "$setIsSubset": [ [<expression1>], [<expression2>] ] }
 */
void
ParseDollarSetIsSubset(const bson_value_t *argument,
					   AggregationExpressionData *data,
					   ParseAggregationExpressionContext *parseContext)
{
	data->value.value_type = BSON_TYPE_BOOL;
	data->value.value.v_bool = false;

	ParseSetDualOperands(argument, data, "$setIsSubset", ProcessDollarSetIsSubset,
						 parseContext);
}


/*
 * Evaluates the output of an $setIsSubset expression.
 * Since $setIsSubset is expressed as { "$setIsSubset": [ [<expression1>], [<expression2>] ] }
 * We evaluate the inner expressions and then return true if set1 is subset of set2.
 */
void
HandlePreParsedDollarSetIsSubset(pgbson *doc, void *arguments,
								 ExpressionResult *expressionResult)
{
	bson_value_t result;
	result.value_type = BSON_TYPE_BOOL;
	HandlePreParsedSetDualOperands(doc, arguments, expressionResult, &result,
								   ProcessDollarSetIsSubset);
}


/*
 * Parses a $anyElementTrue expression and sets the parsed data in the data argument.
 * $anyElementTrue is expressed as { "$anyElementTrue": [ [<expression1>], [<expression2>], ... ] }
 */
void
ParseDollarAnyElementTrue(const bson_value_t *argument,
						  AggregationExpressionData *data,
						  ParseAggregationExpressionContext *parseContext)
{
	int numOfRequiredArgs = 1;
	data->operator.arguments = ParseFixedArgumentsForExpression(argument,
																numOfRequiredArgs,
																"$anyElementTrue",
																&data->operator.
																argumentsKind,
																parseContext);
}


/*
 * Evaluates the output of an $anyElementTrue expression.
 * Since $anyElementTrue is expressed as { "$anyElementTrue": [ [<expression1>], [<expression2>], ... ] }
 * We evaluate the inner expressions and then return true if any element is true.
 */
void
HandlePreParsedDollarAnyElementTrue(pgbson *doc, void *arguments,
									ExpressionResult *expressionResult)
{
	AggregationExpressionData *argument = (AggregationExpressionData *) arguments;

	bool hasFieldExpression = false;
	bool isNullOnEmpty = false;
	ExpressionResult childResult = ExpressionResultCreateChild(expressionResult);
	EvaluateAggregationExpressionData(argument, doc, &childResult, isNullOnEmpty);

	bson_value_t argumentValue = childResult.value;
	hasFieldExpression = childResult.isFieldPathExpression;

	bson_value_t result;
	result.value_type = BSON_TYPE_BOOL;
	bool checkAllElementsTrueInArray = false;

	ProcessDollarAllOrAnyElementsTrue(&argumentValue,
									  &checkAllElementsTrueInArray, &result,
									  hasFieldExpression);
	ExpressionResultSetValue(expressionResult, &result);
}


/*
 * Parses a $allElementsTrue expression and sets the parsed data in the data argument.
 * $allElementsTrue is expressed as { "$allElementsTrue": [ [<expression1>], [<expression2>], ... ] }
 */
void
ParseDollarAllElementsTrue(const bson_value_t *argument,
						   AggregationExpressionData *data,
						   ParseAggregationExpressionContext *parseContext)
{
	int numOfRequiredArgs = 1;
	data->operator.arguments = ParseFixedArgumentsForExpression(argument,
																numOfRequiredArgs,
																"$allElementsTrue",
																&data->operator.
																argumentsKind,
																parseContext);
}


/*
 * Evaluates the output of an $allElementsTrue expression.
 * Since $allElementsTrue is expressed as { "$allElementsTrue": [ [<expression1>], [<expression2>], ... ] }
 * We evaluate the inner expressions and then return true if all elements are true.
 */
void
HandlePreParsedDollarAllElementsTrue(pgbson *doc, void *arguments,
									 ExpressionResult *expressionResult)
{
	AggregationExpressionData *argument = (AggregationExpressionData *) arguments;

	bool hasFieldExpression = false;
	bool isNullOnEmpty = false;
	ExpressionResult childResult = ExpressionResultCreateChild(expressionResult);
	EvaluateAggregationExpressionData(argument, doc, &childResult, isNullOnEmpty);

	bson_value_t argumentValue = childResult.value;
	hasFieldExpression = childResult.isFieldPathExpression;

	bson_value_t result;
	result.value_type = BSON_TYPE_BOOL;
	bool checkAllElementsTrueInArray = true;

	ProcessDollarAllOrAnyElementsTrue(&argumentValue, &checkAllElementsTrueInArray,
									  &result, hasFieldExpression);
	ExpressionResultSetValue(expressionResult, &result);
}


/* Helper to parse arithmetic operators that take strictly two arguments. */
static void
ParseSetDualOperands(const bson_value_t *argument,
					 AggregationExpressionData *data, const
					 char *operatorName,
					 ProcessSetDualOperands processOperatorFunc,
					 ParseAggregationExpressionContext *context)
{
	int numOfRequiredArgs = 2;
	List *arguments = ParseFixedArgumentsForExpression(argument,
													   numOfRequiredArgs,
													   operatorName,
													   &data->operator.argumentsKind,
													   context);

	AggregationExpressionData *firstArg = list_nth(arguments, 0);
	AggregationExpressionData *secondArg = list_nth(arguments, 1);

	/* If both arguments are constants: compute comparison result, change
	 * expression type to constant, store the result in the expression value
	 * and free the arguments list as it won't be needed anymore. */
	if (IsAggregationExpressionConstant(firstArg) && IsAggregationExpressionConstant(
			secondArg))
	{
		DualArgumentExpressionState state;
		memset(&state, 0, sizeof(DualArgumentExpressionState));

		InitializeDualArgumentExpressionState(firstArg->value, secondArg->value, false,
											  &state);

		const char *collationString = IsCollationApplicable(context->collationString) ?
									  context->collationString : NULL;
		processOperatorFunc(&state, collationString, &data->value);

		data->kind = AggregationExpressionKind_Constant;
		list_free_deep(arguments);
	}
	else
	{
		BinarySetOperatorState *binarySetOperatorState = palloc0(
			sizeof(BinarySetOperatorState));
		binarySetOperatorState->firstValue = firstArg;
		binarySetOperatorState->secondValue = secondArg;

		if (IsCollationApplicable(context->collationString))
		{
			binarySetOperatorState->collationString = pstrdup(context->collationString);
		}

		data->operator.arguments = binarySetOperatorState;
		data->operator.argumentsKind = AggregationExpressionArgumentsKind_Palloc;
	}
}


/* Helper to evaluate pre-parsed expressions of set operators that take strictly two operands. */
static void
HandlePreParsedSetDualOperands(pgbson *doc, void *arguments,
							   ExpressionResult *expressionResult, bson_value_t *result,
							   ProcessSetDualOperands
							   processOperatorFunc)
{
	BinarySetOperatorState *operatorState = (BinarySetOperatorState *) arguments;
	AggregationExpressionData *firstArg = operatorState->firstValue;
	AggregationExpressionData *secondArg = operatorState->secondValue;

	bool hasFieldExpression = false;
	bool isNullOnEmpty = false;
	ExpressionResult childResult = ExpressionResultCreateChild(expressionResult);
	EvaluateAggregationExpressionData(firstArg, doc, &childResult, isNullOnEmpty);

	bson_value_t firstValue = childResult.value;
	hasFieldExpression = childResult.isFieldPathExpression;

	ExpressionResultReset(&childResult);
	EvaluateAggregationExpressionData(secondArg, doc, &childResult, isNullOnEmpty);
	hasFieldExpression = hasFieldExpression || childResult.isFieldPathExpression;

	bson_value_t secondValue = childResult.value;

	DualArgumentExpressionState state;
	memset(&state, 0, sizeof(DualArgumentExpressionState));

	InitializeDualArgumentExpressionState(firstValue, secondValue, hasFieldExpression,
										  &state);
	processOperatorFunc(&state, operatorState->collationString, result);

	ExpressionResultSetValue(expressionResult, result);
}


/* Helper to parse set operators that take variable number of operands. */
static void
ParseSetVariableOperands(const bson_value_t *argument,
						 AggregationExpressionData *data,
						 DollarSetOperatorState *state,
						 ParseAggregationExpressionContext *parseContext,
						 ProcessSetVariableOperands processOperatorFunc)
{
	bool areArgumentsConstant = true;
	List *argumentsList = ParseVariableArgumentsForExpression(argument,
															  &areArgumentsConstant,
															  parseContext);

	if (areArgumentsConstant)
	{
		int idx = 0;

		while (argumentsList != NIL && idx < argumentsList->length)
		{
			AggregationExpressionData *currentData = list_nth(argumentsList, idx);

			bool continueEnumerating = processOperatorFunc(&currentData->value, state,
														   &data->value, false);
			if (!continueEnumerating)
			{
				break;
			}

			idx++;
		}

		data->kind = AggregationExpressionKind_Constant;
		list_free_deep(argumentsList);
	}
	else
	{
		VariadicSetOperatorState *operatorState = palloc0(
			sizeof(VariadicSetOperatorState));
		operatorState->inputArgumentsList = argumentsList;

		if (IsCollationApplicable(parseContext->collationString))
		{
			operatorState->collationString = pstrdup(parseContext->collationString);
		}

		data->operator.arguments = operatorState;
		data->operator.argumentsKind = AggregationExpressionArgumentsKind_Palloc;
	}
}


/* Helper to evaluate pre-parsed expressions of set operators that take variable number of operands. */
static void
HandlePreParsedSetVariableOperands(pgbson *doc, void *arguments,
								   void *state,
								   bson_value_t *result,
								   ExpressionResult *expressionResult,
								   ProcessSetVariableOperands
								   processOperatorFunc)
{
	VariadicSetOperatorState *operatorState = (VariadicSetOperatorState *) arguments;
	List *argumentList = operatorState->inputArgumentsList;

	DollarSetOperatorState *dollarSetState = (DollarSetOperatorState *) state;
	if (IsCollationApplicable(operatorState->collationString))
	{
		dollarSetState->collationString = pstrdup(operatorState->collationString);
	}

	int idx = 0;
	while (argumentList != NIL && idx < argumentList->length)
	{
		AggregationExpressionData *currentData = list_nth(argumentList, idx);

		bool isNullOnEmpty = false;
		ExpressionResult childResult = ExpressionResultCreateChild(expressionResult);
		EvaluateAggregationExpressionData(currentData, doc, &childResult, isNullOnEmpty);

		bson_value_t currentValue = childResult.value;

		bool continueEnumerating = processOperatorFunc(&currentValue, dollarSetState,
													   result,
													   childResult.
													   isFieldPathExpression);
		if (!continueEnumerating)
		{
			return;
		}

		idx++;
	}
}


/* Function that processes a single argument for $setIntersection. */
static bool
ProcessDollarSetIntersection(const bson_value_t *currentElement, void *state,
							 bson_value_t *result,
							 bool isFieldPathExpression)
{
	if (IsExpressionResultNullOrUndefined(currentElement))
	{
		result->value_type = BSON_TYPE_NULL;
		return false; /* stop processing more arguments. */
	}

	if (currentElement->value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION17047), errmsg(
							"All operands of $setIntersection must be arrays, but one provided argument has type: %s",
							BsonTypeName(currentElement->value_type))));
	}

	ProcessSetElement(currentElement, (DollarSetOperatorState *) state);
	return true;
}


/* Function that validates the final state before returning the result for $setIntersection. */
static void
ProcessDollarSetIntersectionResult(void *state, bson_value_t *result)
{
	DollarSetOperatorState *intersectionState = (DollarSetOperatorState *) state;

	if (result->value_type == BSON_TYPE_NULL)
	{
		hash_destroy(intersectionState->arrayElementsHashTable);
		return;
	}

	HASH_SEQ_STATUS seq_status;
	SetOperatorBsonValueHashEntry *entry;

	hash_seq_init(&seq_status, intersectionState->arrayElementsHashTable);
	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	pgbson_array_writer arrayWriter;
	PgbsonWriterStartArray(&writer, "", 0, &arrayWriter);

	while ((entry = hash_seq_search(&seq_status)) != NULL)
	{
		if (entry->count == intersectionState->arrayCount)
		{
			PgbsonArrayWriterWriteValue(&arrayWriter, &entry->bsonValue);
		}
	}

	PgbsonWriterEndArray(&writer, &arrayWriter);
	hash_destroy(intersectionState->arrayElementsHashTable);
	*result = PgbsonArrayWriterGetValue(&arrayWriter);
}


/* Function that processes a single argument for $setUnion. */
static bool
ProcessDollarSetUnion(const bson_value_t *currentValue, void *state, bson_value_t *result,
					  bool isFieldPathExpression)
{
	if (IsExpressionResultNullOrUndefined(currentValue))
	{
		result->value_type = BSON_TYPE_NULL;
		return false; /* stop processing more arguments. */
	}

	if (currentValue->value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION17043), errmsg(
							"All operands of $setUnion are required to be arrays, but one provided argument has the type: %s",
							BsonTypeName(currentValue->value_type))));
	}

	ProcessSetElement(currentValue, (DollarSetOperatorState *) state);
	return true;
}


/* Function that validates the final state before returning the result for $setUnion. */
static void
ProcessDollarSetUnionResult(void *state, bson_value_t *result)
{
	DollarSetOperatorState *unionState = (DollarSetOperatorState *) state;

	if (result->value_type == BSON_TYPE_NULL)
	{
		hash_destroy(unionState->arrayElementsHashTable);
		return;
	}

	HASH_SEQ_STATUS seq_status;
	SetOperatorBsonValueHashEntry *entry;

	hash_seq_init(&seq_status, unionState->arrayElementsHashTable);
	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	pgbson_array_writer arrayWriter;
	PgbsonWriterStartArray(&writer, "", 0, &arrayWriter);

	while ((entry = hash_seq_search(&seq_status)) != NULL)
	{
		PgbsonArrayWriterWriteValue(&arrayWriter, &entry->bsonValue);
	}

	PgbsonWriterEndArray(&writer, &arrayWriter);
	hash_destroy(unionState->arrayElementsHashTable);
	*result = PgbsonArrayWriterGetValue(&arrayWriter);
}


/* Function that processes a single argument for $setEquals. */
static bool
ProcessDollarSetEqualsElement(const bson_value_t *currentElement, void *state,
							  bson_value_t *result,
							  bool isFieldPathExpression)
{
	if (currentElement->value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION17044), errmsg(
							"All operands of $setEquals must be arrays; one provided argument is of type: %s",
							BsonTypeNameExtended(currentElement->value_type))));
	}

	DollarSetOperatorState *setEqualsState = (DollarSetOperatorState *) state;
	ProcessSetElement(currentElement, setEqualsState);

	/* Since the current set did not match the previous set, there is no need to proceed with further checks, and an early exit can be taken */
	if (!setEqualsState->isMatchWithPreviousSet)
	{
		return false;
	}

	return true;
}


/* Function that validates the final state before returning the result for $setEquals. */
static void
ProcessDollarSetEqualsResult(void *state, bson_value_t *result)
{
	DollarSetOperatorState *setEqualsState = (DollarSetOperatorState *) state;

	if (!setEqualsState->isMatchWithPreviousSet)
	{
		hash_destroy(setEqualsState->arrayElementsHashTable);
		return;
	}

	HASH_SEQ_STATUS seq_status;
	SetOperatorBsonValueHashEntry *entry;

	hash_seq_init(&seq_status, setEqualsState->arrayElementsHashTable);


	bool isEqual = true;
	while ((entry = hash_seq_search(&seq_status)) != NULL)
	{
		if (entry->count != setEqualsState->arrayCount)
		{
			isEqual = false;
			hash_seq_term(&seq_status);
			break;
		}
	}

	result->value.v_bool = isEqual;
	hash_destroy(setEqualsState->arrayElementsHashTable);
}


/* Function that validates the final state before returning the result for $setDifference. */
static void
ProcessDollarSetDifference(void *state, const char *collationString, bson_value_t *result)
{
	DualArgumentExpressionState *context = (DualArgumentExpressionState *) state;

	if (context->hasNullOrUndefined)
	{
		result->value_type = BSON_TYPE_NULL;
		return;
	}

	if (context->firstArgument.value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION17048), errmsg(
							"Both operands in $setDifference must be arrays, but the first provided argument is of type: %s",
							BsonTypeName(context->firstArgument.value_type))));
	}

	if (context->secondArgument.value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION17049), errmsg(
							"Both operands in $setDifference need to be arrays, but the second one has type: %s",
							BsonTypeName(context->secondArgument.value_type))));
	}

	DollarSetOperatorState setDifferenceState =
	{
		.arrayCount = 0,
		.isMatchWithPreviousSet = true,
		.arrayElementsHashTable = CreateBsonValueWithCollationHashSet(
			BsonValueHashEntryExtraDataSize),
		.collationString = collationString
	};

	ProcessSetElement(&context->secondArgument, &setDifferenceState);

	bson_iter_t arrayIterator;
	BsonValueInitIterator(&context->firstArgument, &arrayIterator);

	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	pgbson_array_writer arrayWriter;
	PgbsonWriterStartArray(&writer, "", 0, &arrayWriter);

	while (bson_iter_next(&arrayIterator))
	{
		const bson_value_t *arrayElement = bson_iter_value(&arrayIterator);
		SetOperatorBsonValueHashEntry elementToFind = {
			.bsonValue = *arrayElement, .collationString = collationString
		};

		bool found = false;

		hash_search(setDifferenceState.arrayElementsHashTable,
					&elementToFind,
					HASH_ENTER,
					&found);

		if (!found)
		{
			PgbsonArrayWriterWriteValue(&arrayWriter, arrayElement);
		}
	}

	PgbsonWriterEndArray(&writer, &arrayWriter);
	hash_destroy(setDifferenceState.arrayElementsHashTable);
	*result = PgbsonArrayWriterGetValue(&arrayWriter);
}


/* Function that validates the final state before returning the result for $setIsSubset. */
static void
ProcessDollarSetIsSubset(void *state, const char *collationString, bson_value_t *result)
{
	DualArgumentExpressionState *context = (DualArgumentExpressionState *) state;

	if (context->firstArgument.value_type != BSON_TYPE_ARRAY)
	{
		int errorCode = ERRCODE_DOCUMENTDB_LOCATION17310;
		char *typeName = BsonTypeNameExtended(context->firstArgument.value_type);

		if (context->firstArgument.value_type != BSON_TYPE_EOD)
		{
			errorCode = ERRCODE_DOCUMENTDB_LOCATION17046;
		}

		ereport(ERROR, (errcode(errorCode), errmsg(
							"Both operands used with $setIsSubset should be arrays, but the first provided argument is of type: %s",
							typeName)));
	}

	if (context->secondArgument.value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION17042), errmsg(
							"Both operands in $setIsSubset must be arrays, but the second operand provided is of type: %s",
							BsonTypeNameExtended(context->secondArgument.value_type))));
	}

	DollarSetOperatorState setIsSubsetState =
	{
		.arrayCount = 0,
		.isMatchWithPreviousSet = true,
		.arrayElementsHashTable = CreateBsonValueWithCollationHashSet(
			BsonValueHashEntryExtraDataSize),
		.collationString = collationString
	};

	ProcessSetElement(&context->secondArgument, &setIsSubsetState);

	bson_iter_t arrayIterator;
	BsonValueInitIterator(&context->firstArgument, &arrayIterator);

	bool isSubset = true;
	while (bson_iter_next(&arrayIterator))
	{
		const bson_value_t *arrayElement = bson_iter_value(&arrayIterator);
		SetOperatorBsonValueHashEntry elementToFind = {
			.bsonValue = *arrayElement, .collationString = collationString
		};

		bool found = false;

		hash_search(setIsSubsetState.arrayElementsHashTable,
					&elementToFind,
					HASH_FIND,
					&found);

		if (!found)
		{
			isSubset = false;
			break;
		}
	}

	hash_destroy(setIsSubsetState.arrayElementsHashTable);
	result->value_type = BSON_TYPE_BOOL;
	result->value.v_bool = isSubset;
}


/*
 * For the currentElement which is of type BSON_TYPE_ARRAY,
 * iterate through currentElement and add all unique elements hash table
 * increment the frequency of each added element in the HTable.
 */
static void
ProcessSetElement(const bson_value_t *currentValue,
				  DollarSetOperatorState *state)
{
	HTAB *arrayElementsHashTable = state->arrayElementsHashTable;
	bson_iter_t arrayIterator;
	BsonValueInitIterator(currentValue, &arrayIterator);
	state->arrayCount++;

	while (bson_iter_next(&arrayIterator))
	{
		const bson_value_t *arrayElement = bson_iter_value(&arrayIterator);
		SetOperatorBsonValueHashEntry elementToFind = {
			.bsonValue = *arrayElement, .collationString = state->collationString
		};

		bool found = false;
		SetOperatorBsonValueHashEntry *foundElement =
			(SetOperatorBsonValueHashEntry *) hash_search(arrayElementsHashTable,
														  &elementToFind,
														  HASH_ENTER,
														  &found);

		/* This condition ensures that the element being checked is not a duplicate in current array. */
		if (foundElement->lastSeenArray != state->arrayCount)
		{
			foundElement->count++;
		}

		if (state->arrayCount > 1 && !found)
		{
			state->isMatchWithPreviousSet = false;
		}

		/* By assigning the current arrayCount to lastSeenArray, */
		/* we can effectively disregard any subsequent occurrences of the same element in this array. */
		foundElement->lastSeenArray = state->arrayCount;
	}
}


/*
 * Function that processes a single argument for $allElementsTrue $anyElementTrue and find the result.
 */
static bool
ProcessDollarAllOrAnyElementsTrue(const bson_value_t *currentValue, void *state,
								  bson_value_t *result, bool isFieldPathExpression)
{
	bool IsAllElementsTrueOp = *((bool *) state);

	if (currentValue->value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(IsAllElementsTrueOp ? ERRCODE_DOCUMENTDB_LOCATION17040 :
								ERRCODE_DOCUMENTDB_LOCATION17041),
						errmsg(
							"The argument provided for %s must be of array type, yet it is actually %s.",
							IsAllElementsTrueOp ? "$allElementsTrue" :
							"$anyElementTrue",
							BsonTypeNameExtended(currentValue->value_type))));
	}

	bson_iter_t arrayIterator;
	BsonValueInitIterator(currentValue, &arrayIterator);
	result->value_type = BSON_TYPE_BOOL;
	result->value.v_bool = IsAllElementsTrueOp;

	while (bson_iter_next(&arrayIterator))
	{
		bool currElement = BsonValueAsBool(bson_iter_value(&arrayIterator));

		/* if operator is $allElementsTrue and currElement is false then result will be false */
		if (IsAllElementsTrueOp && !currElement)
		{
			result->value.v_bool = false;
			break;
		}

		/* if operator is $anyElementTrue and currElement is true then result will be true */
		if (!IsAllElementsTrueOp && currElement)
		{
			result->value.v_bool = true;
			break;
		}
	}

	return true;
}
