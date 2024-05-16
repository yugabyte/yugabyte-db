/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bson/bson_expression_array_operators.c
 *
 * Array Operator expression implementations of BSON.
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <math.h>
#include <miscadmin.h>
#include <nodes/pg_list.h>
#include <utils/hsearch.h>

#include "io/helio_bson_core.h"
#include "query/helio_bson_compare.h"
#include "operators/bson_expression.h"
#include "operators/bson_expression_operators.h"
#include "types/decimal128.h"
#include "utils/hashset_utils.h"
#include "commands/commands_common.h"
#include "utils/sort_utils.h"
#include "utils/heap_utils.h"

#include "planner/helio_planner.h"

#define MAX_BUFFER_SIZE_DOLLAR_RANGE (64 * 1024 * 1024)
#define EMPTY_BSON_ARRAY_SIZE_BYTES 5 /* size of empty array is fixed as 5 bytes. */

/*
 * This value is required as array can't exist alone.
 * It will exist in a document and this is what libbson also does.
 */
#define SIZE_OF_PARENT_OF_ARRAY_FOR_BSON 7


/* --------------------------------------------------------- */
/* Type declaration */
/* --------------------------------------------------------- */

/* State for a $arrayElemAt, $first or $last operator. */
typedef struct ArrayElemAtArgumentState
{
	DualArgumentExpressionState dualState; /* Must be first element */

	/* Indicates if the operator $arrayElemAt operator.
	 * If false, it means it is either $first or $last. */
	bool isArrayElemAtOperator;

	/* Name of operator for logging purposes. */
	const char *opName;
} ArrayElemAtArgumentState;

/* State for $concatArray operator. */
typedef struct ConcatArraysState
{
	/* The parent writer which holds the buffer for the array writer. */
	pgbson_writer writer;

	/* The actual array writer. */
	pgbson_array_writer arrayWriter;
} ConcatArraysState;

/* Struct that represents the parsed arguments to a $filter expression. */
typedef struct DollarFilterArguments
{
	/* The array input to the $filter expression. */
	AggregationExpressionData input;

	/* The filter condition to evaluate against every element in the input array. */
	AggregationExpressionData cond;

	/* Optional: The variable name to use to evaluate each element of the array. (defaults to: "this") */
	AggregationExpressionData alias;

	/* Optional: The limit of elements we should return in the result array. (defaults to all elements in the array). */
	AggregationExpressionData limit;
} DollarFilterArguments;


/* Struct that represents the parsed arguments to a $firstN/$lastN expression. */
typedef struct DollarFirstNLastNArguments
{
	/* The array input to the $filter expression. */
	AggregationExpressionData input;

	/* The limit of elements we should return in the result array*/
	AggregationExpressionData elementsToFetch;
} DollarFirstNLastNArguments;

/* Struct that represents the parsed arguments to a $map expression. */
typedef struct DollarMapArguments
{
	/* The array input to the $map expression. */
	AggregationExpressionData input;

	/* The field condition to evaluate against every element in the input array. */
	AggregationExpressionData in;

	/* Optional: A name for the variable that represents each individual element of the input array. */
	AggregationExpressionData as;
} DollarMapArguments;

/* Struct that represents the parsed arguments to a $sortArray expression. */
typedef struct DollarSortArrayArguments
{
	/* The array input to the $sortArray expression. */
	AggregationExpressionData input;

	/* document specifies a sort ordering. */
	SortContext sortContext;
} DollarSortArrayArguments;

/* Struct that represents the parsed arguments to a $maxN / $minN expression. */
typedef struct DollarMaxMinNArguments
{
	/* The array input to the $maxN or $minN expression. */
	AggregationExpressionData input;

	/* The maxn n: <numeric-expression>, the number of results from input array. */
	AggregationExpressionData n;

	/* This bool serves both maxn and minn. true: maxn; false: minn;*/
	bool isMaxN;
} DollarMaxMinNArguments;

/* Struct that represents the parsed arguments to a $reduce expression. */
typedef struct DollarReduceArguments
{
	/* The array input to the $reduce expression. */
	AggregationExpressionData input;

	/* The field condition to evaluate against every element in the input array. */
	AggregationExpressionData in;

	/* The initial cumulative value set before in is applied to the first element of the input array.. */
	AggregationExpressionData initialValue;
} DollarReduceArguments;

/* Struct that represents the parsed arguments to a $zip expression. */
typedef struct DollarZipArguments
{
	/* The array input to the $zip expression. */
	AggregationExpressionData inputs;

	/* The boolean value specifies whether the length of the longest array determines the number of arrays in the output array. */
	AggregationExpressionData useLongestLength;

	/* The array of default element values to use if input arrays have different lengths. */
	AggregationExpressionData defaults;
} DollarZipArguments;

/* Struct that represents the parsed inputs argument of $zip expression. */
/* This struct is to pass the middle result of parsed inputs argument to main logic. */
typedef struct ZipParseInputsResult
{
	/* The array stores pointers to each subarray in inputs. */
	bson_value_t **inputsElements;

	/* The array stores the lengths of each subarray in inputs. */
	int *inputsElementLengths;

	/* The length of subarrays in output. */
	/* If useLongestLength is true, this will be the length of longest subarray in inputs */
	/* If useLongestLength is false, this will be the length of the shortest subarray in inputs */
	int outputSubArrayLength;
} ZipParseInputsResult;

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */
static void ProcessDollarIn(bson_value_t *result, void *state);
static void ProcessDollarSlice(bson_value_t *result, void *state);
static void ProcessDollarArrayElemAt(bson_value_t *result, void *state);
static bool ProcessDollarIsArrayElement(bson_value_t *result, const
										bson_value_t *currentElement,
										bool isFieldPathExpression, void *state);
static bool ProcessDollarSizeElement(bson_value_t *result, const
									 bson_value_t *currentElement,
									 bool isFieldPathExpression, void *state);
static bool ProcessDollarArrayToObjectElement(bson_value_t *result,
											  const bson_value_t *currentElement,
											  bool isFieldPathExpression, void *state);
static bool ProcessDollarObjectToArrayElement(bson_value_t *result,
											  const bson_value_t *currentElement,
											  bool isFieldPathExpression, void *state);
static bool ProcessDollarConcatArraysElement(bson_value_t *result,
											 const bson_value_t *currentElement,
											 bool isFieldPathExpression, void *state);
static void ProcessDollarMaxMinN(bson_value_t *result, bson_value_t *evaluatedInput,
								 bson_value_t *evaluatedLimit, bool isMaxN);
static void ProcessDollarConcatArraysResult(bson_value_t *result, void *state);
static void ParseDollarMaxMinN(const bson_value_t *argument,
							   AggregationExpressionData *data,
							   bool isMaxN);
static pgbsonelement ParseElementFromObjectForArrayToObject(const bson_value_t *element);
static pgbsonelement ParseElementFromArrayForArrayToObject(const bson_value_t *element);
static void ParseInputDocumentForFirstAndLastN(const bson_value_t *inputDocument,
											   bson_value_t *input,
											   bson_value_t *elementsToFetch, const
											   char *opName);
static void ValidateElementForFirstAndLastN(bson_value_t *elementsToFetch,
											const char *opName);
static void FillResultForDollarFirstAndLastN(bson_value_t *input,
											 bson_value_t *elementsToFetch,
											 bool isSkipElement,
											 bson_value_t *result);
static int32_t GetStartValueForDollarRange(bson_value_t *startValue);
static int32_t GetEndValueForDollarRange(bson_value_t *endValue);
static int32_t GetStepValueForDollarRange(bson_value_t *stepValue);
static void HandlerForParsingMinMax(bool isMax, const bson_value_t *argument,
									AggregationExpressionData *data);
static void HandlerForParsingSumAvg(bool isSum, const bson_value_t *argument,
									AggregationExpressionData *data);
static void SetResultArrayForDollarRange(int32_t startValue, int32_t endValue,
										 int32_t stepValue, bson_value_t *result);
static void ValidateArraySizeLimit(int32_t startValue, int32_t endValue,
								   int32_t stepValue);
static int32 GetIndexValueFromDollarIdxInput(bson_value_t *arg, bool isStartIndex);
static int32 FindIndexInArrayForElement(bson_value_t *array, bson_value_t *element,
										int32 startIndex, int32 endIndex);
static void SetResultArrayForDollarReverse(bson_value_t *array, bson_value_t *result);
static void SetResultValueForDollarMaxMin(const bson_value_t *inputArgument,
										  bson_value_t *result, bool isFindMax);
static void SetResultValueForDollarSumAvg(const bson_value_t *inputArgument,
										  bson_value_t *result, bool isSum);
static bool HeapSortComparatorMaxN(const void *first, const void *second);
static bool HeapSortComparatorMinN(const void *first, const void *second);
static bson_value_t * ParseZipDefaultsArgument(int rowNum, bson_value_t
											   evaluatedDefaultsArg, bool
											   useLongestLengthArgBoolValue);
static ZipParseInputsResult ParseZipInputsArgument(int rowNum, bson_value_t
												   evaluatedInputsArg, bool
												   useLongestLengthArgBoolValue);
static void ProcessDollarZip(bson_value_t evaluatedInputsArg, bson_value_t
							 evaluatedLongestLengthArg, bson_value_t evaluatedDefaultsArg,
							 bson_value_t *resultPtr);
static void SetResultArrayForDollarZip(int rowNum, ZipParseInputsResult parsedInputs,
									   bson_value_t *defaultsElements,
									   bson_value_t *resultPtr);
static void ProcessDollarSortArray(bson_value_t *inputValue, SortContext *sortContext,
								   bson_value_t *result);

/*
 * validate second and third argument of dollar slice operator
 */
static inline void
DollarSliceInputValidation(bson_value_t *inputValue, bool isSecondArg)
{
	if (!BsonValueIsNumber(inputValue))
	{
		ereport(ERROR, (errcode(isSecondArg ?
								MongoDollarSliceInvalidTypeSecondArg :
								MongoDollarSliceInvalidTypeThirdArg),
						errmsg(
							"%s argument to $slice must be numeric, but is of type: %s",
							isSecondArg ? "Second" : "Third",
							BsonTypeName(inputValue->value_type)),
						errhint(
							"%s argument to $slice must be numeric, but is of type: %s",
							isSecondArg ? "Second" : "Third",
							BsonTypeName(inputValue->value_type))));
	}

	bool checkForFixedInteger = true;

	if (!IsBsonValue32BitInteger(inputValue, checkForFixedInteger))
	{
		ereport(ERROR, (errcode(isSecondArg ?
								MongoDollarSliceInvalidValueSecondArg :
								MongoDollarSliceInvalidValueThirdArg),
						errmsg(
							"%s argument to $slice can't be represented as a 32-bit integer: %s",
							isSecondArg ? "Second" : "Third",
							BsonValueToJsonForLogging(inputValue)),
						errhint(
							"%s argument of type %s to $slice can't be represented as a 32-bit integer",
							isSecondArg ? "Second" : "Third",
							BsonTypeName(inputValue->value_type))));
	}
}


/*
 * Evaluates the output of a $isArray expression.
 * Since a $isArray is expressed as { "$isArray": <expression> }
 * or { "$isArray": [ <expression> ] }
 * We evaluate the inner expression and then return isArray.
 */
void
HandleDollarIsArray(pgbson *doc, const bson_value_t *operatorValue,
					ExpressionResult *expressionResult)
{
	ExpressionArgumentHandlingContext context =
	{
		.processElementFunc = ProcessDollarIsArrayElement,
		.processExpressionResultFunc = NULL,
		.state = NULL,
	};

	int numberOfRequiredArgs = 1;
	HandleFixedArgumentExpression(doc, operatorValue, expressionResult,
								  numberOfRequiredArgs, "$isArray", &context);
}


/*
 * Evaluates the output of a $in expression.
 * $in is expressed as { "$in": [ <expression>, <array> ] }
 * We evaluate the inner expression and then return a boolean
 * to indicate if the evaluated expression was found in the array.
 */
void
HandleDollarIn(pgbson *doc, const bson_value_t *operatorValue,
			   ExpressionResult *expressionResult)
{
	DualArgumentExpressionState state;
	memset(&state, 0, sizeof(DualArgumentExpressionState));

	ExpressionArgumentHandlingContext context =
	{
		.processElementFunc = ProcessDualArgumentElement,
		.processExpressionResultFunc = ProcessDollarIn,
		.state = &state,
	};

	int numberOfRequiredArgs = 2;
	HandleFixedArgumentExpression(doc, operatorValue, expressionResult,
								  numberOfRequiredArgs, "$in", &context);
}


/*
 * Evaluates the output of a $size expression.
 * $size is expressed as { "$size": [ <array> ] }
 * We evaluate the size of the array and return that as an int.
 */
void
HandleDollarSize(pgbson *doc, const bson_value_t *operatorValue,
				 ExpressionResult *expressionResult)
{
	ExpressionArgumentHandlingContext context =
	{
		.processElementFunc = ProcessDollarSizeElement,
		.processExpressionResultFunc = NULL,
		.state = NULL,
	};

	int numberOfRequiredArgs = 1;
	HandleFixedArgumentExpression(doc, operatorValue, expressionResult,
								  numberOfRequiredArgs, "$size", &context);
}


/*
 * Evaluates the output of a $slice expression.
 * $slice is expressed as { "$slice": [ <array>,numToSkip, numToReturn ] }
 * We slice input array using values numToSkip and numToReturn and returns new sliced array.
 */
void
HandleDollarSlice(pgbson *doc, const bson_value_t *operatorValue,
				  ExpressionResult *expressionResult)
{
	ThreeArgumentExpressionState state;
	memset(&state, 0, sizeof(ThreeArgumentExpressionState));

	ExpressionArgumentHandlingContext context =
	{
		.processElementFunc = ProcessThreeArgumentElement,
		.processExpressionResultFunc = ProcessDollarSlice,
		.state = &state,
	};

	int minRequiredArgs = 2;
	int maxRequiredArgs = 3;

	HandleRangedArgumentExpression(doc, operatorValue, expressionResult,
								   minRequiredArgs, maxRequiredArgs, "$slice", &context);
}


/*
 * Evaluates the output of a $arrayElemAt expression.
 * $arrayElemAt is expressed as { "$arrayElemAt": [ <array>, <idx> ] }
 * We evaluate the inner expressions, and return the element at the specified index.
 * If the index is a negative value, we return counting from the end of the array.
 * If the index exceeds the array bounds, it does not return a result.
 */
void
HandleDollarArrayElemAt(pgbson *doc, const bson_value_t *operatorValue,
						ExpressionResult *expressionResult)
{
	ArrayElemAtArgumentState state;
	memset(&state, 0, sizeof(ArrayElemAtArgumentState));

	state.isArrayElemAtOperator = true;
	state.opName = "$arrayElemAt";

	ExpressionArgumentHandlingContext context =
	{
		.processElementFunc = ProcessDualArgumentElement,
		.processExpressionResultFunc = ProcessDollarArrayElemAt,
		.state = &state.dualState,
	};

	int numberOfRequiredArgs = 2;
	HandleFixedArgumentExpression(doc, operatorValue, expressionResult,
								  numberOfRequiredArgs, state.opName, &context);
}


/*
 * Evaluates the output of a $first expression.
 * $first is expressed as { "$first": [ <expression> ] }
 * We evaluate the inner expression and return the first element of the array.
 * $first is an alias of {$arrayElemAt: [ <expression>, 0 ]}, so we just redirect to that operator.
 */
void
HandleDollarFirst(pgbson *doc, const bson_value_t *operatorValue,
				  ExpressionResult *expressionResult)
{
	ArrayElemAtArgumentState state;
	memset(&state, 0, sizeof(ArrayElemAtArgumentState));

	bson_value_t secondArg;
	secondArg.value_type = BSON_TYPE_INT32;
	secondArg.value.v_int32 = 0;

	state.dualState.secondArgument = secondArg;
	state.isArrayElemAtOperator = false;
	state.opName = "$first";

	ExpressionArgumentHandlingContext context =
	{
		.processElementFunc = ProcessDualArgumentElement,
		.processExpressionResultFunc = ProcessDollarArrayElemAt,
		.state = &state.dualState,
	};

	int numberOfRequiredArgs = 1;
	HandleFixedArgumentExpression(doc, operatorValue, expressionResult,
								  numberOfRequiredArgs, state.opName, &context);
}


/*
 * Evaluates the output of a $last expression.
 * $last is expressed as { "$last": [ <expression> ] }
 * We evaluate the inner expression and return the last element of the array.
 * $last is an alias of {$arrayElemAt: [ <expression>, -1 ]}, so we just redirect to that operator.
 */
void
HandleDollarLast(pgbson *doc, const bson_value_t *operatorValue,
				 ExpressionResult *expressionResult)
{
	ArrayElemAtArgumentState state;
	memset(&state, 0, sizeof(ArrayElemAtArgumentState));

	bson_value_t secondArg;
	secondArg.value_type = BSON_TYPE_INT32;
	secondArg.value.v_int32 = -1;

	state.dualState.secondArgument = secondArg;
	state.isArrayElemAtOperator = false;
	state.opName = "$last";

	ExpressionArgumentHandlingContext context =
	{
		.processElementFunc = ProcessDualArgumentElement,
		.processExpressionResultFunc = ProcessDollarArrayElemAt,
		.state = &state.dualState,
	};

	int numberOfRequiredArgs = 1;
	HandleFixedArgumentExpression(doc, operatorValue, expressionResult,
								  numberOfRequiredArgs, state.opName, &context);
}


/*
 * Evaluates the output of a $objectToArray expression.
 * $objectToArray is expressed as { "$objectToArray": [ <object-expression> ] } or
 * { "$objectToArray": <object-expression> }
 * We evaluate the inner expression and return an array with an object for each key-value pair
 * found in the object expression, $objectToArray does not recursively apply to embedded document fields.
 * i.e:
 *   input: {"a": 1, "b": { "c": 2 } }
 *   result: [{ "k": "a", "v": "1" }, { "k": "b", "v": { "c": 2 } }]
 */
void
HandleDollarObjectToArray(pgbson *doc, const bson_value_t *operatorValue,
						  ExpressionResult *expressionResult)
{
	ExpressionArgumentHandlingContext context =
	{
		.processElementFunc = ProcessDollarObjectToArrayElement,
		.processExpressionResultFunc = NULL,
		.state = expressionResult,
	};

	int numberOfRequiredArgs = 1;
	HandleFixedArgumentExpression(doc, operatorValue, expressionResult,
								  numberOfRequiredArgs, "$objectToArray", &context);
}


/*
 * Evaluates the output of a $arrayToObject expression.
 * $arrayToObject is expressed as { "$arrayToObject": [ [ {"k": "key", "v": <expression> }, ... ] ] } or
 * { "$arrayToObject": [ ["key", value], ... ]}
 * We evaluate the inner expression and return an an object constructed from the key value pairs found in the array.
 * i.e:
 *   input: [[{ "k": "a", "v": "1" }, { "k": "b", "v": { "c": 2 } }]] or [["a", "1"], ["b", {"c": 2}]]
 *   result: {"a": 1, "b": { "c": 2 } }
 */
void
HandleDollarArrayToObject(pgbson *doc, const bson_value_t *operatorValue,
						  ExpressionResult *expressionResult)
{
	ExpressionArgumentHandlingContext context =
	{
		.processElementFunc = ProcessDollarArrayToObjectElement,
		.processExpressionResultFunc = NULL,
		.state = expressionResult,
	};

	int numberOfRequiredArgs = 1;
	HandleFixedArgumentExpression(doc, operatorValue, expressionResult,
								  numberOfRequiredArgs, "$arrayToObject", &context);
}


/*
 * Evaluates the output of a $concatArrays expression.
 * $concatArrays is expressed as { "$concatArrays": [ <array1>, <array2>, .. ] }
 * We evaluate each array and write an array to the result with the concatenation of all arrays
 */
void
HandleDollarConcatArrays(pgbson *doc, const bson_value_t *operatorValue,
						 ExpressionResult *expressionResult)
{
	ConcatArraysState state;
	memset(&state, 0, sizeof(ConcatArraysState));

	PgbsonWriterInit(&state.writer);
	PgbsonWriterStartArray(&state.writer, "", 0, &state.arrayWriter);

	ExpressionArgumentHandlingContext context =
	{
		.processElementFunc = ProcessDollarConcatArraysElement,
		.processExpressionResultFunc = ProcessDollarConcatArraysResult,
		.state = &state,
	};

	bson_value_t startValue;
	startValue.value_type = BSON_TYPE_ARRAY;
	HandleVariableArgumentExpression(doc, operatorValue, expressionResult,
									 &startValue, &context);
}


/* Process the $in operator and returns true or false if the first argument is
 * found or not in the second argument which is the array to search. */
static void
ProcessDollarIn(bson_value_t *result, void *state)
{
	DualArgumentExpressionState *dollarInState =
		(DualArgumentExpressionState *) state;

	bson_value_t array = dollarInState->secondArgument;

	if (array.value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(MongoDollarInRequiresArray), errmsg(
							"$in requires an array as a second argument, found: %s",
							array.value_type == BSON_TYPE_EOD ?
							MISSING_TYPE_NAME :
							BsonTypeName(array.value_type)),
						errhint("$in requires an array as a second argument, found: %s",
								array.value_type == BSON_TYPE_EOD ?
								MISSING_TYPE_NAME :
								BsonTypeName(array.value_type))));
	}

	bool found = false;
	bson_value_t elementToFind = dollarInState->firstArgument;
	bson_iter_t arrayIterator;
	BsonValueInitIterator(&array, &arrayIterator);

	/* $in expression doesn't support matching by regex */
	while (bson_iter_next(&arrayIterator))
	{
		const bson_value_t *currentValue = bson_iter_value(&arrayIterator);

		if (elementToFind.value_type == BSON_TYPE_NULL &&
			currentValue->value_type == BSON_TYPE_NULL)
		{
			found = true;
			break;
		}

		bool isComparisonValid = false;
		int cmp = CompareBsonValueAndType(&elementToFind, currentValue,
										  &isComparisonValid);
		if (cmp == 0 && isComparisonValid)
		{
			found = true;
			break;
		}
	}

	result->value_type = BSON_TYPE_BOOL;
	result->value.v_bool = found;
}


/* Process the $slice operator and save the sliced array into result */
static void
ProcessDollarSlice(bson_value_t *result, void *state)
{
	ThreeArgumentExpressionState *context = (ThreeArgumentExpressionState *) state;
	bson_value_t *sourceArray = NULL;
	int numToSkip = 0;
	int numToReturn = INT32_MAX;

	if (context->hasNullOrUndefined)
	{
		result->value_type = BSON_TYPE_NULL;
		return;
	}

	/* fetch first argument from context */
	sourceArray = &(context->firstArgument);

	if (sourceArray->value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(MongoDollarSliceInvalidInput), errmsg(
							"First argument to $slice must be an array, but is of type: %s",
							BsonTypeName(sourceArray->value_type)),
						errhint(
							"First argument to $slice must be an array, but is of type: %s",
							BsonTypeName(sourceArray->value_type))));
	}

	/* fetch second argument from context */
	bson_value_t *currentElement = &(context->secondArgument);

	DollarSliceInputValidation(currentElement, true);

	int int32Val = BsonValueAsInt32(currentElement);

	if (context->totalProcessedArgs == 2 && int32Val >= 0)
	{
		numToReturn = int32Val;
	}
	else if (int32Val < 0)
	{
		int sourceArrayLength = BsonDocumentValueCountKeys(sourceArray);
		numToSkip = sourceArrayLength + int32Val;
	}
	else
	{
		numToSkip = int32Val;
	}

	/* fetch third argument from context */
	if (context->totalProcessedArgs == 3)
	{
		currentElement = &(context->thirdArgument);

		DollarSliceInputValidation(currentElement, false);

		int32Val = BsonValueAsInt32(currentElement);

		if (int32Val <= 0)
		{
			ereport(ERROR, (errcode(MongoDollarSliceInvalidSignThirdArg),
							errmsg(
								"Third argument to $slice must be positive: %s",
								BsonValueToJsonForLogging(currentElement)),
							errhint(
								"Third argument to $slice must be positive but found negative")));
		}
		numToReturn = BsonValueAsInt32(currentElement);
	}

	/* Traverse input array and create a new sliced array using numToSkip and numToReturn */
	bson_iter_t arrayIter;
	BsonValueInitIterator(sourceArray, &arrayIter);
	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	pgbson_array_writer arrayWriter;
	PgbsonWriterStartArray(&writer, "", 0, &arrayWriter);

	while (numToSkip > 0 && bson_iter_next(&arrayIter))
	{
		numToSkip--;
	}

	while (bson_iter_next(&arrayIter) && numToReturn > 0)
	{
		const bson_value_t *tmpVal = bson_iter_value(&arrayIter);
		PgbsonArrayWriterWriteValue(&arrayWriter, tmpVal);
		numToReturn--;
	}

	PgbsonWriterEndArray(&writer, &arrayWriter);
	*result = PgbsonArrayWriterGetValue(&arrayWriter);
}


/* Process the $arrayElemAt operator and returns the element in the array at the index provided */
static void
ProcessDollarArrayElemAt(bson_value_t *result, void *state)
{
	ArrayElemAtArgumentState *elemAtState =
		(ArrayElemAtArgumentState *) state;

	if (elemAtState->dualState.hasNullOrUndefined)
	{
		result->value_type = BSON_TYPE_NULL;
		return;
	}

	bson_value_t array = elemAtState->dualState.firstArgument;
	bson_value_t indexValue = elemAtState->dualState.secondArgument;

	if (array.value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(MongoArrayOperatorElemAtFirstArgMustBeArray), errmsg(
							elemAtState->isArrayElemAtOperator ?
							"%s's first argument must be an array, but is %s" :
							"%s's argument must be an array, but is %s",
							elemAtState->opName,
							BsonTypeName(array.value_type)),
						errhint(elemAtState->isArrayElemAtOperator ?
								"%s's first argument must be an array, but is %s" :
								"%s's argument must be an array, but is %s",
								elemAtState->opName,
								BsonTypeName(array.value_type))));
	}
	if (elemAtState->isArrayElemAtOperator && !BsonTypeIsNumber(indexValue.value_type))
	{
		bool isUndefined = IsExpressionResultUndefined(&indexValue);
		ereport(ERROR, (errcode(MongoDollarArrayElemAtSecondArgArgMustBeNumeric),
						errmsg(
							"$arrayElemAt's second argument must be a numeric value, but is %s",
							isUndefined ?
							MISSING_TYPE_NAME :
							BsonTypeName(indexValue.value_type)),
						errhint(
							"$arrayElemAt's second argument must be a numeric value, but is %s",
							isUndefined ?
							MISSING_TYPE_NAME :
							BsonTypeName(indexValue.value_type))));
	}

	bool checkFixedInteger = true;
	if (elemAtState->isArrayElemAtOperator &&
		!IsBsonValue32BitInteger(&indexValue, checkFixedInteger))
	{
		ereport(ERROR, (errcode(MongoDollarArrayElemAtSecondArgArgMustBe32Bit), errmsg(
							"$arrayElemAt's second argument must be representable as a 32-bit integer: %s",
							BsonValueToJsonForLogging(&indexValue)),
						errhint(
							"$arrayElemAt's second argument of type %s can't be representable as a 32-bit integer",
							BsonTypeName(indexValue.value_type))));
	}

	int32_t indexToFind = BsonValueAsInt32(&indexValue);
	bool found = false;

	/* If the provided index is negative, we need to treat the index as if it started from the end of the array */
	if (indexToFind < 0)
	{
		indexToFind++;
		bson_iter_t firstIter;
		BsonValueInitIterator(&array, &firstIter);

		bson_iter_t secondIter;
		BsonValueInitIterator(&array, &secondIter);
		while (bson_iter_next(&firstIter))
		{
			if (indexToFind == 0)
			{
				found = true;
				bson_iter_next(&secondIter);
			}
			else
			{
				indexToFind++;
			}
		}

		if (found)
		{
			*result = *bson_iter_value(&secondIter);
		}
	}
	else
	{
		int currentIndex = 0;
		bson_iter_t arrayIterator;
		BsonValueInitIterator(&array, &arrayIterator);
		while (bson_iter_next(&arrayIterator))
		{
			if (indexToFind == currentIndex)
			{
				found = true;
				*result = *bson_iter_value(&arrayIterator);
			}

			currentIndex++;
		}
	}

	if (!found)
	{
		/* The index was out of bounds, no result is returned. */
		result->value_type = BSON_TYPE_EOD;
	}
}


/* Function that checks if $isArray is true or false given an argument. */
static bool
ProcessDollarIsArrayElement(bson_value_t *result, const
							bson_value_t *currentElement,
							bool isFieldPathExpression, void *state)
{
	result->value_type = BSON_TYPE_BOOL;
	result->value.v_bool = currentElement->value_type == BSON_TYPE_ARRAY;
	return true;
}


/* Function that checks if the argument for $size is an array and returns the size of it. */
static bool
ProcessDollarSizeElement(bson_value_t *result, const
						 bson_value_t *currentElement,
						 bool isFieldPathExpression, void *state)
{
	if (currentElement->value_type != BSON_TYPE_ARRAY)
	{
		bool isUndefined = IsExpressionResultUndefined(currentElement);
		ereport(ERROR, (errcode(MongoDollarSizeRequiresArray), errmsg(
							"The argument to $size must be an array, but was of type: %s",
							isUndefined ?
							MISSING_TYPE_NAME :
							BsonTypeName(currentElement->value_type)),
						errhint(
							"The argument to $size must be an array, but was of type: %s",
							isUndefined ?
							MISSING_TYPE_NAME :
							BsonTypeName(currentElement->value_type))));
	}

	int size = 0;
	bson_iter_t arrayIterator;
	BsonValueInitIterator(currentElement, &arrayIterator);
	while (bson_iter_next(&arrayIterator))
	{
		size++;
	}

	result->value_type = BSON_TYPE_INT32;
	result->value.v_int32 = size;
	return true;
}


/* Function that checks if the passed argument for $arrayToObject is valid and builds the object from the array,
 * and writes it into the expression result.
 * If there is a duplicate path the last found path wins and it's value is preserved. */
static bool
ProcessDollarArrayToObjectElement(bson_value_t *result,
								  const bson_value_t *currentElement,
								  bool isFieldPathExpression, void *state)
{
	if (IsExpressionResultNullOrUndefined(currentElement))
	{
		result->value_type = BSON_TYPE_NULL;
		return true;
	}
	else if (currentElement->value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(MongoDollarArrayToObjectRequiresArray), errmsg(
							"$arrayToObject requires an array input, found: %s",
							BsonTypeName(currentElement->value_type)),
						errhint("$arrayToObject requires an array input, found: %s",
								BsonTypeName(currentElement->value_type))));
	}

	ExpressionResult *expressionResult = (ExpressionResult *) state;

	bson_iter_t arrayIter;
	BsonValueInitIterator(currentElement, &arrayIter);

	List *elementsToWrite = NIL;
	HTAB *hashTable = CreatePgbsonElementHashSet();

	if (bson_iter_next(&arrayIter))
	{
		if (!BSON_ITER_HOLDS_ARRAY(&arrayIter) &&
			!BSON_ITER_HOLDS_DOCUMENT(&arrayIter))
		{
			ereport(ERROR, (errcode(MongoDollarArrayToObjectBadInputTypeFormat), errmsg(
								"Unrecognised input type format for $arrayToObject: %s",
								BsonIterTypeName(&arrayIter)),
							errhint(
								"Unrecognised input type format for $arrayToObject: %s",
								BsonIterTypeName(&arrayIter))));
		}

		bool expectObjectElements = BSON_ITER_HOLDS_DOCUMENT(&arrayIter);
		do {
			pgbsonelement elementToWrite;
			const bson_value_t *arrayValue = bson_iter_value(&arrayIter);

			if (expectObjectElements)
			{
				elementToWrite = ParseElementFromObjectForArrayToObject(arrayValue);
			}
			else
			{
				elementToWrite = ParseElementFromArrayForArrayToObject(arrayValue);
			}

			if (strlen(elementToWrite.path) < elementToWrite.pathLength)
			{
				MongoErrorEreportCode errorCode = expectObjectElements ?
												  MongoLocation4940401 :
												  MongoLocation4940400;

				ereport(ERROR, (errcode(errorCode), errmsg(
									"Key field cannot contain an embedded null byte")));
			}

			PgbsonElementHashEntry searchEntry = {
				.element = elementToWrite
			};

			bool found = false;
			PgbsonElementHashEntry *hashEntry = hash_search(hashTable, &searchEntry,
															HASH_ENTER, &found);

			if (!found)
			{
				elementsToWrite = lappend(elementsToWrite, hashEntry);
			}

			hashEntry->element = elementToWrite;
		} while (bson_iter_next(&arrayIter));
	}

	pgbson_writer objectWriter;
	pgbson_element_writer *elementWriter =
		ExpressionResultGetElementWriter(expressionResult);

	PgbsonElementWriterStartDocument(elementWriter, &objectWriter);

	ListCell *elementToWriteCell = NULL;
	foreach(elementToWriteCell, elementsToWrite)
	{
		CHECK_FOR_INTERRUPTS();

		PgbsonElementHashEntry *hashEntry =
			(PgbsonElementHashEntry *) lfirst(elementToWriteCell);
		pgbsonelement element = hashEntry->element;

		PgbsonWriterAppendValue(&objectWriter, element.path, element.pathLength,
								&element.bsonValue);
	}

	PgbsonElementWriterEndDocument(elementWriter, &objectWriter);
	ExpressionResultSetValueFromWriter(expressionResult);

	hash_destroy(hashTable);
	list_free(elementsToWrite);
	return true;
}


static pgbsonelement
ParseElementFromObjectForArrayToObject(const bson_value_t *element)
{
	if (element->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(MongoDollarArrayToObjectAllMustBeObjects), errmsg(
							"$arrayToObject requires a consistent input format. Elements must all be arrays or all be objects. Object was detected, now found: %s",
							BsonTypeName(element->value_type)),
						errhint(
							"$arrayToObject requires a consistent input format. Elements must all be arrays or all be objects. Object was detected, now found: %s",
							BsonTypeName(element->value_type))));
	}

	int keyCount = BsonDocumentValueCountKeys(element);
	if (keyCount != 2)
	{
		ereport(ERROR, (errcode(MongoDollarArrayToObjectIncorrectNumberOfKeys), errmsg(
							"$arrayToObject requires an object keys of 'k' and 'v'. Found incorrect number of keys:%d",
							keyCount),
						errhint(
							"$arrayToObject requires an object keys of 'k' and 'v'. Found incorrect number of keys:%d",
							keyCount)));
	}

	pgbsonelement value = { 0 };

	bson_iter_t docIter;
	BsonValueInitIterator(element, &docIter);

	while (bson_iter_next(&docIter))
	{
		const char *currentKey = bson_iter_key(&docIter);
		if (strcmp(currentKey, "k") == 0)
		{
			const bson_value_t *resultKey = bson_iter_value(&docIter);
			if (resultKey->value_type != BSON_TYPE_UTF8)
			{
				ereport(ERROR, (errcode(MongoDollarArrayToObjectObjectKeyMustBeString),
								errmsg(
									"$arrayToObject requires an object with keys 'k' and 'v', where the value of 'k' must be of type string. Found type: %s",
									BsonTypeName(resultKey->value_type)),
								errhint(
									"$arrayToObject requires an object with keys 'k' and 'v', where the value of 'k' must be of type string. Found type: %s",
									BsonTypeName(resultKey->value_type))));
			}

			value.path = resultKey->value.v_utf8.str;
			value.pathLength = resultKey->value.v_utf8.len;
		}
		else if (strcmp(currentKey, "v") == 0)
		{
			value.bsonValue = *bson_iter_value(&docIter);
		}
		else
		{
			ereport(ERROR, (errcode(MongoDollarArrayToObjectRequiresObjectWithKAndV),
							errmsg(
								"$arrayToObject requires an object with keys 'k' and 'v'. Missing either or both keys from: %s",
								BsonValueToJsonForLogging(element)),
							errhint(
								"$arrayToObject requires an object with keys 'k' and 'v'. Missing either or both keys")));
		}
	}

	return value;
}


static pgbsonelement
ParseElementFromArrayForArrayToObject(const bson_value_t *element)
{
	if (element->value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(MongoDollarArrayToObjectAllMustBeArrays), errmsg(
							"$arrayToObject requires a consistent input format. Elements must all be arrays or all be objects. Array was detected, now found: %s",
							BsonTypeName(element->value_type)),
						errhint(
							"$arrayToObject requires a consistent input format. Elements must all be arrays or all be objects. Array was detected, now found: %s",
							BsonTypeName(element->value_type))));
	}

	int arrayLength = BsonDocumentValueCountKeys(element);
	if (arrayLength != 2)
	{
		ereport(ERROR, (errcode(MongoDollarArrayToObjectIncorrectArrayLength), errmsg(
							"$arrayToObject requires an array of size 2 arrays,found array of size: %d",
							arrayLength),
						errhint(
							"$arrayToObject requires an array of size 2 arrays,found array of size: %d",
							arrayLength)));
	}

	pgbsonelement value = { 0 };
	bson_iter_t arrayIter;
	BsonValueInitIterator(element, &arrayIter);

	bson_iter_next(&arrayIter);
	const bson_value_t *currentKey = bson_iter_value(&arrayIter);
	if (currentKey->value_type != BSON_TYPE_UTF8)
	{
		ereport(ERROR, (errcode(MongoDollarArrayToObjectArrayKeyMustBeString), errmsg(
							"$arrayToObject requires an array of key-value pairs, where the key must be of type string. Found key type: %s",
							BsonTypeName(currentKey->value_type)),
						errhint(
							"$arrayToObject requires an array of key-value pairs, where the key must be of type string. Found key type: %s",
							BsonTypeName(currentKey->value_type))));
	}

	value.path = currentKey->value.v_utf8.str;
	value.pathLength = currentKey->value.v_utf8.len;

	bson_iter_next(&arrayIter);
	value.bsonValue = *bson_iter_value(&arrayIter);

	return value;
}


/* Function that checks if the passed argument for $objectToArray is valid and builds the array from the object,
 * and writest it into the expression result. */
static bool
ProcessDollarObjectToArrayElement(bson_value_t *result,
								  const bson_value_t *currentElement,
								  bool isFieldPathExpression, void *state)
{
	if (IsExpressionResultNullOrUndefined(currentElement))
	{
		result->value_type = BSON_TYPE_NULL;
		return true;
	}
	else if (currentElement->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(MongoDollarObjectToArrayRequiresObject), errmsg(
							"$objectToArray requires a document input, found: %s",
							BsonTypeName(currentElement->value_type)),
						errhint("$objectToArray requires a document input, found: %s",
								BsonTypeName(currentElement->value_type))));
	}

	ExpressionResult *expressionResult = (ExpressionResult *) state;

	bson_iter_t documentIter;
	BsonValueInitIterator(currentElement, &documentIter);

	pgbson_array_writer childArrayWriter;
	pgbson_element_writer childArrayElementWriter;
	pgbson_element_writer *elementWriter =
		ExpressionResultGetElementWriter(expressionResult);

	PgbsonElementWriterStartArray(elementWriter, &childArrayWriter);
	PgbsonInitArrayElementWriter(&childArrayWriter, &childArrayElementWriter);

	while (bson_iter_next(&documentIter))
	{
		pgbson_writer childObjectWriter;
		PgbsonElementWriterStartDocument(&childArrayElementWriter, &childObjectWriter);

		PgbsonWriterAppendUtf8(&childObjectWriter, "k", 1,
							   bson_iter_key(&documentIter));
		PgbsonWriterAppendValue(&childObjectWriter, "v", 1,
								bson_iter_value(&documentIter));

		PgbsonElementWriterEndDocument(&childArrayElementWriter, &childObjectWriter);
	}

	PgbsonElementWriterEndArray(elementWriter, &childArrayWriter);
	ExpressionResultSetValueFromWriter(expressionResult);
	return true;
}


/* Function that processes an argument for $concatArrays, validates it is a valid input and adds it to the final result. */
static bool
ProcessDollarConcatArraysElement(bson_value_t *result,
								 const bson_value_t *currentElement,
								 bool isFieldPathExpression, void *state)
{
	if (IsExpressionResultNullOrUndefined(currentElement))
	{
		result->value_type = BSON_TYPE_NULL;
		return false; /* stop processing more arguments. */
	}

	if (currentElement->value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(MongoLocation28664), errmsg(
							"$concatArrays only supports arrays, not %s",
							BsonTypeName(currentElement->value_type)),
						errhint("$concatArrays only supports arrays, not %s",
								BsonTypeName(currentElement->value_type))));
	}

	ConcatArraysState *concatArraysState = state;
	bson_iter_t arrayIter;
	BsonValueInitIterator(currentElement, &arrayIter);
	while (bson_iter_next(&arrayIter))
	{
		PgbsonArrayWriterWriteValue(&concatArraysState->arrayWriter,
									bson_iter_value(&arrayIter));
	}

	return true;
}


/* Function that writes the final concat arrays result from the array writer. */
static void
ProcessDollarConcatArraysResult(bson_value_t *result, void *state)
{
	ConcatArraysState *concatArraysState = state;

	/* If we found a null or undefined argument, we should not write
	 * the result from the writer. */
	if (result->value_type == BSON_TYPE_NULL)
	{
		PgbsonWriterFree(&concatArraysState->writer);
		return;
	}

	PgbsonWriterEndArray(&concatArraysState->writer, &concatArraysState->arrayWriter);
	*result = PgbsonArrayWriterGetValue(&concatArraysState->arrayWriter);
}


/* *******************************************
 *  New aggregation operator's framework which uses pre parsed expression
 *  when building the projection tree.
 *  *******************************************
 */

/*
 * Evaluates the output of a $filter expression.
 * $filter is expressed as { "$filter": { input: <array-expression>, cond: <expression>, as: <string>, limit: <num-expression> } }
 * We evalute the condition with every element of the input array and filter elements when the expression evaluates to false.
 */
void
HandlePreParsedDollarFilter(pgbson *doc, void *arguments,
							ExpressionResult *expressionResult)
{
	DollarFilterArguments *filterArguments = arguments;

	bool isNullOnEmpty = false;

	ExpressionResult childExpression = ExpressionResultCreateChild(expressionResult);
	EvaluateAggregationExpressionData(&filterArguments->limit, doc, &childExpression,
									  isNullOnEmpty);

	bson_value_t evaluatedLimit = childExpression.value;
	int32_t limit;

	if (IsExpressionResultNullOrUndefined(&evaluatedLimit))
	{
		limit = INT32_MAX;
	}
	else
	{
		bool checkFixedInteger = true;
		if (!IsBsonValue32BitInteger(&evaluatedLimit, checkFixedInteger))
		{
			ereport(ERROR, (errcode(MongoLocation327391), errmsg(
								"$filter: limit must be represented as a 32-bit integral value: %s",
								BsonValueToJsonForLogging(&evaluatedLimit)),
							errhint(
								"$filter: limit of type %s can't be represented as a 32-bit integral value",
								BsonTypeName(evaluatedLimit.value_type))));
		}

		limit = BsonValueAsInt32(&evaluatedLimit);
		if (limit < 1)
		{
			ereport(ERROR, (errcode(MongoLocation327392), errmsg(
								"$filter: limit must be greater than 0: %d",
								limit)));
		}
	}

	ExpressionResultReset(&childExpression);
	EvaluateAggregationExpressionData(&filterArguments->input, doc, &childExpression,
									  isNullOnEmpty);

	bson_value_t evaluatedInputArg = childExpression.value;

	/* In native mongo if the input array is null or an undefined path the result is null. */
	if (IsExpressionResultNullOrUndefined(&evaluatedInputArg))
	{
		bson_value_t nullValue = {
			.value_type = BSON_TYPE_NULL
		};

		ExpressionResultSetValue(expressionResult, &nullValue);
		return;
	}

	if (evaluatedInputArg.value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(MongoLocation28651), errmsg(
							"input to $filter must be an array not %s", BsonTypeName(
								evaluatedInputArg.value_type)),
						errhint("input to $filter must be an array not %s",
								BsonTypeName(evaluatedInputArg.value_type))));
	}

	StringView aliasName = {
		.string = filterArguments->alias.value.value.v_utf8.str,
		.length = filterArguments->alias.value.value.v_utf8.len,
	};

	pgbson_element_writer *resultWriter = ExpressionResultGetElementWriter(
		expressionResult);
	pgbson_array_writer arrayWriter;
	PgbsonElementWriterStartArray(resultWriter, &arrayWriter);

	bson_iter_t arrayIter;
	BsonValueInitIterator(&evaluatedInputArg, &arrayIter);

	ExpressionResultReset(&childExpression);

	bson_value_t emptyValue = { 0 };
	ExpressionResultSetVariable(&childExpression, aliasName, &emptyValue);

	while (limit > 0 && bson_iter_next(&arrayIter))
	{
		const bson_value_t *currentElem = bson_iter_value(&arrayIter);

		ExpressionResultReset(&childExpression);
		ExpressionResultOverrideSingleVariableValue(&childExpression, currentElem);
		EvaluateAggregationExpressionData(&filterArguments->cond, doc, &childExpression,
										  isNullOnEmpty);

		if (BsonValueAsBool(&childExpression.value))
		{
			PgbsonArrayWriterWriteValue(&arrayWriter, currentElem);
			limit--;
		}
	}

	PgbsonElementWriterEndArray(resultWriter, &arrayWriter);
	ExpressionResultSetValueFromWriter(expressionResult);
}


/* Parses the $filter expression specified in the bson_value_t and stores it in the data argument.
 * $filter is expressed as { "$filter": { input: <array-expression>, cond: <expression>, as: <string>, limit: <num-expression> } }.
 */
void
ParseDollarFilter(const bson_value_t *argument, AggregationExpressionData *data)
{
	if (argument->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(MongoLocation28646), errmsg(
							"$filter only supports an object as its argument")));
	}

	data->operator.returnType = BSON_TYPE_ARRAY;

	bson_iter_t docIter;
	BsonValueInitIterator(argument, &docIter);

	bson_value_t input = { 0 };
	bson_value_t cond = { 0 };
	bson_value_t as = { 0 };
	bson_value_t limit = { 0 };
	while (bson_iter_next(&docIter))
	{
		const char *key = bson_iter_key(&docIter);
		if (strcmp(key, "input") == 0)
		{
			input = *bson_iter_value(&docIter);
		}
		else if (strcmp(key, "cond") == 0)
		{
			cond = *bson_iter_value(&docIter);
		}
		else if (strcmp(key, "as") == 0)
		{
			as = *bson_iter_value(&docIter);
		}
		else if (strcmp(key, "limit") == 0)
		{
			limit = *bson_iter_value(&docIter);
		}
		else
		{
			ereport(ERROR, (errcode(MongoLocation28647), errmsg(
								"Unrecognized parameter to $filter: %s", key),
							errhint(
								"Unrecognized parameter to $filter, unexpected key")));
		}
	}

	if (input.value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(MongoLocation28648), errmsg(
							"Missing 'input' parameter to $filter")));
	}

	if (cond.value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(MongoLocation28650), errmsg(
							"Missing 'cond' parameter to $filter")));
	}

	bson_value_t aliasValue = {
		.value_type = BSON_TYPE_UTF8,
		.value.v_utf8.len = 4,
		.value.v_utf8.str = "this"
	};

	if (as.value_type != BSON_TYPE_EOD)
	{
		if (as.value_type != BSON_TYPE_UTF8)
		{
			aliasValue.value.v_utf8.len = 0;
			aliasValue.value.v_utf8.str = "";
		}
		else
		{
			aliasValue = as;
		}

		StringView aliasNameView = {
			.length = aliasValue.value.v_utf8.len,
			.string = aliasValue.value.v_utf8.str,
		};

		ValidateVariableName(aliasNameView);
	}

	DollarFilterArguments *arguments = palloc0(sizeof(DollarFilterArguments));
	arguments->alias.value = aliasValue;

	if (limit.value_type == BSON_TYPE_EOD)
	{
		limit.value_type = BSON_TYPE_INT32;
		limit.value.v_int32 = INT32_MAX;
	}

	/* TODO: optimize, if input, limit and cond are constants, we can calculate the result at this phase. */
	ParseAggregationExpressionData(&arguments->input, &input);
	ParseAggregationExpressionData(&arguments->limit, &limit);
	ParseAggregationExpressionData(&arguments->cond, &cond);
	data->operator.arguments = arguments;
	data->operator.argumentsKind = AggregationExpressionArgumentsKind_Palloc;
}


/**
 * Parses the input document for FirstN and extracts the value for input and n.
 */
void
ParseDollarFirstN(const bson_value_t *inputDocument,
				  AggregationExpressionData *data)
{
	bson_value_t input = { 0 };
	bson_value_t elementsToFetch = { 0 };

	data->operator.returnType = BSON_TYPE_ARRAY;

	ParseInputDocumentForFirstAndLastN(inputDocument, &input,
									   &elementsToFetch, "$firstN");

	DollarFirstNLastNArguments *arguments = palloc0(sizeof(DollarFirstNLastNArguments));

	ParseAggregationExpressionData(&arguments->input, &input);
	ParseAggregationExpressionData(&arguments->elementsToFetch, &elementsToFetch);

	if (IsAggregationExpressionConstant(&arguments->input) &&
		IsAggregationExpressionConstant(&arguments->elementsToFetch))
	{
		/* Validating the n expression to throw error codes wrt native mongo in case of discrepancy. */
		ValidateElementForFirstAndLastN(&arguments->elementsToFetch.value,
										"$firstN");

		bson_value_t result = { 0 };
		bool isSkipElement = false;
		FillResultForDollarFirstAndLastN(&arguments->input.value,
										 &arguments->elementsToFetch.value,
										 isSkipElement, &result);
		data->value = result;
		data->kind = AggregationExpressionKind_Constant;
		pfree(arguments);
	}
	else
	{
		data->operator.arguments = arguments;
		data->operator.argumentsKind = AggregationExpressionArgumentsKind_Palloc;
	}
}


/**
 * This function computes the result for dollarFirstN function. and writes into the expression result.
 * @param arguments: This is struct which holds data for the DollarFirstN input args.
 */
void
HandlePreParsedDollarFirstN(pgbson *doc, void *arguments,
							ExpressionResult *expressionResult)
{
	DollarFirstNLastNArguments *dollarOpArgs = arguments;

	bool isNullOnEmpty = false;

	ExpressionResult childExpression = ExpressionResultCreateChild(expressionResult);
	EvaluateAggregationExpressionData(&dollarOpArgs->elementsToFetch, doc,
									  &childExpression,
									  isNullOnEmpty);
	bson_value_t evaluatedElementsToFetch = childExpression.value;

	/* Validating the n expression to throw error codes wrt native mongo in case of discrepancy. */
	ValidateElementForFirstAndLastN(&evaluatedElementsToFetch, "$firstN");

	ExpressionResultReset(&childExpression);
	EvaluateAggregationExpressionData(&dollarOpArgs->input, doc, &childExpression,
									  isNullOnEmpty);

	bson_value_t evaluatedInputArg = childExpression.value;

	/* Compute the final Result and write to expressionResult. */
	bson_value_t result = { 0 };
	bool isSkipElement = false;
	FillResultForDollarFirstAndLastN(&evaluatedInputArg, &evaluatedElementsToFetch,
									 isSkipElement, &result);
	ExpressionResultSetValue(expressionResult, &result);
}


/**
 * Parses the input document for LastN and extracts the value for input and n.
 */
void
ParseDollarLastN(const bson_value_t *inputDocument,
				 AggregationExpressionData *data)
{
	bson_value_t input = { 0 };
	bson_value_t elementsToFetch = { 0 };

	data->operator.returnType = BSON_TYPE_ARRAY;

	ParseInputDocumentForFirstAndLastN(inputDocument, &input,
									   &elementsToFetch, "$lastN");

	DollarFirstNLastNArguments *arguments = palloc0(sizeof(DollarFirstNLastNArguments));

	ParseAggregationExpressionData(&arguments->input, &input);
	ParseAggregationExpressionData(&arguments->elementsToFetch, &elementsToFetch);
	if (IsAggregationExpressionConstant(&arguments->input) &&
		IsAggregationExpressionConstant(&arguments->elementsToFetch))
	{
		/* Validating the n expression to throw error codes wrt native mongo in case of discrepancy. */
		ValidateElementForFirstAndLastN(&arguments->elementsToFetch.value,
										"$lastN");

		bson_value_t result = { 0 };
		bool isSkipElement = true;
		FillResultForDollarFirstAndLastN(&arguments->input.value,
										 &arguments->elementsToFetch.value,
										 isSkipElement, &result);
		data->value = result;
		data->kind = AggregationExpressionKind_Constant;
		pfree(arguments);
	}
	else
	{
		data->operator.arguments = arguments;
		data->operator.argumentsKind = AggregationExpressionArgumentsKind_Palloc;
	}
}


/**
 * This function computes the result for dollarLastN function. and writes into the expression result.
 * @param arguments: This is struct which holds data for the DollarLastN input args.
 */
void
HandlePreParsedDollarLastN(pgbson *doc, void *arguments,
						   ExpressionResult *expressionResult)
{
	DollarFirstNLastNArguments *dollarOpArgs = arguments;

	bool isNullOnEmpty = false;

	ExpressionResult childExpression = ExpressionResultCreateChild(expressionResult);
	EvaluateAggregationExpressionData(&dollarOpArgs->elementsToFetch, doc,
									  &childExpression,
									  isNullOnEmpty);
	bson_value_t evaluatedElementsToFetch = childExpression.value;

	/* Validating the n expression to throw error codes wrt native mongo in case of discrepancy. */
	ValidateElementForFirstAndLastN(&evaluatedElementsToFetch, "$lastN");

	ExpressionResultReset(&childExpression);
	EvaluateAggregationExpressionData(&dollarOpArgs->input, doc, &childExpression,
									  isNullOnEmpty);

	bson_value_t evaluatedInputArg = childExpression.value;

	/* Compute the final Result and write to expressionResult. */
	bson_value_t result = { 0 };
	bool isSkipElement = true;
	FillResultForDollarFirstAndLastN(&evaluatedInputArg, &evaluatedElementsToFetch,
									 isSkipElement, &result);
	ExpressionResultSetValue(expressionResult, &result);
}


/**
 * Parses the input document for FirstN and LastN array expression operator and extracts the value for input and n.
 * @param inputDocument: input document for the $firstN operator
 * @param input:  this is a pointer which after parsing will hold array expression
 * @param elementsToFetch: this is a pointer which after parsing will hold n i.e. how many elements to fetch for result
 * @param opName: this contains the name of the operator for error msg formatting purposes. This value is supposed to be $firstN/$lastN.
 */
static void
ParseInputDocumentForFirstAndLastN(const bson_value_t *inputDocument, bson_value_t *input,
								   bson_value_t *elementsToFetch, const char *opName)
{
	if (inputDocument->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(MongoLocation5787801), errmsg(
							"specification must be an object; found %s :%s",
							opName, BsonValueToJsonForLogging(inputDocument)),
						errhint(
							"specification must be an object; found opname:%s input type:%s",
							opName, BsonTypeName(inputDocument->value_type))));
	}

	bson_iter_t docIter;
	BsonValueInitIterator(inputDocument, &docIter);

	while (bson_iter_next(&docIter))
	{
		const char *key = bson_iter_key(&docIter);
		if (strcmp(key, "input") == 0)
		{
			*input = *bson_iter_value(&docIter);
		}
		else if (strcmp(key, "n") == 0)
		{
			*elementsToFetch = *bson_iter_value(&docIter);
		}
		else
		{
			ereport(ERROR, (errcode(MongoLocation5787901), errmsg(
								"%s found an unknown argument: %s", opName, key),
							errhint("%s found an unknown argument, while parsing request",
									opName)));
		}
	}

	/**
	 * Validation check to see if input and elements to fetch are present otherwise throw error.
	 */
	if (input->value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(MongoLocation5787907), errmsg(
							"%s requires an 'input' field", opName)));
	}

	if (elementsToFetch->value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(MongoLocation5787906), errmsg(
							"%s requires an 'n' field", opName)));
	}
}


/**
 * This function validates and throws error in case bson type is not a numeric > 0 and less than max value of int64 i.e. 9223372036854775807
 */
static void
ValidateElementForFirstAndLastN(bson_value_t *elementsToFetch, const
								char *opName)
{
	switch (elementsToFetch->value_type)
	{
		case BSON_TYPE_INT32:
		case BSON_TYPE_INT64:
		case BSON_TYPE_DOUBLE:
		case BSON_TYPE_DECIMAL128:
		{
			if (!IsBsonValueFixedInteger(elementsToFetch))
			{
				ereport(ERROR, (errcode(MongoLocation5787903), errmsg(
									"Value for 'n' must be of integral type, but found %s",
									BsonValueToJsonForLogging(elementsToFetch)),
								errhint(
									"Value for 'n' must be of integral type, but found of type %s",
									BsonTypeName(elementsToFetch->value_type))));
			}

			/* This is done as elements to fetch must only be int64. */
			bool throwIfFailed = true;
			elementsToFetch->value.v_int64 = BsonValueAsInt64WithRoundingMode(
				elementsToFetch, ConversionRoundingMode_Floor, throwIfFailed);
			elementsToFetch->value_type = BSON_TYPE_INT64;

			if (elementsToFetch->value.v_int64 <= 0)
			{
				ereport(ERROR, (errcode(MongoLocation5787908), errmsg(
									"'n' must be greater than 0, found %s",
									BsonValueToJsonForLogging(elementsToFetch)),
								errhint(
									"'n' must be greater than 0 found %ld for %s operator",
									elementsToFetch->value.v_int64, opName)));
			}
			break;
		}

		default:
		{
			ereport(ERROR, (errcode(MongoLocation5787902), errmsg(
								"Value for 'n' must be of integral type, but found %s",
								BsonValueToJsonForLogging(elementsToFetch)),
							errhint(
								"Value for 'n' must be of integral type, but found of type %s",
								BsonTypeName(elementsToFetch->value_type))));
		}
	}
}


/**
 * Writes the final result for $firstN and $lastN of bson type array.
 * It iterates the input array and based on elements to skip adds them to the result array. The elements to skip is 0 for $firstN and some int64 value for $lastN.
 */
static void
FillResultForDollarFirstAndLastN(bson_value_t *input,
								 bson_value_t *elementsToFetch,
								 bool isSkipElement,
								 bson_value_t *result)
{
	/**
	 * Input should be of type BSON_TYPE_ARRAY
	 */
	if (input->value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(MongoLocation5788200), errmsg(
							"Input must be an array")));
	}
	int64_t elements_to_skip = 0;

	/* This is required for $lastN to skip the first x elements and add rest of elements into the result array. */
	if (isSkipElement)
	{
		elements_to_skip = BsonDocumentValueCountKeys(input) -
						   elementsToFetch->value.v_int64;

		if (elements_to_skip < 0)
		{
			elements_to_skip = 0;
		}
	}

	bson_iter_t arrayIter;
	BsonValueInitIterator(input, &arrayIter);
	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	pgbson_array_writer arrayWriter;
	PgbsonWriterStartArray(&writer, "", 0, &arrayWriter);

	int64 elements_to_fetch_count = elementsToFetch->value.v_int64;

	while (elements_to_skip > 0 && bson_iter_next(&arrayIter))
	{
		elements_to_skip--;
	}


	while (bson_iter_next(&arrayIter) && elements_to_fetch_count > 0)
	{
		const bson_value_t *tmpVal = bson_iter_value(&arrayIter);
		PgbsonArrayWriterWriteValue(&arrayWriter, tmpVal);
		elements_to_fetch_count--;
	}

	PgbsonWriterEndArray(&writer, &arrayWriter);

	*result = PgbsonArrayWriterGetValue(&arrayWriter);
}


/*
 * Evaluates the output of an $range expression.
 * $range is expressed as { "$range": [ <expression1>, <expression2>, <expression3 optional> ] }
 * We evaluate the inner expressions and then return the final array.
 */
void
HandlePreParsedDollarRange(pgbson *doc, void *arguments,
						   ExpressionResult *expressionResult)
{
	List *argList = arguments;

	AggregationExpressionData *first = list_nth(argList, 0);
	AggregationExpressionData *second = list_nth(argList, 1);
	AggregationExpressionData *third = NULL;
	if (argList->length == 3)
	{
		third = list_nth(argList, 2);
	}

	bool isNullOnEmpty = false;
	ExpressionResult childResult = ExpressionResultCreateChild(expressionResult);
	EvaluateAggregationExpressionData(first, doc, &childResult, isNullOnEmpty);
	bson_value_t startRange = childResult.value;
	int32_t startValInt32 = GetStartValueForDollarRange(&startRange);

	ExpressionResultReset(&childResult);
	EvaluateAggregationExpressionData(second, doc, &childResult, isNullOnEmpty);
	bson_value_t endRange = childResult.value;
	int32_t endValInt32 = GetEndValueForDollarRange(&endRange);

	int32_t stepValInt32 = 1;

	/*third arg is optional. If this does not exist the step val should be 1. */
	if (third)
	{
		ExpressionResultReset(&childResult);
		EvaluateAggregationExpressionData(third, doc, &childResult, isNullOnEmpty);

		stepValInt32 = GetStepValueForDollarRange(&childResult.value);
	}

	bson_value_t result = { 0 };
	SetResultArrayForDollarRange(startValInt32, endValInt32, stepValInt32, &result);

	ExpressionResultSetValue(expressionResult, &result);
}


/*
 * Parses an $range expression and sets the parsed data in the data argument.
 * $range is expressed as { "$range": [ <expression1>, <expression2>, <expression3 optional> ] }
 */
void
ParseDollarRange(const bson_value_t *argument, AggregationExpressionData *data)
{
	int minRequiredArgs = 2;
	int maxRequiredArgs = 3;
	List *argList = ParseRangeArgumentsForExpression(argument,
													 minRequiredArgs,
													 maxRequiredArgs,
													 "$range",
													 &data->operator.
													 argumentsKind);

	AggregationExpressionData *first = list_nth(argList, 0);
	AggregationExpressionData *second = list_nth(argList, 1);
	AggregationExpressionData *third = NULL;

	if (argList->length == 3)
	{
		third = list_nth(argList, 2);
	}

	/* pre-processing the args when input is constant. */
	if (IsAggregationExpressionConstant(first) &&
		IsAggregationExpressionConstant(second) &&
		(!third || IsAggregationExpressionConstant(third)))
	{
		bson_value_t startRange = first->value;
		int32_t startValInt32 = GetStartValueForDollarRange(&startRange);

		bson_value_t endRange = second->value;
		int32_t endValInt32 = GetEndValueForDollarRange(&endRange);

		int32_t stepValInt32 = 1;

		/* Reassign stepVal from a default value when in input in operator. */
		if (third)
		{
			stepValInt32 = GetStepValueForDollarRange(&third->value);
		}

		SetResultArrayForDollarRange(startValInt32, endValInt32, stepValInt32,
									 &data->value);

		data->kind = AggregationExpressionKind_Constant;

		/* freeing the list args */
		list_free_deep(argList);
		return;
	}

	data->operator.arguments = argList;
	data->operator.returnType = BSON_TYPE_ARRAY;
}


/*
 * This function validates if value can be converted to int32 for start range. if not throws error otherwise returns an int32.
 */
static int32_t
GetStartValueForDollarRange(bson_value_t *startValue)
{
	if (startValue->value_type == BSON_TYPE_INT32)
	{
		return startValue->value.v_int32;
	}

	bool checkFixedInteger = true;
	if (!BsonTypeIsNumber(startValue->value_type))
	{
		ereport(ERROR, (errcode(MongoLocation34443), errmsg(
							"$range requires a numeric starting value, found value of type: %s",
							BsonTypeName(startValue->value_type)),
						errhint(
							"$range requires a numeric starting value, found value of type: %s",
							BsonTypeName(startValue->value_type))));
	}
	else if (!IsBsonValue32BitInteger(startValue, checkFixedInteger))
	{
		ereport(ERROR, (errcode(MongoLocation34444), errmsg(
							"$range requires a starting value that can be represented as a 32-bit integer, found value: %s",
							BsonValueToJsonForLogging(startValue))));
	}
	else
	{
		return BsonValueAsInt32(startValue);
	}
}


/*
 * This function validates if value can be converted to int32 for end range. if not throws error otherwise returns an int32.
 */
static int32_t
GetEndValueForDollarRange(bson_value_t *endValue)
{
	if (endValue->value_type == BSON_TYPE_INT32)
	{
		return endValue->value.v_int32;
	}

	bool checkFixedInteger = true;
	if (!BsonTypeIsNumber(endValue->value_type))
	{
		ereport(ERROR, (errcode(MongoLocation34445), errmsg(
							"$range requires a numeric ending value, found value of type: %s",
							BsonTypeName(endValue->value_type))));
	}
	else if (!IsBsonValue32BitInteger(endValue, checkFixedInteger))
	{
		ereport(ERROR, (errcode(MongoLocation34446), errmsg(
							"$range requires a ending value that can be represented as a 32-bit integer, found value: %s",
							BsonValueToJsonForLogging(endValue))));
	}
	else
	{
		return BsonValueAsInt32(endValue);
	}
}


/*
 * This function validates if value can be converted to int32 for step value. if not throws error otherwise returns an int32.
 */
static int32_t
GetStepValueForDollarRange(bson_value_t *stepValue)
{
	bool checkFixedInteger = true;
	int32_t stepValInt32;
	if (!BsonTypeIsNumber(stepValue->value_type))
	{
		ereport(ERROR, (errcode(MongoLocation34447), errmsg(
							"$range requires a numeric step value, found value of type: %s",
							BsonTypeName(stepValue->value_type)),
						errhint(
							"$range requires a numeric step value, found value of type: %s",
							BsonTypeName(stepValue->value_type))));
	}
	else if (!IsBsonValue32BitInteger(stepValue, checkFixedInteger))
	{
		ereport(ERROR, (errcode(MongoLocation34448), errmsg(
							"$range requires a step value that can be represented as a 32-bit integer, found value: %s",
							BsonValueToJsonForLogging(stepValue))));
	}
	else
	{
		stepValInt32 = BsonValueAsInt32(stepValue);
	}

	/* step value cannot be zero as it will generate infinite numbers. */
	if (stepValInt32 == 0)
	{
		ereport(ERROR, (errcode(MongoLocation34449), errmsg(
							"$range requires a non-zero step value")));
	}

	return stepValInt32;
}


/*
 * Gives the final result array for dollar range from start to endValue (excluding the endValue).
 */
static void
SetResultArrayForDollarRange(int32_t startValue, int32_t endValue, int32_t stepValue,
							 bson_value_t *result)
{
	/* This step validates before writing array that size of array should be less than 100MB and 64MB during writing. */
	ValidateArraySizeLimit(startValue, endValue, stepValue);

	/* start iterating and writing the result and stop when start >= end */
	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	pgbson_array_writer arrayWriter;
	PgbsonWriterStartArray(&writer, "", 0, &arrayWriter);


	bool isSeriesAsc = startValue < endValue;

	/*
	 * series should move towards end range. Otherwise it should give empty
	 * eg $range : [100, 2, 1] or $range: [2, 100, -1]
	 */
	if ((isSeriesAsc && stepValue < 0) || (!isSeriesAsc && stepValue > 0))
	{
		PgbsonWriterEndArray(&writer, &arrayWriter);
		*result = PgbsonArrayWriterGetValue(&arrayWriter);
		return;
	}

	int64_t elementValue = startValue;
	bson_value_t elementBsonValue = { .value_type = BSON_TYPE_INT32 };

	/* start iterating towards the end range and write to array. */
	while ((isSeriesAsc && elementValue < endValue) ||
		   (!isSeriesAsc && elementValue > endValue))
	{
		elementBsonValue.value.v_int32 = elementValue;
		PgbsonArrayWriterWriteValue(&arrayWriter, &elementBsonValue);
		elementValue += stepValue;
	}

	PgbsonWriterEndArray(&writer, &arrayWriter);

	*result = PgbsonArrayWriterGetValue(&arrayWriter);
}


/*
 * This function ensures the size of array should not exceed the limits for native mongo.
 * Currently, native mongo has checks the array size should not go beyond 100MB and 64MB when writing the array. 64MB is an optimization before writing.
 * All calculation in this function is done to compute the bytes used if the array were to be written.
 * This function computes size exactly replicating libbson bson_append_value functions used in  PgbsonArrayWriterWriteValue.
 * Size calculation logic is based on (size of empty array +  size of keys in array + size of values in array).
 */
static void
ValidateArraySizeLimit(int32_t startValue, int32_t endValue, int32_t stepValue)
{
	int64_t numberOfElements = ((endValue - startValue - 1) / stepValue) + 1;

	int64_t totalSizeForValuesOfArray = numberOfElements * sizeof(int32_t); /* int32 uses 4 bytes. */

	/*
	 * Approach to Calculate Total Size of Keys in a Given Range:
	 *
	 * 1. Initialize totalBytes = 0.
	 * 2. Start with base value 9; calculate numKeysBase as min(given_elements, 9). This is because we are trying to bucketize the keys range ie.e 0-9, 10-99, 100-999 ...
	 * 3. Add numKeysBase * 3 to totalBytes; subtract numKeysBase from given_elements.
	 * 4. Repeat steps 2-3 for ranges (e.g., 99, 999, 9999) until given_elements is exhausted.
	 *
	 * This approach leverages known key range sizes (3 byte for 0-9, 4 bytes for 10-99, etc.)
	 * and optimizes computation by processing elements stepwise.
	 */

	int64_t iterCount = numberOfElements;
	int64_t bucketEndRange = 9; /* this represents the bucket end range. It will go like 9, 99, 999, 9999, etc. */
	int64_t sizeOfKeyForBucket = 3; /* size for keys for range 0-9 is 3 bytes. This will grow as we move from 1 bucket to other. */
	int64_t bucketMulValue = 9; /* this signifies the number of elements for a given bucketRange. It will go like 9, 90, 900, 9000, etc*/
	int64_t totalSizeOfKeys = 3; /* 1*3 this is done as 0-9 bucket has 10 elements but code below does not factor in 10th element for code simplicity. */
	while (iterCount > bucketEndRange)
	{
		totalSizeOfKeys += bucketMulValue * sizeOfKeyForBucket;
		bucketEndRange = (bucketEndRange * 10) + 9;
		sizeOfKeyForBucket++;
		bucketMulValue *= 10;
	}
	bucketEndRange = (bucketEndRange - 9) / 10 + 1;
	totalSizeOfKeys += (iterCount - bucketEndRange) * sizeOfKeyForBucket;

	int64_t totalSizeOfArray = EMPTY_BSON_ARRAY_SIZE_BYTES + totalSizeForValuesOfArray +
							   totalSizeOfKeys + SIZE_OF_PARENT_OF_ARRAY_FOR_BSON;
	if (totalSizeOfArray > BSON_MAX_ALLOWED_SIZE_INTERMEDIATE)
	{
		ereport(ERROR, (errcode(MongoExceededMemoryLimit), errmsg(
							"$range would use too much memory (%ld bytes) and cannot spill to disk. Memory limit: 104857600 bytes",
							totalSizeOfArray),
						errhint(
							"$range would use too much memory (%ld bytes) and cannot spill to disk. Memory limit: 104857600 bytes",
							totalSizeOfArray)));
	}

	if (totalSizeOfArray > MAX_BUFFER_SIZE_DOLLAR_RANGE)
	{
		ereport(ERROR, (errcode(MongoLocation13548), errmsg(
							"$range: the size of buffer to store output exceeded the 64MB limit")));
	}
}


/*
 *	This function is handler for the $reverseArray. The input to the function is {$reverseArray : {array expression}}.
 *	This evaluates the expression value and then reverses the array.
 */
void
HandlePreParsedDollarReverseArray(pgbson *doc, void *state,
								  ExpressionResult *expressionResult)
{
	bool isNullOnEmpty = false;
	ExpressionResult childResult = ExpressionResultCreateChild(expressionResult);
	EvaluateAggregationExpressionData(
		(AggregationExpressionData *) state, doc,
		&childResult,
		isNullOnEmpty);


	if (IsExpressionResultNullOrUndefined(&childResult.value))
	{
		bson_value_t result = { 0 };
		result.value_type = BSON_TYPE_NULL;
		ExpressionResultSetValue(expressionResult, &result);
		return;
	}

	if (childResult.value.value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(MongoLocation34435), errmsg(
							"The argument to $reverseArray must be an array, but was of type: %s",
							BsonTypeName(childResult.value.value_type)),
						errhint(
							"The argument to $reverseArray must be an array, but was of type: %s",
							BsonTypeName(childResult.value.value_type))));
	}

	bson_value_t result = { 0 };
	SetResultArrayForDollarReverse(&childResult.value, &result);
	ExpressionResultSetValue(expressionResult, &result);
}


/*
 * Parses the input for $reverseArray. Syntax : {$reverseArray: <array expression>}.
 * The expression should resolve to an array.
 * $reverseArray function takes in the desired input array and gives the output array which is reversed.
 */
void
ParseDollarReverseArray(const bson_value_t *argument, AggregationExpressionData *data)
{
	data->operator.arguments = ParseFixedArgumentsForExpression(argument, 1,
																"$reverseArray",
																&data->operator.
																argumentsKind);
	data->operator.returnType = BSON_TYPE_ARRAY;
}


/*
 * This function iterates over the given bson type array and gives the result by reversing the array.
 */
static void
SetResultArrayForDollarReverse(bson_value_t *array, bson_value_t *result)
{
	bson_iter_t arrayIterator;
	BsonValueInitIterator(array, &arrayIterator);

	pgbson_writer writer;
	PgbsonWriterInit(&writer);

	pgbson_array_writer arrayWriter;
	PgbsonWriterStartArray(&writer, "", 0, &arrayWriter);

	int keysCount = BsonDocumentValueCountKeys(array);

	if (keysCount == 0)
	{
		PgbsonWriterEndArray(&writer, &arrayWriter);
		*result = PgbsonArrayWriterGetValue(&arrayWriter);
		return;
	}

	/* allocating memory in 1 go for the given array elements. */
	bson_value_t *valueCopy = (bson_value_t *) palloc(sizeof(bson_value_t) * keysCount);

	/* Iterating the bson array from front and maintaining in valueCopy in reversed order.*/
	int index = keysCount - 1;
	while (bson_iter_next(&arrayIterator))
	{
		valueCopy[index--] = *((bson_value_t *) bson_iter_value(&arrayIterator));
	}

	/* Iterating the valueCopy array from front to write to a bson array */
	index = 0;
	while (keysCount > index)
	{
		PgbsonArrayWriterWriteValue(&arrayWriter, &valueCopy[index++]);
	}

	pfree(valueCopy);

	PgbsonWriterEndArray(&writer, &arrayWriter);

	*result = PgbsonArrayWriterGetValue(&arrayWriter);
}


/*
 * This function handles the result and processing after the input has been parsed for $indexOfArray.
 * This function scans the given array in the input to find the specified element in the array given start and end positions.
 * The start and end positions can be optional if not provided we need to return the first index where the element occurs.
 */
void
HandlePreParsedDollarIndexOfArray(pgbson *doc, void *arguments,
								  ExpressionResult *expressionResult)
{
	List *argsList = (List *) arguments;

	/* evaluating the array argument expression */
	AggregationExpressionData *arrExpressionData = list_nth(argsList, 0);
	bool isNullOnEmpty = false;
	ExpressionResult childResult = ExpressionResultCreateChild(expressionResult);
	EvaluateAggregationExpressionData(arrExpressionData, doc, &childResult,
									  isNullOnEmpty);

	if (IsExpressionResultNullOrUndefined(&childResult.value))
	{
		bson_value_t result = { 0 };
		result.value_type = BSON_TYPE_NULL;
		ExpressionResultSetValue(expressionResult, &result);
		return;
	}
	else if (childResult.value.value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(MongoLocation40090), errmsg(
							"$indexOfArray requires an array as a first argument, found: %s",
							BsonTypeName(arrExpressionData->value.value_type)),
						errhint(
							"$indexOfArray requires an array as a first argument, found: %s",
							BsonTypeName(arrExpressionData->value.value_type)
							)));
	}

	bson_value_t arrayExpression = childResult.value;

	/* evaluating the to be searched argument expression. */
	AggregationExpressionData *searchExpressionData = list_nth(argsList, 1);
	ExpressionResultReset(&childResult);
	EvaluateAggregationExpressionData(searchExpressionData, doc, &childResult,
									  isNullOnEmpty);
	bson_value_t element = childResult.value;

	/* start and end are optional hence need to add a safe check*/
	AggregationExpressionData *startIndexExpressionData = argsList->length > 2 ? list_nth(
		argsList, 2) :
														  NULL;
	AggregationExpressionData *endIndexExpressionData = argsList->length > 3 ? list_nth(
		argsList, 3) :
														NULL;

	int32 startIndex = 0;
	int32 endIndex = INT32_MAX;

	bool isStartIndex = true;

	if (startIndexExpressionData)
	{
		ExpressionResultReset(&childResult);
		EvaluateAggregationExpressionData(startIndexExpressionData, doc, &childResult,
										  isNullOnEmpty);
		startIndex = GetIndexValueFromDollarIdxInput(&childResult.value, isStartIndex);
	}

	if (endIndexExpressionData)
	{
		ExpressionResultReset(&childResult);
		EvaluateAggregationExpressionData(endIndexExpressionData, doc, &childResult,
										  isNullOnEmpty);
		endIndex = GetIndexValueFromDollarIdxInput(&childResult.value, !isStartIndex);
	}

	bson_value_t result = { .value_type = BSON_TYPE_INT32 };
	result.value.v_int32 = FindIndexInArrayForElement(&arrayExpression, &element,
													  startIndex,
													  endIndex);
	ExpressionResultSetValue(expressionResult, &result);
}


/*
 * This function parses the input for dollarIndexOfArray.
 * The input to this function is of the following format { $indexOfArray: [ <array expression>, <search expression>, <start>, <end> ] }.
 * Start and end can be expressions which are optional
 */
void
ParseDollarIndexOfArray(const bson_value_t *argument, AggregationExpressionData *data)
{
	int minRequiredArgs = 2;
	int maxRequiredArgs = 4;
	List *argsList = ParseRangeArgumentsForExpression(argument, minRequiredArgs,
													  maxRequiredArgs, "$indexOfArray",
													  &data->operator.argumentsKind);

	/*This function checks if all elements in list are constant for optimization*/
	if (AreElementsInListConstant(argsList))
	{
		AggregationExpressionData *arrExpressionData = list_nth(argsList, 0);
		AggregationExpressionData *searchExpressionData = list_nth(argsList, 1);

		/* startIndex and endIndex are optional hence need to add a safe check*/
		AggregationExpressionData *startIndexExpressionData = argsList->length > 2 ?
															  list_nth(argsList, 2) :
															  NULL;
		AggregationExpressionData *endIndexExpressionData = argsList->length > 3 ?
															list_nth(argsList, 3) :
															NULL;

		if (IsExpressionResultNullOrUndefined(&arrExpressionData->value))
		{
			bson_value_t result = { 0 };
			result.value_type = BSON_TYPE_NULL;
			data->value = result;
			data->kind = AggregationExpressionKind_Constant;

			/* free the list */
			list_free_deep(argsList);
			return;
		}
		else if (arrExpressionData->value.value_type != BSON_TYPE_ARRAY)
		{
			ereport(ERROR, (errcode(MongoLocation40090), errmsg(
								"$indexOfArray requires an array as a first argument, found: %s",
								BsonTypeName(arrExpressionData->value.value_type)),
							errhint(
								"$indexOfArray requires an array as a first argument, found: %s",
								BsonTypeName(arrExpressionData->value.value_type))));
		}

		int32 startIndex = 0;
		int32 endIndex = INT32_MAX;
		bool isStartIndex = true;
		if (startIndexExpressionData)
		{
			startIndex = GetIndexValueFromDollarIdxInput(&startIndexExpressionData->value,
														 isStartIndex);
		}

		if (endIndexExpressionData)
		{
			endIndex = GetIndexValueFromDollarIdxInput(&endIndexExpressionData->value,
													   !isStartIndex);
		}

		bson_value_t result = { .value_type = BSON_TYPE_INT32 };
		result.value.v_int32 = FindIndexInArrayForElement(&arrExpressionData->value,
														  &searchExpressionData->value,
														  startIndex,
														  endIndex);

		data->value = result;
		data->kind = AggregationExpressionKind_Constant;

		/* free the list */
		list_free_deep(argsList);

		return;
	}
	data->operator.arguments = argsList;
}


/*
 * This function validates the given input start and endIndexes with the respective correct bson types.
 * The values cannot be negatives and should resolve to always integral expression.
 * This function validates for both start and end indexes based on the input bool flag it sets the default values and formats the error messages.
 */
static int32
GetIndexValueFromDollarIdxInput(bson_value_t *arg, bool isStartIndex)
{
	const char *endingIndexString = "ending";
	const char *startingIndexString = "starting";
	if (!BsonTypeIsNumber(arg->value_type) || !IsBsonValueFixedInteger(arg))
	{
		ereport(ERROR, (errcode(MongoLocation40096), errmsg(
							"$indexOfArray requires an integral %s index, found a value of type: %s, with value: %s",
							isStartIndex ? startingIndexString : endingIndexString,
							BsonTypeName(arg->value_type),
							BsonValueToJsonForLogging(arg)),
						errhint(
							"$indexOfArray requires an integral %s index, found a value of type: %s",
							isStartIndex ? startingIndexString : endingIndexString,
							BsonTypeName(arg->value_type)
							)));
	}

	int64 result = BsonValueAsInt64(arg);

	if (result > INT32_MAX)
	{
		ereport(ERROR, (errcode(MongoLocation40096), errmsg(
							"$indexOfArray requires an integral %s index, found a value of type: %s, with value: %s",
							isStartIndex ? startingIndexString : endingIndexString,
							BsonTypeName(arg->value_type),
							BsonValueToJsonForLogging(arg)),
						errhint(
							"$indexOfArray requires an integral %s index, found a value of type: %s",
							isStartIndex ? startingIndexString : endingIndexString,
							BsonTypeName(arg->value_type)
							)));
	}
	else if (result < 0)
	{
		ereport(ERROR, (errcode(MongoLocation40097), errmsg(
							"$indexOfArray requires a nonnegative %s index, found: %s",
							isStartIndex ? startingIndexString : endingIndexString,
							BsonValueToJsonForLogging(arg)),
						errhint(
							"$indexOfArray requires a nonnegative %s indexes",
							isStartIndex ? startingIndexString : endingIndexString
							)));
	}
	return (int32) result;
}


/*
 * This function iterates over the array. It compares if the current index is within the specified limits
 * Then, the compares the bson value at that index with the elment to be searched for and returns the index at which the element is found first.
 * This function returns -1 if startIndex > endIndex or start value is greater than the size of the array.
 */
static int32
FindIndexInArrayForElement(bson_value_t *array, bson_value_t *element, int32 startIndex,
						   int32 endIndex)
{
	if (startIndex >= endIndex)
	{
		return -1;
	}

	int32 currentIndex = 0;
	bson_iter_t arrayIterator;
	BsonValueInitIterator(array, &arrayIterator);

	/* iterating till the startIndex . */
	while (currentIndex < startIndex && bson_iter_next(&arrayIterator))
	{
		currentIndex++;
	}

	while (bson_iter_next(&arrayIterator) && currentIndex < endIndex)
	{
		if (BsonValueEquals(bson_iter_value(&arrayIterator), element))
		{
			return currentIndex;
		}
		currentIndex++;
	}

	return -1;
}


/*
 * This function handles the pre-parsed dollarMax input and results the maximum element in the given argument.
 */
void
HandlePreParsedDollarMax(pgbson *doc, void *arguments,
						 ExpressionResult *expressionResult)
{
	bool isNullOnEmpty = false;
	ExpressionResult childResult = ExpressionResultCreateChild(expressionResult);
	EvaluateAggregationExpressionData(
		(AggregationExpressionData *) arguments, doc,
		&childResult,
		isNullOnEmpty);

	bson_value_t result = { 0 };
	bool isFindMax = true;
	SetResultValueForDollarMaxMin(&childResult.value, &result, isFindMax);
	ExpressionResultSetValue(expressionResult, &result);
}


/*
 * This function parses the input for operator $max.
 * The input is specified of type {$max: <expression>} where expression can be any expression.
 * In case the expression is an array it gives the maximum element in array otherwise returns the resolved expression as it is.
 */
void
ParseDollarMax(const bson_value_t *argument, AggregationExpressionData *data)
{
	bool isMax = true;
	HandlerForParsingMinMax(isMax, argument, data);
}


/*
 * This function handles the pre-parsed dollarMax input and results the maximum element in the given argument.
 */
void
HandlePreParsedDollarMin(pgbson *doc, void *arguments,
						 ExpressionResult *expressionResult)
{
	bool isNullOnEmpty = false;
	ExpressionResult childResult = ExpressionResultCreateChild(expressionResult);
	EvaluateAggregationExpressionData(
		(AggregationExpressionData *) arguments, doc,
		&childResult,
		isNullOnEmpty);

	bson_value_t result = { 0 };
	bool isFindMax = false;
	SetResultValueForDollarMaxMin(&childResult.value, &result, isFindMax);
	ExpressionResultSetValue(expressionResult, &result);
}


/*
 * This function parses the input for operator $min.
 * The input is specified of type {$min: <expression>} where expression can be any expression.
 * In case the expression is an array it gives the minimum element in array otherwise returns the resolved expression as it is.
 */
void
ParseDollarMin(const bson_value_t *argument, AggregationExpressionData *data)
{
	bool isMax = false;
	HandlerForParsingMinMax(isMax, argument, data);
}


/*
 * This function parses the input for operator $sum.
 * The input is specified of type {$sum: <expression>} where expression can be any expression.
 * In case the expression is an array it gives the sum of elements in array.
 */
void
ParseDollarSum(const bson_value_t *argument, AggregationExpressionData *data)
{
	bool isSum = true;
	HandlerForParsingSumAvg(isSum, argument, data);
}


/*
 * This function parses the input for operator $avg.
 * The input is specified of type {$sum: <expression>} where expression can be any expression.
 * In case the expression is an array it gives the sum of elements in array.
 */
void
ParseDollarAvg(const bson_value_t *argument, AggregationExpressionData *data)
{
	bool isSum = false;
	HandlerForParsingSumAvg(isSum, argument, data);
}


/*
 * This function handles the pre-parsed $sum input and results the maximum element in the given argument.
 */
void
HandlePreParsedDollarSum(pgbson *doc, void *arguments,
						 ExpressionResult *expressionResult)
{
	bool isNullOnEmpty = false;
	ExpressionResult childResult = ExpressionResultCreateChild(expressionResult);
	EvaluateAggregationExpressionData(
		(AggregationExpressionData *) arguments, doc,
		&childResult,
		isNullOnEmpty);

	bson_value_t result = { 0 };
	bool isSum = true;
	SetResultValueForDollarSumAvg(&childResult.value, &result, isSum);
	ExpressionResultSetValue(expressionResult, &result);
}


/*
 * This function handles the pre-parsed $avg input and results the maximum element in the given argument.
 */
void
HandlePreParsedDollarAvg(pgbson *doc, void *arguments,
						 ExpressionResult *expressionResult)
{
	bool isNullOnEmpty = false;
	ExpressionResult childResult = ExpressionResultCreateChild(expressionResult);
	EvaluateAggregationExpressionData(
		(AggregationExpressionData *) arguments, doc,
		&childResult,
		isNullOnEmpty);

	bson_value_t result = { 0 };
	bool isSum = false;
	SetResultValueForDollarSumAvg(&childResult.value, &result, isSum);
	ExpressionResultSetValue(expressionResult, &result);
}


/*
 * This function is a common function to parse min and max operator.
 * This takes in bool isMax as extra argument to confirm whether to process for min or max operator.
 */
static void
HandlerForParsingMinMax(bool isMax, const bson_value_t *argument,
						AggregationExpressionData *data)
{
	char *opName = isMax ? "$max" : "$min";
	AggregationExpressionData *argumentData = palloc0(
		sizeof(AggregationExpressionData));

	/*
	 * When operator expects a single argument and input is an array of single element,
	 * evaluate the element as argument (not as a list) to match the scenario when input is not an array.
	 */
	if (argument->value_type == BSON_TYPE_ARRAY &&
		BsonDocumentValueCountKeys(argument) == 1)
	{
		argumentData = ParseFixedArgumentsForExpression(argument,
														1,
														opName,
														&argumentData->operator.
														argumentsKind);
	}
	else
	{
		ParseAggregationExpressionData(argumentData, argument);
	}

	if (IsAggregationExpressionConstant(argumentData))
	{
		SetResultValueForDollarMaxMin(&argumentData->value, &data->value, isMax);
		data->kind = AggregationExpressionKind_Constant;
		pfree(argumentData);
		return;
	}

	data->operator.arguments = argumentData;
	data->operator.argumentsKind = AggregationExpressionArgumentsKind_Palloc;
}


/*
 * This function is a common function to parse sum and avg operator.
 * This takes in bool isSum as extra argument to confirm whether to process for sum or avg operator.
 */
static void
HandlerForParsingSumAvg(bool isSum, const bson_value_t *argument,
						AggregationExpressionData *data)
{
	char *opName = isSum ? "$sum" : "$avg";
	AggregationExpressionData *argumentData = palloc0(
		sizeof(AggregationExpressionData));

	/*
	 * When operator expects a single argument and input is an array of single element,
	 * evaluate the element as argument (not as a list) to match the scenario when input is not an array.
	 */
	if (argument->value_type == BSON_TYPE_ARRAY &&
		BsonDocumentValueCountKeys(argument) == 1)
	{
		argumentData = ParseFixedArgumentsForExpression(argument,
														1,
														opName,
														&argumentData->operator.
														argumentsKind);
	}
	else
	{
		ParseAggregationExpressionData(argumentData, argument);
	}

	if (IsAggregationExpressionConstant(argumentData))
	{
		SetResultValueForDollarSumAvg(&argumentData->value, &data->value, isSum);
		data->kind = AggregationExpressionKind_Constant;
		pfree(argumentData);
		return;
	}

	data->operator.arguments = argumentData;
	data->operator.argumentsKind = AggregationExpressionArgumentsKind_Palloc;
}


/*
 * This function takes care of taking in the inputArgument and computing the final max/min element and storing in result.
 * This function expects the result argument should be passed with value_type BSON_TYPE_NULL.
 */
static void
SetResultValueForDollarMaxMin(const bson_value_t *inputArgument, bson_value_t *result,
							  bool isFindMax)
{
	result->value_type = BSON_TYPE_NULL;

	/* In case the element is null return as default result value is null. */
	if (IsExpressionResultNullOrUndefined(inputArgument))
	{
		return;
	}

	/* For any other case set the inputArgument as it is */
	if (inputArgument->value_type != BSON_TYPE_ARRAY)
	{
		result->value_type = inputArgument->value_type;
		result->value = inputArgument->value;
		return;
	}

	bson_iter_t arrayIterator;
	BsonValueInitIterator(inputArgument, &arrayIterator);
	bson_value_t *arrayElem;
	bool isResultInitialized = false;

	while (bson_iter_next(&arrayIterator))
	{
		arrayElem = (bson_value_t *) bson_iter_value(&arrayIterator);

		/* As per the the expected behaviour operator only considers the non-null and the non-missing values for the comparison. */
		if (IsExpressionResultNullOrUndefined(arrayElem))
		{
			continue;
		}

		/* Initialize result with the first non-null element of the array for comparison. */
		if (!isResultInitialized)
		{
			result->value = arrayElem->value;
			result->value_type = arrayElem->value_type;
			isResultInitialized = true;
			continue;
		}

		bool isComparisonValid = true;
		int cmp = CompareBsonValueAndType(result, arrayElem, &isComparisonValid);

		/* This part sets the value for result based on $max and $min */
		if ((cmp < 0 && isFindMax) || (cmp > 0 && !isFindMax))
		{
			result->value = arrayElem->value;
			result->value_type = arrayElem->value_type;
		}
	}
}


/*
 * Given an evaluated expression for $sum/$avg - computes the result and
 * writes it as a result.
 */
static void
SetResultValueForDollarSumAvg(const bson_value_t *inputArgument, bson_value_t *result,
							  bool isSum)
{
	/* Default value for $sum/$avg */
	if (isSum)
	{
		result->value_type = BSON_TYPE_INT32;
		result->value.v_int32 = 0;
	}
	else
	{
		result->value_type = BSON_TYPE_NULL;
	}

	/* In case the element is null return as default result value is null. */
	if (IsExpressionResultNullOrUndefined(inputArgument))
	{
		return;
	}

	if (BsonValueIsNumber(inputArgument))
	{
		*result = *inputArgument;
		return;
	}

	/* For any other case set the inputArgument as it is */
	if (inputArgument->value_type != BSON_TYPE_ARRAY)
	{
		return;
	}

	bson_iter_t arrayIterator;
	BsonValueInitIterator(inputArgument, &arrayIterator);
	bson_value_t currentSum = { 0 };
	currentSum.value_type = BSON_TYPE_INT32;
	int count = 0;
	while (bson_iter_next(&arrayIterator))
	{
		const bson_value_t *arrayElem = bson_iter_value(&arrayIterator);

		/* As per the the expected behaviour operator only considers the non-null and the non-missing values for the comparison. */
		if (IsExpressionResultNullOrUndefined(arrayElem))
		{
			continue;
		}

		if (!BsonValueIsNumber(arrayElem))
		{
			/* Skip non-numeric values */
			continue;
		}

		bool overFlowedFromInt64Ignore = false;
		AddNumberToBsonValue(&currentSum, arrayElem, &overFlowedFromInt64Ignore);
		count++;
	}

	if (count > 0)
	{
		if (!isSum)
		{
			double sum = BsonValueAsDouble(&currentSum);
			result->value_type = BSON_TYPE_DOUBLE;
			result->value.v_double = sum / count;
		}
		else
		{
			*result = currentSum;
		}
	}
}


/*
 * Evaluates the output of a $map expression.
 * $map is expressed as:
 * { $map: { input: <expression>, as: <string>, in: <expression> } }
 */
void
HandlePreParsedDollarMap(pgbson *doc, void *arguments,
						 ExpressionResult *expressionResult)
{
	DollarMapArguments *mapArguments = arguments;

	bool isNullOnEmpty = false;

	ExpressionResult childExpression = ExpressionResultCreateChild(expressionResult);

	EvaluateAggregationExpressionData(&mapArguments->input, doc, &childExpression,
									  isNullOnEmpty);

	bson_value_t evaluatedInputArg = childExpression.value;

	/* In native mongo if the input array is null or an undefined path the result is null. */
	if (IsExpressionResultNullOrUndefined(&evaluatedInputArg))
	{
		bson_value_t nullValue = {
			.value_type = BSON_TYPE_NULL
		};

		ExpressionResultSetValue(expressionResult, &nullValue);
		return;
	}

	if (evaluatedInputArg.value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(MongoLocation16883), errmsg(
							"input to $map must be an array not %s", BsonTypeName(
								evaluatedInputArg.value_type)),
						errhint("input to $map must be an array not %s",
								BsonTypeName(evaluatedInputArg.value_type))));
	}

	StringView aliasName = {
		.string = mapArguments->as.value.value.v_utf8.str,
		.length = mapArguments->as.value.value.v_utf8.len,
	};

	pgbson_element_writer *resultWriter = ExpressionResultGetElementWriter(
		expressionResult);
	pgbson_array_writer arrayWriter;
	PgbsonElementWriterStartArray(resultWriter, &arrayWriter);

	bson_iter_t arrayIter;
	BsonValueInitIterator(&evaluatedInputArg, &arrayIter);

	bson_value_t emptyValue = { 0 };
	ExpressionResultSetVariable(&childExpression, aliasName, &emptyValue);

	const bson_value_t nullValue = {
		.value_type = BSON_TYPE_NULL
	};

	while (bson_iter_next(&arrayIter))
	{
		const bson_value_t *currentElem = bson_iter_value(&arrayIter);

		ExpressionResult elementExpression = ExpressionResultCreateChild(
			&childExpression);
		ExpressionResultOverrideSingleVariableValue(&childExpression, currentElem);
		EvaluateAggregationExpressionData(&mapArguments->in, doc, &elementExpression,
										  isNullOnEmpty);
		if (IsExpressionResultNullOrUndefined(&elementExpression.value))
		{
			PgbsonArrayWriterWriteValue(&arrayWriter, &nullValue);
		}
		else
		{
			PgbsonArrayWriterWriteValue(&arrayWriter, &elementExpression.value);
		}
	}

	PgbsonElementWriterEndArray(resultWriter, &arrayWriter);
	ExpressionResultSetValueFromWriter(expressionResult);
}


/* Parses the $map expression specified in the bson_value_t and stores it in the data argument.
 */
void
ParseDollarMap(const bson_value_t *argument, AggregationExpressionData *data)
{
	if (argument->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(MongoLocation16878), errmsg(
							"$map only supports an object as its argument")));
	}

	data->operator.returnType = BSON_TYPE_ARRAY;

	bson_iter_t docIter;
	BsonValueInitIterator(argument, &docIter);

	bson_value_t input = { 0 };
	bson_value_t in = { 0 };
	bson_value_t as = { 0 };
	while (bson_iter_next(&docIter))
	{
		const char *key = bson_iter_key(&docIter);
		if (strcmp(key, "input") == 0)
		{
			input = *bson_iter_value(&docIter);
		}
		else if (strcmp(key, "in") == 0)
		{
			in = *bson_iter_value(&docIter);
		}
		else if (strcmp(key, "as") == 0)
		{
			as = *bson_iter_value(&docIter);
		}
		else
		{
			ereport(ERROR, (errcode(MongoLocation16879), errmsg(
								"Unrecognized parameter to $map: %s", key),
							errhint(
								"Unrecognized parameter to $map, unexpected key")));
		}
	}

	if (input.value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(MongoLocation16880), errmsg(
							"Missing 'input' parameter to $map")));
	}

	if (in.value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(MongoLocation16882), errmsg(
							"Missing 'in' parameter to $map")));
	}

	bson_value_t aliasValue = {
		.value_type = BSON_TYPE_UTF8,
		.value.v_utf8.len = 4,
		.value.v_utf8.str = "this"
	};

	if (as.value_type != BSON_TYPE_EOD)
	{
		if (as.value_type != BSON_TYPE_UTF8)
		{
			aliasValue.value.v_utf8.len = 0;
			aliasValue.value.v_utf8.str = "";
		}
		else
		{
			aliasValue = as;
		}

		StringView aliasNameView = {
			.length = aliasValue.value.v_utf8.len,
			.string = aliasValue.value.v_utf8.str,
		};

		ValidateVariableName(aliasNameView);
	}

	DollarMapArguments *arguments = palloc0(sizeof(DollarMapArguments));
	arguments->as.value = aliasValue;

	ParseAggregationExpressionData(&arguments->input, &input);
	ParseAggregationExpressionData(&arguments->in, &in);
	data->operator.arguments = arguments;
	data->operator.argumentsKind = AggregationExpressionArgumentsKind_Palloc;
}


/*
 * Evaluates the output of a $reduce expression.
 * $reduce is expressed as:
 * { $reduce: { input: <expression>, as: <string>, in: <expression> } }
 */
void
HandlePreParsedDollarReduce(pgbson *doc, void *arguments,
							ExpressionResult *expressionResult)
{
	DollarReduceArguments *reduceArguments = arguments;

	bool isNullOnEmpty = false;

	ExpressionResult childExpression = ExpressionResultCreateChild(expressionResult);

	EvaluateAggregationExpressionData(&reduceArguments->input, doc, &childExpression,
									  isNullOnEmpty);

	bson_value_t evaluatedInputArg = childExpression.value;

	/* In native mongo if the input array is null or an undefined path the result is null. */
	if (IsExpressionResultNullOrUndefined(&evaluatedInputArg))
	{
		bson_value_t nullValue = {
			.value_type = BSON_TYPE_NULL
		};

		ExpressionResultSetValue(expressionResult, &nullValue);
		return;
	}

	if (evaluatedInputArg.value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(MongoLocation40080), errmsg(
							"input to $reduce must be an array not %s", BsonTypeName(
								evaluatedInputArg.value_type)),
						errhint("input to $reduce must be an array not %s",
								BsonTypeName(evaluatedInputArg.value_type))));
	}

	ExpressionResultReset(&childExpression);

	EvaluateAggregationExpressionData(&reduceArguments->initialValue, doc,
									  &childExpression,
									  isNullOnEmpty);

	bson_value_t evaluatedInitialValueArg = childExpression.value;

	/* In native mongo if the input array is null or an undefined path the result is null. */
	if (IsExpressionResultNullOrUndefined(&evaluatedInitialValueArg))
	{
		bson_value_t nullValue = {
			.value_type = BSON_TYPE_NULL
		};

		ExpressionResultSetValue(expressionResult, &nullValue);
		return;
	}

	StringView thisVariableName = {
		.string = "this",
		.length = 4,
	};
	StringView valueVariableName = {
		.string = "value",
		.length = 5,
	};
	ExpressionResultSetVariable(&childExpression, valueVariableName,
								&evaluatedInitialValueArg);

	bson_iter_t arrayIter;
	BsonValueInitIterator(&evaluatedInputArg, &arrayIter);
	bson_value_t result = evaluatedInitialValueArg;
	while (bson_iter_next(&arrayIter))
	{
		const bson_value_t *currentElem = bson_iter_value(&arrayIter);
		ExpressionResult elementExpression =
			ExpressionResultCreateChild(&childExpression);
		ExpressionResultSetVariable(&childExpression, thisVariableName, currentElem);

		EvaluateAggregationExpressionData(&reduceArguments->in, doc, &elementExpression,
										  isNullOnEmpty);

		ExpressionResultSetVariable(&childExpression, valueVariableName,
									&elementExpression.value);
		result = elementExpression.value;
	}

	ExpressionResultSetValue(expressionResult, &result);
}


/* Parses the $reduce expression specified in the bson_value_t and stores it in the data argument.
 */
void
ParseDollarReduce(const bson_value_t *argument, AggregationExpressionData *data)
{
	if (argument->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(MongoLocation40075), errmsg(
							"$reduce only supports an object as its argument")));
	}

	data->operator.returnType = BSON_TYPE_ARRAY;

	bson_iter_t docIter;
	BsonValueInitIterator(argument, &docIter);

	bson_value_t input = { 0 };
	bson_value_t in = { 0 };
	bson_value_t initialValue = { 0 };
	while (bson_iter_next(&docIter))
	{
		const char *key = bson_iter_key(&docIter);
		if (strcmp(key, "input") == 0)
		{
			input = *bson_iter_value(&docIter);
		}
		else if (strcmp(key, "in") == 0)
		{
			in = *bson_iter_value(&docIter);
		}
		else if (strcmp(key, "initialValue") == 0)
		{
			initialValue = *bson_iter_value(&docIter);
		}
		else
		{
			ereport(ERROR, (errcode(MongoLocation40076), errmsg(
								"Unrecognized parameter to $reduce: %s", key),
							errhint(
								"Unrecognized parameter to $reduce, unexpected key")));
		}
	}

	if (input.value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(MongoLocation40077), errmsg(
							"Missing 'input' parameter to $reduce")));
	}

	if (in.value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(MongoLocation40079), errmsg(
							"Missing 'in' parameter to $reduce")));
	}

	if (initialValue.value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(MongoLocation40078), errmsg(
							"Missing 'initialValue' parameter to $reduce")));
	}

	DollarReduceArguments *arguments = palloc0(sizeof(DollarReduceArguments));

	ParseAggregationExpressionData(&arguments->input, &input);
	ParseAggregationExpressionData(&arguments->in, &in);
	ParseAggregationExpressionData(&arguments->initialValue, &initialValue);
	data->operator.arguments = arguments;
	data->operator.argumentsKind = AggregationExpressionArgumentsKind_Palloc;
}


/*
 * Evaluates the output of a $sortArray expression.
 * $sortArray is expressed as:
 * { $sortArray: { input: <array>, sortBy: <sort spec> } }
 */
void
HandlePreParsedDollarSortArray(pgbson *doc, void *arguments,
							   ExpressionResult *expressionResult)
{
	DollarSortArrayArguments *sortArrayArguments = arguments;
	bool isNullOnEmpty = false;
	ExpressionResult childExpression = ExpressionResultCreateChild(expressionResult);
	EvaluateAggregationExpressionData(&sortArrayArguments->input, doc, &childExpression,
									  isNullOnEmpty);
	bson_value_t evaluatedInputArg = childExpression.value;

	bson_value_t result;
	ProcessDollarSortArray(&evaluatedInputArg, &sortArrayArguments->sortContext, &result);

	ExpressionResultSetValue(expressionResult, &result);
}


/**
 * Parses the $sortArray expression specified in the bson_value_t and stores it in the data argument.
 */
void
ParseDollarSortArray(const bson_value_t *argument, AggregationExpressionData *data)
{
	if (argument->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(MongoLocation2942500), errmsg(
							"$sortArray requires an object as an argument, found: %s",
							BsonTypeName(argument->value_type)),
						errhint(
							"$sortArray requires an object as an argument, found: %s",
							BsonTypeName(argument->value_type))));
	}

	data->operator.returnType = BSON_TYPE_ARRAY;

	bson_iter_t docIter;
	BsonValueInitIterator(argument, &docIter);

	bson_value_t input = { 0 };
	bson_value_t sortby = { 0 };
	while (bson_iter_next(&docIter))
	{
		const char *key = bson_iter_key(&docIter);
		if (strcmp(key, "input") == 0)
		{
			input = *bson_iter_value(&docIter);
		}
		else if (strcmp(key, "sortBy") == 0)
		{
			sortby = *bson_iter_value(&docIter);
		}
		else
		{
			ereport(ERROR, (errcode(MongoLocation2942501), errmsg(
								"$sortArray found an unknown argument: %s", key),
							errhint(
								"$sortArray found an unknown argument: %s", key)));
		}
	}

	if (input.value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(MongoLocation2942502), errmsg(
							"$sortArray requires 'input' to be specified")));
	}

	if (sortby.value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(MongoLocation2942503), errmsg(
							"$sortArray requires 'sortBy' to be specified")));
	}

	DollarSortArrayArguments *arguments = palloc0(sizeof(DollarSortArrayArguments));

	ParseAggregationExpressionData(&arguments->input, &input);

	/* Validate $sort spec, and all nested values if value is object */
	SortContext sortContext;
	ValidateSortSpecAndSetSortContext(sortby, &sortContext);
	arguments->sortContext = sortContext;

	if (IsAggregationExpressionConstant(&arguments->input))
	{
		ProcessDollarSortArray(&arguments->input.value, &arguments->sortContext,
							   &data->value);
		data->kind = AggregationExpressionKind_Constant;

		pfree(arguments);
	}
	else
	{
		data->operator.arguments = arguments;
		data->operator.argumentsKind = AggregationExpressionArgumentsKind_Palloc;
	}
}


static
void
ProcessDollarSortArray(bson_value_t *inputValue, SortContext *sortContext,
					   bson_value_t *result)
{
	/* In native mongo if the input array is null or an undefined path the result is null. */
	if (IsExpressionResultNullOrUndefined(inputValue))
	{
		result->value_type = BSON_TYPE_NULL;
		return;
	}

	if (inputValue->value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(MongoLocation2942504), errmsg(
							"The input argument to $sortArray must be an array, but was of type: %s",
							BsonTypeName(inputValue->value_type)),
						errhint(
							"The input argument to $sortArray must be an array, but was of type: %s",
							BsonTypeName(inputValue->value_type))));
	}

	bson_iter_t arrayIter;
	BsonValueInitIterator(inputValue, &arrayIter);

	int64_t nElementsInArray = BsonDocumentValueCountKeys(inputValue);

	/* this is temp array to clone the input array */
	ElementWithIndex *elementsArr = palloc(nElementsInArray * sizeof(ElementWithIndex));

	uint64_t iteration = 0;

	while (bson_iter_next(&arrayIter))
	{
		UpdateElementWithIndex(bson_iter_value(&arrayIter), iteration,
							   &elementsArr[iteration]);
		iteration++;
	}

	qsort_arg(elementsArr, nElementsInArray, sizeof(ElementWithIndex),
			  CompareBsonValuesForSort, sortContext);

	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	pgbson_array_writer arrayWriter;
	PgbsonWriterStartArray(&writer, "", 0, &arrayWriter);

	/* Write the elements in elementsArr into result */
	for (int64_t i = 0; i < nElementsInArray; i++)
	{
		PgbsonArrayWriterWriteValue(&arrayWriter, &elementsArr[i].bsonValue);
	}

	PgbsonWriterEndArray(&writer, &arrayWriter);

	*result = PgbsonArrayWriterGetValue(&arrayWriter);

	/* All done with temp resources, release*/
	pfree(elementsArr);
}


/*
 * Evaluates the output of a $maxN/$minN expression. Via the new operator framework.
 */
void
HandlePreParsedDollarMaxMinN(pgbson *doc, void *arguments,
							 ExpressionResult *expressionResult)
{
	DollarMaxMinNArguments *maxMinNArguments = arguments;
	bool isMaxN = maxMinNArguments->isMaxN;

	bool isNullOnEmpty = false;

	ExpressionResult childExpression = ExpressionResultCreateChild(expressionResult);
	EvaluateAggregationExpressionData(&maxMinNArguments->n, doc, &childExpression,
									  isNullOnEmpty);
	bson_value_t evaluatedLimit = childExpression.value;

	ExpressionResultReset(&childExpression);
	EvaluateAggregationExpressionData(&maxMinNArguments->input, doc, &childExpression,
									  isNullOnEmpty);
	bson_value_t evaluatedInput = childExpression.value;

	bson_value_t result;

	ProcessDollarMaxMinN(&result, &evaluatedLimit, &evaluatedInput, isMaxN);

	ExpressionResultSetValue(expressionResult, &result);
}


/* Parses the $maxN expression specified in the bson_value_t and stores it in the data argument.
 */
void
ParseDollarMaxN(const bson_value_t *argument, AggregationExpressionData *data)
{
	ParseDollarMaxMinN(argument, data, true);
}


/* Parses the $minN expression specified in the bson_value_t and stores it in the data argument.
 */
void
ParseDollarMinN(const bson_value_t *argument, AggregationExpressionData *data)
{
	ParseDollarMaxMinN(argument, data, false);
}


/* Function that processes arguments for $maxN/MinN and calculate result.
 * Validates if arguments are specified correctly
 */
static void
ProcessDollarMaxMinN(bson_value_t *result, bson_value_t *evaluatedLimit,
					 bson_value_t *evaluatedInput, bool isMaxN)
{
	int64_t nValue;
	if (!IsExpressionResultNullOrUndefined(evaluatedLimit) &&
		BsonTypeIsNumber(evaluatedLimit->value_type))
	{
		nValue = BsonValueAsInt64(evaluatedLimit);

		bool checkFixedInteger = true;
		if (!IsBsonValue64BitInteger(evaluatedLimit, checkFixedInteger))
		{
			ereport(ERROR, (errcode(MongoLocation31109), errmsg(
								"Can't coerce out of range value %s to long",
								BsonValueToJsonForLogging(evaluatedLimit)),
							errhint(
								"Can't coerce out of range value to long")));
		}

		if (nValue < 1)
		{
			ereport(ERROR, (errcode(MongoLocation5787908), errmsg(
								"'n' must be greater than 0, found %ld",
								nValue),
							errhint(
								"'n' must be greater than 0, found %ld",
								nValue)));
		}
	}
	else
	{
		ereport(ERROR, (errcode(MongoLocation5787902), errmsg(
							"Value for 'n' must be of integral type, but found %s",
							BsonValueToJsonForLogging(evaluatedLimit)),
						errhint(
							"Value for 'n' must be of integral type, but found %s",
							BsonTypeName(evaluatedLimit->value_type))));
	}


	/* In native mongo if the input array is null or an undefined path the result is null. */
	if (IsExpressionResultNullOrUndefined(evaluatedInput))
	{
		result->value_type = BSON_TYPE_NULL;
		return;
	}

	if (evaluatedInput->value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(MongoLocation5788200)), errmsg(
					"Input must be an array"));
	}

	bson_iter_t arrayIter;
	BsonValueInitIterator(evaluatedInput, &arrayIter);

	int64_t nElementsInArray = BsonDocumentValueCountKeys(evaluatedInput);

	if (nValue > nElementsInArray)
	{
		nValue = nElementsInArray;  /* n val result will at most be the size of the array */
	}

	HeapComparator comparator = isMaxN == true ? HeapSortComparatorMaxN :
								HeapSortComparatorMinN;

	BinaryHeap *valueHeap = AllocateHeap(nValue, comparator);

	/* Insert all the elements into a heap */
	while (bson_iter_next(&arrayIter))
	{
		const bson_value_t *next = (bson_iter_value(&arrayIter));

		/* skip if value is null or undefined */
		if (IsExpressionResultNullOrUndefined(next))
		{
			continue;
		}

		/* Heap is full, replace the top & heapify if the new value should be included instead */
		if (valueHeap->heapSize == valueHeap->heapSpace)
		{
			const bson_value_t topHeap = TopHeap(valueHeap);
			if (!valueHeap->heapComparator(next, &topHeap))
			{
				PopFromHeap(valueHeap);
				PushToHeap(valueHeap, next);
			}
		}
		else
		{
			PushToHeap(valueHeap, next);
		}
	}

	int64_t numEntries = valueHeap->heapSize;

	bson_value_t *valueArray = (bson_value_t *) palloc(
		sizeof(bson_value_t) * numEntries);

	/* Write the array in sorted order */
	while (valueHeap->heapSize > 0)
	{
		valueArray[valueHeap->heapSize - 1] = PopFromHeap(valueHeap);
	}

	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	pgbson_array_writer arrayWriter;
	PgbsonWriterStartArray(&writer, "", 0, &arrayWriter);


	/* Write the elements in cArray into result */
	for (int64_t i = 0; i < numEntries; i++)
	{
		PgbsonArrayWriterWriteValue(&arrayWriter, &valueArray[i]);
	}

	PgbsonWriterEndArray(&writer, &arrayWriter);
	*result = PgbsonArrayWriterGetValue(&arrayWriter);

	pfree(valueArray);
	FreeHeap(valueHeap);
}


/* Parses the $maxN/$minN expression specified in the bson_value_t and stores it in the data argument.
 * $maxN is expressed as { "$maxN": { n : <numeric-expression>, input: <array-expression> } }
 * $minN is expressed as { "$minN": { n : <numeric-expression>, input: <array-expression> } }
 */
static void
ParseDollarMaxMinN(const bson_value_t *argument, AggregationExpressionData *data,
				   bool isMaxN)
{
	const char *operatorName = isMaxN == true ? "$maxN" : "$minN";

	if (argument->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(MongoLocation5787900), errmsg(
							"specification must be an object; found %s: %s", operatorName,
							BsonValueToJsonForLogging(argument)),
						errhint(
							"specification must be an object; found opname:%s input type:%s",
							operatorName, BsonTypeName(argument->value_type))));
	}

	data->operator.returnType = BSON_TYPE_ARRAY;

	bson_iter_t docIter;
	BsonValueInitIterator(argument, &docIter);

	bson_value_t input = { 0 };
	bson_value_t count = { 0 };
	while (bson_iter_next(&docIter))
	{
		const char *key = bson_iter_key(&docIter);
		if (strcmp(key, "input") == 0)
		{
			input = *bson_iter_value(&docIter);
		}
		else if (strcmp(key, "n") == 0)
		{
			count = *bson_iter_value(&docIter);
		}
		else
		{
			ereport(ERROR, (errcode(MongoLocation5787901), errmsg(
								"Unknown argument for 'n' operator: %s", key),
							errhint(
								"Unknown argument for 'n' operator: %s", key)));
		}
	}

	if (input.value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(MongoLocation5787907), errmsg(
							"Missing value for 'input'")));
	}

	if (count.value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(MongoLocation5787906), errmsg(
							"Missing value for 'n'")));
	}

	DollarMaxMinNArguments *arguments = palloc0(sizeof(DollarMaxMinNArguments));

	ParseAggregationExpressionData(&arguments->input, &input);
	ParseAggregationExpressionData(&arguments->n, &count);

	arguments->isMaxN = isMaxN;

	if (IsAggregationExpressionConstant(&arguments->input) &&
		IsAggregationExpressionConstant(&arguments->n))
	{
		ProcessDollarMaxMinN(&data->value, &arguments->n.value, &arguments->input.value,
							 isMaxN);
		data->kind = AggregationExpressionKind_Constant;
		pfree(arguments);
	}
	else
	{
		data->operator.arguments = arguments;
		data->operator.argumentsKind = AggregationExpressionArgumentsKind_Palloc;
	}
}


/*
 * Comparator function for heap utils. For MaxN, we need to build min-heap
 */
static bool
HeapSortComparatorMaxN(const void *first,
					   const void *second)
{
	bool ignoreIsComparisonValid = false; /* IsComparable ensures this is taken care of */
	return CompareBsonValueAndType((const bson_value_t *) first,
								   (const bson_value_t *) second,
								   &ignoreIsComparisonValid) < 0;
}


/*
 * Comparator function for heap utils. For MinN, we need to build max-heap
 */
static bool
HeapSortComparatorMinN(const void *first,
					   const void *second)
{
	bool ignoreIsComparisonValid = false; /* IsComparable ensures this is taken care of */
	return CompareBsonValueAndType((const bson_value_t *) first,
								   (const bson_value_t *) second,
								   &ignoreIsComparisonValid) > 0;
}


/*
 * Evaluates the output of a $zip expression.
 * $zip is expressed as:
 * { $zip: { inputs: <array of arrays>, useLongestLength: <bool>, defaults: <array> } }
 */
void
HandlePreParsedDollarZip(pgbson *doc, void *arguments,
						 ExpressionResult *expressionResult)
{
	DollarZipArguments *zipArguments = arguments;

	bool isNullOnEmpty = false;

	ExpressionResult childExpression = ExpressionResultCreateChild(expressionResult);

	EvaluateAggregationExpressionData(&zipArguments->inputs, doc, &childExpression,
									  isNullOnEmpty);

	bson_value_t evaluatedInputsArg = childExpression.value;

	ExpressionResultReset(&childExpression);

	EvaluateAggregationExpressionData(&zipArguments->defaults, doc, &childExpression,
									  isNullOnEmpty);

	bson_value_t evaluatedDefaultsArg = childExpression.value;

	bson_value_t result = { 0 };
	ProcessDollarZip(evaluatedInputsArg, zipArguments->useLongestLength.value,
					 evaluatedDefaultsArg, &result);

	ExpressionResultSetValue(expressionResult, &result);
}


/*
 * This function parses the input for operator $zip.
 * The input to this function is of the following format { $zip: { inputs: <expression(array of arrays)>, useLongestLength: <bool>, defaults: <expression> } }.
 * useLongestLength and defaults are optional arguments.
 * useLongestLength must be true if defaults is specified.
 */
void
ParseDollarZip(const bson_value_t *argument, AggregationExpressionData *data)
{
	if (argument->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(MongoLocation34460), errmsg(
							"$zip only supports an object as an argument, found %s",
							BsonTypeName(
								argument->value_type)),
						errhint("$zip only supports an object as an argument, found %s",
								BsonTypeName(
									argument->value_type))));
	}

	data->operator.returnType = BSON_TYPE_ARRAY;

	bson_iter_t docIter;
	BsonValueInitIterator(argument, &docIter);

	bson_value_t inputs = { 0 };
	bson_value_t useLongestLength = { 0 };
	bson_value_t defaults = { 0 };
	while (bson_iter_next(&docIter))
	{
		const char *key = bson_iter_key(&docIter);
		if (strcmp(key, "inputs") == 0)
		{
			inputs = *bson_iter_value(&docIter);
		}
		else if (strcmp(key, "useLongestLength") == 0)
		{
			useLongestLength = *bson_iter_value(&docIter);
		}
		else if (strcmp(key, "defaults") == 0)
		{
			defaults = *bson_iter_value(&docIter);
		}
		else
		{
			ereport(ERROR, (errcode(MongoLocation34464), errmsg(
								"$zip found an unknown argument: %s", key),
							errhint("$zip found an unknown argument: %s", key)));
		}
	}

	if (inputs.value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(MongoLocation34465), errmsg(
							"$zip requires at least one input array")));
	}

	if (useLongestLength.value_type == BSON_TYPE_EOD)
	{
		useLongestLength.value_type = BSON_TYPE_BOOL;
		useLongestLength.value.v_bool = false;
	}
	else if (useLongestLength.value_type != BSON_TYPE_BOOL)
	{
		ereport(ERROR, (errcode(MongoLocation34463), errmsg(
							"useLongestLength must be a bool, found %s", BsonTypeName(
								useLongestLength.value_type)),
						errhint("useLongestLength must be a bool, found %s",
								BsonTypeName(
									useLongestLength.value_type))));
	}

	DollarZipArguments *arguments = palloc0(sizeof(DollarZipArguments));

	arguments->useLongestLength.value = useLongestLength;
	ParseAggregationExpressionData(&arguments->inputs, &inputs);
	ParseAggregationExpressionData(&arguments->defaults, &defaults);

	if (IsAggregationExpressionConstant(&arguments->inputs) &&
		IsAggregationExpressionConstant(&arguments->defaults))
	{
		/* If all input arguments are constant, we can calculate the result now */
		ProcessDollarZip(inputs, useLongestLength, defaults, &data->value);
		data->kind = AggregationExpressionKind_Constant;
		pfree(arguments);
	}
	else
	{
		data->operator.arguments = arguments;
		data->operator.argumentsKind = AggregationExpressionArgumentsKind_Palloc;
	}
}


/* Function that processes arguments for $zip and calculate result*/
/* Validates if arguments are specified correctly */
static void
ProcessDollarZip(bson_value_t evaluatedInputsArg, bson_value_t evaluatedLongestLengthArg,
				 bson_value_t evaluatedDefaultsArg, bson_value_t *resultPtr)
{
	bson_value_t nullValue = {
		.value_type = BSON_TYPE_NULL
	};

	if (evaluatedInputsArg.value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(MongoLocation34461), errmsg(
							"inputs must be an array of expressions, found %s",
							BsonTypeName(
								evaluatedInputsArg.value_type)),
						errhint("inputs must be an array of expressions, found %s",
								BsonTypeName(
									evaluatedInputsArg.value_type))));
	}

	int rowNum = BsonDocumentValueCountKeys(&evaluatedInputsArg);

	if (rowNum == 0)
	{
		ereport(ERROR, (errcode(MongoLocation34465), errmsg(
							"$zip requires at least one input array")));
	}

	bool useLongestLengthArgBoolValue = evaluatedLongestLengthArg.value.v_bool;

	/* array to store the copy of elements in the defaults, avoid using array iterators multiple times in following loop */
	bson_value_t *defaultsElements = ParseZipDefaultsArgument(rowNum,
															  evaluatedDefaultsArg,
															  useLongestLengthArgBoolValue);

	/* struct to store the parsed inputs argument, avoid using array iterators multiple times in following loop */
	ZipParseInputsResult parsedInputs = ParseZipInputsArgument(rowNum,
															   evaluatedInputsArg,
															   useLongestLengthArgBoolValue);

	/* Early return if any of the inputs arrays resolves to a value of null or refers to a missing field */
	if (parsedInputs.outputSubArrayLength < 0)
	{
		*resultPtr = nullValue;
		pfree(defaultsElements);
		return;
	}

	SetResultArrayForDollarZip(rowNum, parsedInputs, defaultsElements, resultPtr);

	/* free the allocated memory */
	for (int i = 0; i < rowNum; i++)
	{
		if (parsedInputs.inputsElements[i])
		{
			pfree(parsedInputs.inputsElements[i]);
		}
	}
	pfree(parsedInputs.inputsElements);
	pfree(parsedInputs.inputsElementLengths);
	pfree(defaultsElements);
}


/* Function that sets result for $zip operator*/
static void
SetResultArrayForDollarZip(int rowNum, ZipParseInputsResult parsedInputs,
						   bson_value_t *defaultsElements, bson_value_t *resultPtr)
{
	/* array to store the copy of elements in the inputs, avoid using array iterators multiple times in following loop */
	bson_value_t **inputsElements = parsedInputs.inputsElements;

	/* array to store the length of each array in inputs */
	int *inputsElementLengths = parsedInputs.inputsElementLengths;

	/* length of the output subarrays */
	/* If useLongestLength is true, it is the length of the longest input array. */
	/* If useLongestLength is false, it is the length of the shortest input array. */
	int outputSubArrayLength = parsedInputs.outputSubArrayLength;

	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	pgbson_array_writer arrayWriter;
	PgbsonWriterStartArray(&writer, "", 0, &arrayWriter);

	for (int i = 0; i < outputSubArrayLength; i++)
	{
		pgbson_writer subWriter;
		PgbsonWriterInit(&subWriter);
		pgbson_array_writer subArrayWriter;
		PgbsonWriterStartArray(&subWriter, "", 0, &subArrayWriter);
		for (int j = 0; j < rowNum; j++)
		{
			if (i < inputsElementLengths[j])
			{
				PgbsonArrayWriterWriteValue(&subArrayWriter, &inputsElements[j][i]);
			}
			else
			{
				/* use default value or null */
				PgbsonArrayWriterWriteValue(&subArrayWriter, &defaultsElements[j]);
			}
		}
		PgbsonWriterEndArray(&subWriter, &subArrayWriter);
		bson_value_t subArrayValue = PgbsonArrayWriterGetValue(&subArrayWriter);
		PgbsonArrayWriterWriteValue(&arrayWriter, &subArrayValue);
	}
	PgbsonWriterEndArray(&writer, &arrayWriter);
	*resultPtr = PgbsonArrayWriterGetValue(&arrayWriter);
}


/* Function that processes defaults argument for $zip */
/* Validates if defaults is specified correctly */
static bson_value_t *
ParseZipDefaultsArgument(int rowNum, bson_value_t evaluatedDefaultsArg, bool
						 useLongestLengthArgBoolValue)
{
	if (!IsExpressionResultNullOrUndefined(&evaluatedDefaultsArg))
	{
		if (evaluatedDefaultsArg.value_type != BSON_TYPE_ARRAY)
		{
			ereport(ERROR, (errcode(MongoLocation34462), errmsg(
								"defaults must be an array of expressions, found %s",
								BsonTypeName(
									evaluatedDefaultsArg.value_type)),
							errhint("defaults must be an array of expressions, found %s",
									BsonTypeName(
										evaluatedDefaultsArg.value_type))));
		}
		else if (useLongestLengthArgBoolValue == false)
		{
			ereport(ERROR, (errcode(MongoLocation34466), errmsg(
								"cannot specify defaults unless useLongestLength is true")));
		}
		else if (BsonDocumentValueCountKeys(&evaluatedDefaultsArg) != rowNum)
		{
			ereport(ERROR, (errcode(MongoLocation34467), errmsg(
								"defaults and inputs must have the same length")));
		}
	}

	bson_value_t nullValue = {
		.value_type = BSON_TYPE_NULL
	};
	bson_value_t *defaultsElements = (bson_value_t *) palloc0(rowNum *
															  sizeof(bson_value_t));

	/* In native mongo, if defaults is empty or not specified, $zip uses null as the default value. */
	if (IsExpressionResultNullOrUndefined(&evaluatedDefaultsArg))
	{
		for (int i = 0; i < rowNum; i++)
		{
			defaultsElements[i] = nullValue;
		}
	}
	else
	{
		bson_iter_t defaultsIter;
		BsonValueInitIterator(&evaluatedDefaultsArg, &defaultsIter);

		for (int defaultsIndex = 0; bson_iter_next(&defaultsIter); defaultsIndex++)
		{
			defaultsElements[defaultsIndex] = *bson_iter_value(&defaultsIter);
		}
	}
	return defaultsElements;
}


/* Function that processes inputs argument for $zip */
/* Validates if inputs is specified correctly */
static ZipParseInputsResult
ParseZipInputsArgument(int rowNum, bson_value_t evaluatedInputsArg, bool
					   useLongestLengthArgBoolValue)
{
	/* array to store the copy of elements in the inputs, avoid using array iterators multiple times in following loop */
	bson_value_t **inputsElements = (bson_value_t **) palloc0(rowNum *
															  sizeof(bson_value_t *));

	/* array to store the length of each array in inputs */
	int *inputsElementLengths = (int *) palloc0(rowNum * sizeof(int32_t));

	bson_iter_t inputsIter;
	BsonValueInitIterator(&evaluatedInputsArg, &inputsIter);

	int maxSubArrayLength = -1;
	int minSubArrayLength = INT_MAX;

	for (int inputsIndex = 0; bson_iter_next(&inputsIter); inputsIndex++)
	{
		const bson_value_t *inputsElem = bson_iter_value(&inputsIter);

		/* The length of current subarray in inputs */
		int currentSubArrayLen = 0;

		/* In native mongo, if any of the inputs arrays resolves to a value of null or refers to a missing field, $zip returns null. */
		if (IsExpressionResultNullOrUndefined(inputsElem))
		{
			ZipParseInputsResult nullValue;
			nullValue.outputSubArrayLength = -1;
			pfree(inputsElements);
			pfree(inputsElementLengths);
			return nullValue;
		}

		/* In native mongo, if any of the inputs arrays does not resolve to an array or null nor refers to a missing field, $zip returns an error. */
		else if (inputsElem->value_type != BSON_TYPE_ARRAY)
		{
			ereport(ERROR, (errcode(MongoLocation34468), errmsg(
								"$zip found a non-array expression in input: %s",
								BsonValueToJsonForLogging(inputsElem)),
							errhint("$zip found a non-array expression in input: %s",
									BsonValueToJsonForLogging(inputsElem))));
		}
		else
		{
			currentSubArrayLen = BsonDocumentValueCountKeys(inputsElem);
		}

		maxSubArrayLength = Max(maxSubArrayLength, currentSubArrayLen);
		minSubArrayLength = Min(minSubArrayLength, currentSubArrayLen);

		inputsElementLengths[inputsIndex] = currentSubArrayLen;

		/* handle empty array in inputs */
		if (currentSubArrayLen > 0)
		{
			bson_value_t *subArrayElements = (bson_value_t *) palloc0(currentSubArrayLen *
																	  sizeof(bson_value_t));
			inputsElements[inputsIndex] = subArrayElements;

			bson_iter_t subInputArrayIter;
			BsonValueInitIterator(inputsElem, &subInputArrayIter);

			for (int subArrayIndex = 0; bson_iter_next(&subInputArrayIter);
				 subArrayIndex++)
			{
				subArrayElements[subArrayIndex] = *bson_iter_value(&subInputArrayIter);
			}
		}
		else
		{
			inputsElements[inputsIndex] = NULL;
		}
	}

	int outputSubArrayLength = useLongestLengthArgBoolValue ? maxSubArrayLength :
							   minSubArrayLength;

	return (ZipParseInputsResult) {
			   .inputsElements = inputsElements,
			   .inputsElementLengths = inputsElementLengths,
			   .outputSubArrayLength = outputSubArrayLength
	};
}
