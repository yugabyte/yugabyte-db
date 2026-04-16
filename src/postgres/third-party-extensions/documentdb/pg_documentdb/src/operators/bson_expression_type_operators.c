/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/operators/bson_expression_type_operators.c
 *
 * Type Operator expression implementations of BSON.
 *
 *-------------------------------------------------------------------------
 */

 #include <postgres.h>
 #include <regex.h>

 #include "common/base64.h"
 #include "io/bson_core.h"
 #include "operators/bson_expression.h"
 #include "operators/bson_expression_operators.h"
 #include "operators/bson_expression_date_operators.h"
 #include "types/decimal128.h"
 #include "utils/version_utils.h"
 #include <utils/uuid.h>
 #include "metadata/metadata_cache.h"

 #define UUID_STRING_LEN 36

/* --------------------------------------------------------- */
/* Type definitions */
/* --------------------------------------------------------- */
typedef void (*ProcessToTypeOperator)(const bson_value_t *currentValue,
									  bson_value_t *result);

typedef struct ConvertToTypeArguments
{
	AggregationExpressionData *inputData;
	AggregationExpressionData *toData;
	AggregationExpressionData *formatData;
	AggregationExpressionData *onNullData;
	AggregationExpressionData *onErrorData;
} ConvertToTypeArguments;


/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */
static void ProcessDollarIsNumber(const bson_value_t *currentValue, bson_value_t *result);
static void ProcessDollarType(const bson_value_t *currentValue, bson_value_t *result);
static void ProcessDollarToBool(const bson_value_t *currentValue, bson_value_t *result);
static void ProcessDollarToObjectId(const bson_value_t *currentValue,
									bson_value_t *result);
static void ProcessDollarToInt(const bson_value_t *currentValue, bson_value_t *result);
static void ProcessDollarToLong(const bson_value_t *currentValue, bson_value_t *result);
static void ProcessDollarToString(const bson_value_t *currentValue, const
								  bson_value_t *format,
								  bson_value_t *result);
static void ProcessDollarToDate(const bson_value_t *currentValue, bson_value_t *result);
static void ProcessDollarToDouble(const bson_value_t *currentValue, bson_value_t *result);
static void ProcessDollarToDecimal(const bson_value_t *currentValue,
								   bson_value_t *result);
static void ProcessDollarConvert(List *arguments, bson_value_t *result);
static void ProcessDollarToUUID(const bson_value_t *currentValue, bson_value_t *result);
static void ProcessDollarToHashedIndexKey(const bson_value_t *currentValue,
										  bson_value_t *result);
static void ProcessDollarToBinData(const bson_value_t *currentValue,
								   const bson_type_t *toType,
								   const bson_subtype_t toSubtype,
								   const bson_value_t *format,
								   bson_value_t *result);

static void ParseTypeOperatorOneOperand(const bson_value_t *argument,
										AggregationExpressionData *data,
										const char *operatorName,
										ParseAggregationExpressionContext *parseContext,
										ProcessToTypeOperator
										processOperatorFunc);
static void HandlePreParsedTypeOperatorOneOperand(pgbson *doc, void *arguments,
												  ExpressionResult *expressionResult,
												  ProcessToTypeOperator
												  processOperatorFunc);

static void ApplyDollarConvert(List *arguments,
							   const AggregationExpressionData *onErrorData,
							   bson_value_t *result, bool *hasError);

static int32_t ConvertStringToInt32(const bson_value_t *value);
static int64_t ConvertStringToInt64(const bson_value_t *value);
static double ConvertStringToDouble(const bson_value_t *value);
static bson_decimal128_t ConvertStringToDecimal128(const bson_value_t *value);
static void GetToTypeAndSubTypeForConvert(const bson_value_t *toValue,
										  bson_value_t *toType,
										  bson_subtype_t *toSubtype);
static bool ValidateUUIDString(const char *string);
static void ConvertUUIDStringToPgUUID(const char *uuidString, bson_value_t *result);
static void ValidateStringIsNotHexBase(const bson_value_t *value);
static void ValidateConvertToTypeFormat(const bson_value_t formatValue);
static void ValidateValueIsNotNaNOrInfinity(const bson_value_t *value);
static void ValidateAndGetConvertToType(const bson_value_t *value, bson_type_t *toType);
static inline void ThrowInvalidConversionError(bson_type_t sourceType,
											   bson_type_t targetType);
static inline void ThrowOverflowTargetError(const bson_value_t *value);
static inline void ThrowFailedToParseNumberError(const char *value, const char *reason);
static inline void ThrowFailedToParseBinDataError(const char *value, const char *reason);
static inline void ThrowFailedToParseBinDataDeprecatedSubTypeError(int deprecatedSubType,
																   int supportedSubType);
static void ValidateBinDataSubType(const bson_subtype_t toSubtype);

/* --------------------------------------------------------- */
/* Parse and handle pre-parse functions */
/* --------------------------------------------------------- */

/*
 * Parses a $isNumber expression.
 * $isNumber is expressed as { "$isNumber": <expression> }
 */
void
ParseDollarIsNumber(const bson_value_t *argument, AggregationExpressionData *data,
					ParseAggregationExpressionContext *context)
{
	ParseTypeOperatorOneOperand(argument, data, "$isNumber", context,
								ProcessDollarIsNumber);
}


/*
 * Handles executing a pre-parsed $isNumber expression.
 */
void
HandlePreParsedDollarIsNumber(pgbson *doc, void *arguments,
							  ExpressionResult *expressionResult)
{
	HandlePreParsedTypeOperatorOneOperand(doc, arguments, expressionResult,
										  ProcessDollarIsNumber);
}


/*
 * Parses a $type expression.
 * $type is expressed as { "$type": <expression> }
 */
void
ParseDollarType(const bson_value_t *argument, AggregationExpressionData *data,
				ParseAggregationExpressionContext *context)
{
	ParseTypeOperatorOneOperand(argument, data, "$type", context, ProcessDollarType);
}


/*
 * Handles executing a pre-parsed $type expression.
 */
void
HandlePreParsedDollarType(pgbson *doc, void *arguments,
						  ExpressionResult *expressionResult)
{
	HandlePreParsedTypeOperatorOneOperand(doc, arguments, expressionResult,
										  ProcessDollarType);
}


/*
 * Parses a $toBool expression.
 * $toBool is expressed as { "$toBool": <expression> }
 */
void
ParseDollarToBool(const bson_value_t *argument, AggregationExpressionData *data,
				  ParseAggregationExpressionContext *context)
{
	ParseTypeOperatorOneOperand(argument, data, "$toBool", context,
								ProcessDollarToBool);
}


/*
 * Handles executing a pre-parsed $toBool expression.
 */
void
HandlePreParsedDollarToBool(pgbson *doc, void *arguments,
							ExpressionResult *expressionResult)
{
	HandlePreParsedTypeOperatorOneOperand(doc, arguments, expressionResult,
										  ProcessDollarToBool);
}


/*
 * Parses a $toObjectId expression.
 * $toObjectId is expressed as { "$toObjectId": <strExpression> }
 */
void
ParseDollarToObjectId(const bson_value_t *argument, AggregationExpressionData *data,
					  ParseAggregationExpressionContext *context)
{
	ParseTypeOperatorOneOperand(argument, data, "$toObjectId", context,
								ProcessDollarToObjectId);
}


/*
 * Handles executing a pre-parsed $toObjectId expression.
 */
void
HandlePreParsedDollarToObjectId(pgbson *doc, void *arguments,
								ExpressionResult *expressionResult)
{
	HandlePreParsedTypeOperatorOneOperand(doc, arguments, expressionResult,
										  ProcessDollarToObjectId);
}


/*
 * Parses a $toInt expression.
 * $toInt is expressed as { "$toInt": <expression> }
 */
void
ParseDollarToInt(const bson_value_t *argument, AggregationExpressionData *data,
				 ParseAggregationExpressionContext *context)
{
	ParseTypeOperatorOneOperand(argument, data, "$toInt", context, ProcessDollarToInt);
}


/*
 * Handles executing a pre-parsed $toInt expression.
 */
void
HandlePreParsedDollarToInt(pgbson *doc, void *arguments,
						   ExpressionResult *expressionResult)
{
	HandlePreParsedTypeOperatorOneOperand(doc, arguments, expressionResult,
										  ProcessDollarToInt);
}


/*
 * Parses a $toLong expression.
 * $toLong is expressed as { "$toLong": <expression> }
 */
void
ParseDollarToLong(const bson_value_t *argument, AggregationExpressionData *data,
				  ParseAggregationExpressionContext *context)
{
	ParseTypeOperatorOneOperand(argument, data, "$toLong", context,
								ProcessDollarToLong);
}


/*
 * Handles executing a pre-parsed $toLong expression.
 */
void
HandlePreParsedDollarToLong(pgbson *doc, void *arguments,
							ExpressionResult *expressionResult)
{
	HandlePreParsedTypeOperatorOneOperand(doc, arguments, expressionResult,
										  ProcessDollarToLong);
}


/*
 * Parses a $toString expression.
 * $toString is expressed as { "$toString": <expression> }
 */
void
ParseDollarToString(const bson_value_t *argument, AggregationExpressionData *data,
					ParseAggregationExpressionContext *context)
{
	int numOfRequiredArgs = 1;
	AggregationExpressionData *parsedData = ParseFixedArgumentsForExpression(argument,
																			 numOfRequiredArgs,
																			 "$toString",
																			 &data->
																			 operator.
																			 argumentsKind,
																			 context);

	/* If the arguments is constant: compute comparison result, change
	 * expression type to constant, store the result in the expression value
	 * and free the arguments list as it won't be needed anymore. */
	if (IsAggregationExpressionConstant(parsedData))
	{
		/* format value for conversion from binData */
		bson_value_t format = { 0 };
		format.value_type = BSON_TYPE_UTF8;
		format.value.v_utf8.str = "auto";
		format.value.v_utf8.len = 4;

		ProcessDollarToString(&parsedData->value, &format, &data->value);
		data->kind = AggregationExpressionKind_Constant;
		pfree(parsedData);
	}
	else
	{
		data->operator.argumentsKind = AggregationExpressionArgumentsKind_Palloc;
		data->operator.arguments = parsedData;
	}
}


/*
 * Handles executing a pre-parsed $toString expression.
 */
void
HandlePreParsedDollarToString(pgbson *doc, void *arguments,
							  ExpressionResult *expressionResult)
{
	AggregationExpressionData *argument = (AggregationExpressionData *) arguments;

	bool isNullOnEmpty = false;
	ExpressionResult childResult = ExpressionResultCreateChild(expressionResult);
	EvaluateAggregationExpressionData(argument, doc, &childResult, isNullOnEmpty);
	bson_value_t currentValue = childResult.value;

	/* format value for conversion from binData */
	bson_value_t format = { 0 };
	format.value_type = BSON_TYPE_UTF8;
	format.value.v_utf8.str = "auto";
	format.value.v_utf8.len = 4;

	bson_value_t result = { 0 };
	ProcessDollarToString(&currentValue, &format, &result);
	ExpressionResultSetValue(expressionResult, &result);
}


/*
 * Parses a $toDate expression.
 * $toDate is expressed as { "$toDate": <expression> }
 */
void
ParseDollarToDate(const bson_value_t *argument, AggregationExpressionData *data,
				  ParseAggregationExpressionContext *context)
{
	ParseTypeOperatorOneOperand(argument, data, "$toDate", context,
								ProcessDollarToDate);
}


/*
 * Handles executing a pre-parsed $toDate expression.
 */
void
HandlePreParsedDollarToDate(pgbson *doc, void *arguments,
							ExpressionResult *expressionResult)
{
	HandlePreParsedTypeOperatorOneOperand(doc, arguments, expressionResult,
										  ProcessDollarToDate);
}


/*
 * Parses a $toDouble expression.
 * $toDouble is expressed as { "$toDouble": <expression> }
 */
void
ParseDollarToDouble(const bson_value_t *argument, AggregationExpressionData *data,
					ParseAggregationExpressionContext *context)
{
	ParseTypeOperatorOneOperand(argument, data, "$toDouble", context,
								ProcessDollarToDouble);
}


/*
 * Handles executing a pre-parsed $toDouble expression.
 */
void
HandlePreParsedDollarToDouble(pgbson *doc, void *arguments,
							  ExpressionResult *expressionResult)
{
	HandlePreParsedTypeOperatorOneOperand(doc, arguments, expressionResult,
										  ProcessDollarToDouble);
}


/*
 * Parses a $toDecimal expression.
 * $toDecimal is expressed as { "$toDecimal": <expression> }
 */
void
ParseDollarToDecimal(const bson_value_t *argument, AggregationExpressionData *data,
					 ParseAggregationExpressionContext *context)
{
	ParseTypeOperatorOneOperand(argument, data, "$toDecimal", context,
								ProcessDollarToDecimal);
}


/*
 * Handles executing a pre-parsed $toDecimal expression.
 */
void
HandlePreParsedDollarToDecimal(pgbson *doc, void *arguments,
							   ExpressionResult *expressionResult)
{
	HandlePreParsedTypeOperatorOneOperand(doc, arguments, expressionResult,
										  ProcessDollarToDecimal);
}


/*
 * Parses a $toUUID expression.
 * $toUUID is expressed as { "$toUUID": <expression> }
 */
void
ParseDollarToUUID(const bson_value_t *argument, AggregationExpressionData *data,
				  ParseAggregationExpressionContext *context)
{
	ParseTypeOperatorOneOperand(argument, data, "$toUUID", context,
								ProcessDollarToUUID);
}


/*
 * Handles executing a pre-parsed $toUUID expression.
 */
void
HandlePreParsedDollarToUUID(pgbson *doc, void *arguments,
							ExpressionResult *expressionResult)
{
	HandlePreParsedTypeOperatorOneOperand(doc, arguments, expressionResult,
										  ProcessDollarToUUID);
}


/*
 * Parses a $toHashedIndexKey expression.
 * $toHashedIndexKey is expressed as { $toHashedIndexKey: <key or string to hash> }
 */
void
ParseDollarToHashedIndexKey(const bson_value_t *argument, AggregationExpressionData *data,
							ParseAggregationExpressionContext *context)
{
	AggregationExpressionData *argumentAggExpData = palloc0(
		sizeof(AggregationExpressionData));
	ParseAggregationExpressionData(argumentAggExpData, argument, context);

	/* if the input is constant, calculate hash value directly. */
	if (IsAggregationExpressionConstant(argumentAggExpData))
	{
		ProcessDollarToHashedIndexKey(&argumentAggExpData->value, &data->value);
		data->kind = AggregationExpressionKind_Constant;
		pfree(argumentAggExpData);
	}
	else
	{
		data->operator.arguments = argumentAggExpData;
		data->operator.argumentsKind = AggregationExpressionArgumentsKind_Palloc;
	}
}


/*
 * Handles executing a pre-parsed $toHashedIndexKey expression.
 */
void
HandlePreParsedDollarToHashedIndexKey(pgbson *doc, void *arguments,
									  ExpressionResult *expressionResult)
{
	AggregationExpressionData *toHashArguments = arguments;

	ExpressionResult childExpression = ExpressionResultCreateChild(expressionResult);
	bool isNullOnEmpty = false;
	EvaluateAggregationExpressionData(toHashArguments, doc, &childExpression,
									  isNullOnEmpty);
	bson_value_t evaluatedArguments = childExpression.value;

	bson_value_t result;
	ProcessDollarToHashedIndexKey(&evaluatedArguments, &result);
	ExpressionResultSetValue(expressionResult, &result);
}


/*
 * Parses a $convert expression.
 * $convert is expressed as { "$convert": {"input": <expression>, "to": <typeExpression>, [onError: <expression>, onNull: <expression> ] } }
 */
void
ParseDollarConvert(const bson_value_t *argument, AggregationExpressionData *data,
				   ParseAggregationExpressionContext *context)
{
	if (argument->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE), errmsg(
							"$convert requires an object containing named arguments, but instead received: %s",
							BsonTypeName(argument->value_type))));
	}

	bson_value_t inputExpression = { 0 };
	bson_value_t toExpression = { 0 };
	bson_value_t onErrorExpression = { 0 };
	bson_value_t onNullExpression = { 0 };
	bson_value_t formatExpression = { 0 };

	bson_iter_t docIter;
	BsonValueInitIterator(argument, &docIter);
	while (bson_iter_next(&docIter))
	{
		const char *key = bson_iter_key(&docIter);

		if (strcmp(key, "input") == 0)
		{
			inputExpression = *bson_iter_value(&docIter);
		}
		else if (strcmp(key, "to") == 0)
		{
			toExpression = *bson_iter_value(&docIter);
		}
		else if (strcmp(key, "format") == 0)
		{
			formatExpression = *bson_iter_value(&docIter);
		}
		else if (strcmp(key, "onError") == 0)
		{
			onErrorExpression = *bson_iter_value(&docIter);
		}
		else if (strcmp(key, "onNull") == 0)
		{
			onNullExpression = *bson_iter_value(&docIter);
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE), errmsg(
								"$convert encountered an unrecognized argument: %s",
								key)));
		}
	}

	if (inputExpression.value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE), errmsg(
							"Missing 'input' parameter to $convert")));
	}

	if (toExpression.value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE), errmsg(
							"'to' parameter is missing for $convert")));
	}

	ConvertToTypeArguments *arguments = palloc0(sizeof(ConvertToTypeArguments));

	/* onError and onNull expressions are evaluated first,
	 * regardless of if they are going to be needed or not. */
	if (onErrorExpression.value_type != BSON_TYPE_EOD)
	{
		arguments->onErrorData = palloc0(sizeof(AggregationExpressionData));
		ParseAggregationExpressionData(arguments->onErrorData, &onErrorExpression,
									   context);
	}

	if (onNullExpression.value_type != BSON_TYPE_EOD)
	{
		arguments->onNullData = palloc0(sizeof(AggregationExpressionData));
		ParseAggregationExpressionData(arguments->onNullData, &onNullExpression, context);
	}

	bool isFormatUndefinedOrConstant = true;
	bson_value_t formatValue = { 0 };
	if (!IsExpressionResultNullOrUndefined(&formatExpression))
	{
		arguments->formatData = palloc0(sizeof(AggregationExpressionData));
		ParseAggregationExpressionData(arguments->formatData, &formatExpression,
									   context);

		isFormatUndefinedOrConstant = IsAggregationExpressionConstant(
			arguments->formatData);
		if (isFormatUndefinedOrConstant)
		{
			formatValue = arguments->formatData->value;

			if (!IsExpressionResultNullOrUndefined(&formatValue))
			{
				ValidateConvertToTypeFormat(formatValue);
			}
		}
	}

	/* Then evaluate <to-expression>, <input-expression> in this order. */
	arguments->toData = palloc0(sizeof(AggregationExpressionData));
	ParseAggregationExpressionData(arguments->toData, &toExpression, context);

	bson_type_t toType = BSON_TYPE_EOD;
	bson_subtype_t toSubtype = BSON_SUBTYPE_BINARY;
	bson_value_t originalToValue = arguments->toData->value;

	if (IsAggregationExpressionConstant(arguments->toData))
	{
		/*  <to-expression> could be { "to": {type: <targetType>, "subtype": <subtype> } } */
		/* The subtype specification is considered optional */
		if (originalToValue.value_type == BSON_TYPE_DOCUMENT)
		{
			/* Extract and validate the to.type and subtype. */
			GetToTypeAndSubTypeForConvert(&originalToValue,
										  &arguments->toData->value,
										  &toSubtype);
		}

		ValidateAndGetConvertToType(&arguments->toData->value, &toType);

		if (toType == BSON_TYPE_BINARY)
		{
			ValidateBinDataSubType(toSubtype);
		}
	}

	arguments->inputData = palloc0(sizeof(AggregationExpressionData));
	ParseAggregationExpressionData(arguments->inputData, &inputExpression, context);

	bool evaluatedOnConstants = false;
	if (IsAggregationExpressionConstant(arguments->inputData))
	{
		bson_value_t inputValue = arguments->inputData->value;
		if (inputValue.value_type == BSON_TYPE_BINARY &&
			!IsExpressionResultNullOrUndefined(&formatValue))
		{
			ValidateConvertToTypeFormat(formatValue);
		}

		bson_value_t defaultNullValue = {
			.value_type = BSON_TYPE_NULL,
		};

		/* Null 'input' argument takes precedence over null 'to' argument. */
		if (IsExpressionResultNullOrUndefined(&inputValue))
		{
			/* If no onNull expression was specified, set to the default null value. */
			/* Else set to the value of the onNull expression if it's constant. */
			if (arguments->onNullData == NULL)
			{
				data->value = defaultNullValue;

				data->kind = AggregationExpressionKind_Constant;
				evaluatedOnConstants = true;
			}
			else if (IsAggregationExpressionConstant(arguments->onNullData))
			{
				data->value = arguments->onNullData->value;

				data->kind = AggregationExpressionKind_Constant;
				evaluatedOnConstants = true;
			}
		}
		else if (IsAggregationExpressionConstant(arguments->toData))
		{
			if (IsExpressionResultNullOrUndefined(&arguments->toData->value))
			{
				data->value = defaultNullValue;

				data->kind = AggregationExpressionKind_Constant;
				evaluatedOnConstants = true;
			}
			else if (isFormatUndefinedOrConstant)
			{
				bson_value_t onErrorValue = { 0 };
				if (onErrorExpression.value_type != BSON_TYPE_EOD &&
					arguments->onErrorData != NULL)
				{
					onErrorValue = arguments->onErrorData->value;
				}
				bool hasError = false;
				List *argumentsList = list_make4(&inputValue, &toType, &toSubtype,
												 &formatValue);

				ApplyDollarConvert(argumentsList, arguments->onErrorData, &data->value,
								   &hasError);

				if (hasError)
				{
					if (onErrorValue.value_type == BSON_TYPE_EOD)
					{
						return;
					}

					if (IsAggregationExpressionConstant(arguments->onErrorData))
					{
						data->value = arguments->onErrorData->value;
						data->kind = AggregationExpressionKind_Constant;
						evaluatedOnConstants = true;
					}
				}
				else
				{
					data->kind = AggregationExpressionKind_Constant;
					evaluatedOnConstants = true;
				}
			}
		}
	}

	/* If we did not already evaluate and set to a constant value. */
	if (!evaluatedOnConstants)
	{
		/* Use the original to-value given. */
		/* This will preserve to.type and to.subtype for binData conversions */
		arguments->toData->value = originalToValue;

		data->operator.arguments = arguments;
		data->operator.argumentsKind = AggregationExpressionArgumentsKind_Palloc;
	}
}


/*
 * Handles executing a pre-parsed $convert expression.
 */
void
HandlePreParsedDollarConvert(pgbson *doc, void *arguments,
							 ExpressionResult *expressionResult)
{
	ConvertToTypeArguments *args = (ConvertToTypeArguments *) arguments;

	AggregationExpressionData *inputData = args->inputData;
	AggregationExpressionData *toData = args->toData;
	AggregationExpressionData *formatData = args->formatData;
	AggregationExpressionData *onErrorData = args->onErrorData;
	AggregationExpressionData *onNullData = args->onNullData;

	ExpressionResult childResult;

	bson_value_t onErrorValue = { 0 };
	if (onErrorData != NULL)
	{
		childResult = ExpressionResultCreateChild(expressionResult);
		EvaluateAggregationExpressionData(onErrorData, doc, &childResult, false);
		onErrorValue = childResult.value;
		ExpressionResultReset(&childResult);
	}

	bson_value_t onNullValue = { 0 };
	if (onNullData != NULL)
	{
		childResult = ExpressionResultCreateChild(expressionResult);
		EvaluateAggregationExpressionData(onNullData, doc, &childResult, false);
		onNullValue = childResult.value;
		ExpressionResultReset(&childResult);
	}

	bson_value_t formatValue = { 0 };
	if (formatData != NULL)
	{
		childResult = ExpressionResultCreateChild(expressionResult);
		EvaluateAggregationExpressionData(formatData, doc, &childResult, false);
		formatValue = childResult.value;
		ExpressionResultReset(&childResult);
	}

	bson_value_t toValue = { 0 };
	childResult = ExpressionResultCreateChild(expressionResult);
	EvaluateAggregationExpressionData(toData, doc, &childResult, false);
	toValue = childResult.value;
	ExpressionResultReset(&childResult);


	bson_subtype_t toSubtype = BSON_SUBTYPE_BINARY;
	if (toValue.value_type == BSON_TYPE_DOCUMENT)
	{
		bson_value_t originalToValue = toValue;
		GetToTypeAndSubTypeForConvert(&originalToValue, &toValue,
									  &toSubtype);
	}

	bson_type_t toType = BSON_TYPE_EOD;
	ValidateAndGetConvertToType(&toValue, &toType);

	if (toType == BSON_TYPE_BINARY)
	{
		ValidateBinDataSubType(toSubtype);

		if (!IsExpressionResultNullOrUndefined(&formatValue))
		{
			ValidateConvertToTypeFormat(formatValue);
		}
	}

	bson_value_t inputValue = { 0 };
	childResult = ExpressionResultCreateChild(expressionResult);
	EvaluateAggregationExpressionData(inputData, doc, &childResult, false);
	inputValue = childResult.value;
	ExpressionResultReset(&childResult);

	if (inputValue.value_type == BSON_TYPE_BINARY &&
		!IsExpressionResultNullOrUndefined(&formatValue))
	{
		ValidateConvertToTypeFormat(formatValue);
	}

	bson_value_t defaultNullValue = {
		.value_type = BSON_TYPE_NULL,
	};

	/* Null 'input' argument takes precedence over null 'to' argument. */
	if (IsExpressionResultNullOrUndefined(&inputValue))
	{
		/* onNull was not specified. Just set it to null */

		/* if onNull is specified but was a field path expression
		 * and the path was not found, we should return no result. */
		if (onNullData == NULL)
		{
			ExpressionResultSetValue(expressionResult, &defaultNullValue);
			return;
		}
		else if (onNullValue.value_type == BSON_TYPE_EOD)
		{
			return;
		}

		ExpressionResultSetValue(expressionResult, &onNullValue);
		return;
	}
	else if (IsExpressionResultNullOrUndefined(&toValue))
	{
		ExpressionResultSetValue(expressionResult, &defaultNullValue);
		return;
	}

	bson_value_t result = { 0 };

	List *argumentsList = list_make4(&inputValue, &toType, &toSubtype, &formatValue);
	bool hasError = false;
	ApplyDollarConvert(argumentsList, onErrorData, &result, &hasError);

	if (hasError)
	{
		if (onErrorValue.value_type == BSON_TYPE_EOD)
		{
			return;
		}

		result = onErrorValue;
	}

	ExpressionResultSetValue(expressionResult, &result);
}


/*
 * Parses a $makeArray expression.
 * "$makeArray" is expressed as { "$makeArray": <expression> }
 */
void
ParseDollarMakeArray(const bson_value_t *inputDocument, AggregationExpressionData *data,
					 ParseAggregationExpressionContext *context)
{
	AggregationExpressionData *argumentData = palloc0(
		sizeof(AggregationExpressionData));

	ParseAggregationExpressionData(argumentData, inputDocument, context);

	data->operator.arguments = argumentData;
	data->operator.argumentsKind = AggregationExpressionArgumentsKind_Palloc;
}


/*
 * Handles executing a pre-parsed $makeArray Expression.
 * If the expression evaluates to undefined, then writes empty array.
 * If the expression evaluates to an array, writes it as-is
 * If the expression evaluates to any other value, wraps it in an array.
 */
void
HandlePreParsedDollarMakeArray(pgbson *doc, void *arguments,
							   ExpressionResult *expressionResult)
{
	bool isNullOnEmpty = false;
	ExpressionResult childResult = ExpressionResultCreateChild(expressionResult);
	EvaluateAggregationExpressionData(
		(AggregationExpressionData *) arguments, doc,
		&childResult,
		isNullOnEmpty);

	if (IsExpressionResultNullOrUndefined(&childResult.value))
	{
		pgbson_array_writer arrayWriter;
		pgbson_element_writer *elementWriter = ExpressionResultGetElementWriter(
			expressionResult);
		PgbsonElementWriterStartArray(elementWriter, &arrayWriter);
		PgbsonElementWriterEndArray(elementWriter, &arrayWriter);
		return;
	}

	if (childResult.value.value_type == BSON_TYPE_ARRAY)
	{
		ExpressionResultSetValue(expressionResult, &childResult.value);
	}
	else
	{
		pgbson_array_writer arrayWriter;
		pgbson_element_writer *elementWriter = ExpressionResultGetElementWriter(
			expressionResult);
		PgbsonElementWriterStartArray(elementWriter, &arrayWriter);
		PgbsonArrayWriterWriteValue(&arrayWriter, &childResult.value);
		PgbsonElementWriterEndArray(elementWriter, &arrayWriter);
	}
}


/* --------------------------------------------------------- */
/* Parse and Handle pre-parse helper functions */
/* --------------------------------------------------------- */

/* Helper to parse type operators that take exactly 1 argument. */
static void
ParseTypeOperatorOneOperand(const bson_value_t *argument,
							AggregationExpressionData *data,
							const char *operatorName,
							ParseAggregationExpressionContext *context,
							ProcessToTypeOperator processOperatorFunc)
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
		data->operator.argumentsKind = AggregationExpressionArgumentsKind_Palloc;
		data->operator.arguments = parsedData;
	}
}


/* Helper function that evaluates a preparsed type operator expression. */
static void
HandlePreParsedTypeOperatorOneOperand(pgbson *doc, void *arguments,
									  ExpressionResult *expressionResult,
									  ProcessToTypeOperator
									  processOperatorFunc)
{
	AggregationExpressionData *argument = (AggregationExpressionData *) arguments;

	bool isNullOnEmpty = false;
	ExpressionResult childResult = ExpressionResultCreateChild(expressionResult);
	EvaluateAggregationExpressionData(argument, doc, &childResult, isNullOnEmpty);

	bson_value_t currentValue = childResult.value;

	bson_value_t result = { 0 };
	processOperatorFunc(&currentValue, &result);
	ExpressionResultSetValue(expressionResult, &result);
}


/* --------------------------------------------------------- */
/* Process operator helper functions */
/* --------------------------------------------------------- */

/* Helper function that based on the toType argument, calls the underlying $to* convert handler. */
static void
ProcessDollarConvert(List *arguments, bson_value_t *result)
{
	bson_value_t *currentValue = (bson_value_t *) linitial(arguments);
	bson_type_t *toType = (bson_type_t *) lsecond(arguments);

	switch (*toType)
	{
		case BSON_TYPE_DOUBLE:
		{
			ProcessDollarToDouble(currentValue, result);
			break;
		}

		case BSON_TYPE_UTF8:
		{
			bson_value_t *format = (bson_value_t *) lfourth(arguments);
			ProcessDollarToString(currentValue, format, result);
			break;
		}

		case BSON_TYPE_OID:
		{
			ProcessDollarToObjectId(currentValue, result);
			break;
		}

		case BSON_TYPE_BOOL:
		{
			ProcessDollarToBool(currentValue, result);
			break;
		}

		case BSON_TYPE_DATE_TIME:
		{
			ProcessDollarToDate(currentValue, result);
			break;
		}

		case BSON_TYPE_INT32:
		{
			ProcessDollarToInt(currentValue, result);
			break;
		}

		case BSON_TYPE_INT64:
		{
			ProcessDollarToLong(currentValue, result);
			break;
		}

		case BSON_TYPE_DECIMAL128:
		{
			ProcessDollarToDecimal(currentValue, result);
			break;
		}

		case BSON_TYPE_BINARY:
		{
			bson_subtype_t *toSubtype = (bson_subtype_t *) lthird(arguments);
			bson_value_t *format = (bson_value_t *) lfourth(arguments);
			ProcessDollarToBinData(currentValue, toType, *toSubtype, format, result);
			break;
		}

		default:
		{
			ThrowInvalidConversionError(currentValue->value_type, *toType);
		}
	}
}


/* Process the evaluated expression for $isNumber and sets the result to a bool indicating if the element is a number or not. */
static void
ProcessDollarIsNumber(const bson_value_t *currentValue, bson_value_t *result)
{
	result->value_type = BSON_TYPE_BOOL;
	result->value.v_bool = BsonValueIsNumber(currentValue);
}


/* Proccess the evaluated expression for $type and set the result to the resolved type name for it.
 * If the expression evaluation resulted in an EOD (missing path in the current document) the type name is set to 'missing'. */
static void
ProcessDollarType(const bson_value_t *currentValue, bson_value_t *result)
{
	bson_type_t type = currentValue->value_type;

	/* We need to cover the case where the expression is a field path and it doesn't exist, for compatibility, the expected behavior is to return 'missing'.
	 * However, 'missing' is not a valid type name for other ops, so we cover here rather than in the common BsonTypeName method. */
	char *name = BsonTypeNameExtended(type);

	result->value_type = BSON_TYPE_UTF8;
	result->value.v_utf8.str = name;
	result->value.v_utf8.len = strlen(name);
}


/* Process the evaluated expression for $toBool and returns its bool equivalent.
 * If null or undefined, the result should be null. */
static void
ProcessDollarToBool(const bson_value_t *currentValue, bson_value_t *result)
{
	if (IsExpressionResultNullOrUndefined(currentValue))
	{
		result->value_type = BSON_TYPE_NULL;
	}
	else
	{
		result->value_type = BSON_TYPE_BOOL;
		result->value.v_bool = BsonValueAsBool(currentValue);
	}
}


/* Process the evaluated expression for $toObjectId.
 * If null or undefined, the result should be null. */
static void
ProcessDollarToObjectId(const bson_value_t *currentValue, bson_value_t *result)
{
	if (IsExpressionResultNullOrUndefined(currentValue))
	{
		result->value_type = BSON_TYPE_NULL;
		return;
	}

	if (currentValue->value_type == BSON_TYPE_OID)
	{
		*result = *currentValue;
		return;
	}

	if (currentValue->value_type != BSON_TYPE_UTF8)
	{
		ThrowInvalidConversionError(currentValue->value_type, BSON_TYPE_OID);
	}

	const char *str = currentValue->value.v_utf8.str;
	uint32_t length = currentValue->value.v_utf8.len;
	if (length != 24)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_CONVERSIONFAILURE), errmsg(
							"Failed to parse objectId '%s' in $convert with no onError value: Invalid string length for parsing to OID, expected 24 but found %d",
							str, length)));
	}

	/* We could use bson_oid_is_valid but we would need to get the invalid char anyways, so we just validate it ourselves. */
	uint32_t i;
	for (i = 0; i < length; i++)
	{
		if (!isxdigit(str[i]))
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_CONVERSIONFAILURE), errmsg(
								"Failed to parse objectId '%s' in $convert with no onError value: Invalid character found in hex string: '%c'",
								str, str[i])));
		}
	}

	result->value_type = BSON_TYPE_OID;
	bson_oid_init_from_string(&result->value.v_oid, str);
}


/* Process the evaluated expression for $toInt.
 * If null or undefined, the result should be null. */
static void
ProcessDollarToInt(const bson_value_t *currentValue, bson_value_t *result)
{
	if (IsExpressionResultNullOrUndefined(currentValue))
	{
		result->value_type = BSON_TYPE_NULL;
		return;
	}

	switch (currentValue->value_type)
	{
		/* Don't use BsonValueAsInt32 as we have validation and errors that are very
		 * specific to the convert aggregation operators. */
		case BSON_TYPE_BOOL:
		{
			result->value.v_int32 = (int32_t) currentValue->value.v_bool;
			break;
		}

		case BSON_TYPE_INT32:
		{
			result->value.v_int32 = currentValue->value.v_int32;
			break;
		}

		case BSON_TYPE_DOUBLE:
		case BSON_TYPE_DECIMAL128:
		{
			ValidateValueIsNotNaNOrInfinity(currentValue);

			/* If not NaN or Infinity/-Infinity fall over to BSON_TYPE_INT64 branch. */
		}

		case BSON_TYPE_INT64:
		{
			bool checkFixedInteger = false;
			if (!IsBsonValue32BitInteger(currentValue, checkFixedInteger))
			{
				ThrowOverflowTargetError(currentValue);
			}

			result->value.v_int32 = BsonValueAsInt32(currentValue);
			break;
		}

		case BSON_TYPE_UTF8:
		{
			result->value.v_int32 = ConvertStringToInt32(currentValue);
			break;
		}

		default:
		{
			ThrowInvalidConversionError(currentValue->value_type, BSON_TYPE_INT32);
			break;
		}
	}

	result->value_type = BSON_TYPE_INT32;
}


/* Process the evaluated expression for $toLong.
 * If null or undefined, the result should be null. */
static void
ProcessDollarToLong(const bson_value_t *currentValue, bson_value_t *result)
{
	if (IsExpressionResultNullOrUndefined(currentValue))
	{
		result->value_type = BSON_TYPE_NULL;
		return;
	}

	switch (currentValue->value_type)
	{
		case BSON_TYPE_BOOL:
		case BSON_TYPE_INT32:
		case BSON_TYPE_INT64:
		case BSON_TYPE_DATE_TIME:
		{
			result->value.v_int64 = BsonValueAsInt64(currentValue);
			break;
		}

		/* For these types there are checks and errors specific to convert operators
		 * so we don't use BsonValueAsInt64. */
		case BSON_TYPE_DOUBLE:
		case BSON_TYPE_DECIMAL128:
		{
			ValidateValueIsNotNaNOrInfinity(currentValue);
			bool checkFixedInteger = false;
			if (!IsBsonValueUnquantized64BitInteger(currentValue, checkFixedInteger))
			{
				ThrowOverflowTargetError(currentValue);
			}

			result->value.v_int64 = BsonValueAsInt64(currentValue);
			break;
		}

		case BSON_TYPE_UTF8:
		{
			result->value.v_int64 = ConvertStringToInt64(currentValue);
			break;
		}

		default:
		{
			ThrowInvalidConversionError(currentValue->value_type, BSON_TYPE_INT64);
			break;
		}
	}

	result->value_type = BSON_TYPE_INT64;
}


/* Process the evaluated expression for $toDouble.
 * If null or undefined, the result should be null. */
static void
ProcessDollarToDouble(const bson_value_t *currentValue, bson_value_t *result)
{
	if (IsExpressionResultNullOrUndefined(currentValue))
	{
		result->value_type = BSON_TYPE_NULL;
		return;
	}

	switch (currentValue->value_type)
	{
		case BSON_TYPE_DOUBLE:
		case BSON_TYPE_BOOL:
		case BSON_TYPE_INT32:
		case BSON_TYPE_INT64:
		case BSON_TYPE_DATE_TIME:
		{
			result->value.v_double = BsonValueAsDouble(currentValue);
			break;
		}

		/* Don't use BsonValueAsDouble for these two types as this require validation and behaviors
		 * that are very specific to the convert operators. */
		case BSON_TYPE_DECIMAL128:
		{
			if (!IsDecimal128InDoubleRange(currentValue))
			{
				ThrowOverflowTargetError(currentValue);
			}

			result->value.v_double = GetBsonDecimal128AsDouble(currentValue);
			break;
		}

		case BSON_TYPE_UTF8:
		{
			result->value.v_double = ConvertStringToDouble(currentValue);
			break;
		}

		default:
		{
			ThrowInvalidConversionError(currentValue->value_type, BSON_TYPE_DOUBLE);
			break;
		}
	}

	result->value_type = BSON_TYPE_DOUBLE;
}


/* Process the evaluated expression for $toDecimal.
 * If null or undefined, the result should be null. */
static void
ProcessDollarToDecimal(const bson_value_t *currentValue, bson_value_t *result)
{
	if (IsExpressionResultNullOrUndefined(currentValue))
	{
		result->value_type = BSON_TYPE_NULL;
		return;
	}

	switch (currentValue->value_type)
	{
		case BSON_TYPE_BOOL:
		case BSON_TYPE_INT32:
		case BSON_TYPE_INT64:
		case BSON_TYPE_DOUBLE:
		case BSON_TYPE_DECIMAL128:
		case BSON_TYPE_DATE_TIME:
		{
			result->value.v_decimal128 = GetBsonValueAsDecimal128Quantized(
				currentValue);
			break;
		}

		/* Don't call GetBsonValueAsDecimal128 as this is very specific to convert
		 * including parsing rules and errors thrown. */
		case BSON_TYPE_UTF8:
		{
			result->value.v_decimal128 = ConvertStringToDecimal128(currentValue);
			break;
		}

		default:
		{
			ThrowInvalidConversionError(currentValue->value_type, BSON_TYPE_DECIMAL128);
			break;
		}
	}

	result->value_type = BSON_TYPE_DECIMAL128;
}


/* Process the evaluated expression for $toString.
 * If null or undefined, the result should be null. */
static void
ProcessDollarToString(const bson_value_t *currentValue, const bson_value_t *format,
					  bson_value_t *result)
{
	if (IsExpressionResultNullOrUndefined(currentValue))
	{
		result->value_type = BSON_TYPE_NULL;
		return;
	}

	switch (currentValue->value_type)
	{
		case BSON_TYPE_BOOL:
		case BSON_TYPE_INT32:
		case BSON_TYPE_INT64:
		case BSON_TYPE_DOUBLE:
		case BSON_TYPE_DECIMAL128:
		{
			char *str = (char *) BsonValueToJsonForLogging(currentValue);
			result->value.v_utf8.str = str;
			result->value.v_utf8.len = strlen(str);
			break;
		}

		case BSON_TYPE_UTF8:
		{
			*result = *currentValue;
			break;
		}

		case BSON_TYPE_OID:
		{
			char str[25];
			bson_oid_to_string(&currentValue->value.v_oid, str);
			result->value.v_utf8.str = pnstrdup(str, 24);
			result->value.v_utf8.len = 24;
			break;
		}

		case BSON_TYPE_DATE_TIME:
		{
			/* don't use any timezone offset as the result should be in the timezone the date is specified. */
			ExtensionTimezone timezone = {
				.isUtcOffset = true,
				.offsetInMs = 0,
			};

			int64_t dateInMs = currentValue->value.v_datetime;
			StringView dateStrView =
				GetDateStringWithDefaultFormat(dateInMs, timezone,
											   DateStringFormatCase_UpperCase);
			result->value.v_utf8.str = (char *) dateStrView.string;
			result->value.v_utf8.len = dateStrView.length;
			break;
		}

		case BSON_TYPE_BINARY:
		{
			if (IsExpressionResultNullOrUndefined(format))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE), errmsg(
									" A format must be specified when performing a conversion from %s to the string type.",
									BsonTypeName(currentValue->value_type))));
				break;
			}

			char *formatString = format->value.v_utf8.str;
			if (strcmp(formatString, "auto") == 0)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED), errmsg(
									"Convert to string from binData with format 'auto' is not supported yet.")));
			}

			char *encodedValue = NULL;
			bson_subtype_t subtype = currentValue->value.v_binary.subtype;
			switch (subtype)
			{
				case BSON_SUBTYPE_UUID:
				case BSON_SUBTYPE_BINARY:
				case BSON_SUBTYPE_MD5:
				case BSON_SUBTYPE_ENCRYPTED:
				case BSON_SUBTYPE_USER:
				case BSON_SUBTYPE_COLUMN:
				case BSON_SUBTYPE_SENSITIVE:
				{
					const uint8_t *binData = currentValue->value.v_binary.data;
					const uint32_t binDataLen = currentValue->value.v_binary.data_len;

					if (strcmp(formatString, "uuid") == 0)
					{
						/* convert binary data to pg_uuid_t structure */
						pg_uuid_t *uuid = (pg_uuid_t *) palloc(sizeof(pg_uuid_t));
						memcpy(uuid->data, binData, 16);

						/* convert to string using PostgreSQL UUID output function */
						Datum uuidDatum = UUIDPGetDatum(uuid);
						encodedValue = DatumGetCString(DirectFunctionCall1(uuid_out,
																		   uuidDatum));
					}
					else if (strcmp(formatString, "base64") == 0 ||
							 strcmp(formatString, "base64url") == 0)
					{
						int encodedValueLen = pg_b64_enc_len(binDataLen) + 1;  /* +1 for '\0' */
						encodedValue = palloc(encodedValueLen);

						encodedValueLen = pg_b64_encode((char_uint8_compat *) binData,
														binDataLen,
														encodedValue,
														encodedValueLen);

						/* perform substitutions for base64url */
						if (strcmp(formatString, "base64url") == 0)
						{
							/* replace '+' with '-' and '/' with '_' */
							/* base64url should not contain '+' and '/' */
							for (int i = 0; i < encodedValueLen; ++i)
							{
								if (encodedValue[i] == '+')
								{
									encodedValue[i] = '-';
								}
								else if (encodedValue[i] == '/')
								{
									encodedValue[i] = '_';
								}
							}

							/* Remove padding if any */
							while (encodedValueLen > 0 &&
								   encodedValue[encodedValueLen - 1] == '=')
							{
								encodedValue[encodedValueLen - 1] = '\0';
								encodedValueLen--;
							}
						}

						encodedValue[encodedValueLen] = '\0';
					}
					else if (strcmp(formatString, "hex") == 0)
					{
						char *binDataString = (char *) binData;

						int hexStringLen = binDataLen * 2 + 1;  /* +1 for '\0 */
						encodedValue = palloc(hexStringLen);
						int hexStringActualSize = hex_encode(binDataString, binDataLen,
															 encodedValue);

						/* convert all lowercase characters to uppercase */
						for (int i = 0; i < hexStringActualSize; i++)
						{
							if (encodedValue[i] >= 'a' && encodedValue[i] <= 'f')
							{
								encodedValue[i] -= 32;
							}
						}

						encodedValue[hexStringActualSize] = '\0';
					}
					else if (strcmp(formatString, "utf8") == 0)
					{
						encodedValue = (char *) binData;
						encodedValue[binDataLen] = '\0';
					}
					break;
				}

				case BSON_SUBTYPE_BINARY_DEPRECATED:
				{
					ThrowFailedToParseBinDataDeprecatedSubTypeError(subtype,
																	BSON_SUBTYPE_BINARY);
					break;
				}

				case BSON_SUBTYPE_UUID_DEPRECATED:
				{
					ThrowFailedToParseBinDataDeprecatedSubTypeError(subtype,
																	BSON_SUBTYPE_UUID);
					break;
				}

				default:
				{
					ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_CONVERSIONFAILURE), errmsg(
										"Failed to parse BinData in $convert with no onError value: \
										Unsupported subtype %d for BinData conversion to string.",
										subtype)));
					break;
				}
			}

			if (encodedValue != NULL)
			{
				result->value.v_utf8.str = encodedValue;
				result->value.v_utf8.len = strlen(encodedValue);
			}

			break;
		}

		default:
		{
			ThrowInvalidConversionError(currentValue->value_type, BSON_TYPE_UTF8);
		}
	}

	result->value_type = BSON_TYPE_UTF8;
}


/* Process the evaluated expression for $toDate.
 * If null or undefined, the result should be null. */
static void
ProcessDollarToDate(const bson_value_t *currentValue, bson_value_t *result)
{
	if (IsExpressionResultNullOrUndefined(currentValue))
	{
		result->value_type = BSON_TYPE_NULL;
		return;
	}

	/* int32 -> date conversion is not allowed. */
	switch (currentValue->value_type)
	{
		case BSON_TYPE_DOUBLE:
		case BSON_TYPE_DECIMAL128:
		case BSON_TYPE_INT64:
		{
			bool checkFixedInteger = false;
			if (!IsBsonValueUnquantized64BitInteger(currentValue, checkFixedInteger))
			{
				ThrowOverflowTargetError(currentValue);
			}

			result->value.v_datetime = BsonValueAsInt64(currentValue);
			break;
		}

		case BSON_TYPE_OID:
		case BSON_TYPE_TIMESTAMP:
		case BSON_TYPE_DATE_TIME:
		{
			result->value.v_datetime = BsonValueAsDateTime(currentValue);
			break;
		}

		case BSON_TYPE_UTF8:
		{
			bson_value_t dateString = *currentValue;
			bson_value_t dateValue = { 0 };

			StringToDateWithDefaultFormat(&dateString, &dateValue);
			result->value.v_datetime = dateValue.value.v_datetime;
			break;
		}

		default:
		{
			ThrowInvalidConversionError(currentValue->value_type, BSON_TYPE_DATE_TIME);
		}
	}

	result->value_type = BSON_TYPE_DATE_TIME;
}


/* Process the evaluated expression for $toUUID.
 * If null or undefined, the result should be null. */
static void
ProcessDollarToUUID(const bson_value_t *currentValue, bson_value_t *result)
{
	if (IsExpressionResultNullOrUndefined(currentValue))
	{
		result->value_type = BSON_TYPE_NULL;
		return;
	}

	if (currentValue->value_type != BSON_TYPE_UTF8)
	{
		ThrowInvalidConversionError(currentValue->value_type, BSON_TYPE_BINARY);
	}

	const char *uuidStr = currentValue->value.v_utf8.str;

	/* Validate the UUID string before processing */
	if (!ValidateUUIDString(uuidStr))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_CONVERSIONFAILURE), errmsg(
							"The provided UUID string '%s' is not valid", uuidStr)));
	}

	PG_TRY();
	{
		ConvertUUIDStringToPgUUID(uuidStr, result);
	}
	PG_CATCH();
	{
		ErrorData *edata;
		edata = CopyErrorData();
		FlushErrorState();

		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_CONVERSIONFAILURE), errmsg(
							"Unable to parse BinData %s in $convert without onError parameter: Provided UUID string is invalid: %s",
							uuidStr, uuidStr), errdetail_log(
							"Could not interpret BinData as a valid UUID due to error: %s",
							edata->message)));
	}
	PG_END_TRY();
}


/* Function that calculate the hash value of bson_value_t */
static void
ProcessDollarToHashedIndexKey(const bson_value_t *arguments, bson_value_t *result)
{
	int64 hashValue = BsonValueHash(arguments, 0);

	result->value_type = BSON_TYPE_INT64;
	result->value.v_int64 = hashValue;
}


/* Converts a string and binData to a binData. */
static void
ProcessDollarToBinData(const bson_value_t *currentValue, const bson_type_t *toType, const
					   bson_subtype_t toSubtype, const bson_value_t *format,
					   bson_value_t *result)
{
	Assert(toType != NULL && format != NULL && result != NULL && currentValue != NULL);

	if (IsExpressionResultNullOrUndefined(currentValue))
	{
		result->value_type = BSON_TYPE_NULL;
		return;
	}

	bson_type_t currentValueType = currentValue->value_type;
	if (currentValueType != BSON_TYPE_UTF8 && currentValueType != BSON_TYPE_BINARY)
	{
		ThrowInvalidConversionError(currentValueType, BSON_TYPE_BINARY);
	}

	switch (currentValueType)
	{
		case BSON_TYPE_UTF8:
		{
			if (IsExpressionResultNullOrUndefined(format))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE), errmsg(
									"A specific format must be provided when attempting to convert from %s into 'binData'.",
									BsonTypeName(currentValueType))));
			}

			char *formatString = format->value.v_utf8.str;
			if (strcmp(formatString, "auto") == 0)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE), errmsg(
									"Format '%s' is not supported for string to 'binData' conversion",
									formatString)));
			}

			char *currentString = (char *) currentValue->value.v_binary.data;

			if ((toSubtype == BSON_SUBTYPE_UUID) ^ (strcmp(formatString, "uuid") == 0))
			{
				ThrowFailedToParseBinDataError(currentString,
											   "Only the UUID subtype (4) is allowed with the 'uuid' format");
			}

			/* Check if the input format is correct */
			char *decodedValue = currentString;
			int decodedValueLen = strlen(currentString);
			if (strcmp(formatString, "base64") == 0)
			{
				decodedValue = palloc(decodedValueLen);
				decodedValueLen = pg_b64_decode(currentString,
												currentValue->value.v_utf8.len,
												(char_uint8_compat *) decodedValue,
												decodedValueLen);

				if (decodedValueLen == -1)
				{
					ThrowFailedToParseBinDataError(currentString,
												   "The provided input does not represent a valid base64-encoded string.");
				}
			}
			else if (strcmp(formatString, "base64url") == 0)
			{
				int padding = (4 - (decodedValueLen % 4)) % 4;
				decodedValue = palloc(decodedValueLen + padding + 1);

				memcpy(decodedValue, currentString, decodedValueLen);

				/* Replace URL safe characters with base64 equivalent characters. */
				/* base64url should not contain '+' and '/' */
				for (int i = 0; i < decodedValueLen; ++i)
				{
					if (decodedValue[i] == '-')
					{
						decodedValue[i] = '+';
					}
					else if (decodedValue[i] == '_')
					{
						decodedValue[i] = '/';
					}
					else if (decodedValue[i] == '+')
					{
						ThrowFailedToParseBinDataError(currentString,
													   "The provided input does not represent a valid base64-encoded string.");
					}
					else if (decodedValue[i] == '/')
					{
						ThrowFailedToParseBinDataError(currentString,
													   "The provided input does not represent a valid base64-encoded string.");
					}
				}

				/* Add padding if necessary. */
				if (padding > 0)
				{
					memset(decodedValue + decodedValueLen, '=', padding);
				}

				/* Decode as base64. */
				decodedValueLen = pg_b64_decode(decodedValue, decodedValueLen + padding,
												(char_uint8_compat *) decodedValue,
												decodedValueLen);

				if (decodedValueLen == -1)
				{
					ThrowFailedToParseBinDataError(currentString,
												   "Input is not a valid base64url string.");
				}
			}
			else if (strcmp(formatString, "hex") == 0)
			{
				decodedValueLen = (decodedValueLen / 2) + VARHDRSZ;
				decodedValue = palloc(decodedValueLen);

				decodedValueLen = hex_decode(currentString, strlen(currentString),
											 decodedValue);
			}
			else if (strcmp(formatString, "uuid") == 0)
			{
				if (!ValidateUUIDString(currentString))
				{
					ThrowFailedToParseBinDataError(currentString,
												   "The provided input does not represent a properly formatted UUID string %s.");
				}

				ConvertUUIDStringToPgUUID(currentString, result);
				return;
			}

			result->value_type = BSON_TYPE_BINARY;
			result->value.v_binary.data = (uint8_t *) decodedValue;
			result->value.v_binary.data_len = decodedValueLen;
			result->value.v_binary.subtype = toSubtype;

			break;
		}

		case BSON_TYPE_BINARY:
		{
			uint8_t *binData = currentValue->value.v_binary.data;
			uint32_t binDataLen = currentValue->value.v_binary.data_len;

			/* use same binary representation but set subtype */
			result->value.v_binary.data = binData;
			result->value.v_binary.data_len = binDataLen;
			result->value_type = BSON_TYPE_BINARY;
			result->value.v_binary.subtype = toSubtype;

			break;
		}

		default:
		{
			break;
		}
	}
}


/* --------------------------------------------------------- */
/* Other helper functions. */
/* --------------------------------------------------------- */

/* Extract the 'type' and 'subtype' from a document 'to' value. */
static void
GetToTypeAndSubTypeForConvert(const bson_value_t *toValue,
							  bson_value_t *toTypeValue,
							  bson_subtype_t *toSubtype)
{
	Assert(toValue->value_type == BSON_TYPE_DOCUMENT);

	bson_iter_t toIter;
	BsonValueInitIterator(toValue, &toIter);

	while (bson_iter_next(&toIter))
	{
		const char *key = bson_iter_key(&toIter);
		const bson_value_t *value = bson_iter_value(&toIter);

		if (strcmp(key, "type") == 0)
		{
			*toTypeValue = *value;
		}
		else if (strcmp(key, "subtype") == 0)
		{
			if (value->value_type == BSON_TYPE_INT32)
			{
				*toSubtype = value->value.v_int32;
			}
			else if (value->value_type == BSON_TYPE_DOUBLE)
			{
				*toSubtype = (int) value->value.v_double;
			}
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE), errmsg(
								"Unknown key in $convert 'to' expression: %s",
								key)));
		}
	}
}


/* Ensures toSubtype is a valid BinData subtype */
static void
ValidateBinDataSubType(const bson_subtype_t toSubtype)
{
	if (toSubtype == BSON_SUBTYPE_UUID_DEPRECATED)
	{
		ThrowFailedToParseBinDataDeprecatedSubTypeError(toSubtype, BSON_SUBTYPE_UUID);
	}

	if (toSubtype == BSON_SUBTYPE_BINARY_DEPRECATED)
	{
		ThrowFailedToParseBinDataDeprecatedSubTypeError(toSubtype, BSON_SUBTYPE_BINARY);
	}

	/* user-defined subtypes must fall within the valid numeric range of 128 to 255. */
	if (toSubtype >= 128 && toSubtype <= 255)
	{
		return;
	}

	if (toSubtype < BSON_SUBTYPE_BINARY || toSubtype > BSON_SUBTYPE_SENSITIVE)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION4341107), errmsg(
							"Cannot use bindata subtype %d for $convert operation",
							toSubtype)));
	}
}


/* Validates the type to convert to and sets 'toType' to the validated type. */
static void
ValidateAndGetConvertToType(const bson_value_t *toValue, bson_type_t *toType)
{
	if (toValue->value_type == BSON_TYPE_UTF8)
	{
		const char *typeName = toValue->value.v_utf8.str;
		uint32_t len = toValue->value.v_utf8.len;
		if (len == 7 && strcmp(typeName, MISSING_TYPE_NAME) == 0)
		{
			/* Should support 'missing' as a valid type for $convert.
			 * We convert it to EOD as there is no valid conversion from any type
			 * to 'missing', and we will handle this when throwing the error. */
			*toType = BSON_TYPE_EOD;
		}
		else
		{
			*toType = BsonTypeFromName(toValue->value.v_utf8.str);
		}
	}
	else if (BsonValueIsNumber(toValue))
	{
		if (!IsBsonValueFixedInteger(toValue))
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE), errmsg(
								"In $convert, numeric 'to' argument is not an integer")));
		}

		int64_t typeCode = BsonValueAsInt64(toValue);

		if (!TryGetTypeFromInt64(typeCode, toType))
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE), errmsg(
								"During $convert, the numeric value provided for 'to' is not a valid BSON type: %lld",
								(long long int) typeCode)));
		}
	}
	else if (!IsExpressionResultNullOrUndefined(toValue))
	{
		/* If the 'to' evaluated value is null or undefined, we should return null, not an error.
		 * however, we can't do it here yet, as if the 'input' expression evaluates to null and onNull is specified,
		 * we must return that instead. */
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE), errmsg(
							"The 'to' parameter in $convert must contain either a string value or a numeric value, but it currently holds %s",
							BsonTypeName(toValue->value_type))));
	}
}


/* Validates the format field of $convert */
/* 'format' must be a string with value 'base64', 'base64url', 'hex', 'uuid', 'auto' */
static void
ValidateConvertToTypeFormat(const bson_value_t formatValue)
{
	if (IsExpressionResultNullOrUndefined(&formatValue))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE), errmsg(
							"A format must be clearly specified when performing a conversion to 'binData'.")));
	}

	if (formatValue.value_type != BSON_TYPE_UTF8)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_CONVERSIONFAILURE), errmsg(
							"Invalid format value provided for $convert: %s",
							BsonTypeName(formatValue.value_type))));
	}

	const char *format = formatValue.value.v_utf8.str;
	if (strcmp(format, "base64") != 0 && strcmp(format, "base64url") != 0 &&
		strcmp(format, "hex") != 0 && strcmp(format, "uuid") != 0 &&
		strcmp(format, "utf8") != 0 && strcmp(format, "auto") != 0)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_CONVERSIONFAILURE), errmsg(
							"Invalid format value provided for $convert: %s",
							format)));
	}
}


/* Helper to apply the conversion to the 'toType'. Throws an error if no onError expression was specified. */
static void
ApplyDollarConvert(List *arguments, const AggregationExpressionData *onErrorData,
				   bson_value_t *result, bool *hasError)
{
	MemoryContext savedMemoryContext = CurrentMemoryContext;

	PG_TRY();
	{
		ProcessDollarConvert(arguments, result);
	}
	PG_CATCH();
	{
		/* onError was not specified, rethrow the error. */
		if (onErrorData == NULL)
		{
			PG_RE_THROW();
		}

		MemoryContextSwitchTo(savedMemoryContext);
		FlushErrorState();

		/* if onError is specified but was a field path expression
		 * and the path was not found, we should return no result. */
		*hasError = true;
	}
	PG_END_TRY();
}


/* Converts a string to an int32 and if not possible throws an exception for the $convert operator. */
static int32_t
ConvertStringToInt32(const bson_value_t *value)
{
	Assert(value->value_type == BSON_TYPE_UTF8);

	int64_t result = ConvertStringToInt64(value);

	if (result < INT32_MIN || result > INT32_MAX)
	{
		ThrowFailedToParseNumberError(value->value.v_utf8.str, "Overflow");
	}

	return (int32_t) result;
}


/* Converts a string to an int64 and if not possible throws an exception for the $convert operator. */
static int64_t
ConvertStringToInt64(const bson_value_t *value)
{
	Assert(value->value_type == BSON_TYPE_UTF8);

	char *str = value->value.v_utf8.str;
	uint32_t len = value->value.v_utf8.len;

	if (len == 0)
	{
		ThrowFailedToParseNumberError(str, "No digits");
	}

	if (*str == ' ')
	{
		ThrowFailedToParseNumberError(str, "Failed to process the entire string.");
	}

	ValidateStringIsNotHexBase(value);

	int base = 10;
	char *endptr = str;
	int64_t result = strtoll(str, &endptr, base);

	if (endptr != (str + len))
	{
		ThrowFailedToParseNumberError(str, "Failed to process the entire string.");
	}

	if ((result == INT64_MAX || result == INT64_MIN) && errno == ERANGE)
	{
		ThrowFailedToParseNumberError(str, "Overflow");
	}

	return result;
}


/* Converts a string to a double and if not possible throws an exception for the $convert operator. */
static double
ConvertStringToDouble(const bson_value_t *value)
{
	Assert(value->value_type == BSON_TYPE_UTF8);

	bson_value_t decimalResult = {
		.value_type = BSON_TYPE_DECIMAL128,
		.value.v_decimal128 = ConvertStringToDecimal128(value),
	};

	if (!IsDecimal128InDoubleRange(&decimalResult))
	{
		ThrowFailedToParseNumberError(value->value.v_utf8.str,
									  "Value exceeds allowed range");
	}

	return GetBsonDecimal128AsDouble(&decimalResult);
}


/* Converts a string to an int32 and if not possible throws an exception for the $convert operator. */
static bson_decimal128_t
ConvertStringToDecimal128(const bson_value_t *value)
{
	Assert(value->value_type == BSON_TYPE_UTF8);

	char *str = value->value.v_utf8.str;
	uint32_t len = value->value.v_utf8.len;

	if (len == 0)
	{
		ThrowFailedToParseNumberError(str, "Empty string");
	}

	ValidateStringIsNotHexBase(value);

	bson_decimal128_t dec128;
	if (!bson_decimal128_from_string_w_len(str, len, &dec128))
	{
		ThrowFailedToParseNumberError(str,
									  "Unable to convert the provided string into a valid decimal format");
	}

	return dec128;
}


/* Performs validation that the provided string doesn't represent a hex number.
 * Only lowercase 'x' is considered a valid hexadecimal prefix in this context. */
static void
ValidateStringIsNotHexBase(const bson_value_t *value)
{
	Assert(value->value_type == BSON_TYPE_UTF8);

	/* Validation checks for lowercase 'x' following a leading '0'. */
	if (value->value.v_utf8.len >= 2 &&
		value->value.v_utf8.str[0] == '0' &&
		value->value.v_utf8.str[1] == 'x')
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_CONVERSIONFAILURE), errmsg(
							"Invalid hexadecimal input detected in $convert without a specified onError handler: %s",
							value->value.v_utf8.str)));
	}
}


/* Validates if a value is NaN or -Infinity/Infinity, and throws if so. */
static void
ValidateValueIsNotNaNOrInfinity(const bson_value_t *value)
{
	if (IsBsonValueNaN(value) || IsBsonValueInfinity(value) != 0)
	{
		const char *sourceValue = IsBsonValueNaN(value) ? "NaN" : "infinity";
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_CONVERSIONFAILURE), errmsg(
							"Attempt to convert %s value to integer type in $convert with no onError value",
							sourceValue)));
	}
}


/* Checks if 'string' is a valid UUID.
 * Valid UUID string has length UUID_STRING_LEN (36), has '-' at indices 8, 13, 18, 23
 */
static bool
ValidateUUIDString(const char *uuidString)
{
	if (strlen(uuidString) != UUID_STRING_LEN)
	{
		return false;
	}

	int underScoreInds[] = { 8, 13, 18, 23 };
	for (int i = 0; i < 4; i++)
	{
		if (uuidString[underScoreInds[i]] != '-')
		{
			return false;
		}
	}
	return true;
}


/* Convert a UUID string to PG's UUID format using pg_uuid_in */
static void
ConvertUUIDStringToPgUUID(const char *uuidString, bson_value_t *result)
{
	/* Use PostgreSQL uuid_in function to parse and validate the UUID */
	Datum uuidDatum = CStringGetDatum(uuidString);
	Datum uuidResult = DirectFunctionCall1(uuid_in, uuidDatum);
	pg_uuid_t *uuid = DatumGetUUIDP(uuidResult);

	result->value_type = BSON_TYPE_BINARY;
	result->value.v_binary.subtype = BSON_SUBTYPE_UUID;
	result->value.v_binary.data = (uint8_t *) palloc(16);
	result->value.v_binary.data_len = 16;

	memcpy(result->value.v_binary.data, uuid->data, 16);
}


/* Throws an invalid conversion error with the sourceType and targetType in the error message. */
static inline void
pg_attribute_noreturn()
ThrowInvalidConversionError(bson_type_t sourceType, bson_type_t targetType)
{
	/* Only target type name can be "missing". */
	const char *targetTypeName = BsonTypeNameExtended(targetType);

	ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_CONVERSIONFAILURE), errmsg(
						"Unsupported conversion from %s to %s in $convert with no onError value",
						BsonTypeNameExtended(sourceType), targetTypeName)));
}


/* Throws an overflow error with the value that was tried to be converted in the message. */
static inline void
pg_attribute_noreturn()
ThrowOverflowTargetError(const bson_value_t * value)
{
	ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_CONVERSIONFAILURE),
					errmsg(
						"Conversion would overflow target type in $convert with no onError value: %s",
						BsonValueToJsonForLogging(value)),
					errdetail_log(
						"Conversion would overflow target type in $convert with no onError value type: %s",
						BsonTypeName(value->value_type))));
}


/* Throws an error for when an input string is not able to be parsed as a number. */
static inline void
pg_attribute_noreturn()
ThrowFailedToParseNumberError(const char * value, const char * reason)
{
	ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_CONVERSIONFAILURE), errmsg(
						"Unable to interpret number '%s' within $convert, as no onError value was specified: %s",
						value, reason)));
}


/* Throws an error when a formatted string is not successfully converted to a BinData. */
static inline void
pg_attribute_noreturn()
ThrowFailedToParseBinDataError(const char * value, const char * reason)
{
	ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE), errmsg(
						"Unable to interpret BinData '%s' during $convert operation due to missing onError parameter: %s",
						value, reason)));
}


/* Throws an error when a deprecated bson_subtype_t (2 or 3) are used. */
static inline void
pg_attribute_noreturn()
ThrowFailedToParseBinDataDeprecatedSubTypeError(int deprecatedSubType, int
												supportedSubType)
{
	ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE), errmsg(
						"Failed to parse BinData with deprecated subtype %d in $convert with no onError value: Use subtype %d instead.",
						deprecatedSubType, supportedSubType)));
}
