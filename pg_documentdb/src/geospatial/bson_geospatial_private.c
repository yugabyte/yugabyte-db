/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/geospatial/bson_geospatial_private.c
 *
 * Implementation for method for private utilities for geospatial e.g. Well Know Binary utilities etc
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "math.h"
#include "lib/stringinfo.h"

#include "io/helio_bson_core.h"
#include "geospatial/bson_geospatial_common.h"
#include "geospatial/bson_geospatial_private.h"
#include "geospatial/bson_geojson_utils.h"
#include "utils/helio_errors.h"


static bool ParseBsonValueAsLegacyPointInternal(const bson_value_t *pointValue,
												bool throwError, bool checkLongLatBounds,
												GeospatialErrorContext *errCtxt,
												Point *outPoint);

/*
 * Given a bson vlaue, parse it as legacy point
 */
bool
ParseBsonValueAsPoint(const bson_value_t *value, bool throwError,
					  GeospatialErrorContext *errCtxt, Point *outPoint)
{
	bool checkLongLatBounds = false;
	return ParseBsonValueAsLegacyPointInternal(value, throwError, checkLongLatBounds,
											   errCtxt, outPoint);
}


/*
 * Given a bson vlaue, parse it as legacy point with longitude latitude checks.
 */
bool
ParseBsonValueAsPointWithBounds(const bson_value_t *value, bool throwError,
								GeospatialErrorContext *errCtxt, Point *outPoint)
{
	bool checkLongLatBounds = true;
	return ParseBsonValueAsLegacyPointInternal(value, throwError,
											   checkLongLatBounds,
											   errCtxt, outPoint);
}


/*
 * Parses the values and gets WKB representation of the geometry value
 */
bool
BsonValueGetGeometryWKB(const bson_value_t *value,
						const GeometryParseFlag parseFlag,
						GeoJsonParseState *parseState)
{
	Assert(value != NULL && parseFlag != ParseFlag_None && parseState->buffer != NULL);
	bool shouldThrowError = parseState->shouldThrowValidityError;

	StringInfo wkbBuffer = parseState->buffer;

	bool isValid = false;

	/* Check if we need to parse this as legacy point */
	if ((parseFlag & ParseFlag_Legacy) == ParseFlag_Legacy ||
		(parseFlag & ParseFlag_Legacy_NoError) == ParseFlag_Legacy_NoError)
	{
		Point point;
		memset(&point, 0, sizeof(Point));

		/* Suppress error if needed */
		bool shouldThrowErrorInner = (parseFlag & ParseFlag_Legacy_NoError) ==
									 ParseFlag_Legacy_NoError ?
									 false : shouldThrowError;
		isValid = ParseBsonValueAsPoint(value, shouldThrowErrorInner,
										parseState->errorCtxt, &point);
		if (isValid)
		{
			WriteHeaderToWKBBuffer(wkbBuffer, WKBGeometryType_Point);
			WritePointToWKBBuffer(wkbBuffer, &point);

			/* Consider legacy point as GeoJSON point */
			parseState->type = GeoJsonType_POINT;
		}
	}

	/* Check if we need to parse this as any GeoJSON type */
	if (!isValid && ((parseFlag & ParseFlag_GeoJSON_All) == ParseFlag_GeoJSON_All))
	{
		parseState->expectedType = GeoJsonType_ALL;
		isValid = ParseValueAsGeoJSON(value, parseState);
	}

	/* Check if we need to parse this as GeoJSON Point alone */
	if (!isValid && ((parseFlag & ParseFlag_GeoJSON_Point) == ParseFlag_GeoJSON_Point))
	{
		parseState->expectedType = GeoJsonType_POINT;
		isValid = ParseValueAsGeoJSON(value, parseState);
	}

	return isValid;
}


/* ================= Private Helpers ===================*/


/*
 * Given a bson value, parse it as a legacy point coordinate pair.
 * e.g. {a: 10, b: 10} or [10, 10]
 *
 * If there are more than 2 values then those are ignored, the
 * resulting
 *
 * @param throwError : returns error only when `throrError` is true
 * @param checkLongLatBounds : Additionally checks for longitude and latitude bounds
 */
static bool
ParseBsonValueAsLegacyPointInternal(const bson_value_t *pointValue,
									bool throwError, bool checkLongLatBounds,
									GeospatialErrorContext *errCtxt,
									Point *outPoint)
{
	if (pointValue->value_type != BSON_TYPE_ARRAY &&
		pointValue->value_type != BSON_TYPE_DOCUMENT)
	{
		RETURN_FALSE_IF_ERROR_NOT_EXPECTED(
			throwError, (
				errcode(GEO_ERROR_CODE(errCtxt)),
				errmsg("%sPoint must be an array or object",
					   GEO_ERROR_PREFIX(errCtxt)),
				errdetail_log("%sPoint must be an array or object",
							  GEO_HINT_PREFIX(errCtxt))));
	}

	if (IsBsonValueEmptyArray(pointValue) || IsBsonValueEmptyDocument(pointValue))
	{
		RETURN_FALSE_IF_ERROR_NOT_EXPECTED(
			throwError, (
				errcode(GEO_ERROR_CODE(errCtxt)),
				errmsg("%sPoint must only contain numeric elements",
					   GEO_ERROR_PREFIX(errCtxt)),
				errdetail_log("%sPoint must only contain numeric elements",
							  GEO_HINT_PREFIX(errCtxt))));
	}

	bson_iter_t pointValueIter;
	BsonValueInitIterator(pointValue, &pointValueIter);
	int index = 0;

	/* Ignore values other than first 2 */
	while (index < 2 && bson_iter_next(&pointValueIter))
	{
		const bson_value_t *value = bson_iter_value(&pointValueIter);
		if (!BsonTypeIsNumber(value->value_type))
		{
			RETURN_FALSE_IF_ERROR_NOT_EXPECTED(
				throwError, (
					errcode(GEO_ERROR_CODE(errCtxt)),
					errmsg("%sPoint must only contain numeric elements",
						   GEO_ERROR_PREFIX(errCtxt)),
					errdetail_log("%sPoint must only contain numeric elements",
								  GEO_HINT_PREFIX(errCtxt))));
		}

		/* Get double degrees in quiet mode and check for finite number */
		double doubleValue = BsonValueAsDoubleQuiet(value);

		if (!isfinite(doubleValue))
		{
			RETURN_FALSE_IF_ERROR_NOT_EXPECTED(
				throwError, (
					errcode(GEO_ERROR_CODE(errCtxt)),
					errmsg("%sPoint coordinates must be finite numbers",
						   GEO_ERROR_PREFIX(errCtxt)),
					errdetail_log("%sPoint coordinates must be finite numbers",
								  GEO_HINT_PREFIX(errCtxt))));
		}

		if (index == 0)
		{
			outPoint->x = doubleValue;
		}
		else
		{
			outPoint->y = doubleValue;
		}

		index++;
	}
	if (index < 2)
	{
		RETURN_FALSE_IF_ERROR_NOT_EXPECTED(
			throwError, (
				errcode(GEO_ERROR_CODE(errCtxt)),
				errmsg("%sPoint must only contain numeric elements",
					   GEO_ERROR_PREFIX(errCtxt)),
				errdetail_log("%sPoint must only contain numeric elements",
							  GEO_HINT_PREFIX(errCtxt))));
	}

	if (checkLongLatBounds)
	{
		if (outPoint->x < -180.0 || outPoint->x > 180.0 || outPoint->y < -90.0 ||
			outPoint->y > 90.0)
		{
			RETURN_FALSE_IF_ERROR_NOT_EXPECTED(
				throwError, (
					errcode(GEO_ERROR_CODE(errCtxt)),
					errmsg("%slongitude/latitude is out of bounds, lng: %g lat: %g",
						   GEO_ERROR_PREFIX(errCtxt),
						   outPoint->x, outPoint->y),
					errdetail_log("%slongitude/latitude is out of bounds.",
								  GEO_HINT_PREFIX(errCtxt))));
		}
	}

	return true;
}
