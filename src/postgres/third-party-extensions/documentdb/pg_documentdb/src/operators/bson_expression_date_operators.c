/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/operators/bson_expression_date_operators.c
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

#include "io/bson_core.h"
#include "operators/bson_expression.h"
#include "operators/bson_expression_operators.h"
#include "utils/date_utils.h"
#include "utils/documentdb_errors.h"
#include "utils/fmgrprotos.h"
#include "utils/timestamp.h"
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

/* This flag is used while parsing as we cannot supply timezone in string and in timezone field together. */
#define FLAG_TIMEZONE_SPECIFIED (1 << 0)

/* This flag is used to handle the case format is not specified and use preset formats to parse */
#define FLAG_FORMAT_SPECIFIED (1 << 1)

/* This flag is used to handle the case when onError is specified but not a normal value */
#define FLAG_ON_ERROR_SPECIFIED (1 << 2)

/* This flag is used to handle the case when onNull is specified*/
#define FLAG_ON_NULL_SPECIFIED (1 << 3)

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
	DatePart_Str_Month_Abbr = 14,
	DatePart_Str_Timezone_Offset = 15,
	DatePart_Str_Minutes_Offset = 16,
	DataPart_Placeholder_Percent = 17,
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

	uint8_t flags;
} DollarDateFromStringArgumentState;

/* forward declaration so that it can be a parameter of ValidateAndParseDatePartFunc */
struct DateFormatMap;

typedef bool (*ValidateAndParseDatePartFunc)(char *rawString, const struct
											 DateFormatMap *dateFormatMap,
											 DollarDateFromPartsBsonValue *dateFromParts);

/* This is a struct for defining a map between postgres and mongo format and it's length in string and range of acceptible values. */
typedef struct DateFormatMap
{
	/* Mongo format specifier */
	const char *mongoFormat;

	/* Is the format an ISO format */
	const bool isIsoFormat;

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

	/* Func to validate and parse date part */
	ValidateAndParseDatePartFunc validateAndParseFunc;
} DateFormatMap;

/* This is a struct for storing the splitted date part which contains raw string and its DateFormatMap */
typedef struct RawDatePart
{
	/* the DateFormatMap for this date part */
	const DateFormatMap *formatMap;

	/* the raw string for this date part */
	char *rawString;
} RawDatePart;

/* date string parser for a specific format representation */
typedef struct DateFormatParser
{
	/* format represented in string */
	char *format;

	/* min length allowed by this format parser */
	int minLen;

	/* max length allowed by this format parser */
	int maxLen;
} DateFormatParser;

/* a mapping struct from abbreviation to offset for timezone */
typedef struct TimezoneMap
{
	/* timezone abbreviation */
	char *abbreviation;

	/* timezone offset for this abbreviation represented in [+/-][hh]:[mm]*/
	char *offset;
} TimezoneMap;

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

/* This is a static preset mapping for timezone offset to timezone abbreviation. */
const TimezoneMap timezoneMap[] = {
	{ "A", "+01:00" }, { "B", "+02:00" }, { "BST", "+01:00" }, { "C", "+03:00" },
	{ "CEST", "+02:00" }, { "CET", "+01:00" }, { "D", "+04:00" }, { "E", "+05:00" },
	{ "EST", "-05:00" }, { "F", "+06:00" }, { "G", "+07:00" }, { "GMT", "+00:00" },
	{ "H", "+08:00" }, { "I", "+09:00" }, { "K", "+10:00" }, { "L", "+11:00" },
	{ "M", "+12:00" }, { "N", "-01:00" }, { "O", "-02:00" }, { "P", "-03:00" },
	{ "PDT", "-07:00" }, { "PST", "-08:00" }, { "Q", "-04:00" }, { "R", "-05:00" },
	{ "S", "-06:00" }, { "T", "-07:00" }, { "U", "-08:00" }, { "UTC", "+00:00" },
	{ "V", "-09:00" }, { "W", "-10:00" }, { "X", "-11:00" }, { "Y", "-12:00" },
	{ "Z", "+00:00" }
};

/* default preset supported format for the case that format is not specified */
const DateFormatParser presetDateFormatParser[] = {
	{ "%Y-%m-%dT%H:%M:%S.%LZ", 22, 24 },

	{ "%Y-%m-%dT%H:%M:%S.%L", 21, 23 },
	{ "%Y-%m-%dT%H:%M:%S", 19, 19 },
	{ "%Y-%m-%dT%H:%M.%S.%LZ", 22, 24 },

	{ "%Y-%m-%dT%H:%M.%S.%L", 21, 23 },
	{ "%Y-%m-%dT%H:%M.%S", 19, 19 },

	{ "%Y-%m-%d %H:%M:%S", 19, 19 },
	{ "%Y-%m-%d", 10, 10 },

	{ "%Y-%m-%dT%H:%M:%S.%L%z", 23, 33 },
	{ "%Y-%m-%dT%H:%M.%S%z", 20, 26 },
	{ "%Y-%m-%dT%H:%M:%S%z", 20, 29 },

	/* Wacky formats */
	{ "%B %D, %Y", 13, 20 },
	{ "%B %D, %Y %H:%M:%S%z", 24, 39 },
	{ "%B %D, %Y %h", 17, 25 },
	{ "%m/%d/%y", 6, 10 },
	{ "%d-%m-%Y", 10, 10 },
	{ "%Y-%b-%d %h", 15, 16 },
	{ "%Y-%m-%d %H:%M:%S%z", 21, 29 },

	/* invalid formats */
	{ "%B %D", 7, 16 },
	{ "%H:%M:%S", 8, 8 },

	/* last format which is used to trigger error */
	{ "%Y-%m-%dT%H:%M:%S.%LZ", 0, 99 },
};

static const char *IsoDateFormat = "IYYY-IW-ID";
static const char *DefaultTimezone = "UTC";

static const char *DefaultPostgresFormatForDateString = "YYYY-MM-DD HH24:MI:SS.MS";
static const char *DefaultIsoPostgresFormatForDateString = "IYYY-IW-ID HH24:MI:SS.MS";

static const int QuartersPerYear = 4;
static const int MonthsPerQuarter = 3;
static const int MaxMinsRepresentedByUTCOffset = 6039;

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */
static Datum AddIntervalToTimestampWithPgTry(Datum timestamp, Datum interval,
											 bool *isResultOverflow);
static inline bool CheckFlag(uint8_t flags, uint8_t flag);
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
												   uint8_t flags);
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
static void ParseDatePartOperator(const bson_value_t *argument,
								  const char *operatorName,
								  DatePart datePart, AggregationExpressionData *data,
								  ParseAggregationExpressionContext *context);
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
										uint8_t *flags);
static void ParseInputForDollarDateAddSubtract(const bson_value_t *inputArgument,
											   char *opName,
											   bson_value_t *startDate,
											   bson_value_t *unit,
											   bson_value_t *amount,
											   bson_value_t *timezone);
static ExtensionTimezone ParseTimezone(StringView timezone);
static int64_t ParseUtcOffset(StringView offset);
static inline void ParseUtcOffsetForDateString(char *dateString, int sizeOfDateString,
											   int *indexOfDateStringIter,
											   char *timezoneOffset,
											   int *timezoneOffsetLen, bool
											   isOnErrorPresent, bool *isInputValid);
static void HandlePreParsedDatePartOperator(pgbson *doc, void *arguments, const
											char *operatorName, DatePart datePart,
											ExpressionResult *expressionResult);
static void ProcessDollarDateToString(const bson_value_t *dateValue, ExtensionTimezone
									  timezoneToApply, StringView formatString,
									  bson_value_t *result);
static void ProcessDollarDateToParts(const bson_value_t *dateValue, bool isIsoRequested,
									 ExtensionTimezone timezoneToApply,
									 bson_value_t *result);
static int32_t DetermineUtcOffsetForEpochWithTimezone(int64_t unixEpoch,
													  ExtensionTimezone timezone);
static void GetDatePartResult(bson_value_t *dateValue, ExtensionTimezone timezone,
							  DatePart datePart,
							  bson_value_t *result);
static bool GetTimezoneToApply(const bson_value_t *timezoneValue, const
							   char *operatorName, ExtensionTimezone *timezoneToApply);
static bool GetIsIsoRequested(bson_value_t *isoValue, bool *isIsoRequested);
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
static inline int GetCurrentCentury();
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
static bool TryParseTwoDigitNumber(StringView str, uint32_t *result);
static Datum TruncateTimestampToPrecision(Datum timestamp, const char *precisionUnit);
static void ValidateArgumentsForDateTrunc(bson_value_t *binSize, bson_value_t *date,
										  bson_value_t *startOfWeek,
										  bson_value_t *timezone, bson_value_t *unit,
										  DateTruncUnit *dateTruncUnitEnum,
										  WeekDay *weekDay,
										  ExtensionTimezone *timezoneToApply,
										  ExtensionTimezone resultTimezone);
static void ValidateInputArgumentForDateDiff(bson_value_t *startDate,
											 bson_value_t *endDate,
											 bson_value_t *unit, bson_value_t *timezone,
											 bson_value_t *startOfWeek,
											 DateUnit *dateUnitEnum,
											 WeekDay *weekDayEnum);
static void ValidateInputForDateFromString(bson_value_t *dateString,
										   bson_value_t *format,
										   bson_value_t *timezone,
										   bson_value_t *onError,
										   uint8_t flags,
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
static void VerifyAndParseFormatStringToParts(bson_value_t *dateString,
											  char *format,
											  DollarDateFromPartsBsonValue *dateFromParts,
											  bool *isInputValid,
											  bool isOnErrorPresent,
											  bool tryWithPresetFormat);
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

/* These methods are used for validating and parsing date part */
static bool ValidateAndParseDigits(char *rawString, const DateFormatMap *map,
								   DollarDateFromPartsBsonValue *dateFromParts);
static bool ValidateAndParseStrMonth(char *rawString, const DateFormatMap *map,
									 DollarDateFromPartsBsonValue *dateFromParts);
static bool ValidateAndParsePlaceholderPercent(char *rawString, const DateFormatMap *map,
											   DollarDateFromPartsBsonValue *dateFromParts);
static bool ValidateAndParseAbbrStrMonth(char *rawString, const DateFormatMap *map,
										 DollarDateFromPartsBsonValue *dateFromParts);
static bool ValidateAndParseStrHour(char *rawString, const DateFormatMap *map,
									DollarDateFromPartsBsonValue *dateFromParts);
static bool ValidateAndParseMinOffset(char *rawString, const DateFormatMap *map,
									  DollarDateFromPartsBsonValue *dateFromParts);
static bool ValidateAndParseTwoDigitsYear(char *rawString, const DateFormatMap *map,
										  DollarDateFromPartsBsonValue *dateFromParts);
static bool ValidateAndParseStrDayOfMonth(char *rawString, const DateFormatMap *map,
										  DollarDateFromPartsBsonValue *dateFromParts);
static bool ValidateAndParseTimezoneOffset(char *rawString, const DateFormatMap *map,
										   DollarDateFromPartsBsonValue *dateFromParts);

/* static mapping for mongo format specifiers to postgres format specifiers. These are sorted on mongo format specifier(index 0) and should be kept sorted */
static const DateFormatMap dateFormats[] = {
	{ "%%", false, "%", DataPart_Placeholder_Percent, 1, 1, -1, -1,
	  ValidateAndParsePlaceholderPercent },
	{ "%B", false, "month", DatePart_Str_Month, 4, 9, -1, -1, ValidateAndParseStrMonth },

	/* customized day represented format like 1st, 2nd, 3rd, 10th and etc. */
	{ "%D", false, "DD", DatePart_DayOfMonth, 3, 4, -1, -1,
	  ValidateAndParseStrDayOfMonth },
	{ "%G", true, "IYYY", DatePart_IsoWeekYear, 1, 4, 0, 9999, ValidateAndParseDigits },
	{ "%H", false, "HH24", DatePart_Hour, 2, 2, 0, 23, ValidateAndParseDigits },
	{ "%L", false, "MS", DatePart_Millisecond, 1, 3, 0, 999, ValidateAndParseDigits },
	{ "%M", false, "MI", DatePart_Minute, 2, 2, 0, 59, ValidateAndParseDigits },
	{ "%S", false, "SS", DatePart_Second, 2, 2, 0, 59, ValidateAndParseDigits },
	{ "%V", true, "IW", DatePart_IsoWeek, 1, 2, 1, 53, ValidateAndParseDigits },
	{ "%Y", false, "YYYY", DatePart_Year, 1, 4, 0, 9999, ValidateAndParseDigits },
	{ "%Z", false, "TZ", DatePart_Str_Timezone_Offset, 1, 4, -1, -1,
	  ValidateAndParseMinOffset },
	{ "%b", false, "mon", DatePart_Str_Month_Abbr, 3, 3, -1, -1,
	  ValidateAndParseAbbrStrMonth },
	{ "%d", false, "DD", DatePart_DayOfMonth, 1, 2, 1, 31, ValidateAndParseDigits },

	/* customized parser for string represented hour like 10am, 11pm, noon */
	{ "%h", false, "HH12", DatePart_Hour, 3, 4, -1, -1, ValidateAndParseStrHour },
	{ "%j", false, "DDD", DatePart_DayOfYear, 1, 3, 1, 999, ValidateAndParseDigits },
	{ "%m", false, "MM", DatePart_Month, 1, 2, 1, 12, ValidateAndParseDigits },
	{ "%u", true, "ID", DatePart_IsoDayOfWeek, 1, 1, 1, 7, ValidateAndParseDigits },

	/* customized parser for 2-digits represented year */
	{ "%y", false, "YYYY", DatePart_Year, 2, 2, 0, 99, ValidateAndParseTwoDigitsYear },
	{ "%z", false, "TZ", DatePart_Str_Timezone_Offset, 1, 30, -1, -1,
	  ValidateAndParseTimezoneOffset },
};

/* Helper method that throws the error for an invalid timezone argument. */
static inline void
pg_attribute_noreturn()
ThrowInvalidTimezoneIdentifier(const char * identifier)
{
	ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40485), errmsg(
						"unrecognized time zone identifier: \"%s\"", identifier)));
}

/* Helper method that throws common Location40517 when a string is not provided for the timezone argument. */
static inline void
pg_attribute_noreturn()
ThrowLocation40517Error(bson_type_t foundType)
{
	ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40517), errmsg(
						"timezone must evaluate to a string, found %s",
						BsonTypeName(foundType)),
					errdetail_log("timezone must evaluate to a string, found %s",
								  BsonTypeName(foundType))));
}


/*
 * This is a helper method to throw error when unit does not matches the type of utf8 for date
 */
static inline void
pg_attribute_noreturn() ThrowLocation5439013Error(bson_type_t foundType, char * opName)
{
	ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5439013), errmsg(
						"%s requires 'unit' to be a string, but got %s",
						opName, BsonTypeName(foundType)),
					errdetail_log("%s requires 'unit' to be a string, but got %s",
								  opName, BsonTypeName(foundType))));
}

/*
 * This is a helper method to throw error when startOfWeek does not matches the type of utf8 for date
 */
static inline void
pg_attribute_noreturn() ThrowLocation5439015Error(bson_type_t foundType, char * opName)
{
	ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5439015), errmsg(
						"%s requires 'startOfWeek' to be a string, but got %s",
						opName, BsonTypeName(foundType)),
					errdetail_log("%s requires 'startOfWeek' to be a string, but got %s",
								  opName, BsonTypeName(foundType))));
}

/* Helper method that throws common ConversionFailure when a timezone string is not parseable by format. */
static inline void
pg_attribute_noreturn() ThrowMongoConversionErrorForTimezoneIdentifier(
	char * dateString,
	int sizeOfDateString,
	int * indexOfDateStringIter)
{
	ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_CONVERSIONFAILURE), errmsg(
						"Error parsing date string '%s'; %d: passing a time zone identifier as part of the string is not allowed '%c'",
						dateString, sizeOfDateString, dateString[*indexOfDateStringIter]),
					errdetail_log(
						"Error parsing date string. passing a time zone identifier as part of the string is not allowed '%c'",
						dateString[*indexOfDateStringIter])));
}

/* Helper that validates a date value is in the valid range 0-9999. */
static inline void
ValidateDateValueIsInRange(uint32_t value)
{
	if (value > 9999)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION18537), errmsg(
							"Could not convert date to string: date component was outside the supported range of 0-9999: %d",
							value),
						errdetail_log(
							"Could not convert date to string: date component was outside the supported range of 0-9999: %d",
							value)));
	}
}


/*
 * Parses a $hour expression.
 * $hour is expressed as { "$hour": <dateExpression> }
 * or { "$hour": { date: <dateExpression>, timezone: <tzExpression> } }
 * We evaluate the inner expression and then return the hour part in the specified date expression with the specified timezone.
 */
void
ParseDollarHour(const bson_value_t *argument, AggregationExpressionData *data,
				ParseAggregationExpressionContext *context)
{
	ParseDatePartOperator(argument, "$hour", DatePart_Hour, data, context);
}


/*
 * Handles executing a pre-parsed $hour expression.
 */
void
HandlePreParsedDollarHour(pgbson *doc, void *arguments,
						  ExpressionResult *expressionResult)
{
	HandlePreParsedDatePartOperator(doc, arguments, "$hour", DatePart_Hour,
									expressionResult);
}


/*
 * Parses a $minute expression.
 * $minute is expressed as { "$minute": <dateExpression> }
 * or { "$minute": { date: <dateExpression>, timezone: <tzExpression> } }
 * We evaluate the inner expression and then return the minute part in the specified date expression with the specified timezone.
 */
void
ParseDollarMinute(const bson_value_t *argument, AggregationExpressionData *data,
				  ParseAggregationExpressionContext *context)
{
	ParseDatePartOperator(argument, "$minute", DatePart_Minute, data, context);
}


/*
 * Handles executing a pre-parsed $minute expression.
 */
void
HandlePreParsedDollarMinute(pgbson *doc, void *arguments,
							ExpressionResult *expressionResult)
{
	HandlePreParsedDatePartOperator(doc, arguments, "$minute", DatePart_Minute,
									expressionResult);
}


/*
 * Parses a $second expression.
 * $second is expressed as { "$second": <dateExpression> }
 * or { "$second": { date: <dateExpression>, timezone: <tzExpression> } }
 * We evaluate the inner expression and then return the second part in the specified date expression with the specified timezone.
 */
void
ParseDollarSecond(const bson_value_t *argument,
				  AggregationExpressionData *data,
				  ParseAggregationExpressionContext *context)
{
	ParseDatePartOperator(argument, "$second", DatePart_Second, data, context);
}


/*
 * Handles executing a pre-parsed $second expression.
 */
void
HandlePreParsedDollarSecond(pgbson *doc, void *arguments,
							ExpressionResult *expressionResult)
{
	HandlePreParsedDatePartOperator(doc, arguments, "$second", DatePart_Second,
									expressionResult);
}


/*
 * Parses a $millisecond expression.
 * $millisecond is expressed as { "$millisecond": <dateExpression> }
 * or { "$millisecond": { date: <dateExpression>, timezone: <tzExpression> } }
 * We evaluate the inner expression and then return the millisecond part in the specified date expression with the specified timezone.
 */
void
ParseDollarMillisecond(const bson_value_t *argument, AggregationExpressionData *data,
					   ParseAggregationExpressionContext *context)
{
	ParseDatePartOperator(argument, "$millisecond", DatePart_Millisecond, data, context);
}


/*
 * Handles executing a pre-parsed $millisecond expression.
 */
void
HandlePreParsedDollarMillisecond(pgbson *doc, void *arguments,
								 ExpressionResult *expressionResult)
{
	HandlePreParsedDatePartOperator(doc, arguments, "$millisecond", DatePart_Millisecond,
									expressionResult);
}


/*
 * Parses a $year expression.
 * $year is expressed as { "$year": <dateExpression> }
 * or { "$year": { date: <dateExpression>, timezone: <tzExpression> } }
 * We evaluate the inner expression and then return the year part in the specified date expression with the specified timezone.
 */
void
ParseDollarYear(const bson_value_t *argument, AggregationExpressionData *data,
				ParseAggregationExpressionContext *context)
{
	ParseDatePartOperator(argument, "$year", DatePart_Year, data, context);
}


/*
 * Handles executing a pre-parsed $year expression.
 */
void
HandlePreParsedDollarYear(pgbson *doc, void *arguments,
						  ExpressionResult *expressionResult)
{
	HandlePreParsedDatePartOperator(doc, arguments, "$year", DatePart_Year,
									expressionResult);
}


/*
 * Parses a $month expression.
 * $month is expressed as { "$month": <dateExpression> }
 * or { "$month": { date: <dateExpression>, timezone: <tzExpression> } }
 * We evaluate the inner expression and then return the month part in the specified date expression with the specified timezone.
 */
void
ParseDollarMonth(const bson_value_t *argument, AggregationExpressionData *data,
				 ParseAggregationExpressionContext *context)
{
	ParseDatePartOperator(argument, "$month", DatePart_Month, data, context);
}


/*
 * Handles executing a pre-parsed $month expression.
 */
void
HandlePreParsedDollarMonth(pgbson *doc, void *arguments,
						   ExpressionResult *expressionResult)
{
	HandlePreParsedDatePartOperator(doc, arguments, "$month", DatePart_Month,
									expressionResult);
}


/*
 * Parses a $week expression.
 * $week is expressed as { "$week": <dateExpression> }
 * or { "$week": { date: <dateExpression>, timezone: <tzExpression> } }
 * We evaluate the inner expression and then return the week number [0-53 (leap year)] in the specified date expression with the specified timezone.
 */
void
ParseDollarWeek(const bson_value_t *argument, AggregationExpressionData *data,
				ParseAggregationExpressionContext *context)
{
	ParseDatePartOperator(argument, "$week", DatePart_Week, data, context);
}


/*
 * Handles executing a pre-parsed $week expression.
 */
void
HandlePreParsedDollarWeek(pgbson *doc, void *arguments,
						  ExpressionResult *expressionResult)
{
	HandlePreParsedDatePartOperator(doc, arguments, "$week", DatePart_Week,
									expressionResult);
}


/*
 * Parses a $dayOfYear expression.
 * $dayOfYear is expressed as { "$dayOfYear": <dateExpression> }
 * or { "$dayOfYear": { date: <dateExpression>, timezone: <tzExpression> } }
 * We evaluate the inner expression and then return the day of the year [1-366 (leap year)] in the specified date expression with the specified timezone.
 */
void
ParseDollarDayOfYear(const bson_value_t *argument, AggregationExpressionData *data,
					 ParseAggregationExpressionContext *context)
{
	ParseDatePartOperator(argument, "$dayOfYear", DatePart_DayOfYear, data, context);
}


/*
 * Handles executing a pre-parsed $dayOfYear expression.
 */
void
HandlePreParsedDollarDayOfYear(pgbson *doc, void *arguments,
							   ExpressionResult *expressionResult)
{
	HandlePreParsedDatePartOperator(doc, arguments, "$dayOfYear", DatePart_DayOfYear,
									expressionResult);
}


/*
 * Parses a $dayOfMonth expression.
 * $dayOfMonth is expressed as { "$dayOfMonth": <dateExpression> }
 * or { "$dayOfMonth": { date: <dateExpression>, timezone: <tzExpression> } }
 * We evaluate the inner expression and then return the day of the month [1-31] in the specified date expression with the specified timezone.
 */
void
ParseDollarDayOfMonth(const bson_value_t *argument, AggregationExpressionData *data,
					  ParseAggregationExpressionContext *context)
{
	ParseDatePartOperator(argument, "$dayOfMonth", DatePart_DayOfMonth, data, context);
}


/*
 * Handles executing a pre-parsed $dayOfMonth expression.
 */
void
HandlePreParsedDollarDayOfMonth(pgbson *doc, void *arguments,
								ExpressionResult *expressionResult)
{
	HandlePreParsedDatePartOperator(doc, arguments, "$dayOfMonth", DatePart_DayOfMonth,
									expressionResult);
}


/*
 * Parses a $dayOfWeek expression.
 * $dayOfWeek is expressed as { "$dayOfWeek": <dateExpression> }
 * or { "$dayOfWeek": { date: <dateExpression>, timezone: <tzExpression> } }
 * We evaluate the inner expression and then return the day of the week [1 (Sunday) - 7 (Saturday)] in the specified date expression with the specified timezone.
 */
void
ParseDollarDayOfWeek(const bson_value_t *argument, AggregationExpressionData *data,
					 ParseAggregationExpressionContext *context)
{
	ParseDatePartOperator(argument, "$dayOfWeek", DatePart_DayOfWeek, data, context);
}


/*
 * Handles executing a pre-parsed $dayOfWeek expression.
 */
void
HandlePreParsedDollarDayOfWeek(pgbson *doc, void *arguments,
							   ExpressionResult *expressionResult)
{
	HandlePreParsedDatePartOperator(doc, arguments, "$dayOfWeek", DatePart_DayOfWeek,
									expressionResult);
}


/*
 * Parses a $isoWeekYear expression.
 * $isoWeekYear is expressed as { "$isoWeekYear": <dateExpression> }
 * or { "$isoWeekYear": { date: <dateExpression>, timezone: <tzExpression> } }
 * We evaluate the inner expression and then return the year based on the ISO 8601 week numbering in the specified date expression with the specified timezone.
 * In ISO 8601 the year starts with the Monday of week 1 and ends with the Sunday of the last week.
 * So in early January or late December the ISO year may be different from the Gregorian year. See HandleDollarIsoWeek summary for more details.
 */
void
ParseDollarIsoWeekYear(const bson_value_t *argument, AggregationExpressionData *data,
					   ParseAggregationExpressionContext *context)
{
	ParseDatePartOperator(argument, "$isoWeekYear", DatePart_IsoWeekYear, data, context);
}


/*
 * Handles executing a pre-parsed $isoWeekYear expression.
 */
void
HandlePreParsedDollarIsoWeekYear(pgbson *doc, void *arguments,
								 ExpressionResult *expressionResult)
{
	HandlePreParsedDatePartOperator(doc, arguments, "$isoWeekYear", DatePart_IsoWeekYear,
									expressionResult);
}


/*
 * Parses a $isoWeek expression.
 * $isoWeek is expressed as { "$isoWeek": <dateExpression> }
 * or { "$isoWeek": { date: <dateExpression>, timezone: <tzExpression> } }
 * We evaluate the inner expression and then return the week based on the ISO 8601 week numbering in the specified date expression with the specified timezone.
 * Iso week start on Mondays and the first week of a year contains January 4 of that year. In other words, the first Thursday
 * of a year is in week 1 of that year. So it is possible for early-January dates to be part of the 52nd or 53rd week
 * of the previous year and for late-December dates to be part of the first week of the next year. i.e: 2005-01-01
 * in ISO 8601 is part of the 53rd week of year 2004
 */
void
ParseDollarIsoWeek(const bson_value_t *argument, AggregationExpressionData *data,
				   ParseAggregationExpressionContext *context)
{
	ParseDatePartOperator(argument, "$isoWeek", DatePart_IsoWeek, data, context);
}


/*
 * Handles executing a pre-parsed $isoWeek expression.
 */
void
HandlePreParsedDollarIsoWeek(pgbson *doc, void *arguments,
							 ExpressionResult *expressionResult)
{
	HandlePreParsedDatePartOperator(doc, arguments, "$isoWeek", DatePart_IsoWeek,
									expressionResult);
}


/*
 * Parses a $isoDayOfWeek expression.
 * $isoDayOfWeek is expressed as { "$isoDayOfWeek": <dateExpression> }
 * or { "$isoDayOfWeek": { date: <dateExpression>, timezone: <tzExpression> } }
 * We evaluate the inner expression and then return the day of week based on the ISO 8601 numbering in the specified date expression with the specified timezone.
 * ISO 8601 week start on Mondays (1) and end on Sundays (7).
 */
void
ParseDollarIsoDayOfWeek(const bson_value_t *argument, AggregationExpressionData *data,
						ParseAggregationExpressionContext *context)
{
	ParseDatePartOperator(argument, "$isoDayOfWeek", DatePart_IsoDayOfWeek, data,
						  context);
}


/*
 * Handles executing a pre-parsed $isoDayOfWeek expression.
 */
void
HandlePreParsedDollarIsoDayOfWeek(pgbson *doc, void *arguments,
								  ExpressionResult *expressionResult)
{
	HandlePreParsedDatePartOperator(doc, arguments, "$isoDayOfWeek",
									DatePart_IsoDayOfWeek, expressionResult);
}


/*
 * Parses a $dateToString expression.
 * $datToString is expressed as { "$dateToString": { date: <dateExpression>, [ format: <strExpression>, timezone: <tzExpression>, onNull: <expression> ] } }
 * We validate the input document and return the string with the specified timezone using the specified format if any.
 * If no format is specified the default format is: %Y-%m-%dT%H:%M:%S.%LZ
 */
void
ParseDollarDateToString(const bson_value_t *argument, AggregationExpressionData *data,
						ParseAggregationExpressionContext *context)
{
	if (argument->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION18629), errmsg(
							"$dateToString only supports an object as its argument")));
	}

	bson_value_t dateExpression = { 0 };
	bson_value_t formatExpression = { 0 };
	bson_value_t timezoneExpression = { 0 };
	bson_value_t onNullExpression = { 0 };
	bson_iter_t documentIter;
	BsonValueInitIterator(argument, &documentIter);

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
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION18534), errmsg(
								"Unrecognized argument to $dateToString: %s", key),
							errdetail_log(
								"Unrecognized argument to $dateToString, Unexpected key found while parsing")));
		}
	}

	if (dateExpression.value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION18628), errmsg(
							"Missing 'date' parameter to $dateToString")));
	}

	bool allArgumentsConstant = true;

	/* The arguments will be in the order [date, format, timezone, onNull]*/
	List *parsedArguments = NIL;

	/* onNull is only used for when 'date' argument is null or undefined, so we default it to null for the other arguments. */
	bson_value_t onNullResult;
	onNullResult.value_type = BSON_TYPE_NULL;

	/* A format string was specified as an argument. */
	AggregationExpressionData *formatData = NULL;
	if (formatExpression.value_type != BSON_TYPE_EOD)
	{
		formatData = palloc0(sizeof(AggregationExpressionData));
		ParseAggregationExpressionData(formatData, &formatExpression, context);

		if (IsAggregationExpressionConstant(formatData))
		{
			/* Match native mongo, if any argument is null when evaluating, bail and don't evaluate the rest of the args. */
			if (IsExpressionResultNullOrUndefined(&formatData->value))
			{
				data->value = onNullResult;
				data->kind = AggregationExpressionKind_Constant;
				return;
			}

			if (formatData->value.value_type != BSON_TYPE_UTF8)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION18533), errmsg(
									"$dateToString requires that 'format' be a string, found: %s with value %s",
									BsonTypeName(formatData->value.value_type),
									BsonValueToJsonForLogging(&formatData->value)),
								errdetail_log(
									"$dateToString requires that 'format' be a string, found: %s",
									BsonTypeName(formatData->value.value_type))));
			}
		}

		allArgumentsConstant = allArgumentsConstant && IsAggregationExpressionConstant(
			formatData);
	}

	parsedArguments = list_make1(formatData);

	StringView formatString = {
		.length = 0,
		.string = NULL,
	};
	if (formatData != NULL)
	{
		formatString.length = formatData->value.value.v_utf8.len;
		formatString.string = formatData->value.value.v_utf8.str;
	}

	/* If no timezone is specified, we don't apply any offset as the default should be UTC. */
	ExtensionTimezone timezoneToApply = {
		.isUtcOffset = true,
		.offsetInMs = 0,
	};

	AggregationExpressionData *timezoneData = NULL;
	if (timezoneExpression.value_type != BSON_TYPE_EOD)
	{
		timezoneData = palloc0(sizeof(AggregationExpressionData));
		ParseAggregationExpressionData(timezoneData, &timezoneExpression, context);

		if (IsAggregationExpressionConstant(timezoneData))
		{
			if (IsExpressionResultNullOrUndefined(&timezoneData->value))
			{
				data->value = onNullResult;
				data->kind = AggregationExpressionKind_Constant;
				return;
			}

			if (timezoneData->value.value_type != BSON_TYPE_UTF8)
			{
				ThrowLocation40517Error(timezoneData->value.value_type);
			}

			if (!GetTimezoneToApply(&timezoneData->value, "$dateToString",
									&timezoneToApply))
			{
				bson_value_t nullResult;
				nullResult.value_type = BSON_TYPE_NULL;
				data->value = nullResult;
				data->kind = AggregationExpressionKind_Constant;

				pfree(timezoneData);
				if (formatData != NULL)
				{
					pfree(formatData);
				}
				return;
			}
		}

		allArgumentsConstant = allArgumentsConstant && IsAggregationExpressionConstant(
			timezoneData);
	}

	parsedArguments = lappend(parsedArguments, timezoneData);

	AggregationExpressionData *onNullData = NULL;

	/* onNull argument was specified. */
	if (onNullExpression.value_type != BSON_TYPE_EOD)
	{
		onNullData = palloc0(sizeof(AggregationExpressionData));
		ParseAggregationExpressionData(onNullData, &onNullExpression, context);
		allArgumentsConstant = allArgumentsConstant && IsAggregationExpressionConstant(
			onNullData);
	}

	parsedArguments = lappend(parsedArguments, onNullData);

	AggregationExpressionData *dateData = palloc0(sizeof(AggregationExpressionData));
	ParseAggregationExpressionData(dateData, &dateExpression, context);

	bool evaluatedOnConstants = false;
	parsedArguments = list_insert_nth(parsedArguments, 0, dateData);

	if (IsAggregationExpressionConstant(dateData))
	{
		if (IsExpressionResultNullOrUndefined(&dateData->value))
		{
			/*onNull argument was specified but evaluates to EOD, i.e: non existent path in the document,
			 * the result should be empty, so just bail. */
			if (onNullData != NULL && IsAggregationExpressionConstant(onNullData))
			{
				if (onNullData->value.value_type == BSON_TYPE_EOD)
				{
					FreeVariableLengthArgs(parsedArguments);
					evaluatedOnConstants = true;
					return;
				}
				else
				{
					onNullResult = onNullData->value;
				}

				data->value = onNullResult;
				data->kind = AggregationExpressionKind_Constant;
				FreeVariableLengthArgs(parsedArguments);
				evaluatedOnConstants = true;
				return;
			}
		}

		allArgumentsConstant = allArgumentsConstant && IsAggregationExpressionConstant(
			dateData);

		if (allArgumentsConstant)
		{
			ProcessDollarDateToString(&dateData->value, timezoneToApply, formatString,
									  &data->value);
			data->kind = AggregationExpressionKind_Constant;
			FreeVariableLengthArgs(parsedArguments);
			evaluatedOnConstants = true;
			return;
		}
	}

	if (!evaluatedOnConstants)
	{
		data->operator.arguments = parsedArguments;
		data->operator.argumentsKind = AggregationExpressionArgumentsKind_List;
	}
}


/*
 * Handles executing a pre-parsed $dateToString expression.
 */
void
HandlePreParsedDollarDateToString(pgbson *doc, void *arguments,
								  ExpressionResult *expressionResult)
{
	/* Arguments are in the order: [date, format, timezone, onNull] */
	List *parsedArguments = (List *) arguments;

	bool hasNullOrUndefined = false;
	bson_value_t onNullResult;
	onNullResult.value_type = BSON_TYPE_NULL;

	ExpressionResult childResult = ExpressionResultCreateChild(expressionResult);

	AggregationExpressionData *formatData = (AggregationExpressionData *) list_nth(
		parsedArguments, 1);

	if (formatData != NULL)
	{
		EvaluateAggregationExpressionData(formatData, doc, &childResult,
										  hasNullOrUndefined);

		if (IsExpressionResultNullOrUndefined(&childResult.value))
		{
			ExpressionResultSetValue(expressionResult, &onNullResult);
			return;
		}

		if (formatData->value.value_type != BSON_TYPE_UTF8)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION18533), errmsg(
								"$dateToString requires that 'format' be a string, found: %s with value %s",
								BsonTypeName(formatData->value.value_type),
								BsonValueToJsonForLogging(&formatData->value)),
							errdetail_log(
								"$dateToString requires that 'format' be a string, found: %s",
								BsonTypeName(formatData->value.value_type))));
		}

		ExpressionResultReset(&childResult);
	}

	StringView formatString = {
		.length = 0,
		.string = NULL,
	};

	if (formatData != NULL)
	{
		formatString.length = formatData->value.value.v_utf8.len;
		formatString.string = formatData->value.value.v_utf8.str;
	}

	ExtensionTimezone timezoneToApply = {
		.isUtcOffset = true,
		.offsetInMs = 0,
	};

	AggregationExpressionData *timezoneData = (AggregationExpressionData *) list_nth(
		parsedArguments, 2);
	if (timezoneData != NULL)
	{
		EvaluateAggregationExpressionData(timezoneData, doc, &childResult,
										  hasNullOrUndefined);

		bson_value_t timezoneValue = childResult.value;
		if (IsExpressionResultNullOrUndefined(&timezoneValue))
		{
			ExpressionResultSetValue(expressionResult, &onNullResult);
			return;
		}

		if (timezoneValue.value_type != BSON_TYPE_UTF8)
		{
			ThrowLocation40517Error(timezoneValue.value_type);
		}

		if (!GetTimezoneToApply(&timezoneValue, "$dateToString", &timezoneToApply))
		{
			bson_value_t nullResult;
			nullResult.value_type = BSON_TYPE_NULL;
			ExpressionResultSetValue(expressionResult, &nullResult);
			return;
		}

		ExpressionResultReset(&childResult);
	}

	bson_value_t onNullValue = { 0 };
	AggregationExpressionData *onNullData = (AggregationExpressionData *) list_nth(
		parsedArguments, 3);
	if (onNullData != NULL)
	{
		EvaluateAggregationExpressionData(onNullData, doc, &childResult,
										  hasNullOrUndefined);
		onNullValue = childResult.value;
		ExpressionResultReset(&childResult);
	}

	AggregationExpressionData *dateData = (AggregationExpressionData *) list_nth(
		parsedArguments, 0);
	EvaluateAggregationExpressionData(dateData, doc, &childResult, hasNullOrUndefined);
	bson_value_t dateValue = childResult.value;

	if (IsExpressionResultNullOrUndefined(&dateValue))
	{
		/*onNull argument was specified but evaluates to EOD, i.e: non existent path in the document,
		 * the result should be empty, so just bail. */
		if (onNullData != NULL)
		{
			if (onNullValue.value_type == BSON_TYPE_EOD)
			{
				return;
			}
			else
			{
				onNullResult = onNullValue;
			}

			ExpressionResultSetValue(expressionResult, &onNullResult);
			return;
		}
	}

	bson_value_t result = { 0 };
	ProcessDollarDateToString(&dateValue, timezoneToApply, formatString,
							  &result);
	ExpressionResultSetValue(expressionResult, &result);
}


/*
 * Parses a $dateToParts expression.
 * $datToParts is expressed as { "$dateToParts": { date: <dateExpression>, [ timezone: <tzExpression>, iso8601: <boolExpression> ] } }
 * We validate the input document and return the date parts from the 'date' adjusted to the provided 'timezone'. The date parts that we return
 * depend on the iso8601 argument which defaults to false when not provided. This is the output based on is08601 value:
 *   True -> {"isoWeekYear": <val>, "isoWeek": <val>, "isoDayOfWeek": <val>, "hour": <val>, "minute": <val>, "second": <val>, "millisecond": <val> }
 *   False -> {"year": <val>, "month": <val>, "day": <val>, hour": <val>, "minute": <val>, "second": <val>, "millisecond": <val> }
 */
void
ParseDollarDateToParts(const bson_value_t *argument, AggregationExpressionData *data,
					   ParseAggregationExpressionContext *context)
{
	if (argument->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40524), errmsg(
							"$dateToParts only supports an object as its argument")));
	}

	bson_value_t dateExpression = { 0 };
	bson_value_t timezoneExpression = { 0 };
	bson_value_t isoArgExpression = { 0 };
	bson_iter_t documentIter = { 0 };
	BsonValueInitIterator(argument, &documentIter);

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
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40520), errmsg(
								"Unrecognized argument to $dateToParts: %s", key),
							errdetail_log(
								"Unrecognized argument to $dateToParts, Unexpected key found in input")));
		}
	}

	if (dateExpression.value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40522), errmsg(
							"Missing 'date' parameter to $dateToParts")));
	}

	bson_value_t nullValue;
	nullValue.value_type = BSON_TYPE_NULL;

	bool allArgumentsConstant = true;

	/* If no timezone is specified, we don't apply any offset as that's the default timezone, UTC. */
	ExtensionTimezone timezoneToApply = {
		.offsetInMs = 0,
		.isUtcOffset = true,
	};

	AggregationExpressionData *timezoneData = NULL;
	if (timezoneExpression.value_type != BSON_TYPE_EOD)
	{
		timezoneData = palloc0(sizeof(AggregationExpressionData));
		ParseAggregationExpressionData(timezoneData, &timezoneExpression, context);

		if (IsAggregationExpressionConstant(timezoneData))
		{
			if (IsExpressionResultNullOrUndefined(&timezoneData->value))
			{
				data->value = nullValue;
				data->kind = AggregationExpressionKind_Constant;
				return;
			}

			if (timezoneData->value.value_type != BSON_TYPE_UTF8)
			{
				ThrowLocation40517Error(timezoneData->value.value_type);
			}

			if (!GetTimezoneToApply(&timezoneData->value, "$dateToParts",
									&timezoneToApply))
			{
				bson_value_t nullResult;
				nullResult.value_type = BSON_TYPE_NULL;
				data->value = nullResult;
				data->kind = AggregationExpressionKind_Constant;

				pfree(timezoneData);
				return;
			}
		}

		allArgumentsConstant = allArgumentsConstant && IsAggregationExpressionConstant(
			timezoneData);
	}

	bool isIsoRequested = false;
	AggregationExpressionData *isoData = NULL;
	if (isoArgExpression.value_type != BSON_TYPE_EOD)
	{
		isoData = palloc0(sizeof(AggregationExpressionData));
		ParseAggregationExpressionData(isoData, &isoArgExpression, context);

		if (IsAggregationExpressionConstant(isoData))
		{
			/* Match native mongo, if iso8601 resolves to null or undefined, we bail before
			 * evaluating the other arguments and return null. */
			if (!GetIsIsoRequested(&isoData->value, &isIsoRequested))
			{
				data->value = nullValue;
				data->kind = AggregationExpressionKind_Constant;
				return;
			}
		}

		allArgumentsConstant = allArgumentsConstant && IsAggregationExpressionConstant(
			isoData);
	}

	AggregationExpressionData *dateData = palloc0(sizeof(AggregationExpressionData));
	ParseAggregationExpressionData(dateData, &dateExpression, context);

	List *parsedArguments = list_make3(dateData, timezoneData, isoData);
	bool evaluatedOnConstants = false;

	if (IsAggregationExpressionConstant(dateData))
	{
		if (IsExpressionResultNullOrUndefined(
				&dateData->value))
		{
			data->value = nullValue;
			data->kind = AggregationExpressionKind_Constant;
			FreeVariableLengthArgs(parsedArguments);
			evaluatedOnConstants = true;
			return;
		}

		if (allArgumentsConstant)
		{
			ProcessDollarDateToParts(&dateData->value, isIsoRequested, timezoneToApply,
									 &data->value);
			data->kind = AggregationExpressionKind_Constant;
			FreeVariableLengthArgs(parsedArguments);
			evaluatedOnConstants = true;
		}
	}

	if (!evaluatedOnConstants)
	{
		data->operator.arguments = parsedArguments;
		data->operator.argumentsKind = AggregationExpressionArgumentsKind_List;
	}
}


/*
 * Handles executing a pre-parsed $dateToParts expression.
 */
void
HandlePreParsedDollarDateToParts(pgbson *doc, void *arguments,
								 ExpressionResult *expressionResult)
{
	/* Args are in the order: date, timezone, iso8601 */
	List *argumentsList = (List *) arguments;
	bool hasNullOrUndefined = false;
	ExpressionResult childResult = ExpressionResultCreateChild(expressionResult);

	bson_value_t nullValue;
	nullValue.value_type = BSON_TYPE_NULL;

	ExtensionTimezone timezoneToApply = {
		.offsetInMs = 0,
		.isUtcOffset = true,
	};

	AggregationExpressionData *timezoneData = list_nth(argumentsList, 1);
	bson_value_t timezoneValue = { 0 };

	if (timezoneData != NULL)
	{
		childResult = ExpressionResultCreateChild(expressionResult);
		EvaluateAggregationExpressionData(timezoneData, doc, &childResult,
										  hasNullOrUndefined);
		timezoneValue = childResult.value;
		ExpressionResultReset(&childResult);

		if (IsExpressionResultNullOrUndefined(&timezoneValue))
		{
			ExpressionResultSetValue(expressionResult, &nullValue);
			return;
		}

		if (timezoneValue.value_type != BSON_TYPE_UTF8)
		{
			ThrowLocation40517Error(timezoneValue.value_type);
		}

		if (!GetTimezoneToApply(&timezoneValue, "$dateToParts", &timezoneToApply))
		{
			ExpressionResultSetValue(expressionResult, &nullValue);
			return;
		}
	}

	AggregationExpressionData *isoData = list_nth(argumentsList, 2);
	bool isIsoRequested = false;

	if (isoData != NULL)
	{
		childResult = ExpressionResultCreateChild(expressionResult);
		EvaluateAggregationExpressionData(isoData, doc, &childResult,
										  hasNullOrUndefined);

		/* Match native mongo, if iso8601 resolves to null or undefined, we bail before
		 * evaluating the other arguments and return null. */
		if (!GetIsIsoRequested(&childResult.value, &isIsoRequested))
		{
			ExpressionResultSetValue(expressionResult, &nullValue);
			return;
		}

		ExpressionResultReset(&childResult);
	}

	AggregationExpressionData *dateData = list_nth(argumentsList, 0);
	EvaluateAggregationExpressionData(dateData, doc, &childResult, hasNullOrUndefined);

	bson_value_t dateValue = childResult.value;

	if (IsExpressionResultNullOrUndefined(&dateValue))
	{
		ExpressionResultSetValue(expressionResult, &nullValue);
		return;
	}

	bson_value_t result = { 0 };
	ProcessDollarDateToParts(&dateValue, isIsoRequested, timezoneToApply,
							 &result);
	ExpressionResultSetValue(expressionResult, &result);
}


/* --------------------------------------------------------- */
/* Parser and Handle Pre-parser helper functions. */
/* --------------------------------------------------------- */

/* Common method for operators that return a single date part. This method parses and validates the arguments and
 * sets the result into the specified ExpressionResult. For more details about every operator, see its
 * HandleDollar* method description. */
static void
ParseDatePartOperator(const bson_value_t *argument,
					  const char *operatorName,
					  DatePart datePart, AggregationExpressionData *data,
					  ParseAggregationExpressionContext *context)
{
	bson_value_t dateExpression;
	bson_value_t timezoneExpression;
	bool isTimezoneSpecified = ParseDatePartOperatorArgument(argument, operatorName,
															 &dateExpression,
															 &timezoneExpression);

	AggregationExpressionData *dateData = palloc0(sizeof(AggregationExpressionData));
	ParseAggregationExpressionData(dateData, &dateExpression, context);
	if (IsAggregationExpressionConstant(dateData) && IsExpressionResultNullOrUndefined(
			&dateData->value))
	{
		bson_value_t nullResult;
		nullResult.value_type = BSON_TYPE_NULL;
		data->value = nullResult;

		data->kind = AggregationExpressionKind_Constant;
		pfree(dateData);
		return;
	}


	/* In case no timezone is specified we just apply a 0 offset to get the default timezone which is UTC.*/
	ExtensionTimezone timezoneToApply = {
		.offsetInMs = 0,
		.isUtcOffset = true,
	};

	AggregationExpressionData *timezoneData = NULL;
	if (isTimezoneSpecified)
	{
		timezoneData = palloc0(sizeof(AggregationExpressionData));
		ParseAggregationExpressionData(timezoneData, &timezoneExpression, context);

		if (IsAggregationExpressionConstant(timezoneData))
		{
			if (!GetTimezoneToApply(&timezoneData->value, operatorName, &timezoneToApply))
			{
				bson_value_t nullResult;
				nullResult.value_type = BSON_TYPE_NULL;
				data->value = nullResult;
				data->kind = AggregationExpressionKind_Constant;

				pfree(dateData);
				pfree(timezoneData);
				return;
			}
		}
	}


	if (IsAggregationExpressionConstant(dateData) &&
		(timezoneData == NULL || IsAggregationExpressionConstant(timezoneData)))
	{
		GetDatePartResult(&dateData->value, timezoneToApply, datePart, &data->value);

		data->kind = AggregationExpressionKind_Constant;
		pfree(dateData);
		if (timezoneData != NULL)
		{
			pfree(timezoneData);
		}
	}
	else
	{
		List *argumentsList = list_make2(dateData, timezoneData);
		data->operator.arguments = argumentsList;
		data->operator.argumentsKind = AggregationExpressionArgumentsKind_List;
	}
}


/*
 * Helper to handle executing a pre-parsed date part operator expressions.
 */
static void
HandlePreParsedDatePartOperator(pgbson *doc, void *arguments, const char *operatorName,
								DatePart datePart, ExpressionResult *expressionResult)
{
	List *argumentsList = (List *) arguments;
	bool hasNullOrUndefined = false;
	ExpressionResult childResult = ExpressionResultCreateChild(expressionResult);

	AggregationExpressionData *dateData = list_nth(argumentsList, 0);
	EvaluateAggregationExpressionData(dateData, doc, &childResult, hasNullOrUndefined);

	bson_value_t dateValue = childResult.value;
	ExpressionResultReset(&childResult);

	if (IsExpressionResultNullOrUndefined(&dateValue))
	{
		bson_value_t nullValue;
		nullValue.value_type = BSON_TYPE_NULL;
		ExpressionResultSetValue(expressionResult, &nullValue);
		return;
	}

	ExtensionTimezone timezoneToApply = {
		.offsetInMs = 0,
		.isUtcOffset = true,
	};

	AggregationExpressionData *timezoneData = list_nth(argumentsList, 1);
	bson_value_t timezoneValue = { 0 };

	if (timezoneData != NULL)
	{
		childResult = ExpressionResultCreateChild(expressionResult);
		EvaluateAggregationExpressionData(timezoneData, doc, &childResult,
										  hasNullOrUndefined);
		timezoneValue = childResult.value;
		ExpressionResultReset(&childResult);

		if (!GetTimezoneToApply(&timezoneValue, operatorName, &timezoneToApply))
		{
			bson_value_t nullResult;
			nullResult.value_type = BSON_TYPE_NULL;
			ExpressionResultSetValue(expressionResult, &nullResult);
			return;
		}
	}

	bson_value_t result = { 0 };
	GetDatePartResult(&dateValue, timezoneToApply, datePart, &result);
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
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40536), errmsg(
								"%s accepts exactly one argument if given an array, but was given %d",
								operatorName, numArgs),
							errdetail_log(
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
					ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40535), errmsg(
										"unrecognized option to %s: \"%s\"",
										operatorName, key),
									errdetail_log(
										"unrecognized option to operator %s, Unexpected key in input",
										operatorName)));
				}
			} while (bson_iter_next(&documentIter));
		}

		if (!isDateSpecified)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40539),
							errmsg(
								"missing 'date' argument to %s, provided: %s: %s",
								operatorName, operatorName, BsonValueToJsonForLogging(
									operatorValue)),
							errdetail_log(
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


/* --------------------------------------------------------- */
/* Process operator helper functions. */
/* --------------------------------------------------------- */

static void
ProcessDollarDateToString(const bson_value_t *dateValue, ExtensionTimezone
						  timezoneToApply, StringView formatString, bson_value_t *result)
{
	if (IsExpressionResultNullOrUndefined(dateValue))
	{
		bson_value_t nullResult;
		nullResult.value_type = BSON_TYPE_NULL;
		*result = nullResult;
		return;
	}

	int64_t dateTimeInMs = BsonValueAsDateTime(dateValue);

	StringView stringResult;
	if (formatString.length != 0)
	{
		stringResult = GetDateStringWithFormat(dateTimeInMs, timezoneToApply,
											   formatString);
	}
	else
	{
		stringResult = GetDateStringWithDefaultFormat(dateTimeInMs, timezoneToApply,
													  DateStringFormatCase_UpperCase);
	}

	bson_value_t stringValue = {
		.value_type = BSON_TYPE_UTF8,
		.value.v_utf8.str = (char *) stringResult.string,
		.value.v_utf8.len = stringResult.length,
	};

	*result = stringValue;
}


/* Eexecutes the core logic for a $dateToParts operation. */
static void
ProcessDollarDateToParts(const bson_value_t *dateValue, bool isIsoRequested,
						 ExtensionTimezone timezoneToApply, bson_value_t *result)
{
	int64_t dateInMs = BsonValueAsDateTime(dateValue);
	Datum pgTimestamp = GetPgTimestampFromEpochWithTimezone(dateInMs, timezoneToApply);

	/* Get date parts and write them to the result. */
	pgbson_writer objectWriter;
	PgbsonWriterInit(&objectWriter);

	pgbson_element_writer elementWriter;
	PgbsonInitObjectElementWriter(&objectWriter, &elementWriter, "", 0);

	pgbson_writer childWriter;
	PgbsonElementWriterStartDocument(&elementWriter, &childWriter);

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
		PgbsonWriterAppendInt32(&childWriter, "isoWeekYear", 11, isoWeekY);
		PgbsonWriterAppendInt32(&childWriter, "isoWeek", 7, isoWeek);
		PgbsonWriterAppendInt32(&childWriter, "isoDayOfWeek", 12, isoDoW);
	}
	else
	{
		int year = GetDatePartFromPgTimestamp(pgTimestamp, DatePart_Year);
		int month = GetDatePartFromPgTimestamp(pgTimestamp, DatePart_Month);
		int dom = GetDatePartFromPgTimestamp(pgTimestamp, DatePart_DayOfMonth);
		PgbsonWriterAppendInt32(&childWriter, "year", 4, year);
		PgbsonWriterAppendInt32(&childWriter, "month", 5, month);
		PgbsonWriterAppendInt32(&childWriter, "day", 3, dom);
	}

	int hour = GetDatePartFromPgTimestamp(pgTimestamp, DatePart_Hour);
	int minute = GetDatePartFromPgTimestamp(pgTimestamp, DatePart_Minute);
	int second = GetDatePartFromPgTimestamp(pgTimestamp, DatePart_Second);
	int ms = GetDatePartFromPgTimestamp(pgTimestamp, DatePart_Millisecond);
	PgbsonWriterAppendInt32(&childWriter, "hour", 4, hour);
	PgbsonWriterAppendInt32(&childWriter, "minute", 6, minute);
	PgbsonWriterAppendInt32(&childWriter, "second", 6, second);
	PgbsonWriterAppendInt32(&childWriter, "millisecond", 11, ms);

	PgbsonElementWriterEndDocument(&elementWriter, &childWriter);
	const bson_value_t bsonValue = PgbsonElementWriterGetValue(&elementWriter);

	pgbson *pgbson = BsonValueToDocumentPgbson(&bsonValue);
	pgbsonelement element;
	PgbsonToSinglePgbsonElement(pgbson, &element);
	*result = element.bsonValue;
}


static bool
GetIsIsoRequested(bson_value_t *isoValue, bool *isIsoRequested)
{
	if (IsExpressionResultNullOrUndefined(isoValue))
	{
		return false;
	}

	if (isoValue->value_type != BSON_TYPE_BOOL)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40521), errmsg(
							"iso8601 must evaluate to a bool, found %s",
							BsonTypeName(isoValue->value_type)),
						errdetail_log("iso8601 must evaluate to a bool, found %s",
									  BsonTypeName(isoValue->value_type))));
	}

	*isIsoRequested = isoValue->value.v_bool;
	return true;
}


static bool
GetTimezoneToApply(const bson_value_t *timezoneValue, const char *operatorName,
				   ExtensionTimezone *timezoneToApply)
{
	if (IsExpressionResultNullOrUndefined(timezoneValue))
	{
		return false;
	}

	if (timezoneValue->value_type != BSON_TYPE_UTF8)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40533), errmsg(
							"%s requires a string for the timezone argument, but was given a %s (%s)",
							operatorName, BsonTypeName(
								timezoneValue->value_type),
							BsonValueToJsonForLogging(timezoneValue)),
						errdetail_log(
							"'%s' requires a string for the timezone argument, but was given a %s",
							operatorName, BsonTypeName(
								timezoneValue->value_type))));
	}

	StringView timezone = {
		.string = timezoneValue->value.v_utf8.str,
		.length = timezoneValue->value.v_utf8.len,
	};

	*timezoneToApply = ParseTimezone(timezone);
	return true;
}


static void
GetDatePartResult(bson_value_t *dateValue, ExtensionTimezone timezone, DatePart datePart,
				  bson_value_t *result)
{
	if (IsExpressionResultNullOrUndefined(dateValue))
	{
		bson_value_t nullResult;
		nullResult.value_type = BSON_TYPE_NULL;
		*result = nullResult;
		return;
	}


	int64_t dateTimeInMs = BsonValueAsDateTime(dateValue);
	Datum pgTimestamp = GetPgTimestampFromEpochWithTimezone(dateTimeInMs,
															timezone);
	uint32_t datePartResult = GetDatePartFromPgTimestamp(pgTimestamp, datePart);

	if (datePart == DatePart_IsoWeekYear)
	{
		/* $isoWeekYear is a long in native mongo */
		result->value_type = BSON_TYPE_INT64;
		result->value.v_int64 = (int64_t) datePartResult;
	}
	else
	{
		result->value_type = BSON_TYPE_INT32;
		result->value.v_int32 = datePartResult;
	}
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
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION18535), errmsg(
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
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION18536), errmsg(
									"Invalid format character '%%%c' in format string",
									*formatPtr),
								errdetail_log(
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
						errdetail_log(
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
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
							errmsg("Invalid date part unit %d", datePart),
							errdetail_log("Invalid date part unit %d", datePart)));
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
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40519), errmsg(
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
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40518), errmsg(
								"Unrecognized argument to $dateFromParts: %s", key),
							errdetail_log(
								"Unrecognized argument to $dateFromParts, unexpected key")));
		}
	}

	/* Both the isodate part and normal date part present */
	if (*isIsoWeekDate && year->value_type != BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40489), errmsg(
							"$dateFromParts does not allow mixing natural dates with ISO dates")));
	}

	/* In case of ISO date normal date part should not be present */
	if (*isIsoWeekDate && (month->value_type != BSON_TYPE_EOD || day->value_type !=
						   BSON_TYPE_EOD))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40525), errmsg(
							"$dateFromParts does not allow mixing natural dates with ISO dates")));
	}

	/* Either year or isoWeekYear is a must given it's type is iso format or non-iso format */
	if ((!(*isIsoWeekDate) && year->value_type == BSON_TYPE_EOD) ||
		(*isIsoWeekDate && isoWeekYear->value_type == BSON_TYPE_EOD))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40516), errmsg(
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
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40517), errmsg(
							"timezone must evaluate to a string, found %s", BsonTypeName(
								dateFromPartsValue->timezone.value_type)),
						errdetail_log(
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
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40515), errmsg(
							"'%s' must evaluate to an integer, found %s with value :%s",
							inputKey, BsonTypeName(inputValue->value_type),
							BsonValueToJsonForLogging(inputValue)),
						errdetail_log(
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
				ereport(ERROR, (errcode(datePart == DatePart_Year ?
										ERRCODE_DOCUMENTDB_LOCATION40523 :
										ERRCODE_DOCUMENTDB_LOCATION31095), errmsg(
									"'%s' must evaluate to an integer in the range 1 to 9999, found %s",
									inputKey, BsonValueToJsonForLogging(inputValue)),
								errdetail_log(
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
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION31034), errmsg(
									"'%s' must evaluate to a value in the range [-32768, 32767]; value %s is not in range",
									inputKey, BsonValueToJsonForLogging(inputValue)),
								errdetail_log(
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
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_DURATIONOVERFLOW), errmsg(
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
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5439007), errmsg(
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
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5439014), errmsg(
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
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5439008), errmsg(
								"Unrecognized argument to $dateTrunc: %s. Expected arguments are date, unit, and optionally, binSize, timezone, startOfWeek",
								key),
							errdetail_log(
								"Unrecognized argument to $dateTrunc: %s. Expected arguments are date, unit, and optionally, binSize, timezone, startOfWeek",
								key)));
		}
	}

	/*	Validation to check date and unit are required. */
	if (date->value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5439009), errmsg(
							"Missing 'date' parameter to $dateTrunc")));
	}

	if (unit->value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5439010), errmsg(
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
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5439012), errmsg(
							"$dateTrunc requires 'date' to be a date, but got %s",
							BsonTypeName(date->value_type)),
						errdetail_log(
							"$dateTrunc requires 'date' to be a date, but got %s",
							BsonTypeName(date->value_type))));
	}

	/* validate unit type */
	if (unit->value_type != BSON_TYPE_UTF8)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5439013), errmsg(
							"$dateTrunc requires 'unit' to be a string, but got %s",
							BsonTypeName(unit->value_type)),
						errdetail_log(
							"$dateTrunc requires 'unit' to be a string, but got %s",
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
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5439014), errmsg(
							"$dateTrunc parameter 'unit' value cannot be recognized as a time unit: %s",
							unit->value.v_utf8.str),
						errdetail_log(
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
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5439016), errmsg(
								"$dateTrunc parameter 'startOfWeek' value cannot be recognized as a day of a week: %s",
								startOfWeek->value.v_utf8.str),
							errdetail_log(
								"$dateTrunc parameter 'startOfWeek' value cannot be recognized as a day of a week")));
		}
	}

	/* check binSize value type */
	if (!IsBsonValueFixedInteger(binSize))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5439017), errmsg(
							"$dateTrunc requires 'binSize' to be a 64-bit integer, but got value '%s' of type %s",
							BsonValueToJsonForLogging(binSize), BsonTypeName(
								binSize->value_type)),
						errdetail_log(
							"$dateTrunc requires 'binSize' to be a 64-bit integer, but got value of type %s",
							BsonTypeName(binSize->value_type))));
	}

	/* binSize should be greater than zero. */
	int64_t binSizeVal = BsonValueAsInt64(binSize);
	if (binSizeVal <= 0)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5439018), errmsg(
							"$dateTrunc requires 'binSize' to be greater than 0, but got value %ld",
							binSizeVal),
						errdetail_log(
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
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5166400), errmsg(
							"%s expects an object as its argument.found input type:%s",
							opName, BsonValueToJsonForLogging(inputArgument)),
						errdetail_log(
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
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5166401), errmsg(
								"Unrecognized argument to %s: %s. Expected arguments are startDate, unit, amount, and optionally timezone",
								opName, key),
							errdetail_log(
								"Unrecognized argument to %s: Expected arguments are startDate, unit, amount, and optionally timezone",
								opName)));
		}
	}

	if (startDate->value_type == BSON_TYPE_EOD || unit->value_type == BSON_TYPE_EOD ||
		amount == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5166402), errmsg(
							"%s requires startDate, unit, and amount to be present",
							opName),
						errdetail_log(
							"%s requires startDate, unit, and amount to be present",
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
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5166406), errmsg(
							"%s overflowed", opName),
						errdetail_log("%s overflowed", opName)));
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
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5166406), errmsg(
							"$dateSubtract overflowed"),
						errdetail_log("$dateSubtract overflowed")));
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
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5166403), errmsg(
							"%s requires startDate to be convertible to a date",
							opName),
						errdetail_log("%s requires startDate to be convertible to a date",
									  opName)));
	}

	if (unit->value_type != BSON_TYPE_UTF8)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5166404), errmsg(
							"%s expects string defining the time unit",
							opName),
						errdetail_log("%s expects string defining the time unit",
									  opName)));
	}
	char *unitStrValue = unit->value.v_utf8.str;
	DateUnit unitEnum = GetDateUnitFromString(unitStrValue);

	if (unitEnum == DateUnit_Invalid)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE), errmsg(
							"unknown time unit value: %s", unitStrValue),
						errdetail_log("unknown time unit value")));
	}

	if (!IsBsonValueFixedInteger(amount))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5166405), errmsg(
							"%s expects integer amount of time units", opName),
						errdetail_log("%s expects integer amount of time units",
									  opName)));
	}

	int64 amountVal = BsonValueAsInt64(amount);

	if (!isDateAdd && amountVal == INT64_MIN)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION6045000), errmsg(
							"invalid %s 'amount' parameter value: %s %s", opName,
							BsonValueToJsonForLogging(amount), unitStrValue),
						errdetail_log("invalid %s 'amount' parameter value: %s %s",
									  opName,
									  BsonValueToJsonForLogging(amount), unitStrValue)));
	}

	if (!IsAmountRangeValidForDateUnit(amountVal, unitEnum))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5976500), errmsg(
							"invalid %s 'amount' parameter value: %s %s", opName,
							BsonValueToJsonForLogging(amount), unitStrValue),
						errdetail_log("invalid %s 'amount' parameter value: %s %s",
									  opName,
									  BsonValueToJsonForLogging(amount), unitStrValue)));
	}

	/* Since no , default value is set hence, need to check if not eod then it should be UTF8 */
	if (timezone->value_type != BSON_TYPE_EOD && timezone->value_type != BSON_TYPE_UTF8)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40517), errmsg(
							"timezone must evaluate to a string, found %s", BsonTypeName(
								timezone->value_type)),
						errdetail_log("timezone must evaluate to a string, found %s",
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
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5166301), errmsg(
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
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5166302), errmsg(
								"Unrecognized argument to $dateDiff: %s",
								key),
							errdetail_log(
								"Unrecognized argument to $dateDiff: %s",
								key)));
		}
	}

	/*	Validation to check start and end date */
	if (startDate->value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5166303), errmsg(
							"Missing 'startDate' parameter to $dateDiff"),
						errdetail_log("Missing 'startDate' parameter to $dateDiff")));
	}

	if (endDate->value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5166304), errmsg(
							"Missing 'endDate' parameter to $dateDiff"),
						errdetail_log("Missing 'endDate' parameter to $dateDiff")));
	}

	if (unit->value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5166305), errmsg(
							"Missing 'unit' parameter to $dateDiff"),
						errdetail_log("Missing 'unit' parameter to $dateDiff")));
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
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5166307), errmsg(
							"$dateDiff requires '%s' to be a date, but got %s",
							(isStartDateValid ? "endDate" : "startDate"),
							BsonTypeName(isStartDateValid ? endDate->value_type :
										 startDate->value_type)),
						errdetail_log("$dateDiff requires '%s' to be a date, but got %s",
									  (isStartDateValid ? "endDate" : "startDate"),
									  BsonTypeName(isStartDateValid ?
												   endDate->value_type :
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
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5439014), errmsg(
							"$dateDiff parameter 'unit' value cannot be recognized as a time unit: %s",
							unit->value.v_utf8.str),
						errdetail_log(
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
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5439016), errmsg(
								"$dateDiff parameter 'startOfWeek' value cannot be recognized as a day of a week: %s",
								startOfWeek->value.v_utf8.str),
							errdetail_log(
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
	if (IsArgumentForDateFromStringNull(&dateString,
										&format,
										&timezone,
										&isDateStringNull,
										dateFromStringState->flags))
	{
		/* Set the result to onNull expression if dateString is null otherwise set null. */
		if (CheckFlag(dateFromStringState->flags, FLAG_ON_NULL_SPECIFIED) &&
			isDateStringNull)
		{
			result = onNull;
		}
		if (result.value_type != BSON_TYPE_EOD)
		{
			ExpressionResultSetValue(expressionResult, &result);
		}
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

	ValidateInputForDateFromString(&dateString,
								   &format, &timezone, &onError,
								   dateFromStringState->flags,
								   dateFromParts,
								   &timezoneToApply, &isInputValid);

	if (isInputValid)
	{
		SetResultValueForDateFromString(dateFromParts, timezoneToApply, &result);
	}
	else if (CheckFlag(dateFromStringState->flags, FLAG_ON_ERROR_SPECIFIED))
	{
		result = onError;
	}
	pfree(dateFromParts);
	if (result.value_type != BSON_TYPE_EOD)
	{
		ExpressionResultSetValue(expressionResult, &result);
	}
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
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40540), errmsg(
							"$dateFromString only supports an object as an argument, found: %s",
							BsonTypeName(argument->value_type)),
						errdetail_log(
							"$dateFromString only supports an object as an argument, found: %s",
							BsonTypeName(argument->value_type))));
	}
	bson_value_t format = { 0 };
	bson_value_t dateString = { 0 };
	bson_value_t timezone = { 0 };
	bson_value_t onNull = { 0 };
	bson_value_t onError = { 0 };

	/* bool isTimezoneProvided = false; */
	/* bool isFormatSpecified = false; */
	/* bool isOnErrorSpecified = false; */
	/* bool isOnNullSpecified = false; */
	uint8_t flags = 0;
	ParseInputForDateFromString(argument, &dateString, &format, &timezone, &onNull,
								&onError, &flags);
	DollarDateFromStringArgumentState *dateFromStringArguments = palloc0(
		sizeof(DollarDateFromStringArgumentState));

	/* We need to set this flag as true because we need to identify a way if input timezone was provided or not. */
	dateFromStringArguments->flags = flags;

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
											flags))
		{
			/* Only in the case that dateString is nullish and onNull is specified, onNull will be returned. */
			if (CheckFlag(flags, FLAG_ON_NULL_SPECIFIED) && isDateStringNull)
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
									   dateFromStringArguments->flags,
									   dateFromParts,
									   &timezoneToApply,
									   &isInputValid);

		/* If isInputValid is false then we set the result as onError otherwise we would have already thrown error. */
		if (isInputValid)
		{
			SetResultValueForDateFromString(dateFromParts, timezoneToApply, &result);
		}
		else if (CheckFlag(flags, FLAG_ON_ERROR_SPECIFIED))
		{
			result = dateFromStringArguments->onError.value;
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
 * The principles for handling null arguments are as follows:
 *   1) If the `dateString` argument is null and onNull is specified, the result is onNull.
 *   2) If the `dateString` argument is null and onNull is not specified, the result is null.
 *   3) If the `dateString` argument is string and either `format` or `timezone` is null, the result is null.
 *
 * For the other cases, the result should be handled by parse stage. If the parse stage fails, the result will
 * be set to onError if it is specified; otherwise, the error should be thrown.
 */
static inline bool
IsArgumentForDateFromStringNull(bson_value_t *dateString,
								bson_value_t *format,
								bson_value_t *timezone,
								bool *isDateStringNull,
								uint8_t flags)
{
	*isDateStringNull = IsExpressionResultNullOrUndefined(dateString);
	if (*isDateStringNull)
	{
		return true;
	}

	return ((CheckFlag(flags, FLAG_FORMAT_SPECIFIED) && IsExpressionResultNullOrUndefined(
				 format)) ||
			(CheckFlag(flags, FLAG_TIMEZONE_SPECIFIED) &&
			 IsExpressionResultNullOrUndefined(timezone))) &&
		   dateString->value_type == BSON_TYPE_UTF8;
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


static inline bool
CheckFlag(uint8_t flags, uint8_t flag)
{
	return (flags & flag) != 0;
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


static inline int
CompareTimezoneMap(const void *a, const void *b)
{
	const TimezoneMap *mapA = (const TimezoneMap *) a;
	const TimezoneMap *mapB = (const TimezoneMap *) b;
	return strcmp(mapA->abbreviation, mapB->abbreviation);
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
																	  ERRCODE_DOCUMENTDB_CONVERSIONFAILURE),
																  errmsg(
																	  "Error parsing date string '%s';The parsed date was invalid",
																	  dateString),
																  errdetail_log(
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
																	  ERRCODE_DOCUMENTDB_CONVERSIONFAILURE),
																  errmsg(
																	  "Error Parsing date string %s;A 'day of year' can only come after a year has been found",
																	  dateString),
																  errdetail_log(
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
																	  ERRCODE_DOCUMENTDB_CONVERSIONFAILURE),
																  errmsg(
																	  "an incomplete date/time string has been found, with elements missing: '%s'",
																	  dateString),
																  errdetail_log(
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
	// YB: Added initializer to avoid uninitialized const fields.
	DateFormatMap key = {};
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


static inline char *
GetOffsetFromTimezoneAbbr(char *timezoneAbbr)
{
	TimezoneMap key;
	key.abbreviation = timezoneAbbr;
	TimezoneMap *result = bsearch(&key, timezoneMap, sizeof(timezoneMap) /
								  sizeof(TimezoneMap), sizeof(TimezoneMap),
								  CompareTimezoneMap);
	if (result != NULL)
	{
		return result->offset;
	}
	return NULL;
}


/**
 * Parses the given input document and sets the result in respective variables.
 */
void
ParseInputForDateFromString(const bson_value_t *inputArgument, bson_value_t *dateString,
							bson_value_t *format, bson_value_t *timezone,
							bson_value_t *onNull, bson_value_t *onError,
							uint8_t *flags)
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
			*flags |= FLAG_FORMAT_SPECIFIED;
		}
		else if (strcmp(key, "onNull") == 0)
		{
			*onNull = *bson_iter_value(&docIter);
			*flags |= FLAG_ON_NULL_SPECIFIED;
		}
		else if (strcmp(key, "timezone") == 0)
		{
			*timezone = *bson_iter_value(&docIter);
			*flags |= FLAG_TIMEZONE_SPECIFIED;
		}
		else if (strcmp(key, "onError") == 0)
		{
			*onError = *bson_iter_value(&docIter);
			*flags |= FLAG_ON_ERROR_SPECIFIED;
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40541), errmsg(
								"Unrecognized argument to $dateFromString: %s",
								key),
							errdetail_log(
								"Unrecognized argument to $dateFromString: %s",
								key)));
		}
	}

	/*	Validation to check date and unit are required. */
	if (dateString->value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40542), errmsg(
							"Missing 'dateString' parameter to $dateFromString"),
						errdetail_log(
							"Missing 'dateString' parameter to $dateFromString")));
	}

	if (onNull->value_type == BSON_TYPE_EOD)
	{
		onNull->value_type = BSON_TYPE_NULL;
	}

	/* Setting the default value for format in case of absent */
	/* if (format->value_type == BSON_TYPE_EOD) */
	/* { */
	/*  format->value_type = BSON_TYPE_UTF8; */
	/*  format->value.v_utf8.str = (char *) DefaultFormatForDateFromString; */
	/*  format->value.v_utf8.len = DefaultFormatLenForDateFromString; */
	/* } */
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
 * This function validates the input and sets the isInputValid flag to false if the input is invalid.
 */
static void
ValidateInputForDateFromString(bson_value_t *dateString,
							   bson_value_t *format,
							   bson_value_t *timezone, bson_value_t *onError,
							   uint8_t flags,
							   DollarDateFromPartsBsonValue *dateFromParts,
							   ExtensionTimezone *timezoneToApply, bool *isInputValid)
{
	/* Even if the value of onError is null or EOD, we will use it as it is rather than throwing error. */
	bool isOnErrorPresent = CheckFlag(flags, FLAG_ON_ERROR_SPECIFIED);
	bool isFormatSpecified = CheckFlag(flags, FLAG_FORMAT_SPECIFIED);

	/* If dateString is not a string */
	if (dateString->value_type != BSON_TYPE_UTF8)
	{
		CONDITIONAL_EREPORT(isOnErrorPresent,
							ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_CONVERSIONFAILURE),
											errmsg(
												"$dateFromString requires that 'dateString' be a string, found: %s with value %s",
												BsonTypeName(dateString->value_type),
												BsonValueToJsonForLogging(dateString)),
											errdetail_log(
												"$dateFromString requires that 'dateString' be a string, found: %s",
												BsonTypeName(dateString->value_type)))));
		*isInputValid = false;
		return;
	}

	if (isFormatSpecified && format->value_type != BSON_TYPE_UTF8)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40684), errmsg(
							"$dateFromString requires that 'format' be a string, found: %s with value %s",
							BsonTypeName(format->value_type), BsonValueToJsonForLogging(
								format)),
						errdetail_log(
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

	if (isFormatSpecified)
	{
		VerifyAndParseFormatStringToParts(dateString, format->value.v_utf8.str,
										  dateFromParts,
										  isInputValid,
										  isOnErrorPresent, false);
	}
	else
	{
		/* try with preset format */
		int presetDateFormatParserSize = sizeof(presetDateFormatParser) /
										 sizeof(DateFormatParser);
		for (int i = 0; i < presetDateFormatParserSize; i++)
		{
			/* use date string length to filter out unnecessary attampts */
			int dateStrLen = strlen(dateString->value.v_utf8.str);
			if (dateStrLen < presetDateFormatParser[i].minLen ||
				dateStrLen > presetDateFormatParser[i].maxLen)
			{
				continue;
			}

			/* If it is not the last try, we won't throw error */
			bool isNotLastTry = i != presetDateFormatParserSize - 1;
			*isInputValid = true;
			DollarDateFromPartsBsonValue *tmpDateFromParts = palloc0(
				sizeof(DollarDateFromPartsBsonValue));
			tmpDateFromParts->timezone = dateFromParts->timezone;
			VerifyAndParseFormatStringToParts(dateString,
											  presetDateFormatParser[i].format,
											  tmpDateFromParts, isInputValid,
											  isOnErrorPresent, isNotLastTry);
			if (*isInputValid)
			{
				*dateFromParts = *tmpDateFromParts;
				pfree(tmpDateFromParts);
				break;
			}
			pfree(tmpDateFromParts);
		}
	}

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


static inline int
GetCurrentCentury()
{
	TimestampTz now = GetCurrentTimestamp();
	struct pg_tm tm;
	fsec_t fsec;

	if (timestamp2tm(now, NULL, &tm, &fsec, NULL, NULL) == 0)
	{
		return (tm.tm_year / 100) * 100;
	}

	/* If we can't get the current year, return a default value. */
	return 2000;
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


static void
VerifyAndParseFormatStringToParts(bson_value_t *dateString, char *format,
								  DollarDateFromPartsBsonValue *dateFromParts,
								  bool *isInputValid, bool isOnErrorPresent, bool
								  tryWithPresetFormat)
{
	char *rawDateString = dateString->value.v_utf8.str;
	char *date = dateString->value.v_utf8.str;

	/* at most N parts is needed where N = strlen(format) / 2 */
	RawDatePart *parts = (RawDatePart *) palloc0(sizeof(RawDatePart) * (strlen(format) /
																		2));
	int partsCount = 0;
	bool dateStrIsEnough = true;

	while (*format != '\0')
	{
		if (*date == '\0')
		{
			dateStrIsEnough = false;
			break;
		}
		if (*format == '%')
		{
			/* If the format string ends with % */
			if (*(format + 1) == '\0')
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION18536), errmsg(
									"End with '%%' in format string"),
								errdetail_log(
									"Invalid format character in format string")));
			}

			char specifier[3] = { *format, *(format + 1), '\0' };
			int fmtSpecifierIndex = IsFormatSpecifierExist(specifier);

			/* Format specifier is not found in the list of supported format specifiers. */
			if (fmtSpecifierIndex == -1)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION18536), errmsg(
									"Invalid format character '%s' in format string",
									specifier),
								errdetail_log(
									"Invalid format character in format string")));
			}

			int minCharsToRead = dateFormats[fmtSpecifierIndex].minLen;
			int maxCharsToRead = dateFormats[fmtSpecifierIndex].maxLen;

			/* read date snip */
			char *rawDateElement = (char *) palloc0((maxCharsToRead + 1) * sizeof(char));
			char stopChar = *(format + 2);

			int readLength = 0;
			while (*date != '\0' && readLength < maxCharsToRead)
			{
				if (*date == stopChar)
				{
					break;
				}
				rawDateElement[readLength] = *date;
				readLength++;
				date++;
			}
			rawDateElement[readLength] = '\0';

			parts[partsCount].formatMap = &dateFormats[fmtSpecifierIndex];
			parts[partsCount].rawString = rawDateElement;
			partsCount++;

			format += 2;

			/* we put this check here to ensure that rawDateElement has been assigned to parts */
			/* so that its memory could be released at the end of this func */
			if (readLength < minCharsToRead)
			{
				dateStrIsEnough = false;
				break;
			}
		}
		else
		{
			if (*date != *format)
			{
				CONDITIONAL_EREPORT(isOnErrorPresent && !tryWithPresetFormat, ereport(
										ERROR, (errcode(
													ERRCODE_DOCUMENTDB_CONVERSIONFAILURE),
												errmsg(
													"Error parsing date string '%s'. Format literal not found '%c'",
													rawDateString, *date),
												errdetail_log(
													"Error parsing date string '%s'. Format literal not found '%c'",
													rawDateString, *date))));
				*isInputValid = false;
				return;
			}
			date++;
			format++;
		}
	}

	/* date used up but format has more characters*/
	if (!dateStrIsEnough)
	{
		CONDITIONAL_EREPORT(isOnErrorPresent && !tryWithPresetFormat, ereport(ERROR,
																			  (errcode(
																				   ERRCODE_DOCUMENTDB_CONVERSIONFAILURE),
																			   errmsg(
																				   "Error parsing date string '%s': Not enough data available to satisfy format",
																				   rawDateString),
																			   errdetail_log(
																				   "Error parsing date string. Not enough data available to satisfy format"))));
		*isInputValid = false;
		return;
	}

	/* format used up but date has more characters */
	if (*date != '\0')
	{
		CONDITIONAL_EREPORT(isOnErrorPresent && !tryWithPresetFormat, ereport(ERROR,
																			  (errcode(
																				   ERRCODE_DOCUMENTDB_CONVERSIONFAILURE),
																			   errmsg(
																				   "Error parsing date string '%s'. Trailing data '%c'",
																				   rawDateString,
																				   *date),
																			   errdetail_log(
																				   "Error parsing date string '%s'. Trailing data '%c'",
																				   rawDateString,
																				   *date))));
		*isInputValid = false;
		return;
	}

	/* validate format itself */
	if (partsCount == 0)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION18536), errmsg(
							"format is invalid"), errdetail_log("format is invalid")));
	}

	/* validate whether format is mixed */
	bool isIsoFormat = parts[0].formatMap->isIsoFormat;
	for (int i = 1; i < partsCount; i++)
	{
		if (parts[i].formatMap->isIsoFormat != isIsoFormat)
		{
			CONDITIONAL_EREPORT(isOnErrorPresent, ereport(ERROR, (errcode(
																	  ERRCODE_DOCUMENTDB_CONVERSIONFAILURE),
																  errmsg(
																	  "Error parsing date string '%s'; Mixing of ISO dates with natural dates is not allowed",
																	  rawDateString),
																  errdetail_log(
																	  "Error parsing date string; Mixing of ISO dates with natural dates is not allowed"))));
			*isInputValid = false;
			return;
		}
	}

	/* validate timezone conflict */
	bool isTimezonePresent = dateFromParts->timezone.value_type != BSON_TYPE_EOD;
	if (isTimezonePresent)
	{
		for (int i = 0; i < partsCount; i++)
		{
			if (parts[i].formatMap->datePart == DatePart_Str_Timezone_Offset ||
				parts[i].formatMap->datePart == DatePart_Str_Minutes_Offset)
			{
				bool isGMTOffset = parts[i].formatMap->datePart ==
								   DatePart_Str_Minutes_Offset || strchr(
					parts[i].rawString, '+') || strchr(
					parts[i].rawString, '-');

				if (isGMTOffset)
				{
					CONDITIONAL_EREPORT(isOnErrorPresent, ereport(ERROR, (errcode(
																			  ERRCODE_DOCUMENTDB_CONVERSIONFAILURE),
																		  errmsg(
																			  "you cannot pass in a date/time string with GMT offset together with a timezone argument"),
																		  errdetail_log(
																			  "you cannot pass in a date/time string with GMT offset together with a timezone argument"))));
				}
				else
				{
					CONDITIONAL_EREPORT(isOnErrorPresent, ereport(ERROR, (errcode(
																			  ERRCODE_DOCUMENTDB_CONVERSIONFAILURE),
																		  errmsg(
																			  "you cannot pass in a date/time string with time zone information ('%s') together with a timezone argument",
																			  parts[i].
																			  rawString),
																		  errdetail_log(
																			  "you cannot pass in a date/time string with time zone information together with a timezone argument"))));
				}

				*isInputValid = false;
				return;
			}
		}
	}

	/* parse each part into dateFromParts */
	for (int i = 0; i < partsCount; i++)
	{
		bool isPartValid = parts[i].formatMap->validateAndParseFunc(parts[i].rawString,
																	parts[i].formatMap,
																	dateFromParts);
		if (!isPartValid)
		{
			*isInputValid = false;
		}
	}

	/* check required parts are present */
	CheckIfRequiredPartsArePresent(dateFromParts, dateString->value.v_utf8.str,
								   isInputValid, isOnErrorPresent);

	/* free parts */
	for (int i = 0; i < partsCount; i++)
	{
		pfree(parts[i].rawString);
	}
	pfree(parts);
}


static bool
ValidateAndParseDigits(char *rawString, const DateFormatMap *map,
					   DollarDateFromPartsBsonValue *dateFromParts)
{
	if (rawString == NULL)
	{
		return false;
	}

	int len = strlen(rawString);

	if (len < map->minLen || len > map->maxLen)
	{
		return false;
	}

	for (int i = 0; i < len; i++)
	{
		if (!isdigit(rawString[i]))
		{
			return false;
		}
	}

	int value = atoi(rawString);
	if (value < map->minRangeValue || value > map->maxRangeValue)
	{
		return false;
	}

	bson_value_t dateElementBsonValue;
	dateElementBsonValue.value_type = BSON_TYPE_INT32;
	dateElementBsonValue.value.v_int32 = value;

	switch (map->datePart)
	{
		case DatePart_Year:
		{
			dateFromParts->year = dateElementBsonValue;
			break;
		}

		case DatePart_Month:
		{
			dateFromParts->month = dateElementBsonValue;
			break;
		}

		case DatePart_DayOfMonth:
		{
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
			dateFromParts->isoWeek = dateElementBsonValue;
			dateFromParts->isIsoFormat = true;
			break;
		}

		case DatePart_IsoWeekYear:
		{
			dateFromParts->isoWeekYear = dateElementBsonValue;
			dateFromParts->isIsoFormat = true;
			break;
		}

		case DatePart_IsoDayOfWeek:
		{
			dateFromParts->isoDayOfWeek = dateElementBsonValue;
			dateFromParts->isIsoFormat = true;
			break;
		}

		case DatePart_DayOfYear:
		{
			dateFromParts->dayOfYear = dateElementBsonValue;
			break;
		}

		default:
		{
			break;
		}
	}

	return true;
}


static bool
ValidateAndParseStrMonth(char *rawString, const DateFormatMap *map,
						 DollarDateFromPartsBsonValue *dateFromParts)
{
	if (rawString == NULL)
	{
		return false;
	}

	int month = GetMonthIndexFromString(rawString, false);
	if (month == -1)
	{
		return false;
	}
	dateFromParts->month.value.v_int32 = month;
	dateFromParts->month.value_type = BSON_TYPE_INT32;
	return true;
}


static bool
ValidateAndParsePlaceholderPercent(char *rawString, const DateFormatMap *map,
								   DollarDateFromPartsBsonValue *dateFromParts)
{
	if (rawString == NULL)
	{
		return false;
	}

	int len = strlen(rawString);
	if (len != 1 || *rawString != '%')
	{
		return false;
	}
	return true;
}


static bool
ValidateAndParseAbbrStrMonth(char *rawString, const DateFormatMap *map,
							 DollarDateFromPartsBsonValue *dateFromParts)
{
	if (rawString == NULL)
	{
		return false;
	}

	int month = GetMonthIndexFromString(rawString, true);
	if (month == -1)
	{
		return false;
	}
	dateFromParts->month.value.v_int32 = month;
	dateFromParts->month.value_type = BSON_TYPE_INT32;
	return true;
}


static bool
ValidateAndParseStrHour(char *rawString, const DateFormatMap *map,
						DollarDateFromPartsBsonValue *dateFromParts)
{
	if (rawString == NULL)
	{
		return false;
	}

	int len = strlen(rawString);
	if (len < map->minLen || len > map->maxLen)
	{
		return false;
	}

	if (strcasecmp(rawString, "noon") == 0)
	{
		dateFromParts->hour.value.v_int32 = 12;
		dateFromParts->hour.value_type = BSON_TYPE_INT32;
		return true;
	}

	int hour = 0;
	if (len == 3)
	{
		if (isdigit(rawString[0]))
		{
			hour = rawString[0] - '0';
		}
		else
		{
			return false;
		}
	}
	else
	{
		if (isdigit(rawString[0]) && isdigit(rawString[1]))
		{
			hour = (rawString[0] - '0') * 10 + (rawString[1] - '0');
		}
		else
		{
			return false;
		}
	}

	if (hour < 1 || hour > 12)
	{
		return false;
	}


	const char *suffix = rawString + len - 2;
	if (strcasecmp(suffix, "am") != 0 && strcasecmp(suffix, "pm") != 0)
	{
		return false;
	}
	if (strcasecmp(suffix, "pm") == 0 && hour < 12)
	{
		hour += 12;
	}

	dateFromParts->hour.value.v_int32 = hour;
	dateFromParts->hour.value_type = BSON_TYPE_INT32;
	return true;
}


static bool
ValidateAndParseMinOffset(char *rawString, const DateFormatMap *map,
						  DollarDateFromPartsBsonValue *dateFromParts)
{
	if (rawString == NULL)
	{
		return false;
	}

	if (*rawString != '+' && *rawString != '-')
	{
		return false;
	}
	char sign = *rawString;
	rawString++;

	char *endptr;
	long totalMins = strtol(rawString, &endptr, 10);

	if (*endptr != '\0')
	{
		return false;
	}

	if (totalMins > MaxMinsRepresentedByUTCOffset)
	{
		return false;
	}

	int hours = totalMins / 60;
	int minutes = totalMins % 60;

	char *hoursAndMinsFmt = (char *) palloc0(7 * sizeof(char));
	sprintf(hoursAndMinsFmt, "%c%02d:%02d", sign, hours, minutes);

	dateFromParts->timezone.value_type = BSON_TYPE_UTF8;
	dateFromParts->timezone.value.v_utf8.str = hoursAndMinsFmt;
	dateFromParts->timezone.value.v_utf8.len = strlen(hoursAndMinsFmt);

	return true;
}


static bool
ValidateAndParseTwoDigitsYear(char *rawString, const DateFormatMap *map,
							  DollarDateFromPartsBsonValue *dateFromParts)
{
	if (rawString == NULL)
	{
		return false;
	}

	int len = strlen(rawString);
	if (len != 2)
	{
		return false;
	}

	int century = GetCurrentCentury();
	int year = atoi(rawString);
	if (year < 0 || year > 99)
	{
		return false;
	}

	/* we use pivot year rule here */
	if (year < 70)
	{
		year += century;
	}
	else
	{
		year += (century - 100);
	}

	dateFromParts->year.value.v_int32 = year;
	dateFromParts->year.value_type = BSON_TYPE_INT32;
	return true;
}


static bool
ValidateAndParseStrDayOfMonth(char *rawString, const DateFormatMap *map,
							  DollarDateFromPartsBsonValue *dateFromParts)
{
	if (rawString == NULL)
	{
		return false;
	}

	int len = strlen(rawString);
	if (len < map->minLen || len > map->maxLen)
	{
		return false;
	}

	int day = -1;

	if (strcasecmp(rawString, "1st") == 0)
	{
		day = 1;
	}
	if (strcasecmp(rawString, "2nd") == 0)
	{
		day = 2;
	}
	if (strcasecmp(rawString, "3rd") == 0)
	{
		day = 3;
	}
	if (strcasecmp(rawString, "21st") == 0)
	{
		day = 21;
	}
	if (strcasecmp(rawString, "22nd") == 0)
	{
		day = 22;
	}
	if (strcasecmp(rawString, "23rd") == 0)
	{
		day = 23;
	}
	if (strcasecmp(rawString, "31st") == 0)
	{
		day = 31;
	}

	if (day != -1)
	{
		dateFromParts->day.value.v_int32 = day;
		dateFromParts->day.value_type = BSON_TYPE_INT32;
		return true;
	}

	char *number_part = palloc0(sizeof(char) * (len + 1));
	int number_part_len = 0;
	while (isdigit(*rawString))
	{
		number_part[number_part_len++] = *rawString;
		rawString++;
	}

	if (strcasecmp(rawString, "th") != 0)
	{
		return false;
	}

	day = atoi(number_part);
	if (day < 1 || day > 31 || day == 1 || day == 2 || day == 3 || day == 21 || day ==
		22 || day == 23 || day == 31)
	{
		return false;
	}
	pfree(number_part);

	dateFromParts->day.value.v_int32 = day;
	dateFromParts->day.value_type = BSON_TYPE_INT32;
	return true;
}


static bool
ValidateAndParseTimezoneOffset(char *rawString, const DateFormatMap *map,
							   DollarDateFromPartsBsonValue *dateFromParts)
{
	if (rawString == NULL)
	{
		return false;
	}

	/* verify length of rawString for timezone */
	int len = strlen(rawString);
	if (len < map->minLen)
	{
		return false;
	}

	/* trim leading space */
	while (isspace(*rawString))
	{
		rawString++;
	}

	/* check whether timezone is an abbr */
	char *offset = GetOffsetFromTimezoneAbbr(rawString);
	if (offset != NULL)
	{
		dateFromParts->timezone.value.v_utf8.str = offset;
		dateFromParts->timezone.value.v_utf8.len = strlen(offset);
		dateFromParts->timezone.value_type = BSON_TYPE_UTF8;
		return true;
	}

	/* check whether timezone is format like GMT+/-[hh]:[mm]*/
	const char *gmt = "GMT";
	if (strncmp(rawString, gmt, strlen(gmt)) == 0)
	{
		/* Skip GMT and process offset afterwards as the case that timezone is exact 'GMT' is already handled before */
		rawString += 3;
	}

	/**
	 * no matter the remaining string is like +05:30 or other string, we can just leave it as it is
	 * and let ParseTimezone() to handle it.
	 * We need to copy the remaining string to a new memory location as the rawString will be released after parse.
	 */
	char *timezone = palloc0(sizeof(char) * (strlen(rawString) + 1));
	strcpy(timezone, rawString);
	dateFromParts->timezone.value.v_utf8.str = timezone;
	dateFromParts->timezone.value.v_utf8.len = strlen(timezone);
	dateFromParts->timezone.value_type = BSON_TYPE_UTF8;
	return true;
}
