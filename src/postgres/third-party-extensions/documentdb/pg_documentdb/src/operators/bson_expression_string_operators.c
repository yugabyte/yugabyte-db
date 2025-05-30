/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/operators/bson_expression_string_operators.c
 *
 * String Operator expression implementations of BSON.
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>

#include "io/bson_core.h"
#include "query/bson_compare.h"
#include "operators/bson_expression.h"
#include "operators/bson_expression_operators.h"
#include "utils/documentdb_errors.h"
#include "utils/string_view.h"
#include "utils/hashset_utils.h"
#include "types/pcre_regex.h"
#include "query/bson_dollar_operators.h"

#define PCRE2_INDEX_UNSET (~(size_t) 0)

#define MAX_REGEX_OUTPUT_BUFFER_SIZE (64 * 1024 * 1024)

/* --------------------------------------------------------- */
/* Type definitions */
/* --------------------------------------------------------- */
typedef void (*ProcessStringOperatorOneOperand)(const bson_value_t *currentValue,
												bson_value_t *result);
typedef void (*ProcessStringOperatorTwoOperands)(void *state, bson_value_t *result);
typedef void (*ProcessStringOperatorThreeOperands)(void *state, bson_value_t *result);
typedef bool (*ProcessStringOperatorVariableOperands)(const bson_value_t *currentValue,
													  void *state, bson_value_t *result);

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
static void ParseStringOperatorOneOperand(const bson_value_t *argument,
										  AggregationExpressionData *data, const
										  char *operatorName,
										  ProcessStringOperatorOneOperand
										  processOperatorFunc,
										  ParseAggregationExpressionContext *context);
static void ParseStringOperatorTwoOperands(const bson_value_t *argument,
										   AggregationExpressionData *data, const
										   char *operatorName,
										   ProcessStringOperatorTwoOperands
										   processOperatorFunc,
										   ParseAggregationExpressionContext *context);
static void ParseStringOperatorThreeOperands(const bson_value_t *argument,
											 AggregationExpressionData *data, const
											 char *operatorName,
											 ProcessStringOperatorThreeOperands
											 processOperatorFunc,
											 ParseAggregationExpressionContext *
											 context);
static void ParseStringOperatorVariableOperands(const bson_value_t *argument,
												void *state,
												AggregationExpressionData *data,
												ProcessStringOperatorVariableOperands
												processOperatorFunc,
												ParseAggregationExpressionContext *context);
static void ParseDollarIndexOfBytesAndIndexOfCP(const bson_value_t *argument,
												AggregationExpressionData *data, const
												char *operatorName,
												bool isIndexOfCP,
												ParseAggregationExpressionContext *
												context);
static void HandlePreParsedStringOperatorcOneOperand(pgbson *doc, void *arguments,
													 ExpressionResult *expressionResult,
													 ProcessStringOperatorOneOperand
													 processOperatorFunc);
static void HandlePreParsedStringOperatorTwoOperands(pgbson *doc, void *arguments,
													 ExpressionResult *expressionResult,
													 ProcessStringOperatorTwoOperands
													 processOperatorFunc);
static void HandlePreParsedStringOperatorThreeOperands(pgbson *doc, void *arguments,
													   ExpressionResult *expressionResult,
													   ProcessStringOperatorThreeOperands
													   processOperatorFunc);
static void HandlePreParsedIndexOfBytesAndIndexOfCP(pgbson *doc, void *arguments,
													ExpressionResult *expressionResult,
													bool isIndexOfCP);
static void HandlePreParsedStringOperatorcVariableOperands(pgbson *doc, void *arguments,
														   void *state,
														   bson_value_t *result,
														   ExpressionResult *
														   expressionResult,
														   ProcessStringOperatorVariableOperands
														   processOperatorFunc);

static void ParseDollarTrimCore(const bson_value_t *argument,
								AggregationExpressionData *data, const char *opName,
								ParseAggregationExpressionContext *context);
static bool ParseDollarRegexInput(const bson_value_t *operatorValue,
								  AggregationExpressionData *data,
								  bson_value_t *input,
								  RegexData *regexData,
								  const char *opName,
								  bool enableNoAutoCapture,
								  bool *isNullOrUndefinedInput,
								  ParseAggregationExpressionContext *context);
static void HandlePreParsedDollarTrimCore(pgbson *doc, void *arguments, const
										  char *opName,
										  ExpressionResult *expressionResult);

static bool ProcessDollarConcatElement(const bson_value_t *currentValue, void *state,
									   bson_value_t *result);
static void ProcessDollarToUpper(const bson_value_t *currentValue, bson_value_t *result);
static void ProcessDollarToLower(const bson_value_t *currentValue, bson_value_t *result);
static void ProcessDollarStrLenBytes(const bson_value_t *currentValue,
									 bson_value_t *result);
static void ProcessDollarStrLenCP(const bson_value_t *currentValue,
								  bson_value_t *result);
static void ProcessDollarSplit(void *state, bson_value_t *result);
static void ProcessDollarConcatResult(void *state, bson_value_t *result);
static void ProcessDollarIndexOfBytes(void *state, bson_value_t *result);
static void ProcessDollarIndexOfCP(void *state, bson_value_t *result);
static void ProcessDollarIndexOfCore(FourArgumentExpressionState *input,
									 bool isIndexOfBytesOp, int *startIndex,
									 int *endIndex);
static void ProcessDollarStrCaseCmp(void *state, bson_value_t *result);
static void ProcessCoersionForStrCaseCmp(bson_value_t *element);
static void ProcessDollarReplace(bson_value_t *input,
								 bson_value_t *result,
								 bson_value_t *find,
								 bson_value_t *replacement,
								 const char *opName,
								 bool isDollarReplaceOne);
static void ProcessDollarSubstrCP(void *state, bson_value_t *result);
static void ProcessDollarSubstrBytes(void *state, bson_value_t *result);
static void ProcessDollarTrim(const bson_value_t *inputValue, const
							  bson_value_t *charsValue, bson_value_t *result, const bool
							  providedChars, const
							  char *opName);

static void WriteDollarTrimResult(const bson_value_t *input, bson_value_t *result,
								  int startIndex, int endIndex);
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
													 const bson_value_t *currentValue,
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


/* --------------------------------------------------------- */
/* Parse and Handle Pre-parse functions */
/* --------------------------------------------------------- */

/*
 * Parses a $toUpper expression.
 * $toUpper is expressed as { "$toUpper": <expression> }
 */
void
ParseDollarToUpper(const bson_value_t *argument, AggregationExpressionData *data,
				   ParseAggregationExpressionContext *context)
{
	ParseStringOperatorOneOperand(argument, data, "$toUpper", ProcessDollarToUpper,
								  context);
}


/*
 * Handles executing a pre-parsed $toUpper expression.
 */
void
HandlePreParsedDollarToUpper(pgbson *doc, void *arguments,
							 ExpressionResult *expressionResult)
{
	HandlePreParsedStringOperatorcOneOperand(doc, arguments, expressionResult,
											 ProcessDollarToUpper);
}


/*
 * Parses a $toLower expression.
 * $toLower is expressed as { "$toLower": <expression> }
 */
void
ParseDollarToLower(const bson_value_t *argument, AggregationExpressionData *data,
				   ParseAggregationExpressionContext *context)
{
	ParseStringOperatorOneOperand(argument, data, "$toLower", ProcessDollarToLower,
								  context);
}


/*
 * Handles executing a pre-parsed $toLower expression.
 */
void
HandlePreParsedDollarToLower(pgbson *doc, void *arguments,
							 ExpressionResult *expressionResult)
{
	HandlePreParsedStringOperatorcOneOperand(doc, arguments, expressionResult,
											 ProcessDollarToLower);
}


/*
 * Parses a $strLenBytes expression.
 * $strLenBytes is expressed as { "$strLenBytes": <expression> }
 */
void
ParseDollarStrLenBytes(const bson_value_t *argument, AggregationExpressionData *data,
					   ParseAggregationExpressionContext *context)
{
	ParseStringOperatorOneOperand(argument, data, "$strLenBytes",
								  ProcessDollarStrLenBytes, context);
}


/*
 * Handles executing a pre-parsed $strLenBytes expression.
 */
void
HandlePreParsedDollarStrLenBytes(pgbson *doc, void *arguments,
								 ExpressionResult *expressionResult)
{
	HandlePreParsedStringOperatorcOneOperand(doc, arguments, expressionResult,
											 ProcessDollarStrLenBytes);
}


/*
 * Parses a $strLenCP expression.
 * $strLenCP is expressed as { "$strLenCP": <expression> }
 */
void
ParseDollarStrLenCP(const bson_value_t *argument, AggregationExpressionData *data,
					ParseAggregationExpressionContext *context)
{
	ParseStringOperatorOneOperand(argument, data, "$strLenCP",
								  ProcessDollarStrLenCP, context);
}


/*
 * Handles executing a pre-parsed $strLenCP expression.
 */
void
HandlePreParsedDollarStrLenCP(pgbson *doc, void *arguments,
							  ExpressionResult *expressionResult)
{
	HandlePreParsedStringOperatorcOneOperand(doc, arguments, expressionResult,
											 ProcessDollarStrLenCP);
}


/*
 * Parses a $split expression.
 * $split is expressed as { "$split": [ <string>, <delimiter> ] }
 */
void
ParseDollarSplit(const bson_value_t *argument, AggregationExpressionData *data,
				 ParseAggregationExpressionContext *context)
{
	ParseStringOperatorTwoOperands(argument, data, "$split",
								   ProcessDollarSplit, context);
}


/*
 * Handles executing a pre-parsed $split expression.
 */
void
HandlePreParsedDollarSplit(pgbson *doc, void *arguments,
						   ExpressionResult *expressionResult)
{
	HandlePreParsedStringOperatorTwoOperands(doc, arguments, expressionResult,
											 ProcessDollarSplit);
}


/*
 * Parses a $strCaseCmp expression.
 * $strCaseCmp is expressed as { "$strCaseCmp": [ <string1>, <string2> ] }
 */
void
ParseDollarStrCaseCmp(const bson_value_t *argument, AggregationExpressionData *data,
					  ParseAggregationExpressionContext *context)
{
	ParseStringOperatorTwoOperands(argument, data, "$strCaseCmp",
								   ProcessDollarStrCaseCmp, context);
}


/*
 * Handles executing a pre-parsed $strCaseCmp expression.
 */
void
HandlePreParsedDollarStrCaseCmp(pgbson *doc, void *arguments,
								ExpressionResult *expressionResult)
{
	HandlePreParsedStringOperatorTwoOperands(doc, arguments, expressionResult,
											 ProcessDollarStrCaseCmp);
}


/*
 * Parses an $concat expression.
 * $concat is expressed as { "$concat": [ "string1", "string2" ,... ] }
 */
void
ParseDollarConcat(const bson_value_t *argument, AggregationExpressionData *data,
				  ParseAggregationExpressionContext *context)
{
	DollarConcatOperatorState state =
	{
		.stringList = NULL,
		.totalSize = 0,
	};

	data->value.value_type = BSON_TYPE_UTF8;
	ParseStringOperatorVariableOperands(argument, &state, data,
										ProcessDollarConcatElement,
										context);

	if (data->kind == AggregationExpressionKind_Constant)
	{
		ProcessDollarConcatResult(&state, &data->value);
	}
}


/*
 * Handles executing a pre-parsed $concat expression.
 */
void
HandlePreParsedDollarConcat(pgbson *doc, void *arguments,
							ExpressionResult *expressionResult)
{
	DollarConcatOperatorState state =
	{
		.stringList = NIL,
		.totalSize = 0,
	};

	bson_value_t result = { .value_type = BSON_TYPE_UTF8 };
	HandlePreParsedStringOperatorcVariableOperands(doc, arguments, &state, &result,
												   expressionResult,
												   ProcessDollarConcatElement);

	if (result.value_type != BSON_TYPE_NULL)
	{
		ProcessDollarConcatResult(&state, &result);
	}

	ExpressionResultSetValue(expressionResult, &result);
}


/*
 * Parses an $substrBytes expression.
 * $substrBytes is expressed as { "$substrBytes": [<string>, <offset>, <length>] }
 */
void
ParseDollarSubstrBytes(const bson_value_t *argument, AggregationExpressionData *data,
					   ParseAggregationExpressionContext *context)
{
	ParseStringOperatorThreeOperands(argument, data, "$substrBytes",
									 ProcessDollarSubstrBytes, context);
}


/*
 * Handles executing a pre-parsed $substr/$substrBytes expression.
 */
void
HandlePreParsedDollarSubstrBytes(pgbson *doc, void *arguments,
								 ExpressionResult *expressionResult)
{
	HandlePreParsedStringOperatorThreeOperands(doc, arguments, expressionResult,
											   ProcessDollarSubstrBytes);
}


/*
 * Parses an $substrCP expression.
 * $substrCP is expressed as { "$substrCP": [<string>, <offset>, <length>] }
 */
void
ParseDollarSubstrCP(const bson_value_t *argument, AggregationExpressionData *data,
					ParseAggregationExpressionContext *context)
{
	ParseStringOperatorThreeOperands(argument, data, "$substrCP",
									 ProcessDollarSubstrCP, context);
}


/*
 * Handles executing a pre-parsed $substrCP expression.
 */
void
HandlePreParsedDollarSubstrCP(pgbson *doc, void *arguments,
							  ExpressionResult *expressionResult)
{
	HandlePreParsedStringOperatorThreeOperands(doc, arguments, expressionResult,
											   ProcessDollarSubstrCP);
}


/*
 * Parses a $trim expression.
 * $trim is expressed as { "$trim": { input: <string>, chars: <string> } }
 * chars is optional and if not provided, it defaults to whitespace characters.
 */
void
ParseDollarTrim(const bson_value_t *argument, AggregationExpressionData *data,
				ParseAggregationExpressionContext *context)
{
	ParseDollarTrimCore(argument, data, "$trim", context);
}


/*
 * Handles executing a pre-parsed $trim expression.
 */
void
HandlePreParsedDollarTrim(pgbson *doc, void *arguments,
						  ExpressionResult *expressionResult)
{
	HandlePreParsedDollarTrimCore(doc, arguments, "$trim", expressionResult);
}


/*
 * Parses a $ltrim expression.
 * $ltrim is expressed as { "$ltrim": { input: <string>, chars: <string> } }
 * chars is optional and if not provided, it defaults to whitespace characters.
 */
void
ParseDollarLtrim(const bson_value_t *argument, AggregationExpressionData *data,
				 ParseAggregationExpressionContext *context)
{
	ParseDollarTrimCore(argument, data, "$ltrim", context);
}


/*
 * Handles executing a pre-parsed $ltrim expression.
 */
void
HandlePreParsedDollarLtrim(pgbson *doc, void *arguments,
						   ExpressionResult *expressionResult)
{
	HandlePreParsedDollarTrimCore(doc, arguments, "$ltrim", expressionResult);
}


/*
 * Parses a $rtrim expression.
 * $rtrim is expressed as { "$rtrim": { input: <string>, chars: <string> } }
 * chars is optional and if not provided, it defaults to whitespace characters.
 */
void
ParseDollarRtrim(const bson_value_t *argument, AggregationExpressionData *data,
				 ParseAggregationExpressionContext *context)
{
	ParseDollarTrimCore(argument, data, "$rtrim", context);
}


/*
 * Handles executing a pre-parsed $rtrim expression.
 */
void
HandlePreParsedDollarRtrim(pgbson *doc, void *arguments,
						   ExpressionResult *expressionResult)
{
	HandlePreParsedDollarTrimCore(doc, arguments, "$rtrim", expressionResult);
}


/*
 * Parses a $indexOfBytes expression.
 * $indexOfBytes is expressed as { "$indexOfBytes": [ <string>,<substring>,<startIndex>,<endIndex> ] }
 */
void
ParseDollarIndexOfBytes(const bson_value_t *argument, AggregationExpressionData *data,
						ParseAggregationExpressionContext *context)
{
	ParseDollarIndexOfBytesAndIndexOfCP(argument, data, "$indexOfBytes",
										false, context);
}


/*
 * Handles executing a pre-parsed $indexOfBytes expression.
 */
void
HandlePreParsedDollarIndexOfBytes(pgbson *doc, void *arguments,
								  ExpressionResult *expressionResult)
{
	HandlePreParsedIndexOfBytesAndIndexOfCP(doc, arguments, expressionResult,
											false);
}


/*
 * Parses a $indexOfCP expression.
 * $indexOfCP is expressed as { "$indexOfCP": [ <string>,<substring>,<startIndex>,<endIndex> ] }
 */
void
ParseDollarIndexOfCP(const bson_value_t *argument, AggregationExpressionData *data,
					 ParseAggregationExpressionContext *context)
{
	ParseDollarIndexOfBytesAndIndexOfCP(argument, data, "$indexOfCP",
										true, context);
}


/*
 * Handles executing a pre-parsed $indexOfCP expression.
 */
void
HandlePreParsedDollarIndexOfCP(pgbson *doc, void *arguments,
							   ExpressionResult *expressionResult)
{
	HandlePreParsedIndexOfBytesAndIndexOfCP(doc, arguments, expressionResult,
											true);
}


/*
 * Parses an $regexFind expression.
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
 * Handles executing a pre-parsed $regexFind expression.
 * $regexFind is expressed as { "$regexFind": {"input" :<string>, "regex" : <regex pattern>, "options": <regexOption>}}
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
 * Parses an $regexMatch expression.
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
 * Handles executing a pre-parsed $regexMatch expression.
 * $regexMatch is expressed as { "$regexMatch": {"input" :<string>, "regex" : <regex pattern>, "options": <regexOption>}}
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
 * Parses an $regexFindAll expression.
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
 * Handles executing a pre-parsed $regexFindAll expression.
 * $regexFindAll is expressed as { "$regexFindAll": {"input" :<string>, "regex" : <regex pattern>, "options": <regexOption>}}
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


/* --------------------------------------------------------- */
/* Parse and Handle Pre-parse helper functions */
/* --------------------------------------------------------- */

/* Helper to parse arithmetic operators that take strictly 1 operand. */
static void
ParseStringOperatorOneOperand(const bson_value_t *argument,
							  AggregationExpressionData *data, const
							  char *operatorName,
							  ProcessStringOperatorOneOperand
							  processOperatorFunc,
							  ParseAggregationExpressionContext *context)
{
	int numOfRequiredArgs = 1;
	AggregationExpressionData *parsedData = ParseFixedArgumentsForExpression(argument,
																			 numOfRequiredArgs,
																			 operatorName,
																			 &data->
																			 operator.
																			 argumentsKind,
																			 context);

	/* If the arguments is constant: compute comparison result, change
	 * expression type to constant, store the result in the expression value
	 * and free the arguments list as it won't be needed anymore. */
	if (IsAggregationExpressionConstant(parsedData))
	{
		processOperatorFunc(&parsedData->value, &data->value);

		data->kind = AggregationExpressionKind_Constant;
		pfree(parsedData);
	}
	else
	{
		data->operator.arguments = parsedData;
	}
}


/* Helper to evaluate pre-parsed expressions of operators that take strictly 1 operand. */
static void
HandlePreParsedStringOperatorcOneOperand(pgbson *doc, void *arguments,
										 ExpressionResult *expressionResult,
										 ProcessStringOperatorOneOperand
										 processOperatorFunc)
{
	AggregationExpressionData *argument = (AggregationExpressionData *) arguments;

	bool isNullOnEmpty = false;
	ExpressionResult childResult = ExpressionResultCreateChild(expressionResult);
	EvaluateAggregationExpressionData(argument, doc, &childResult, isNullOnEmpty);

	bson_value_t currentValue = childResult.value;

	bson_value_t result = { .value_type = BSON_TYPE_UTF8 };
	processOperatorFunc(&currentValue, &result);
	ExpressionResultSetValue(expressionResult, &result);
}


/* Helper to parse string operators that take strictly two arguments. */
static void
ParseStringOperatorTwoOperands(const bson_value_t *argument,
							   AggregationExpressionData *data, const
							   char *operatorName,
							   ProcessStringOperatorTwoOperands
							   processOperatorFunc,
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
		processOperatorFunc(&state, &data->value);

		data->kind = AggregationExpressionKind_Constant;
		list_free_deep(arguments);
	}
	else
	{
		data->operator.arguments = arguments;
	}
}


/* Helper to evaluate pre-parsed expressions of string operators that take strictly two operands. */
static void
HandlePreParsedStringOperatorTwoOperands(pgbson *doc, void *arguments,
										 ExpressionResult *expressionResult,
										 ProcessStringOperatorTwoOperands
										 processOperatorFunc)
{
	List *argumentList = (List *) arguments;
	AggregationExpressionData *firstArg = list_nth(argumentList, 0);
	AggregationExpressionData *secondArg = list_nth(argumentList, 1);

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

	bson_value_t result = { 0 };
	DualArgumentExpressionState state;
	memset(&state, 0, sizeof(DualArgumentExpressionState));

	InitializeDualArgumentExpressionState(firstValue, secondValue, hasFieldExpression,
										  &state);
	processOperatorFunc(&state, &result);

	ExpressionResultSetValue(expressionResult, &result);
}


/* Helper to parse arithmetic operators that take exactly three operands. */
void
ParseStringOperatorThreeOperands(const bson_value_t *argument,
								 AggregationExpressionData *data,
								 const char *operatorName,
								 ProcessStringOperatorThreeOperands
								 processOperatorFunc,
								 ParseAggregationExpressionContext *context)
{
	int numOfRequiredArgs = 3;
	List *arguments = ParseFixedArgumentsForExpression(argument,
													   numOfRequiredArgs,
													   operatorName,
													   &data->operator.argumentsKind,
													   context);

	AggregationExpressionData *first = list_nth(arguments, 0);
	AggregationExpressionData *second = list_nth(arguments, 1);
	AggregationExpressionData *third = list_nth(arguments, 2);

	if (IsAggregationExpressionConstant(first) &&
		IsAggregationExpressionConstant(second) &&
		IsAggregationExpressionConstant(third))
	{
		ThreeArgumentExpressionState state = {
			.firstArgument = first->value,
			.secondArgument = second->value,
			.thirdArgument = third->value
		};

		processOperatorFunc(&state, &data->value);

		data->kind = AggregationExpressionKind_Constant;
		list_free_deep(arguments);

		return;
	}
	else
	{
		data->operator.arguments = arguments;
	}
}


/* Helper to evaluate pre-parsed expressions of operators that take exactly three operands. */
static void
HandlePreParsedStringOperatorThreeOperands(pgbson *doc, void *arguments,
										   ExpressionResult *expressionResult,
										   ProcessStringOperatorThreeOperands
										   processOperatorFunc)
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
	ThreeArgumentExpressionState state = {
		.firstArgument = firstValue,
		.secondArgument = secondValue,
		.thirdArgument = thirdValue
	};
	processOperatorFunc(&state, &result);

	ExpressionResultSetValue(expressionResult, &result);
}


/* Helper to parse arithmetic operators that take variable number of operands. */
void
ParseStringOperatorVariableOperands(const bson_value_t *argument,
									void *state,
									AggregationExpressionData *data,
									ProcessStringOperatorVariableOperands
									processOperatorFunc,
									ParseAggregationExpressionContext *context)
{
	bool areArgumentsConstant;
	List *argumentsList = ParseVariableArgumentsForExpression(argument,
															  &areArgumentsConstant,
															  context);

	if (areArgumentsConstant)
	{
		int idx = 0;
		while (argumentsList != NIL && idx < argumentsList->length)
		{
			AggregationExpressionData *currentData = list_nth(argumentsList, idx);

			bool continueEnumerating = processOperatorFunc(&currentData->value,
														   state,
														   &data->value);
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
		data->operator.arguments = argumentsList;
	}
}


/* Helper to evaluate pre-parsed expressions of operators that take variable number of operands. */
static void
HandlePreParsedStringOperatorcVariableOperands(pgbson *doc, void *arguments, void *state,
											   bson_value_t *result,
											   ExpressionResult *expressionResult,
											   ProcessStringOperatorVariableOperands
											   processOperatorFunc)
{
	List *argumentList = (List *) arguments;

	int idx = 0;
	while (argumentList != NIL && idx < argumentList->length)
	{
		AggregationExpressionData *currentData = list_nth(argumentList, idx);

		bool isNullOnEmpty = false;
		ExpressionResult childResult = ExpressionResultCreateChild(expressionResult);
		EvaluateAggregationExpressionData(currentData, doc, &childResult, isNullOnEmpty);

		bson_value_t currentValue = childResult.value;

		bool continueEnumerating = processOperatorFunc(&currentValue, state, result);
		if (!continueEnumerating)
		{
			return;
		}

		idx++;
	}
}


/*
 * Helper to evaluate pre-parsed expressions of $indexOfBytes and $indexOfCP.
 */
static void
ParseDollarIndexOfBytesAndIndexOfCP(const bson_value_t *argument,
									AggregationExpressionData *data,
									const char *operatorName,
									bool isIndexOfCP,
									ParseAggregationExpressionContext *context)
{
	int minRequiredArgs = 2;
	int maxRequiredArgs = 4;

	AggregationExpressionArgumentsKind argumentsKind;
	List *arguments = ParseRangeArgumentsForExpression(argument,
													   minRequiredArgs,
													   maxRequiredArgs,
													   operatorName,
													   &argumentsKind,
													   context);

	/* Creating a constant argument list of size 4. */
	int numArgs = arguments->length;
	while (arguments->length < maxRequiredArgs)
	{
		arguments = lappend(arguments, NULL);
	}


	AggregationExpressionData *firstArg = list_nth(arguments, 0);
	AggregationExpressionData *secondArg = list_nth(arguments, 1);
	AggregationExpressionData *thirdArg = list_nth(arguments, 2);
	AggregationExpressionData *fourthArg = list_nth(arguments, 3);

	bool evaluatedOnConstants = false;
	if (IsAggregationExpressionConstant(firstArg) && IsAggregationExpressionConstant(
			secondArg))
	{
		bool hasNullOrUndefined = IsExpressionResultNullOrUndefined(&firstArg->value) ||
								  IsExpressionResultNullOrUndefined(&secondArg->value);

		bool allArgumentsConstant = numArgs == 2;
		FourArgumentExpressionState state = {
			.firstArgument = firstArg->value,
			.secondArgument = secondArg->value
		};

		if (numArgs == 3 && IsAggregationExpressionConstant(thirdArg))
		{
			state.thirdArgument = thirdArg->value;
			allArgumentsConstant = true;

			hasNullOrUndefined = hasNullOrUndefined || IsExpressionResultNullOrUndefined(
				&thirdArg->value);
		}
		else if (numArgs == 4 &&
				 IsAggregationExpressionConstant(thirdArg) &&
				 IsAggregationExpressionConstant(fourthArg))
		{
			state.thirdArgument = thirdArg->value;
			state.fourthArgument = fourthArg->value;
			allArgumentsConstant = true;

			hasNullOrUndefined = hasNullOrUndefined || IsExpressionResultNullOrUndefined(
				&thirdArg->value) ||
								 IsExpressionResultNullOrUndefined(&fourthArg->value);
		}

		if (allArgumentsConstant)
		{
			state.hasNullOrUndefined = hasNullOrUndefined;
			state.totalProcessedArgs = numArgs;

			if (isIndexOfCP)
			{
				ProcessDollarIndexOfCP(&state, &data->value);
			}
			else
			{
				ProcessDollarIndexOfBytes(&state, &data->value);
			}

			data->kind = AggregationExpressionKind_Constant;

			FreeVariableLengthArgs(arguments);
			evaluatedOnConstants = true;
		}
	}

	if (!evaluatedOnConstants)
	{
		data->operator.arguments = arguments;
		data->operator.argumentsKind = AggregationExpressionArgumentsKind_List;
	}
}


/*
 * Helper to evaluate pre-parsed expressions of $indexOfBytes and $indexOfCP.
 */
static void
HandlePreParsedIndexOfBytesAndIndexOfCP(pgbson *doc, void *arguments,
										ExpressionResult *expressionResult,
										bool isIndexOfCP)
{
	List *argumentList = (List *) arguments;
	AggregationExpressionData *firstArg = list_nth(argumentList, 0);
	AggregationExpressionData *secondArg = list_nth(argumentList, 1);
	AggregationExpressionData *thirdArg = list_nth(argumentList, 2);
	AggregationExpressionData *fourthArg = list_nth(argumentList, 3);

	int numArgs = 2;
	bool isNullOnEmpty = false;
	ExpressionResult childResult = ExpressionResultCreateChild(expressionResult);
	EvaluateAggregationExpressionData(firstArg, doc, &childResult, isNullOnEmpty);

	bson_value_t firstValue = childResult.value;

	ExpressionResultReset(&childResult);
	EvaluateAggregationExpressionData(secondArg, doc, &childResult, isNullOnEmpty);

	bson_value_t secondValue = childResult.value;

	bson_value_t thirdValue = { 0 };
	if (thirdArg != NULL)
	{
		ExpressionResultReset(&childResult);
		EvaluateAggregationExpressionData(thirdArg, doc, &childResult, isNullOnEmpty);
		thirdValue = childResult.value;
		numArgs++;
	}

	bson_value_t fourthValue = { 0 };
	if (fourthArg != NULL)
	{
		ExpressionResultReset(&childResult);
		EvaluateAggregationExpressionData(fourthArg, doc, &childResult, isNullOnEmpty);
		fourthValue = childResult.value;
		numArgs++;
	}

	FourArgumentExpressionState state = {
		.firstArgument = firstValue,
		.secondArgument = secondValue,
		.thirdArgument = thirdValue,
		.fourthArgument = fourthValue,
		.totalProcessedArgs = numArgs,
		.hasNullOrUndefined = IsExpressionResultNullOrUndefined(&firstValue) ||
							  IsExpressionResultNullOrUndefined(&secondValue) ||
							  IsExpressionResultNullOrUndefined(&thirdValue) ||
							  IsExpressionResultNullOrUndefined(&fourthValue),
	};

	if (isIndexOfCP)
	{
		ProcessDollarIndexOfCP(&state, &childResult.value);
	}
	else
	{
		ProcessDollarIndexOfBytes(&state, &childResult.value);
	}
	ExpressionResultSetValue(expressionResult, &childResult.value);
}


/*
 * Helper to parse $trim, $ltrim, and $rtrim expressions.
 */
static void
ParseDollarTrimCore(const bson_value_t *argument, AggregationExpressionData *data, const
					char *opName, ParseAggregationExpressionContext *context)
{
	if (argument->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION50696), errmsg(
							"%s only supports an object as an argument, found %s",
							opName, BsonTypeName(argument->value_type))));
	}

	bson_value_t input = { 0 };
	bson_value_t chars = { 0 };

	bson_iter_t docIter;
	BsonValueInitIterator(argument, &docIter);

	while (bson_iter_next(&docIter))
	{
		const char *key = bson_iter_key(&docIter);
		if (strcmp(key, "input") == 0)
		{
			input = *bson_iter_value(&docIter);
		}
		else if (strcmp(key, "chars") == 0)
		{
			chars = *bson_iter_value(&docIter);
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION50694), errmsg(
								"%s found an unknown argument: %s", opName, key)));
		}
	}

	if (input.value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION50695), errmsg(
							"%s requires an 'input' field", opName)));
	}

	AggregationExpressionData *parsedInput = palloc0(sizeof(AggregationExpressionData));
	ParseAggregationExpressionData(parsedInput, &input, context);


	/* chars is an optional argument. */
	AggregationExpressionData *parsedChars = NULL;
	if (chars.value_type != BSON_TYPE_EOD)
	{
		parsedChars = palloc0(sizeof(AggregationExpressionData));
		ParseAggregationExpressionData(parsedChars, &chars, context);
	}

	if (IsAggregationExpressionConstant(parsedInput) && (parsedChars == NULL ||
														 IsAggregationExpressionConstant(
															 parsedChars)))
	{
		bson_value_t parsedCharsValue = parsedChars == NULL ? (bson_value_t) {
			0
		}
		: parsedChars->value;
		ProcessDollarTrim(&parsedInput->value, &parsedCharsValue, &data->value,
						  parsedChars != NULL, opName);
		data->kind = AggregationExpressionKind_Constant;

		pfree(parsedInput);
		if (parsedChars != NULL)
		{
			pfree(parsedChars);
		}
	}
	else
	{
		/* If the input is not constant, we need to store the parsed input in the data argument. */
		data->operator.arguments = list_make2(parsedInput, parsedChars);
		data->operator.argumentsKind = AggregationExpressionArgumentsKind_List;
	}
}


/*
 * Helper to execute pre-parsed $trim, $ltrim and $rtrim
 */
static void
HandlePreParsedDollarTrimCore(pgbson *doc, void *arguments, const char *opName,
							  ExpressionResult *expressionResult)
{
	List *argumentList = (List *) arguments;
	AggregationExpressionData *inputData = list_nth(argumentList, 0);
	AggregationExpressionData *charsData = list_nth(argumentList, 1);

	bool hasFieldExpression = false;
	bool isNullOnEmpty = false;
	ExpressionResult childResult = ExpressionResultCreateChild(expressionResult);
	EvaluateAggregationExpressionData(inputData, doc, &childResult, isNullOnEmpty);

	bson_value_t inputValue = childResult.value;
	ExpressionResultReset(&childResult);

	bson_value_t charsValue = { 0 };
	if (charsData != NULL)
	{
		EvaluateAggregationExpressionData(charsData, doc, &childResult, isNullOnEmpty);
		charsValue = childResult.value;
	}

	DualArgumentExpressionState state;
	memset(&state, 0, sizeof(DualArgumentExpressionState));
	InitializeDualArgumentExpressionState(inputValue, charsValue, hasFieldExpression,
										  &state);

	bson_value_t result = { 0 };
	ProcessDollarTrim(&inputValue, &charsValue, &result, charsData != NULL, opName);
	ExpressionResultSetValue(expressionResult, &result);
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
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION51103), errmsg(
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
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION31024), errmsg(
								"%s found an unknown argument: %s", opName, key)));
		}
	}

	if (input->value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION31022), errmsg(
							"%s requires 'input' parameter", opName)));
	}

	if (regex.value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION31023), errmsg(
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
		if (!ValidateEvaluatedRegexInput(&regexArgs->input.value, &regexArgs->regex.value,
										 &regexArgs->options.value, regexData, opName,
										 enableNoAutoCapture))
		{
			*isNullOrUndefinedInput = true;
		}

		*input = regexArgs->input.value;

		pfree(regexArgs);
		data->kind = AggregationExpressionKind_Constant;

		return true;
	}
	else if (IsAggregationExpressionConstant(&regexArgs->regex) &&
			 IsAggregationExpressionConstant(&regexArgs->options))
	{
		if (ValidateEvaluatedRegexInput(&regexArgs->input.value, &regexArgs->regex.value,
										&regexArgs->options.value, regexData, opName,
										enableNoAutoCapture))
		{
			regexArgs->pcreData = regexData->pcreData;
		}
	}

	data->operator.arguments = regexArgs;
	data->operator.argumentsKind = AggregationExpressionArgumentsKind_Palloc;

	return false;
}


/* --------------------------------------------------------- */
/* Process operators helper functions */
/* --------------------------------------------------------- */

/* Function that processes a single argument for $concat. */
static bool
ProcessDollarConcatElement(const bson_value_t *currentValue, void *state,
						   bson_value_t *result)
{
	if (IsExpressionResultNullOrUndefined(currentValue))
	{
		result->value_type = BSON_TYPE_NULL;
		return false; /* stop processing more arguments. */
	}

	DollarConcatOperatorState *context = (DollarConcatOperatorState *) state;

	if (currentValue->value_type != BSON_TYPE_UTF8)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION16702), errmsg(
							"$concat only supports strings, not %s",
							BsonTypeName(currentValue->value_type))));
	}

	StringView *strView = AllocateStringViewFromBsonValueString(currentValue);
	context->stringList = lappend(context->stringList, strView);
	context->totalSize += currentValue->value.v_utf8.len;
	return true;
}


/* Function that validates the final state before returning the result for $concat. */
static void
ProcessDollarConcatResult(void *state, bson_value_t *result)
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
		StringView *currentValue = lfirst(cell);
		memcpy(result->value.v_utf8.str + result->value.v_utf8.len,
			   currentValue->string, currentValue->length);
		result->value.v_utf8.len += currentValue->length;
		pfree(currentValue);
	}

	list_free(context->stringList);
}


/* Function that processes a single argument for $toUpper. */
static void
ProcessDollarToUpper(const bson_value_t *currentValue, bson_value_t *result)
{
	if (IsExpressionResultNullOrUndefined(currentValue))
	{
		result->value_type = BSON_TYPE_UTF8;
		result->value.v_utf8.str = "";
		result->value.v_utf8.len = 0;
		return;
	}

	switch (currentValue->value_type)
	{
		case BSON_TYPE_UTF8:
		{
			result->value_type = BSON_TYPE_UTF8;
			char *str = currentValue->value.v_utf8.str;
			for (uint32_t currByte = 0; currByte < currentValue->value.v_utf8.len;
				 currByte++)
			{
				if (islower(*str))
				{
					*str = 'A' + (*str - 'a');
				}
				str++;
			}
			result->value = currentValue->value;
			break;
		}

		default:
		{
			ProcessCommonBsonTypesForStringOperators(result, currentValue,
													 DateStringFormatCase_UpperCase);
		}
	}
}


/* Function that validates the final state before returning the result for $split. */
static void
ProcessDollarSplit(void *state, bson_value_t *result)
{
	DualArgumentExpressionState *context = (DualArgumentExpressionState *) state;

	if (context->hasNullOrUndefined)
	{
		result->value_type = BSON_TYPE_NULL;
		return;
	}

	if (context->firstArgument.value_type != BSON_TYPE_UTF8)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40085), errmsg(
							"$split requires an expression that evaluates to a string as a first argument, found: %s",
							BsonTypeName(context->firstArgument.value_type))));
	}

	if (context->secondArgument.value_type != BSON_TYPE_UTF8)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40086), errmsg(
							"$split requires an expression that evaluates to a string as a second argument, found: %s",
							BsonTypeName(context->secondArgument.value_type))));
	}

	if (context->secondArgument.value.v_utf8.len == 0)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40087), errmsg(
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
static void
ProcessDollarStrLenBytes(const bson_value_t *currentValue, bson_value_t *result)
{
	if (currentValue->value_type != BSON_TYPE_UTF8)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION34473), errmsg(
							"$strLenBytes requires a string argument, found: %s",
							currentValue->value_type == BSON_TYPE_EOD ?
							MISSING_TYPE_NAME :
							BsonTypeName(currentValue->value_type))));
	}

	result->value_type = BSON_TYPE_INT32;
	result->value.v_int32 = currentValue->value.v_utf8.len;
}


/* Function that processes a single argument for $strLenCP. */
static void
ProcessDollarStrLenCP(const bson_value_t *currentValue, bson_value_t *result)
{
	if (currentValue->value_type != BSON_TYPE_UTF8)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION34471), errmsg(
							"$strLenCP requires a string argument, found: %s",
							currentValue->value_type == BSON_TYPE_EOD ?
							MISSING_TYPE_NAME :
							BsonTypeName(currentValue->value_type))));
	}

	result->value_type = BSON_TYPE_INT32;
	result->value.v_int32 = Utf8CodePointCount(currentValue);
}


/* Function that processes a single argument for $tolower. */
static void
ProcessDollarToLower(const bson_value_t *currentValue, bson_value_t *result)
{
	if (IsExpressionResultNullOrUndefined(currentValue))
	{
		result->value_type = BSON_TYPE_UTF8;
		result->value.v_utf8.str = "";
		result->value.v_utf8.len = 0;
		return;
	}

	switch (currentValue->value_type)
	{
		case BSON_TYPE_UTF8:
		{
			result->value_type = BSON_TYPE_UTF8;
			ConvertToLower(currentValue->value.v_utf8.str,
						   currentValue->value.v_utf8.len);
			result->value = currentValue->value;
			break;
		}

		default:
		{
			ProcessCommonBsonTypesForStringOperators(result, currentValue,
													 DateStringFormatCase_LowerCase);
		}
	}
}


/**
 * A common function to process types for toLower and toUpper aggregation operators.
 * @param result : the final result which will contain the output
 * @param currentValue :  the element given which contains the type of input and the value of the input.
 * @param isToUpper : Since this is a common function so a way is needed to process the results differently based on upper or lower case operator. It specified whether the call was made for toUpper or toLower aggregation operator
 */
static bool
ProcessCommonBsonTypesForStringOperators(bson_value_t *result,
										 const bson_value_t *currentValue,
										 DateStringFormatCase formatCase)
{
	switch (currentValue->value_type)
	{
		case BSON_TYPE_UTF8:
		{
			result->value_type = BSON_TYPE_UTF8;
			result->value = currentValue->value;
			break;
		}

		case BSON_TYPE_INT32:
		case BSON_TYPE_INT64:
		{
			char *strNumber = (char *) BsonValueToJsonForLogging(currentValue);
			result->value_type = BSON_TYPE_UTF8;
			result->value.v_utf8.str = strNumber;
			result->value.v_utf8.len = strlen(strNumber);
			break;
		}

		case BSON_TYPE_DOUBLE:
		case BSON_TYPE_DECIMAL128:
		{
			char *strNumber = (char *) BsonValueToJsonForLogging(currentValue);
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

			int64_t dateInMs = currentValue->value.v_datetime;

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
				currentValue, timezone, formatCase);
			result->value_type = BSON_TYPE_UTF8;
			result->value.v_utf8.str = (char *) timestampStrView.string;
			result->value.v_utf8.len = timestampStrView.length;
			break;
		}

		default:
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION16007), errmsg(
								"can't convert from BSON type %s to String",
								BsonTypeName(currentValue->value_type))));
		}
	}
	return true;
}


/* Process the $IndexOfBytes operator and find the occurance of substr and save it in result*/
static void
ProcessDollarIndexOfBytes(void *state, bson_value_t *result)
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
	ProcessDollarIndexOfCore(context, isIndexOfBytesOp, &startIndex,
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
ProcessDollarIndexOfCP(void *state, bson_value_t *result)
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
	ProcessDollarIndexOfCore(context, isIndexOfBytesOp, &startIndex,
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
ProcessDollarIndexOfCore(FourArgumentExpressionState *input,
						 bool isIndexOfBytesOp,
						 int *startIndex, int *endIndex)
{
	const char *opName = isIndexOfBytesOp ? "$indexOfBytes" : "$indexOfCP";

	if (input->firstArgument.value_type != BSON_TYPE_UTF8)
	{
		int errorCode = isIndexOfBytesOp ? ERRCODE_DOCUMENTDB_LOCATION40091 :
						ERRCODE_DOCUMENTDB_LOCATION40093;
		ereport(ERROR, (errcode(errorCode), errmsg(
							"%s requires a string as the first argument, found: %s",
							opName,
							BsonTypeName(input->firstArgument.value_type))));
	}

	if (input->secondArgument.value_type != BSON_TYPE_UTF8)
	{
		int errorCode = isIndexOfBytesOp ? ERRCODE_DOCUMENTDB_LOCATION40092 :
						ERRCODE_DOCUMENTDB_LOCATION40094;
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
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40096), errmsg(
								"%s requires an integral starting index, found a value of type: %s, with value: %s",
								opName,
								input->thirdArgument.value_type == BSON_TYPE_EOD ?
								MISSING_TYPE_NAME :
								BsonTypeName(input->thirdArgument.value_type),
								input->thirdArgument.value_type == BSON_TYPE_EOD ?
								MISSING_VALUE_NAME :
								BsonValueToJsonForLogging(&input->thirdArgument)),
							errdetail_log(
								"%s requires an integral starting index, found a value of type: %s",
								opName,
								input->thirdArgument.value_type == BSON_TYPE_EOD ?
								MISSING_TYPE_NAME :
								BsonTypeName(input->thirdArgument.value_type))));
		}

		*startIndex = BsonValueAsInt32(&input->thirdArgument);
		if (*startIndex < 0)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40097), errmsg(
								"%s requires a nonnegative start index, found: %d",
								opName,
								*startIndex)));
		}
	}

	if (input->totalProcessedArgs == 4)
	{
		if (!IsBsonValueFixedInteger(&input->fourthArgument))
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40096), errmsg(
								"%s requires an integral ending index, found a value of type: %s, with value: %s",
								opName,
								input->fourthArgument.value_type == BSON_TYPE_EOD ?
								MISSING_TYPE_NAME :
								BsonTypeName(input->fourthArgument.value_type),
								input->fourthArgument.value_type == BSON_TYPE_EOD ?
								MISSING_VALUE_NAME :
								BsonValueToJsonForLogging(&input->fourthArgument)),
							errdetail_log(
								"%s requires an integral ending index, found a value of type: %s",
								opName,
								input->fourthArgument.value_type == BSON_TYPE_EOD ?
								MISSING_TYPE_NAME :
								BsonTypeName(input->fourthArgument.value_type))));
		}

		*endIndex = BsonValueAsInt32(&input->fourthArgument);

		if (*endIndex < 0)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40097), errmsg(
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
ProcessDollarStrCaseCmp(void *state, bson_value_t *result)
{
	DualArgumentExpressionState *dualState = (DualArgumentExpressionState *) state;

	ProcessCoersionForStrCaseCmp(&dualState->firstArgument);
	ProcessCoersionForStrCaseCmp(&dualState->secondArgument);

	const char *collationStringIgnore = NULL;
	int cmp = CompareStrings(dualState->firstArgument.value.v_utf8.str,
							 dualState->firstArgument.value.v_utf8.len,
							 dualState->secondArgument.value.v_utf8.str,
							 dualState->secondArgument.value.v_utf8.len,
							 collationStringIgnore);
	result->value_type = BSON_TYPE_INT32;
	result->value.v_int32 = cmp == 0 ? 0 : cmp > 0 ? 1 : -1;
}


/**
 * @param element : this refers to the element which is given as input aka currentValue in most of the operator handlers
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
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION16007), errmsg(
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


/* --------------------------------------------------------- */
/* Other helper functions */
/* --------------------------------------------------------- */

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


/* Computes result for $substrBytes operator. */
static void
ProcessDollarSubstrBytes(void *state, bson_value_t *result)
{
	ThreeArgumentExpressionState *context = (ThreeArgumentExpressionState *) state;
	const bson_value_t firstValue = context->firstArgument;
	const bson_value_t secondValue = context->secondArgument;
	const bson_value_t thirdValue = context->thirdArgument;

	if (!BsonValueIsNumber(&secondValue))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION16034), errmsg(
							"$substrBytes: starting index must be a numeric type (is BSON type %s)",
							BsonTypeName(secondValue.value_type))));
	}
	else if (!BsonValueIsNumber(&thirdValue))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION16035), errmsg(
							"$substrBytes: length must be a numeric type (is BSON type %s)",
							BsonTypeName(thirdValue.value_type))));
	}
	else if (IsExpressionResultNullOrUndefined(&firstValue))
	{
		result->value_type = BSON_TYPE_UTF8;
		result->value.v_utf8.str = "";
		result->value.v_utf8.len = 0;

		return;
	}

	ProcessCommonBsonTypesForStringOperators(result, &firstValue,
											 DateStringFormatCase_CamelCase);

	int64_t offset = BsonValueAsInt64WithRoundingMode(&secondValue, 0, true);

	if (offset < 0)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION50752), errmsg(
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
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION28656), errmsg(
							"$substrBytes:  Invalid range, starting index is a UTF-8 continuation byte."
							)));
	}

	int64_t length = BsonValueAsInt64WithRoundingMode(&thirdValue, 0, true);

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
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION28657), errmsg(
							"$substrBytes: Invalid range, ending index is in the middle of a UTF-8 character."
							)));
	}

	result->value.v_utf8.str = offsetString;
	result->value.v_utf8.len = substringLength;
}


/* Computes result for $substrCP operator. */
static void
ProcessDollarSubstrCP(void *state, bson_value_t *result)
{
	ThreeArgumentExpressionState *context = (ThreeArgumentExpressionState *) state;
	const bson_value_t firstValue = context->firstArgument;
	const bson_value_t secondValue = context->secondArgument;
	const bson_value_t thirdValue = context->thirdArgument;

	bool checkFixedInteger = true;
	if (!BsonValueIsNumber(&secondValue))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION34450), errmsg(
							"$substrCP: starting index must be a numeric type (is BSON type %s)",
							BsonTypeName(secondValue.value_type))));
	}
	else if (!IsBsonValue32BitInteger(&secondValue, checkFixedInteger))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION34451), errmsg(
							"$substrCP: starting index cannot be represented as a 32-bit integral value")));
	}
	else if (!BsonValueIsNumber(&thirdValue))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION34452), errmsg(
							"$substrCP: length must be a numeric type (is BSON type %s)",
							BsonTypeName(thirdValue.value_type))));
	}
	else if (!IsBsonValue32BitInteger(&thirdValue, checkFixedInteger))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION34453), errmsg(
							"$substrCP: length cannot be represented as a 32-bit integral value")));
	}
	else if (IsExpressionResultNullOrUndefined(&firstValue))
	{
		result->value_type = BSON_TYPE_UTF8;
		result->value.v_utf8.str = "";
		result->value.v_utf8.len = 0;

		return;
	}

	ProcessCommonBsonTypesForStringOperators(result, &firstValue,
											 DateStringFormatCase_CamelCase);

	int offset = BsonValueAsInt32(&secondValue);
	int length = BsonValueAsInt32(&thirdValue);

	if (length < 0)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION34454), errmsg(
							"$substrCP: length must be a nonnegative integer."
							)));
	}
	else if (offset < 0)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION34455), errmsg(
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


static void
ProcessDollarTrim(const bson_value_t *inputValue, const bson_value_t *charsValue,
				  bson_value_t *result, const bool providedChars, const char *opName)
{
	if (IsExpressionResultNullOrUndefined(inputValue) ||
		(providedChars && IsExpressionResultNullOrUndefined(charsValue)))
	{
		result->value_type = BSON_TYPE_NULL;
		return;
	}

	if (inputValue->value_type != BSON_TYPE_UTF8)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION50699),
						errmsg(
							"%s requires its input to be a string, got %s (of type %s) instead.",
							opName, BsonValueToJsonForLogging(inputValue), BsonTypeName(
								inputValue->value_type)),
						errdetail_log(
							"%s requires its input to be a string, got of type %s instead.",
							opName, BsonTypeName(inputValue->value_type))));
	}

	if (charsValue->value_type != BSON_TYPE_EOD && charsValue->value_type !=
		BSON_TYPE_UTF8)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION50700), errmsg(
							" %s requires 'chars' to be a string, got %s (of type %s) instead.",
							opName, BsonValueToJsonForLogging(charsValue), BsonTypeName(
								charsValue->value_type)), errdetail_log(
							" %s requires 'chars' to be a string, got of type %s instead.",
							opName, BsonTypeName(charsValue->value_type))));
	}

	result->value_type = BSON_TYPE_UTF8;

	HTAB *charsHashTable = CreateStringViewHashSet();
	FillLookUpTableDollarTrim(charsHashTable, charsValue);

	int startIndex, endIndex;
	if (strcmp(opName, "$ltrim") == 0)
	{
		startIndex = FindStartIndexDollarTrim(charsHashTable, inputValue);
		endIndex = inputValue->value.v_utf8.len - 1;
	}
	else if (strcmp(opName, "$rtrim") == 0)
	{
		startIndex = 0;
		endIndex = FindEndIndexDollarTrim(charsHashTable, inputValue);
	}
	else
	{
		startIndex = FindStartIndexDollarTrim(charsHashTable, inputValue);
		endIndex = FindEndIndexDollarTrim(charsHashTable, inputValue);
	}

	WriteDollarTrimResult(inputValue, result, startIndex, endIndex);
	hash_destroy(charsHashTable);
}


/*
 * This function removes everything before the start index and after the end index,
 * and then assigns the trimmed string to the result of the $trim, $rtrim, or $ltrim operator.
 */
static void
WriteDollarTrimResult(const bson_value_t *input, bson_value_t *result, int startIndex,
					  int endIndex)
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


/* Parses a $replaceOne expression. */
void
ParseDollarReplaceOne(const bson_value_t *argument, AggregationExpressionData *data,
					  ParseAggregationExpressionContext *context)
{
	/* boolean to avoid strcmp on opName. Passing opName as well to avoid if-else cases in error messages. */
	bool isDollarReplaceOne = true;
	const char *opName = "$replaceOne";
	ParseDollarReplaceHelper(argument, data, opName, isDollarReplaceOne, context);
}


/* Parses a $replaceAll expression. */
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
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION51104), errmsg(
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
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION51110), errmsg(
									"%s:  regular expression options cannot contain an embedded null byte",
									opName)));
			}

			if (!IsValidRegexOptions(options->value.v_utf8.str))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION51108), errmsg(
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
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION51106), errmsg(
									"%s needs 'options' to be of type string", opName)));
			}

			if (typeRegexOptionLength > 0)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION51107), errmsg(
									"%s found regex option(s) specified in both 'regex' and 'option' fields",
									opName)));
			}

			if (BsonValueStringHasNullCharcter(options))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION51110), errmsg(
									"%s:  regular expression options cannot contain an embedded null byte",
									opName)));
			}

			if (!IsValidRegexOptions(options->value.v_utf8.str))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION51108), errmsg(
									"%s invalid flag in regex options: %s", opName,
									options->value.v_utf8.str)));
			}

			regexData->options = options->value.v_utf8.str;
		}
		else if (typeRegexOptionLength > 0 && !IsValidRegexOptions(
					 regex->value.v_regex.options))
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION51108), errmsg(
								"%s invalid flag in regex options: %s", opName,
								regex->value.v_regex.options)));
		}
	}
	else if (regex->value_type == BSON_TYPE_UTF8)
	{
		if (BsonValueStringHasNullCharcter(regex))
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION51109), errmsg(
								"%s: regular expression cannot contain an embedded null byte",
								opName)));
		}

		if (!IsExpressionResultNullOrUndefined(options))
		{
			if (options->value_type != BSON_TYPE_UTF8)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION51106), errmsg(
									"%s needs 'options' to be of type string", opName)));
			}

			if (BsonValueStringHasNullCharcter(options))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION51110), errmsg(
									"%s:  regular expression options cannot contain an embedded null byte",
									opName)));
			}
		}

		if (!IsValidRegexOptions(options->value.v_utf8.str))
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION51108), errmsg(
								"%s invalid flag in regex options: %s", opName,
								options->value.v_utf8.str)));
		}

		regexData->regex = regex->value.v_utf8.str;
		regexData->options = options->value.v_utf8.str;
	}
	else
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION51105), errmsg(
							"%s needs 'regex' to be of type string or regex", opName)));
	}

	if (IsExpressionResultNullOrUndefined(input))
	{
		validInput = false;
	}
	else if (input->value_type != BSON_TYPE_UTF8)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION51104), errmsg(
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

		if (currMatchStartID == PCRE2_INDEX_UNSET)
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
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION51151), errmsg(
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
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION51751), errmsg(
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
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION51750), errmsg(
								"%s found an unknown argument: %s", opName, key)));
		}
	}

	if (input.value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION51749), errmsg(
							"%s requires 'input' to be specified", opName)));
	}
	else if (find.value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION51748), errmsg(
							"%s requires 'find' to be specified", opName)));
	}
	else if (replacement.value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION51747), errmsg(
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
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION51746), errmsg(
							"%s requires that 'input' be a string, found: %s",
							opName,
							(char *) BsonValueToJsonForLogging(input)), errdetail_log(
							"%s requires that 'input' be a string, found of type %s",
							opName,
							BsonTypeName(input->value_type))));
	}
	else if (find->value_type != BSON_TYPE_UTF8 &&
			 !IsExpressionResultNullOrUndefined(find))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION51745), errmsg(
							"%s requires that 'find' be a string, found: %s",
							opName,
							(char *) BsonValueToJsonForLogging(find)), errdetail_log(
							"%s requires that 'find' be a string, found of type %s",
							opName,
							BsonTypeName(find->value_type))));
	}
	else if (replacement->value_type != BSON_TYPE_UTF8 &&
			 !IsExpressionResultNullOrUndefined(replacement))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION51744), errmsg(
							"%s requires that 'replacement' be a string, found: %s",
							opName,
							(char *) BsonValueToJsonForLogging(replacement)),
						errdetail_log(
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
