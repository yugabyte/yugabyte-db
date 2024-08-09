/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bson/bson_expression_date_operators.c
 *
 * Object Operator expression implementations of BSON.
 *
 *-------------------------------------------------------------------------
 */
#include <postgres.h>
#include <pgtime.h>
#include <fmgr.h>
#include <math.h>
#include <utils/numeric.h>
#include <utils/datetime.h>
#include <utils/timestamp.h>
#include <utils/builtins.h>

#include "io/helio_bson_core.h"
#include "operators/bson_expression.h"
#include "operators/bson_expression_operators.h"
#include "utils/date_utils.h"
#include "utils/mongo_errors.h"
#include "utils/fmgrprotos.h"
#include "metadata/metadata_cache.h"

#define SECONDS_IN_MINUTE 60
#define MINUTES_IN_HOUR 60
#define DAYS_IN_WEEK 7

/*
 * This represents unix ms for 0001-01-01.
 * Significance for this is it's used in dateFromParts.
 * This is the base underlying year for calculations we add year, month, day, hour, minute, seconds interval to this timestamp.
 */
#define DATE_FROM_PART_START_DATE_MS -62135596800000L
#define DATE_TRUNC_TIMESTAMP_MS 946684800000L
#define SECONDS_IN_DAY 86400
#define CONDITIONAL_EREPORT(isOnErrorPresent, ereportCall) \
	if (!isOnErrorPresent) { \
		ereportCall; \
	}

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
	DatePart_Str_Month = 13,
	DatePart_Str_Month_Abbr = 14
} DatePart;


/* Enum that defines possible units for dateTrunc */
typedef enum DateTruncUnit
{
	DateTruncUnit_Invalid = 0,
	DateTruncUnit_Day = 1,
	DateTruncUnit_Hour = 2,
	DateTruncUnit_Minute = 3,
	DateTruncUnit_Month = 4,
	DateTruncUnit_Quarter = 5,
	DateTruncUnit_Second = 6,
	DateTruncUnit_Week = 7,
	DateTruncUnit_Year = 8,
	DateTruncUnit_Millisecond = 9,
} DateTruncUnit;

typedef enum WeekDay
{
	WeekDay_Invalid = 0,
	WeekDay_Monday = 1,
	WeekDay_Tuesday = 2,
	WeekDay_Wednesday = 3,
	WeekDay_Thursday = 4,
	WeekDay_Friday = 5,
	WeekDay_Saturday = 6,
	WeekDay_Sunday = 7
}WeekDay;


typedef struct DollarDateAddSubtract
{
	/* The date to truncate, specified in UTC */
	AggregationExpressionData startDate;

	/* The unit to add/subtract. Like second, year, month, etc */
	AggregationExpressionData unit;

	/* The amount to add/subtract. This is supposed to be long*/
	AggregationExpressionData amount;

	/* Optional: Timezone for the $dateTrunc calculation */
	AggregationExpressionData timezone;
} DollarDateAddSubtract;


/* Struct that represents the parsed arguments to a $dateFromParts expression. */
typedef struct DollarDateFromParts
{
	AggregationExpressionData year;
	AggregationExpressionData isoWeekYear;
	AggregationExpressionData month;
	AggregationExpressionData isoWeek;
	AggregationExpressionData isoDayOfWeek;
	AggregationExpressionData day;
	AggregationExpressionData hour;
	AggregationExpressionData minute;
	AggregationExpressionData second;
	AggregationExpressionData millisecond;
	AggregationExpressionData timezone;
	bool isISOWeekDate;
} DollarDateFromParts;

/* Struct that represents the bson_value_t arguments to a $dateFromParts input values. */
typedef struct DollarDateFromPartsBsonValue
{
	/* This part refers to the year value part in date */
	bson_value_t year;

	/* This part refers to the iso year value part in date */
	bson_value_t isoWeekYear;

	/* This part refers to the month value part in date ranges from 1-12 */
	bson_value_t month;

	/* This part refers to the iso week value part in date ranges from 1-53 */
	bson_value_t isoWeek;

	/* This part refers to the iso day of week value part in date ranges from 1-366 */
	bson_value_t isoDayOfWeek;

	/* This part refers to the day of month value part in date ranges from 1-31*/
	bson_value_t day;

	/* This part refers to the hour value part in date */
	bson_value_t hour;

	/* This part refers to the minute value part in date */
	bson_value_t minute;

	/* This part refers to the second value part in date */
	bson_value_t second;

	/* This part refers to the millisecond value part in date */
	bson_value_t millisecond;

	/* This part refers to the timezone  value part in date. It's a string  */
	bson_value_t timezone;

	/* This part refers to the day of year value part in date ranges from 1-366 */
	bson_value_t dayOfYear;

	/* This is added as during parsing we need to know if the date is in ISO format or not. So just storing it here for use of dateFromString */
	bool isIsoFormat;
} DollarDateFromPartsBsonValue;

/* State for a $dateTrunc operator. */
typedef struct DollarDateTruncArgumentState
{
	/* The date to truncate, specified in UTC */
	AggregationExpressionData date;

	/*	The unit of time specified in year, quarter, week, month, day, hour, minute, second */
	AggregationExpressionData unit;

	/*Optional: numeric value to specify the time to divide */
	AggregationExpressionData binSize;

	/* Optional: Timezone for the $dateTrunc calculation */
	AggregationExpressionData timezone;

	/* Optional: Only used when unit is week , used as the first day of the week for the calculation */
	AggregationExpressionData startOfWeek;

	/*Stores unit as an enum . */
	DateTruncUnit dateTruncUnit;

	/* Stores startOfWeek as enum. */
	WeekDay weekDay;
} DollarDateTruncArgumentState;


/* State for a $dateDiff operator. */
typedef struct DollarDateDiffArgumentState
{
	/* Start date specified in UTC */
	AggregationExpressionData startDate;

	/* End date specified in UTC */
	AggregationExpressionData endDate;

	/*	The unit of time specified in year, quarter, week, month, day, hour, minute, second */
	AggregationExpressionData unit;

	/* Optional: Timezone for the $dateTrunc calculation */
	AggregationExpressionData timezone;

	/* Optional: Only used when unit is week , used as the first day of the week for the calculation */
	AggregationExpressionData startOfWeek;
} DollarDateDiffArgumentState;

/* State for a $dateFromString operator. */
typedef struct DollarDateFromStringArgumentState
{
	/*The date string to convert to object*/
	AggregationExpressionData dateString;

	/*Optional : Format to use for converting dateString*/
	AggregationExpressionData format;

	/*Optional: To handle null inputs for dateString*/
	AggregationExpressionData onNull;

	/*Optional: To specify timezone to format the date*/
	AggregationExpressionData timezone;

	/*Optional: To handle errors while parsing given dateString*/
	AggregationExpressionData onError;

	/* This flag is being while parsing as we cannot supply timezone in string and in timezone field together. */
	bool isTimezoneProvided;
} DollarDateFromStringArgumentState;

/* This is a struct for defining a map between postgres and mongo format and it's length in string and range of acceptible values. */
typedef struct DateFormatMap
{
	/* Mongo format specifier */
	const char *mongoFormat;

	/* Postgres format specifier*/
	const char *postgresFormat;

	/*What specific part it represents*/
	DatePart datePart;

	/* Value accepted min len*/
	const int minLen;

	/* Value accepted max len*/
	const int maxLen;

	/* minRange for the date part */
	const int minRangeValue;

	/* maxRange for the date part */
	const int maxRangeValue;
} DateFormatMap;


static const char *monthNamesCamelCase[12] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};
static const char *monthNamesUpperCase[12] = {
	"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"
};
static const char *monthNamesLowerCase[12] = {
	"jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec"
};

static const char *monthNamesFull[12] = {
	"january", "february", "march", "april", "may", "june", "july", "august", "september",
	"october", "november", "december"
};

static const char *unitSizeForDateTrunc[9] = {
	"day", "hour", "minute", "month", "quarter", "second", "week", "year", "millisecond"
};

static const char *weekDaysFullName[7] = {
	"monday", "tuesday", "wednesday", "thursday", "friday", "saturday", "sunday"
};

static const char *weekDaysAbbreviated[7] = {
	"mon", "tue", "wed", "thu", "fri", "sat", "sun"
};

/* static mapping for mongo format specifiers to postgres format specifiers. These are sorted on mongo format specifier(index 0) and should be kept sorted */
static const DateFormatMap dateFormats[] = {
	{ "%B", "month", DatePart_Str_Month, 4, 9, -1, -1 },
	{ "%G", "IYYY", DatePart_IsoWeekYear, 1, 4, 0, 9999 },
	{ "%H", "HH24", DatePart_Hour, 2, 2, 0, 23 },
	{ "%L", "MS", DatePart_Millisecond, 1, 3, 0, 999 },
	{ "%M", "MI", DatePart_Minute, 2, 2, 0, 59 },
	{ "%S", "SS", DatePart_Second, 2, 2, 0, 59 },
	{ "%V", "IW", DatePart_IsoWeek, 1, 2, 1, 53 },
	{ "%Y", "YYYY", DatePart_Year, 1, 4, 0, 9999 },
	{ "%b", "mon", DatePart_Str_Month_Abbr, 3, 3, -1, -1 },
	{ "%d", "DD", DatePart_DayOfMonth, 1, 2, 1, 31 },
	{ "%j", "DDD", DatePart_DayOfYear, 1, 3, 1, 999 },
	{ "%m", "MM", DatePart_Month, 1, 2, 1, 12 },
	{ "%u", "ID", DatePart_IsoDayOfWeek, 1, 1, 1, 7 }
};

/*
 * Using these values in the rangeforDateUnit array
 * The order is same as the enum DateUnit
 * These values are extrapolated manually based on the behaviour of operator
 */
const int64 rangeforDateUnit[] = {
	0,     /* DateUnit_Invalid, dummy value */
	584942417L,     /* DateUnit_Year */
	1754827251L,     /* DateUnit_Quarter */
	7019309004L,     /* DateUnit_Month */
	30500568905L,     /* DateUnit_Week */
	213503982335L,     /* DateUnit_Day */
	5124095576040L,     /* DateUnit_Hour */
	307445734562400L,     /* DateUnit_Minute */
	18446744073744000L,     /* DateUnit_Second */
};

static const char *IsoDateFormat = "IYYY-IW-ID";
static const char *DefaultTimezone = "UTC";
static const char *DefaultFormatForDateFromString = "%Y-%m-%dT%H:%M:%S.%LZ";
static const char *DefaultPostgresFormatForDateString = "YYYY-MM-DD HH24:MI:SS.MS";
static const char *DefaultIsoPostgresFormatForDateString = "IYYY-IW-ID HH24:MI:SS.MS";
static const int DefaultFormatLenForDateFromString = 21;
static const int QuartersPerYear = 4;
static const int MonthsPerQuarter = 3;
static const int MaxMinsRepresentedByUTCOffset = 6039;

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */
static Datum AddIntervalToTimestampWithPgTry(Datum timestamp, Datum interval,
											 bool *isResultOverflow);
static inline int CompareDateFormatMap(const void *a, const void *b);
static inline void ConstructDateStringFromParts(
	DollarDateFromPartsBsonValue *dateFromParts,
	char *dateString);
static inline void CheckIfRequiredPartsArePresent(
	DollarDateFromPartsBsonValue *dateFromParts,
	char *dateString, bool *isInputValid,
	bool isOnErrorPresent);
static inline void DeepFreeFormatArray(char **elements, int sizeOfSplitFormat);
static int GetAdjustHourWithTimezoneForDateAddSubtract(Datum startTimestamp, Datum
													   timestampIntervalAdjusted,
													   DateUnit unitEnum,
													   ExtensionTimezone timezoneToApply);
static bool IsAmountRangeValidForDateUnit(int64 amountVal, DateUnit unit);
static inline bool IsArgumentForDateDiffNull(bson_value_t *startDate,
											 bson_value_t *endDate,
											 bson_value_t *unit, bson_value_t *timezone,
											 bson_value_t *startOfWeek);
static inline bool IsArgumentForDateFromStringNull(bson_value_t *dateString,
												   bson_value_t *format,
												   bson_value_t *timezone,
												   bool *isDateStringNull,
												   bool isTimezoneProvided);
static inline bool IsArgumentForDateTruncNull(bson_value_t *date, bson_value_t *unit,
											  bson_value_t *binSize,
											  bson_value_t *timezone,
											  bson_value_t *startOfWeek);
static inline bool IsBsonValueDateTime(bson_type_t bsonType);
static inline bool IsDateDiffArgumentConstant(DollarDateDiffArgumentState *
											  dateDiffArgumentState);
static inline bool IsDateFromStringArgumentConstant(DollarDateFromStringArgumentState *
													dateFromStringArguments);
static inline bool IsDateTruncArgumentConstant(DollarDateTruncArgumentState *
											   dateTruncArgument);
static inline bool IsDateFromPartsArgumentConstant(DollarDateFromParts *datePart);
static inline int IsFormatSpecifierExist(char *formatSpecifier);
static inline bool IsUnitAndStartOfWeekConstant(AggregationExpressionData *unit,
												AggregationExpressionData *startOfWeek,
												char *opName);
static inline bool IsInputForDatePartNull(
	DollarDateFromPartsBsonValue *dateFromPartsValue,
	bool isIsoWeekDate);
static inline bool IsValidUnitForDateBinOid(DateTruncUnit dateTruncUnit);
static void HandleCommonParseForDateAddSubtract(char *opName, bool isDateAdd, const
												bson_value_t *argument,
												AggregationExpressionData *data,
												ParseAggregationExpressionContext *context);
static void HandleCommonPreParsedForDateAddSubtract(char *opName, bool isDateAdd,
													pgbson *doc, void *arguments,
													ExpressionResult *expressionResult);
static void HandleDatePartOperator(pgbson *doc, const bson_value_t *operatorValue,
								   ExpressionResult *expressionResult,
								   const char *operatorName, DatePart datePart);
static bool ParseDatePartOperatorArgument(const bson_value_t *operatorValue,
										  const char *operatorName,
										  bson_value_t *dateExpression,
										  bson_value_t *timezoneExpression);
static void ParseInputDocumentForDateDiff(const bson_value_t *inputArgument,
										  bson_value_t *startDate,
										  bson_value_t *endDate,
										  bson_value_t *unit,
										  bson_value_t *timezone,
										  bson_value_t *startOfWeek);
static void ParseInputDocumentForDateTrunc(const bson_value_t *inputArgument,
										   bson_value_t *date, bson_value_t *unit,
										   bson_value_t *binSize, bson_value_t *timezone,
										   bson_value_t *startOfWeek);
static void ParseInputForDateFromParts(const bson_value_t *argument, bson_value_t *year,
									   bson_value_t *isoWeekYear, bson_value_t *month,
									   bson_value_t *isoWeek,
									   bson_value_t *isoDayOfWeek, bson_value_t *day,
									   bson_value_t *hour, bson_value_t *minute,
									   bson_value_t *second, bson_value_t *millisecond,
									   bson_value_t *timezone, bool *isIsoWeekDate);
static void ParseInputForDateFromString(const bson_value_t *argument,
										bson_value_t *dateString, bson_value_t *format,
										bson_value_t *timezone,
										bson_value_t *onNull, bson_value_t *onError,
										bool *isTimezoneProvided);
static void ParseInputForDollarDateAddSubtract(const bson_value_t *inputArgument,
											   char *opName,
											   bson_value_t *startDate,
											   bson_value_t *unit,
											   bson_value_t *amount,
											   bson_value_t *timezone);
static void ParseDateStringWithFormat(StringView dateString, char **elements, int
									  sizeOfSplitFormat,
									  DollarDateFromPartsBsonValue *dateFromParts,
									  bool *isInputValid, bool isOnErrorPresent);
static ExtensionTimezone ParseTimezone(StringView timezone);
static int64_t ParseUtcOffset(StringView offset);
static inline void ParseUtcOffsetForDateString(char *dateString, int sizeOfDateString,
											   int *indexOfDateStringIter,
											   char *timezoneOffset,
											   int *timezoneOffsetLen, bool
											   isOnErrorPresent, bool *isInputValid);
static int32_t DetermineUtcOffsetForEpochWithTimezone(int64_t unixEpoch,
													  ExtensionTimezone timezone);
static uint32_t GetDatePartFromPgTimestamp(Datum pgTimestamp, DatePart datePart);
static StringView GetDateStringWithFormat(int64_t dateInMs, ExtensionTimezone timezone,
										  StringView format);
static DateUnit GetDateUnitFromString(char *unit);
static inline const char * GetDateUnitStringFromEnum(DateUnit unitEnum);
static inline int GetDayOfWeek(int year, int month, int day);
static inline int GetDifferenceInDaysForStartOfWeek(int dow, WeekDay weekdayEnum);
static float8 GetEpochDiffForDateDiff(DateUnit dateUnitEnum, ExtensionTimezone
									  timezoneToApply, int64 startDateEpoch, int64
									  endDateEpoch);
static Datum GetIntervalFromBinSize(int64_t binSize, DateTruncUnit dateTruncUnit);
static int GetIsoWeeksForYear(int64 year);
static inline int GetMonthIndexFromString(char *monthName, bool isAbbreviated);
static Datum GetPgTimestampAdjustedToTimezone(Datum timestamp, ExtensionTimezone
											  timezoneToApply);
static Datum GetPgTimestampFromEpochWithTimezone(int64_t epochInMs, ExtensionTimezone
												 timezone);
static Datum GetPgTimestampFromEpochWithoutTimezone(int64_t epochInMs,
													ExtensionTimezone timezone);
static Datum GetPgTimestampFromUnixEpoch(int64_t epochInMs);
static inline int64_t GetUnixEpochFromPgTimestamp(Datum timestamp);
static inline void ReadThroughDatePart(char *dateString, int *indexOfDateStringIter,
									   int maxCharsToRead, char *dateElement,
									   bool areAllNumbers);
static inline void SetDefaultValueForDatePart(bson_value_t *datePart, bson_type_t
											  bsonType, int64 defaultValue);
static void SetResultForDateDiff(bson_value_t *startDate, bson_value_t *endDate,
								 DateUnit dateUnitEnum, WeekDay weekdayEnum,
								 ExtensionTimezone timezoneToApply, bson_value_t *result);
static void SetResultForDateFromParts(DollarDateFromPartsBsonValue *dateFromPartsValue,
									  ExtensionTimezone timezoneToApply,
									  bson_value_t *result);
static void SetResultForDateFromIsoParts(DollarDateFromPartsBsonValue *dateFromPartsValue,
										 ExtensionTimezone timezoneToApply,
										 bson_value_t *result);
static void SetResultValueForDateFromString(DollarDateFromPartsBsonValue *dateFromParts,
											ExtensionTimezone timezoneToApply,
											bson_value_t *result);
static void SetResultValueForDateFromStringInputDayOfYear(
	DollarDateFromPartsBsonValue *dateFromParts,
	ExtensionTimezone
	timezoneToApply,
	bson_value_t *result);
static void SetResultValueForDateTruncFromDateBin(Datum pgTimestamp, Datum
												  referenceTimestamp, ExtensionTimezone
												  timezoneToApply, int64 binSize,
												  DateTruncUnit dateTruncUnit,
												  bson_value_t *result);
static void SetResultValueForDayUnitDateTrunc(Datum pgTimestamp, ExtensionTimezone
											  timezoneToApply, int64 binSize,
											  DateTruncUnit dateTruncUnit,
											  bson_value_t *result);
static void SetResultValueForDollarDateTrunc(DateTruncUnit dateTruncUnit, WeekDay weekDay,
											 ExtensionTimezone timezoneToApply,
											 ExtensionTimezone resultTimezone,
											 bson_value_t *binSize, bson_value_t *date,
											 bson_value_t *result);
static void SetResultValueForMonthUnitDateTrunc(Datum pgTimestamp, ExtensionTimezone
												timezoneToApply, int64 binSize,
												DateTruncUnit dateTruncUnit,
												bson_value_t *result);
static void SetResultValueForWeekUnitDateTrunc(Datum pgTimestamp, ExtensionTimezone
											   timezoneToApply, int64 binSize,
											   DateTruncUnit dateTruncUnit,
											   WeekDay startOfWeek,
											   bson_value_t *result);
static void SetResultValueForYearUnitDateTrunc(Datum pgTimestamp, ExtensionTimezone
											   timezoneToApply, int64 binSize,
											   DateTruncUnit dateTruncUnit,
											   bson_value_t *result);
static void SplitFormatString(StringView formatStrview, char **elements,
							  int *numElements);
static bool TryParseTwoDigitNumber(StringView str, uint32_t *result);
static Datum TruncateTimestampToPrecision(Datum timestamp, const char *precisionUnit);
static void ValidateArgumentsForDateTrunc(bson_value_t *binSize, bson_value_t *date,
										  bson_value_t *startOfWeek,
										  bson_value_t *timezone, bson_value_t *unit,
										  DateTruncUnit *dateTruncUnitEnum,
										  WeekDay *weekDay,
										  ExtensionTimezone *timezoneToApply,
										  ExtensionTimezone resultTimezone);
static void ValidateDatePartFromDateString(int indexOfDateFormatMap, char *dateString, int
										   datStringLen, int *indexOfDateStringIter,
										   DollarDateFromPartsBsonValue *dateFromParts,
										   bool *isInputValid, int *isIsoWeekDateFmt,
										   bool isOnErrorPresent);
static void ValidateInputArgumentForDateDiff(bson_value_t *startDate,
											 bson_value_t *endDate,
											 bson_value_t *unit, bson_value_t *timezone,
											 bson_value_t *startOfWeek,
											 DateUnit *dateUnitEnum,
											 WeekDay *weekDayEnum);
static void ValidateInputForDateFromString(bson_value_t *dateString, bson_value_t *format,
										   bson_value_t *timezone, bson_value_t *onError,
										   DollarDateFromPartsBsonValue *dateFromParts,
										   ExtensionTimezone *timezoneToApply,
										   bool *isInputValid);
static void ValidateDatePart(DatePart datePart, bson_value_t *inputValue, char *inputKey);
static void ValidateInputForDateFromParts(
	DollarDateFromPartsBsonValue *dateFromPartsValue, bool isIsoWeekDate);
static void ValidateInputForDollarDateAddSubtract(char *opName, bool isDateAdd,
												  bson_value_t *startDate,
												  bson_value_t *unit,
												  bson_value_t *amount,
												  bson_value_t *timezone);
static void ValidateOffsetMinutes(char *dateString, int sizeOfDateString,
								  int *indexOfDateStringIter,
								  DollarDateFromPartsBsonValue *dateFromParts,
								  bool *isInputValid, bool isOnErrorPresent);
static void ValidateTimezoneOffsetForDateString(char *dateString, int sizeOfDateString,
												int *indexOfDateStringIter,
												DollarDateFromPartsBsonValue *
												dateFromParts,
												bool *isInputValid, bool
												isOnErrorPresent);
static void VerifyAndParseFormatStringForDateString(bson_value_t *dateString,
													bson_value_t *format,
													DollarDateFromPartsBsonValue *
													dateFromParts,
													bool *isInputValid, bool
													isOnErrorPresent);
static DateUnit GetDateUnitFromString(char *unit);
static void SetResultForDollarDateAddSubtract(bson_value_t *startDate, DateUnit unitEum,
											  int64 amount,
											  ExtensionTimezone timezoneToApply,
											  char *opName,
											  bool isDateAdd,
											  bson_value_t *result);
static void SetResultForDollarDateSubtract(bson_value_t *startDate, DateUnit unitEum,
										   int64 amount,
										   ExtensionTimezone timezoneToApply,
										   bson_value_t *result);

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


/*
 * This is a helper method to throw error when unit does not matches the type of utf8 for date
 */
static inline void
pg_attribute_noreturn() ThrowLocation5439013Error(bson_type_t foundType, char * opName)
{
	ereport(ERROR, (errcode(MongoLocation5439013), errmsg(
						"%s requires 'unit' to be a string, but got %s",
						opName, BsonTypeName(foundType)),
					errhint("%s requires 'unit' to be a string, but got %s",
							opName, BsonTypeName(foundType))));
}

/*
 * This is a helper method to throw error when startOfWeek does not matches the type of utf8 for date
 */
static inline void
pg_attribute_noreturn() ThrowLocation5439015Error(bson_type_t foundType, char * opName)
{
	ereport(ERROR, (errcode(MongoLocation5439015), errmsg(
						"%s requires 'startOfWeek' to be a string, but got %s",
						opName, BsonTypeName(foundType)),
					errhint("%s requires 'startOfWeek' to be a string, but got %s",
							opName, BsonTypeName(foundType))));
}

/* Helper method that throws common ConversionFailure when a timezone string is not parseable by format. */
static inline void
pg_attribute_noreturn() ThrowMongoConversionErrorForTimezoneIdentifier(
	char * dateString,
	int sizeOfDateString,
	int * indexOfDateStringIter)
{
	ereport(ERROR, (errcode(MongoConversionFailure), errmsg(
						"Error parsing date string '%s'; %d: passing a time zone identifier as part of the string is not allowed '%c'",
						dateString, sizeOfDateString, dateString[*indexOfDateStringIter]),
					errhint(
						"Error parsing date string. passing a time zone identifier as part of the string is not allowed '%c'",
						dateString[*indexOfDateStringIter])));
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
				key = bson_iter_key(&documentIter);
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
	char *allocBuffer = NULL;
	char *buffer;
	char tmp[256];

	/* If we need less than 256 bytes, allocate it on the stack, if greater, on the heap. */
	if (bufferSize > 256)
	{
		allocBuffer = palloc0(sizeof(char) * bufferSize);
		buffer = allocBuffer;
	}
	else
	{
		buffer = tmp;
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

	if (allocBuffer != NULL)
	{
		/* We allocated on the heap and need to free the buffer. */
		pfree(allocBuffer);
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


/* Given a Unix epoch in milliseconds and a timezone (utc offset or an Olson Timezone Identifier) it returns a
 * Datum holding a Timestamp instance adjusted to the provided timezone in order to use for date operations with postgres.
 * It assumes the timezone is valid and was created with the ParseTimezone method. */
static Datum
GetPgTimestampFromEpochWithoutTimezone(int64_t epochInMs, ExtensionTimezone timezone)
{
	if (timezone.isUtcOffset)
	{
		epochInMs -= timezone.offsetInMs;
		return GetPgTimestampFromUnixEpoch(epochInMs);
	}


	return OidFunctionCall2(PostgresTimestampToZoneWithoutTzFunctionId(),
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


/*
 * New Method Implementation for aggrgegation operators
 */


/* This function handles $dateFromParts after parsing and gives the result as BSON_TYPE_DATETIME */
void
HandlePreParsedDollarDateFromParts(pgbson *doc, void *arguments,
								   ExpressionResult *expressionResult)
{
	DollarDateFromParts *dollarOpArgs = arguments;
	bool isNullOnEmpty = false;

	DollarDateFromPartsBsonValue dateFromPartsValue = {
		.hour = { 0 },
		.minute = { 0 },
		.second = { 0 },
		.millisecond = { 0 },
		.timezone = { 0 },
		.isoWeekYear = { 0 },
		.isoWeek = { 0 },
		.isoDayOfWeek = { 0 },
		.year = { 0 },
		.month = { 0 },
		.day = { 0 },
	};
	ExpressionResult childExpression = ExpressionResultCreateChild(expressionResult);
	EvaluateAggregationExpressionData(&dollarOpArgs->hour, doc,
									  &childExpression,
									  isNullOnEmpty);
	dateFromPartsValue.hour = childExpression.value;
	ExpressionResultReset(&childExpression);

	EvaluateAggregationExpressionData(&dollarOpArgs->minute, doc, &childExpression,
									  isNullOnEmpty);
	dateFromPartsValue.minute = childExpression.value;
	ExpressionResultReset(&childExpression);

	EvaluateAggregationExpressionData(&dollarOpArgs->second, doc, &childExpression,
									  isNullOnEmpty);
	dateFromPartsValue.second = childExpression.value;
	ExpressionResultReset(&childExpression);

	EvaluateAggregationExpressionData(&dollarOpArgs->millisecond, doc, &childExpression,
									  isNullOnEmpty);
	dateFromPartsValue.millisecond = childExpression.value;
	ExpressionResultReset(&childExpression);

	EvaluateAggregationExpressionData(&dollarOpArgs->timezone, doc, &childExpression,
									  isNullOnEmpty);
	dateFromPartsValue.timezone = childExpression.value;
	ExpressionResultReset(&childExpression);

	if (dollarOpArgs->isISOWeekDate)
	{
		/* If using ISO week date parts */
		EvaluateAggregationExpressionData(&dollarOpArgs->isoWeekYear, doc,
										  &childExpression,
										  isNullOnEmpty);
		dateFromPartsValue.isoWeekYear = childExpression.value;
		ExpressionResultReset(&childExpression);

		EvaluateAggregationExpressionData(&dollarOpArgs->isoWeek, doc, &childExpression,
										  isNullOnEmpty);
		dateFromPartsValue.isoWeek = childExpression.value;
		ExpressionResultReset(&childExpression);

		EvaluateAggregationExpressionData(&dollarOpArgs->isoDayOfWeek, doc,
										  &childExpression,
										  isNullOnEmpty);
		dateFromPartsValue.isoDayOfWeek = childExpression.value;
		ExpressionResultReset(&childExpression);
	}
	else
	{
		EvaluateAggregationExpressionData(&dollarOpArgs->year, doc, &childExpression,
										  isNullOnEmpty);
		dateFromPartsValue.year = childExpression.value;
		ExpressionResultReset(&childExpression);

		EvaluateAggregationExpressionData(&dollarOpArgs->month, doc, &childExpression,
										  isNullOnEmpty);
		dateFromPartsValue.month = childExpression.value;
		ExpressionResultReset(&childExpression);

		EvaluateAggregationExpressionData(&dollarOpArgs->day, doc, &childExpression,
										  isNullOnEmpty);
		dateFromPartsValue.day = childExpression.value;
		ExpressionResultReset(&childExpression);
	}


	bson_value_t result = { .value_type = BSON_TYPE_DATE_TIME };
	if (IsInputForDatePartNull(&dateFromPartsValue, dollarOpArgs->isISOWeekDate))
	{
		result.value_type = BSON_TYPE_NULL;
		ExpressionResultSetValue(expressionResult, &result);
		return;
	}

	ValidateInputForDateFromParts(&dateFromPartsValue, dollarOpArgs->isISOWeekDate);
	StringView timezoneToParse = {
		.string = dateFromPartsValue.timezone.value.v_utf8.str,
		.length = dateFromPartsValue.timezone.value.v_utf8.len
	};
	ExtensionTimezone timezoneToApply = ParseTimezone(timezoneToParse);
	if (!dollarOpArgs->isISOWeekDate)
	{
		SetResultForDateFromParts(&dateFromPartsValue, timezoneToApply, &result);
	}
	else
	{
		SetResultForDateFromIsoParts(&dateFromPartsValue, timezoneToApply, &result);
	}

	ExpressionResultSetValue(expressionResult, &result);
}


/* This function parses the input argument of the format : */

/* {
 *  $dateFromParts : {
 *      'year': <year>, 'month': <month>, 'day': <day>,
 *      'hour': <hour>, 'minute': <minute>, 'second': <second>,
 *      'millisecond': <ms>, 'timezone': <tzExpression>
 *  }
 * }
 * This function also validates the input expression and stores the result in case all the arguments are constant
 */
void
ParseDollarDateFromParts(const bson_value_t *argument, AggregationExpressionData *data,
						 ParseAggregationExpressionContext *context)
{
	if (argument->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(MongoLocation40519), errmsg(
							"$dateFromParts only supports an object as its argument")));
	}

	bson_value_t year = { 0 };
	bson_value_t isoWeekYear = { 0 };
	bson_value_t month = { 0 };
	bson_value_t isoWeek = { 0 };
	bson_value_t isoDayOfWeek = { 0 };
	bson_value_t day = { 0 };
	bson_value_t hour = { 0 };
	bson_value_t minute = { 0 };
	bson_value_t second = { 0 };
	bson_value_t millisecond = { 0 };
	bson_value_t timezone = {
		.value_type = BSON_TYPE_UTF8, .value.v_utf8.str = "UTC", .value.v_utf8.len = 3
	};

	bool isIsoWeekDate;
	ParseInputForDateFromParts(argument, &year, &isoWeekYear, &month, &isoWeek,
							   &isoDayOfWeek, &day, &hour, &minute, &second, &millisecond,
							   &timezone, &isIsoWeekDate);

	DollarDateFromParts *dateFromParts = palloc0(sizeof(DollarDateFromParts));

	/* Set the value is iso date format or not. */
	dateFromParts->isISOWeekDate = isIsoWeekDate;

	if (dateFromParts->isISOWeekDate)
	{
		ParseAggregationExpressionData(&dateFromParts->isoWeekYear, &isoWeekYear,
									   context);
		ParseAggregationExpressionData(&dateFromParts->isoWeek, &isoWeek, context);
		ParseAggregationExpressionData(&dateFromParts->isoDayOfWeek, &isoDayOfWeek,
									   context);
	}
	else
	{
		ParseAggregationExpressionData(&dateFromParts->year, &year, context);
		ParseAggregationExpressionData(&dateFromParts->month, &month, context);
		ParseAggregationExpressionData(&dateFromParts->day, &day, context);
	}
	ParseAggregationExpressionData(&dateFromParts->hour, &hour, context);
	ParseAggregationExpressionData(&dateFromParts->minute, &minute, context);
	ParseAggregationExpressionData(&dateFromParts->second, &second, context);
	ParseAggregationExpressionData(&dateFromParts->millisecond, &millisecond, context);
	ParseAggregationExpressionData(&dateFromParts->timezone, &timezone, context);

	if (IsDateFromPartsArgumentConstant(dateFromParts))
	{
		bson_value_t result = { .value_type = BSON_TYPE_DATE_TIME };

		DollarDateFromPartsBsonValue dateFromPartsBsonValue = {
			.hour = dateFromParts->hour.value,
			.minute = dateFromParts->minute.value,
			.second = dateFromParts->second.value,
			.millisecond = dateFromParts->millisecond.value,
			.timezone = dateFromParts->timezone.value,
			.isoWeekYear = dateFromParts->isoWeekYear.value,
			.isoWeek = dateFromParts->isoWeek.value,
			.isoDayOfWeek = dateFromParts->isoDayOfWeek.value,
			.year = dateFromParts->year.value,
			.month = dateFromParts->month.value,
			.day = dateFromParts->day.value,
		};
		if (IsInputForDatePartNull(&dateFromPartsBsonValue,
								   dateFromParts->isISOWeekDate))
		{
			result.value_type = BSON_TYPE_NULL;
			data->value = result;
			data->kind = AggregationExpressionKind_Constant;
			pfree(dateFromParts);
			return;
		}

		ValidateInputForDateFromParts(&dateFromPartsBsonValue,
									  dateFromParts->isISOWeekDate);
		StringView timezoneToParse = {
			.string = dateFromParts->timezone.value.value.v_utf8.str,
			.length = dateFromParts->timezone.value.value.v_utf8.len
		};
		ExtensionTimezone timezoneToApply = ParseTimezone(timezoneToParse);
		if (!dateFromParts->isISOWeekDate)
		{
			SetResultForDateFromParts(&dateFromPartsBsonValue, timezoneToApply, &result);
		}
		else
		{
			SetResultForDateFromIsoParts(&dateFromPartsBsonValue, timezoneToApply,
										 &result);
		}

		data->value = result;
		data->kind = AggregationExpressionKind_Constant;
		pfree(dateFromParts);
	}
	else
	{
		data->operator.arguments = dateFromParts;
		data->operator.argumentsKind = AggregationExpressionArgumentsKind_Palloc;
	}
}


/* A function which when given a year tells if that year has 52 / 53 ISO weeks. */
/* This is required for adjusting the isoweeks when input of isoweeks is 0. */
/* We tend to reduce isoYear argument by 1 and set the value to isoWeek as 52/53. */
static int
GetIsoWeeksForYear(int64 year)
{
	int dowDec28 = GetDayOfWeek(year, 12, 28);

	/* ISO 8601 specifies that if December 28 is a Monday, then the year has 53 weeks. */
	/* If the day of the week for December 28 is Monday (1), then the year has 53 weeks. */
	/* Otherwise, it has 52 weeks. */
	return (dowDec28 == 1) ? 53 : 52;
}


/* Function to calculate the day of the week for the given year month and day
 *  Sakamoto’s algorithm is used here for its simplicity and effectiveness
 *  This proposes to give data in O(1) taking into account the leap year as well.
 */
static inline int
GetDayOfWeek(int year, int month, int day)
{
	static int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
	year -= month < 3;
	return (year + (year / 4) - (year / 100) + (year / 400) + t[month - 1] + day) % 7;
}


/* A utility function to check if the struct DollarDateFromParts has constant values. */
/* This helps us optimize the flow */
static inline bool
IsDateFromPartsArgumentConstant(DollarDateFromParts *datePart)
{
	if (!IsAggregationExpressionConstant(&datePart->hour) ||
		!IsAggregationExpressionConstant(&datePart->minute) ||
		!IsAggregationExpressionConstant(&datePart->second) ||
		!IsAggregationExpressionConstant(&datePart->millisecond) ||
		!IsAggregationExpressionConstant(&datePart->timezone))
	{
		return false;
	}

	/* If datePart uses ISO week dates, check ISO week components */
	if (datePart->isISOWeekDate)
	{
		return IsAggregationExpressionConstant(&datePart->isoWeekYear) &&
			   IsAggregationExpressionConstant(&datePart->isoWeek) &&
			   IsAggregationExpressionConstant(&datePart->isoDayOfWeek);
	}

	/* Otherwise, check year, month, day, and minute components */
	return IsAggregationExpressionConstant(&datePart->year) &&
		   IsAggregationExpressionConstant(&datePart->month) &&
		   IsAggregationExpressionConstant(&datePart->day);
}


/* A function to check if any input part is null. */
/* In case of any null input we should not process any further and return null as output. */
static inline bool
IsInputForDatePartNull(DollarDateFromPartsBsonValue *dateFromPartsValue, bool
					   isIsoWeekDate)
{
	if (IsExpressionResultNullOrUndefined(&dateFromPartsValue->hour) ||
		IsExpressionResultNullOrUndefined(&dateFromPartsValue->minute) ||
		IsExpressionResultNullOrUndefined(&dateFromPartsValue->second) ||
		IsExpressionResultNullOrUndefined(&dateFromPartsValue->millisecond) ||
		IsExpressionResultNullOrUndefined(&dateFromPartsValue->timezone))
	{
		return true;
	}

	/* Check based on whether the date uses ISO week dates */
	if (isIsoWeekDate)
	{
		return IsExpressionResultNullOrUndefined(&dateFromPartsValue->isoWeekYear) ||
			   IsExpressionResultNullOrUndefined(&dateFromPartsValue->isoWeek) ||
			   IsExpressionResultNullOrUndefined(&dateFromPartsValue->isoDayOfWeek);
	}

	/* Check for null or undefined in standard date parts */
	return IsExpressionResultNullOrUndefined(&dateFromPartsValue->year) ||
		   IsExpressionResultNullOrUndefined(&dateFromPartsValue->month) ||
		   IsExpressionResultNullOrUndefined(&dateFromPartsValue->day);
}


/* A function which takes in the input argument of format $dateFromParts: {input document arg} */
/* It extracts the required fields from the document. */
static void
ParseInputForDateFromParts(const bson_value_t *argument, bson_value_t *year,
						   bson_value_t *isoWeekYear, bson_value_t *month,
						   bson_value_t *isoWeek,
						   bson_value_t *isoDayOfWeek, bson_value_t *day,
						   bson_value_t *hour, bson_value_t *minute,
						   bson_value_t *second, bson_value_t *millisecond,
						   bson_value_t *timezone, bool *isIsoWeekDate)
{
	*isIsoWeekDate = false;
	bson_iter_t docIter;
	BsonValueInitIterator(argument, &docIter);
	while (bson_iter_next(&docIter))
	{
		const char *key = bson_iter_key(&docIter);
		if (strcmp(key, "year") == 0)
		{
			*year = *bson_iter_value(&docIter);
		}
		else if (strcmp(key, "isoWeekYear") == 0)
		{
			*isoWeekYear = *bson_iter_value(&docIter);
			*isIsoWeekDate = true;
		}
		else if (strcmp(key, "month") == 0)
		{
			*month = *bson_iter_value(&docIter);
		}
		else if (strcmp(key, "isoWeek") == 0)
		{
			*isoWeek = *bson_iter_value(&docIter);
			*isIsoWeekDate = true;
		}
		else if (strcmp(key, "day") == 0)
		{
			*day = *bson_iter_value(&docIter);
		}
		else if (strcmp(key, "isoDayOfWeek") == 0)
		{
			*isoDayOfWeek = *bson_iter_value(&docIter);
			*isIsoWeekDate = true;
		}
		else if (strcmp(key, "hour") == 0)
		{
			*hour = *bson_iter_value(&docIter);
		}
		else if (strcmp(key, "minute") == 0)
		{
			*minute = *bson_iter_value(&docIter);
		}
		else if (strcmp(key, "second") == 0)
		{
			*second = *bson_iter_value(&docIter);
		}
		else if (strcmp(key, "millisecond") == 0)
		{
			*millisecond = *bson_iter_value(&docIter);
		}
		else if (strcmp(key, "timezone") == 0)
		{
			*timezone = *bson_iter_value(&docIter);
		}
		else
		{
			ereport(ERROR, (errcode(MongoLocation40518), errmsg(
								"Unrecognized argument to $dateFromParts: %s", key),
							errhint(
								"Unrecognized argument to $dateFromParts, unexpected key")));
		}
	}

	/* Both the isodate part and normal date part present */
	if (*isIsoWeekDate && year->value_type != BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(MongoLocation40489), errmsg(
							"$dateFromParts does not allow mixing natural dates with ISO dates")));
	}

	/* In case of ISO date normal date part should not be present */
	if (*isIsoWeekDate && (month->value_type != BSON_TYPE_EOD || day->value_type !=
						   BSON_TYPE_EOD))
	{
		ereport(ERROR, (errcode(MongoLocation40525), errmsg(
							"$dateFromParts does not allow mixing natural dates with ISO dates")));
	}

	/* Either year or isoWeekYear is a must given it's type is iso format or non-iso format */
	if ((!(*isIsoWeekDate) && year->value_type == BSON_TYPE_EOD) ||
		(*isIsoWeekDate && isoWeekYear->value_type == BSON_TYPE_EOD))
	{
		ereport(ERROR, (errcode(MongoLocation40516), errmsg(
							"$dateFromParts requires either 'year' or 'isoWeekYear' to be present")));
	}

	/* Set default values if not present */
	if (!(*isIsoWeekDate) && month->value_type == BSON_TYPE_EOD)
	{
		SetDefaultValueForDatePart(month, BSON_TYPE_INT64, 1);
	}
	if (!(*isIsoWeekDate) && day->value_type == BSON_TYPE_EOD)
	{
		SetDefaultValueForDatePart(day, BSON_TYPE_INT64, 1);
	}

	if (*isIsoWeekDate && isoDayOfWeek->value_type == BSON_TYPE_EOD)
	{
		SetDefaultValueForDatePart(isoDayOfWeek, BSON_TYPE_INT64, 1);
	}
	if (*isIsoWeekDate && isoWeek->value_type == BSON_TYPE_EOD)
	{
		SetDefaultValueForDatePart(isoWeek, BSON_TYPE_INT64, 1);
	}

	if (hour->value_type == BSON_TYPE_EOD)
	{
		SetDefaultValueForDatePart(hour, BSON_TYPE_INT64, 0);
	}
	if (minute->value_type == BSON_TYPE_EOD)
	{
		SetDefaultValueForDatePart(minute, BSON_TYPE_INT64, 0);
	}
	if (second->value_type == BSON_TYPE_EOD)
	{
		SetDefaultValueForDatePart(second, BSON_TYPE_INT64, 0);
	}
	if (millisecond->value_type == BSON_TYPE_EOD)
	{
		SetDefaultValueForDatePart(millisecond, BSON_TYPE_INT64, 0);
	}
}


/* This function takes is all the date parts and verifies the input type as per the expected behaviour. */
static void
ValidateInputForDateFromParts(DollarDateFromPartsBsonValue *dateFromPartsValue, bool
							  isIsoWeekDate)
{
	/* Validating Input type */
	if (isIsoWeekDate)
	{
		ValidateDatePart(DatePart_IsoWeekYear, &dateFromPartsValue->isoWeekYear,
						 "isoWeekYear");
		ValidateDatePart(DatePart_IsoWeek, &dateFromPartsValue->isoWeek, "isoWeek");
		ValidateDatePart(DatePart_IsoDayOfWeek, &dateFromPartsValue->isoDayOfWeek,
						 "isoDayOfWeek");
	}
	else
	{
		ValidateDatePart(DatePart_Year, &dateFromPartsValue->year, "year");
		ValidateDatePart(DatePart_Month, &dateFromPartsValue->month, "month");
		ValidateDatePart(DatePart_DayOfMonth, &dateFromPartsValue->day, "day");
	}

	ValidateDatePart(DatePart_Hour, &dateFromPartsValue->hour, "hour");
	ValidateDatePart(DatePart_Minute, &dateFromPartsValue->minute, "minute");
	ValidateDatePart(DatePart_Second, &dateFromPartsValue->second, "second");
	ValidateDatePart(DatePart_Millisecond, &dateFromPartsValue->millisecond,
					 "millisecond");

	if (dateFromPartsValue->timezone.value_type != BSON_TYPE_UTF8)
	{
		ereport(ERROR, (errcode(MongoLocation40517), errmsg(
							"timezone must evaluate to a string, found %s", BsonTypeName(
								dateFromPartsValue->timezone.value_type)),
						errhint(
							"timezone must evaluate to a string, found %s", BsonTypeName(
								dateFromPartsValue->timezone.value_type)
							)));
	}
}


/* A function which based on the input date part type validates the input range and input type */
static void
ValidateDatePart(DatePart datePart, bson_value_t *inputValue, char *inputKey)
{
	/* Validate input type */
	bool checkFixedInteger = false;
	if (!BsonValueIsNumber(inputValue) || !IsBsonValueFixedInteger(inputValue) ||
		(datePart == DatePart_Millisecond && !IsBsonValue64BitInteger(inputValue,
																	  checkFixedInteger)))
	{
		ereport(ERROR, (errcode(MongoLocation40515), errmsg(
							"'%s' must evaluate to an integer, found %s with value :%s",
							inputKey, BsonTypeName(inputValue->value_type),
							BsonValueToJsonForLogging(inputValue)),
						errhint(
							"'%s' must evaluate to an integer, found %s", inputKey,
							BsonTypeName(inputValue->value_type))));
	}
	int64 datePartValue = BsonValueAsInt64(inputValue);

	/* Validate Range */
	switch (datePart)
	{
		case DatePart_Year:
		case DatePart_IsoWeekYear:
		{
			if (datePartValue < 1 || datePartValue > 9999)
			{
				ereport(ERROR, (errcode(datePart == DatePart_Year ? MongoLocation40523 :
										MongoLocation31095), errmsg(
									"'%s' must evaluate to an integer in the range 1 to 9999, found %s",
									inputKey, BsonValueToJsonForLogging(inputValue)),
								errhint(
									"'%s' must evaluate to an integer in the range 1 to 9999, found %ld",
									inputKey, datePartValue)));
			}

			break;
		}

		case DatePart_Month:
		case DatePart_DayOfMonth:
		case DatePart_IsoWeek:
		case DatePart_IsoDayOfWeek:
		case DatePart_Hour:
		case DatePart_Minute:
		{
			if (datePartValue < -32768 || datePartValue > 32767)
			{
				ereport(ERROR, (errcode(MongoLocation31034), errmsg(
									"'%s' must evaluate to a value in the range [-32768, 32767]; value %s is not in range",
									inputKey, BsonValueToJsonForLogging(inputValue)),
								errhint(
									"'%s' must evaluate to a value in the range [-32768, 32767]; value %ld is not in range",
									inputKey, datePartValue)));
			}
			break;
		}

		case DatePart_Second:
		case DatePart_Millisecond:
		{
			/* as per the tests we should throw error for bit values >= 2^54 */
			if (datePartValue >= 18014398509481984 || datePartValue <= -18014398509481984)
			{
				ereport(ERROR, (errcode(MongoDurationOverflow), errmsg(
									"Overflow casting from a lower-precision duration to a higher-precision duration"
									)));
			}
			break;
		}

		default:
		{
			break;
		}
	}

	/* Convert all the values to same base post validation. */
	inputValue->value_type = BSON_TYPE_INT64;
	inputValue->value.v_int64 = datePartValue;
}


/* A common helper function to set default value for date part. */
static inline void
SetDefaultValueForDatePart(bson_value_t *datePart, bson_type_t bsonType, int64
						   defaultValue)
{
	datePart->value.v_int64 = defaultValue;
	datePart->value_type = bsonType;
}


/*
 * This function sets the result value after processing input for non-ISO date parts.
 */
static void
SetResultForDateFromParts(DollarDateFromPartsBsonValue *dateFromPartsValue,
						  ExtensionTimezone
						  timezoneToApply, bson_value_t *result)
{
	Datum timestampStart = GetPgTimestampFromUnixEpoch(DATE_FROM_PART_START_DATE_MS);
	Datum interval = GetIntervalFromDatePart(dateFromPartsValue->year.value.v_int64 - 1,
											 dateFromPartsValue->month.value.v_int64 - 1,
											 0,
											 dateFromPartsValue->day.value.v_int64 - 1,
											 dateFromPartsValue->hour.value.v_int64,
											 dateFromPartsValue->minute.value.v_int64,
											 dateFromPartsValue->second.value.v_int64,
											 dateFromPartsValue->millisecond.value.v_int64);

	Datum timestampPlusInterval = OidFunctionCall2(
		PostgresAddIntervalToTimestampFunctionId(),
		timestampStart, interval);

	Datum resultTimeWithZone;

	/* Optimization as all calculations are in UTC by default so, no need to change timezone. */
	if (!timezoneToApply.isUtcOffset && strcmp(timezoneToApply.id, DefaultTimezone) == 0)
	{
		resultTimeWithZone = timestampPlusInterval;
	}
	else
	{
		resultTimeWithZone = GetPgTimestampAdjustedToTimezone(timestampPlusInterval,
															  timezoneToApply);
	}

	Datum resultEpoch = OidFunctionCall2(PostgresDatePartFunctionId(),
										 CStringGetTextDatum("epoch"),
										 resultTimeWithZone);

	result->value.v_datetime = (int64) (DatumGetFloat8(resultEpoch) * 1000);
}


/*
 * This function sets the result value after processing input for isoDateParts
 */
static void
SetResultForDateFromIsoParts(DollarDateFromPartsBsonValue *dateFromPartsValue,
							 ExtensionTimezone timezoneToApply,
							 bson_value_t *result)
{
	int64 isoYearValue = dateFromPartsValue->isoWeekYear.value.v_int64;
	int64 isoWeekDaysValue = dateFromPartsValue->isoDayOfWeek.value.v_int64;
	int64 isoWeekValue = dateFromPartsValue->isoWeek.value.v_int64;

	/* This is a special handling for 0 as in this case as  $dateFromPartsValue carries or subtracts the difference from other date parts to calculate the date. */
	/* In this case we have to subtract year by 1 and based on what year has isoWeek we need to set that value it could be 52 or 53. */
	if (isoWeekValue == 0)
	{
		isoYearValue -= 1;
		isoWeekValue = GetIsoWeeksForYear(isoYearValue);
	}

	char buffer[25] = { 0 };
	sprintf(buffer, "%ld-%ld-%ld", isoYearValue,
			isoWeekValue,
			isoWeekDaysValue);
	Datum toDateResult = OidFunctionCall2(PostgresToDateFunctionId(),
										  CStringGetTextDatum(buffer),
										  CStringGetTextDatum(IsoDateFormat));
	int64 daysAdjust = 0;
	if (isoWeekDaysValue > 7)
	{
		daysAdjust += isoWeekDaysValue - 7;
	}
	else if (isoWeekDaysValue <= 0)
	{
		daysAdjust -= -isoWeekDaysValue + 7;
	}
	Datum interval = GetIntervalFromDatePart(0,
											 0,
											 0,
											 daysAdjust,
											 dateFromPartsValue->hour.value.v_int64,
											 dateFromPartsValue->minute.value.v_int64,
											 dateFromPartsValue->second.value.v_int64,
											 dateFromPartsValue->millisecond.value.v_int64);

	Datum datePlusInterval = OidFunctionCall2(PostgresAddIntervalToDateFunctionId(),
											  toDateResult, interval);

	Datum resultTimeWithZone;

	/* Optimization as all calculations are in UTC by default so, no need to change timezone. */
	if (!timezoneToApply.isUtcOffset && strcmp(timezoneToApply.id, DefaultTimezone) == 0)
	{
		resultTimeWithZone = datePlusInterval;
	}
	else
	{
		resultTimeWithZone = GetPgTimestampAdjustedToTimezone(datePlusInterval,
															  timezoneToApply);
	}

	Datum resultEpoch = OidFunctionCall2(PostgresDatePartFunctionId(),
										 CStringGetTextDatum("epoch"),
										 resultTimeWithZone);

	result->value.v_datetime = (int64) (DatumGetFloat8(resultEpoch) * 1000);
}


void
HandlePreParsedDollarDateTrunc(pgbson *doc, void *arguments,
							   ExpressionResult *expressionResult)
{
	DollarDateTruncArgumentState *dateTruncState = arguments;
	bson_value_t binSize = { 0 };
	bson_value_t date = { 0 };
	bson_value_t startOfWeek = { 0 };
	bson_value_t timezone = { 0 };
	bson_value_t unit = { 0 };

	bool isNullOnEmpty = false;
	ExpressionResult childExpression = ExpressionResultCreateChild(expressionResult);

	EvaluateAggregationExpressionData(&dateTruncState->date, doc,
									  &childExpression,
									  isNullOnEmpty);
	date = childExpression.value;

	ExpressionResultReset(&childExpression);
	EvaluateAggregationExpressionData(&dateTruncState->binSize, doc,
									  &childExpression,
									  isNullOnEmpty);
	binSize = childExpression.value;

	ExpressionResultReset(&childExpression);
	EvaluateAggregationExpressionData(&dateTruncState->unit, doc,
									  &childExpression,
									  isNullOnEmpty);
	unit = childExpression.value;

	ExpressionResultReset(&childExpression);
	EvaluateAggregationExpressionData(&dateTruncState->timezone, doc,
									  &childExpression,
									  isNullOnEmpty);
	timezone = childExpression.value;

	ExpressionResultReset(&childExpression);
	EvaluateAggregationExpressionData(&dateTruncState->startOfWeek, doc,
									  &childExpression,
									  isNullOnEmpty);
	startOfWeek = childExpression.value;

	bson_value_t result = { .value_type = BSON_TYPE_NULL };
	if (IsArgumentForDateTruncNull(&date, &unit, &binSize, &timezone, &startOfWeek))
	{
		ExpressionResultSetValue(expressionResult, &result);
		return;
	}

	ExtensionTimezone timezoneToApply;

	ExtensionTimezone resultTimezone = {
		.offsetInMs = 0,
		.isUtcOffset = true,
	};

	dateTruncState->dateTruncUnit = DateTruncUnit_Invalid;
	dateTruncState->weekDay = WeekDay_Invalid;

/* Validate the input symbols and modify binSize to int64 val and timezone to understandable format*/
	ValidateArgumentsForDateTrunc(&binSize,
								  &date,
								  &startOfWeek,
								  &timezone,
								  &unit,
								  &dateTruncState->dateTruncUnit,
								  &dateTruncState->weekDay,
								  &timezoneToApply,
								  resultTimezone);

	SetResultValueForDollarDateTrunc(dateTruncState->dateTruncUnit,
									 dateTruncState->weekDay, timezoneToApply,
									 resultTimezone, &binSize, &date, &result);

	ExpressionResultSetValue(expressionResult, &result);
}


void
ParseDollarDateTrunc(const bson_value_t *argument, AggregationExpressionData *data,
					 ParseAggregationExpressionContext *context)
{
	if (argument->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(MongoLocation5439007), errmsg(
							"$dateTrunc only supports an object as its argument")));
	}

	bson_value_t binSize = { 0 };
	bson_value_t date = { 0 };
	bson_value_t startOfWeek = { 0 };
	bson_value_t timezone = { 0 };
	bson_value_t unit = { 0 };

	ParseInputDocumentForDateTrunc(argument, &date, &unit, &binSize, &timezone,
								   &startOfWeek);

	DollarDateTruncArgumentState *dateTruncState = palloc0(
		sizeof(DollarDateTruncArgumentState));

	ParseAggregationExpressionData(&dateTruncState->date, &date, context);
	ParseAggregationExpressionData(&dateTruncState->unit, &unit, context);
	ParseAggregationExpressionData(&dateTruncState->binSize, &binSize, context);
	ParseAggregationExpressionData(&dateTruncState->timezone, &timezone, context);
	ParseAggregationExpressionData(&dateTruncState->startOfWeek, &startOfWeek, context);
	if (IsDateTruncArgumentConstant(dateTruncState))
	{
		/*add null check for behaviour */
		bson_value_t result = { .value_type = BSON_TYPE_NULL };
		if (IsArgumentForDateTruncNull(&dateTruncState->date.value,
									   &dateTruncState->unit.value,
									   &dateTruncState->binSize.value,
									   &dateTruncState->timezone.value,
									   &dateTruncState->startOfWeek.value))
		{
			data->value = result;
			data->kind = AggregationExpressionKind_Constant;
			pfree(dateTruncState);
			return;
		}
		ExtensionTimezone timezoneToApply;

		ExtensionTimezone resultTimezone = {
			.offsetInMs = 0,
			.isUtcOffset = true,
		};

		dateTruncState->dateTruncUnit = DateTruncUnit_Invalid;
		dateTruncState->weekDay = WeekDay_Invalid;

		/* Validate the input symbols and modify binSize to int64 val and timezone to understandable format*/
		ValidateArgumentsForDateTrunc(&dateTruncState->binSize.value,
									  &dateTruncState->date.value,
									  &dateTruncState->startOfWeek.value,
									  &dateTruncState->timezone.value,
									  &dateTruncState->unit.value,
									  &dateTruncState->dateTruncUnit,
									  &dateTruncState->weekDay,
									  &timezoneToApply,
									  resultTimezone);

		SetResultValueForDollarDateTrunc(dateTruncState->dateTruncUnit,
										 dateTruncState->weekDay, timezoneToApply,
										 resultTimezone,
										 &dateTruncState->binSize.value,
										 &dateTruncState->date.value, &result);
		data->value = result;
		data->kind = AggregationExpressionKind_Constant;
		pfree(dateTruncState);
	}
	else
	{
		data->operator.arguments = dateTruncState;
		data->operator.argumentsKind = AggregationExpressionArgumentsKind_Palloc;
	}
}


/* This function adds timestamp to interval and then returns a datum containing result. */
/* In case of overflowing timestamp we return NULL. */
static Datum
AddIntervalToTimestampWithPgTry(Datum timestamp, Datum interval, bool *isResultOverflow)
{
	/* making volatile as we need to initialise with the default value and getting compilation error. */
	volatile Datum resultTimestamp = 0;
	PG_TRY();
	{
		/* Add interval to timestamp*/
		resultTimestamp = OidFunctionCall2(PostgresAddIntervalToTimestampFunctionId(),
										   timestamp,
										   interval);
	}
	PG_CATCH();
	{
		*isResultOverflow = true;
	}
	PG_END_TRY();
	return resultTimestamp;
}


/* This function takes in unit and bin and gets the interval datum. */
static Datum
GetIntervalFromBinSize(int64_t binSize, DateTruncUnit dateTruncUnit)
{
	int64 year = 0, month = 0, week = 0, day = 0, hour = 0, minute = 0, second = 0,
		  millisecond = 0;
	switch (dateTruncUnit)
	{
		case DateTruncUnit_Millisecond:
		{
			millisecond = binSize;
			break;
		}

		case DateTruncUnit_Second:
		{
			second = binSize;
			break;
		}

		case DateTruncUnit_Minute:
		{
			minute = binSize;
			break;
		}

		case DateTruncUnit_Hour:
		{
			hour = binSize;
			break;
		}

		case DateTruncUnit_Day:
		{
			day = binSize;
			break;
		}

		case DateTruncUnit_Week:
		{
			week = binSize;
			break;
		}

		case DateTruncUnit_Month:
		{
			month = binSize;
			break;
		}

		case DateTruncUnit_Quarter:
		{
			month = binSize * 3;
			break;
		}

		case DateTruncUnit_Year:
		{
			year = binSize;
			break;
		}

		default:
			ereport(ERROR, (errcode(MongoLocation5439014), errmsg(
								"Invalid unit specified. Cannot make interval")));
	}

	return GetIntervalFromDatePart(year, month, week, day, hour, minute, second,
								   millisecond);
}


/* Given a datum timestamp and a timezone (utc offset or an Olson Timezone Identifier) it returns a
 * Datum holding a Timestamp instance adjusted to the provided timezone in order to use for date operations with postgres.
 * This function returns a timestamp shifted to the specified timezone.
 * It assumes the timezone is valid and was created with the ParseTimezone method.
 * This is a helper function which just transforms the timestamp to a particular timezone.
 * This function returns a datum containing instance of timestamp and not timestamptz.
 */
static Datum
GetPgTimestampAdjustedToTimezone(Datum timestamp, ExtensionTimezone timezoneToApply)
{
	/* No timezone is specified . All calculations are in UTC */
	if (timezoneToApply.offsetInMs == 0)
	{
		return timestamp;
	}

	/* Handling the utcoffset timezone */
	if (timezoneToApply.isUtcOffset)
	{
		/* changing the sign as original input is in UTC and +04:30 means we need to subtract -04:30 epochms to get utc timestamp */
		Datum interval = GetIntervalFromDatePart(0, 0, 0, 0, 0, 0, 0,
												 -timezoneToApply.offsetInMs);
		return OidFunctionCall2(PostgresAddIntervalToTimestampFunctionId(),
								timestamp, interval);
	}

	/* Adjusting to timezone specified. */
	return OidFunctionCall2(PostgresTimestampToZoneWithoutTzFunctionId(),
							CStringGetTextDatum(timezoneToApply.id), timestamp);
}


/* This function returns the unix epoch from PgTimestamp */
static inline int64_t
GetUnixEpochFromPgTimestamp(Datum timestamp)
{
	float8 resultSeconds = DatumGetFloat8(OidFunctionCall2(
											  PostgresDatePartFunctionId(),
											  CStringGetTextDatum(EPOCH),
											  timestamp));
	return resultSeconds * MILLISECONDS_IN_SECOND;
}


/*
 * This function takes in input as the argument of $dateTrunc and parses the given input document to find required values.
 */
static void
ParseInputDocumentForDateTrunc(const bson_value_t *inputArgument,
							   bson_value_t *date, bson_value_t *unit,
							   bson_value_t *binSize, bson_value_t *timezone,
							   bson_value_t *startOfWeek)
{
	bson_iter_t docIter;
	BsonValueInitIterator(inputArgument, &docIter);

	while (bson_iter_next(&docIter))
	{
		const char *key = bson_iter_key(&docIter);
		if (strcmp(key, "binSize") == 0)
		{
			*binSize = *bson_iter_value(&docIter);
		}
		else if (strcmp(key, "date") == 0)
		{
			*date = *bson_iter_value(&docIter);
		}
		else if (strcmp(key, "startOfWeek") == 0)
		{
			*startOfWeek = *bson_iter_value(&docIter);
		}
		else if (strcmp(key, "timezone") == 0)
		{
			*timezone = *bson_iter_value(&docIter);
		}
		else if (strcmp(key, "unit") == 0)
		{
			*unit = *bson_iter_value(&docIter);
		}
		else
		{
			ereport(ERROR, (errcode(MongoLocation5439008), errmsg(
								"Unrecognized argument to $dateTrunc: %s. Expected arguments are date, unit, and optionally, binSize, timezone, startOfWeek",
								key),
							errhint(
								"Unrecognized argument to $dateTrunc: %s. Expected arguments are date, unit, and optionally, binSize, timezone, startOfWeek",
								key)));
		}
	}

	/*	Validation to check date and unit are required. */
	if (date->value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(MongoLocation5439009), errmsg(
							"Missing 'date' parameter to $dateTrunc")));
	}

	if (unit->value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(MongoLocation5439010), errmsg(
							"Missing 'unit' parameter to $dateTrunc")));
	}

	if (binSize->value_type == BSON_TYPE_EOD)
	{
		binSize->value_type = BSON_TYPE_INT32;
		binSize->value.v_int32 = 1;
	}

	if (timezone->value_type == BSON_TYPE_EOD)
	{
		timezone->value_type = BSON_TYPE_UTF8;
		timezone->value.v_utf8.str = "UTC";
		timezone->value.v_utf8.len = 3;
	}

	if (startOfWeek->value_type == BSON_TYPE_EOD)
	{
		startOfWeek->value_type = BSON_TYPE_UTF8;
		startOfWeek->value.v_utf8.str = "Sunday";
		startOfWeek->value.v_utf8.len = 6;
	}
}


/* As per the behaviour if any argument is null return null */
static inline bool
IsArgumentForDateTruncNull(bson_value_t *date, bson_value_t *unit, bson_value_t *binSize,
						   bson_value_t *timezone, bson_value_t *startOfWeek)
{
	/* if unit is week then check startOfWeek else ignore */
	bool isUnitNull = IsExpressionResultNullOrUndefined(unit);

	/* Since unit is null we don't need to evaluate others*/
	if (isUnitNull)
	{
		return true;
	}

	/* If unit is not of type UTF8 then throw error as we cannot process for unit week and startOfWeek*/
	if (unit->value_type != BSON_TYPE_UTF8)
	{
		ThrowLocation5439013Error(unit->value_type, "$dateTrunc");
	}

	bool isUnitWeek = !isUnitNull && strcasecmp(unit->value.v_utf8.str, "week") == 0;
	if (isUnitNull ||
		(isUnitWeek && IsExpressionResultNullOrUndefined(startOfWeek)) ||
		IsExpressionResultNullOrUndefined(date) ||
		IsExpressionResultNullOrUndefined(binSize) ||
		IsExpressionResultNullOrUndefined(timezone))
	{
		return true;
	}

	return false;
}


/*
 * This function checks if all the arguments for the operator are constant.
 */
static inline bool
IsDateTruncArgumentConstant(DollarDateTruncArgumentState *
							dateTruncArgument)
{
	bool isBasicComponentsConstant =
		IsAggregationExpressionConstant(&dateTruncArgument->date) &&
		IsAggregationExpressionConstant(&dateTruncArgument->binSize) &&
		IsAggregationExpressionConstant(&dateTruncArgument->timezone);

	/* Early return if any of the basic components is not constant. */
	if (!isBasicComponentsConstant)
	{
		return false;
	}

	char *opName = "$dateTrunc";
	return IsUnitAndStartOfWeekConstant(&dateTruncArgument->unit,
										&dateTruncArgument->startOfWeek,
										opName);
}


/*
 * This function checks if we call date_bin internal function for date calculation.
 * If true, we will call date_bin internal postgres function for the same.
 * If false, we do custom logic as per the unit.
 */
static inline bool
IsValidUnitForDateBinOid(DateTruncUnit dateTruncUnit)
{
	if (dateTruncUnit == DateTruncUnit_Millisecond || dateTruncUnit ==
		DateTruncUnit_Second || dateTruncUnit == DateTruncUnit_Minute ||
		dateTruncUnit == DateTruncUnit_Hour)
	{
		return true;
	}
	return false;
}


/*
 * This function takes in all the arguments for the dateTrunc operator and validates them for the required value type
 * This function also parses the timezone and sets the offset in ms in timezone to apply
 * This function also modifies the binsize to a value of int64.
 */
static void
ValidateArgumentsForDateTrunc(bson_value_t *binSize, bson_value_t *date,
							  bson_value_t *startOfWeek, bson_value_t *timezone,
							  bson_value_t *unit, DateTruncUnit *dateTruncUnitEnum,
							  WeekDay *weekDay, ExtensionTimezone *timezoneToApply,
							  ExtensionTimezone resultTimezone)
{
	/* date is only expected to be of type date, timestamp, oid. */
	if (!((date->value_type == BSON_TYPE_DATE_TIME) ||
		  (date->value_type == BSON_TYPE_TIMESTAMP) ||
		  (date->value_type == BSON_TYPE_OID)))
	{
		ereport(ERROR, (errcode(MongoLocation5439012), errmsg(
							"$dateTrunc requires 'date' to be a date, but got %s",
							BsonTypeName(date->value_type)),
						errhint("$dateTrunc requires 'date' to be a date, but got %s",
								BsonTypeName(date->value_type))));
	}

	/* validate unit type */
	if (unit->value_type != BSON_TYPE_UTF8)
	{
		ereport(ERROR, (errcode(MongoLocation5439013), errmsg(
							"$dateTrunc requires 'unit' to be a string, but got %s",
							BsonTypeName(unit->value_type)),
						errhint("$dateTrunc requires 'unit' to be a string, but got %s",
								BsonTypeName(unit->value_type))));
	}

	int dateTruncUnitListSize = sizeof(unitSizeForDateTrunc) /
								sizeof(unitSizeForDateTrunc[0]);
	for (int index = 0; index < dateTruncUnitListSize; index++)
	{
		if (strcasecmp(unit->value.v_utf8.str, unitSizeForDateTrunc[index]) == 0)
		{
			/* Doing index +1 as invalid is the first index which is not in the unitSizeList. */
			*dateTruncUnitEnum = (DateTruncUnit) (index + 1);
			break;
		}
	}

	if (*dateTruncUnitEnum == DateTruncUnit_Invalid)
	{
		ereport(ERROR, (errcode(MongoLocation5439014), errmsg(
							"$dateTrunc parameter 'unit' value cannot be recognized as a time unit: %s",
							unit->value.v_utf8.str),
						errhint(
							"$dateTrunc parameter 'unit' value cannot be recognized as a time unit")));
	}

	/* Validate startOfWeekValue only if unit is week. Index 6 in the list if 'week' */
	if (*dateTruncUnitEnum == DateTruncUnit_Week)
	{
		if (startOfWeek->value_type != BSON_TYPE_UTF8)
		{
			ThrowLocation5439015Error(startOfWeek->value_type, "$dateTrunc");
		}

		/* Typed loop check as days can never be increased. */
		for (int index = 0; index < 7; index++)
		{
			if (strcasecmp(startOfWeek->value.v_utf8.str, weekDaysFullName[index]) == 0 ||
				strcasecmp(startOfWeek->value.v_utf8.str, weekDaysAbbreviated[index]) ==
				0)
			{
				/* Doing index +1 as invalid is the first index which is not in the weekDays list. */
				*weekDay = (WeekDay) (index + 1);
				break;
			}
		}
		if (*weekDay == WeekDay_Invalid)
		{
			ereport(ERROR, (errcode(MongoLocation5439016), errmsg(
								"$dateTrunc parameter 'startOfWeek' value cannot be recognized as a day of a week: %s",
								startOfWeek->value.v_utf8.str),
							errhint(
								"$dateTrunc parameter 'startOfWeek' value cannot be recognized as a day of a week")));
		}
	}

	/* check binSize value type */
	if (!IsBsonValueFixedInteger(binSize))
	{
		ereport(ERROR, (errcode(MongoLocation5439017), errmsg(
							"$dateTrunc requires 'binSize' to be a 64-bit integer, but got value '%s' of type %s",
							BsonValueToJsonForLogging(binSize), BsonTypeName(
								binSize->value_type)),
						errhint(
							"$dateTrunc requires 'binSize' to be a 64-bit integer, but got value of type %s",
							BsonTypeName(binSize->value_type))));
	}

	/* binSize should be greater than zero. */
	int64_t binSizeVal = BsonValueAsInt64(binSize);
	if (binSizeVal <= 0)
	{
		ereport(ERROR, (errcode(MongoLocation5439018), errmsg(
							"$dateTrunc requires 'binSize' to be greater than 0, but got value %ld",
							binSizeVal),
						errhint(
							"$dateTrunc requires 'binSize' to be greater than 0, but got value %ld",
							binSizeVal)));
	}

	/* Setting the value in binsize to be of int64. Converting to a single type for calculations. */
	binSize->value.v_int64 = binSizeVal;
	binSize->value_type = BSON_TYPE_INT64;

	/* Validate timezone type and then parse it to find offset in ms */
	if (timezone->value_type != BSON_TYPE_UTF8)
	{
		ThrowLocation40517Error(timezone->value_type);
	}

	/* UTC is set as default value while parsing. In case no value is supplied we don't intend to make another Oid function call. */
	if (strcmp(timezone->value.v_utf8.str, "UTC") != 0)
	{
		StringView timezoneToParse = {
			.string = timezone->value.v_utf8.str,
			.length = timezone->value.v_utf8.len
		};
		*timezoneToApply = ParseTimezone(timezoneToParse);
	}
	else
	{
		*timezoneToApply = resultTimezone;
	}
}


static void
SetResultValueForDateTruncFromDateBin(Datum pgTimestamp, Datum referenceTimestamp,
									  ExtensionTimezone timezoneToApply, int64 binSize,
									  DateTruncUnit dateTruncUnit, bson_value_t *result)
{
	Datum interval = GetIntervalFromBinSize(binSize, dateTruncUnit);
	Datum timestampDateBinResult = OidFunctionCall3(PostgresDateBinFunctionId(),
													interval, pgTimestamp,
													referenceTimestamp);
	result->value.v_datetime = GetUnixEpochFromPgTimestamp(timestampDateBinResult);
}


/* This function calculates the date bin for unit type day.
 * Firstly, this calculates interval difference between start and end timestamp.
 * Converts, that difference interval to number of days.
 * Adds the interval to source timestamp and then apply the timezone specified
 */
static void
SetResultValueForDayUnitDateTrunc(Datum pgTimestamp,
								  ExtensionTimezone timezoneToApply, int64 binSize,
								  DateTruncUnit dateTruncUnit,
								  bson_value_t *result)
{
	/* Get timestamp for reference since epoch ms in UTC */
	Datum defaultReferenceTimestamp = GetPgTimestampFromUnixEpoch(
		DATE_TRUNC_TIMESTAMP_MS);

	/* Calls Age function to determine the interval between 2 timestamps. */
	Datum intervalDifference = OidFunctionCall2(PostgresAgeBetweenTimestamp(),
												pgTimestamp, defaultReferenceTimestamp);

	/* calculates the total number of seconds between the target date and the reference date */
	Datum datePartFromInverval = OidFunctionCall2(PostgresDatePartFromInterval(),
												  CStringGetTextDatum("epoch"),
												  intervalDifference);

	float8 epochMsFromDatePart = DatumGetFloat8(datePartFromInverval);

	/* elog(NOTICE, "epoch sm from date partt %f", epochMsFromDatePart); */
	int64 dayInterval = floor(epochMsFromDatePart / (SECONDS_IN_DAY * binSize));

	/* elog(NOTICE, "days interval %ld", dayInterval); */

	/* Make intervals from int64 */
	Datum intervalDaySinceRef = GetIntervalFromBinSize(dayInterval * binSize,
													   dateTruncUnit);

	/* Add interval to timestamp */
	bool isResultOverflow = false;
	Datum resultTimestamp = AddIntervalToTimestampWithPgTry(defaultReferenceTimestamp,
															intervalDaySinceRef,
															&isResultOverflow);

	if (isResultOverflow)
	{
		result->value.v_datetime = INT64_MAX;
		return;
	}

	/* Adjusting to timezone specified . This is required to handle DST, etc. */
	Datum resultTimestampWithZoneAdjusted = GetPgTimestampAdjustedToTimezone(
		resultTimestamp, timezoneToApply);

	result->value.v_datetime = GetUnixEpochFromPgTimestamp(
		resultTimestampWithZoneAdjusted);
}


/* A function which takes in all the args and gives the result for dollarDateTrunc operator. */
static void
SetResultValueForDollarDateTrunc(DateTruncUnit dateTruncUnit, WeekDay weekDay,
								 ExtensionTimezone timezoneToApply, ExtensionTimezone
								 resultTimezone,
								 bson_value_t *binSize, bson_value_t *date,
								 bson_value_t *result)
{
	result->value_type = BSON_TYPE_DATE_TIME;
	int64_t dateValueInMs = BsonValueAsDateTime(date);
	Datum datePgTimestamp = GetPgTimestampFromEpochWithTimezone(dateValueInMs,
																resultTimezone);
	if (IsValidUnitForDateBinOid(dateTruncUnit))
	{
		Datum referencePgTimestamp = GetPgTimestampFromEpochWithoutTimezone(
			DATE_TRUNC_TIMESTAMP_MS,
			timezoneToApply);
		SetResultValueForDateTruncFromDateBin(datePgTimestamp, referencePgTimestamp,
											  timezoneToApply,
											  binSize->value.v_int64,
											  dateTruncUnit, result);
	}
	else if (dateTruncUnit == DateTruncUnit_Day)
	{
		SetResultValueForDayUnitDateTrunc(datePgTimestamp,
										  timezoneToApply,
										  binSize->value.v_int64,
										  dateTruncUnit, result);
	}
	else if (dateTruncUnit == DateTruncUnit_Month ||
			 dateTruncUnit == DateTruncUnit_Quarter)
	{
		SetResultValueForMonthUnitDateTrunc(datePgTimestamp,
											timezoneToApply,
											binSize->value.v_int64,
											dateTruncUnit, result);
	}
	else if (dateTruncUnit == DateTruncUnit_Year)
	{
		SetResultValueForYearUnitDateTrunc(datePgTimestamp,
										   timezoneToApply,
										   binSize->value.v_int64,
										   dateTruncUnit, result);
	}
	else if (dateTruncUnit == DateTruncUnit_Week)
	{
		SetResultValueForWeekUnitDateTrunc(datePgTimestamp,
										   timezoneToApply,
										   binSize->value.v_int64,
										   dateTruncUnit,
										   weekDay,
										   result);
	}
}


/* This function calculates the date bin for unit type month.
 * Firstly, this calculates interval difference between start and end timestamp.
 * Converts, that difference interval to number of months.
 * The interval can be of format x years and y months .
 * To exactly compute the difference in months we extract number of years and then multiply by 12 to get months.
 * Also, we extract the number of months and add with number of months extracted from years.
 * Adds the interval to source timestamp and then apply the timezone specified
 */
static void
SetResultValueForMonthUnitDateTrunc(Datum pgTimestamp, ExtensionTimezone
									timezoneToApply, int64 binSize,
									DateTruncUnit dateTruncUnit,
									bson_value_t *result)
{
	/* Get timestamp for reference since epoch ms in UTC */
	Datum defaultReferenceTimestamp = GetPgTimestampFromUnixEpoch(
		DATE_TRUNC_TIMESTAMP_MS);

	/* Calls Age function to determine the interval between 2 timestamps. */
	Datum intervalDifference = OidFunctionCall2(PostgresAgeBetweenTimestamp(),
												pgTimestamp, defaultReferenceTimestamp);

	/* calculates the total number of years between the target date and the reference date */
	Datum datePartYearFromInverval = OidFunctionCall2(PostgresDatePartFromInterval(),
													  CStringGetTextDatum("year"),
													  intervalDifference);

	/* elog(NOTICE, "year interval from date part is %f", DatumGetFloat8(datePartYearFromInverval)); */

	/* calculates the total number of months between the target date and the reference date */
	Datum datePartMonthFromInverval = OidFunctionCall2(PostgresDatePartFromInterval(),
													   CStringGetTextDatum("month"),
													   intervalDifference);

	/* elog(NOTICE, "month interval from date part is %f", DatumGetFloat8(datePartMonthFromInverval)); */


	float8 intervalDiffAdjustedToMonths = DatumGetFloat8(datePartYearFromInverval) *
										  MONTHS_PER_YEAR +
										  DatumGetFloat8(datePartMonthFromInverval);

	/* elog(NOTICE, "interval diff adjusted to months %f", intervalDiffAdjustedToMonths); */

	/* Making sure that for quarters the binsize for months is * 3 by default. */
	int64 monthInterval = floor(intervalDiffAdjustedToMonths / (binSize *
																(dateTruncUnit ==
																 DateTruncUnit_Quarter
																 ? 3 : 1)));

	/* Make intervals from int64 . Not multiplying *3 here as this function takes care of differencing between months and quarters. */
	Datum intervalMonthSinceRef = GetIntervalFromBinSize(monthInterval * binSize,
														 dateTruncUnit);

	bool isResultOverflow = false;
	Datum resultTimestamp = AddIntervalToTimestampWithPgTry(defaultReferenceTimestamp,
															intervalMonthSinceRef,
															&isResultOverflow);

	if (isResultOverflow)
	{
		result->value.v_datetime = INT64_MAX;
		return;
	}

	/* Adjusting to timezone specified . This is required to handle DST, etc. */
	Datum resultTimestampWithZoneAdjusted = GetPgTimestampAdjustedToTimezone(
		resultTimestamp,
		timezoneToApply);

	result->value.v_datetime = GetUnixEpochFromPgTimestamp(
		resultTimestampWithZoneAdjusted);
}


/* This function calculates the date bin for unit type week.
 * First adjust the given timestamp to fit the startOfWeek.
 * Then calculates interval difference between start and end timestamp.
 * Converts, that difference interval to number of seconds.
 * Adds interval in days to adjust for startOfWeek.
 * As per the reference date 2000 1st Jan which is Saturday. Default value is Sunday if not specified.
 * So for every calculation we add interval in days to adjust for startOfWeek
 * Adds the interval to source timestamp and then apply the timezone specified
 */
static void
SetResultValueForWeekUnitDateTrunc(Datum pgTimestamp, ExtensionTimezone
								   timezoneToApply, int64 binSize,
								   DateTruncUnit dateTruncUnit,
								   WeekDay startOfWeek,
								   bson_value_t *result)
{
	/* Get timestamp for reference since epoch ms in UTC */
	Datum defaultReferenceTimestamp = GetPgTimestampFromUnixEpoch(
		DATE_TRUNC_TIMESTAMP_MS);

	/* default dow in postgres resolves to sunday and goes like sunday , monday, tuesday, .. */
	int dayOfWeek = GetDatePartFromPgTimestamp(pgTimestamp, DatePart_DayOfWeek);
	int diffBwStartOfWeekAndCurrent = GetDifferenceInDaysForStartOfWeek(dayOfWeek,
																		startOfWeek);
	Datum daysIntervalToAdjust = GetIntervalFromDatePart(0, 0, 0,
														 diffBwStartOfWeekAndCurrent, 0,
														 0, 0, 0);
	Datum timestampAdjustedToStartOfWeek = DirectFunctionCall2(timestamp_mi_interval,
															   pgTimestamp,
															   daysIntervalToAdjust);

	/* Calls Age function to determine the interval between 2 timestamps. */
	Datum intervalDifference = OidFunctionCall2(PostgresAgeBetweenTimestamp(),
												timestampAdjustedToStartOfWeek,
												defaultReferenceTimestamp);

	/* calculates the total number of seconds between the target date and the reference date */
	Datum datePartFromInverval = OidFunctionCall2(PostgresDatePartFromInterval(),
												  CStringGetTextDatum("epoch"),
												  intervalDifference);

	float8 epochMsFromDatePart = DatumGetFloat8(datePartFromInverval);

	int64 elapsedBinSeconds = (int64) SECONDS_IN_DAY * (int64) 7 * binSize;

	int64 secondsInterval = floor(epochMsFromDatePart / (elapsedBinSeconds));

	/* takes care of negative number while modulo. Positive mod: (a % b + b) % b; */
	int64 daysIntervalForWeekStart = ((((int) startOfWeek - 6) % 7) + 7) % 7;
	Datum intervalWeeksSinceRef = GetIntervalFromDatePart(0, 0, 0,
														  daysIntervalForWeekStart, 0, 0,
														  secondsInterval *
														  elapsedBinSeconds,
														  0);

	/*
	 *	This handling is done as for cases when seconds elapsed is itself out of range of timestamp.
	 * So , we need to return invalid date.
	 * Adding only time as while making interval max component is in seconds.
	 * Day interval can max be 7 to adjust for startOfWeek
	 */
	int64 interval = ((Interval *) DatumGetIntervalP(intervalWeeksSinceRef))->time;
	if (!IS_VALID_TIMESTAMP(interval))
	{
		result->value.v_datetime = INT64_MAX;
		return;
	}

	/* Add interval to timestamp */
	Datum resultTimestamp = OidFunctionCall2(PostgresAddIntervalToTimestampFunctionId(),
											 defaultReferenceTimestamp,
											 intervalWeeksSinceRef);

	/* Adjusting to timezone specified . This is required to handle DST, etc. */
	Datum resultTimestampWithZoneAdjusted = GetPgTimestampAdjustedToTimezone(
		resultTimestamp, timezoneToApply);

	result->value.v_datetime = GetUnixEpochFromPgTimestamp(
		resultTimestampWithZoneAdjusted);
}


/* This function calculates the date bin for unit type year.
 * Firstly, this calculates interval difference between start and end timestamp.
 * Converts, that difference interval to number of years.
 * Adds the interval to source timestamp and then apply the timezone specified
 */
static void
SetResultValueForYearUnitDateTrunc(Datum pgTimestamp, ExtensionTimezone
								   timezoneToApply, int64 binSize,
								   DateTruncUnit dateTruncUnit,
								   bson_value_t *result)
{
	Datum defaultReferenceTimestamp = GetPgTimestampFromUnixEpoch(
		DATE_TRUNC_TIMESTAMP_MS);

	/* Calls Age function to determine the interval between 2 timestamps. */
	Datum intervalDifference = OidFunctionCall2(PostgresAgeBetweenTimestamp(),
												pgTimestamp, defaultReferenceTimestamp);

	/* calculates the total number of years between the target date and the reference date */
	Datum datePartYearFromInverval = OidFunctionCall2(PostgresDatePartFromInterval(),
													  CStringGetTextDatum("year"),
													  intervalDifference);

	int64 yearInterval = floor(DatumGetFloat8(datePartYearFromInverval) / binSize);

	/* Make intervals from int64 .*/
	Datum intervalYearsSinceRef = GetIntervalFromBinSize(yearInterval * binSize,
														 dateTruncUnit);

	bool isResultOverflow = false;
	Datum resultTimestamp = AddIntervalToTimestampWithPgTry(defaultReferenceTimestamp,
															intervalYearsSinceRef,
															&isResultOverflow);

	if (isResultOverflow)
	{
		result->value.v_datetime = INT64_MAX;
		return;
	}

	/* Adjusting to timezone specified . This is required to handle DST, etc. */
	Datum resultTimestampWithZoneAdjusted = GetPgTimestampAdjustedToTimezone(
		resultTimestamp,
		timezoneToApply);

	result->value.v_datetime = GetUnixEpochFromPgTimestamp(
		resultTimestampWithZoneAdjusted);
}


/*
 * This function handles $dateAdd after parsing and gives the result as BSON_TYPE_DATETIME.
 */
void
HandlePreParsedDollarDateAdd(pgbson *doc, void *arguments,
							 ExpressionResult *expressionResult)
{
	char *opName = "$dateAdd";
	bool isDateAdd = true;
	HandleCommonPreParsedForDateAddSubtract(opName, isDateAdd, doc, arguments,
											expressionResult);
}


/*
 * This function parses the input given for $dateAdd.
 * The input to the function $dateAdd is of the format  : {$dateAdd : {startDate: expression , unit: expression, amount: expression , timezone: expression}}
 * The function parses the input and if input is not constant stores into a struct DollarDateAddSubtract.
 */
void
ParseDollarDateAdd(const bson_value_t *argument, AggregationExpressionData *data,
				   ParseAggregationExpressionContext *context)
{
	char *opName = "$dateAdd";
	bool isDateAdd = true;
	HandleCommonParseForDateAddSubtract(opName, isDateAdd, argument, data, context);
}


/* This function tells if the timestamp supplied with the timezone for dateAdd needs to be adjusted.
 * This returns 1 or -1 if needs to be adjusted which is the amount of hour to add or subtract from result timestamp.
 * In case we do need need to adjust we return 0
 * This takes into account that timezone is applied only above days unit i.e for day, week , month, etc.
 * Also, it only adjusts the timezone with the DST offset if olson timezone is specified.
 * Timezone is applied only when post adding interval we are entering DST or DST is ending as per mongo behaviour.
 */
static int
GetAdjustHourWithTimezoneForDateAddSubtract(Datum startTimestamp, Datum
											timestampIntervalAdjusted, DateUnit unitEnum,
											ExtensionTimezone timezoneToApply)
{
	if (unitEnum >= DateUnit_Hour || timezoneToApply.isUtcOffset)
	{
		return 0;
	}

	Datum startTimestampZoneAdjusted = GetPgTimestampAdjustedToTimezone(startTimestamp,
																		timezoneToApply);
	Datum timestampIntervalWithTimezone = GetPgTimestampAdjustedToTimezone(
		timestampIntervalAdjusted, timezoneToApply);

	/* check if DST offset needs to be adjusted for result. */
	int hourForStartTimestamp = GetDatePartFromPgTimestamp(startTimestampZoneAdjusted,
														   DatePart_Hour);
	int hourFortimestampInterval = GetDatePartFromPgTimestamp(
		timestampIntervalWithTimezone, DatePart_Hour);

	/* This will tell if there's a DST is applied. */
	return (hourFortimestampInterval - hourForStartTimestamp);
}


/* This is a common parser function since the logic was same hence, extracted it and made it part of a common function */
static void
HandleCommonParseForDateAddSubtract(char *opName, bool isDateAdd, const
									bson_value_t *argument,
									AggregationExpressionData *data,
									ParseAggregationExpressionContext *context)
{
	bson_value_t startDate = { 0 }, unit = { 0 }, amount = { 0 }, timezone = { 0 };
	ParseInputForDollarDateAddSubtract(argument, opName, &startDate, &unit, &amount,
									   &timezone);

	DollarDateAddSubtract *dateAddSubtractArgs = palloc0(sizeof(DollarDateAddSubtract));

	ParseAggregationExpressionData(&dateAddSubtractArgs->startDate, &startDate, context);
	ParseAggregationExpressionData(&dateAddSubtractArgs->unit, &unit, context);
	ParseAggregationExpressionData(&dateAddSubtractArgs->amount, &amount, context);
	ParseAggregationExpressionData(&dateAddSubtractArgs->timezone, &timezone, context);

	/* This here is part of optimization as now we know if all inputs are constant */
	if (IsAggregationExpressionConstant(&dateAddSubtractArgs->startDate) &&
		IsAggregationExpressionConstant(&dateAddSubtractArgs->unit) &&
		IsAggregationExpressionConstant(&dateAddSubtractArgs->amount) &&
		IsAggregationExpressionConstant(&dateAddSubtractArgs->timezone))
	{
		bson_value_t result = { .value_type = BSON_TYPE_NULL };

		/* If any part is null or undefined we return null. */
		if (IsExpressionResultNullOrUndefined(&dateAddSubtractArgs->startDate.value) ||
			IsExpressionResultNullOrUndefined(&dateAddSubtractArgs->unit.value) ||
			IsExpressionResultNullOrUndefined(&dateAddSubtractArgs->amount.value) ||
			IsExpressionResultNull(&dateAddSubtractArgs->timezone.value))
		{
			data->value = result;
			data->kind = AggregationExpressionKind_Constant;
			pfree(dateAddSubtractArgs);
			return;
		}

		ValidateInputForDollarDateAddSubtract(opName, isDateAdd,
											  &dateAddSubtractArgs->startDate.value,
											  &dateAddSubtractArgs->unit.value,
											  &dateAddSubtractArgs->amount.value,
											  &dateAddSubtractArgs->timezone.value);

		ExtensionTimezone timezoneToApply = {
			.offsetInMs = 0,
			.isUtcOffset = true,
		};

		if (dateAddSubtractArgs->timezone.value.value_type != BSON_TYPE_EOD)
		{
			StringView timezoneToParse = {
				.string = dateAddSubtractArgs->timezone.value.value.v_utf8.str,
				.length = dateAddSubtractArgs->timezone.value.value.v_utf8.len
			};
			timezoneToApply = ParseTimezone(timezoneToParse);
		}

		DateUnit unitEnum = GetDateUnitFromString(
			dateAddSubtractArgs->unit.value.value.v_utf8.str);
		if (!isDateAdd && unitEnum < DateUnit_Hour)
		{
			SetResultForDollarDateSubtract(&dateAddSubtractArgs->startDate.value,
										   unitEnum,
										   BsonValueAsInt64(
											   &dateAddSubtractArgs->amount.value),
										   timezoneToApply,
										   &result);
		}
		else
		{
			SetResultForDollarDateAddSubtract(&dateAddSubtractArgs->startDate.value,
											  unitEnum,
											  BsonValueAsInt64(
												  &dateAddSubtractArgs->amount.value),
											  timezoneToApply,
											  opName,
											  isDateAdd,
											  &result);
		}
		data->value = result;
		data->kind = AggregationExpressionKind_Constant;
		pfree(dateAddSubtractArgs);
	}
	else
	{
		data->operator.arguments = dateAddSubtractArgs;
		data->operator.argumentsKind = AggregationExpressionArgumentsKind_Palloc;
	}
}


/* This is a common function where we handle post parsing logic for dateAdd, dateSubtract. */
static void
HandleCommonPreParsedForDateAddSubtract(char *opName, bool isDateAdd, pgbson *doc,
										void *arguments,
										ExpressionResult *expressionResult)
{
	bson_value_t startDate = { 0 }, unit = { 0 }, amount = { 0 }, timezone = { 0 };

	DollarDateAddSubtract *dateAddSubtractArguments = arguments;

	bool isNullOnEmpty = false;
	ExpressionResult childExpression = ExpressionResultCreateChild(expressionResult);
	EvaluateAggregationExpressionData(&dateAddSubtractArguments->startDate, doc,
									  &childExpression,
									  isNullOnEmpty);
	startDate = childExpression.value;

	ExpressionResultReset(&childExpression);
	EvaluateAggregationExpressionData(&dateAddSubtractArguments->unit, doc,
									  &childExpression,
									  isNullOnEmpty);
	unit = childExpression.value;

	ExpressionResultReset(&childExpression);
	EvaluateAggregationExpressionData(&dateAddSubtractArguments->amount, doc,
									  &childExpression,
									  isNullOnEmpty);
	amount = childExpression.value;

	ExpressionResultReset(&childExpression);
	EvaluateAggregationExpressionData(&dateAddSubtractArguments->timezone, doc,
									  &childExpression,
									  isNullOnEmpty);
	timezone = childExpression.value;

	bson_value_t result = { .value_type = BSON_TYPE_NULL };

	if (IsExpressionResultNullOrUndefined(&startDate) ||
		IsExpressionResultNullOrUndefined(&unit) ||
		IsExpressionResultNullOrUndefined(&amount) ||
		IsExpressionResultNull(&timezone))
	{
		ExpressionResultSetValue(expressionResult, &result);
		return;
	}

	ValidateInputForDollarDateAddSubtract(opName, isDateAdd, &startDate, &unit, &amount,
										  &timezone);

	ExtensionTimezone timezoneToApply = {
		.offsetInMs = 0,
		.isUtcOffset = true,
	};

	if (timezone.value_type != BSON_TYPE_EOD)
	{
		StringView timezoneToParse = {
			.string = timezone.value.v_utf8.str,
			.length = timezone.value.v_utf8.len
		};
		timezoneToApply = ParseTimezone(timezoneToParse);
	}
	DateUnit unitEnum = GetDateUnitFromString(unit.value.v_utf8.str);
	if (!isDateAdd && unitEnum < DateUnit_Hour)
	{
		SetResultForDollarDateSubtract(&startDate, unitEnum, BsonValueAsInt64(&amount),
									   timezoneToApply, &result);
	}
	else
	{
		SetResultForDollarDateAddSubtract(&startDate, unitEnum, BsonValueAsInt64(&amount),
										  timezoneToApply, opName, isDateAdd, &result);
	}
	ExpressionResultSetValue(expressionResult, &result);
}


/*
 * This function checks wheter dateUnit range and amount to add/substract is withing the specified limit for $dateAdd and $dateSubtract.
 * This function checks before computing that if the amount values supplied can take the date to out of range.
 */
static bool
IsAmountRangeValidForDateUnit(int64 amountVal, DateUnit unit)
{
	/* Ensure the unit is within the expected range to avoid accessing out of bounds */
	if (unit < DateUnit_Year)
	{
		return false; /* Invalid DateUnit value */
	}

	/* for unit millisecond we don't have any range */
	if (unit > DateUnit_Second)
	{
		return true;
	}

	return (-rangeforDateUnit[unit] < amountVal) && (amountVal < rangeforDateUnit[unit]);
}


static inline bool
IsBsonValueDateTime(bson_type_t bsonType)
{
	if (bsonType == BSON_TYPE_DATE_TIME || bsonType == BSON_TYPE_TIMESTAMP || bsonType ==
		BSON_TYPE_OID)
	{
		return true;
	}

	return false;
}


/*
 * This function parses the given input document and extracts and sets the result in given bson_value_t types.
 */
static void
ParseInputForDollarDateAddSubtract(const bson_value_t *inputArgument,
								   char *opName,
								   bson_value_t *startDate, bson_value_t *unit,
								   bson_value_t *amount,
								   bson_value_t *timezone)
{
	if (inputArgument->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(MongoLocation5166400), errmsg(
							"%s expects an object as its argument.found input type:%s",
							opName, BsonValueToJsonForLogging(inputArgument)),
						errhint(
							"%s expects an object as its argument.found input type:%s",
							opName, BsonTypeName(inputArgument->value_type))));
	}

	bson_iter_t docIter;
	BsonValueInitIterator(inputArgument, &docIter);

	while (bson_iter_next(&docIter))
	{
		const char *key = bson_iter_key(&docIter);
		if (strcmp(key, "startDate") == 0)
		{
			*startDate = *bson_iter_value(&docIter);
		}
		else if (strcmp(key, "unit") == 0)
		{
			*unit = *bson_iter_value(&docIter);
		}
		else if (strcmp(key, "amount") == 0)
		{
			*amount = *bson_iter_value(&docIter);
		}
		else if (strcmp(key, "timezone") == 0)
		{
			*timezone = *bson_iter_value(&docIter);
		}
		else
		{
			ereport(ERROR, (errcode(MongoLocation5166401), errmsg(
								"Unrecognized argument to %s: %s. Expected arguments are startDate, unit, amount, and optionally timezone",
								opName, key),
							errhint(
								"Unrecognized argument to %s: Expected arguments are startDate, unit, amount, and optionally timezone",
								opName)));
		}
	}

	if (startDate->value_type == BSON_TYPE_EOD || unit->value_type == BSON_TYPE_EOD ||
		amount == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(MongoLocation5166402), errmsg(
							"%s requires startDate, unit, and amount to be present",
							opName),
						errhint("%s requires startDate, unit, and amount to be present",
								opName)));
	}
}


/* This function does the final computation for the dateAdd and then stores the computed answer to result. */
static void
SetResultForDollarDateAddSubtract(bson_value_t *startDate, DateUnit unitEnum,
								  int64 amount, ExtensionTimezone timezoneToApply,
								  char *opName,
								  bool isDateAdd,
								  bson_value_t *result)
{
	result->value_type = BSON_TYPE_DATE_TIME;

	/* Negating the amount as for date subtract we need to minus the amount. */
	if (!isDateAdd)
	{
		amount *= -1;
	}
	int64 epochMs = BsonValueAsDateTime(startDate);
	Datum startDateTimestamp = GetPgTimestampFromUnixEpoch(epochMs);
	Datum interval = GetIntervalFromDateUnitAndAmount(unitEnum, amount);

	bool isResultOverflow = false;
	Datum resultTimestamp = AddIntervalToTimestampWithPgTry(startDateTimestamp, interval,
															&isResultOverflow);

	if (isResultOverflow)
	{
		ereport(ERROR, (errcode(MongoLocation5166406), errmsg(
							"%s overflowed", opName),
						errhint("%s overflowed", opName)));
	}

	int diffHour = GetAdjustHourWithTimezoneForDateAddSubtract(startDateTimestamp,
															   resultTimestamp, unitEnum,
															   timezoneToApply);

	Datum resultEpoch = DirectFunctionCall2(timestamp_part,
											CStringGetTextDatum("epoch"),
											resultTimestamp);

	int64 resultEpochMs = (int64) (DatumGetFloat8(resultEpoch) * 1000);

	if (diffHour != 0)
	{
		resultEpochMs += (diffHour * 60 * 60 * MILLISECONDS_IN_SECOND);
	}
	result->value.v_datetime = resultEpochMs;
}


/* This is a function handling for $dateSubtract logic for last day handling and dst offset.
 * This is applicable when unit is day or greater like week, month, quarter ,etc.
 */
static void
SetResultForDollarDateSubtract(bson_value_t *startDate, DateUnit unitEnum,
							   int64 amount,
							   ExtensionTimezone timezoneToApply,
							   bson_value_t *result)
{
	result->value_type = BSON_TYPE_DATE_TIME;
	amount *= -1;
	int64 epochMs = BsonValueAsDateTime(startDate);
	Datum startDateTimestamp = GetPgTimestampFromEpochWithTimezone(epochMs,
																   timezoneToApply);
	Datum interval = GetIntervalFromDateUnitAndAmount(unitEnum, amount);

	bool isResultOverflow = false;
	Datum resultTimestamp = AddIntervalToTimestampWithPgTry(startDateTimestamp, interval,
															&isResultOverflow);

	if (isResultOverflow)
	{
		ereport(ERROR, (errcode(MongoLocation5166406), errmsg(
							"$dateSubtract overflowed"),
						errhint("$dateSubtract overflowed")));
	}

	Datum resultTimestampWithUTC = GetPgTimestampAdjustedToTimezone(resultTimestamp,
																	timezoneToApply);
	Datum resultEpoch = DirectFunctionCall2(timestamp_part,
											CStringGetTextDatum("epoch"),
											resultTimestampWithUTC);

	int64 resultEpochMs = (int64) (DatumGetFloat8(resultEpoch) * 1000);
	result->value.v_datetime = resultEpochMs;
}


/* This function validates the input given for each argument. */
static void
ValidateInputForDollarDateAddSubtract(char *opName, bool isDateAdd,
									  bson_value_t *startDate, bson_value_t *unit,
									  bson_value_t *amount, bson_value_t *timezone)
{
	if (!IsBsonValueDateTime(startDate->value_type))
	{
		ereport(ERROR, (errcode(MongoLocation5166403), errmsg(
							"%s requires startDate to be convertible to a date",
							opName),
						errhint("%s requires startDate to be convertible to a date",
								opName)));
	}

	if (unit->value_type != BSON_TYPE_UTF8)
	{
		ereport(ERROR, (errcode(MongoLocation5166404), errmsg(
							"%s expects string defining the time unit",
							opName),
						errhint("%s expects string defining the time unit",
								opName)));
	}
	char *unitStrValue = unit->value.v_utf8.str;
	DateUnit unitEnum = GetDateUnitFromString(unitStrValue);

	if (unitEnum == DateUnit_Invalid)
	{
		ereport(ERROR, (errcode(MongoFailedToParse), errmsg(
							"unknown time unit value: %s", unitStrValue),
						errhint("unknown time unit value")));
	}

	if (!IsBsonValueFixedInteger(amount))
	{
		ereport(ERROR, (errcode(MongoLocation5166405), errmsg(
							"%s expects integer amount of time units", opName),
						errhint("%s expects integer amount of time units", opName)));
	}

	int64 amountVal = BsonValueAsInt64(amount);

	if (!isDateAdd && amountVal == INT64_MIN)
	{
		ereport(ERROR, (errcode(MongoLocation6045000), errmsg(
							"invalid %s 'amount' parameter value: %s %s", opName,
							BsonValueToJsonForLogging(amount), unitStrValue),
						errhint("invalid %s 'amount' parameter value: %s %s", opName,
								BsonValueToJsonForLogging(amount), unitStrValue)));
	}

	if (!IsAmountRangeValidForDateUnit(amountVal, unitEnum))
	{
		ereport(ERROR, (errcode(MongoLocation5976500), errmsg(
							"invalid %s 'amount' parameter value: %s %s", opName,
							BsonValueToJsonForLogging(amount), unitStrValue),
						errhint("invalid %s 'amount' parameter value: %s %s", opName,
								BsonValueToJsonForLogging(amount), unitStrValue)));
	}

	/* Since no , default value is set hence, need to check if not eod then it should be UTF8 */
	if (timezone->value_type != BSON_TYPE_EOD && timezone->value_type != BSON_TYPE_UTF8)
	{
		ereport(ERROR, (errcode(MongoLocation40517), errmsg(
							"timezone must evaluate to a string, found %s", BsonTypeName(
								timezone->value_type)),
						errhint("timezone must evaluate to a string, found %s",
								BsonTypeName(timezone->value_type))));
	}
}


/*
 * This function handles $dateSubtract after parsing and gives the result as BSON_TYPE_DATETIME.
 */
void
HandlePreParsedDollarDateSubtract(pgbson *doc, void *arguments,
								  ExpressionResult *expressionResult)
{
	char *opName = "$dateSubtract";
	bool isDateAdd = false;
	HandleCommonPreParsedForDateAddSubtract(opName, isDateAdd, doc, arguments,
											expressionResult);
}


/*
 * This function parses the input given for $dateSubtract.
 * The input to the function $dateSubtract is of the format  : {$dateSubtract : {startDate: expression , unit: expression, amount: expression , timezone: expression}}
 * The function parses the input and if input is not constant stores into a struct DollarDateAddSubtract.
 */
void
ParseDollarDateSubtract(const bson_value_t *argument, AggregationExpressionData *data,
						ParseAggregationExpressionContext *context)
{
	char *opName = "$dateSubtract";
	bool isDateAdd = false;
	HandleCommonParseForDateAddSubtract(opName, isDateAdd, argument, data, context);
}


/*
 * This function handles the output for dollar dateDiff after the data has been parsed for the aggregation operator.
 */
void
HandlePreParsedDollarDateDiff(pgbson *doc, void *arguments,
							  ExpressionResult *expressionResult)
{
	DollarDateDiffArgumentState *dateDiffArgumentState = arguments;
	bool isNullOnEmpty = false;

	bson_value_t startDate = { 0 };
	bson_value_t endDate = { 0 };
	bson_value_t startOfWeek = { 0 };
	bson_value_t timezone = { 0 };
	bson_value_t unit = { 0 };

	ExpressionResult childExpression = ExpressionResultCreateChild(expressionResult);
	EvaluateAggregationExpressionData(&dateDiffArgumentState->startDate, doc,
									  &childExpression,
									  isNullOnEmpty);
	startDate = childExpression.value;

	ExpressionResultReset(&childExpression);
	EvaluateAggregationExpressionData(&dateDiffArgumentState->endDate, doc,
									  &childExpression,
									  isNullOnEmpty);
	endDate = childExpression.value;

	ExpressionResultReset(&childExpression);
	EvaluateAggregationExpressionData(&dateDiffArgumentState->unit, doc,
									  &childExpression,
									  isNullOnEmpty);
	unit = childExpression.value;

	ExpressionResultReset(&childExpression);
	EvaluateAggregationExpressionData(&dateDiffArgumentState->timezone, doc,
									  &childExpression,
									  isNullOnEmpty);
	timezone = childExpression.value;

	ExpressionResultReset(&childExpression);
	EvaluateAggregationExpressionData(&dateDiffArgumentState->startOfWeek, doc,
									  &childExpression,
									  isNullOnEmpty);
	startOfWeek = childExpression.value;

	bson_value_t result = { .value_type = BSON_TYPE_NULL };
	if (IsArgumentForDateDiffNull(&startDate,
								  &endDate,
								  &unit,
								  &timezone,
								  &startOfWeek))
	{
		ExpressionResultSetValue(expressionResult, &result);
		return;
	}

	DateUnit dateUnitEnum = DateUnit_Invalid;
	WeekDay weekDayEnum = WeekDay_Invalid;
	ValidateInputArgumentForDateDiff(&startDate,
									 &endDate,
									 &unit,
									 &timezone,
									 &startOfWeek,
									 &dateUnitEnum, &weekDayEnum);

	ExtensionTimezone timezoneToApply = {
		.offsetInMs = 0,
		.isUtcOffset = true,
	};

	/* timezone is not default value */
	if (strcmp(timezone.value.v_utf8.str, DefaultTimezone) != 0)
	{
		StringView timezoneToParse = {
			.string = timezone.value.v_utf8.str,
			.length = timezone.value.v_utf8.len
		};
		timezoneToApply = ParseTimezone(timezoneToParse);
	}

	SetResultForDateDiff(&startDate,
						 &endDate,
						 dateUnitEnum, weekDayEnum,
						 timezoneToApply, &result);

	ExpressionResultSetValue(expressionResult, &result);
}


/*
 * This function takes in the input arguments and then parses the expression for dollar date diff.
 * The input exresssion is of the format $dateDiff : {startDate: <Expression>, endDate: <expression>, unit: <Expression> , timezone: <tzExpression>, startOfWeek: <string>}
 */
void
ParseDollarDateDiff(const bson_value_t *argument, AggregationExpressionData *data,
					ParseAggregationExpressionContext *context)
{
	if (argument->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(MongoLocation5166301), errmsg(
							"$dateDiff only supports an object as its argument")));
	}

	bson_value_t startDate = { 0 };
	bson_value_t endDate = { 0 };
	bson_value_t startOfWeek = { 0 };
	bson_value_t timezone = { 0 };
	bson_value_t unit = { 0 };

	ParseInputDocumentForDateDiff(argument, &startDate, &endDate, &unit, &timezone,
								  &startOfWeek);
	DollarDateDiffArgumentState *dateDiffArgumentState = palloc0(
		sizeof(DollarDateDiffArgumentState));

	ParseAggregationExpressionData(&dateDiffArgumentState->startDate, &startDate,
								   context);
	ParseAggregationExpressionData(&dateDiffArgumentState->endDate, &endDate, context);
	ParseAggregationExpressionData(&dateDiffArgumentState->unit, &unit, context);
	ParseAggregationExpressionData(&dateDiffArgumentState->timezone, &timezone, context);
	ParseAggregationExpressionData(&dateDiffArgumentState->startOfWeek, &startOfWeek,
								   context);

	if (IsDateDiffArgumentConstant(dateDiffArgumentState))
	{
		bson_value_t result = { .value_type = BSON_TYPE_NULL };
		if (IsArgumentForDateDiffNull(&dateDiffArgumentState->startDate.value,
									  &dateDiffArgumentState->endDate.value,
									  &dateDiffArgumentState->unit.value,
									  &dateDiffArgumentState->timezone.value,
									  &dateDiffArgumentState->startOfWeek.value))
		{
			data->value = result;
			data->kind = AggregationExpressionKind_Constant;
			pfree(dateDiffArgumentState);
			return;
		}

		DateUnit dateUnitEnum = DateUnit_Invalid;
		WeekDay weekDayEnum = WeekDay_Invalid;
		ValidateInputArgumentForDateDiff(&dateDiffArgumentState->startDate.value,
										 &dateDiffArgumentState->endDate.value,
										 &dateDiffArgumentState->unit.value,
										 &dateDiffArgumentState->timezone.value,
										 &dateDiffArgumentState->startOfWeek.value,
										 &dateUnitEnum, &weekDayEnum);

		ExtensionTimezone timezoneToApply = {
			.offsetInMs = 0,
			.isUtcOffset = true,
		};

		if (strcmp(dateDiffArgumentState->timezone.value.value.v_utf8.str,
				   DefaultTimezone) != 0)
		{
			StringView timezoneToParse = {
				.string = dateDiffArgumentState->timezone.value.value.v_utf8.str,
				.length = dateDiffArgumentState->timezone.value.value.v_utf8.len
			};
			timezoneToApply = ParseTimezone(timezoneToParse);
		}

		SetResultForDateDiff(&dateDiffArgumentState->startDate.value,
							 &dateDiffArgumentState->endDate.value,
							 dateUnitEnum, weekDayEnum,
							 timezoneToApply, &result);
		data->value = result;
		data->kind = AggregationExpressionKind_Constant;
		pfree(dateDiffArgumentState);
	}
	else
	{
		data->operator.arguments = dateDiffArgumentState;
		data->operator.argumentsKind = AggregationExpressionArgumentsKind_Palloc;
	}
}


/* As per the behaviour if any argument is null return null */
static inline bool
IsArgumentForDateDiffNull(bson_value_t *startDate, bson_value_t *endDate,
						  bson_value_t *unit,
						  bson_value_t *timezone, bson_value_t *startOfWeek)
{
	/* if unit is week then check startOfWeek else ignore
	 * Since timezone is optional we don't need to check for EOD. In case of null value we return true.
	 */
	bool isUnitNull = IsExpressionResultNullOrUndefined(unit);

	/* Since unit is null we don't need to evaluate others*/
	if (isUnitNull)
	{
		return true;
	}

	/* If unit is not of type UTF8 then throw error as we cannot process for unit week and startOfWeek*/
	if (unit->value_type != BSON_TYPE_UTF8)
	{
		ThrowLocation5439013Error(unit->value_type, "$dateDiff");
	}
	bool isUnitWeek = !isUnitNull && (strcasecmp(unit->value.v_utf8.str, DWEEK) == 0);
	if (isUnitNull ||
		(isUnitWeek && IsExpressionResultNullOrUndefined(startOfWeek)) ||
		IsExpressionResultNullOrUndefined(startDate) ||
		IsExpressionResultNullOrUndefined(endDate) ||
		IsExpressionResultNullOrUndefined(timezone))
	{
		return true;
	}

	return false;
}


/* This function checks if all the argument values are constant for dateDiffArfumentState struct */
static inline bool
IsDateDiffArgumentConstant(DollarDateDiffArgumentState *
						   dateDiffArgumentState)
{
	bool isBasicComponentsConstant =
		IsAggregationExpressionConstant(&dateDiffArgumentState->startDate) &&
		IsAggregationExpressionConstant(&dateDiffArgumentState->endDate) &&
		IsAggregationExpressionConstant(&dateDiffArgumentState->timezone);

	/* Early return if any of the basic components is not constant. */
	if (!isBasicComponentsConstant)
	{
		return false;
	}

	char *opName = "$dateDiff";
	return IsUnitAndStartOfWeekConstant(&dateDiffArgumentState->unit,
										&dateDiffArgumentState->startOfWeek,
										opName);
}


/* This is a helper function for checking if unit of date and startOfWeek (used when unit is week) are constant. */
static inline bool
IsUnitAndStartOfWeekConstant(AggregationExpressionData *unit,
							 AggregationExpressionData *startOfWeek,
							 char *opName)
{
	/* Check if the 'unit' component of dateTruncArgument is constant. */
	bool isUnitConstant = IsAggregationExpressionConstant(unit);

	if (!isUnitConstant)
	{
		return false;
	}

	/* If unit is null then it's obviously constant, so we can return true */
	if (IsExpressionResultNullOrUndefined(&unit->value))
	{
		return true;
	}

	if (unit->value.value_type != BSON_TYPE_UTF8)
	{
		ThrowLocation5439013Error(unit->value.value_type, opName);
	}

	/* Check if the 'unit' value is 'week'. */
	bool isUnitWeek = strcasecmp(unit->value.value.v_utf8.str,
								 DWEEK) == 0;

	/* If 'unit' is 'week', also check if 'startOfWeek' is constant. */
	if (isUnitWeek)
	{
		return IsAggregationExpressionConstant(startOfWeek);
	}

	/* If all checks passed and we didn't early return, all required components are constant. */
	return true;
}


/* This function parses the input document for dateDiff and then sets the values in corresponding bson_value_t */
static void
ParseInputDocumentForDateDiff(const bson_value_t *inputArgument,
							  bson_value_t *startDate,
							  bson_value_t *endDate,
							  bson_value_t *unit,
							  bson_value_t *timezone,
							  bson_value_t *startOfWeek)
{
	bson_iter_t docIter;
	BsonValueInitIterator(inputArgument, &docIter);

	while (bson_iter_next(&docIter))
	{
		const char *key = bson_iter_key(&docIter);
		if (strcmp(key, "startDate") == 0)
		{
			*startDate = *bson_iter_value(&docIter);
		}
		else if (strcmp(key, "endDate") == 0)
		{
			*endDate = *bson_iter_value(&docIter);
		}
		else if (strcmp(key, "startOfWeek") == 0)
		{
			*startOfWeek = *bson_iter_value(&docIter);
		}
		else if (strcmp(key, "timezone") == 0)
		{
			*timezone = *bson_iter_value(&docIter);
		}
		else if (strcmp(key, "unit") == 0)
		{
			*unit = *bson_iter_value(&docIter);
		}
		else
		{
			ereport(ERROR, (errcode(MongoLocation5166302), errmsg(
								"Unrecognized argument to $dateDiff: %s",
								key),
							errhint(
								"Unrecognized argument to $dateDiff: %s",
								key)));
		}
	}

	/*	Validation to check start and end date */
	if (startDate->value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(MongoLocation5166303), errmsg(
							"Missing 'startDate' parameter to $dateDiff"),
						errhint("Missing 'startDate' parameter to $dateDiff")));
	}

	if (endDate->value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(MongoLocation5166304), errmsg(
							"Missing 'endDate' parameter to $dateDiff"),
						errhint("Missing 'endDate' parameter to $dateDiff")));
	}

	if (unit->value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(MongoLocation5166305), errmsg(
							"Missing 'unit' parameter to $dateDiff"),
						errhint("Missing 'unit' parameter to $dateDiff")));
	}

	if (startOfWeek->value_type == BSON_TYPE_EOD)
	{
		startOfWeek->value_type = BSON_TYPE_UTF8;
		startOfWeek->value.v_utf8.str = "Sunday";
		startOfWeek->value.v_utf8.len = 6;
	}

	if (timezone->value_type == BSON_TYPE_EOD)
	{
		timezone->value_type = BSON_TYPE_UTF8;
		timezone->value.v_utf8.str = "UTC";
		timezone->value.v_utf8.len = 3;
	}
}


/*
 * This is a utility function for getting the date unit char* from date unit.
 */
static inline const char *
GetDateUnitStringFromEnum(DateUnit unitEnum)
{
	Assert(unitEnum > 0);
	return dateUnitStr[unitEnum - 1];
}


/*
 * This is a helper function which takes in current day of week as per postgres and then takes in week day enum which
 * can be the startOfWeek. It computes how many days difference is between those 2 days and returns that.
 * The value of dow can range from 1-7. 1 for sunday and so on.
 * Here dow is the current dow which comes from startDate.
 * weekDayEnum is what startOfWeek is input from user.
 */
static inline int
GetDifferenceInDaysForStartOfWeek(int dow, WeekDay weekDayEnum)
{
	int adjustedDayForEnum;

	/* 7 is a sunday according to enum and since we (via postgres functions) count days from 1-7 1 being sunday. */
	/* So, we adjust enum values as according to dow values. */
	if (weekDayEnum == 7)
	{
		adjustedDayForEnum = 1;
	}
	else
	{
		adjustedDayForEnum = weekDayEnum + 1;
	}

	/* Logic to compute difference in days. We need to iterate backwards here always. */
	if (dow == adjustedDayForEnum)
	{
		return 0;
	}
	else if (dow > adjustedDayForEnum)
	{
		return dow - adjustedDayForEnum;
	}
	else
	{
		return (dow + 7) - adjustedDayForEnum;
	}
}


/*
 * This function takes in the enum , timezone and start and end epoch seconds and returns the difference between then in milliseconds
 * This function is usually for units like seconds, minute, hour, day.
 */
static float8
GetEpochDiffForDateDiff(DateUnit dateUnitEnum, ExtensionTimezone timezoneToApply, int64
						startDateEpoch, int64 endDateEpoch)
{
	/*
	 *  We are trying to process the output with the format like this equivalent pg query.
	 *  select (date_part('epoch',date_trunc('unit','endTimestamp' at time zone 'timezoneToApply')) -
	 *  date_part('epoch',date_trunc('unit','starTimestamp at time zone 'timezoneToApply')))
	 *
	 */
	const char *unitStr = GetDateUnitStringFromEnum(dateUnitEnum);

	Datum startTimestampWithTimezone = GetPgTimestampFromEpochWithTimezone(startDateEpoch,
																		   timezoneToApply);
	Datum endTimestampWithTimezone = GetPgTimestampFromEpochWithTimezone(endDateEpoch,
																		 timezoneToApply);
	Datum truncatedStartTimestamp = TruncateTimestampToPrecision(
		startTimestampWithTimezone,
		unitStr);
	Datum truncatedEndTimestamp = TruncateTimestampToPrecision(endTimestampWithTimezone,
															   unitStr);
	float8 truncatedStartEpochTimestamp = DatumGetFloat8(OidFunctionCall2(
															 PostgresDatePartFunctionId(),
															 CStringGetTextDatum(
																 EPOCH),
															 truncatedStartTimestamp));
	float8 truncatedEndEpochTimestamp = DatumGetFloat8(OidFunctionCall2(
														   PostgresDatePartFunctionId(),
														   CStringGetTextDatum(
															   EPOCH),
														   truncatedEndTimestamp));
	return ((truncatedEndEpochTimestamp - truncatedStartEpochTimestamp) *
			MILLISECONDS_IN_SECOND);
}


/*
 * This function takes in the input arguments and process the end result for dateDiff for different unit types and sets them in result.
 * The return result value is of type int64.
 */
static void
SetResultForDateDiff(bson_value_t *startDate, bson_value_t *endDate,
					 DateUnit dateUnitEnum, WeekDay weekdayEnum,
					 ExtensionTimezone timezoneToApply, bson_value_t *result)
{
	result->value_type = BSON_TYPE_INT64;
	float8 preciseResultDiff = 0;
	int64 startDateEpoch = BsonValueAsDateTime(startDate);
	int64 endDateEpoch = BsonValueAsDateTime(endDate);
	ExtensionTimezone defaultUTCTimezone = {
		.offsetInMs = 0,
		.isUtcOffset = true
	};
	switch (dateUnitEnum)
	{
		case DateUnit_Millisecond:
		{
			preciseResultDiff = endDateEpoch - startDateEpoch;
			break;
		}

		case DateUnit_Second:
		{
			preciseResultDiff = GetEpochDiffForDateDiff(dateUnitEnum, defaultUTCTimezone,
														startDateEpoch, endDateEpoch) /
								MILLISECONDS_IN_SECOND;
			break;
		}

		case DateUnit_Minute:
		{
			preciseResultDiff = GetEpochDiffForDateDiff(dateUnitEnum, defaultUTCTimezone,
														startDateEpoch, endDateEpoch) /
								(MILLISECONDS_IN_SECOND * SECONDS_IN_MINUTE);
			break;
		}

		case DateUnit_Hour:
		{
			preciseResultDiff = GetEpochDiffForDateDiff(dateUnitEnum, defaultUTCTimezone,
														startDateEpoch, endDateEpoch) /
								(MILLISECONDS_IN_SECOND * SECONDS_IN_MINUTE *
								 MINUTES_IN_HOUR);
			break;
		}

		case DateUnit_Day:
		{
			preciseResultDiff = GetEpochDiffForDateDiff(dateUnitEnum, timezoneToApply,
														startDateEpoch, endDateEpoch) /
								(MILLISECONDS_IN_SECOND * SECONDS_IN_MINUTE *
								 MINUTES_IN_HOUR
								 *
								 HOURS_PER_DAY);
			break;
		}

		case DateUnit_Week:
		{
			Datum startTimestampWithTimezone = GetPgTimestampFromEpochWithTimezone(
				startDateEpoch,
				timezoneToApply);
			Datum endTimestampWithTimezone = GetPgTimestampFromEpochWithTimezone(
				endDateEpoch,
				timezoneToApply);

			/* default dow in postgres resolves to sunday and goes like sunday , monday, tuesday, .. */
			int dayOfWeek = GetDatePartFromPgTimestamp(startTimestampWithTimezone,
													   DatePart_DayOfWeek);
			int diffBwStartOfWeekAndCurrent = GetDifferenceInDaysForStartOfWeek(dayOfWeek,
																				weekdayEnum);
			Datum truncatedStartTimestamp = TruncateTimestampToPrecision(
				startTimestampWithTimezone, DDAY);
			Datum truncatedEndTimestamp = TruncateTimestampToPrecision(
				endTimestampWithTimezone, DDAY);

			float8 truncatedStartEpochTimestamp = DatumGetFloat8(OidFunctionCall2(
																	 PostgresDatePartFunctionId(),
																	 CStringGetTextDatum(
																		 EPOCH),
																	 truncatedStartTimestamp));
			float8 truncatedEndEpochTimestamp = DatumGetFloat8(OidFunctionCall2(
																   PostgresDatePartFunctionId(),
																   CStringGetTextDatum(
																	   EPOCH),
																   truncatedEndTimestamp));
			preciseResultDiff = (truncatedEndEpochTimestamp -
								 truncatedStartEpochTimestamp +
								 diffBwStartOfWeekAndCurrent * SECONDS_IN_DAY) /
								(SECONDS_IN_DAY * 7);
			break;
		}

		case DateUnit_Month:
		case DateUnit_Quarter:
		{
			Datum startTimestampWithTimezone = GetPgTimestampFromEpochWithTimezone(
				startDateEpoch,
				timezoneToApply);
			Datum endTimestampWithTimezone = GetPgTimestampFromEpochWithTimezone(
				endDateEpoch,
				timezoneToApply);
			int64 endTimestampYear = GetDatePartFromPgTimestamp(endTimestampWithTimezone,
																DatePart_Year);
			int64 startTimestampYear = GetDatePartFromPgTimestamp(
				startTimestampWithTimezone, DatePart_Year);
			int64 endTimestampMonths = GetDatePartFromPgTimestamp(
				endTimestampWithTimezone, DatePart_Month);
			int64 startTimestampMonths = GetDatePartFromPgTimestamp(
				startTimestampWithTimezone, DatePart_Month);
			if (dateUnitEnum == DateUnit_Quarter)
			{
				/* for quarter logic is (years_diff) *4 + (quarter month end - quarter month start) */
				preciseResultDiff = (float8) ((endTimestampYear - startTimestampYear) *
											  QuartersPerYear +
											  ((endTimestampMonths - 1) /
											   MonthsPerQuarter - (startTimestampMonths -
																   1) /
											   MonthsPerQuarter));
			}
			else
			{
				/* For months logic is years_diff * 12 + (DATE_PART('month', end) - DATE_PART('month', start))*/
				preciseResultDiff = (float8) ((endTimestampYear - startTimestampYear) *
											  MONTHS_PER_YEAR +
											  (endTimestampMonths -
											   startTimestampMonths));
			}
			break;
		}

		case DateUnit_Year:
		{
			Datum startTimestampWithTimezone = GetPgTimestampFromEpochWithTimezone(
				startDateEpoch,
				timezoneToApply);
			Datum endTimestampWithTimezone = GetPgTimestampFromEpochWithTimezone(
				endDateEpoch,
				timezoneToApply);
			int64 endTimestampYear = GetDatePartFromPgTimestamp(endTimestampWithTimezone,
																DatePart_Year);
			int64 startTimestampYear = GetDatePartFromPgTimestamp(
				startTimestampWithTimezone, DatePart_Year);
			preciseResultDiff = (float8) (endTimestampYear - startTimestampYear);
			break;
		}

		default:
		{
			break;
		}
	}

	/* Floor as for negative cases. */
	result->value.v_int64 = (int64) (floor(preciseResultDiff));
}


/*
 * This function takes in a timestamp and truncates to the nearest unit.
 * For eg: - trunc on timestamp 10-10-2023T04:50:00 with unit hour will yield 10-10-2023T04:00:00
 */
static Datum
TruncateTimestampToPrecision(Datum timestamp, const char *precisionUnit)
{
	return DirectFunctionCall2(timestamptz_trunc,
							   CStringGetTextDatum(precisionUnit),
							   timestamp);
}


/*
 * This function validates the input arguments and then throws error if any of them has invalid input type or value.
 * Post validation this function extracts the bson_value_t startOFweek and unit value str and sets them in dateUnit and week day enum.
 */
static void
ValidateInputArgumentForDateDiff(bson_value_t *startDate, bson_value_t *endDate,
								 bson_value_t *unit, bson_value_t *timezone,
								 bson_value_t *startOfWeek, DateUnit *dateUnitEnum,
								 WeekDay *weekDayEnum)
{
	/* validate start and end date types */
	bool isStartDateValid = IsBsonValueDateTimeFormat(startDate->value_type);
	bool isEndDateValid = IsBsonValueDateTimeFormat(endDate->value_type);
	if (!isStartDateValid || !isEndDateValid)
	{
		ereport(ERROR, (errcode(MongoLocation5166307), errmsg(
							"$dateDiff requires '%s' to be a date, but got %s",
							(isStartDateValid ? "endDate" : "startDate"),
							BsonTypeName(isStartDateValid ? endDate->value_type :
										 startDate->value_type)),
						errhint("$dateDiff requires '%s' to be a date, but got %s",
								(isStartDateValid ? "endDate" : "startDate"),
								BsonTypeName(isStartDateValid ? endDate->value_type :
											 startDate->value_type))));
	}

	/* validate unit type */
	if (unit->value_type != BSON_TYPE_UTF8)
	{
		ThrowLocation5439013Error(unit->value_type, "$dateDiff");
	}

	/* validate unit values */
	*dateUnitEnum = GetDateUnitFromString(unit->value.v_utf8.str);
	if (*dateUnitEnum == DateUnit_Invalid)
	{
		ereport(ERROR, (errcode(MongoLocation5439014), errmsg(
							"$dateDiff parameter 'unit' value cannot be recognized as a time unit: %s",
							unit->value.v_utf8.str),
						errhint(
							"$dateDiff parameter 'unit' value cannot be recognized as a time unit")));
	}

	/* Validate startOfWeekValue only if unit is week. Index 6 in the list if 'week' */
	if (*dateUnitEnum == DateUnit_Week)
	{
		if (startOfWeek->value_type != BSON_TYPE_UTF8)
		{
			ThrowLocation5439015Error(startOfWeek->value_type, "$dateDiff");
		}

		/* Typed loop check as days can never be increased. */
		for (int index = 0; index < 7; index++)
		{
			if (strcasecmp(startOfWeek->value.v_utf8.str, weekDaysFullName[index]) == 0 ||
				strcasecmp(startOfWeek->value.v_utf8.str, weekDaysAbbreviated[index]) ==
				0)
			{
				/* Doing index +1 as invalid is the first index which is not in the weekDays list. */
				*weekDayEnum = (WeekDay) (index + 1);
				break;
			}
		}
		if (*weekDayEnum == WeekDay_Invalid)
		{
			ereport(ERROR, (errcode(MongoLocation5439016), errmsg(
								"$dateDiff parameter 'startOfWeek' value cannot be recognized as a day of a week: %s",
								startOfWeek->value.v_utf8.str),
							errhint(
								"$dateDiff parameter 'startOfWeek' value cannot be recognized as a day of a week")));
		}
	}

	if (timezone->value_type != BSON_TYPE_EOD && timezone->value_type != BSON_TYPE_UTF8)
	{
		ThrowLocation40517Error(timezone->value_type);
	}
}


/* This function handles the pre-parsing logic for the $dateFromString operator. */
void
HandlePreParsedDollarDateFromString(pgbson *doc, void *arguments,
									ExpressionResult *expressionResult)
{
	DollarDateFromStringArgumentState *dateFromStringState = arguments;
	bool isNullOnEmpty = false;

	bson_value_t dateString = { 0 };
	bson_value_t format = { 0 };
	bson_value_t timezone = { 0 };
	bson_value_t onNull = { 0 };
	bson_value_t onError = { 0 };

	ExpressionResult childExpression = ExpressionResultCreateChild(expressionResult);
	EvaluateAggregationExpressionData(&dateFromStringState->dateString, doc,
									  &childExpression,
									  isNullOnEmpty);
	dateString = childExpression.value;

	ExpressionResultReset(&childExpression);
	EvaluateAggregationExpressionData(&dateFromStringState->format, doc,
									  &childExpression,
									  isNullOnEmpty);
	format = childExpression.value;

	ExpressionResultReset(&childExpression);
	EvaluateAggregationExpressionData(&dateFromStringState->timezone, doc,
									  &childExpression,
									  isNullOnEmpty);
	timezone = childExpression.value;

	ExpressionResultReset(&childExpression);
	EvaluateAggregationExpressionData(&dateFromStringState->onNull, doc,
									  &childExpression,
									  isNullOnEmpty);
	onNull = childExpression.value;

	ExpressionResultReset(&childExpression);
	EvaluateAggregationExpressionData(&dateFromStringState->onError, doc,
									  &childExpression,
									  isNullOnEmpty);
	onError = childExpression.value;

	bson_value_t result = { .value_type = BSON_TYPE_NULL };
	bool isDateStringNull = false;
	if (IsArgumentForDateFromStringNull(&dateString, &format, &timezone,
										&isDateStringNull,
										dateFromStringState->isTimezoneProvided))
	{
		/* Set the result to onNull expression if dateString is null otherwise set null. */
		if (isDateStringNull)
		{
			result = onNull;
		}

		ExpressionResultSetValue(expressionResult, &result);
		return;
	}

	/* This flag is used to set context that error has occurred but we still need to set the result as onError is specified. */
	bool isInputValid = true;
	ExtensionTimezone timezoneToApply = {
		.offsetInMs = 0,
		.isUtcOffset = true
	};

	/* This will store value in each of the date part. */
	DollarDateFromPartsBsonValue *dateFromParts = palloc0(
		sizeof(DollarDateFromPartsBsonValue));

	ValidateInputForDateFromString(&dateString, &format, &timezone, &onError,
								   dateFromParts,
								   &timezoneToApply, &isInputValid);

	/* If isInputValid is false then we set the result as onError otherwise we would have already thrown error. */
	if (!isInputValid)
	{
		result = onError;
	}
	else
	{
		SetResultValueForDateFromString(dateFromParts, timezoneToApply, &result);
	}
	pfree(dateFromParts);
	ExpressionResultSetValue(expressionResult, &result);
}


/*
 * This function takes in the input argument for the $dateFromString operator.
 * The input argument should be in the following format:
 * {
 *     $dateFromString: {
 *         dateString: <dateStringExpression>,
 *         format: <formatStringExpression>,
 *         timezone: <tzExpression>,
 *         onError: <onErrorExpression>,
 *         onNull: <onNullExpression>
 *     }
 * }
 */
void
ParseDollarDateFromString(const bson_value_t *argument, AggregationExpressionData *data,
						  ParseAggregationExpressionContext *context)
{
	if (argument->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(MongoLocation40540), errmsg(
							"$dateFromString only supports an object as an argument, found: %s",
							BsonTypeName(argument->value_type)),
						errhint(
							"$dateFromString only supports an object as an argument, found: %s",
							BsonTypeName(argument->value_type))));
	}
	bson_value_t format = { 0 };
	bson_value_t dateString = { 0 };
	bson_value_t timezone = { 0 };
	bson_value_t onNull = { 0 };
	bson_value_t onError = { 0 };

	bool isTimezoneProvided = false;
	ParseInputForDateFromString(argument, &dateString, &format, &timezone, &onNull,
								&onError, &isTimezoneProvided);
	DollarDateFromStringArgumentState *dateFromStringArguments = palloc0(
		sizeof(DollarDateFromStringArgumentState));

	/* We need to set this flag as true because we need to identify a way if input timezone was provided or not. */
	dateFromStringArguments->isTimezoneProvided = isTimezoneProvided;

	ParseAggregationExpressionData(&dateFromStringArguments->dateString, &dateString,
								   context);
	ParseAggregationExpressionData(&dateFromStringArguments->format, &format, context);
	ParseAggregationExpressionData(&dateFromStringArguments->timezone, &timezone,
								   context);
	ParseAggregationExpressionData(&dateFromStringArguments->onNull, &onNull, context);
	ParseAggregationExpressionData(&dateFromStringArguments->onError, &onError, context);

	if (IsDateFromStringArgumentConstant(dateFromStringArguments))
	{
		bson_value_t result = { .value_type = BSON_TYPE_NULL };

		bool isDateStringNull = false;
		if (IsArgumentForDateFromStringNull(&dateFromStringArguments->dateString.value,
											&dateFromStringArguments->format.value,
											&dateFromStringArguments->timezone.value,
											&isDateStringNull,
											isTimezoneProvided))
		{
			/* Set the result to onNull expression if dateString is null otherwise set null. */
			if (isDateStringNull)
			{
				result = dateFromStringArguments->onNull.value;
			}

			data->value = result;
			data->kind = AggregationExpressionKind_Constant;
			pfree(dateFromStringArguments);
			return;
		}

		/* This flag is used to set context that error has occurred but we still need to set the result as onError is specified. */
		bool isInputValid = true;
		ExtensionTimezone timezoneToApply = {
			.offsetInMs = 0,
			.isUtcOffset = true
		};

		/* This will store value in each of the date part. */
		DollarDateFromPartsBsonValue *dateFromParts = palloc0(
			sizeof(DollarDateFromPartsBsonValue));
		ValidateInputForDateFromString(&dateFromStringArguments->dateString.value,
									   &dateFromStringArguments->format.value,
									   &dateFromStringArguments->timezone.value,
									   &dateFromStringArguments->onError.value,
									   dateFromParts,
									   &timezoneToApply,
									   &isInputValid);

		/* If isInputValid is false then we set the result as onError otherwise we would have already thrown error. */
		if (!isInputValid)
		{
			result = dateFromStringArguments->onError.value;
		}
		else
		{
			SetResultValueForDateFromString(dateFromParts, timezoneToApply, &result);
		}

		data->value = result;
		data->kind = AggregationExpressionKind_Constant;
		pfree(dateFromParts);
		pfree(dateFromStringArguments);
	}
	else
	{
		data->operator.arguments = dateFromStringArguments;
		data->operator.argumentsKind = AggregationExpressionArgumentsKind_Palloc;
	}
}


/*
 * This function takes in the dateFromparts and constructs the date string based on the parts.
 */
static inline void
ConstructDateStringFromParts(DollarDateFromPartsBsonValue *dateFromParts,
							 char *postgresDateString)
{
	if (dateFromParts->isIsoFormat)
	{
		/* Construct the date string based on the parts */
		sprintf(postgresDateString, "%d-%d-%d %d:%d:%d.%d",
				dateFromParts->isoWeekYear.value.v_int32,
				(dateFromParts->isoWeek.value_type != BSON_TYPE_EOD) ?
				dateFromParts->isoWeek.value.v_int32 : 1,
				(dateFromParts->isoDayOfWeek.value_type != BSON_TYPE_EOD) ?
				dateFromParts->isoDayOfWeek.value.v_int32 : 1,
				(dateFromParts->hour.value_type != BSON_TYPE_EOD) ?
				dateFromParts->hour.value.v_int32 : 0,
				(dateFromParts->minute.value_type != BSON_TYPE_EOD) ?
				dateFromParts->minute.value.v_int32 : 0,
				(dateFromParts->second.value_type != BSON_TYPE_EOD) ?
				dateFromParts->second.value.v_int32 : 0,
				(dateFromParts->millisecond.value_type != BSON_TYPE_EOD) ?
				dateFromParts->millisecond.value.v_int32 : 0);
	}
	else
	{
		sprintf(postgresDateString, "%d-%d-%d %d:%d:%d.%d",
				dateFromParts->year.value.v_int32,
				dateFromParts->month.value.v_int32,
				dateFromParts->day.value.v_int32,
				(dateFromParts->hour.value_type != BSON_TYPE_EOD) ?
				dateFromParts->hour.value.v_int32 : 0,
				(dateFromParts->minute.value_type != BSON_TYPE_EOD) ?
				dateFromParts->minute.value.v_int32 : 0,
				(dateFromParts->second.value_type != BSON_TYPE_EOD) ?
				dateFromParts->second.value.v_int32 : 0,
				(dateFromParts->millisecond.value_type != BSON_TYPE_EOD) ?
				dateFromParts->millisecond.value.v_int32 : 0);
	}
}


/**
 * Checks if the argument for the `dateFromString` function is null.
 */
static inline bool
IsArgumentForDateFromStringNull(bson_value_t *dateString,
								bson_value_t *format,
								bson_value_t *timezone,
								bool *isDateStringNull,
								bool isTimezoneProvided)
{
	*isDateStringNull = IsExpressionResultNullOrUndefined(dateString);
	if (*isDateStringNull)
	{
		return true;
	}
	return IsExpressionResultNullOrUndefined(format) ||
		   (isTimezoneProvided && IsExpressionResultNullOrUndefined(timezone));
}


/*
 * This function checks if the input arguments specified in struct are constant
 */
static inline bool
IsDateFromStringArgumentConstant(DollarDateFromStringArgumentState *
								 dateFromStringArguments)
{
	if (IsAggregationExpressionConstant(&dateFromStringArguments->dateString) &&
		IsAggregationExpressionConstant(&dateFromStringArguments->format) &&
		IsAggregationExpressionConstant(&dateFromStringArguments->timezone) &&
		IsAggregationExpressionConstant(&dateFromStringArguments->onNull) &&
		IsAggregationExpressionConstant(&dateFromStringArguments->onError))
	{
		return true;
	}

	return false;
}


/*
 * This function is used to compare format strings during binary search
 */
static inline int
CompareDateFormatMap(const void *a, const void *b)
{
	const DateFormatMap *mapA = (const DateFormatMap *) a;
	const DateFormatMap *mapB = (const DateFormatMap *) b;
	return strcmp(mapA->mongoFormat, mapB->mongoFormat);
}


/*
 * This function checks if all the required parts are present in the input arguments.
 */
static inline void
CheckIfRequiredPartsArePresent(DollarDateFromPartsBsonValue *dateFromParts,
							   char *dateString, bool *isInputValid, bool
							   isOnErrorPresent)
{
	if (dateFromParts->isIsoFormat)
	{
		if (dateFromParts->isoWeekYear.value_type == BSON_TYPE_EOD)
		{
			CONDITIONAL_EREPORT(isOnErrorPresent, ereport(ERROR, (errcode(
																	  MongoConversionFailure),
																  errmsg(
																	  "Error parsing date string '%s';The parsed date was invalid",
																	  dateString),
																  errhint(
																	  "Error parsing date string ;The parsed date was invalid"))));
			*isInputValid = false;
			return;
		}
	}
	/* Special handling for case when day of year is present as it only requires year and this is non-iso format*/
	else if (dateFromParts->dayOfYear.value_type == BSON_TYPE_INT32)
	{
		if (dateFromParts->year.value_type == BSON_TYPE_EOD)
		{
			CONDITIONAL_EREPORT(isOnErrorPresent, ereport(ERROR, (errcode(
																	  MongoConversionFailure),
																  errmsg(
																	  "Error Parsing date string %s;A 'day of year' can only come after a year has been found",
																	  dateString),
																  errhint(
																	  "Error Parsing date string;A 'day of year' can only come after a year has been found; No Year found  day of year is %d",
																	  dateFromParts->
																	  dayOfYear.value.
																	  v_int32)
																  )));
			*isInputValid = false;
			return;
		}
	}
	else
	{
		if (dateFromParts->year.value_type == BSON_TYPE_EOD ||
			dateFromParts->month.value_type == BSON_TYPE_EOD ||
			dateFromParts->day.value_type == BSON_TYPE_EOD)
		{
			CONDITIONAL_EREPORT(isOnErrorPresent, ereport(ERROR, (errcode(
																	  MongoConversionFailure),
																  errmsg(
																	  "an incomplete date/time string has been found, with elements missing: '%s'",
																	  dateString),
																  errhint(
																	  "an incomplete date/time string has been found, with elements missing"))));
			*isInputValid = false;
			return;
		}
	}
}


/*
 * This function frees the memory allocated for the elements array.
 */
static inline void
DeepFreeFormatArray(char **elements, int sizeOfSplitFormat)
{
	for (int i = 0; i < sizeOfSplitFormat; i++)
	{
		pfree(elements[i]);
	}
	pfree(elements);
}


static inline int
IsFormatSpecifierExist(char *formatSpecifier)
{
	DateFormatMap key;
	key.mongoFormat = formatSpecifier;
	DateFormatMap *result = bsearch(&key, dateFormats, sizeof(dateFormats) /
									sizeof(DateFormatMap), sizeof(DateFormatMap),
									CompareDateFormatMap);
	if (result != NULL)
	{
		return result - dateFormats;
	}
	return -1;
}


/**
 * Parses the given input document and sets the result in respective variables.
 */
void
ParseInputForDateFromString(const bson_value_t *inputArgument, bson_value_t *dateString,
							bson_value_t *format, bson_value_t *timezone,
							bson_value_t *onNull, bson_value_t *onError,
							bool *isTimezoneProvided)
{
	bson_iter_t docIter;
	BsonValueInitIterator(inputArgument, &docIter);

	while (bson_iter_next(&docIter))
	{
		const char *key = bson_iter_key(&docIter);
		if (strcmp(key, "dateString") == 0)
		{
			*dateString = *bson_iter_value(&docIter);
		}
		else if (strcmp(key, "format") == 0)
		{
			*format = *bson_iter_value(&docIter);
		}
		else if (strcmp(key, "onNull") == 0)
		{
			*onNull = *bson_iter_value(&docIter);
		}
		else if (strcmp(key, "timezone") == 0)
		{
			*timezone = *bson_iter_value(&docIter);
			*isTimezoneProvided = true;
		}
		else if (strcmp(key, "onError") == 0)
		{
			*onError = *bson_iter_value(&docIter);
		}
		else
		{
			ereport(ERROR, (errcode(MongoLocation40541), errmsg(
								"Unrecognized argument to $dateFromString: %s",
								key),
							errhint(
								"Unrecognized argument to $dateFromString: %s",
								key)));
		}
	}

	/*	Validation to check date and unit are required. */
	if (dateString->value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(MongoLocation40542), errmsg(
							"Missing 'dateString' parameter to $dateFromString"),
						errhint("Missing 'dateString' parameter to $dateFromString")));
	}

	if (onNull->value_type == BSON_TYPE_EOD)
	{
		onNull->value_type = BSON_TYPE_NULL;
	}

	/* Setting the default value for format in case of absent */
	if (format->value_type == BSON_TYPE_EOD)
	{
		format->value_type = BSON_TYPE_UTF8;
		format->value.v_utf8.str = (char *) DefaultFormatForDateFromString;
		format->value.v_utf8.len = DefaultFormatLenForDateFromString;
	}
}


/*
 * This function parses the dateString based on the formatElements array and writes the pgFormatString and corresponding dateString for postgres to convert.
 */
static void
ParseDateStringWithFormat(StringView dateStringView, char **elements, int
						  sizeOfSplitFormat,
						  DollarDateFromPartsBsonValue *dateFromParts, bool *isInputValid,
						  bool isOnErrorPresent)
{
	char *dateString = (char *) dateStringView.string;
	int sizeOfDateString = dateStringView.length;

	/* This is to iterate over the dateString char by char. */
	int indexOfDateStringIter = 0;

	/* This is to iterate over the elements of format array. */
	int indexOfElementIter = 0;

	/* We have to error in case of formatStr contains iso and non-iso parts together. */
	int isIsoWeekDateFmt = -1;

	/* This will be used to check if the iso format variable was ever changed*/
	int prevIsoFormat = -1;

	/* Iterate over the format elements and dateString. */
	while ((indexOfElementIter < sizeOfSplitFormat) &&
		   (indexOfDateStringIter < sizeOfDateString))
	{
		char *elementAtIndex = elements[indexOfElementIter];


		/* Parsing offset minutes. */
		if (elementAtIndex[0] == '%' && elementAtIndex[1] == 'Z')
		{
			ValidateOffsetMinutes(dateString, sizeOfDateString, &indexOfDateStringIter,
								  dateFromParts, isInputValid, isOnErrorPresent);
			if (!(*isInputValid))
			{
				return;
			}
		}
		/* Parsing utc offset */
		else if (elementAtIndex[0] == '%' && elementAtIndex[1] == 'z')
		{
			/* Skip the space after the date string if any. */
			while (dateString[indexOfDateStringIter] == ' ')
			{
				indexOfDateStringIter++;
			}
			ValidateTimezoneOffsetForDateString(dateString, sizeOfDateString,
												&indexOfDateStringIter, dateFromParts,
												isInputValid, isOnErrorPresent);
			if (!(*isInputValid))
			{
				return;
			}
		}
		/* If elementAtIndex is a format specifier */
		else if (elementAtIndex[0] == '%' && elementAtIndex[1] != '%')
		{
			/* Get the index of the format specifier in the list of supported format specifiers. */
			int fmtSpecifierIndex = IsFormatSpecifierExist(elementAtIndex);

			/* Format specifier is not found in the list of supported format specifiers. */
			if (fmtSpecifierIndex == -1)
			{
				ereport(ERROR, (errcode(MongoLocation18536), errmsg(
									"Invalid format character '%s' in format string",
									elementAtIndex),
								errhint(
									"Invalid format character in format string")));
			}

			/* Validate the date part and extract the value from dateString */
			ValidateDatePartFromDateString(fmtSpecifierIndex,
										   dateString, sizeOfDateString,
										   &indexOfDateStringIter,
										   dateFromParts, isInputValid,
										   &isIsoWeekDateFmt, isOnErrorPresent);

			/* This is to break if the input is invalid as this would mean isOnError is present. */
			if (!isInputValid)
			{
				break;
			}

			/* Mixing of iso and non-iso parts is not allowed. */

			if (prevIsoFormat != -1 && prevIsoFormat != isIsoWeekDateFmt)
			{
				CONDITIONAL_EREPORT(isOnErrorPresent, ereport(ERROR, (errcode(
																		  MongoConversionFailure),
																	  errmsg(
																		  "Error parsing date string '%s'; %d: Mixing of ISO dates with natural dates is not allowed",
																		  dateString,
																		  sizeOfDateString),
																	  errhint(
																		  "Error parsing date string; Mixing of ISO dates with natural dates is not allowed"))));
				*isInputValid = false;
				return;
			}

			/* On successful parsing of the date part, set the prevIsoFormat to isIsoWeekDateFmt and we know that what sort of format we are dealing with. */
			prevIsoFormat = isIsoWeekDateFmt;
		}
		else
		{
			/* %% is a escape for literal %, so modify the element at index to be % only for comparison in dateString */
			if (strcmp(elementAtIndex, "%%") == 0)
			{
				elementAtIndex = "%";
			}

			/* Mismatch in the element at index. */
			if (elementAtIndex[0] != dateString[indexOfDateStringIter])
			{
				CONDITIONAL_EREPORT(isOnErrorPresent, ereport(ERROR, (errcode(
																		  MongoConversionFailure),
																	  errmsg(
																		  "Error parsing date string '%s'; %d: Format literal not found '%c'",
																		  dateString,
																		  indexOfDateStringIter,
																		  dateString[
																			  indexOfDateStringIter
																		  ]),
																	  errhint(
																		  "Error parsing date string. Mismatch in element at index %d ",
																		  indexOfDateStringIter))));
				*isInputValid = false;
				return;
			}
			indexOfDateStringIter++;
		}

		/* increment the format specifier index */
		indexOfElementIter++;
	}

	/* Date String is left but format is over */
	if ((indexOfDateStringIter < sizeOfDateString) && (indexOfElementIter >=
													   sizeOfSplitFormat))
	{
		CONDITIONAL_EREPORT(isOnErrorPresent, ereport(ERROR, (errcode(
																  MongoConversionFailure),
															  errmsg(
																  "Error parsing date string %s'; %d: Trailing data '%c'",
																  dateString,
																  indexOfDateStringIter,
																  dateString[
																	  indexOfDateStringIter
																  ]),
															  errhint(
																  "Error parsing date string %s'; %d: Trailing data '%c'",
																  dateString,
																  indexOfDateStringIter,
																  dateString[
																	  indexOfDateStringIter
																  ]))));
		*isInputValid = false;
		return;
	}

	/* Format is left but dateString is over. */
	if ((indexOfElementIter < sizeOfSplitFormat) && (indexOfDateStringIter >=
													 sizeOfDateString))
	{
		CONDITIONAL_EREPORT(isOnErrorPresent, ereport(ERROR, (errcode(
																  MongoConversionFailure),
															  errmsg(
																  "Error parsing date string %s'; %d: Not enough data available to satisfy format ''",
																  dateString,
																  sizeOfDateString),
															  errhint(
																  "Error parsing date string. Not enough data available to satisfy format"))));
		*isInputValid = false;
		return;
	}
	dateFromParts->isIsoFormat = isIsoWeekDateFmt;

	CheckIfRequiredPartsArePresent(dateFromParts, dateString, isInputValid,
								   isOnErrorPresent);
}


/*
 * This function parses the UTC offset with +/- from given string and sets the timezoneOffset.
 */
static inline void
ParseUtcOffsetForDateString(char *dateString, int sizeOfDateString,
							int *indexOfDateStringIter, char *timezoneOffset,
							int *timezoneOffsetLen, bool
							isOnErrorPresent, bool *isInputValid)
{
	/* Copying the hours */
	int hoursLen = 0;
	while (isdigit(dateString[*indexOfDateStringIter]) && hoursLen < 2)
	{
		timezoneOffset[(*timezoneOffsetLen)++] = dateString[(*indexOfDateStringIter)++];
		hoursLen++;
	}

	/* Copying the colon */
	if (dateString[*indexOfDateStringIter] == ':')
	{
		timezoneOffset[(*timezoneOffsetLen)++] = dateString[(*indexOfDateStringIter)++];
	}

	/* Copying the minutes */
	int minutesLen = 0;
	while (isdigit(dateString[*indexOfDateStringIter]) && minutesLen < 2)
	{
		timezoneOffset[(*timezoneOffsetLen)++] = dateString[(*indexOfDateStringIter)++];
		minutesLen++;
	}

	/* Copying the null terminator */
	timezoneOffset[(*timezoneOffsetLen)] = '\0';

	/* In case the length is < 2 then it is invalid. 1 for +/- and one for : or 2 digits. */
	if (*timezoneOffsetLen < 2)
	{
		CONDITIONAL_EREPORT(isOnErrorPresent,
							ThrowMongoConversionErrorForTimezoneIdentifier(dateString,
																		   sizeOfDateString,
																		   indexOfDateStringIter));
		*isInputValid = false;
		return;
	}
}


/*
 * This function reads through the given dateString with given number of chars to read and adds those in dateElement.
 * Since all the parts of timestamp as of now are numbers we can read through them and add them in dateElement. In case not numbers we break
 * and set the dateElement. This way max chars is not honoured and we can add validations for the same.
 */
static inline void
ReadThroughDatePart(char *dateString, int *indexOfDateStringIter, int maxCharsToRead,
					char *dateElement, bool areAllNumbers)
{
	int i = 0;
	while (i < maxCharsToRead && dateString[*indexOfDateStringIter] != '\0')
	{
		if (areAllNumbers && !isdigit(dateString[*indexOfDateStringIter]))
		{
			break;
		}
		else if (!areAllNumbers && !isalpha(dateString[*indexOfDateStringIter]))
		{
			break;
		}
		dateElement[i] = dateString[*indexOfDateStringIter];
		i++;
		(*indexOfDateStringIter)++;
	}

	/* This will not go out of bound as we allocated max charsToRead with size of terminating char */
	dateElement[i] = '\0';
}


/*
 * This function sets the result for dateFromString operator.
 */
static void
SetResultValueForDateFromString(DollarDateFromPartsBsonValue *dateFromParts,
								ExtensionTimezone timezoneToApply, bson_value_t *result)
{
	/*
	 * Speical handling for when dayOfYear is given as input.
	 * In this case to handle number of days we simply do a simple addition of interval to timestamp.
	 */
	if (dateFromParts->dayOfYear.value_type == BSON_TYPE_INT32)
	{
		SetResultValueForDateFromStringInputDayOfYear(dateFromParts, timezoneToApply,
													  result);
		return;
	}

	/* This 30 represents a fixed length string YYYY-DDD hh:mm:ss.ms */
	char *postgresDateString = (char *) palloc(30 * sizeof(char));
	char *postgresFormatString = (char *) (dateFromParts->isIsoFormat ?
										   DefaultIsoPostgresFormatForDateString :
										   DefaultPostgresFormatForDateString);
	ConstructDateStringFromParts(dateFromParts, postgresDateString);
	Datum timestampFromStringDatum = DirectFunctionCall2(to_timestamp,
														 CStringGetTextDatum(
															 postgresDateString),
														 CStringGetTextDatum(
															 postgresFormatString));
	Datum timestampAdjustedToTimezone =
		GetPgTimestampAdjustedToTimezone(timestampFromStringDatum, timezoneToApply);
	Datum resultEpoch = DirectFunctionCall2(timestamp_part,
											CStringGetTextDatum(EPOCH),
											timestampAdjustedToTimezone);

	result->value.v_datetime = (int64) (DatumGetFloat8(resultEpoch) * 1000);
	result->value_type = BSON_TYPE_DATE_TIME;
}


/*
 * This function computes the result for input when we have dateParts specially dayOfYear.
 * This function makes a timestamp using the given year and then add interval of days, hours, mins, sec to that year to find the result value.
 */
static void
SetResultValueForDateFromStringInputDayOfYear(DollarDateFromPartsBsonValue *dateFromParts,
											  ExtensionTimezone timezoneToApply,
											  bson_value_t *result)
{
	/* Diretly using year as we have check year is required for dayOfyear calculation */
	int32 year = dateFromParts->year.value.v_int32;
	int32 dayOfyear = dateFromParts->dayOfYear.value.v_int32;
	int32 hour = dateFromParts->hour.value_type != BSON_TYPE_EOD ?
				 dateFromParts->hour.value.v_int32 : 0;
	int32 minute = dateFromParts->minute.value_type != BSON_TYPE_EOD ?
				   dateFromParts->minute.value.v_int32 : 0;
	int32 second = dateFromParts->second.value_type != BSON_TYPE_EOD ?
				   dateFromParts->second.value.v_int32 : 0.0;
	int32 millis = dateFromParts->millisecond.value_type != BSON_TYPE_EOD ?
				   dateFromParts->millisecond.value.v_int32 : 0;

	int32 defaultValueForMonth = 1;
	int32 defaultValueForDay = 1;

	float8 secondsAdjustedWithMillis = second + (((float8) millis) /
												 MILLISECONDS_IN_SECOND);
	Datum timestampFromYear = DirectFunctionCall6(make_timestamp,
												  year,
												  defaultValueForMonth,
												  defaultValueForDay,
												  hour,
												  minute,
												  secondsAdjustedWithMillis);
	Datum intervalWithDays = GetIntervalFromDatePart(0, 0, 0, dayOfyear, 0, 0, 0, 0);

	bool isResultOverflow = false;
	Datum timestampWithDaysAdjusted = AddIntervalToTimestampWithPgTry(timestampFromYear,
																	  intervalWithDays,
																	  &isResultOverflow);

	/* This would be highly unlikely as days can be at max 999 but when year value is very high we return max value. */
	if (isResultOverflow)
	{
		result->value_type = BSON_TYPE_DATE_TIME;
		result->value.v_datetime = INT64_MAX;
		return;
	}

	Datum timestampAdjustedToTimezone =
		GetPgTimestampAdjustedToTimezone(timestampWithDaysAdjusted, timezoneToApply);
	Datum resultEpoch = DirectFunctionCall2(timestamp_part,
											CStringGetTextDatum(EPOCH),
											timestampAdjustedToTimezone);

	result->value.v_datetime = (int64) (DatumGetFloat8(resultEpoch) * 1000);
	result->value_type = BSON_TYPE_DATE_TIME;
}


/*
 * This function takes in the format and splits into array of elements.
 * This is required as we need to first understand the format string and then convert it to the postgres format string.
 * This breakdown also would be used to parse dateString char by char.
 */
static void
SplitFormatString(StringView formatStrview, char **elements, int *numElements)
{
	int length = formatStrview.length;
	char *formatString = (char *) formatStrview.string;
	int count = 0;
	int i = 0;

	while (i < length)
	{
		if (formatString[i] == '%')
		{
			if ((i + 1) < length)
			{
				char *formatSpecifier = (char *) palloc0(3 * sizeof(char));
				formatSpecifier[0] = formatString[i];
				formatSpecifier[1] = formatString[i + 1];
				formatSpecifier[2] = '\0';
				elements[count] = formatSpecifier;
				count++;
				i += 2;
			}
			else
			{
				DeepFreeFormatArray(elements, count);
				ereport(ERROR, (errcode(MongoLocation18535), errmsg(
									"Unmatched '%%' at end of format string")));
			}
		}
		else
		{
			char *splitter = (char *) palloc0(2 * sizeof(char));
			splitter[0] = formatString[i];
			splitter[1] = '\0';
			elements[count] = splitter;
			count++;
			i++;
		}
	}
	*numElements = count;
}


/*
 * This function validates the input and sets the isInputValid flag to false if the input is invalid.
 */
static void
ValidateInputForDateFromString(bson_value_t *dateString, bson_value_t *format,
							   bson_value_t *timezone, bson_value_t *onError,
							   DollarDateFromPartsBsonValue *dateFromParts,
							   ExtensionTimezone *timezoneToApply, bool *isInputValid)
{
	bool isOnErrorPresent = onError->value_type != BSON_TYPE_EOD;

	/* If dateString is not a string */
	if (dateString->value_type != BSON_TYPE_UTF8)
	{
		CONDITIONAL_EREPORT(isOnErrorPresent,
							ereport(ERROR, (errcode(MongoConversionFailure),
											errmsg(
												"$dateFromString requires that 'dateString' be a string, found: %s with value %s",
												BsonTypeName(dateString->value_type),
												BsonValueToJsonForLogging(dateString)),
											errhint(
												"$dateFromString requires that 'dateString' be a string, found: %s",
												BsonTypeName(dateString->value_type)))));
		*isInputValid = false;
		return;
	}

	if (format->value_type != BSON_TYPE_UTF8)
	{
		ereport(ERROR, (errcode(MongoLocation40684), errmsg(
							"$dateFromString requires that 'format' be a string, found: %s with value %s",
							BsonTypeName(format->value_type), BsonValueToJsonForLogging(
								format)),
						errhint(
							"$dateFromString requires that 'format' be a string, found: %s",
							BsonTypeName(format->value_type))));
	}

	bool isTimezonePresent = !IsExpressionResultNullOrUndefined(timezone);
	if (isTimezonePresent && timezone->value_type != BSON_TYPE_UTF8)
	{
		ThrowLocation40517Error(timezone->value_type);
	}

	/* setting default value of timezone as we cannot pass timezone when there is timezone in dateString. So , for verification we need to set it to what has been passed. */
	dateFromParts->timezone.value = timezone->value;
	dateFromParts->timezone.value_type = timezone->value_type;

	VerifyAndParseFormatStringForDateString(dateString, format, dateFromParts,
											isInputValid,
											isOnErrorPresent);

	if (!(*isInputValid))
	{
		return;
	}
	StringView timezoneToParse = {
		.string = dateFromParts->timezone.value.v_utf8.str,
		.length = dateFromParts->timezone.value.v_utf8.len
	};

	if (timezoneToParse.length == 0)
	{
		return;
	}
	else
	{
		*timezoneToApply = ParseTimezone(timezoneToParse);
	}
}


/*
 * This function takes in the datepart and extracts the particular values from date String based on indexOfDateStringIter.
 * In case the datePart is isoWeekDateFmt then it sets the isIsoWeekDateFmt flag to true.
 * In case of any error in validating it also changes the isInputValid and sets it to false instead of throwing error.
 */
static void
ValidateDatePartFromDateString(int indexOfDateFormatMap, char *dateString, int
							   dateStringLen, int *indexOfDateStringIter,
							   DollarDateFromPartsBsonValue *dateFromParts,
							   bool *isInputValid, int *isIsoWeekDateFmt, bool
							   isOnErrorPresent)
{
	DatePart datePart = dateFormats[indexOfDateFormatMap].datePart;

	int minCharsToRead = dateFormats[indexOfDateFormatMap].minLen;
	int maxCharsToRead = dateFormats[indexOfDateFormatMap].maxLen;
	int minRangeVal = dateFormats[indexOfDateFormatMap].minRangeValue;
	int maxRangeVal = dateFormats[indexOfDateFormatMap].maxRangeValue;
	char *dateElement = (char *) palloc0((maxCharsToRead + 1) * sizeof(char));

	/* When Str parts are supplied then areAllNumbers are false otherwise true. */
	bool areAllNumbers = !(datePart == DatePart_Str_Month || datePart ==
						   DatePart_Str_Month_Abbr);
	ReadThroughDatePart(dateString, indexOfDateStringIter, maxCharsToRead, dateElement,
						areAllNumbers);
	int lenDateElement = strlen(dateElement);

	/*
	 * Check if the length of the dateElement is less than the minimum characters required to read.
	 * We, don't error out as since during parsing we will get the error.
	 */
	if (lenDateElement < minCharsToRead)
	{
		pfree(dateElement);
		*isInputValid = false;
		return;
	}

	bson_value_t dateElementBsonValue = { .value_type = BSON_TYPE_INT32 };

	/* In case all parts are numbers in mongo format then we can parse them upront*/
	if (areAllNumbers)
	{
		/* Converting to integer and checking if the value is in the range. */
		int dateElementValue = atoi(dateElement);
		pfree(dateElement);

		if (dateElementValue < minRangeVal || dateElementValue > maxRangeVal)
		{
			CONDITIONAL_EREPORT(isOnErrorPresent, ereport(ERROR, (errcode(
																	  MongoConversionFailure),
																  errmsg(
																	  "Error parsing date string '%s'; %d: Value %d is out of range",
																	  dateString,
																	  *
																	  indexOfDateStringIter,
																	  dateElementValue),
																  errhint(
																	  "Error parsing date string. Value %d is out of range",
																	  dateElementValue))));
			*isInputValid = false;
			return;
		}
		dateElementBsonValue.value.v_int32 = dateElementValue;
		dateElementBsonValue.value_type = BSON_TYPE_INT32;
	}

	switch (datePart)
	{
		case DatePart_Year:
		{
			*isIsoWeekDateFmt = 0;
			dateFromParts->year = dateElementBsonValue;
			break;
		}

		case DatePart_Month:
		{
			*isIsoWeekDateFmt = 0;
			dateFromParts->month = dateElementBsonValue;
			break;
		}

		case DatePart_DayOfMonth:
		{
			*isIsoWeekDateFmt = 0;
			dateFromParts->day = dateElementBsonValue;
			break;
		}

		case DatePart_Hour:
		{
			dateFromParts->hour = dateElementBsonValue;
			break;
		}

		case DatePart_Minute:
		{
			dateFromParts->minute = dateElementBsonValue;
			break;
		}

		case DatePart_Second:
		{
			dateFromParts->second = dateElementBsonValue;
			break;
		}

		case DatePart_Millisecond:
		{
			dateFromParts->millisecond = dateElementBsonValue;
			break;
		}

		case DatePart_IsoWeek:
		{
			*isIsoWeekDateFmt = 1;
			dateFromParts->isoWeek = dateElementBsonValue;
			break;
		}

		case DatePart_IsoWeekYear:
		{
			*isIsoWeekDateFmt = 1;
			dateFromParts->isoWeekYear = dateElementBsonValue;
			break;
		}

		case DatePart_IsoDayOfWeek:
		{
			*isIsoWeekDateFmt = 1;
			dateFromParts->isoDayOfWeek = dateElementBsonValue;
			break;
		}

		case DatePart_DayOfYear:
		{
			*isIsoWeekDateFmt = 0;
			dateFromParts->dayOfYear = dateElementBsonValue;
			break;
		}

		case DatePart_Str_Month:
		case DatePart_Str_Month_Abbr:
		{
			*isIsoWeekDateFmt = 0;
			bool isAbbreviated = datePart == DatePart_Str_Month_Abbr;
			int month = GetMonthIndexFromString(dateElement, isAbbreviated);
			pfree(dateElement);

			/* In case the name of month is wrong we return error.*/
			if (month == -1)
			{
				CONDITIONAL_EREPORT(isOnErrorPresent, ereport(ERROR, (errcode(
																		  MongoConversionFailure),
																	  errmsg(
																		  "Error parsing date string '%s'; %d: Textual month cannot be found",
																		  dateString,
																		  *
																		  indexOfDateStringIter),
																	  errhint(
																		  "Error parsing date string. Textual month cannot be found for input month"))));
				*isInputValid = false;
				return;
			}
			dateElementBsonValue.value.v_int32 = month;
			dateFromParts->month = dateElementBsonValue;
			break;
		}

		default:
		{
			/* Invalid date part */
			*isInputValid = false;
			break;
		}
	}
}


/* This is a small utility function which given the string name of month returns it's index.
 * This function compares string ignoring case.
 */
static inline int
GetMonthIndexFromString(char *monthName, bool isAbbreviated)
{
	int numMonths = sizeof(monthNamesFull) / sizeof(monthNamesFull[0]);

	for (int index = 0; index < numMonths; index++)
	{
		if (isAbbreviated)
		{
			/* Compare only the first 3 characters, ignoring case */
			if (strncasecmp(monthNamesFull[index], monthName, 3) == 0)
			{
				return index + 1;
			}
		}
		else
		{
			/* Compare the full month name, ignoring case */
			if (strcasecmp(monthNamesFull[index], monthName) == 0)
			{
				return index + 1;
			}
		}
	}
	return -1;
}


/*
 * This function validates the timezone minutes offset in dateString and sets the value in dateFromParts.
 * It is of the format +/-mmm
 */
static void
ValidateOffsetMinutes(char *dateString, int sizeOfDateString, int *indexOfDateStringIter,
					  DollarDateFromPartsBsonValue *dateFromParts, bool *isInputValid,
					  bool isOnErrorPresent)
{
	if ((dateString[*indexOfDateStringIter] != '+') &&
		(dateString[*indexOfDateStringIter] != '-'))
	{
		CONDITIONAL_EREPORT(isOnErrorPresent,
							ereport(ERROR, (errcode(MongoConversionFailure),
											errmsg(
												"Error parsing date string '%s'; %d: Invalid timezone offset in minutes '%c'",
												dateString,
												sizeOfDateString,
												dateString[*indexOfDateStringIter]),
											errhint(
												"Error parsing date string. Invalid timezone offset in minutes '%c'",
												dateString[*indexOfDateStringIter]))));
		*isInputValid = false;
		return;
	}

	char sign = dateString[*indexOfDateStringIter];

	/* Incrementing as 1 char +/- has been read. */
	(*indexOfDateStringIter)++;

	/* todo : optimise if this can be better */
	char *timezoneOffset = (char *) palloc0(sizeOfDateString * sizeof(char));

	bool areAllNumbers = true;
	ReadThroughDatePart(dateString, indexOfDateStringIter, sizeOfDateString,
						timezoneOffset, areAllNumbers);

	if (dateFromParts->timezone.value_type != BSON_TYPE_EOD)
	{
		CONDITIONAL_EREPORT(isOnErrorPresent, ereport(ERROR, (errcode(
																  MongoConversionFailure),
															  errmsg(
																  "you cannot pass in a date/time string with GMT offset together with a timezone argument"))));

		*isInputValid = false;
		pfree(timezoneOffset);
		return;
	}

	/* Pointer to store the address of the first character after the converted number */
	char *endptr;

	/* Converting to int */
	long totalMins = strtol(timezoneOffset, &endptr, 10);

	if (*endptr != '\0')
	{
		ereport(ERROR, (errcode(MongoConversionFailure), errmsg(
							"Out of range Utc minutes offset provided for $dateFromString")));
	}

	/* Since we now support only hh:mm format for utc offset parsing so converting to this format and max mins are 99 hours and 99 mins in this format. */
	if (totalMins > MaxMinsRepresentedByUTCOffset)
	{
		ereport(ERROR, (errcode(MongoConversionFailure), errmsg(
							"UTC Minutes Offset provided cannot be parsed into UTC offset form for $dateFromString")));
	}
	int hours = totalMins / 60;
	int minutes = totalMins % 60;

	char hoursAndMinsFmt[7];
	sprintf(hoursAndMinsFmt, "%c%02d:%02d", sign, hours, minutes);

	dateFromParts->timezone.value_type = BSON_TYPE_UTF8;
	dateFromParts->timezone.value.v_utf8.str = hoursAndMinsFmt;
	dateFromParts->timezone.value.v_utf8.len = strlen(hoursAndMinsFmt);

	pfree(timezoneOffset);
}


/*
 * This function validates the timezone offset in dateString and sets the value in dateFromParts.
 * It is of the format +/-hhmm or TimeZone +/-hh:mm like UTC+05:30 or +0530
 */
static void
ValidateTimezoneOffsetForDateString(char *dateString, int sizeOfDateString,
									int *indexOfDateStringIter,
									DollarDateFromPartsBsonValue *dateFromParts,
									bool *isInputValid, bool isOnErrorPresent)
{
	/* Space indicates that dateString should be of format like 'UTC+05:30' or  'UTC'. All timezones are added with space. */
	if (isalpha(dateString[*indexOfDateStringIter]))
	{
		/* for UTC+05:30 or -05:30 Taking max len as 25 as it cannot be more than this number in length '/0' */
		char *timezoneOffset = (char *) palloc0(25 * sizeof(char));
		int timezoneOffsetLen = 0;

		/* This will be used to tell if utc offset is present, This is to throw different errors for each. */
		bool hasUTCOffset = false;

		/* Copying the timezone identifier */
		while (isalpha(dateString[*indexOfDateStringIter]))
		{
			timezoneOffset[timezoneOffsetLen++] = dateString[(*indexOfDateStringIter)++];
		}

		if ((timezoneOffsetLen > 0) && (*indexOfDateStringIter < sizeOfDateString) &&
			(dateString[*indexOfDateStringIter] == '+' ||
			 dateString[*indexOfDateStringIter] == '-'))
		{
			/* sign is present either +/-. */
			hasUTCOffset = true;

			/* Check if the extracted offset is "GMT" */
			if (strcmp(timezoneOffset, "GMT") != 0)
			{
				/* If not "GMT", throw an error */
				CONDITIONAL_EREPORT(isOnErrorPresent,
									ThrowMongoConversionErrorForTimezoneIdentifier(
										dateString,
										sizeOfDateString,
										indexOfDateStringIter));
				*isInputValid = false;
				return;
			}
			char sign = dateString[(*indexOfDateStringIter)++];

			/* reverse the sign as for timezone calculations we have to adjust for postgres. */
			timezoneOffset[timezoneOffsetLen++] = ((sign == '+') ? '-' : '+');

			/* If "GMT" is specified, continue processing the offset */
			ParseUtcOffsetForDateString(dateString, sizeOfDateString,
										indexOfDateStringIter,
										timezoneOffset, &timezoneOffsetLen,
										isOnErrorPresent,
										isInputValid);
			if (!(*isInputValid))
			{
				return;
			}
		}
		else
		{
			/* Check if the timezone identifier is not empty */
			if (timezoneOffsetLen < 0)
			{
				CONDITIONAL_EREPORT(isOnErrorPresent,
									ThrowMongoConversionErrorForTimezoneIdentifier(
										dateString,
										sizeOfDateString,
										indexOfDateStringIter));
				*isInputValid = false;
				return;
			}
			else
			{
				timezoneOffset[timezoneOffsetLen] = '\0';
			}
		}
		if (dateFromParts->timezone.value_type != BSON_TYPE_EOD)
		{
			/* For error to cx replace the sign negation. */
			if (hasUTCOffset)
			{
				CONDITIONAL_EREPORT(isOnErrorPresent, ereport(ERROR, (errcode(
																		  MongoConversionFailure),
																	  errmsg(
																		  "you cannot pass in a date/time string with GMT offset together with a timezone argument"))));
			}
			else
			{
				CONDITIONAL_EREPORT(isOnErrorPresent, ereport(ERROR, (errcode(
																		  MongoConversionFailure),
																	  errmsg(
																		  "you cannot pass in a date/time string with time zone information ('%s') together with a timezone argument",
																		  timezoneOffset),
																	  errhint(
																		  "you cannot pass in a date/time string with time zone information together with a timezone argument"))));
			}

			*isInputValid = false;
			return;
		}
		dateFromParts->timezone.value_type = BSON_TYPE_UTF8;
		dateFromParts->timezone.value.v_utf8.str = timezoneOffset;
		dateFromParts->timezone.value.v_utf8.len = timezoneOffsetLen + 1;
		return;
	}
	else if ((dateString[*indexOfDateStringIter] == '+') ||
			 (dateString[*indexOfDateStringIter] == '-'))
	{
		/* for +0530 or -05:30 max len is 6 and 1 for '/0' */
		char timezoneOffset[7];
		int timezoneOffsetLen = 0;

		/* Copying the sign */
		timezoneOffset[timezoneOffsetLen++] = dateString[(*indexOfDateStringIter)++];

		/* Building rest of utc offset */
		ParseUtcOffsetForDateString(dateString, sizeOfDateString, indexOfDateStringIter,
									timezoneOffset, &timezoneOffsetLen, isOnErrorPresent,
									isInputValid);
		if (!(*isInputValid))
		{
			return;
		}

		if (dateFromParts->timezone.value_type != BSON_TYPE_EOD)
		{
			CONDITIONAL_EREPORT(isOnErrorPresent, ereport(ERROR, (errcode(
																	  MongoConversionFailure),
																  errmsg(
																	  "you cannot pass in a date/time string with GMT offset together with a timezone argument"))));
			*isInputValid = false;
			return;
		}
		dateFromParts->timezone.value_type = BSON_TYPE_UTF8;
		dateFromParts->timezone.value.v_utf8.str = timezoneOffset;
		dateFromParts->timezone.value.v_utf8.len = timezoneOffsetLen;
		return;
	}
	else
	{
		CONDITIONAL_EREPORT(isOnErrorPresent,
							ThrowMongoConversionErrorForTimezoneIdentifier(dateString,
																		   sizeOfDateString,
																		   indexOfDateStringIter));
		*isInputValid = false;
		return;
	}
}


/*
 * This function parses the given dateString , format and validates the respective values.
 * Post parsing sets the pgFormatString which will be used to specify the format for the date string conversion.
 */
static void
VerifyAndParseFormatStringForDateString(bson_value_t *dateString, bson_value_t *format,
										DollarDateFromPartsBsonValue *dateFromParts,
										bool *isInputValid, bool isOnErrorPresent)
{
	StringView formatStr = {
		.string = format->value.v_utf8.str,
		.length = format->value.v_utf8.len
	};
	StringView dateStringView = {
		.string = dateString->value.v_utf8.str,
		.length = dateString->value.v_utf8.len
	};

	int sizeOfSplitFormat = 0;
	char **elements = palloc0(formatStr.length * sizeof(char *));
	SplitFormatString(formatStr, elements, &sizeOfSplitFormat);

	ParseDateStringWithFormat(dateStringView, elements, sizeOfSplitFormat, dateFromParts,
							  isInputValid, isOnErrorPresent);

	DeepFreeFormatArray(elements, sizeOfSplitFormat);
}
