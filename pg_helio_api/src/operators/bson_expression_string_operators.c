/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bson/bson_expression_string_operators.c
 *
 * String Operator expression implementations of BSON.
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>

#include "io/helio_bson_core.h"
#include "query/helio_bson_compare.h"
#include "operators/bson_expression.h"
#include "operators/bson_expression_operators.h"
#include "query/helio_bson_compare.h"
#include "utils/mongo_errors.h"
#include "utils/string_view.h"
#include "utils/hashset_utils.h"
#include "types/pcre_regex.h"
#include "query/bson_dollar_operators.h"

#define HELIO_PCRE2_INDEX_UNSET (~(size_t) 0)

#define MAX_REGEX_OUTPUT_BUFFER_SIZE (64 * 1024 * 1024)

/* --------------------------------------------------------- */
/* Type definitions */
/* --------------------------------------------------------- */

typedef struct DollarConcatOperatorState
{
	/* List of string  */
	List *stringList;

	/* required allocation size for concatenating all the strings  */
	int totalSize;
} DollarConcatOperatorState;

/* Struct that represents the parsed arguments to a $regexMatch, $regexFind, $regexFindAll expression. */
typedef struct DollarRegexArguments
{
	/* The input to the $regexMatch, $regexFind, $regexFindAll expression. */
	AggregationExpressionData input;

	/* The regex to the $regexMatch, $regexFind, $regexFindAll expression. */
	AggregationExpressionData regex;

	/* The options to the $regexMatch, $regexFind, $regexFindAll expression. */
	AggregationExpressionData options;

	/*
	 * If the regex and options are constant, we compile the regex during parsing, store it in PcreData,
	 * reuse it across documents without recompilation in the operator handler function.
	 */
	PcreData *pcreData;
} DollarRegexArguments;

/* Struct that represents the parsed arguments to a $replaceOne/$replaceAll expression. */
typedef struct DollarReplaceArguments
{
	/* The string input to the $replaceOne/$replaceAll expression. */
	AggregationExpressionData input;

	/* The substring to find and replace within the input. */
	AggregationExpressionData find;

	/* The string to replace find with. */
	AggregationExpressionData replacement;
} DollarReplaceArguments;

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */
static bool ProcessDollarConcatElement(bson_value_t *result,
									   const bson_value_t *currentElement,
									   bool isFieldPathExpression, void *state);
static bool ProcessDollarToUpperElement(bson_value_t *result,
										const bson_value_t *currentElement,
										bool isFieldPathExpression,
										void *state);
static bool ProcessDollarToLowerElement(bson_value_t *result,
										const bson_value_t *currentElement,
										bool isFieldPathExpression,
										void *state);
static bool ProcessDollarStrLenBytesElement(bson_value_t *result,
											const bson_value_t *currentElement,
											bool isFieldPathExpression, void *state);
static bool ProcessDollarStrLenCPElement(bson_value_t *result,
										 const bson_value_t *currentElement,
										 bool isFieldPathExpression, void *state);

static void ProcessDollarSplitResult(bson_value_t *result, void *state);
static void ProcessDollarConcatResult(bson_value_t *result, void *state);
static void ProcessDollarIndexOfBytes(bson_value_t *result, void *state);
static void ProcessDollarIndexOfCP(bson_value_t *result, void *state);
static void ProcessInputDollarIndexOfOperators(FourArgumentExpressionState *input,
											   bool isIndexOfBytesOp, int *startIndex,
											   int *endIndex);
static void WriteDollarTrimResult(bson_value_t *result, bson_value_t *input,
								  int startIndex, int endIndex,
								  ExpressionResult *expressionResult);
static bool ParseDollarTrimInput(pgbson *doc, const bson_value_t *inputDocument,
								 bson_value_t *input, bson_value_t *chars,
								 ExpressionResult *expressionResult, const char *opName);
static bool ParseDollarRegexInput(const bson_value_t *operatorValue,
								  AggregationExpressionData *data,
								  bson_value_t *input,
								  RegexData *regexData,
								  const char *opName,
								  bool enableNoAutoCapture,
								  bool *isNullOrUndefinedInput,
								  ParseAggregationExpressionContext *context);
static void ProcessDollarStrCaseCmpResult(bson_value_t *result, void *state);
static void ProcessCoersionForStrCaseCmp(bson_value_t *element);
static void ProcessDollarReplace(bson_value_t *input,
								 bson_value_t *result,
								 bson_value_t *find,
								 bson_value_t *replacement,
								 const char *opName,
								 bool isDollarReplaceOne);
static void ProcessDollarSubstrCP(bson_value_t *firstValue,
								  bson_value_t *secondValue,
								  bson_value_t *thirdValue,
								  bson_value_t *result);
static void ProcessDollarSubstrBytes(bson_value_t *firstValue,
									 bson_value_t *secondValue,
									 bson_value_t *thirdValue,
									 bson_value_t *result);
static void WriteOutputOfDollarRegexFindAll(bson_value_t *input, RegexData *regexData,
											bson_value_t *result);
static bool ValidateEvaluatedRegexInput(bson_value_t *input, bson_value_t *regex,
										bson_value_t *options, RegexData *regexData,
										const char *opName, bool enableNoAutoCapture);
static bson_value_t ConstructResultForDollarRegex(RegexData *regexData,
												  bson_value_t *input,
												  size_t *outputVector, int outputLen,
												  int *previousMatchCP);
static void ParseDollarReplaceHelper(const bson_value_t *argument,
									 AggregationExpressionData *data,
									 const char *opName,
									 bool isDollarReplaceOne,
									 ParseAggregationExpressionContext *context);
static void HandlePreParsedDollarReplaceHelper(pgbson *doc, void *arguments,
											   ExpressionResult *expressionResult,
											   const char *opName,
											   bool isDollarReplaceOne);
static void ValidateParsedInputForDollarReplace(bson_value_t *input,
												bson_value_t *result,
												bson_value_t *find,
												bson_value_t *replacement,
												const char *opName);

static bool ProcessCommonBsonTypesForStringOperators(bson_value_t *result,
													 const bson_value_t *currentElement,
													 DateStringFormatCase formatCase);
static inline StringView * AllocateStringViewFromBsonValueString(const
																 bson_value_t *element);
static inline bool BsonValueStringHasNullCharcter(const bson_value_t *element);
static inline bool IsUtf8ContinuationByte(const char *utf8Str);
static inline size_t Utf8CodePointCount(const bson_value_t *utf8Str);
static inline void ConvertToLower(char *str, uint32_t len);
static inline int GetSubstringPosition(char *str, char *substr, int strLen,
									   int subStrLen, int lastPost);
static void ReplaceSubstring(bson_value_t *result, bson_value_t *find,
							 bson_value_t *replacement, int substrPosition);
static inline uint32_t FindStartIndexDollarTrim(HTAB *charsHashTable,
												const bson_value_t *input);
static inline void FillLookUpTableDollarTrim(HTAB *charsHashTable,
											 const bson_value_t *chars);
static inline void FillLookUpTableWithWhiteSpaceChars(HTAB *charsHashTable);
static inline int FindEndIndexDollarTrim(HTAB *charsHashTable,
										 const bson_value_t *input);

/*
 * Evaluates the output of an $concat expression.
 * Since $concat is expressed as { "$concat": [ "string1", "string2" ,... ] }
 * We evaluate the inner expressions and then return the concatenation of them.
 */
void
HandleDollarConcat(pgbson *doc, const bson_value_t *operatorValue,
				   ExpressionResult *expressionResult)
{
	DollarConcatOperatorState state =
	{
		.stringList = NULL,
		.totalSize = 0,
	};

	ExpressionArgumentHandlingContext context =
	{
		.processElementFunc = ProcessDollarConcatElement,
		.processExpressionResultFunc = ProcessDollarConcatResult,
		.state = &state,
	};

	bson_value_t startValue;
	startValue.value_type = BSON_TYPE_UTF8;
	HandleVariableArgumentExpression(doc, operatorValue, expressionResult, &startValue,
									 &context);
}


/*
 * Evaluates the output of an $toUpper expression.
 * Since $toUpper is expressed as { "$toUpper": <expression> } it has only single parameter.
 * We evaluate the inner expressions and then return the value.
 */
void
HandleDollarToUpper(pgbson *doc, const bson_value_t *operatorValue,
					ExpressionResult *expressionResult)
{
	ExpressionArgumentHandlingContext context = {
		.processElementFunc = ProcessDollarToUpperElement,
		.processExpressionResultFunc = NULL,
		.state = NULL
	};

	int numberOfRequiredArgs = 1;
	HandleFixedArgumentExpression(doc, operatorValue, expressionResult,
								  numberOfRequiredArgs, "$toUpper", &context);
}


/*
 * Evaluates the output of an $toLower expression.
 * Since $toLower is expressed as { "$toLower": <expression> } it has only single parameter.
 * We evaluate the inner expressions and then return the value.
 */
void
HandleDollarToLower(pgbson *doc, const bson_value_t *operatorValue,
					ExpressionResult *expressionResult)
{
	ExpressionArgumentHandlingContext context = {
		.processElementFunc = ProcessDollarToLowerElement,
		.processExpressionResultFunc = NULL,
		.state = NULL
	};

	HandleFixedArgumentExpression(doc, operatorValue, expressionResult, 1, "$toLower",
								  &context);
}


/*
 * Evaluates the output of an $split expression.
 * Since $split is expressed as { "$split": [ <string expression>, <delimiter> ] }
 * We evaluate the inner expressions and Divides a string into an array of substrings based on a delimiter.
 */
void
HandleDollarSplit(pgbson *doc, const bson_value_t *operatorValue,
				  ExpressionResult *expressionResult)
{
	DualArgumentExpressionState state;
	memset(&state, 0, sizeof(DualArgumentExpressionState));

	ExpressionArgumentHandlingContext context =
	{
		.processElementFunc = ProcessDualArgumentElement,
		.processExpressionResultFunc = ProcessDollarSplitResult,
		.state = &state,
	};

	int numberOfRequiredArgs = 2;
	HandleFixedArgumentExpression(doc, operatorValue, expressionResult,
								  numberOfRequiredArgs, "$split", &context);
}


/*
 * Evaluates the output of an $strLenBytes expression.
 * Since $strLenBytes is expressed as { "$strLenBytes": <string expression> }
 * We evaluate the inner expressions and count number of UTF-8 encoded bytes in the specified string.
 */
void
HandleDollarStrLenBytes(pgbson *doc, const bson_value_t *operatorValue,
						ExpressionResult *expressionResult)
{
	ExpressionArgumentHandlingContext context =
	{
		.processElementFunc = ProcessDollarStrLenBytesElement,
		.processExpressionResultFunc = NULL,
		.state = NULL,
	};

	int numberOfRequiredArgs = 1;
	HandleFixedArgumentExpression(doc, operatorValue, expressionResult,
								  numberOfRequiredArgs, "$strLenBytes", &context);
}


/*
 * Evaluates the output of an $strLenCP expression.
 * Since $strLenCP is expressed as { "$strLenCP": <string expression> }
 * We evaluate the inner expressions and count number of UTF-8 encoded code points in the specified string.
 */
void
HandleDollarStrLenCP(pgbson *doc, const bson_value_t *operatorValue,
					 ExpressionResult *expressionResult)
{
	ExpressionArgumentHandlingContext context =
	{
		.processElementFunc = ProcessDollarStrLenCPElement,
		.processExpressionResultFunc = NULL,
		.state = NULL,
	};

	int numberOfRequiredArgs = 1;
	HandleFixedArgumentExpression(doc, operatorValue, expressionResult,
								  numberOfRequiredArgs, "$strLenCP", &context);
}


/*
 * Evaluates the output of an $trim expression.
 * Since $trim is expressed as { "$trim": <string expression> }
 * We evaluate the inner expressions and trim UTF-8 encoded code points from both side of string.
 */
void
HandleDollarTrim(pgbson *doc, const bson_value_t *operatorValue,
				 ExpressionResult *expressionResult)
{
	bson_value_t input = { 0 };
	bson_value_t chars = { 0 };
	bson_value_t result;
	result.value_type = BSON_TYPE_UTF8;

	if (!ParseDollarTrimInput(doc, operatorValue, &input, &chars, expressionResult,
							  "trim"))
	{
		result.value_type = BSON_TYPE_NULL;
		ExpressionResultSetValue(expressionResult, &result);
		return;
	}

	HTAB *charsHashTable = CreateStringViewHashSet();

	FillLookUpTableDollarTrim(charsHashTable, &chars);

	uint32_t startIndex = FindStartIndexDollarTrim(charsHashTable, &input);
	int endIndex = FindEndIndexDollarTrim(charsHashTable, &input);
	WriteDollarTrimResult(&result, &input, startIndex, endIndex, expressionResult);
	hash_destroy(charsHashTable);
}


/*
 * Evaluates the output of an $ltrim expression.
 * Since $ltrim is expressed as { "$ltrim": <string expression> }
 * We evaluate the inner expressions and trim UTF-8 encoded code points from left side of string.
 */
void
HandleDollarLtrim(pgbson *doc, const bson_value_t *operatorValue,
				  ExpressionResult *expressionResult)
{
	bson_value_t input = { 0 };
	bson_value_t chars = { 0 };
	bson_value_t result;
	result.value_type = BSON_TYPE_UTF8;

	if (!ParseDollarTrimInput(doc, operatorValue, &input, &chars, expressionResult,
							  "ltrim"))
	{
		result.value_type = BSON_TYPE_NULL;
		ExpressionResultSetValue(expressionResult, &result);
		return;
	}

	HTAB *charsHashTable = CreateStringViewHashSet();

	FillLookUpTableDollarTrim(charsHashTable, &chars);

	uint32_t startIndex = FindStartIndexDollarTrim(charsHashTable, &input);
	int endIndex = input.value.v_utf8.len - 1;
	WriteDollarTrimResult(&result, &input, startIndex, endIndex, expressionResult);
	hash_destroy(charsHashTable);
}


/*
 * Evaluates the output of an $rtrim expression.
 * Since $rtrim is expressed as { "$rtrim": <string expression> }
 * We evaluate the inner expressions and trim UTF-8 encoded code points from right side of string.
 */
void
HandleDollarRtrim(pgbson *doc, const bson_value_t *operatorValue,
				  ExpressionResult *expressionResult)
{
	bson_value_t input = { 0 };
	bson_value_t chars = { 0 };
	bson_value_t result;
	result.value_type = BSON_TYPE_UTF8;

	if (!ParseDollarTrimInput(doc, operatorValue, &input, &chars, expressionResult,
							  "rtrim"))
	{
		result.value_type = BSON_TYPE_NULL;
		ExpressionResultSetValue(expressionResult, &result);
		return;
	}

	HTAB *charsHashTable = CreateStringViewHashSet();

	FillLookUpTableDollarTrim(charsHashTable, &chars);

	uint32_t startIndex = 0;
	int endIndex = FindEndIndexDollarTrim(charsHashTable, &input);
	WriteDollarTrimResult(&result, &input, startIndex, endIndex, expressionResult);
	hash_destroy(charsHashTable);
}


/*
 * Evaluates the output of a $indexOfBytes expression.
 * $indexOfBytes is expressed as { "$indexOfBytes": [ <string>,<substring>,<startIndex>,<endIndex> ] }
 * We find first occurrence of substring in string using $indexOfBytes.
 */
void
HandleDollarIndexOfBytes(pgbson *doc, const bson_value_t *operatorValue,
						 ExpressionResult *expressionResult)
{
	FourArgumentExpressionState state;
	memset(&state, 0, sizeof(FourArgumentExpressionState));

	ExpressionArgumentHandlingContext context =
	{
		.processElementFunc = ProcessFourArgumentElement,
		.processExpressionResultFunc = ProcessDollarIndexOfBytes,
		.state = &state,
	};

	int minRequiredArgs = 2;
	int maxRequiredArgs = 4;

	HandleRangedArgumentExpression(doc, operatorValue, expressionResult,
								   minRequiredArgs, maxRequiredArgs, "$indexOfBytes",
								   &context);
}


/*
 * Evaluates the output of a $indexOfCP expression.
 * $indexOfCP is expressed as { "$indexOfCP": [ <string>,<substring>,<startIndex>,<endIndex> ] }
 * We find first occurrence of utf8 codepoint substring in string using $indexOfCP.
 */
void
HandleDollarIndexOfCP(pgbson *doc, const bson_value_t *operatorValue,
					  ExpressionResult *expressionResult)
{
	FourArgumentExpressionState state;
	memset(&state, 0, sizeof(FourArgumentExpressionState));

	ExpressionArgumentHandlingContext context =
	{
		.processElementFunc = ProcessFourArgumentElement,
		.processExpressionResultFunc = ProcessDollarIndexOfCP,
		.state = &state,
	};

	int minRequiredArgs = 2;
	int maxRequiredArgs = 4;

	HandleRangedArgumentExpression(doc, operatorValue, expressionResult,
								   minRequiredArgs, maxRequiredArgs, "$indexOfCP",
								   &context);
}


/*
 * Evaluates the output of an $strCaseCmp expression.
 * Since $strCaseCmp is expressed as { $strcasecmp: [ <expression1>, <expression2> ] }
 * We evaluate the inner expressions and compare chars as part of the specified string.
 */
void
HandleDollarStrCaseCmp(pgbson *doc, const bson_value_t *operatorValue,
					   ExpressionResult *expressionResult)
{
	DualArgumentExpressionState state;
	memset(&state, 0, sizeof(DualArgumentExpressionState));
	ExpressionArgumentHandlingContext context =
	{
		.processElementFunc = ProcessDualArgumentElement,
		.processExpressionResultFunc = ProcessDollarStrCaseCmpResult,
		.state = &state,
	};

	int numberOfRequiredArgs = 2;
	HandleFixedArgumentExpression(doc, operatorValue, expressionResult,
								  numberOfRequiredArgs, "$strcasecmp", &context);
}


/* Function that processes a single argument for $concat. */
static bool
ProcessDollarConcatElement(bson_value_t *result, const
						   bson_value_t *currentElement,
						   bool isFieldPathExpression, void *state)
{
	if (IsExpressionResultNullOrUndefined(currentElement))
	{
		result->value_type = BSON_TYPE_NULL;
		return false; /* stop processing more arguments. */
	}

	DollarConcatOperatorState *context = (DollarConcatOperatorState *) state;

	if (currentElement->value_type != BSON_TYPE_UTF8)
	{
		ereport(ERROR, (errcode(MongoLocation16702), errmsg(
							"$concat only supports strings, not %s",
							BsonTypeName(currentElement->value_type))));
	}

	StringView *strView = AllocateStringViewFromBsonValueString(currentElement);
	context->stringList = lappend(context->stringList, strView);
	context->totalSize += currentElement->value.v_utf8.len;
	return true;
}


/* Function that validates the final state before returning the result for $concat. */
static void
ProcessDollarConcatResult(bson_value_t *result, void *state)
{
	DollarConcatOperatorState *context = (DollarConcatOperatorState *) state;

	if (result->value_type == BSON_TYPE_NULL)
	{
		list_free_deep(context->stringList);
		return;
	}

	result->value.v_utf8.str = (char *) palloc0(context->totalSize + 1);
	result->value.v_utf8.len = 0;

	ListCell *cell;
	foreach(cell, context->stringList)
	{
		StringView *currentElement = lfirst(cell);
		memcpy(result->value.v_utf8.str + result->value.v_utf8.len,
			   currentElement->string, currentElement->length);
		result->value.v_utf8.len += currentElement->length;
		pfree(currentElement);
	}

	list_free(context->stringList);
}


/* Function that processes a single argument for $toUpper. */
static bool
ProcessDollarToUpperElement(bson_value_t *result,
							const bson_value_t *currentElement,
							bool isFieldPathExpression,
							void *state)
{
	if (IsExpressionResultNullOrUndefined(currentElement))
	{
		result->value_type = BSON_TYPE_UTF8;
		result->value.v_utf8.str = "";
		result->value.v_utf8.len = 0;
		return true;
	}

	switch (currentElement->value_type)
	{
		case BSON_TYPE_UTF8:
		{
			result->value_type = BSON_TYPE_UTF8;
			char *str = currentElement->value.v_utf8.str;
			for (uint32_t currByte = 0; currByte < currentElement->value.v_utf8.len;
				 currByte++)
			{
				if (islower(*str))
				{
					*str = 'A' + (*str - 'a');
				}
				str++;
			}
			result->value = currentElement->value;
			break;
		}

		default:
		{
			return ProcessCommonBsonTypesForStringOperators(result, currentElement,
															DateStringFormatCase_UpperCase);
		}
	}

	return true;
}


/* Function that validates the final state before returning the result for $split. */
static void
ProcessDollarSplitResult(bson_value_t *result, void *state)
{
	DualArgumentExpressionState *context = (DualArgumentExpressionState *) state;

	if (context->hasNullOrUndefined)
	{
		result->value_type = BSON_TYPE_NULL;
		return;
	}

	if (context->firstArgument.value_type != BSON_TYPE_UTF8)
	{
		ereport(ERROR, (errcode(MongoLocation40085), errmsg(
							"$split requires an expression that evaluates to a string as a first argument, found: %s",
							BsonTypeName(context->firstArgument.value_type))));
	}

	if (context->secondArgument.value_type != BSON_TYPE_UTF8)
	{
		ereport(ERROR, (errcode(MongoLocation40086), errmsg(
							"$split requires an expression that evaluates to a string as a second argument, found: %s",
							BsonTypeName(context->secondArgument.value_type))));
	}

	if (context->secondArgument.value.v_utf8.len == 0)
	{
		ereport(ERROR, (errcode(MongoLocation40087), errmsg(
							"$split requires a non-empty separator")));
	}


	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	pgbson_array_writer arrayWriter;
	PgbsonWriterStartArray(&writer, "", 0, &arrayWriter);

	uint32_t remainingStrLen = context->firstArgument.value.v_utf8.len;
	const char *currPtr = context->firstArgument.value.v_utf8.str;
	const char *headPtr = currPtr;
	uint32_t bytesToProcess = remainingStrLen;
	while (remainingStrLen >= context->secondArgument.value.v_utf8.len)
	{
		if (memcmp(currPtr, context->secondArgument.value.v_utf8.str,
				   context->secondArgument.value.v_utf8.len) == 0)
		{
			PgbsonArrayWriterWriteUtf8WithLength(&arrayWriter, headPtr, currPtr -
												 headPtr);
			currPtr += context->secondArgument.value.v_utf8.len;
			headPtr = currPtr;
			remainingStrLen -= context->secondArgument.value.v_utf8.len;
			bytesToProcess = remainingStrLen;
		}
		else
		{
			currPtr++;
			remainingStrLen--;
		}
	}

	PgbsonArrayWriterWriteUtf8WithLength(&arrayWriter, headPtr, bytesToProcess);
	PgbsonWriterEndArray(&writer, &arrayWriter);
	*result = PgbsonArrayWriterGetValue(&arrayWriter);
}


/* Function that processes a single argument for $strLenBytes. */
static bool
ProcessDollarStrLenBytesElement(bson_value_t *result, const
								bson_value_t *currentElement,
								bool isFieldPathExpression, void *state)
{
	if (currentElement->value_type != BSON_TYPE_UTF8)
	{
		ereport(ERROR, (errcode(MongoLocation34473), errmsg(
							"$strLenBytes requires a string argument, found: %s",
							currentElement->value_type == BSON_TYPE_EOD ?
							MISSING_TYPE_NAME :
							BsonTypeName(currentElement->value_type))));
	}

	result->value_type = BSON_TYPE_INT32;
	result->value.v_int32 = currentElement->value.v_utf8.len;
	return true;
}


/* Function that processes a single argument for $strLenCP. */
static bool
ProcessDollarStrLenCPElement(bson_value_t *result, const
							 bson_value_t *currentElement,
							 bool isFieldPathExpression, void *state)
{
	if (currentElement->value_type != BSON_TYPE_UTF8)
	{
		ereport(ERROR, (errcode(MongoLocation34471), errmsg(
							"$strLenCP requires a string argument, found: %s",
							currentElement->value_type == BSON_TYPE_EOD ?
							MISSING_TYPE_NAME :
							BsonTypeName(currentElement->value_type))));
	}

	result->value_type = BSON_TYPE_INT32;
	result->value.v_int32 = Utf8CodePointCount(currentElement);
	return true;
}


/* Function that processes a single argument for $tolower. */
static bool
ProcessDollarToLowerElement(bson_value_t *result,
							const bson_value_t *currentElement,
							bool isFieldPathExpression,
							void *state)
{
	if (IsExpressionResultNullOrUndefined(currentElement))
	{
		result->value_type = BSON_TYPE_UTF8;
		result->value.v_utf8.str = "";
		result->value.v_utf8.len = 0;
		return true;
	}

	switch (currentElement->value_type)
	{
		case BSON_TYPE_UTF8:
		{
			result->value_type = BSON_TYPE_UTF8;
			ConvertToLower(currentElement->value.v_utf8.str,
						   currentElement->value.v_utf8.len);
			result->value = currentElement->value;
			break;
		}

		default:
		{
			return ProcessCommonBsonTypesForStringOperators(result, currentElement,
															DateStringFormatCase_LowerCase);
		}
	}

	return true;
}


/**
 * A common function to process types for toLower and toUpper aggregation operators.
 * @param result : the final result which will contain the output
 * @param currentElement :  the element given which contains the type of input and the value of the input.
 * @param isToUpper : Since this is a common function so a way is needed to process the results differently based on upper or lower case operator. It specified whether the call was made for toUpper or toLower aggregation operator
 */
static bool
ProcessCommonBsonTypesForStringOperators(bson_value_t *result,
										 const bson_value_t *currentElement,
										 DateStringFormatCase formatCase)
{
	switch (currentElement->value_type)
	{
		case BSON_TYPE_UTF8:
		{
			result->value_type = BSON_TYPE_UTF8;
			result->value = currentElement->value;
			break;
		}

		case BSON_TYPE_INT32:
		case BSON_TYPE_INT64:
		{
			char *strNumber = (char *) BsonValueToJsonForLogging(currentElement);
			result->value_type = BSON_TYPE_UTF8;
			result->value.v_utf8.str = strNumber;
			result->value.v_utf8.len = strlen(strNumber);
			break;
		}

		case BSON_TYPE_DOUBLE:
		case BSON_TYPE_DECIMAL128:
		{
			char *strNumber = (char *) BsonValueToJsonForLogging(currentElement);
			int lenStrNumber = strlen(strNumber);

			/**
			 * This is added as special handling for $toLower euler exponents like 1E10 etc.
			 */
			if (formatCase == DateStringFormatCase_LowerCase)
			{
				ConvertToLower(strNumber, lenStrNumber);
			}
			result->value_type = BSON_TYPE_UTF8;
			result->value.v_utf8.str = strNumber;
			result->value.v_utf8.len = lenStrNumber;
			break;
		}

		case BSON_TYPE_DATE_TIME:
		{
			ExtensionTimezone timezone = {
				.isUtcOffset = true,
				.offsetInMs = 0,
			};

			int64_t dateInMs = currentElement->value.v_datetime;

			StringView dateStrView = GetDateStringWithDefaultFormat(dateInMs, timezone,
																	formatCase);
			result->value_type = BSON_TYPE_UTF8;
			result->value.v_utf8.str = (char *) dateStrView.string;
			result->value.v_utf8.len = dateStrView.length;

			break;
		}

		case BSON_TYPE_TIMESTAMP:
		{
			ExtensionTimezone timezone = {
				.isUtcOffset = true,
				.offsetInMs = 0,
			};

			StringView timestampStrView = GetTimestampStringWithDefaultFormat(
				currentElement, timezone, formatCase);
			result->value_type = BSON_TYPE_UTF8;
			result->value.v_utf8.str = (char *) timestampStrView.string;
			result->value.v_utf8.len = timestampStrView.length;
			break;
		}

		default:
		{
			ereport(ERROR, (errcode(MongoLocation16007), errmsg(
								"can't convert from BSON type %s to String",
								BsonTypeName(currentElement->value_type))));
		}
	}
	return true;
}


/*
 * This function facilitates the parsing of input for the $trim, $ltrim, and $rtrim operators.
 * It stores the parsed input into their respective variables, validates the input, and throws errors if the input is deemed improper.
 * It returns false if, during the parsing process, it determines that the output for any of the operators would be null. Otherwise, it returns true.
 */
static bool
ParseDollarTrimInput(pgbson *doc, const bson_value_t *inputDocument, bson_value_t *input,
					 bson_value_t *chars, ExpressionResult *expressionResult,
					 const char *opName)
{
	if (inputDocument->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(MongoLocation50696), errmsg(
							"%s only supports an object as an argument, found %s",
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
		else if (strcmp(key, "chars") == 0)
		{
			*chars = *bson_iter_value(&docIter);
		}
		else
		{
			ereport(ERROR, (errcode(MongoLocation50694), errmsg(
								"%s found an unknown argument: %s", opName, key)));
		}
	}

	if (input->value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(MongoLocation50695), errmsg(
							"%s requires an 'input' field", opName)));
	}

	bool isNullOnEmpty = false;
	*input = EvaluateExpressionAndGetValue(doc, input, expressionResult, isNullOnEmpty);

	if (IsExpressionResultNullOrUndefined(input))
	{
		return false;
	}

	if (input->value_type != BSON_TYPE_UTF8)
	{
		ereport(ERROR, (errcode(MongoLocation50699),
						errmsg(
							"%s requires its input to be a string, got %s (of type %s) instead.",
							opName, BsonValueToJsonForLogging(input), BsonTypeName(
								input->value_type)),
						errhint(
							"%s requires its input to be a string, got of type %s instead.",
							opName, BsonTypeName(input->value_type))));
	}

	/* chars is an optional argument, bail if not present */
	if (chars->value_type == BSON_TYPE_EOD)
	{
		return true;
	}

	*chars = EvaluateExpressionAndGetValue(doc, chars, expressionResult, isNullOnEmpty);

	if (IsExpressionResultNullOrUndefined(chars))
	{
		return false;
	}

	if (chars->value_type != BSON_TYPE_UTF8)
	{
		ereport(ERROR, (errcode(MongoLocation50700), errmsg(
							" %s requires 'chars' to be a string, got %s (of type %s) instead.",
							opName, BsonValueToJsonForLogging(chars), BsonTypeName(
								chars->value_type)), errhint(
							" %s requires 'chars' to be a string, got of type %s instead.",
							opName, BsonTypeName(chars->value_type))));
	}

	return true;
}


/*
 * This function removes everything before the start index and after the end index,
 * and then assigns the trimmed string to the result of the $trim, $rtrim, or $ltrim operator.
 */
static void
WriteDollarTrimResult(bson_value_t *result, bson_value_t *input, int startIndex,
					  int endIndex, ExpressionResult *expressionResult)
{
	int32 outputLength = (endIndex - startIndex) + 1;
	if (outputLength > 0)
	{
		result->value.v_utf8.len = outputLength;
		result->value.v_utf8.str = palloc(result->value.v_utf8.len);
		memcpy(result->value.v_utf8.str,
			   input->value.v_utf8.str + startIndex,
			   result->value.v_utf8.len);
	}
	else
	{
		result->value.v_utf8.str = "";
		result->value.v_utf8.len = 0;
	}

	ExpressionResultSetValue(expressionResult, result);
}


/* Process the $IndexOfBytes operator and find the occurance of substr and save it in result*/
static void
ProcessDollarIndexOfBytes(bson_value_t *result, void *state)
{
	FourArgumentExpressionState *context = (FourArgumentExpressionState *) state;

	if (IsExpressionResultNullOrUndefined(&context->firstArgument))
	{
		result->value_type = BSON_TYPE_NULL;
		return;
	}

	result->value_type = BSON_TYPE_INT32;
	result->value.v_int32 = -1;
	int startIndex = 0;
	int endIndex = context->firstArgument.value.v_utf8.len;

	bool isIndexOfBytesOp = true;
	ProcessInputDollarIndexOfOperators(context, isIndexOfBytesOp, &startIndex,
									   &endIndex);

	if (startIndex > endIndex)
	{
		return;
	}

	char *string = context->firstArgument.value.v_utf8.str;
	string += startIndex;
	int charToCmp = context->secondArgument.value.v_utf8.len;

	while (startIndex + charToCmp <= endIndex)
	{
		if (memcmp(string, context->secondArgument.value.v_utf8.str,
				   charToCmp) == 0)
		{
			result->value.v_int32 = startIndex;
			return;
		}

		string++;
		startIndex++;
	}
}


/* Process the $IndexOfCP operator and find the occurance of substr and save it in result */
static void
ProcessDollarIndexOfCP(bson_value_t *result, void *state)
{
	FourArgumentExpressionState *context = (FourArgumentExpressionState *) state;
	if (IsExpressionResultNullOrUndefined(&context->firstArgument))
	{
		result->value_type = BSON_TYPE_NULL;
		return;
	}

	result->value_type = BSON_TYPE_INT32;
	result->value.v_int64 = -1;
	int startIndex = 0;
	int endIndex = -1;

	bool isIndexOfBytesOp = false;
	ProcessInputDollarIndexOfOperators(context, isIndexOfBytesOp, &startIndex,
									   &endIndex);

	if (endIndex == -1)
	{
		endIndex = Utf8CodePointCount(&context->firstArgument);
	}

	if (startIndex > endIndex)
	{
		return;
	}

	char *string = context->firstArgument.value.v_utf8.str;
	int currCP = 0;

	while (currCP < startIndex)
	{
		if (!IsUtf8ContinuationByte(++string))
		{
			currCP++;
		}
	}

	int cpToCompare = Utf8CodePointCount(&context->secondArgument);
	while (currCP + cpToCompare <= endIndex)
	{
		if (IsUtf8ContinuationByte(string))
		{
			string++;
			continue;
		}

		if (memcmp(string, context->secondArgument.value.v_utf8.str,
				   context->secondArgument.value.v_utf8.len) == 0)
		{
			result->value.v_int32 = currCP;
			return;
		}

		string++;
		currCP++;
	}
}


/*
 * This function facilitates the parsing and valodation of input for the $indexOfBytes, $indexOfCP operators.
 * it finds out startIndex and endIndex from the input and populates corresponding variables.
 */
static void
ProcessInputDollarIndexOfOperators(FourArgumentExpressionState *input,
								   bool isIndexOfBytesOp,
								   int *startIndex, int *endIndex)
{
	const char *opName = isIndexOfBytesOp ? "$indexOfBytes" : "$indexOfCP";

	if (input->firstArgument.value_type != BSON_TYPE_UTF8)
	{
		int errorCode = isIndexOfBytesOp ? MongoLocation40091 : MongoLocation40093;
		ereport(ERROR, (errcode(errorCode), errmsg(
							"%s requires a string as the first argument, found: %s",
							opName,
							BsonTypeName(input->firstArgument.value_type))));
	}

	if (input->secondArgument.value_type != BSON_TYPE_UTF8)
	{
		int errorCode = isIndexOfBytesOp ? MongoLocation40092 : MongoLocation40094;
		ereport(ERROR, (errcode(errorCode), errmsg(
							"%s requires a string as the second argument, found: %s",
							opName,
							input->secondArgument.value_type == BSON_TYPE_EOD ?
							MISSING_TYPE_NAME :
							BsonTypeName(input->secondArgument.value_type))));
	}

	if (input->totalProcessedArgs >= 3)
	{
		if (!IsBsonValueFixedInteger(&input->thirdArgument))
		{
			ereport(ERROR, (errcode(MongoLocation40096), errmsg(
								"%s requires an integral starting index, found a value of type: %s, with value: %s",
								opName,
								input->thirdArgument.value_type == BSON_TYPE_EOD ?
								MISSING_TYPE_NAME :
								BsonTypeName(input->thirdArgument.value_type),
								input->thirdArgument.value_type == BSON_TYPE_EOD ?
								MISSING_VALUE_NAME :
								BsonValueToJsonForLogging(&input->thirdArgument)),
							errhint(
								"%s requires an integral starting index, found a value of type: %s",
								opName,
								input->thirdArgument.value_type == BSON_TYPE_EOD ?
								MISSING_TYPE_NAME :
								BsonTypeName(input->thirdArgument.value_type))));
		}

		*startIndex = BsonValueAsInt32(&input->thirdArgument);
		if (*startIndex < 0)
		{
			ereport(ERROR, (errcode(MongoLocation40097), errmsg(
								"%s requires a nonnegative start index, found: %d",
								opName,
								*startIndex)));
		}
	}

	if (input->totalProcessedArgs == 4)
	{
		if (!IsBsonValueFixedInteger(&input->fourthArgument))
		{
			ereport(ERROR, (errcode(MongoLocation40096), errmsg(
								"%s requires an integral ending index, found a value of type: %s, with value: %s",
								opName,
								input->fourthArgument.value_type == BSON_TYPE_EOD ?
								MISSING_TYPE_NAME :
								BsonTypeName(input->fourthArgument.value_type),
								input->fourthArgument.value_type == BSON_TYPE_EOD ?
								MISSING_VALUE_NAME :
								BsonValueToJsonForLogging(&input->fourthArgument)),
							errhint(
								"%s requires an integral ending index, found a value of type: %s",
								opName,
								input->fourthArgument.value_type == BSON_TYPE_EOD ?
								MISSING_TYPE_NAME :
								BsonTypeName(input->fourthArgument.value_type))));
		}

		*endIndex = BsonValueAsInt32(&input->fourthArgument);

		if (*endIndex < 0)
		{
			ereport(ERROR, (errcode(MongoLocation40097), errmsg(
								"%s requires a nonnegative ending index, found: %d",
								opName,
								*endIndex)));
		}

		if (isIndexOfBytesOp)
		{
			*endIndex = *endIndex < (int) input->firstArgument.value.v_utf8.len ?
						*endIndex :
						(int) input->firstArgument.value.v_utf8.len;
		}
		else
		{
			int cpCount = Utf8CodePointCount(&input->firstArgument);
			*endIndex = *endIndex < cpCount ? *endIndex : cpCount;
		}
	}
}


/* Function that processes array for $strcasecmp. */
static void
ProcessDollarStrCaseCmpResult(bson_value_t *result, void *state)
{
	DualArgumentExpressionState *dualState = (DualArgumentExpressionState *) state;

	ProcessCoersionForStrCaseCmp(&dualState->firstArgument);
	ProcessCoersionForStrCaseCmp(&dualState->secondArgument);

	int cmp = CompareStrings(dualState->firstArgument.value.v_utf8.str,
							 dualState->firstArgument.value.v_utf8.len,
							 dualState->secondArgument.value.v_utf8.str,
							 dualState->secondArgument.value.v_utf8.len);
	result->value_type = BSON_TYPE_INT32;
	result->value.v_int32 = cmp == 0 ? 0 : cmp > 0 ? 1 : -1;
}


/**
 * @param element : this refers to the element which is given as input aka currentElement in most of the operator handlers
 * This function processes the current element converts to UTF-8 for some bson types and then converts them to a common case so that they can be comparison can be case in-sensitive
 */
static void
ProcessCoersionForStrCaseCmp(bson_value_t *element)
{
	switch (element->value_type)
	{
		case BSON_TYPE_INT32:
		case BSON_TYPE_INT64:
		case BSON_TYPE_DOUBLE:
		case BSON_TYPE_DECIMAL128:
		{
			char *strNumber = (char *) BsonValueToJsonForLogging(element);
			element->value_type = BSON_TYPE_UTF8;
			element->value.v_utf8.str = strNumber;
			element->value.v_utf8.len = strlen(strNumber);
			break;
		}

		case BSON_TYPE_DATE_TIME:
		{
			ExtensionTimezone timezone = {
				.isUtcOffset = true,
				.offsetInMs = 0,
			};

			int64_t dateInMs = element->value.v_datetime;
			StringView dateStrView = GetDateStringWithDefaultFormat(dateInMs, timezone,
																	DateStringFormatCase_LowerCase);
			element->value_type = BSON_TYPE_UTF8;
			element->value.v_utf8.str = (char *) dateStrView.string;
			element->value.v_utf8.len = dateStrView.length;
			return;
		}

		case BSON_TYPE_TIMESTAMP:
		{
			ExtensionTimezone timezone = {
				.isUtcOffset = true,
				.offsetInMs = 0,
			};
			StringView timestampStrView = GetTimestampStringWithDefaultFormat(element,
																			  timezone,
																			  DateStringFormatCase_LowerCase);
			element->value_type = BSON_TYPE_UTF8;
			element->value.v_utf8.str = (char *) timestampStrView.string;
			element->value.v_utf8.len = timestampStrView.length;
			return;
		}

		default:
		{
			if (IsExpressionResultNullOrUndefined(element))
			{
				element->value_type = BSON_TYPE_UTF8;
				element->value.v_utf8.str = "";
				element->value.v_utf8.len = 0;
				return;
			}
			if (element->value_type != BSON_TYPE_UTF8)
			{
				ereport(ERROR, (errcode(MongoLocation16007), errmsg(
									"can't convert from BSON type %s to String",
									BsonTypeName(element->value_type))));
			}
			break;
		}
	}


	/**
	 * This is done as this operator is case insentitive. So, to compare 2 strings that way we need to convert them to a common case. This currently will only process numbers as time and date are returned in lower case format only.
	 */
	ConvertToLower(element->value.v_utf8.str, element->value.v_utf8.len);
}


/*
 * This utility function is designed for the $trim, $ltrim, and $rtrim operations. It populates a hashmap by utilizing the input variable "chars".
 * Each code point from the "chars" string will have its own entry in the hashtable.
 * For example, if "chars" contains "a😊\u0000", the hashtable will have 3 entries :
 * 'a', '😊', '\u0000'
 * By utilizing this hashtable, we can efficiently trim the "input" string by checking which all codepoints from the front and back of the input string exist in the hashtable.
 * This approach avoids the need to repeatedly traverse the "chars" string to check if a code point from the input string exists in "chars".
 */
static inline void
FillLookUpTableDollarTrim(HTAB *charsHashTable, const bson_value_t *chars)
{
	/* chars is an optional argument if it is not present then need to remove whitespaces */
	if (chars->value_type == BSON_TYPE_EOD)
	{
		FillLookUpTableWithWhiteSpaceChars(charsHashTable);
		return;
	}

	char *codePointCurrPtr = chars->value.v_utf8.str;
	char *codePointStartPtr = codePointCurrPtr;
	int currCodePointLength = 1;

	for (uint32_t currIndex = 0; currIndex < chars->value.v_utf8.len; currIndex++)
	{
		if (IsUtf8ContinuationByte(++codePointCurrPtr))
		{
			currCodePointLength++;
			continue;
		}

		bool ignoreFound;
		StringView hashEntry = {
			.string = codePointStartPtr, .length = currCodePointLength
		};
		hash_search(charsHashTable,
					&hashEntry,
					HASH_ENTER,
					&ignoreFound);
		codePointStartPtr = codePointCurrPtr;
		currCodePointLength = 1;
	}
}


/*
 * This function identifies the starting index of a UTF-8 input string,
 * which allows the $trim, $rtrim, and $ltrim operators to trim the string before that determined index.
 */
static inline uint32_t
FindStartIndexDollarTrim(HTAB *charsHashTable, const bson_value_t *input)
{
	uint32_t startIndex = 0;
	char *codePointCurrPtr = input->value.v_utf8.str;
	char *codePointStartPtr = codePointCurrPtr;
	int currCodePointLength = 1;

	for (uint32_t currIndex = 0; currIndex < input->value.v_utf8.len; currIndex++)
	{
		if (IsUtf8ContinuationByte(++codePointCurrPtr))
		{
			currCodePointLength++;
			continue;
		}

		bool found = false;
		StringView hashEntry = {
			.string = codePointStartPtr, .length = currCodePointLength
		};
		hash_search(charsHashTable,
					&hashEntry,
					HASH_FIND,
					&found);

		if (!found)
		{
			break;
		}

		startIndex += currCodePointLength;
		codePointStartPtr = codePointCurrPtr;
		currCodePointLength = 1;
	}

	return startIndex;
}


/*
 * This function identifies the end index of a UTF-8 input string,
 * which allows the $trim, $rtrim, and $ltrim operators to trim the ending after that determined index.
 */
static inline int
FindEndIndexDollarTrim(HTAB *charsHashTable, const bson_value_t *input)
{
	uint32_t endIndex = input->value.v_utf8.len - 1;
	char *codePointCurrPtr = input->value.v_utf8.str + endIndex;
	int currCodePointLength = 1;
	for (uint32_t currIndex = input->value.v_utf8.len; currIndex > 0; currIndex--)
	{
		if (IsUtf8ContinuationByte(codePointCurrPtr--))
		{
			currCodePointLength++;
			continue;
		}

		bool found = false;
		StringView hashEntry = {
			.string = codePointCurrPtr + 1, .length = currCodePointLength
		};
		hash_search(charsHashTable,
					&hashEntry,
					HASH_FIND,
					&found);
		if (!found)
		{
			break;
		}

		endIndex -= currCodePointLength;
		currCodePointLength = 1;
	}

	return endIndex;
}


/*
 * This utility function is designed for the $trim, $ltrim, and $rtrim operations.
 * It populates a hashmap with whitespace charachters of utf8 .
 */
static inline void
FillLookUpTableWithWhiteSpaceChars(HTAB *charsHashTable)
{
	static const StringView entries[] = {
		[0] = { .string = "\0", .length = 1 },
		[1] = { .string = " ", .length = 1 },
		[2] = { .string = "\t", .length = 1 },
		[3] = { .string = "\n", .length = 1 },
		[4] = { .string = "\v", .length = 1 },
		[5] = { .string = "\f", .length = 1 },
		[6] = { .string = "\r", .length = 1 },
		[7] = { .string = "\xE2\x80\xA0", .length = 2 },
		[8] = { .string = "\xE1\x9A\x80", .length = 3 },
		[9] = { .string = "\xE2\x80\x80", .length = 3 },
		[10] = { .string = "\xE2\x80\x81", .length = 3 },
		[11] = { .string = "\xE2\x80\x82", .length = 3 },
		[12] = { .string = "\xE2\x80\x83", .length = 3 },
		[13] = { .string = "\xE2\x80\x84", .length = 3 },
		[14] = { .string = "\xE2\x80\x85", .length = 3 },
		[15] = { .string = "\xE2\x80\x86", .length = 3 },
		[16] = { .string = "\xE2\x80\x87", .length = 3 },
		[17] = { .string = "\xE2\x80\x88", .length = 3 },
		[18] = { .string = "\xE2\x80\x89", .length = 3 },
		[19] = { .string = "\xE2\x80\x8A", .length = 3 }
	};

	for (int i = 0; i < 20; i++)
	{
		bool ignoreFound;
		hash_search(charsHashTable,
					&entries[i],
					HASH_ENTER,
					&ignoreFound);
	}
}


/*
 * @param : utf8Str The UTF-8 string.
 * @return : count of code-points presents in UTF-8 string.
 */
static inline size_t
Utf8CodePointCount(const bson_value_t *utf8Str)
{
	char *str = utf8Str->value.v_utf8.str;
	size_t codePointCount = 0;
	for (uint32_t currByte = 0; currByte < utf8Str->value.v_utf8.len; currByte++)
	{
		/* Increment the codePointCount if the current byte is not a UTF-8 continuation byte. */
		codePointCount += !IsUtf8ContinuationByte(str++);
	}

	return codePointCount;
}


/*
 * Checks if the given character is a UTF-8 continuation byte.
 *
 * @param : utf8Str The UTF-8 string to check.
 * @return : true if the character is a UTF-8 continuation byte, false otherwise.
 */
static inline bool
IsUtf8ContinuationByte(const char *utf8Str)
{
	/*
	 * To determine if a character is a UTF-8 continuation byte, we examine its first two bits.
	 * Continuation bytes always have a binary pattern of '10xxxxxx'.
	 * To perform this check, we use a bitwise AND operation with the mask '0xC0' (binary: 11000000) on the character.
	 * If the result matches '0x80' (binary: 10000000), it indicates that the character is a continuation byte."
	 */
	return (*utf8Str & 0xC0) == 0x80;
}


/**
 * A common helper function to convert a given UTF-8 type string to lower.
 * It operates only on Upper case letters and converts them to lower case.
 * @param str : string which needs to be iterated and converted to lower case.
 * @param len : len in bytes of the given str
 */
static inline void
ConvertToLower(char *str, uint32_t len)
{
	for (uint32_t currByte = 0; currByte < len; currByte++)
	{
		if (isupper(*str))
		{
			*str = 'a' + (*str - 'A');
		}
		str++;
	}
}


/*
 * The function allocates memory for a new StringView object and populates the stringview by using a bson_value
 * that is expected to be of type BSON_TYPE_UTF8.
 */
static inline StringView *
AllocateStringViewFromBsonValueString(const bson_value_t *element)
{
	StringView *strView = palloc(sizeof(StringView));
	strView->string = element->value.v_utf8.str;
	strView->length = element->value.v_utf8.len;
	return strView;
}


/* *******************************************
 *  New aggregation operator's framework which uses pre parsed expression
 *  when building the projection tree.
 *  *******************************************
 */


/*
 * Evaluates the output of an $regexMatch expression.
 * Since $regexMatch is expressed as { "$regexMatch": {"input" :<string>, "regex" : <regex pattern>, "options": <regexOption>}}
 * We evaluate the inner expressions and try to find match, returns true if match exist.
 */
void
HandlePreParsedDollarRegexMatch(pgbson *doc, void *arguments,
								ExpressionResult *expressionResult)
{
	DollarRegexArguments *regexArgs = (DollarRegexArguments *) arguments;

	bool isNullOnEmpty = false;
	ExpressionResult childResult = ExpressionResultCreateChild(expressionResult);
	EvaluateAggregationExpressionData(&regexArgs->input, doc, &childResult,
									  isNullOnEmpty);
	bson_value_t input = childResult.value;

	ExpressionResultReset(&childResult);
	EvaluateAggregationExpressionData(&regexArgs->regex, doc, &childResult,
									  isNullOnEmpty);
	bson_value_t regex = childResult.value;

	ExpressionResultReset(&childResult);
	EvaluateAggregationExpressionData(&regexArgs->options, doc, &childResult,
									  isNullOnEmpty);
	bson_value_t options = childResult.value;
	RegexData regexData = { 0 };
	bool isRegexAlreadyCompiled = false;
	bool enableNoAutoCapture = true;

	if (regexArgs->pcreData)
	{
		isRegexAlreadyCompiled = true;
		regexData.pcreData = regexArgs->pcreData;
	}

	if (!ValidateEvaluatedRegexInput(&input, &regex, &options, &regexData, "$regexMatch",
									 enableNoAutoCapture))
	{
		bson_value_t falseValue = {
			.value_type = BSON_TYPE_BOOL,
			.value.v_bool = false
		};

		ExpressionResultSetValue(expressionResult, &falseValue);
		return;
	}

	bson_value_t resultValue = { .value_type = BSON_TYPE_BOOL };
	resultValue.value.v_bool = CompareRegexTextMatch(&input, &regexData);
	ExpressionResultSetValue(expressionResult, &resultValue);

	if (!isRegexAlreadyCompiled)
	{
		FreePcreData(regexData.pcreData);
	}
}


/*
 * Evaluates the output of an $regexFind expression.
 * Since $regexFind is expressed as { "$regexFind": {"input" :<string>, "regex" : <regex pattern>, "options": <regexOption>}}
 * We evaluate the inner expressions and find the first matching pattern.
 */
void
HandlePreParsedDollarRegexFind(pgbson *doc, void *arguments,
							   ExpressionResult *expressionResult)
{
	DollarRegexArguments *regexArgs = (DollarRegexArguments *) arguments;

	bool isNullOnEmpty = false;
	ExpressionResult childResult = ExpressionResultCreateChild(expressionResult);
	EvaluateAggregationExpressionData(&regexArgs->input, doc, &childResult,
									  isNullOnEmpty);
	bson_value_t input = childResult.value;

	ExpressionResultReset(&childResult);
	EvaluateAggregationExpressionData(&regexArgs->regex, doc, &childResult,
									  isNullOnEmpty);
	bson_value_t regex = childResult.value;

	ExpressionResultReset(&childResult);
	EvaluateAggregationExpressionData(&regexArgs->options, doc, &childResult,
									  isNullOnEmpty);
	bson_value_t options = childResult.value;
	bson_value_t resultValue = { .value_type = BSON_TYPE_NULL };
	RegexData regexData = { 0 };
	bool isRegexAlreadyCompiled = false;
	bool enableNoAutoCapture = false;

	if (regexArgs->pcreData)
	{
		isRegexAlreadyCompiled = true;
		regexData.pcreData = regexArgs->pcreData;
	}

	if (!ValidateEvaluatedRegexInput(&input, &regex, &options, &regexData, "$regexFind",
									 enableNoAutoCapture))
	{
		ExpressionResultSetValue(expressionResult, &resultValue);
		return;
	}

	if (!CompareRegexTextMatch(&input, &regexData))
	{
		ExpressionResultSetValue(expressionResult, &resultValue);
		return;
	}

	size_t *outputVector = GetResultVectorUsingPcreData(regexData.pcreData);
	int outputLen = GetResultLengthUsingPcreData(regexData.pcreData);
	int ignorePreviousMatchCP = 0;

	resultValue = ConstructResultForDollarRegex(&regexData, &input, outputVector,
												outputLen,
												&ignorePreviousMatchCP);
	ExpressionResultSetValue(expressionResult, &resultValue);

	if (!isRegexAlreadyCompiled)
	{
		FreePcreData(regexData.pcreData);
	}
}


/*
 * Evaluates the output of an $regexFindAll expression.
 * Since $regexFindAll is expressed as { "$regexFindAll": {"input" :<string>, "regex" : <regex pattern>, "options": <regexOption>}}
 * We evaluate the inner expressions and find the all matching pattern.
 */
void
HandlePreParsedDollarRegexFindAll(pgbson *doc, void *arguments,
								  ExpressionResult *expressionResult)
{
	DollarRegexArguments *regexArgs = (DollarRegexArguments *) arguments;

	bool isNullOnEmpty = false;
	ExpressionResult childResult = ExpressionResultCreateChild(expressionResult);
	EvaluateAggregationExpressionData(&regexArgs->input, doc, &childResult,
									  isNullOnEmpty);
	bson_value_t input = childResult.value;

	ExpressionResultReset(&childResult);
	EvaluateAggregationExpressionData(&regexArgs->regex, doc, &childResult,
									  isNullOnEmpty);
	bson_value_t regex = childResult.value;

	ExpressionResultReset(&childResult);
	EvaluateAggregationExpressionData(&regexArgs->options, doc, &childResult,
									  isNullOnEmpty);
	bson_value_t options = childResult.value;
	bson_value_t result;
	RegexData regexData = { 0 };
	bool isRegexAlreadyCompiled = false;
	bool enableNoAutoCapture = false;

	if (regexArgs->pcreData)
	{
		isRegexAlreadyCompiled = true;
		regexData.pcreData = regexArgs->pcreData;
	}

	if (!ValidateEvaluatedRegexInput(&input, &regex, &options, &regexData,
									 "$regexFindAll", enableNoAutoCapture))
	{
		InitBsonValueAsEmptyArray(&result);
		ExpressionResultSetValue(expressionResult, &result);
		return;
	}

	WriteOutputOfDollarRegexFindAll(&input, &regexData, &result);
	ExpressionResultSetValue(expressionResult, &result);

	if (!isRegexAlreadyCompiled)
	{
		FreePcreData(regexData.pcreData);
	}
}


/*
 * Evaluates the output of an $substr/$substrBytes expression.
 * $substr is expressed as { "$substr": [<string>, <offset>, <length>] }
 */
void
HandlePreParsedDollarSubstrBytes(pgbson *doc, void *arguments,
								 ExpressionResult *expressionResult)
{
	List *argList = arguments;

	AggregationExpressionData *first = list_nth(argList, 0);
	AggregationExpressionData *second = list_nth(argList, 1);
	AggregationExpressionData *third = list_nth(argList, 2);

	bool isNullOnEmpty = false;

	ExpressionResult childResult = ExpressionResultCreateChild(expressionResult);
	EvaluateAggregationExpressionData(first, doc, &childResult, isNullOnEmpty);

	bson_value_t firstValue = childResult.value;

	ExpressionResultReset(&childResult);
	EvaluateAggregationExpressionData(second, doc, &childResult, isNullOnEmpty);

	bson_value_t secondValue = childResult.value;

	ExpressionResultReset(&childResult);
	EvaluateAggregationExpressionData(third, doc, &childResult, isNullOnEmpty);

	bson_value_t thirdValue = childResult.value;

	bson_value_t result;
	ProcessDollarSubstrBytes(&firstValue, &secondValue, &thirdValue, &result);

	ExpressionResultSetValue(expressionResult, &result);
}


/*
 * Evaluates the output of an $substrCP expression.
 * $substrCP is expressed as { "$substrCP": [<string>, <offset>, <length>] }
 */
void
HandlePreParsedDollarSubstrCP(pgbson *doc, void *arguments,
							  ExpressionResult *expressionResult)
{
	List *argList = arguments;

	AggregationExpressionData *first = list_nth(argList, 0);
	AggregationExpressionData *second = list_nth(argList, 1);
	AggregationExpressionData *third = list_nth(argList, 2);

	bool isNullOnEmpty = false;

	ExpressionResult childResult = ExpressionResultCreateChild(expressionResult);
	EvaluateAggregationExpressionData(first, doc, &childResult, isNullOnEmpty);

	bson_value_t firstValue = childResult.value;

	ExpressionResultReset(&childResult);
	EvaluateAggregationExpressionData(second, doc, &childResult, isNullOnEmpty);

	bson_value_t secondValue = childResult.value;

	ExpressionResultReset(&childResult);
	EvaluateAggregationExpressionData(third, doc, &childResult, isNullOnEmpty);

	bson_value_t thirdValue = childResult.value;

	bson_value_t result;
	ProcessDollarSubstrCP(&firstValue, &secondValue, &thirdValue, &result);

	ExpressionResultSetValue(expressionResult, &result);
}


/*
 * Parses an $regexMatch expression and sets the parsed data in the data argument.
 * $regexMatch is expressed as { "$regexMatch": { "input" : <string>, "regex" : <string>, "options": <string>}}
 */
void
ParseDollarRegexMatch(const bson_value_t *argument, AggregationExpressionData *data,
					  ParseAggregationExpressionContext *context)
{
	bson_value_t input = { 0 };
	RegexData regexData = { 0 };
	bool enableNoAutoCapture = true;
	bool isNullOrUndefinedInput = false;

	if (ParseDollarRegexInput(argument, data, &input, &regexData, "$regexMatch",
							  enableNoAutoCapture, &isNullOrUndefinedInput, context))
	{
		if (isNullOrUndefinedInput)
		{
			data->value.value_type = BSON_TYPE_BOOL;
			data->value.value.v_bool = false;
			return;
		}

		data->value.value_type = BSON_TYPE_BOOL;
		data->value.value.v_bool = CompareRegexTextMatch(&input, &regexData);
		FreePcreData(regexData.pcreData);
	}
}


/*
 * Parses an $regexFind expression and sets the parsed data in the data argument.
 * $regexFind is expressed as { "$regexFind": { "input" : <string>, "regex" : <string>, "options": <string>}}
 */
void
ParseDollarRegexFind(const bson_value_t *argument, AggregationExpressionData *data,
					 ParseAggregationExpressionContext *context)
{
	bson_value_t input = { 0 };
	RegexData regexData = { 0 };
	bool enableNoAutoCapture = false;
	bool isNullOrUndefinedInput = false;

	if (ParseDollarRegexInput(argument, data, &input, &regexData, "$regexFind",
							  enableNoAutoCapture, &isNullOrUndefinedInput, context))
	{
		if (isNullOrUndefinedInput || !CompareRegexTextMatch(&input, &regexData))
		{
			data->value.value_type = BSON_TYPE_NULL;
			FreePcreData(regexData.pcreData);
			return;
		}

		size_t *outputVector = GetResultVectorUsingPcreData(regexData.pcreData);
		int outputLen = GetResultLengthUsingPcreData(regexData.pcreData);
		int ignorePreviousMatchCP = 0;

		data->value = ConstructResultForDollarRegex(&regexData, &input, outputVector,
													outputLen,
													&ignorePreviousMatchCP);
		FreePcreData(regexData.pcreData);
	}
}


/*
 * Parses an $regexFindAll expression and sets the parsed data in the data argument.
 * $regexFindAll is expressed as { "$regexFindAll": { "input" : <string>, "regex" : <string>, "options": <string>}}
 */
void
ParseDollarRegexFindAll(const bson_value_t *argument, AggregationExpressionData *data,
						ParseAggregationExpressionContext *context)
{
	bson_value_t input = { 0 };
	RegexData regexData = { 0 };
	bool enableNoAutoCapture = false;
	bool isNullOrUndefinedInput = false;

	if (ParseDollarRegexInput(argument, data, &input, &regexData, "$regexFindAll",
							  enableNoAutoCapture, &isNullOrUndefinedInput, context))
	{
		if (isNullOrUndefinedInput)
		{
			InitBsonValueAsEmptyArray(&(data->value));
			return;
		}

		WriteOutputOfDollarRegexFindAll(&input, &regexData, &data->value);
		FreePcreData(regexData.pcreData);
	}
}


/*
 * Parses an $substrBytes expression and sets the parsed data in the data argument.
 */
void
ParseDollarSubstrBytes(const bson_value_t *argument, AggregationExpressionData *data,
					   ParseAggregationExpressionContext *context)
{
	int numOfRequiredArgs = 3;
	List *arguments = ParseFixedArgumentsForExpression(argument,
													   numOfRequiredArgs,
													   "$substrBytes",
													   &data->operator.
													   argumentsKind,
													   context);

	AggregationExpressionData *first = list_nth(arguments, 0);
	AggregationExpressionData *second = list_nth(arguments, 1);
	AggregationExpressionData *third = list_nth(arguments, 2);

	if (IsAggregationExpressionConstant(first) &&
		IsAggregationExpressionConstant(second) &&
		IsAggregationExpressionConstant(third))
	{
		ProcessDollarSubstrBytes(&first->value, &second->value, &third->value,
								 &data->value);

		data->kind = AggregationExpressionKind_Constant;

		list_free_deep(arguments);

		return;
	}

	data->operator.arguments = arguments;
}


/*
 * Parses an $substrCP expression and sets the parsed data in the data argument.
 */
void
ParseDollarSubstrCP(const bson_value_t *argument, AggregationExpressionData *data,
					ParseAggregationExpressionContext *context)
{
	int numOfRequiredArgs = 3;
	List *arguments = ParseFixedArgumentsForExpression(argument,
													   numOfRequiredArgs,
													   "$substrCP",
													   &data->operator.
													   argumentsKind,
													   context);

	AggregationExpressionData *first = list_nth(arguments, 0);
	AggregationExpressionData *second = list_nth(arguments, 1);
	AggregationExpressionData *third = list_nth(arguments, 2);

	if (IsAggregationExpressionConstant(first) &&
		IsAggregationExpressionConstant(second) &&
		IsAggregationExpressionConstant(third))
	{
		ProcessDollarSubstrCP(&first->value, &second->value, &third->value, &data->value);

		data->kind = AggregationExpressionKind_Constant;

		list_free_deep(arguments);

		return;
	}

	data->operator.arguments = arguments;
}


/* Computes result for $substrBytes operator. */
static void
ProcessDollarSubstrBytes(bson_value_t *firstValue,
						 bson_value_t *secondValue,
						 bson_value_t *thirdValue,
						 bson_value_t *result)
{
	if (!BsonValueIsNumber(secondValue))
	{
		ereport(ERROR, (errcode(MongoLocation16034), errmsg(
							"$substrBytes: starting index must be a numeric type (is BSON type %s)",
							BsonTypeName(secondValue->value_type))));
	}
	else if (!BsonValueIsNumber(thirdValue))
	{
		ereport(ERROR, (errcode(MongoLocation16035), errmsg(
							"$substrBytes: length must be a numeric type (is BSON type %s)",
							BsonTypeName(thirdValue->value_type))));
	}
	else if (IsExpressionResultNullOrUndefined(firstValue))
	{
		result->value_type = BSON_TYPE_UTF8;
		result->value.v_utf8.str = "";
		result->value.v_utf8.len = 0;

		return;
	}

	ProcessCommonBsonTypesForStringOperators(result, firstValue,
											 DateStringFormatCase_CamelCase);

	int64_t offset = BsonValueAsInt64WithRoundingMode(secondValue, 0, true);

	if (offset < 0)
	{
		ereport(ERROR, (errcode(MongoLocation50752), errmsg(
							"$substrBytes: starting index must be non-negative (got: %ld)",
							offset
							)));
	}

	int64_t remainingLength = result->value.v_utf8.len - offset;
	if (remainingLength <= 0)
	{
		result->value.v_utf8.str = "";
		result->value.v_utf8.len = 0;

		return;
	}

	char *offsetString = result->value.v_utf8.str + offset;
	if (IsUtf8ContinuationByte(offsetString))
	{
		ereport(ERROR, (errcode(MongoLocation28656), errmsg(
							"$substrBytes:  Invalid range, starting index is a UTF-8 continuation byte."
							)));
	}

	int64_t length = BsonValueAsInt64WithRoundingMode(thirdValue, 0, true);

	if (length < 0)
	{
		result->value.v_utf8.str = offsetString;
		result->value.v_utf8.len = remainingLength;

		return;
	}

	int substringLength = remainingLength > length ? length : remainingLength;

	char *offsetStringWithLength = offsetString + substringLength;
	if (IsUtf8ContinuationByte(offsetStringWithLength))
	{
		ereport(ERROR, (errcode(MongoLocation28657), errmsg(
							"$substrBytes: Invalid range, ending index is in the middle of a UTF-8 character."
							)));
	}

	result->value.v_utf8.str = offsetString;
	result->value.v_utf8.len = substringLength;
}


/* Computes result for $substrCP operator. */
static void
ProcessDollarSubstrCP(bson_value_t *firstValue,
					  bson_value_t *secondValue,
					  bson_value_t *thirdValue,
					  bson_value_t *result)
{
	bool checkFixedInteger = true;
	if (!BsonValueIsNumber(secondValue))
	{
		ereport(ERROR, (errcode(MongoLocation34450), errmsg(
							"$substrCP: starting index must be a numeric type (is BSON type %s)",
							BsonTypeName(secondValue->value_type))));
	}
	else if (!IsBsonValue32BitInteger(secondValue, checkFixedInteger))
	{
		ereport(ERROR, (errcode(MongoLocation34451), errmsg(
							"$substrCP: starting index cannot be represented as a 32-bit integral value")));
	}
	else if (!BsonValueIsNumber(thirdValue))
	{
		ereport(ERROR, (errcode(MongoLocation34452), errmsg(
							"$substrCP: length must be a numeric type (is BSON type %s)",
							BsonTypeName(thirdValue->value_type))));
	}
	else if (!IsBsonValue32BitInteger(thirdValue, checkFixedInteger))
	{
		ereport(ERROR, (errcode(MongoLocation34453), errmsg(
							"$substrCP: length cannot be represented as a 32-bit integral value")));
	}
	else if (IsExpressionResultNullOrUndefined(firstValue))
	{
		result->value_type = BSON_TYPE_UTF8;
		result->value.v_utf8.str = "";
		result->value.v_utf8.len = 0;

		return;
	}

	ProcessCommonBsonTypesForStringOperators(result, firstValue,
											 DateStringFormatCase_CamelCase);

	int offset = BsonValueAsInt32(secondValue);
	int length = BsonValueAsInt32(thirdValue);

	if (length < 0)
	{
		ereport(ERROR, (errcode(MongoLocation34454), errmsg(
							"$substrCP: length must be a nonnegative integer."
							)));
	}
	else if (offset < 0)
	{
		ereport(ERROR, (errcode(MongoLocation34455), errmsg(
							"$substrCP: the starting index must be nonnegative integer."
							)));
	}

	int64_t cpCount = Utf8CodePointCount(result);
	int64_t remainingCPCount = cpCount - offset;
	if (remainingCPCount <= 0)
	{
		result->value.v_utf8.str = "";
		result->value.v_utf8.len = 0;

		return;
	}

	while (offset > 0)
	{
		if (!IsUtf8ContinuationByte(result->value.v_utf8.str++))
		{
			offset--;
		}
		result->value.v_utf8.len--;
	}

	while (result->value.v_utf8.len > 0 && IsUtf8ContinuationByte(
			   result->value.v_utf8.str))
	{
		result->value.v_utf8.str++;
		result->value.v_utf8.len--;
	}

	if (length > remainingCPCount)
	{
		return;
	}

	char *string = result->value.v_utf8.str;
	int strLen = result->value.v_utf8.len;
	int numBytes = 0;

	while (length > 0 && strLen > 0)
	{
		if (!IsUtf8ContinuationByte(string++))
		{
			length--;
		}
		numBytes++;
		strLen--;
	}

	while (strLen > 0 && IsUtf8ContinuationByte(string))
	{
		string++;
		numBytes++;
		strLen--;
	}

	result->value.v_utf8.len = numBytes;
}


/*
 * This function facilitates the parsing of input for the $regexFind, $regexFindAll, and $regexMatch operators.
 * It stores the parsed input into their respective variables, validates the input, and throws errors if the input is deemed improper.
 * The function returns `true` if any input values are constant and set the isNullOrUndefinedInput variable to true if any input value is NULL or Undefined.
 */
static bool
ParseDollarRegexInput(const bson_value_t *operatorValue,
					  AggregationExpressionData *data,
					  bson_value_t *input,
					  RegexData *regexData,
					  const char *opName,
					  bool enableNoAutoCapture,
					  bool *isNullOrUndefinedInput,
					  ParseAggregationExpressionContext *context)
{
	bson_value_t regex = { 0 };
	bson_value_t options = { 0 };

	if (operatorValue->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(MongoLocation51103), errmsg(
							"%s expects an object of named arguments but found: %s",
							opName, BsonTypeName(operatorValue->value_type))));
	}

	bson_iter_t docIter;
	BsonValueInitIterator(operatorValue, &docIter);
	while (bson_iter_next(&docIter))
	{
		const char *key = bson_iter_key(&docIter);
		if (strcmp(key, "input") == 0)
		{
			*input = *bson_iter_value(&docIter);
		}
		else if (strcmp(key, "regex") == 0)
		{
			regex = *bson_iter_value(&docIter);
		}
		else if (strcmp(key, "options") == 0)
		{
			options = *bson_iter_value(&docIter);
		}
		else
		{
			ereport(ERROR, (errcode(MongoLocation31024), errmsg(
								"%s found an unknown argument: %s", opName, key)));
		}
	}

	if (input->value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(MongoLocation31022), errmsg(
							"%s requires 'input' parameter", opName)));
	}

	if (regex.value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(MongoLocation31023), errmsg(
							"%s requires 'regex' parameter", opName)));
	}


	DollarRegexArguments *regexArgs = palloc0(sizeof(DollarRegexArguments));
	ParseAggregationExpressionData(&regexArgs->input, input, context);
	ParseAggregationExpressionData(&regexArgs->regex, &regex, context);
	ParseAggregationExpressionData(&regexArgs->options, &options, context);

	if (IsAggregationExpressionConstant(&regexArgs->input) &&
		IsAggregationExpressionConstant(&regexArgs->regex) &&
		IsAggregationExpressionConstant(&regexArgs->options))
	{
		pfree(regexArgs);
		data->kind = AggregationExpressionKind_Constant;

		if (!ValidateEvaluatedRegexInput(input, &regex, &options, regexData, opName,
										 enableNoAutoCapture))
		{
			*isNullOrUndefinedInput = true;
		}

		return true;
	}
	else if (IsAggregationExpressionConstant(&regexArgs->regex) &&
			 IsAggregationExpressionConstant(&regexArgs->options))
	{
		if (ValidateEvaluatedRegexInput(input, &regex, &options, regexData, opName,
										enableNoAutoCapture))
		{
			regexArgs->pcreData = regexData->pcreData;
		}
	}

	data->operator.arguments = regexArgs;
	data->operator.argumentsKind = AggregationExpressionArgumentsKind_Palloc;

	return false;
}


/* Handles preparsed $replaceOne expression to calculate result. */
void
HandlePreParsedDollarReplaceOne(pgbson *doc, void *arguments,
								ExpressionResult *expressionResult)
{
	/* boolean to avoid strcmp on opName. Passing opName as well to avoid if-else cases in error messages. */
	bool isDollarReplaceOne = true;
	const char *opName = "$replaceOne";
	HandlePreParsedDollarReplaceHelper(doc, arguments, expressionResult, opName,
									   isDollarReplaceOne);
}


/* Handles preparsed $replaceAll expression to calculate result. */
void
HandlePreParsedDollarReplaceAll(pgbson *doc, void *arguments,
								ExpressionResult *expressionResult)
{
	/* boolean to avoid strcmp on opName. Passing opName as well to avoid if-else cases in error messages. */
	bool isDollarReplaceOne = false;
	const char *opName = "$replaceAll";
	HandlePreParsedDollarReplaceHelper(doc, arguments, expressionResult, opName,
									   isDollarReplaceOne);
}


/* Parses a $replaceOne expression and sets the parsed data in the data argument. */
void
ParseDollarReplaceOne(const bson_value_t *argument, AggregationExpressionData *data,
					  ParseAggregationExpressionContext *context)
{
	/* boolean to avoid strcmp on opName. Passing opName as well to avoid if-else cases in error messages. */
	bool isDollarReplaceOne = true;
	const char *opName = "$replaceOne";
	ParseDollarReplaceHelper(argument, data, opName, isDollarReplaceOne, context);
}


/* Parses a $replaceAll expression and sets the parsed data in the data argument. */
void
ParseDollarReplaceAll(const bson_value_t *argument, AggregationExpressionData *data,
					  ParseAggregationExpressionContext *context)
{
	/* boolean to avoid strcmp on opName. Passing opName as well to avoid if-else cases in error messages. */
	bool isDollarReplaceOne = false;
	const char *opName = "$replaceAll";
	ParseDollarReplaceHelper(argument, data, opName, isDollarReplaceOne, context);
}


/*
 * Validates the input arguments for `$regexFind`, `$regexMatch`, and `$regexFindAll` operators. If all the arguments are evaluated or constant.
 * The function fills the RegexData as well and returns `true` if the input argument is not null, otherwise `false`.
 * If "regexData->pcreData" is not NULL, the function assumes the regex and options are constant and already validated and compiled, we just validate the input and bail.
 * If function allocate memory for `regexArgs->pcreData` caller needs to free it.
 */
static bool
ValidateEvaluatedRegexInput(bson_value_t *input, bson_value_t *regex,
							bson_value_t *options, RegexData *regexData,
							const char *opName, bool enableNoAutoCapture)
{
	/* regexData->pcreData is not NULL, the function assumes the regex and options are constant and already validated and compiled */
	if (regexData->pcreData != NULL)
	{
		if (IsExpressionResultNullOrUndefined(input))
		{
			return false;
		}
		else if (input->value_type != BSON_TYPE_UTF8)
		{
			ereport(ERROR, (errcode(MongoLocation51104), errmsg(
								"%s needs 'input' to be of type string", opName)));
		}
		return true;
	}

	bool validInput = true;
	if (IsExpressionResultNullOrUndefined(regex))
	{
		if (options->value_type == BSON_TYPE_UTF8)
		{
			if (BsonValueStringHasNullCharcter(options))
			{
				ereport(ERROR, (errcode(MongoLocation51110), errmsg(
									"%s:  regular expression options cannot contain an embedded null byte",
									opName)));
			}

			if (!IsValidRegexOptions(options->value.v_utf8.str))
			{
				ereport(ERROR, (errcode(MongoLocation51108), errmsg(
									"%s invalid flag in regex options: %s", opName,
									options->value.v_utf8.str)));
			}
		}
		validInput = false;
	}
	else if (regex->value_type == BSON_TYPE_REGEX)
	{
		int typeRegexOptionLength = strlen(regex->value.v_regex.options);
		regexData->regex = regex->value.v_utf8.str;
		regexData->options = regex->value.v_regex.options;

		if (options->value_type != BSON_TYPE_EOD)
		{
			if (!IsExpressionResultNullOrUndefined(options) && options->value_type !=
				BSON_TYPE_UTF8)
			{
				ereport(ERROR, (errcode(MongoLocation51106), errmsg(
									"%s needs 'options' to be of type string", opName)));
			}

			if (typeRegexOptionLength > 0)
			{
				ereport(ERROR, (errcode(MongoLocation51107), errmsg(
									"%s found regex option(s) specified in both 'regex' and 'option' fields",
									opName)));
			}

			if (BsonValueStringHasNullCharcter(options))
			{
				ereport(ERROR, (errcode(MongoLocation51110), errmsg(
									"%s:  regular expression options cannot contain an embedded null byte",
									opName)));
			}

			if (!IsValidRegexOptions(options->value.v_utf8.str))
			{
				ereport(ERROR, (errcode(MongoLocation51108), errmsg(
									"%s invalid flag in regex options: %s", opName,
									options->value.v_utf8.str)));
			}

			regexData->options = options->value.v_utf8.str;
		}
		else if (typeRegexOptionLength > 0 && !IsValidRegexOptions(
					 regex->value.v_regex.options))
		{
			ereport(ERROR, (errcode(MongoLocation51108), errmsg(
								"%s invalid flag in regex options: %s", opName,
								regex->value.v_regex.options)));
		}
	}
	else if (regex->value_type == BSON_TYPE_UTF8)
	{
		if (BsonValueStringHasNullCharcter(regex))
		{
			ereport(ERROR, (errcode(MongoLocation51109), errmsg(
								"%s: regular expression cannot contain an embedded null byte",
								opName)));
		}

		if (!IsExpressionResultNullOrUndefined(options))
		{
			if (options->value_type != BSON_TYPE_UTF8)
			{
				ereport(ERROR, (errcode(MongoLocation51106), errmsg(
									"%s needs 'options' to be of type string", opName)));
			}

			if (BsonValueStringHasNullCharcter(options))
			{
				ereport(ERROR, (errcode(MongoLocation51110), errmsg(
									"%s:  regular expression options cannot contain an embedded null byte",
									opName)));
			}
		}

		if (!IsValidRegexOptions(options->value.v_utf8.str))
		{
			ereport(ERROR, (errcode(MongoLocation51108), errmsg(
								"%s invalid flag in regex options: %s", opName,
								options->value.v_utf8.str)));
		}

		regexData->regex = regex->value.v_utf8.str;
		regexData->options = options->value.v_utf8.str;
	}
	else
	{
		ereport(ERROR, (errcode(MongoLocation51105), errmsg(
							"%s needs 'regex' to be of type string or regex", opName)));
	}

	if (IsExpressionResultNullOrUndefined(input))
	{
		validInput = false;
	}
	else if (input->value_type != BSON_TYPE_UTF8)
	{
		ereport(ERROR, (errcode(MongoLocation51104), errmsg(
							"%s needs 'input' to be of type string", opName)));
	}

	if (validInput && regexData->regex)
	{
		char regexInvalidErrorMessage[40] = { 0 };
		Assert(strlen(opName) <= 20);
		sprintf(regexInvalidErrorMessage, "Invalid Regex in %s", opName);
		regexData->pcreData = RegexCompileForAggregation(regexData->regex,
														 regexData->options,
														 enableNoAutoCapture,
														 regexInvalidErrorMessage);
	}

	return validInput;
}


/*
 * The function constructs a BSON_TYPE_DOCUMENT as the result for the $regexFind and $regexFindAll operators and then returns it.
 */
static bson_value_t
ConstructResultForDollarRegex(RegexData *regexData, bson_value_t *input,
							  size_t *outputVector,
							  int outputLen, int *previousMatchCP)
{
	pgbson_writer resultWriter;
	PgbsonWriterInit(&resultWriter);

	size_t currMatchStartID = outputVector[0];
	size_t currMatchEndID = outputVector[1];

	if (outputLen >= 1)
	{
		size_t capture_length = currMatchEndID - currMatchStartID;
		char *capture_text = (char *) palloc(capture_length + 1);
		strncpy(capture_text, input->value.v_utf8.str + currMatchStartID,
				capture_length);
		capture_text[capture_length] = '\0';

		int currMatchStartCP = 0;
		int currMatchEndCP = 0;
		int currIterCP = -1;

		for (size_t currIndex = 0; currIndex < currMatchEndID; currIndex++)
		{
			if (IsUtf8ContinuationByte(input->value.v_utf8.str + currIndex))
			{
				continue;
			}

			currIterCP++;

			if (currIndex == currMatchStartID)
			{
				currMatchStartCP = currIterCP;
			}
		}

		/* if match found */
		if (currIterCP != -1)
		{
			currMatchEndCP = currIterCP;
		}

		PgbsonWriterAppendUtf8(&resultWriter, "match", 5, capture_text);
		PgbsonWriterAppendInt32(&resultWriter, "idx", 3, *previousMatchCP +
								currMatchStartCP);
		*previousMatchCP += currMatchEndCP + 1;
	}

	pgbson_array_writer arrayWriter;
	PgbsonWriterStartArray(&resultWriter, "captures", 8, &arrayWriter);
	for (int i = 2; i < outputLen * 2; i += 2)
	{
		currMatchStartID = outputVector[i];
		currMatchEndID = outputVector[i + 1];

		if (currMatchStartID == HELIO_PCRE2_INDEX_UNSET)
		{
			bson_value_t nullValue;
			nullValue.value_type = BSON_TYPE_NULL;
			PgbsonArrayWriterWriteValue(&arrayWriter, &nullValue);
		}
		else
		{
			size_t capture_length = currMatchEndID - currMatchStartID;
			char *capture_text = (char *) palloc(capture_length + 1);
			strncpy(capture_text, input->value.v_utf8.str + currMatchStartID,
					capture_length);
			capture_text[capture_length] = '\0';
			PgbsonArrayWriterWriteUtf8(&arrayWriter, capture_text);
		}
	}
	PgbsonWriterEndArray(&resultWriter, &arrayWriter);
	return ConvertPgbsonToBsonValue(PgbsonWriterGetPgbson(&resultWriter));
}


/*
 * $regexFindAll generates output in the following format: [{ "match": "", "idx": 0, "captures": [] }]
 * This function identifies the appropriate matches for $regexFindAll and adds each match's data as an array element, updating the final output accordingly.
 */
static void
WriteOutputOfDollarRegexFindAll(bson_value_t *input, RegexData *regexData,
								bson_value_t *result)
{
	int previousMatchId = 0;
	int previousMatchCP = 0;
	int initialLen = input->value.v_utf8.len;
	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	pgbson_array_writer arrayWriter;
	PgbsonWriterStartArray(&writer, "", 0, &arrayWriter);

	while (CompareRegexTextMatch(input, regexData))
	{
		size_t *outputVector = GetResultVectorUsingPcreData(regexData->pcreData);
		int outputLen = GetResultLengthUsingPcreData(regexData->pcreData);
		bson_value_t currentOut = ConstructResultForDollarRegex(regexData, input,
																outputVector,
																outputLen,
																&previousMatchCP);
		PgbsonArrayWriterWriteValue(&arrayWriter, &currentOut);

		if (outputVector[1] == 0)
		{
			previousMatchId++;
			input->value.v_utf8.str++;
			input->value.v_utf8.len--;
		}
		else
		{
			previousMatchId += outputVector[1];
			input->value.v_utf8.str += outputVector[1];
			input->value.v_utf8.len -= outputVector[1];
		}

		/* If the match index is less than or equal to the input length, we should break out of the loop.
		 * It is essential to break later in the loop because for the first iteration, if there is no match, we still want to write an empty output. */
		if (initialLen <= previousMatchId)
		{
			break;
		}

		if (PgbsonArrayWriterGetSize(&arrayWriter) > MAX_REGEX_OUTPUT_BUFFER_SIZE)
		{
			ereport(ERROR, (errcode(MongoLocation51151), errmsg(
								"$regexFindAll: the size of buffer to store output exceeded the 64MB limit")));
		}
	}

	PgbsonWriterEndArray(&writer, &arrayWriter);
	*result = PgbsonArrayWriterGetValue(&arrayWriter);
}


/* Helper method to parse (and calculate result if all arguments are constant)
 * $replaceOne and $replaceAll input. */
static void
ParseDollarReplaceHelper(const bson_value_t *argument,
						 AggregationExpressionData *data,
						 const char *opName,
						 bool isDollarReplaceOne,
						 ParseAggregationExpressionContext *context)
{
	if (argument->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(MongoLocation51751), errmsg(
							"%s requires an object as an argument, found: %s",
							opName,
							BsonTypeName(argument->value_type))));
	}

	data->operator.returnType = BSON_TYPE_UTF8;

	bson_iter_t docIter;
	BsonValueInitIterator(argument, &docIter);

	bson_value_t input = { 0 };
	bson_value_t find = { 0 };
	bson_value_t replacement = { 0 };

	while (bson_iter_next(&docIter))
	{
		const char *key = bson_iter_key(&docIter);
		if (strcmp(key, "input") == 0)
		{
			input = *bson_iter_value(&docIter);
		}
		else if (strcmp(key, "find") == 0)
		{
			find = *bson_iter_value(&docIter);
		}
		else if (strcmp(key, "replacement") == 0)
		{
			replacement = *bson_iter_value(&docIter);
		}
		else
		{
			ereport(ERROR, (errcode(MongoLocation51750), errmsg(
								"%s found an unknown argument: %s", opName, key)));
		}
	}

	if (input.value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(MongoLocation51749), errmsg(
							"%s requires 'input' to be specified", opName)));
	}
	else if (find.value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(MongoLocation51748), errmsg(
							"%s requires 'find' to be specified", opName)));
	}
	else if (replacement.value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(MongoLocation51747), errmsg(
							"%s requires 'replacement' to be specified", opName)));
	}

	DollarReplaceArguments *arguments = palloc0(sizeof(DollarReplaceArguments));

	ParseAggregationExpressionData(&arguments->input, &input, context);
	ParseAggregationExpressionData(&arguments->find, &find, context);
	ParseAggregationExpressionData(&arguments->replacement, &replacement, context);

	if (IsAggregationExpressionConstant(&arguments->input) &&
		IsAggregationExpressionConstant(&arguments->find) &&
		IsAggregationExpressionConstant(&arguments->replacement))
	{
		ProcessDollarReplace(&arguments->input.value,
							 &data->value,
							 &arguments->find.value,
							 &arguments->replacement.value,
							 opName,
							 isDollarReplaceOne);

		data->kind = AggregationExpressionKind_Constant;
		pfree(arguments);
		return;
	}

	data->operator.arguments = arguments;
	data->operator.argumentsKind = AggregationExpressionArgumentsKind_Palloc;
}


/* Common helper method to calculate result for preparsed $replaceOne and $replaceAll input. */
static void
HandlePreParsedDollarReplaceHelper(pgbson *doc, void *arguments,
								   ExpressionResult *expressionResult,
								   const char *opName,
								   bool isDollarReplaceOne)
{
	DollarReplaceArguments *replaceArguments = arguments;

	bool isNullOnEmpty = false;

	ExpressionResult childResult = ExpressionResultCreateChild(expressionResult);
	EvaluateAggregationExpressionData(&replaceArguments->input, doc,
									  &childResult, isNullOnEmpty);

	bson_value_t input = childResult.value;

	ExpressionResultReset(&childResult);
	EvaluateAggregationExpressionData(&replaceArguments->find, doc, &childResult,
									  isNullOnEmpty);

	bson_value_t find = childResult.value;

	ExpressionResultReset(&childResult);
	EvaluateAggregationExpressionData(&replaceArguments->replacement, doc, &childResult,
									  isNullOnEmpty);

	bson_value_t replacement = childResult.value;

	bson_value_t result;
	ProcessDollarReplace(&input, &result, &find, &replacement, opName,
						 isDollarReplaceOne);

	ExpressionResultSetValue(expressionResult, &result);
}


/* Computes result for $replaceOne/$replaceAll operator. */
static void
ProcessDollarReplace(bson_value_t *input,
					 bson_value_t *result,
					 bson_value_t *find,
					 bson_value_t *replacement,
					 const char *opName,
					 bool isDollarReplaceOne)
{
	ValidateParsedInputForDollarReplace(input, result, find, replacement, opName);

	if (result->value_type == BSON_TYPE_NULL)
	{
		return;
	}

	int substrPosition = 0;
	if (isDollarReplaceOne)
	{
		substrPosition = GetSubstringPosition(input->value.v_utf8.str,
											  find->value.v_utf8.str,
											  input->value.v_utf8.len,
											  find->value.v_utf8.len,
											  substrPosition);

		if (substrPosition == -1)
		{
			return;
		}

		ReplaceSubstring(result, find, replacement, substrPosition);
	}
	else
	{
		while ((uint32) substrPosition <= result->value.v_utf8.len -
			   find->value.v_utf8.len)
		{
			substrPosition = GetSubstringPosition(result->value.v_utf8.str,
												  find->value.v_utf8.str,
												  result->value.v_utf8.len,
												  find->value.v_utf8.len,
												  substrPosition);

			if (substrPosition == -1)
			{
				return;
			}

			ReplaceSubstring(result, find, replacement, substrPosition);

			/* This is to handle case when find is empty string. */
			if (find->value.v_utf8.len == 0)
			{
				substrPosition += replacement->value.v_utf8.len + 1;
			}
			else
			{
				substrPosition += replacement->value.v_utf8.len;
			}
		}
	}
}


/* Validate input data type for $replaceOne and $replaceAll. */
static void
ValidateParsedInputForDollarReplace(bson_value_t *input,
									bson_value_t *result,
									bson_value_t *find,
									bson_value_t *replacement,
									const char *opName)
{
	/* native errors out if any of the 3 arguments is neither UTF8 nor null,
	 * even if the other values are null. */
	if (input->value_type != BSON_TYPE_UTF8 &&
		!IsExpressionResultNullOrUndefined(input))
	{
		ereport(ERROR, (errcode(MongoLocation51746), errmsg(
							"%s requires that 'input' be a string, found: %s",
							opName,
							(char *) BsonValueToJsonForLogging(input)), errhint(
							"%s requires that 'input' be a string, found of type %s",
							opName,
							BsonTypeName(input->value_type))));
	}
	else if (find->value_type != BSON_TYPE_UTF8 &&
			 !IsExpressionResultNullOrUndefined(find))
	{
		ereport(ERROR, (errcode(MongoLocation51745), errmsg(
							"%s requires that 'find' be a string, found: %s",
							opName,
							(char *) BsonValueToJsonForLogging(find)), errhint(
							"%s requires that 'find' be a string, found of type %s",
							opName,
							BsonTypeName(find->value_type))));
	}
	else if (replacement->value_type != BSON_TYPE_UTF8 &&
			 !IsExpressionResultNullOrUndefined(replacement))
	{
		ereport(ERROR, (errcode(MongoLocation51744), errmsg(
							"%s requires that 'replacement' be a string, found: %s",
							opName,
							(char *) BsonValueToJsonForLogging(replacement)), errhint(
							"%s requires that 'replacement' be a string, found of type %s",
							opName,
							BsonTypeName(replacement->value_type))));
	}

	if (IsExpressionResultNullOrUndefined(input) ||
		IsExpressionResultNullOrUndefined(find) ||
		IsExpressionResultNullOrUndefined(replacement))
	{
		result->value_type = BSON_TYPE_NULL;
		return;
	}

	result->value_type = BSON_TYPE_UTF8;
	result->value.v_utf8.str = palloc(input->value.v_utf8.len);
	memcpy(result->value.v_utf8.str, input->value.v_utf8.str, input->value.v_utf8.len);
	result->value.v_utf8.len = input->value.v_utf8.len;
}


/*
 * The functions returns true if input UTF8 bson_value has null character otherwise returns false
 * Input bson_value is expected to be of type BSON_TYPE_UTF8.
 */
static inline bool
BsonValueStringHasNullCharcter(const bson_value_t *element)
{
	Assert(element->value_type == BSON_TYPE_UTF8);
	for (size_t i = 0; i < element->value.v_utf8.len; i++)
	{
		if (element->value.v_utf8.str[i] == '\0')
		{
			return true;
		}
	}
	return false;
}


/* Gets the position of the first occurrence of a susbstring in a string.
 * Returns the starting index of the substring in the string; -1 if substring is not found. */
static inline int
GetSubstringPosition(char *str, char *subStr, int strLen,
					 int subStrLen, int lastPos)
{
	int maxIndex = strLen - subStrLen;
	for (int i = lastPos; i <= maxIndex; i++)
	{
		if (memcmp(str + i, subStr, subStrLen) == 0)
		{
			return i;
		}
	}

	return -1;
}


/* Replaces "find" string with "replacement" string in the result for $replaceOne and $replaceAll. */
static void
ReplaceSubstring(bson_value_t *result, bson_value_t *find,
				 bson_value_t *replacement, int substrPosition)
{
	/* Assigning to variables to better readability. */
	uint32_t replacementStringLength = replacement->value.v_utf8.len;
	uint32_t findStringLength = find->value.v_utf8.len;
	int initialLength = result->value.v_utf8.len;
	int finalLength = result->value.v_utf8.len - findStringLength +
					  replacementStringLength;
	int remainingLength = result->value.v_utf8.len - substrPosition -
						  findStringLength;
	char *resultString = result->value.v_utf8.str;
	char *replacementString = replacement->value.v_utf8.str;

	if (findStringLength > replacementStringLength)
	{
		memcpy(resultString + substrPosition, replacementString,
			   replacementStringLength);
		memmove(resultString + substrPosition + replacementStringLength,
				resultString + substrPosition + findStringLength,
				remainingLength);
	}
	else if (findStringLength < replacementStringLength)
	{
		resultString = repalloc(resultString, finalLength);
		memset(resultString + initialLength, 0, (finalLength - initialLength));
		memmove(resultString + substrPosition + replacementStringLength,
				resultString + substrPosition + findStringLength,
				remainingLength);
		memcpy(resultString + substrPosition, replacementString,
			   replacementStringLength);
	}
	else
	{
		memcpy(resultString + substrPosition, replacementString,
			   replacementStringLength);
	}

	result->value.v_utf8.str = resultString;
	result->value.v_utf8.len = finalLength;
}
