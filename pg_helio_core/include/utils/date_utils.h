
/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/utils/date_utils.h
 *
 * Definitions for utilities related to dates and Mongo Date types.
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>

#ifndef DATE_UTILS_H
#define DATE_UTILS_H

#define MILLISECONDS_IN_SECOND 1000L

static const char *dateUnitStr[9] = {
	"year", "quarter", "month", "week", "day", "hour", "minute", "second", "millisecond"
};

/* Enum which defines the possible units which mongo supports for BSON date types */
typedef enum DateUnit
{
	DateUnit_Invalid = 0,
	DateUnit_Year = 1,
	DateUnit_Quarter = 2,
	DateUnit_Month = 3,
	DateUnit_Week = 4,
	DateUnit_Day = 5,
	DateUnit_Hour = 6,
	DateUnit_Minute = 7,
	DateUnit_Second = 8,
	DateUnit_Millisecond = 9
} DateUnit;


/*
 * This function given an input string value returns DateUnit enum corresponding to that str value.
 */
static inline DateUnit
GetDateUnitFromString(char *unitStrValue)
{
	DateUnit unitEnum = DateUnit_Invalid;
	for (int index = 0; index < (int) (sizeof(dateUnitStr) / sizeof(dateUnitStr[0]));
		 index++)
	{
		if (strcmp(dateUnitStr[index], unitStrValue) == 0)
		{
			unitEnum = (DateUnit) (index + 1);
			return unitEnum;
		}
	}
	return unitEnum;
}


/* A function to create postgres interval object given the amount of year, month, week, day, hour, second, millis. */
static inline Datum
GetIntervalFromDatePart(int64 year, int64 month, int64 week, int64 day, int64 hour, int64
						minute, int64 second, int64 millis)
{
	float8 secondsAdjustedWithMillis = second + (((float8) millis) /
												 MILLISECONDS_IN_SECOND);
	return DirectFunctionCall7(make_interval,
							   Int64GetDatum(year), Int64GetDatum(month),
							   Int64GetDatum(week), Int64GetDatum(day),
							   Int64GetDatum(hour), Int64GetDatum(minute),
							   Float8GetDatum(secondsAdjustedWithMillis));
}


/*
 * This function returns a datum interval taking in the date unit and amount.
 * Interval is one of the postgres way of referencing time interval.
 */
static inline Datum
GetIntervalFromDateUnitAndAmount(DateUnit unitEnum, int64 amount)
{
	int64 year = 0, month = 0, week = 0, day = 0, hour = 0, minute = 0, seconds = 0,
		  millis = 0;
	switch (unitEnum)
	{
		case DateUnit_Year:
		{
			year = amount;
			break;
		}

		case DateUnit_Quarter:
		{
			month = amount * 3;
			break;
		}

		case DateUnit_Month:
		{
			month = amount;
			break;
		}

		case DateUnit_Week:
		{
			week = amount;
			break;
		}

		case DateUnit_Day:
		{
			day = amount;
			break;
		}

		case DateUnit_Hour:
		{
			hour = amount;
			break;
		}

		case DateUnit_Minute:
		{
			minute = amount;
			break;
		}

		case DateUnit_Second:
		{
			seconds = amount;
			break;
		}

		case DateUnit_Millisecond:
		{
			millis = amount;
			break;
		}

		default:
		{
			break;
		}
	}

	return GetIntervalFromDatePart(year, month, week, day, hour, minute, seconds, millis);
}


/* Given a Unix epoch in milliseconds returns a Datum containing a postgres Timestamp instance. */
static inline Datum
GetPgTimestampFromUnixEpoch(int64_t epochInMs)
{
	float8 seconds = ((float8) epochInMs) / MILLISECONDS_IN_SECOND;

	/*
	 * This is a defensive check as float8_timestamptz throws error for this and we can pre-check if this range of seconds is valid
	 * This check is similar to check in the file udt/adt/timestamp.c
	 */
	if (seconds < (float8) SECS_PER_DAY * (DATETIME_MIN_JULIAN - UNIX_EPOCH_JDATE) ||
		seconds >= (float8) SECS_PER_DAY * (TIMESTAMP_END_JULIAN - UNIX_EPOCH_JDATE))
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_OVERFLOW),
						errmsg("Invalid conversion to date time.")));
	}

	return DirectFunctionCall1(
		float8_timestamptz,
		Float8GetDatum(seconds));
}


#endif
