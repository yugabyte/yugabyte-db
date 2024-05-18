/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bson/bson_expression_type_operators.c
 *
 * Type Operator expression implementations of BSON.
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>

#include "io/helio_bson_core.h"
#include "operators/bson_expression.h"
#include "operators/bson_expression_operators.h"
#include "types/decimal128.h"


/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */
static bool ProcessDollarIsNumberElement(bson_value_t *result,
										 const bson_value_t *currentElement,
										 bool isFieldPathExpression, void *state);
static bool ProcessDollarTypeElement(bson_value_t *result,
									 const bson_value_t *currentElement,
									 bool isFieldPathExpression, void *state);
static bool ProcessDollarToBoolElement(bson_value_t *result,
									   const bson_value_t *currentElement,
									   bool isFieldPathExpression, void *state);
static bool ProcessDollarToObjectIdElement(bson_value_t *result,
										   const bson_value_t *currentElement,
										   bool isFieldPathExpression, void *state);
static bool ProcessDollarToIntElement(bson_value_t *result,
									  const bson_value_t *currentElement,
									  bool isFieldPathExpression, void *state);
static bool ProcessDollarToLongElement(bson_value_t *result,
									   const bson_value_t *currentElement,
									   bool isFieldPathExpression, void *state);
static bool ProcessDollarToStringElement(bson_value_t *result,
										 const bson_value_t *currentElement,
										 bool isFieldPathExpression, void *state);
static bool ProcessDollarToDateElement(bson_value_t *result,
									   const bson_value_t *currentElement,
									   bool isFieldPathExpression, void *state);
static bool ProcessDollarToDoubleElement(bson_value_t *result,
										 const bson_value_t *currentElement,
										 bool isFieldPathExpression, void *state);
static bool ProcessDollarToDecimalElement(bson_value_t *result,
										  const bson_value_t *currentElement,
										  bool isFieldPathExpression, void *state);
static int32_t ConvertStringToInt32(const bson_value_t *value);
static int64_t ConvertStringToInt64(const bson_value_t *value);
static double ConvertStringToDouble(const bson_value_t *value);
static bson_decimal128_t ConvertStringToDecimal128(const bson_value_t *value);
static void ProcessDollarConvert(bson_value_t *result,
								 const bson_value_t *input,
								 bson_type_t toType);
static void ValidateStringIsNotHexBase(const bson_value_t *value);
static void ValidateValueIsNotNaNOrInfinity(const bson_value_t *value);
static void ThrowInvalidNumArgsConversionOperator(const char *operatorName,
												  int requiredArgs, int numArgs);


/* Throws an invalid conversion error with the sourceType and targetType in the error message. */
static inline void
pg_attribute_noreturn()
ThrowInvalidConversionError(bson_type_t sourceType, bson_type_t targetType)
{
	/* Only target type name can be "missing". */
	const char *targetTypeName = targetType == BSON_TYPE_EOD ?
								 MISSING_TYPE_NAME : BsonTypeName(targetType);

	ereport(ERROR, (errcode(MongoConversionFailure), errmsg(
						"Unsupported conversion from %s to %s in $convert with no onError value",
						BsonTypeName(sourceType), targetTypeName)));
}


/* Throws an overflow error with the value that was tried to be converted in the message. */
static inline void
pg_attribute_noreturn()
ThrowOverflowTargetError(const bson_value_t * value)
{
	ereport(ERROR, (errcode(MongoConversionFailure),
					errmsg(
						"Conversion would overflow target type in $convert with no onError value: %s",
						BsonValueToJsonForLogging(value)),
					errhint(
						"Conversion would overflow target type in $convert with no onError value type: %s",
						BsonTypeName(value->value_type))));
}

/* Throws an error for when an input string is not able to be parsed as a number. */
static inline void
pg_attribute_noreturn()
ThrowFailedToParseNumber(const char * value, const char * reason)
{
	ereport(ERROR, (errcode(MongoConversionFailure), errmsg(
						"Failed to parse number '%s' in $convert with no onError value: %s",
						value, reason)));
}


/*
 * Evaluates the output of a $isNumber expression.
 * Since a $isNumber is expressed as { "$isNumber": <expression> }
 * We evaluate the inner expression and then return a bool indicating if it resolves to an Integer, Decimal, Double, or Long.
 */
void
HandleDollarIsNumber(pgbson *doc, const bson_value_t *operatorValue,
					 ExpressionResult *expressionResult)
{
	ExpressionArgumentHandlingContext context =
	{
		.processElementFunc = ProcessDollarIsNumberElement,
		.processExpressionResultFunc = NULL,
		.state = NULL,
	};

	int numberOfRequiredArgs = 1;
	HandleFixedArgumentExpression(doc, operatorValue, expressionResult,
								  numberOfRequiredArgs, "$isNumber", &context);
}


/*
 * Evaluates the output of a $type expression.
 * Since a $type is expressed as { "$type": <expression> }
 * We evaluate the inner expression and then return a string indicating the name of the underlying type.
 */
void
HandleDollarType(pgbson *doc, const bson_value_t *operatorValue,
				 ExpressionResult *expressionResult)
{
	ExpressionArgumentHandlingContext context =
	{
		.processElementFunc = ProcessDollarTypeElement,
		.processExpressionResultFunc = NULL,
		.state = NULL,
	};

	int numberOfRequiredArgs = 1;
	HandleFixedArgumentExpression(doc, operatorValue, expressionResult,
								  numberOfRequiredArgs, "$type", &context);
}


/*
 * Evaluates the output of a $toBool expression.
 * Since a $toBool is expressed as { "$toBool": <expression> }
 * We evaluate the inner expression and then return a bool represeting its boolean representation per:
 */
void
HandleDollarToBool(pgbson *doc, const bson_value_t *operatorValue,
				   ExpressionResult *expressionResult)
{
	ExpressionArgumentHandlingContext context =
	{
		.processElementFunc = ProcessDollarToBoolElement,
		.processExpressionResultFunc = NULL,
		.throwErrorInvalidNumberOfArgsFunc = ThrowInvalidNumArgsConversionOperator,
		.state = NULL,
	};

	int numberOfRequiredArgs = 1;
	HandleFixedArgumentExpression(doc, operatorValue, expressionResult,
								  numberOfRequiredArgs, "$toBool", &context);
}


/*
 * Evaluates the output of a $toObjectId expression.
 * Since a $toObjectId is expressed as { "$toObjectId": <strExpression> }
 * We evaluate the inner expression, validate it is a valid oid string representation, and return the object id.
 */
void
HandleDollarToObjectId(pgbson *doc, const bson_value_t *operatorValue,
					   ExpressionResult *expressionResult)
{
	ExpressionArgumentHandlingContext context =
	{
		.processElementFunc = ProcessDollarToObjectIdElement,
		.processExpressionResultFunc = NULL,
		.throwErrorInvalidNumberOfArgsFunc = ThrowInvalidNumArgsConversionOperator,
		.state = NULL,
	};

	int numberOfRequiredArgs = 1;
	HandleFixedArgumentExpression(doc, operatorValue, expressionResult,
								  numberOfRequiredArgs, "$toObjectId", &context);
}


/*
 * Evaluates the output of a $toInt expression.
 * Since a $toInt is expressed as { "$toInt": <expression> }
 * We evaluate the inner expression and return the int32 representation if possible.
 */
void
HandleDollarToInt(pgbson *doc, const bson_value_t *operatorValue,
				  ExpressionResult *expressionResult)
{
	ExpressionArgumentHandlingContext context =
	{
		.processElementFunc = ProcessDollarToIntElement,
		.processExpressionResultFunc = NULL,
		.throwErrorInvalidNumberOfArgsFunc = ThrowInvalidNumArgsConversionOperator,
		.state = NULL,
	};

	int numberOfRequiredArgs = 1;
	HandleFixedArgumentExpression(doc, operatorValue, expressionResult,
								  numberOfRequiredArgs, "$toInt", &context);
}


/*
 * Evaluates the output of a $toLong expression.
 * Since a $toLong is expressed as { "$toLong": <expression> }
 * We evaluate the inner expression and return the int64 representation if possible.
 */
void
HandleDollarToLong(pgbson *doc, const bson_value_t *operatorValue,
				   ExpressionResult *expressionResult)
{
	ExpressionArgumentHandlingContext context =
	{
		.processElementFunc = ProcessDollarToLongElement,
		.processExpressionResultFunc = NULL,
		.throwErrorInvalidNumberOfArgsFunc = ThrowInvalidNumArgsConversionOperator,
		.state = NULL,
	};

	int numberOfRequiredArgs = 1;
	HandleFixedArgumentExpression(doc, operatorValue, expressionResult,
								  numberOfRequiredArgs, "$toLong", &context);
}


/*
 * Evaluates the output of a $toDouble expression.
 * Since a $toDouble is expressed as { "$toDouble": <expression> }
 * We evaluate the inner expression and return the double representation if possible.
 */
void
HandleDollarToDouble(pgbson *doc, const bson_value_t *operatorValue,
					 ExpressionResult *expressionResult)
{
	ExpressionArgumentHandlingContext context =
	{
		.processElementFunc = ProcessDollarToDoubleElement,
		.processExpressionResultFunc = NULL,
		.throwErrorInvalidNumberOfArgsFunc = ThrowInvalidNumArgsConversionOperator,
		.state = NULL,
	};

	int numberOfRequiredArgs = 1;
	HandleFixedArgumentExpression(doc, operatorValue, expressionResult,
								  numberOfRequiredArgs, "$toDouble", &context);
}


/*
 * Evaluates the output of a $toDecimal expression.
 * Since a $toDecimal is expressed as { "$toDecimal": <expression> }
 * We evaluate the inner expression and return the value as a decimal128.
 */
void
HandleDollarToDecimal(pgbson *doc, const bson_value_t *operatorValue,
					  ExpressionResult *expressionResult)
{
	ExpressionArgumentHandlingContext context =
	{
		.processElementFunc = ProcessDollarToDecimalElement,
		.processExpressionResultFunc = NULL,
		.throwErrorInvalidNumberOfArgsFunc = ThrowInvalidNumArgsConversionOperator,
		.state = NULL,
	};

	int numberOfRequiredArgs = 1;
	HandleFixedArgumentExpression(doc, operatorValue, expressionResult,
								  numberOfRequiredArgs, "$toDecimal", &context);
}


/*
 * Evaluates the output of a $toString expression.
 * Since a $toString is expressed as { "$toString": <expression> }
 * We evaluate the inner expression and return its string representation if possible.
 */
void
HandleDollarToString(pgbson *doc, const bson_value_t *operatorValue,
					 ExpressionResult *expressionResult)
{
	ExpressionArgumentHandlingContext context =
	{
		.processElementFunc = ProcessDollarToStringElement,
		.processExpressionResultFunc = NULL,
		.throwErrorInvalidNumberOfArgsFunc = ThrowInvalidNumArgsConversionOperator,
		.state = NULL,
	};

	int numberOfRequiredArgs = 1;
	HandleFixedArgumentExpression(doc, operatorValue, expressionResult,
								  numberOfRequiredArgs, "$toString", &context);
}


/*
 * Evaluates the output of a $toDate expression.
 * Since a $toDate is expressed as { "$toDate": <expression> }
 * We evaluate the inner expression and return the corresponding date time result.
 */
void
HandleDollarToDate(pgbson *doc, const bson_value_t *operatorValue,
				   ExpressionResult *expressionResult)
{
	ExpressionArgumentHandlingContext context =
	{
		.processElementFunc = ProcessDollarToDateElement,
		.processExpressionResultFunc = NULL,
		.throwErrorInvalidNumberOfArgsFunc = ThrowInvalidNumArgsConversionOperator,
		.state = NULL,
	};

	int numberOfRequiredArgs = 1;
	HandleFixedArgumentExpression(doc, operatorValue, expressionResult,
								  numberOfRequiredArgs, "$toDate", &context);
}


/*
 * Evaluates the output of a $convert expression.
 * Since a $convert is expressed as { "$convert": {"input": <expression>, "to": <typeExpression>, [onError: <expression>, onNull: <expression> ] } }
 * $convert is a wrapper around specific $to* conversion operators, we evalute the input and type expression, and call the corresponding
 * $to* handler function. If onError is specified we return that expression in the case of an exception during the conversion, not the parsing of the arguments.
 */
void
HandleDollarConvert(pgbson *doc, const bson_value_t *operatorValue,
					ExpressionResult *expressionResult)
{
	if (operatorValue->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(MongoFailedToParse), errmsg(
							"$convert expects an object of named arguments but found: %s",
							BsonTypeName(operatorValue->value_type))));
	}

	bson_value_t inputExpression = { 0 };
	bson_value_t toExpression = { 0 };
	bson_value_t onErrorExpression = { 0 };
	bson_value_t onNullExpression = { 0 };

	bson_iter_t docIter;
	BsonValueInitIterator(operatorValue, &docIter);
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
			ereport(ERROR, (errcode(MongoFailedToParse), errmsg(
								"$convert found an unknown argument: %s",
								key)));
		}
	}

	if (inputExpression.value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(MongoFailedToParse), errmsg(
							"Missing 'input' parameter to $convert")));
	}

	if (toExpression.value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(MongoFailedToParse), errmsg(
							"Missing 'to' parameter to $convert")));
	}

	/* In Native Mongo, onError and onNull expressions are evaluated first,
	 * regardless of if they are going to be needed or not. */
	bson_value_t onErrorEvaluatedValue = { 0 };
	volatile bool hasError = false;
	bool isNullOnEmpty = false;
	if (onErrorExpression.value_type != BSON_TYPE_EOD)
	{
		onErrorEvaluatedValue =
			EvaluateExpressionAndGetValue(doc, &onErrorExpression, expressionResult,
										  isNullOnEmpty);
	}

	bson_value_t onNullEvaluatedValue = { 0 };
	if (onNullExpression.value_type != BSON_TYPE_EOD)
	{
		onNullEvaluatedValue =
			EvaluateExpressionAndGetValue(doc, &onNullExpression, expressionResult,
										  isNullOnEmpty);
	}

	/* Native mongo evaluates them in this order. */
	bson_value_t toEvaluatedValue =
		EvaluateExpressionAndGetValue(doc, &toExpression, expressionResult,
									  isNullOnEmpty);

	bson_value_t inputEvaluatedValue =
		EvaluateExpressionAndGetValue(doc, &inputExpression, expressionResult,
									  isNullOnEmpty);

	bson_type_t toType;

	/* To match native mongo, we must first validate the 'to' argument is a valid type. */
	if (toEvaluatedValue.value_type == BSON_TYPE_UTF8)
	{
		const char *typeName = toEvaluatedValue.value.v_utf8.str;
		uint32_t len = toEvaluatedValue.value.v_utf8.len;
		if (len == 7 && strcmp(typeName, MISSING_TYPE_NAME) == 0)
		{
			/* Should support 'missing' as a valid type for $convert.
			 * We convert it to EOD as there is no valid conversion from any type
			 * to 'missing', and we will handle this when throwing the error. */
			toType = BSON_TYPE_EOD;
		}
		else
		{
			toType = BsonTypeFromName(toEvaluatedValue.value.v_utf8.str);
		}
	}
	else if (BsonValueIsNumber(&toEvaluatedValue))
	{
		if (!IsBsonValueFixedInteger(&toEvaluatedValue))
		{
			ereport(ERROR, (errcode(MongoFailedToParse), errmsg(
								"In $convert, numeric 'to' argument is not an integer")));
		}

		int64_t typeCode = BsonValueAsInt64(&toEvaluatedValue);

		if (!TryGetTypeFromInt64(typeCode, &toType))
		{
			ereport(ERROR, (errcode(MongoFailedToParse), errmsg(
								"In $convert, numeric value for 'to' does not correspond to a BSON type: %lld",
								(long long int) typeCode)));
		}
	}
	else if (!IsExpressionResultNullOrUndefined(&toEvaluatedValue))
	{
		/* If the 'to' evaluated value is null or undefined, we should return null, not an error.
		 * however, we can't do it here yet, as if the 'input' expression evaluates to null and onNull is specified,
		 * we must return that instead. */
		ereport(ERROR, (errcode(MongoFailedToParse), errmsg(
							"$convert's 'to' argument must be a string or number, but is %s",
							BsonTypeName(toEvaluatedValue.value_type))));
	}

	bson_value_t nullValue = {
		.value_type = BSON_TYPE_NULL,
	};

	/* Null 'input' argument takes presedence over null 'to' argument. */
	if (IsExpressionResultNullOrUndefined(&inputEvaluatedValue))
	{
		/* onNull was not specified. Just set it to null */
		if (onNullExpression.value_type == BSON_TYPE_EOD)
		{
			ExpressionResultSetValue(expressionResult, &nullValue);
			return;
		}

		/* if onNull is specified but was a field path expression
		 * and the path was not found, we should return no result. */
		if (onNullEvaluatedValue.value_type == BSON_TYPE_EOD)
		{
			return;
		}

		ExpressionResultSetValue(expressionResult, &onNullEvaluatedValue);
		return;
	}

	if (IsExpressionResultNullOrUndefined(&toEvaluatedValue))
	{
		ExpressionResultSetValue(expressionResult, &nullValue);
		return;
	}

	bson_value_t result;

	MemoryContext savedMemoryContext = CurrentMemoryContext;

	/* If onError is specified, we shouldn't throw, but instead return that value. */
	PG_TRY();
	{
		ProcessDollarConvert(&result, &inputEvaluatedValue, toType);
	}
	PG_CATCH();
	{
		/* onError was not specified, rethrow the error. */
		if (onErrorExpression.value_type == BSON_TYPE_EOD)
		{
			PG_RE_THROW();
		}

		MemoryContextSwitchTo(savedMemoryContext);
		FlushErrorState();

		/* if onError is specified but was a field path expression
		 * and the path was not found, we should return no result. */
		hasError = true;
	}
	PG_END_TRY();

	if (hasError)
	{
		if (onErrorEvaluatedValue.value_type == BSON_TYPE_EOD)
		{
			return;
		}

		result = onErrorEvaluatedValue;
	}

	ExpressionResultSetValue(expressionResult, &result);
}


/* --------------------------------------------------------- */
/* private helper methods. */
/* --------------------------------------------------------- */


/* Helper function that based on the toType argument, calls the underlying $to* convert handler. */
static void
ProcessDollarConvert(bson_value_t *result, const bson_value_t *input, bson_type_t toType)
{
	bool isFieldPathExpressionIgnore = false;

	switch (toType)
	{
		case BSON_TYPE_DOUBLE:
		{
			ProcessDollarToDoubleElement(result, input,
										 isFieldPathExpressionIgnore, NULL);
			break;
		}

		case BSON_TYPE_UTF8:
		{
			ProcessDollarToStringElement(result, input,
										 isFieldPathExpressionIgnore, NULL);
			break;
		}

		case BSON_TYPE_OID:
		{
			ProcessDollarToObjectIdElement(result, input,
										   isFieldPathExpressionIgnore, NULL);
			break;
		}

		case BSON_TYPE_BOOL:
		{
			ProcessDollarToBoolElement(result, input,
									   isFieldPathExpressionIgnore, NULL);
			break;
		}

		case BSON_TYPE_DATE_TIME:
		{
			ProcessDollarToDateElement(result, input, isFieldPathExpressionIgnore, NULL);
			break;
		}

		case BSON_TYPE_INT32:
		{
			ProcessDollarToIntElement(result, input, isFieldPathExpressionIgnore, NULL);
			break;
		}

		case BSON_TYPE_INT64:
		{
			ProcessDollarToLongElement(result, input, isFieldPathExpressionIgnore, NULL);
			break;
		}

		case BSON_TYPE_DECIMAL128:
		{
			ProcessDollarToDecimalElement(result, input,
										  isFieldPathExpressionIgnore, NULL);
			break;
		}

		default:
		{
			ThrowInvalidConversionError(input->value_type, toType);
		}
	}
}


/* Process the evaluated expression for $isNumber and sets the result to a bool indicating if the element is a number or not. */
static bool
ProcessDollarIsNumberElement(bson_value_t *result, const bson_value_t *currentElement,
							 bool isFieldPathExpression, void *state)
{
	result->value_type = BSON_TYPE_BOOL;
	result->value.v_bool = BsonValueIsNumber(currentElement);
	return true;
}


/* Proccess the evaluated expression for $type and set the result to the resolved type name for it.
 * If the expression evaluation resulted in an EOD (missing path in the current document) the type name is set to 'missing'. */
static bool
ProcessDollarTypeElement(bson_value_t *result, const bson_value_t *currentElement,
						 bool isFieldPathExpression, void *state)
{
	bson_type_t type = currentElement->value_type;

	/* We need to cover the case where the expression is a field path and it doesn't exist, native Mongo returns 'missing'.
	 * However, 'missing' is not a valid type name for other ops, so we cover here rather than in the common BsonTypeName method. */
	char *name = type == BSON_TYPE_EOD ?
				 MISSING_TYPE_NAME : BsonTypeName(type);

	result->value_type = BSON_TYPE_UTF8;
	result->value.v_utf8.str = name;
	result->value.v_utf8.len = strlen(name);
	return true;
}


/* Process the evaluated expression for $toBool and returns its bool equivalent.
 * If null or undefined, the result should be null. */
static bool
ProcessDollarToBoolElement(bson_value_t *result, const bson_value_t *currentElement,
						   bool isFieldPathExpression, void *state)
{
	if (IsExpressionResultNullOrUndefined(currentElement))
	{
		result->value_type = BSON_TYPE_NULL;
	}
	else
	{
		result->value_type = BSON_TYPE_BOOL;
		result->value.v_bool = BsonValueAsBool(currentElement);
	}

	return true;
}


/* Process the evaluated expression for $toObjectId.
 * If null or undefined, the result should be null. */
static bool
ProcessDollarToObjectIdElement(bson_value_t *result, const bson_value_t *currentElement,
							   bool isFieldPathExpression, void *state)
{
	if (IsExpressionResultNullOrUndefined(currentElement))
	{
		result->value_type = BSON_TYPE_NULL;
		return true;
	}

	if (currentElement->value_type == BSON_TYPE_OID)
	{
		*result = *currentElement;
		return true;
	}

	if (currentElement->value_type != BSON_TYPE_UTF8)
	{
		ThrowInvalidConversionError(currentElement->value_type, BSON_TYPE_OID);
	}

	const char *str = currentElement->value.v_utf8.str;
	uint32_t length = currentElement->value.v_utf8.len;
	if (length != 24)
	{
		ereport(ERROR, (errcode(MongoConversionFailure), errmsg(
							"Failed to parse objectId '%s' in $convert with no onError value: Invalid string length for parsing to OID, expected 24 but found %d",
							str, length)));
	}

	/* We could use bson_oid_is_valid but we would need to get the invalid char anyways, so we just validate it ourselves. */
	uint32_t i;
	for (i = 0; i < length; i++)
	{
		if (!isxdigit(str[i]))
		{
			ereport(ERROR, (errcode(MongoConversionFailure), errmsg(
								"Failed to parse objectId '%s' in $convert with no onError value: Invalid character found in hex string: '%c'",
								str, str[i])));
		}
	}

	result->value_type = BSON_TYPE_OID;
	bson_oid_init_from_string(&result->value.v_oid, str);
	return true;
}


/* Process the evaluated expression for $toInt.
 * If null or undefined, the result should be null. */
static bool
ProcessDollarToIntElement(bson_value_t *result, const bson_value_t *currentElement,
						  bool isFieldPathExpression, void *state)
{
	if (IsExpressionResultNullOrUndefined(currentElement))
	{
		result->value_type = BSON_TYPE_NULL;
		return true;
	}

	switch (currentElement->value_type)
	{
		/* Don't use BsonValueAsInt32 as we have validation and errors that are very
		 * specific to the convert aggregation operators. */
		case BSON_TYPE_BOOL:
		{
			result->value.v_int32 = (int32_t) currentElement->value.v_bool;
			break;
		}

		case BSON_TYPE_INT32:
		{
			result->value.v_int32 = currentElement->value.v_int32;
			break;
		}

		case BSON_TYPE_DOUBLE:
		case BSON_TYPE_DECIMAL128:
		{
			ValidateValueIsNotNaNOrInfinity(currentElement);

			/* If not NaN or Infinity/-Infinity fall over to BSON_TYPE_INT64 branch. */
		}

		case BSON_TYPE_INT64:
		{
			bool checkFixedInteger = false;
			if (!IsBsonValue32BitInteger(currentElement, checkFixedInteger))
			{
				ThrowOverflowTargetError(currentElement);
			}

			result->value.v_int32 = BsonValueAsInt32(currentElement);
			break;
		}

		case BSON_TYPE_UTF8:
		{
			result->value.v_int32 = ConvertStringToInt32(currentElement);
			break;
		}

		default:
		{
			ThrowInvalidConversionError(currentElement->value_type, BSON_TYPE_INT32);
			break;
		}
	}

	result->value_type = BSON_TYPE_INT32;
	return true;
}


/* Process the evaluated expression for $toLong.
 * If null or undefined, the result should be null. */
static bool
ProcessDollarToLongElement(bson_value_t *result, const bson_value_t *currentElement,
						   bool isFieldPathExpression, void *state)
{
	if (IsExpressionResultNullOrUndefined(currentElement))
	{
		result->value_type = BSON_TYPE_NULL;
		return true;
	}

	switch (currentElement->value_type)
	{
		case BSON_TYPE_BOOL:
		case BSON_TYPE_INT32:
		case BSON_TYPE_INT64:
		case BSON_TYPE_DATE_TIME:
		{
			result->value.v_int64 = BsonValueAsInt64(currentElement);
			break;
		}

		/* For these types there are checks and errors specific to convert operators
		 * so we don't use BsonValueAsInt64. */
		case BSON_TYPE_DOUBLE:
		case BSON_TYPE_DECIMAL128:
		{
			ValidateValueIsNotNaNOrInfinity(currentElement);
			bool checkFixedInteger = false;
			if (!IsBsonValue64BitInteger(currentElement, checkFixedInteger))
			{
				ThrowOverflowTargetError(currentElement);
			}

			result->value.v_int64 = BsonValueAsInt64(currentElement);
			break;
		}

		case BSON_TYPE_UTF8:
		{
			result->value.v_int64 = ConvertStringToInt64(currentElement);
			break;
		}

		default:
		{
			ThrowInvalidConversionError(currentElement->value_type, BSON_TYPE_INT64);
			break;
		}
	}

	result->value_type = BSON_TYPE_INT64;
	return true;
}


/* Process the evaluated expression for $toDouble.
 * If null or undefined, the result should be null. */
static bool
ProcessDollarToDoubleElement(bson_value_t *result, const bson_value_t *currentElement,
							 bool isFieldPathExpression, void *state)
{
	if (IsExpressionResultNullOrUndefined(currentElement))
	{
		result->value_type = BSON_TYPE_NULL;
		return true;
	}

	switch (currentElement->value_type)
	{
		case BSON_TYPE_DOUBLE:
		case BSON_TYPE_BOOL:
		case BSON_TYPE_INT32:
		case BSON_TYPE_INT64:
		case BSON_TYPE_DATE_TIME:
		{
			result->value.v_double = BsonValueAsDouble(currentElement);
			break;
		}

		/* Don't use BsonValueAsDouble for these two types as this require validation and behaviors
		 * that are very specific to the convert operators. */
		case BSON_TYPE_DECIMAL128:
		{
			if (!IsDecimal128InDoubleRange(currentElement))
			{
				ThrowOverflowTargetError(currentElement);
			}

			result->value.v_double = GetBsonDecimal128AsDouble(currentElement);
			break;
		}

		case BSON_TYPE_UTF8:
		{
			result->value.v_double = ConvertStringToDouble(currentElement);
			break;
		}

		default:
		{
			ThrowInvalidConversionError(currentElement->value_type, BSON_TYPE_DOUBLE);
			break;
		}
	}

	result->value_type = BSON_TYPE_DOUBLE;
	return true;
}


/* Process the evaluated expression for $toDecimal.
 * If null or undefined, the result should be null. */
static bool
ProcessDollarToDecimalElement(bson_value_t *result, const bson_value_t *currentElement,
							  bool isFieldPathExpression, void *state)
{
	if (IsExpressionResultNullOrUndefined(currentElement))
	{
		result->value_type = BSON_TYPE_NULL;
		return true;
	}

	switch (currentElement->value_type)
	{
		case BSON_TYPE_BOOL:
		case BSON_TYPE_INT32:
		case BSON_TYPE_INT64:
		case BSON_TYPE_DOUBLE:
		case BSON_TYPE_DECIMAL128:
		case BSON_TYPE_DATE_TIME:
		{
			result->value.v_decimal128 = GetBsonValueAsDecimal128Quantized(
				currentElement);
			break;
		}

		/* Don't call GetBsonValueAsDecimal128 as this is very specific to convert
		 * including parsing rules and errors thrown. */
		case BSON_TYPE_UTF8:
		{
			result->value.v_decimal128 = ConvertStringToDecimal128(currentElement);
			break;
		}

		default:
		{
			ThrowInvalidConversionError(currentElement->value_type, BSON_TYPE_DECIMAL128);
			break;
		}
	}

	result->value_type = BSON_TYPE_DECIMAL128;
	return true;
}


/*
 * Parses a $makeArray expression.
 * This has the syntax:
 * { "$makeArray": $expression }
 * if the expression evaluates to undefined, then writes empty array.
 * If the expression evaluates to an array, writes it as-is
 * If the expression evaluates to any other value, wraps it in an array.
 */
void
ParseDollarMakeArray(const bson_value_t *inputDocument,
					 AggregationExpressionData *data,
					 const ExpressionVariableContext *variableContext)
{
	AggregationExpressionData *argumentData = palloc0(
		sizeof(AggregationExpressionData));

	ParseAggregationExpressionData(argumentData, inputDocument, variableContext);

	data->operator.arguments = argumentData;
	data->operator.argumentsKind = AggregationExpressionArgumentsKind_Palloc;
}


/*
 * Handles executing a pre-parsed $makeArray Expression.
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


/* Process the evaluated expression for $toString.
 * If null or undefined, the result should be null. */
bool
ProcessDollarToStringElement(bson_value_t *result, const bson_value_t *currentElement,
							 bool isFieldPathExpression, void *state)
{
	if (IsExpressionResultNullOrUndefined(currentElement))
	{
		result->value_type = BSON_TYPE_NULL;
		return true;
	}

	switch (currentElement->value_type)
	{
		case BSON_TYPE_BOOL:
		case BSON_TYPE_INT32:
		case BSON_TYPE_INT64:
		case BSON_TYPE_DOUBLE:
		case BSON_TYPE_DECIMAL128:
		{
			char *str = (char *) BsonValueToJsonForLogging(currentElement);
			result->value.v_utf8.str = str;
			result->value.v_utf8.len = strlen(str);
			break;
		}

		case BSON_TYPE_UTF8:
		{
			*result = *currentElement;
			break;
		}

		case BSON_TYPE_OID:
		{
			char str[25];
			bson_oid_to_string(&currentElement->value.v_oid, str);
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

			int64_t dateInMs = currentElement->value.v_datetime;
			StringView dateStrView =
				GetDateStringWithDefaultFormat(dateInMs, timezone,
											   DateStringFormatCase_UpperCase);
			result->value.v_utf8.str = (char *) dateStrView.string;
			result->value.v_utf8.len = dateStrView.length;
			break;
		}

		default:
		{
			ThrowInvalidConversionError(currentElement->value_type, BSON_TYPE_UTF8);
		}
	}

	result->value_type = BSON_TYPE_UTF8;
	return true;
}


/* Process the evaluated expression for $toDate.
 * If null or undefined, the result should be null. */
bool
ProcessDollarToDateElement(bson_value_t *result, const bson_value_t *currentElement,
						   bool isFieldPathExpression, void *state)
{
	if (IsExpressionResultNullOrUndefined(currentElement))
	{
		result->value_type = BSON_TYPE_NULL;
		return true;
	}

	/* Native mongo doesn't support int32 -> date conversion yet. */
	switch (currentElement->value_type)
	{
		case BSON_TYPE_DOUBLE:
		case BSON_TYPE_DECIMAL128:
		case BSON_TYPE_INT64:
		{
			bool checkFixedInteger = false;
			if (!IsBsonValue64BitInteger(currentElement, checkFixedInteger))
			{
				ThrowOverflowTargetError(currentElement);
			}

			result->value.v_datetime = BsonValueAsInt64(currentElement);
			break;
		}

		case BSON_TYPE_OID:
		case BSON_TYPE_TIMESTAMP:
		case BSON_TYPE_DATE_TIME:
		{
			result->value.v_datetime = BsonValueAsDateTime(currentElement);
			break;
		}

		case BSON_TYPE_UTF8:
		{
			/* TODO: from string, will add with $dateFromString operator. */
		}

		default:
		{
			ThrowInvalidConversionError(currentElement->value_type, BSON_TYPE_DATE_TIME);
		}
	}

	result->value_type = BSON_TYPE_DATE_TIME;
	return true;
}


/* Converts a string to an int32 and if not possible throws an exception for the $convert operator. */
static int32_t
ConvertStringToInt32(const bson_value_t *value)
{
	Assert(value->value_type == BSON_TYPE_UTF8);

	int64_t result = ConvertStringToInt64(value);

	if (result < INT32_MIN || result > INT32_MAX)
	{
		ThrowFailedToParseNumber(value->value.v_utf8.str, "Overflow");
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
		ThrowFailedToParseNumber(str, "No digits");
	}

	if (*str == ' ')
	{
		ThrowFailedToParseNumber(str, "Did not consume whole string.");
	}

	ValidateStringIsNotHexBase(value);

	int base = 10;
	char *endptr = str;
	int64_t result = strtoll(str, &endptr, base);

	if (endptr != (str + len))
	{
		ThrowFailedToParseNumber(str, "Did not consume whole string.");
	}

	if ((result == INT64_MAX || result == INT64_MIN) && errno == ERANGE)
	{
		ThrowFailedToParseNumber(str, "Overflow");
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
		ThrowFailedToParseNumber(value->value.v_utf8.str, "Out of range");
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
		ThrowFailedToParseNumber(str, "Empty string");
	}

	ValidateStringIsNotHexBase(value);

	bson_decimal128_t dec128;
	if (!bson_decimal128_from_string_w_len(str, len, &dec128))
	{
		ThrowFailedToParseNumber(str, "Failed to parse string to decimal");
	}

	return dec128;
}


/* Performs validation that the provided string doesn't represent a hex number.
 * We only check for lowercase 'x' to match native mongo. */
static void
ValidateStringIsNotHexBase(const bson_value_t *value)
{
	Assert(value->value_type == BSON_TYPE_UTF8);

	/* Native mongo only identifies lowercase x as hexadecimal value. */
	if (value->value.v_utf8.len >= 2 &&
		value->value.v_utf8.str[0] == '0' &&
		value->value.v_utf8.str[1] == 'x')
	{
		ereport(ERROR, (errcode(MongoConversionFailure), errmsg(
							"Illegal hexadecimal input in $convert with no onError value: %s",
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
		ereport(ERROR, (errcode(MongoConversionFailure), errmsg(
							"Attempt to convert %s value to integer type in $convert with no onError value",
							sourceValue)));
	}
}


/* Call back to throw an error that the wrong number of args for a type operator was provided. */
static void
ThrowInvalidNumArgsConversionOperator(const char *operatorName, int requiredArgs, int
									  numArgs)
{
	ereport(ERROR, (errcode(MongoLocation50723), errmsg(
						"%s requires a single argument, got %d",
						operatorName, numArgs)));
}
