/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bson/bson_expression_date_operators.c
 *
 * Object Operator expression implementations of BSON.
 * See also: https://www.mongodb.com/docs/manual/reference/operator/aggregation/#date-expression-operators
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <pgtime.h>
#include <fmgr.h>
#include <math.h>
#include <utils/datetime.h>
#include <utils/timestamp.h>
#include <utils/builtins.h>

#include "io/helio_bson_core.h"
#include "operators/bson_expression.h"
#include "operators/bson_expression_operators.h"
#include "utils/mongo_errors.h"
#include "metadata/metadata_cache.h"

#define MILLISECONDS_IN_SECOND 1000L
#define SECONDS_IN_MINUTE 60
#define MINUTES_IN_HOUR 60
#define DAYS_IN_WEEK 7

/* --------------------------------------------------------- */
/* Type declaration */
/* --------------------------------------------------------- */

/* Enum that defines the posible parts of a date/timestamp in Mongo. */
typedef enum DatePart
{
	DatePart_Hour = 0,
	DatePart_Minute = 1,
	DatePart_Second = 2,
	DatePart_Millisecond = 3,
	DatePart_Year = 4,
	DatePart_Month = 5,
	DatePart_Week = 6,
	DatePart_DayOfYear = 7,
	DatePart_DayOfMonth = 8,
	DatePart_DayOfWeek = 9,
	DatePart_IsoWeekYear = 10,
	DatePart_IsoWeek = 11,
	DatePart_IsoDayOfWeek = 12,
} DatePart;

static const char *monthNamesCamelCase[12] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};
static const char *monthNamesUpperCase[12] = {
	"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"
};
static const char *monthNamesLowerCase[12] = {
	"jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec"
};


/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */
static void HandleDatePartOperator(pgbson *doc, const bson_value_t *operatorValue,
								   ExpressionResult *expressionResult,
								   const char *operatorName, DatePart datePart);
static bool ParseDatePartOperatorArgument(const bson_value_t *operatorValue,
										  const char *operatorName,
										  bson_value_t *dateExpression,
										  bson_value_t *timezoneExpression);
static ExtensionTimezone ParseTimezone(StringView timezone);
static int64_t ParseUtcOffset(StringView offset);
static int32_t DetermineUtcOffsetForEpochWithTimezone(int64_t unixEpoch,
													  ExtensionTimezone timezone);
static bool TryParseTwoDigitNumber(StringView str, uint32_t *result);
static uint32_t GetDatePartFromPgTimestamp(Datum pgTimestamp, DatePart datePart);
static Datum GetPgTimestampFromEpochWithTimezone(int64_t epochInMs,
												 ExtensionTimezone timezone);
static Datum GetPgTimestampFromUnixEpoch(int64_t epochInMs);
static StringView GetDateStringWithFormat(int64_t dateInMs, ExtensionTimezone timezone,
										  StringView format);

/* These 3 methods are specialized for creating a date string. */
static void WriteCharAndAdvanceBuffer(char **buffer, const char *end, char value);
static int WriteInt32AndAdvanceBuffer(char **buffer, const char *end, int32_t value);
static void WritePaddedUInt32AndAdvanceBuffer(char **buffer, const char *end, int padding,
											  uint32_t value);


/* Helper method that throws the error for an invalid timezone argument. */
static inline void
pg_attribute_noreturn()
ThrowInvalidTimezoneIdentifier(const char * identifier)
{
	ereport(ERROR, (errcode(MongoLocation40485), errmsg(
						"unrecognized time zone identifier: \"%s\"", identifier)));
}

/* Helper method that throws common Location40517 when a string is not provided for the timezone argument. */
static inline void
pg_attribute_noreturn()
ThrowLocation40517Error(bson_type_t foundType)
{
	ereport(ERROR, (errcode(MongoLocation40517), errmsg(
						"timezone must evaluate to a string, found %s",
						BsonTypeName(foundType)),
					errhint("timezone must evaluate to a string, found %s",
							BsonTypeName(foundType))));
}

/* Helper that validates a date value is in the valid range 0-9999. */
static inline void
ValidateDateValueIsInRange(uint32_t value)
{
	if (value > 9999)
	{
		ereport(ERROR, (errcode(MongoLocation18537), errmsg(
							"Could not convert date to string: date component was outside the supported range of 0-9999: %d",
							value),
						errhint(
							"Could not convert date to string: date component was outside the supported range of 0-9999: %d",
							value)));
	}
}


/*
 * Evaluates the output of a $hour expression.
 * Since a $hour is expressed as { "$hour": <dateExpression> }
 * or { "$hour": { date: <dateExpression>, timezone: <tzExpression> } }
 * We evaluate the inner expression and then return the hour part in the specified date expression with the specified timezone.
 */
void
HandleDollarHour(pgbson *doc, const bson_value_t *operatorValue,
				 ExpressionResult *expressionResult)
{
	HandleDatePartOperator(doc, operatorValue, expressionResult, "$hour", DatePart_Hour);
}


/*
 * Evaluates the output of a $minute expression.
 * Since a $minute is expressed as { "$minute": <dateExpression> }
 * or { "$minute": { date: <dateExpression>, timezone: <tzExpression> } }
 * We evaluate the inner expression and then return the minute part in the specified date expression with the specified timezone.
 */
void
HandleDollarMinute(pgbson *doc, const bson_value_t *operatorValue,
				   ExpressionResult *expressionResult)
{
	HandleDatePartOperator(doc, operatorValue, expressionResult, "$minute",
						   DatePart_Minute);
}


/*
 * Evaluates the output of a $second expression.
 * Since a $second is expressed as { "$second": <dateExpression> }
 * or { "$second": { date: <dateExpression>, timezone: <tzExpression> } }
 * We evaluate the inner expression and then return the second part in the specified date expression with the specified timezone.
 */
void
HandleDollarSecond(pgbson *doc, const bson_value_t *operatorValue,
				   ExpressionResult *expressionResult)
{
	HandleDatePartOperator(doc, operatorValue, expressionResult, "$second",
						   DatePart_Second);
}


/*
 * Evaluates the output of a $millisecond expression.
 * Since a $millisecond is expressed as { "$millisecond": <dateExpression> }
 * or { "$millisecond": { date: <dateExpression>, timezone: <tzExpression> } }
 * We evaluate the inner expression and then return the millisecond part in the specified date expression with the specified timezone.
 */
void
HandleDollarMillisecond(pgbson *doc, const bson_value_t *operatorValue,
						ExpressionResult *expressionResult)
{
	HandleDatePartOperator(doc, operatorValue, expressionResult, "$millisecond",
						   DatePart_Millisecond);
}


/*
 * Evaluates the output of a $year expression.
 * Since a $year is expressed as { "$year": <dateExpression> }
 * or { "$year": { date: <dateExpression>, timezone: <tzExpression> } }
 * We evaluate the inner expression and then return the year part in the specified date expression with the specified timezone.
 */
void
HandleDollarYear(pgbson *doc, const bson_value_t *operatorValue,
				 ExpressionResult *expressionResult)
{
	HandleDatePartOperator(doc, operatorValue, expressionResult, "$year", DatePart_Year);
}


/*
 * Evaluates the output of a $month expression.
 * Since a $month is expressed as { "$month": <dateExpression> }
 * or { "$month": { date: <dateExpression>, timezone: <tzExpression> } }
 * We evaluate the inner expression and then return the month part in the specified date expression with the specified timezone.
 */
void
HandleDollarMonth(pgbson *doc, const bson_value_t *operatorValue,
				  ExpressionResult *expressionResult)
{
	HandleDatePartOperator(doc, operatorValue, expressionResult, "$month",
						   DatePart_Month);
}


/*
 * Evaluates the output of a $week expression.
 * Since a $week is expressed as { "$week": <dateExpression> }
 * or { "$week": { date: <dateExpression>, timezone: <tzExpression> } }
 * We evaluate the inner expression and then return the week number [0-53 (leap year)] in the specified date expression with the specified timezone.
 */
void
HandleDollarWeek(pgbson *doc, const bson_value_t *operatorValue,
				 ExpressionResult *expressionResult)
{
	HandleDatePartOperator(doc, operatorValue, expressionResult, "$week", DatePart_Week);
}


/*
 * Evaluates the output of a $dayOfYear expression.
 * Since a $dayOfYear is expressed as { "$dayOfYear": <dateExpression> }
 * or { "$dayOfYear": { date: <dateExpression>, timezone: <tzExpression> } }
 * We evaluate the inner expression and then return the day of the year [1-366 (leap year)] in the specified date expression with the specified timezone.
 */
void
HandleDollarDayOfYear(pgbson *doc, const bson_value_t *operatorValue,
					  ExpressionResult *expressionResult)
{
	HandleDatePartOperator(doc, operatorValue, expressionResult, "$dayOfYear",
						   DatePart_DayOfYear);
}


/*
 * Evaluates the output of a $dayOfMonth expression.
 * Since a $dayOfMonth is expressed as { "$dayOfMonth": <dateExpression> }
 * or { "$dayOfMonth": { date: <dateExpression>, timezone: <tzExpression> } }
 * We evaluate the inner expression and then return the day of the month [1-31] in the specified date expression with the specified timezone.
 */
void
HandleDollarDayOfMonth(pgbson *doc, const bson_value_t *operatorValue,
					   ExpressionResult *expressionResult)
{
	HandleDatePartOperator(doc, operatorValue, expressionResult, "$dayOfMonth",
						   DatePart_DayOfMonth);
}


/*
 * Evaluates the output of a $dayOfWeek expression.
 * Since a $dayOfWeek is expressed as { "$dayOfWeek": <dateExpression> }
 * or { "$dayOfWeek": { date: <dateExpression>, timezone: <tzExpression> } }
 * We evaluate the inner expression and then return the day of the week [1 (Sunday) - 7 (Saturday)] in the specified date expression with the specified timezone.
 */
void
HandleDollarDayOfWeek(pgbson *doc, const bson_value_t *operatorValue,
					  ExpressionResult *expressionResult)
{
	HandleDatePartOperator(doc, operatorValue, expressionResult, "$dayOfWeek",
						   DatePart_DayOfWeek);
}


/*
 * Evaluates the output of a $isoWeekYear expression.
 * Since a $isoWeekYear is expressed as { "$isoWeekYear": <dateExpression> }
 * or { "$isoWeekYear": { date: <dateExpression>, timezone: <tzExpression> } }
 * We evaluate the inner expression and then return the year based on the ISO 8601 week numbering in the specified date expression with the specified timezone.
 * In ISO 8601 the year starts with the Monday of week 1 and ends with the Sunday of the last week.
 * So in early January or late December the ISO year may be different from the Gregorian year. See HandleDollarIsoWeek summary for more details.
 */
void
HandleDollarIsoWeekYear(pgbson *doc, const bson_value_t *operatorValue,
						ExpressionResult *expressionResult)
{
	HandleDatePartOperator(doc, operatorValue, expressionResult, "$isoWeekYear",
						   DatePart_IsoWeekYear);
}


/*
 * Evaluates the output of a $isoWeek expression.
 * Since a $isoWeek is expressed as { "$isoWeek": <dateExpression> }
 * or { "$isoWeek": { date: <dateExpression>, timezone: <tzExpression> } }
 * We evaluate the inner expression and then return the week based on the ISO 8601 week numbering in the specified date expression with the specified timezone.
 * Iso week start on Mondays and the first week of a year contains January 4 of that year. In other words, the first Thursday
 * of a year is in week 1 of that year. So it is possible for early-January dates to be part of the 52nd or 53rd week
 * of the previous year and for late-December dates to be part of the first week of the next year. i.e: 2005-01-01
 * in ISO 8601 is part of the 53rd week of year 2004
 */
void
HandleDollarIsoWeek(pgbson *doc, const bson_value_t *operatorValue,
					ExpressionResult *expressionResult)
{
	HandleDatePartOperator(doc, operatorValue, expressionResult, "$isoWeek",
						   DatePart_IsoWeek);
}


/*
 * Evaluates the output of a $isoDayOfWeek expression.
 * Since a $isoDayOfWeek is expressed as { "$isoDayOfWeek": <dateExpression> }
 * or { "$isoDayOfWeek": { date: <dateExpression>, timezone: <tzExpression> } }
 * We evaluate the inner expression and then return the day of week based on the ISO 8601 numbering in the specified date expression with the specified timezone.
 * ISO 8601 week start on Mondays (1) and end on Sundays (7).
 */
void
HandleDollarIsoDayOfWeek(pgbson *doc, const bson_value_t *operatorValue,
						 ExpressionResult *expressionResult)
{
	HandleDatePartOperator(doc, operatorValue, expressionResult, "$isoDayOfWeek",
						   DatePart_IsoDayOfWeek);
}


/*
 * Evaluates the output of a $dateToParts expression.
 * Since a $datToParts is expressed as { "$dateToParts": { date: <dateExpression>, [ timezone: <tzExpression>, iso8601: <boolExpression> ] } }
 * We validate the input document and return the date parts from the 'date' adjusted to the provided 'timezone'. The date parts that we return
 * depend on the iso8601 argument which defaults to false when not provided. This is the output based on is08601 value:
 *   True -> {"isoWeekYear": <val>, "isoWeek": <val>, "isoDayOfWeek": <val>, "hour": <val>, "minute": <val>, "second": <val>, "millisecond": <val> }
 *   False -> {"year": <val>, "month": <val>, "day": <val>, hour": <val>, "minute": <val>, "second": <val>, "millisecond": <val> }
 */
void
HandleDollarDateToParts(pgbson *doc, const bson_value_t *operatorValue,
						ExpressionResult *expressionResult)
{
	if (operatorValue->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(MongoLocation40524), errmsg(
							"$dateToParts only supports an object as its argument")));
	}

	bson_value_t dateExpression = { 0 };
	bson_value_t timezoneExpression = { 0 };
	bson_value_t isoArgExpression = { 0 };
	bson_iter_t documentIter = { 0 };
	BsonValueInitIterator(operatorValue, &documentIter);

	/* Parse the argument that should be in the following format:
	 * {"date": <dateExpression>, ["timezone": <stringExpression>, "iso8601": <boolExpression>]}. */
	while (bson_iter_next(&documentIter))
	{
		const char *key = bson_iter_key(&documentIter);
		if (strcmp(key, "date") == 0)
		{
			dateExpression = *bson_iter_value(&documentIter);
		}
		else if (strcmp(key, "timezone") == 0)
		{
			timezoneExpression = *bson_iter_value(&documentIter);
		}
		else if (strcmp(key, "iso8601") == 0)
		{
			isoArgExpression = *bson_iter_value(&documentIter);
		}
		else
		{
			ereport(ERROR, (errcode(MongoLocation40520), errmsg(
								"Unrecognized argument to $dateToParts: %s", key),
							errhint(
								"Unrecognized argument to $dateToParts, Unexpected key found in input")));
		}
	}

	if (dateExpression.value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(MongoLocation40522), errmsg(
							"Missing 'date' parameter to $dateToParts")));
	}

	bool isNullOnEmpty = false;
	bson_value_t nullValue;
	nullValue.value_type = BSON_TYPE_NULL;

	/* If no timezone is specified, we don't apply any offset as that's the default timezone, UTC. */
	ExtensionTimezone timezoneToApply = {
		.offsetInMs = 0,
		.isUtcOffset = true,
	};

	/* A timezone was specified. */
	if (timezoneExpression.value_type != BSON_TYPE_EOD)
	{
		bson_value_t evaluatedTz = EvaluateExpressionAndGetValue(doc, &timezoneExpression,
																 expressionResult,
																 isNullOnEmpty);

		/* Match native mongo, if timezone resolves to null or undefined, we bail before
		 * evaluating the other arguments and return null. */
		if (IsExpressionResultNullOrUndefined(&evaluatedTz))
		{
			ExpressionResultSetValue(expressionResult, &nullValue);
			return;
		}

		if (evaluatedTz.value_type != BSON_TYPE_UTF8)
		{
			ThrowLocation40517Error(evaluatedTz.value_type);
		}

		StringView timezoneToParse = {
			.string = evaluatedTz.value.v_utf8.str,
			.length = evaluatedTz.value.v_utf8.len
		};

		timezoneToApply = ParseTimezone(timezoneToParse);
	}

	bool isIsoRequested = false;

	/* iso8601 argument was specified. */
	if (isoArgExpression.value_type != BSON_TYPE_EOD)
	{
		bson_value_t evaluatedIsoArg = EvaluateExpressionAndGetValue(doc,
																	 &isoArgExpression,
																	 expressionResult,
																	 isNullOnEmpty);

		/* Match native mongo, if iso8601 resolves to null or undefined, we bail before
		 * evaluating the other arguments and return null. */
		if (IsExpressionResultNullOrUndefined(&evaluatedIsoArg))
		{
			ExpressionResultSetValue(expressionResult, &nullValue);
			return;
		}

		if (evaluatedIsoArg.value_type != BSON_TYPE_BOOL)
		{
			ereport(ERROR, (errcode(MongoLocation40521), errmsg(
								"iso8601 must evaluate to a bool, found %s",
								BsonTypeName(evaluatedIsoArg.value_type)),
							errhint("iso8601 must evaluate to a bool, found %s",
									BsonTypeName(evaluatedIsoArg.value_type))));
		}

		isIsoRequested = evaluatedIsoArg.value.v_bool;
	}

	bson_value_t evaluatedDate = EvaluateExpressionAndGetValue(doc, &dateExpression,
															   expressionResult,
															   isNullOnEmpty);

	if (IsExpressionResultNullOrUndefined(&evaluatedDate))
	{
		ExpressionResultSetValue(expressionResult, &nullValue);
		return;
	}

	int64_t dateInMs = BsonValueAsDateTime(&evaluatedDate);
	Datum pgTimestamp = GetPgTimestampFromEpochWithTimezone(dateInMs, timezoneToApply);

	/* Get date parts and write them to the result. */
	pgbson_writer objectWriter;
	pgbson_element_writer *elementWriter =
		ExpressionResultGetElementWriter(expressionResult);

	PgbsonElementWriterStartDocument(elementWriter, &objectWriter);

	/* If iso8601 is true we should return an object in the following shape:
	 * {"isoWeekYear": <val>, "isoWeek": <val>, "isoDayOfWeek": <val>, "hour": <val>, "minute": <val>, "second": <val>, "millisecond": <val> }.
	 *
	 * If it is false we should return an oject with the following shape:
	 * {"year": <val>, "month": <val>, "day": <val>, hour": <val>, "minute": <val>, "second": <val>, "millisecond": <val> }. */

	if (isIsoRequested)
	{
		int isoWeekY = GetDatePartFromPgTimestamp(pgTimestamp, DatePart_IsoWeekYear);
		int isoWeek = GetDatePartFromPgTimestamp(pgTimestamp, DatePart_IsoWeek);
		int isoDoW = GetDatePartFromPgTimestamp(pgTimestamp, DatePart_IsoDayOfWeek);
		PgbsonWriterAppendInt32(&objectWriter, "isoWeekYear", 11, isoWeekY);
		PgbsonWriterAppendInt32(&objectWriter, "isoWeek", 7, isoWeek);
		PgbsonWriterAppendInt32(&objectWriter, "isoDayOfWeek", 12, isoDoW);
	}
	else
	{
		int year = GetDatePartFromPgTimestamp(pgTimestamp, DatePart_Year);
		int month = GetDatePartFromPgTimestamp(pgTimestamp, DatePart_Month);
		int dom = GetDatePartFromPgTimestamp(pgTimestamp, DatePart_DayOfMonth);
		PgbsonWriterAppendInt32(&objectWriter, "year", 4, year);
		PgbsonWriterAppendInt32(&objectWriter, "month", 5, month);
		PgbsonWriterAppendInt32(&objectWriter, "day", 3, dom);
	}

	int hour = GetDatePartFromPgTimestamp(pgTimestamp, DatePart_Hour);
	int minute = GetDatePartFromPgTimestamp(pgTimestamp, DatePart_Minute);
	int second = GetDatePartFromPgTimestamp(pgTimestamp, DatePart_Second);
	int ms = GetDatePartFromPgTimestamp(pgTimestamp, DatePart_Millisecond);
	PgbsonWriterAppendInt32(&objectWriter, "hour", 4, hour);
	PgbsonWriterAppendInt32(&objectWriter, "minute", 6, minute);
	PgbsonWriterAppendInt32(&objectWriter, "second", 6, second);
	PgbsonWriterAppendInt32(&objectWriter, "millisecond", 11, ms);

	PgbsonElementWriterEndDocument(elementWriter, &objectWriter);
	ExpressionResultSetValueFromWriter(expressionResult);
}


/*
 * Evaluates the output of a $dateToString expression.
 * Since a $datToString is expressed as { "$dateToString": { date: <dateExpression>, [ format: <strExpression>, timezone: <tzExpression>, onNull: <expression> ] } }
 * We validate the input document and return the string with the specified timezone using the specified format if any.
 * If no format is specified the default format is: %Y-%m-%dT%H:%M:%S.%LZ
 *
 * The valid format specifiers are listed in: https://www.mongodb.com/docs/manual/reference/operator/aggregation/dateToString/#format-specifiers
 */
void
HandleDollarDateToString(pgbson *doc, const bson_value_t *operatorValue,
						 ExpressionResult *expressionResult)
{
	if (operatorValue->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(MongoLocation18629), errmsg(
							"$dateToString only supports an object as its argument")));
	}

	bson_value_t dateExpression = { 0 };
	bson_value_t formatExpression = { 0 };
	bson_value_t timezoneExpression = { 0 };
	bson_value_t onNullExpression = { 0 };
	bson_iter_t documentIter;
	BsonValueInitIterator(operatorValue, &documentIter);

	while (bson_iter_next(&documentIter))
	{
		const char *key = bson_iter_key(&documentIter);
		if (strcmp(key, "date") == 0)
		{
			dateExpression = *bson_iter_value(&documentIter);
		}
		else if (strcmp(key, "format") == 0)
		{
			formatExpression = *bson_iter_value(&documentIter);
		}
		else if (strcmp(key, "timezone") == 0)
		{
			timezoneExpression = *bson_iter_value(&documentIter);
		}
		else if (strcmp(key, "onNull") == 0)
		{
			onNullExpression = *bson_iter_value(&documentIter);
		}
		else
		{
			ereport(ERROR, (errcode(MongoLocation18534), errmsg(
								"Unrecognized argument to $dateToString: %s", key),
							errhint(
								"Unrecognized argument to $dateToString, Unexpected key found while parsing")));
		}
	}

	if (dateExpression.value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(MongoLocation18628), errmsg(
							"Missing 'date' parameter to $dateToString")));
	}

	bool isNullOnEmpty = false;

	/* onNull is only used for when 'date' argument is null or undefined, so we default it to null for the other arguments. */
	bson_value_t onNullResult;
	onNullResult.value_type = BSON_TYPE_NULL;

	/* Initialize to keep compiler happy even though we only use it if a the format argument was specified, which initializes it. */
	StringView formatString = {
		.length = 0,
		.string = NULL,
	};

	/* A format string was specified as an argument. */
	if (formatExpression.value_type != BSON_TYPE_EOD)
	{
		bson_value_t evaluatedFormat = EvaluateExpressionAndGetValue(doc,
																	 &formatExpression,
																	 expressionResult,
																	 isNullOnEmpty);

		/* Match native mongo, if any argument is null when evaluating, bail and don't evaluate the rest of the args. */
		if (IsExpressionResultNullOrUndefined(&evaluatedFormat))
		{
			ExpressionResultSetValue(expressionResult, &onNullResult);
			return;
		}

		if (evaluatedFormat.value_type != BSON_TYPE_UTF8)
		{
			ereport(ERROR, (errcode(MongoLocation18533), errmsg(
								"$dateToString requires that 'format' be a string, found: %s with value %s",
								BsonTypeName(evaluatedFormat.value_type),
								BsonValueToJsonForLogging(&evaluatedFormat)),
							errhint(
								"$dateToString requires that 'format' be a string, found: %s",
								BsonTypeName(evaluatedFormat.value_type))));
		}

		formatString.length = evaluatedFormat.value.v_utf8.len;
		formatString.string = evaluatedFormat.value.v_utf8.str;
	}

	/* If no timezone is specified, we don't apply any offset as the default should be UTC. */
	ExtensionTimezone timezoneToApply = {
		.isUtcOffset = true,
		.offsetInMs = 0,
	};

	/* A timezone was specified. */
	if (timezoneExpression.value_type != BSON_TYPE_EOD)
	{
		bson_value_t evaluatedTimezone = EvaluateExpressionAndGetValue(doc,
																	   &timezoneExpression,
																	   expressionResult,
																	   isNullOnEmpty);

		/* Match native mongo, if any argument is null when evaluating, bail and don't evaluate the rest of the args. */
		if (IsExpressionResultNullOrUndefined(&evaluatedTimezone))
		{
			ExpressionResultSetValue(expressionResult, &onNullResult);
			return;
		}

		if (evaluatedTimezone.value_type != BSON_TYPE_UTF8)
		{
			ThrowLocation40517Error(evaluatedTimezone.value_type);
		}

		StringView timezoneToParse = {
			.string = evaluatedTimezone.value.v_utf8.str,
			.length = evaluatedTimezone.value.v_utf8.len,
		};

		timezoneToApply = ParseTimezone(timezoneToParse);
	}

	bson_value_t evaluatedDate = EvaluateExpressionAndGetValue(doc, &dateExpression,
															   expressionResult,
															   isNullOnEmpty);

	if (IsExpressionResultNullOrUndefined(&evaluatedDate))
	{
		/* onNull argument was specified. */
		if (onNullExpression.value_type != BSON_TYPE_EOD)
		{
			/* The onNull expression is only used if the 'date' argument is null. */
			bson_value_t evaluatedOnNull = EvaluateExpressionAndGetValue(doc,
																		 &onNullExpression,
																		 expressionResult,
																		 isNullOnEmpty);

			/* If the onNull expression evaluates to EOD, i.e: non existent path in the document,
			 * the result should be empty, so just bail. */
			if (evaluatedOnNull.value_type == BSON_TYPE_EOD)
			{
				return;
			}

			onNullResult = evaluatedOnNull;
		}

		ExpressionResultSetValue(expressionResult, &onNullResult);
		return;
	}

	int64_t dateTimeInMs = BsonValueAsDateTime(&evaluatedDate);

	StringView stringResult;
	if (formatExpression.value_type != BSON_TYPE_EOD)
	{
		stringResult =
			GetDateStringWithFormat(dateTimeInMs, timezoneToApply, formatString);
	}
	else
	{
		stringResult = GetDateStringWithDefaultFormat(dateTimeInMs, timezoneToApply,
													  DateStringFormatCase_UpperCase);
	}

	bson_value_t result = {
		.value_type = BSON_TYPE_UTF8,
		.value.v_utf8.str = (char *) stringResult.string,
		.value.v_utf8.len = stringResult.length,
	};

	ExpressionResultSetValue(expressionResult, &result);
}


/* --------------------------------------------------------- */
/* private helper methods. */
/* --------------------------------------------------------- */

/* Common method for operators that return a single date part. This method parses and validates the arguments and
 * sets the result into the specified ExpressionResult. For more details about every operator, see its
 * HandleDollar* method description. */
static void
HandleDatePartOperator(pgbson *doc, const bson_value_t *operatorValue,
					   ExpressionResult *expressionResult, const char *operatorName,
					   DatePart datePart)
{
	bson_value_t dateExpression;
	bson_value_t timezoneExpression;
	bool isTimezoneSpecified = ParseDatePartOperatorArgument(operatorValue, operatorName,
															 &dateExpression,
															 &timezoneExpression);
	bool isNullOnEmpty = false;
	bson_value_t evaluatedDate = EvaluateExpressionAndGetValue(doc, &dateExpression,
															   expressionResult,
															   isNullOnEmpty);

	if (IsExpressionResultNullOrUndefined(&evaluatedDate))
	{
		bson_value_t nullResult;
		nullResult.value_type = BSON_TYPE_NULL;
		ExpressionResultSetValue(expressionResult, &nullResult);
		return;
	}

	int64_t dateTimeInMs = BsonValueAsDateTime(&evaluatedDate);

	/* In case no timezone is specified we just apply a 0 offset to get the default timezone which is UTC.*/
	ExtensionTimezone timezoneToApply = {
		.offsetInMs = 0,
		.isUtcOffset = true,
	};

	/* If a timezone was specified, we need to evaluate the expression, validate, parse and apply it. */
	if (isTimezoneSpecified)
	{
		bson_value_t evaluatedTimezone = EvaluateExpressionAndGetValue(doc,
																	   &timezoneExpression,
																	   expressionResult,
																	   isNullOnEmpty);

		if (IsExpressionResultNullOrUndefined(&evaluatedTimezone))
		{
			bson_value_t nullResult;
			nullResult.value_type = BSON_TYPE_NULL;
			ExpressionResultSetValue(expressionResult, &nullResult);
			return;
		}

		if (evaluatedTimezone.value_type != BSON_TYPE_UTF8)
		{
			ereport(ERROR, (errcode(MongoLocation40533), errmsg(
								"%s requires a string for the timezone argument, but was given a %s (%s)",
								operatorName, BsonTypeName(evaluatedTimezone.value_type),
								BsonValueToJsonForLogging(&evaluatedTimezone)),
							errhint(
								"'%s' requires a string for the timezone argument, but was given a %s",
								operatorName, BsonTypeName(
									evaluatedTimezone.value_type))));
		}

		StringView timezone = {
			.string = evaluatedTimezone.value.v_utf8.str,
			.length = evaluatedTimezone.value.v_utf8.len,
		};

		timezoneToApply = ParseTimezone(timezone);
	}

	Datum pgTimestamp = GetPgTimestampFromEpochWithTimezone(dateTimeInMs,
															timezoneToApply);
	uint32_t datePartResult = GetDatePartFromPgTimestamp(pgTimestamp, datePart);

	bson_value_t result;
	if (datePart == DatePart_IsoWeekYear)
	{
		/* $isoWeekYear is a long in native mongo */
		result.value_type = BSON_TYPE_INT64;
		result.value.v_int64 = (int64_t) datePartResult;
	}
	else
	{
		result.value_type = BSON_TYPE_INT32;
		result.value.v_int32 = datePartResult;
	}

	ExpressionResultSetValue(expressionResult, &result);
}


/* This method parses the single date part operators argument.
 * which can be <expression>, [ <expression> ], { date: <expression>, timezone: <stringExpression> }
 * it does not validate that the date expression is a valid date expression, it just returns it in the specified pointer. */
static bool
ParseDatePartOperatorArgument(const bson_value_t *operatorValue, const char *operatorName,
							  bson_value_t *dateExpression,
							  bson_value_t *timezoneExpression)
{
	bool isTimezoneSpecified = false;

	if (operatorValue->value_type == BSON_TYPE_ARRAY)
	{
		bson_iter_t arrayIterator;
		BsonValueInitIterator(operatorValue, &arrayIterator);

		bool valid = bson_iter_next(&arrayIterator);
		if (valid)
		{
			*dateExpression = *bson_iter_value(&arrayIterator);

			/* array argument is only valid if it has a single element. */
			valid = !bson_iter_next(&arrayIterator);
		}

		if (!valid)
		{
			int numArgs = BsonDocumentValueCountKeys(operatorValue);
			ereport(ERROR, (errcode(MongoLocation40536), errmsg(
								"%s accepts exactly one argument if given an array, but was given %d",
								operatorName, numArgs),
							errhint(
								"%s accepts exactly one argument if given an array, but was given %d",
								operatorName, numArgs)));
		}
	}
	else if (operatorValue->value_type == BSON_TYPE_DOCUMENT)
	{
		/* a document argument could be an expression that evaluates to a date or
		 * an argument following the {date: <dateExpression>, timezone: <strExpression> } spec. */

		bool isDateSpecified = false;
		bson_iter_t documentIter;
		BsonValueInitIterator(operatorValue, &documentIter);

		if (bson_iter_next(&documentIter))
		{
			const char *key = bson_iter_key(&documentIter);
			if (bson_iter_key_len(&documentIter) > 1 && key[0] == '$')
			{
				/* Found an operator, need to evaluate. */
				*dateExpression = *operatorValue;
				return isTimezoneSpecified;
			}

			/* regular argument, try to get the date and timezone. */
			do {
				const char *key = bson_iter_key(&documentIter);
				if (strcmp(key, "date") == 0)
				{
					isDateSpecified = true;
					*dateExpression = *bson_iter_value(&documentIter);
				}
				else if (strcmp(key, "timezone") == 0)
				{
					isTimezoneSpecified = true;
					*timezoneExpression = *bson_iter_value(&documentIter);
				}
				else
				{
					ereport(ERROR, (errcode(MongoLocation40535), errmsg(
										"unrecognized option to %s: \"%s\"",
										operatorName, key),
									errhint(
										"unrecognized option to operator %s, Unexpected key in input",
										operatorName)));
				}
			} while (bson_iter_next(&documentIter));
		}

		if (!isDateSpecified)
		{
			ereport(ERROR, (errcode(MongoLocation40539),
							errmsg(
								"missing 'date' argument to %s, provided: %s: %s",
								operatorName, operatorName, BsonValueToJsonForLogging(
									operatorValue)),
							errhint(
								"missing 'date' argument to %s, provided argument of type: %s",
								operatorName, BsonTypeName(operatorValue->value_type))));
		}
	}
	else
	{
		*dateExpression = *operatorValue;
	}

	return isTimezoneSpecified;
}


/* Helper method that constructs the date string for the dateInMs adjusted to the specified timezone.
 * The string will follow the format specified in the arguments. If the format is null or empty, the result is an empty string.
 * This method assumes that the specified timezone is already validated with ParseTimezone method. */
static StringView
GetDateStringWithFormat(int64_t dateInMs, ExtensionTimezone timezone, StringView format)
{
	StringView result = {
		.length = 0,
		.string = "",
	};

	Datum pgTimestamp = GetPgTimestampFromEpochWithTimezone(dateInMs, timezone);

	/* Special format requested. */
	if (format.length == 0 || format.string == NULL)
	{
		return result;
	}

	/* We get a buffer of size equal to the format length * 3 to get a large enough buffer to fit the final string in.
	 * With this we ensure it is large enough as the longest value produced for a date part is of length
	 * 5, and since every format identifier needs to have a %, the smallest format string with valid identifiers,
	 * is 2. i.e We get a format string such as %z which is ([+|-]hhmm), the result will be of length 5 and we
	 * will be allocating a string of length 6. */

	int bufferSize = format.length * 3;
	char *buffer;

	/* If we need less than 256 bytes, allocate it on the stack, if greater, on the heap. */
	if (bufferSize <= 256)
	{
		char tmp[256];
		buffer = tmp;
	}
	else
	{
		buffer = palloc0(sizeof(char) * bufferSize);
	}

	char *currentPtr = buffer;
	const char *end = buffer + bufferSize;
	char *formatPtr = (char *) format.string;

	uint32_t i = 0;
	uint32_t finalLength = 0;
	while (i < format.length)
	{
		if (*formatPtr != '%')
		{
			WriteCharAndAdvanceBuffer(&currentPtr, end, *formatPtr);
			formatPtr++, i++, finalLength++;
			continue;
		}

		if (i + 1 >= format.length)
		{
			ereport(ERROR, (errcode(MongoLocation18535), errmsg(
								"Unmatched '%%' at end of format string")));
		}

		/* Move to the format specifier. */
		i++;
		formatPtr++;

		DatePart datePart;
		int padding;
		switch (*formatPtr)
		{
			case 'd':
			{
				datePart = DatePart_DayOfMonth;
				padding = 2;
				break;
			}

			case 'G':
			{
				datePart = DatePart_IsoWeekYear;
				padding = 4;
				break;
			}

			case 'H':
			{
				datePart = DatePart_Hour;
				padding = 2;
				break;
			}

			case 'j':
			{
				datePart = DatePart_DayOfYear;
				padding = 3;
				break;
			}

			case 'L':
			{
				datePart = DatePart_Millisecond;
				padding = 3;
				break;
			}

			case 'm':
			{
				datePart = DatePart_Month;
				padding = 2;
				break;
			}

			case 'M':
			{
				datePart = DatePart_Minute;
				padding = 2;
				break;
			}

			case 'S':
			{
				datePart = DatePart_Second;
				padding = 2;
				break;
			}

			case 'w':
			{
				datePart = DatePart_DayOfWeek;
				padding = 1;
				break;
			}

			case 'u':
			{
				datePart = DatePart_IsoDayOfWeek;
				padding = 1;
				break;
			}

			case 'U':
			{
				datePart = DatePart_Week;
				padding = 2;
				break;
			}

			case 'V':
			{
				datePart = DatePart_IsoWeek;
				padding = 2;
				break;
			}

			case 'Y':
			{
				datePart = DatePart_Year;
				padding = 4;
				break;
			}

			case 'z':
			{
				/* Utc offset following the +/-[hh][mm] format */
				int offsetInMinutes = DetermineUtcOffsetForEpochWithTimezone(dateInMs,
																			 timezone);
				bool isNegative = offsetInMinutes < 0;
				offsetInMinutes = isNegative ? -offsetInMinutes : offsetInMinutes;

				WriteCharAndAdvanceBuffer(&currentPtr, end, isNegative ? '-' : '+');

				uint32_t hours = offsetInMinutes / MINUTES_IN_HOUR;
				uint32_t minutes = offsetInMinutes % MINUTES_IN_HOUR;
				WritePaddedUInt32AndAdvanceBuffer(&currentPtr, end, 2, hours);
				WritePaddedUInt32AndAdvanceBuffer(&currentPtr, end, 2, minutes);

				finalLength += 5;
				i++, formatPtr++;
				continue;
			}

			case 'Z':
			{
				/* Utc offset in minutes. */
				int offsetInMinutes = DetermineUtcOffsetForEpochWithTimezone(dateInMs,
																			 timezone);
				finalLength += WriteInt32AndAdvanceBuffer(&currentPtr, end,
														  offsetInMinutes);
				i++, formatPtr++;
				continue;
			}

			case '%':
			{
				WriteCharAndAdvanceBuffer(&currentPtr, end, '%');
				i++, formatPtr++, finalLength++;
				continue;
			}

			default:
			{
				ereport(ERROR, (errcode(MongoLocation18536), errmsg(
									"Invalid format character '%%%c' in format string",
									*formatPtr),
								errhint(
									"Invalid format character '%%%c' in format string",
									*formatPtr)));
			}
		}

		uint32_t datePartValue = GetDatePartFromPgTimestamp(pgTimestamp, datePart);
		ValidateDateValueIsInRange(datePartValue);
		WritePaddedUInt32AndAdvanceBuffer(&currentPtr, end, padding, datePartValue);

		finalLength += padding;
		formatPtr++;
		i++;
	}

	/* Can't use pnstrdup here as if the format string contains a null char, we are going to copy it over,
	 * and pnstrdup calculates the length if different from the argument provided to the first null byte
	 * found if any before the length provided, and we actually want to copy all chars as we built the string. */
	char *finalString = palloc0(sizeof(char) * (finalLength + 1));
	memcpy(finalString, buffer, sizeof(char) * finalLength);
	finalString[finalLength] = '\0';

	result.length = finalLength;
	result.string = finalString;

	if (bufferSize > 256)
	{
		/* We allocated on the heap and need to free the buffer. */
		pfree(buffer);
	}

	return result;
}


/* Helper method that constructs the date string for the dateInMs adjusted to the specified timezone.
 * The string will follow the default format: %Y-%m-%dT%H:%M:%S.%LZ.
 * This method assumes that the specified timezone is already validated with ParseTimezone method.
 * @todo : We can optimise this function . Instead of making multiple calls we can think from the perspective to reduce calls to internal postgres function. Task: 2489730
 * */
StringView
GetDateStringWithDefaultFormat(int64_t dateInMs, ExtensionTimezone timezone,
							   DateStringFormatCase formatCase)
{
	Datum pgTimestamp = GetPgTimestampFromEpochWithTimezone(dateInMs, timezone);

	/* Default format is: %Y-%m-%dT%H:%M:%S.%LZ which is equivalent to: yyyy-mm-ddThh:mm:ss.msZ */
	char buffer[25] = { 0 };

	uint32_t year = GetDatePartFromPgTimestamp(pgTimestamp, DatePart_Year);
	ValidateDateValueIsInRange(year);

	uint32_t month = GetDatePartFromPgTimestamp(pgTimestamp, DatePart_Month);
	uint32_t day = GetDatePartFromPgTimestamp(pgTimestamp, DatePart_DayOfMonth);
	uint32_t hour = GetDatePartFromPgTimestamp(pgTimestamp, DatePart_Hour);
	uint32_t minute = GetDatePartFromPgTimestamp(pgTimestamp, DatePart_Minute);
	uint32_t second = GetDatePartFromPgTimestamp(pgTimestamp, DatePart_Second);
	uint32_t millisecond = GetDatePartFromPgTimestamp(pgTimestamp, DatePart_Millisecond);
	if (formatCase != DateStringFormatCase_LowerCase)
	{
		sprintf(buffer, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", year, month, day, hour,
				minute,
				second, millisecond);
	}
	else
	{
		sprintf(buffer, "%04d-%02d-%02dt%02d:%02d:%02d.%03dz", year, month, day, hour,
				minute,
				second, millisecond);
	}
	StringView result = {
		.length = 24,
		.string = strndup(buffer, 24),
	};

	return result;
}


/**  Helper method that constructs the timestamp string for the dateInMs adjusted to the specified timezone.
 * The string will follow the default format: Month Date Hours:Minutes:Seconds:Milliseconds.
 * This method assumes that the specified timezone is already validated with ParseTimezone method.
 * @param timeStampBsonElement : This is the specified timestamp bson type input
 * @param timezone : this is the adjusted specified timezone.
 * @todo : We can optimise this function . Instead of making multiple calls we can think from the perspective to reduce calls to internal postgres function. Task: 2489730
 * */
StringView
GetTimestampStringWithDefaultFormat(const bson_value_t *timeStampBsonElement,
									ExtensionTimezone timezone,
									DateStringFormatCase formatCase)
{
	Assert(BSON_TYPE_TIMESTAMP == timeStampBsonElement->value_type);

	int64_t dateInMs = timeStampBsonElement->value.v_timestamp.timestamp *
					   MILLISECONDS_IN_SECOND +
					   timeStampBsonElement->value.v_timestamp.increment;

	/* Default format is: Month name (3 letters) dd hours:minutes:seconds:milliseconds which is 19 chars*/
	char buffer[19] = { 0 };

	Datum pgTimestamp = GetPgTimestampFromEpochWithTimezone(dateInMs, timezone);

	uint32_t month = GetDatePartFromPgTimestamp(pgTimestamp, DatePart_Month);
	uint32_t day = GetDatePartFromPgTimestamp(pgTimestamp, DatePart_DayOfMonth);
	uint32_t hour = GetDatePartFromPgTimestamp(pgTimestamp, DatePart_Hour);
	uint32_t minute = GetDatePartFromPgTimestamp(pgTimestamp, DatePart_Minute);
	uint32_t second = GetDatePartFromPgTimestamp(pgTimestamp, DatePart_Second);
	uint32_t millisecond = GetDatePartFromPgTimestamp(pgTimestamp, DatePart_Millisecond);

	const char *formattedMonthName;
	switch (formatCase)
	{
		case DateStringFormatCase_UpperCase:
		{
			formattedMonthName = monthNamesUpperCase[month - 1];
			break;
		}

		case DateStringFormatCase_LowerCase:
		{
			formattedMonthName = monthNamesLowerCase[month - 1];
			break;
		}

		case DateStringFormatCase_CamelCase:
		default:
		{
			formattedMonthName = monthNamesCamelCase[month - 1];
			break;
		}
	}

	sprintf(buffer, "%s %02d %02d:%02d:%02d:%03d", formattedMonthName, day, hour, minute,
			second, millisecond);

	StringView result = {
		.length = 19,
		.string = strndup(buffer, 19),
	};

	return result;
}


/* Helper method that writes a signed int32 to the specified buffer and advances the buffer after writing.
 * It returns the length of the written string. */
static int
WriteInt32AndAdvanceBuffer(char **buffer, const char *end, int32_t value)
{
	char tmp[12]; /* We need at least 12 bytes for a signed int32 string. */
	int length = pg_ltoa(value, tmp);

	if ((*buffer + length) > end)
	{
		ereport(ERROR, (errmsg(
							"Buffer is not big enough to write the requested value for $dateToString.")));
	}

	memcpy(*buffer, tmp, sizeof(char) * length);
	*buffer += length;

	return length;
}


/* Helper method that writes an unsigned int32 padded with zeros using the specified padding.
 * This method also advances the buffer after writing the number of written characters which should be equivalent
 * to the specified padding. */
static void
WritePaddedUInt32AndAdvanceBuffer(char **buffer, const char *end, int padding,
								  uint32_t value)
{
	Assert(padding > 0 && padding <= 4);

	if ((*buffer + padding) > end)
	{
		ereport(ERROR, (errmsg(
							"Buffer is not big enough to write the requested value $dateToString.")));
	}

	char tmp[4];
	int actualLength = pg_ultoa_n(value, tmp);

	if (actualLength > padding)
	{
		ereport(ERROR, (errmsg("Value: %d has more digits than the requested padding: %d",
							   value, padding),
						errhint(
							"Value: %d has more digits than the requested padding: %d",
							value, padding)));
	}

	int numberOfZeros = padding - actualLength;

	if (numberOfZeros > 0)
	{
		memset(*buffer, '0', sizeof(char) * numberOfZeros);
		*buffer += numberOfZeros;
	}

	memcpy(*buffer, tmp, sizeof(char) * actualLength);
	*buffer += actualLength;
}


/* Helper method that writes a single char to the provided buffer and advances the buffer 1 place after writing. */
static void
WriteCharAndAdvanceBuffer(char **buffer, const char *end, char value)
{
	if ((*buffer + 1) > end)
	{
		ereport(ERROR, (errmsg(
							"Buffer is not big enough to write the requested value $dateToString.")));
	}

	**buffer = value;
	*buffer += 1;
}


/* Given a Unix epoch in milliseconds and a timezone (utc offset or an Olson Timezone Identifier) it returns a
 * Datum holding a TimestampTz instance adjusted to the provided timezone in order to use for date operations with postgres.
 * It assumes the timezone is valid and was created with the ParseTimezone method. */
static Datum
GetPgTimestampFromEpochWithTimezone(int64_t epochInMs, ExtensionTimezone timezone)
{
	if (timezone.isUtcOffset)
	{
		epochInMs += timezone.offsetInMs;
		return GetPgTimestampFromUnixEpoch(epochInMs);
	}


	return OidFunctionCall2(PostgresTimestampToZoneFunctionId(),
							CStringGetTextDatum(timezone.id),
							GetPgTimestampFromUnixEpoch(epochInMs));
}


/* Parses a timezone string that can be a utc offset or an Olson timezone identifier.
 * If it represents a utc offset it throws if it doesn't follow the mongo valid offset format: +/-[hh], +/-[hh][mm] or +/-[hh]:[mm].
 * Otherwise, it throws if the timezone doesn't exist. */
static ExtensionTimezone
ParseTimezone(StringView timezone)
{
	ExtensionTimezone result;
	if (timezone.length == 0 || timezone.string == NULL)
	{
		ThrowInvalidTimezoneIdentifier(timezone.string);
	}

	if (timezone.string[0] == '+' || timezone.string[0] == '-')
	{
		/* We've got a utc offset. */
		result.offsetInMs = ParseUtcOffset(timezone);
		result.isUtcOffset = true;
	}
	else
	{
		/* We've got an Timezone Identifier or an offset without a sign (which is not valid in native Mongo). */

		/* pg_tzset also accepts UTC offsets but format is different and more permisive than MongoDB,
		* so in case we got a result, we just check if it was an offset without a sign. i.e: 08:09 */
		const char *p = timezone.string;
		bool isOffset = true;
		while (*p)
		{
			if (!isdigit(*p) && *p != ':')
			{
				isOffset = false;
				break;
			}

			p++;
		}

		/* Check if timezone is supported -- pg_tzset loads the TZ from the TZ database
		 * and adds it to a cache if it exists. If it doesn't it returns NULL.
		 * It doesn't matter that we're loading the TZ to validate if it exists,
		 * as if it does, we are going to use it anyways and it will be in the cache already.
		 */
		if (isOffset || !pg_tzset(timezone.string))
		{
			ThrowInvalidTimezoneIdentifier(timezone.string);
		}

		result.id = timezone.string;
		result.isUtcOffset = false;
	}

	return result;
}


/* Helper method that returns the UTC offset for the provided timezone for the current epoch.
 * The UTC offset is returned in minutes.
 * This method assumes the timezone was already validated by using ParseTimezone method. */
static int32_t
DetermineUtcOffsetForEpochWithTimezone(int64_t epochInMs, ExtensionTimezone timezone)
{
	/* If the timezone is already a utc offset, return that value in minutes, otherwise
	 * use Postgres to determine the offset for the given date and timezone. */

	if (timezone.isUtcOffset)
	{
		return timezone.offsetInMs / MILLISECONDS_IN_SECOND / SECONDS_IN_MINUTE;
	}

	pg_tz *pgTz = pg_tzset(timezone.id);

	/* Should not be null, we should've already validated the timezone identifier. */
	Assert(pgTz != NULL);

	/* We need to represent the timestamp as a pg_tm object which holds the gmtoffset. */
	TimestampTz timestampTz = DatumGetTimestampTz(GetPgTimestampFromUnixEpoch(epochInMs));
	pg_time_t convertedTimeT = timestamptz_to_time_t(timestampTz);
	struct pg_tm *pgLocalTime = pg_localtime(&convertedTimeT, pgTz);

	return pgLocalTime->tm_gmtoff / SECONDS_IN_MINUTE;
}


/* Given a Utc offset string following mongo valid offsets, returns the offset difference in milliseconds.
 * Throws for non valid utc offsets. */
static int64_t
ParseUtcOffset(StringView offset)
{
	uint32_t hours = 0;
	uint32_t minutes = 0;
	bool valid = false;
	StringView start = StringViewSubstring(&offset, 1);
	switch (offset.length)
	{
		case 3:
		{
			/* "+/-[hh]" case */
			valid = TryParseTwoDigitNumber(start, &hours);
			break;
		}

		case 5:
		{
			/* "+/-[hh][mm]" case */
			valid = TryParseTwoDigitNumber(start, &hours);
			valid = valid && TryParseTwoDigitNumber(StringViewSubstring(&start, 2),
													&minutes);
			break;
		}

		case 6:
		{
			/* "+/-[hh]:[mm]" case */
			if (*(StringViewSubstring(&start, 2).string) == ':')
			{
				valid = TryParseTwoDigitNumber(start, &hours);
				valid = valid && TryParseTwoDigitNumber(StringViewSubstring(&start, 3),
														&minutes);
			}

			break;
		}

		default:
		{ }
	}


	if (!valid)
	{
		/* invalid input. */
		ThrowInvalidTimezoneIdentifier(offset.string);
	}

	uint64_t hrsInMs = hours * MINUTES_IN_HOUR * SECONDS_IN_MINUTE *
					   MILLISECONDS_IN_SECOND;
	uint64_t mntsInMs = minutes * SECONDS_IN_MINUTE * MILLISECONDS_IN_SECOND;
	uint64_t offsetInMs = hrsInMs + mntsInMs;

	return offset.string[0] == '-' ? -offsetInMs : offsetInMs;
}


/* Given a string representing a two digit number, converts it to an uint.
 * Returns true for valid two digit number representations, false otherwise. */
static bool
TryParseTwoDigitNumber(StringView str, uint32_t *result)
{
	char firstD = *str.string;
	char secondD = *(StringViewSubstring(&str, 1).string);

	if (isdigit(firstD) && isdigit(secondD))
	{
		*result = ((firstD - '0') * 10) + (secondD - '0');
		return true;
	}

	return false;
}


/* Given a Unix epoch in milliseconds returns a Datum containing a postgres Timestamp instance. */
static Datum
GetPgTimestampFromUnixEpoch(int64_t epochInMs)
{
	return OidFunctionCall1(PostgresToTimestamptzFunctionId(),
							Float8GetDatum(((float8) epochInMs) /
										   MILLISECONDS_IN_SECOND));
}


/* Returns the requested unit part from the provided postgres timestamp as an uint32. */
static uint32_t
GetDatePartFromPgTimestamp(Datum pgTimestamp, DatePart datePart)
{
	const char *partName;

	switch (datePart)
	{
		case DatePart_Hour:
		{
			partName = "hour";
			break;
		}

		case DatePart_Minute:
		{
			partName = "minute";
			break;
		}

		case DatePart_Second:
		{
			partName = "second";
			break;
		}

		case DatePart_Millisecond:
		{
			partName = "millisecond";
			break;
		}

		case DatePart_Year:
		{
			partName = "year";
			break;
		}

		case DatePart_Month:
		{
			partName = "month";
			break;
		}

		case DatePart_DayOfYear:
		{
			partName = "doy";
			break;
		}

		case DatePart_DayOfMonth:
		{
			partName = "day";
			break;
		}

		case DatePart_DayOfWeek:
		{
			partName = "dow";
			break;
		}

		case DatePart_IsoWeekYear:
		{
			partName = "isoyear";
			break;
		}

		case DatePart_IsoWeek:
		{
			partName = "week";
			break;
		}

		case DatePart_IsoDayOfWeek:
		{
			partName = "isodow";
			break;
		}

		case DatePart_Week:
		{
			/* In postgres the week part follows the ISO 8601 week numbering. Which start on Mondays and
			 * the first week of a year contains January 4 of that year. In other words, the first Thursday
			 * of a year is in week 1 of that year. So it is possible for early-January dates to be part of the 52nd or 53rd week
			 * of the previous year and for late-December dates to be part of the first week of the next year. i.e: 2005-01-01
			 * in ISO 8601 is part of the 53rd week of year 2004, in this case we should return 1 as we don't want the ISO week numbering.
			 * Native mongo for non-ISO weeks, weeks begin on Sundays and the first week begins the first Sunday of the year, days preceding
			 * the first Sunday of the year are in week 0. */

			uint32_t dayOfYear = GetDatePartFromPgTimestamp(pgTimestamp,
															DatePart_DayOfYear);
			uint32_t dayOfWeek = GetDatePartFromPgTimestamp(pgTimestamp,
															DatePart_DayOfWeek);

			/* Since weeks start on Sunday and the days of the year preceding the first Sunday of the year, we need to get the
			 * Day of year for the Sunday after the current day of year and then divide it by 7 to get the week on 0-53 range.
			 * previous Sunday should be the dayOfYear - dayOfWeek (since weeks start on Sunday, if negative, it means previous Sunday is part of week 0)
			 * then we can just add 7 (number of days in week) to get the next Sunday. */
			return ((dayOfYear - dayOfWeek) + DAYS_IN_WEEK) / DAYS_IN_WEEK;
		}

		default:
		{
			ereport(ERROR, (errcode(MongoBadValue),
							errmsg("Invalid date part unit %d", datePart),
							errhint("Invalid date part unit %d", datePart)));
		}
	}

	Datum partDatum = OidFunctionCall2(PostgresDatePartFunctionId(),
									   CStringGetTextDatum(partName), pgTimestamp);
	double float8 = DatumGetFloat8(partDatum);
	uint32_t result = (uint32_t) float8;

	if (datePart == DatePart_Millisecond)
	{
		/* In postgres the millisecond part includes full seconds, so we need to strip out the seconds and get the MS part only.
		 * We use round rather than downcast because precision can be lost when postgres gets the milliseconds from the date. */
		result = (uint32_t) round(float8);
		result = result % MILLISECONDS_IN_SECOND;
	}

	if (datePart == DatePart_DayOfWeek)
	{
		/* Postgres range for dow is 0-6 and native mongo uses 1-7 range */
		result = result + 1;
	}

	return result;
}
