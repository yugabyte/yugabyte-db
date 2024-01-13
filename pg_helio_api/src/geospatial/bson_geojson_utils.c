/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/geospatial/bson_geojson_utils.c
 *
 * Implementation for utilities used to work with GeoJSON type
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"
#include "funcapi.h"
#include "math.h"
#include "miscadmin.h"
#include "common/hashfn.h"
#include "lib/stringinfo.h"
#include "utils/array.h"
#include "utils/builtins.h"
#include "utils/hsearch.h"
#include "utils/typcache.h"


#include "utils/mongo_errors.h"
#include "geospatial/bson_geojson_utils.h"
#include "utils/list_utils.h"
#include "utils/hashset_utils.h"


/* Hash entry for points to track duplicate points */
typedef struct PointsHashEntry
{
	/* Points array containing 2 points */
	double points[2];

	/* Index of the point in the point array */
	uint32_t index;
} PointsHashEntry;

static const char * GeoJsonTypeName(GeoJsonType type);
static GeoJsonType GetGeoJsonType(const bson_value_t *value);
static bool WriteBufferGeoJsonCore(const bson_value_t *value,
								   bool insideGeoJsonGeometryCollection,
								   StringInfo goeJsonWKB,
								   GeoJsonParseState *parseState);
static bool WriteBufferGeoJsonCoordinates(const bson_value_t *value,
										  GeoJsonType geoJsonType,
										  bool insideGeometryCollection,
										  StringInfo geoJsonWKT,
										  GeoJsonParseState *parseState);
static bool WriteBufferGeoJsonMultiPoints(const bson_value_t *value,
										  const GeoJsonType type,
										  StringInfo geoJsonWKT,
										  GeoJsonParseState *state);
static bool ValidateCoordinatesNotArray(const bson_value_t *coordinatesValue,
										GeoJsonType geoJsonType,
										GeoJsonParseState *parseState);
static bool AdditionalPolygonValidation(StringInfo geoJsonWKB,
										GeoJsonParseState *parseState,
										int totalRings);

/* HashSet utilities for Points */
static HTAB * CreatePointsHashSet(void);
static int PointsHashEntryCompareFunc(const void *obj1, const void *obj2, Size objsize);
static uint32 PointsHashEntryHashFunc(const void *obj, Size objsize);


/*
 * Parses the GeoJSON value and builds the respective Geometry WKB in the buffer provided in parseState.
 *
 * The `parseState` is also filled with some out variables e.g. the type of GeoJson found or number of rings found for a
 * Polygon which can help in deciding and validating behaviors e.g. $geometry in $geoIntersection can work with any geoJsonType but
 * $geometry in $geoWithin does only work with 'Polygon' or 'MultiPolygon'
 */
bool
ParseValueAsGeoJSON(const bson_value_t *value,
					GeoJsonParseState *parseState)
{
	bool insideGeoJsonGeometryCollection = false;
	bool isValid = WriteBufferGeoJsonCore(value, insideGeoJsonGeometryCollection,
										  parseState->buffer, parseState);


	/* Return false for validity if there is an error */
	return isValid;
}


/********************/
/* Private helpers  */
/********************/

/*
 * WriteBufferGeoJsonCoordinates validates the "coordinates" bson value against the geoJson `type`
 * Note: For `GeometryCollection` coordinates are actually values of 'geometries'
 * e.g.:
 * { "type": "Polygon", "coordinates": [[...]]}
 * { "type": "GeometryCollection", "geometries": [{type: "Polygon": coordinates: [...] }]}
 */
static bool
WriteBufferGeoJsonCoordinates(const bson_value_t *coordinatesValue,
							  GeoJsonType geoJsonType,
							  bool insideGeometryCollection, StringInfo geoJsonWKB,
							  GeoJsonParseState *parseState)
{
	check_stack_depth();
	CHECK_FOR_INTERRUPTS();

	bool shouldThrowError = parseState->shouldThrowValidityError;

	/* Top level validations if coordinates / geometries not an array */
	bool isValid = ValidateCoordinatesNotArray(coordinatesValue, geoJsonType, parseState);
	if (!isValid)
	{
		return false;
	}

	bson_iter_t coordinatesIter;
	BsonValueInitIterator(coordinatesValue, &coordinatesIter);

	switch (geoJsonType)
	{
		case GeoJsonType_POINT:
		{
			Point point;
			memset(&point, 0, sizeof(Point));

			isValid = ParseBsonValueAsPointWithBounds(coordinatesValue, shouldThrowError,
													  parseState->errorCtxt, &point);
			if (!isValid)
			{
				return false;
			}

			/* Valid point, write to buffer */
			WriteHeaderToWKBBuffer(geoJsonWKB, WKBGeometryType_Point);
			WritePointToWKBBuffer(geoJsonWKB, &point);
			break;
		}

		case GeoJsonType_LINESTRING:
		{
			WriteHeaderToWKBBuffer(geoJsonWKB, WKBGeometryType_LineString);
			isValid = WriteBufferGeoJsonMultiPoints(coordinatesValue, geoJsonType,
													geoJsonWKB,
													parseState);
			if (!isValid)
			{
				return false;
			}
			break;
		}

		case GeoJsonType_POLYGON:
		{
			if (IsBsonValueEmptyArray(coordinatesValue))
			{
				RETURN_FALSE_IF_ERROR_NOT_EXPECTED(
					shouldThrowError, (
						errcode(GEO_ERROR_CODE(parseState->errorCtxt)),
						errmsg("%sPolygon has no loops.",
							   GEO_ERROR_PREFIX(parseState->errorCtxt)),
						errhint("%sPolygon has no loops.",
								GEO_HINT_PREFIX(parseState->errorCtxt))));
			}

			int32 totalRings = 0;

			/*
			 * We create a separate buffer for polygon's WKB because we need additional
			 * validations on polygon alone. First 4 bytes are left for varlena header
			 */
			StringInfo polygonWKB = makeStringInfo();
			polygonWKB->len = VARHDRSZ;

			WriteHeaderToWKBBuffer(polygonWKB, WKBGeometryType_Polygon);

			/* Leave the space for num of rings which will be filled later after we are done looping the array */
			uint8 *numOfRings = WKBBufferAppend4EmptyBytes(polygonWKB);

			while (bson_iter_next(&coordinatesIter))
			{
				const bson_value_t *lineStringValue = bson_iter_value(&coordinatesIter);

				/* To match mongo error reporting for case when ring value is not array we validate it agains Linestring */
				isValid = ValidateCoordinatesNotArray(lineStringValue,
													  GeoJsonType_LINESTRING,
													  parseState);
				if (!isValid)
				{
					return false;
				}

				/* Parse and collect multiple points of the buffer */
				isValid = WriteBufferGeoJsonMultiPoints(lineStringValue, geoJsonType,
														polygonWKB,
														parseState);
				if (!isValid)
				{
					return false;
				}
				totalRings++;
			}

			/* Fill the number of rings found */
			WriteNumToWKBBuffer(numOfRings, totalRings);

			/* First append the polygon buffer into the main WKB for geoJson geometry */
			WriteStringInfoBufferToWKBBufferWithOffset(geoJsonWKB, polygonWKB, VARHDRSZ);

			/* addition polygon validity tests */
			isValid = AdditionalPolygonValidation(polygonWKB, parseState, totalRings);
			if (!isValid)
			{
				return false;
			}

			break;
		}

		case GeoJsonType_MULTIPOINT:
		{
			if (IsBsonValueEmptyArray(coordinatesValue))
			{
				RETURN_FALSE_IF_ERROR_NOT_EXPECTED(
					shouldThrowError, (
						errcode(GEO_ERROR_CODE(parseState->errorCtxt)),
						errmsg("%sMultiPoint coordinates must have at least 1 element",
							   GEO_ERROR_PREFIX(parseState->errorCtxt)),
						errhint("%sMultiPoint coordinates must have at least 1 element",
								GEO_HINT_PREFIX(parseState->errorCtxt))));
			}

			int32 totalPoints = 0;
			WriteHeaderToWKBBuffer(geoJsonWKB, WKBGeometryType_MultiPoint);
			uint8 *numOfPoints = WKBBufferAppend4EmptyBytes(geoJsonWKB);

			while (bson_iter_next(&coordinatesIter))
			{
				const bson_value_t *pointValue = bson_iter_value(&coordinatesIter);
				if (pointValue->value_type != BSON_TYPE_ARRAY)
				{
					RETURN_FALSE_IF_ERROR_NOT_EXPECTED(
						shouldThrowError, (
							errcode(GEO_ERROR_CODE(parseState->errorCtxt)),
							errmsg("%sGeoJSON coordinates must be an array",
								   GEO_ERROR_PREFIX(parseState->errorCtxt)),
							errhint("%sGeoJSON coordinates must be an array",
									GEO_HINT_PREFIX(parseState->errorCtxt))));
				}
				isValid = WriteBufferGeoJsonCoordinates(pointValue, GeoJsonType_POINT,
														insideGeometryCollection,
														geoJsonWKB,
														parseState);
				if (!isValid)
				{
					return false;
				}

				totalPoints++;
			}

			WriteNumToWKBBuffer(numOfPoints, totalPoints);
			break;
		}

		case GeoJsonType_MULTILINESTRING:
		{
			if (IsBsonValueEmptyArray(coordinatesValue))
			{
				RETURN_FALSE_IF_ERROR_NOT_EXPECTED(
					shouldThrowError, (
						errcode(GEO_ERROR_CODE(parseState->errorCtxt)),
						errmsg(
							"%sMultiLineString coordinates must have at least 1 element",
							GEO_ERROR_PREFIX(parseState->errorCtxt)),
						errhint(
							"%sMultiLineString coordinates must have at least 1 element",
							GEO_HINT_PREFIX(parseState->errorCtxt))));
			}

			int32 totalLineStrings = 0;
			WriteHeaderToWKBBuffer(geoJsonWKB, WKBGeometryType_MultiLineString);
			uint8 *numOfLines = WKBBufferAppend4EmptyBytes(geoJsonWKB);

			while (bson_iter_next(&coordinatesIter))
			{
				const bson_value_t *lineStringValue = bson_iter_value(&coordinatesIter);
				isValid = WriteBufferGeoJsonCoordinates(lineStringValue,
														GeoJsonType_LINESTRING,
														insideGeometryCollection,
														geoJsonWKB,
														parseState);
				if (!isValid)
				{
					return false;
				}

				totalLineStrings++;
			}

			WriteNumToWKBBuffer(numOfLines, totalLineStrings);

			break;
		}

		case GeoJsonType_MULTIPOLYGON:
		{
			if (IsBsonValueEmptyArray(coordinatesValue))
			{
				RETURN_FALSE_IF_ERROR_NOT_EXPECTED(
					shouldThrowError, (
						errcode(GEO_ERROR_CODE(parseState->errorCtxt)),
						errmsg(
							"%sMultiPolygon coordinates must have at least 1 element",
							GEO_ERROR_PREFIX(parseState->errorCtxt)),
						errhint(
							"%sMultiPolygon coordinates must have at least 1 element",
							GEO_HINT_PREFIX(parseState->errorCtxt))));
			}

			WriteHeaderToWKBBuffer(geoJsonWKB, WKBGeometryType_MultiPolygon);
			int32 totalPolygons = 0;
			uint8 *numOfPolygon = WKBBufferAppend4EmptyBytes(geoJsonWKB);

			while (bson_iter_next(&coordinatesIter))
			{
				const bson_value_t *polygonValue = bson_iter_value(&coordinatesIter);
				isValid = WriteBufferGeoJsonCoordinates(polygonValue, GeoJsonType_POLYGON,
														insideGeometryCollection,
														geoJsonWKB,
														parseState);
				if (!isValid)
				{
					return false;
				}

				totalPolygons++;
			}

			WriteNumToWKBBuffer(numOfPolygon, totalPolygons);

			break;
		}

		case GeoJsonType_GEOMETRYCOLLECTION:
		{
			if (insideGeometryCollection)
			{
				RETURN_FALSE_IF_ERROR_NOT_EXPECTED(
					shouldThrowError, (
						errcode(GEO_ERROR_CODE(parseState->errorCtxt)),
						errmsg("%sGeometryCollections cannot be nested: %s",
							   GEO_ERROR_PREFIX(parseState->errorCtxt),
							   BsonValueToJsonForLogging(coordinatesValue)),
						errhint("%sGeometryCollections cannot be nested",
								GEO_HINT_PREFIX(parseState->errorCtxt))));
			}
			if (IsBsonValueEmptyArray(coordinatesValue))
			{
				RETURN_FALSE_IF_ERROR_NOT_EXPECTED(
					shouldThrowError, (
						errcode(GEO_ERROR_CODE(parseState->errorCtxt)),
						errmsg(
							"%sGeometryCollection geometries must have at least 1 element",
							GEO_ERROR_PREFIX(parseState->errorCtxt)),
						errhint(
							"%sGeometryCollection geometries must have at least 1 element",
							GEO_HINT_PREFIX(parseState->errorCtxt))));
			}
			insideGeometryCollection = true;
			int32 totalGeometries = 0;
			WriteHeaderToWKBBuffer(geoJsonWKB, WKBGeometryType_GeometryCollection);
			uint8 *numOfGeometries = WKBBufferAppend4EmptyBytes(geoJsonWKB);

			while (bson_iter_next(&coordinatesIter))
			{
				/* Check if value is object in collection */
				if (!BSON_ITER_HOLDS_DOCUMENT(&coordinatesIter))
				{
					RETURN_FALSE_IF_ERROR_NOT_EXPECTED(
						shouldThrowError, (
							errcode(GEO_ERROR_CODE(parseState->errorCtxt)),
							errmsg("%sElement %d of \"geometries\" is not an object",
								   GEO_ERROR_PREFIX(parseState->errorCtxt),
								   totalGeometries),
							errhint("%sElement %d of \"geometries\" is not an object",
									GEO_HINT_PREFIX(parseState->errorCtxt),
									totalGeometries)));
				}


				const bson_value_t *geoJsonValue = bson_iter_value(&coordinatesIter);

				GeoJsonParseState nestedParseState;
				memset(&nestedParseState, 0, sizeof(GeoJsonParseState));

				nestedParseState.shouldThrowValidityError =
					parseState->shouldThrowValidityError;
				nestedParseState.buffer = makeStringInfo();
				nestedParseState.expectedType = parseState->expectedType;
				nestedParseState.errorCtxt = parseState->errorCtxt;

				isValid = WriteBufferGeoJsonCore(geoJsonValue, insideGeometryCollection,
												 nestedParseState.buffer,
												 &nestedParseState);

				if (!isValid)
				{
					return false;
				}

				WriteStringInfoBufferToWKBBuffer(geoJsonWKB, nestedParseState.buffer);
				pfree(nestedParseState.buffer->data);

				totalGeometries++;
			}

			WriteNumToWKBBuffer(numOfGeometries, totalGeometries);
			break;
		}

		default:
		{
			/* It should already error out and not reach here */
			RETURN_FALSE_IF_ERROR_NOT_EXPECTED(
				shouldThrowError, (
					errcode(GEO_ERROR_CODE(parseState->errorCtxt)),
					errmsg("%sunknown GeoJSON type: %s",
						   GEO_ERROR_PREFIX(parseState->errorCtxt),
						   BsonValueToJsonForLogging(coordinatesValue)),
					errhint("%sunknown GeoJSON type found: %d",
							GEO_HINT_PREFIX(parseState->errorCtxt),
							geoJsonType)));
		}
	}
	return true;
}


/*
 * WriteBufferGeoJsonMultiPoints parses and validates a most common structure for most of the GeoJson Type
 * which is an array of Points. For different types this multipoint array means different thing.
 *
 * e.g.
 * {type: "Multipoint", coordinates: [[1, 2], [2, 3]]} => This mean really a disconnected multipoint array
 * {type: "Linestring", coordinates: [[1, 2], [2, 3]]} => This means a line generated by these points
 * {type: "Polygon", coordinates: [[<LineString1>, <LineString2>]]} => This means a polygon with multiple rings
 *                                                                     the outer is the area and the inner rings
 *                                                                     creating the holes.
 *
 *
 * The WKB format of a points list is as:
 * <4bytes - total points> [<8bytes - Point's X> <8bytes - Point's Y> , ...]
 * This method modifies the buffer and keeps adding valid points
 *
 * When the function returns `false` and flags validity issues, the caller should not use the buffer and consider it
 * as corrupted or incomplete
 */
static bool
WriteBufferGeoJsonMultiPoints(const bson_value_t *multiPointValue, const GeoJsonType type,
							  StringInfo geoJsonWKT, GeoJsonParseState *state)
{
	Assert(multiPointValue->value_type == BSON_TYPE_ARRAY);
	Assert(type == GeoJsonType_LINESTRING || type == GeoJsonType_POLYGON);

	bool shouldThrowError = state->shouldThrowValidityError;

	/*
	 * For Polygons and Linestrings adjacent same points are not valid for postgis as well as mongo discards
	 * them for internal use with indexing and query
	 * Please refer jstests\core\geo_s2dupe_points.js
	 */
	bool isPolyOrLinestring =
		((type & (GeoJsonType_LINESTRING | GeoJsonType_POLYGON)) != 0);

	/* Create a hash set for polygon validation to identify duplicate points (not adjacent) */
	HTAB *pointsHash = NULL;
	int duplicateFirst = -1;
	int duplicateSecond = -1;
	if (type == GeoJsonType_POLYGON)
	{
		pointsHash = CreatePointsHashSet();
	}

	/*
	 * Advance the buffer by 4bytes to later store the number of found points.
	 */
	uint8 *totalNumOfPoints = WKBBufferAppend4EmptyBytes(geoJsonWKT);
	Point first, last = { .x = INFINITY, .y = INFINITY }, point;

	bson_iter_t multiPointsValueIter;
	BsonValueInitIterator(multiPointValue, &multiPointsValueIter);

	int32 index = 0;
	while (bson_iter_next(&multiPointsValueIter))
	{
		if (!BSON_ITER_HOLDS_ARRAY(&multiPointsValueIter))
		{
			RETURN_FALSE_IF_ERROR_NOT_EXPECTED(
				shouldThrowError, (
					errcode(GEO_ERROR_CODE(state->errorCtxt)),
					errmsg("%sGeoJSON coordinates must be an array",
						   GEO_ERROR_PREFIX(state->errorCtxt)),
					errhint("%sGeoJSON coordinates must be an array",
							GEO_HINT_PREFIX(state->errorCtxt))));
		}

		memset(&point, 0, sizeof(Point));

		const bson_value_t *value = bson_iter_value(&multiPointsValueIter);
		bool isValid = ParseBsonValueAsPointWithBounds(value, shouldThrowError,
													   state->errorCtxt, &point);

		if (!isValid)
		{
			return false;
		}

		/* 1st: Skip adjacent same points for Polygon on line string */
		if (isPolyOrLinestring && point.x == last.x && point.y == last.y)
		{
			continue;
		}

		if (duplicateFirst != -1 && duplicateSecond != -1)
		{
			RETURN_FALSE_IF_ERROR_NOT_EXPECTED(
				shouldThrowError, (
					errcode(GEO_ERROR_CODE(state->errorCtxt)),
					errmsg("%sLoop is not valid: %s Duplicate vertices: %d and %d",
						   GEO_ERROR_PREFIX(state->errorCtxt),
						   BsonValueToJsonForLogging(multiPointValue),
						   duplicateFirst, duplicateSecond),
					errhint("%sLoop is not valid, Duplicate vertices found at: %d and %d",
							GEO_HINT_PREFIX(state->errorCtxt),
							duplicateFirst, duplicateSecond)));
		}

		/*
		 * 2nd: For Linestrings and Polygon a long edge covering a complete latitude or longitude
		 * is not valid in Postgis, it is valid in mongo but we need to handle the error gracefully
		 */
		if (isPolyOrLinestring && index > 0 && (fabs(point.y - last.y) >= 180.0))
		{
			RETURN_FALSE_IF_ERROR_NOT_EXPECTED(
				shouldThrowError, (
					errcode(GEO_ERROR_CODE(state->errorCtxt)),
					errmsg(
						"%sGeoJSON coordinates must not contain antipodal edges. Detected at : %s",
						GEO_ERROR_PREFIX(state->errorCtxt),
						BsonValueToJsonForLogging(multiPointValue)),
					errhint("%sGeoJSON coordinates must not contain antipodal edges.",
							GEO_HINT_PREFIX(state->errorCtxt))));
		}

		/*
		 * 3rd: For Polygon check for duplicate non-adjacent vertices
		 */
		if (type == GeoJsonType_POLYGON)
		{
			PointsHashEntry entry = {
				.index = index,
				.points = { point.x, point.y }
			};
			bool found = false;
			PointsHashEntry *foundPoint = hash_search(pointsHash, &entry, HASH_ENTER,
													  &found);
			if (found)
			{
				/*
				 * The last point is expected to match to close the ring, so we just save these duplicate points
				 * and if there are more point in the array then we error out above.
				 */
				duplicateFirst = foundPoint->index;
				duplicateSecond = index;
			}
		}

		if (index == 0)
		{
			first.x = point.x;
			first.y = point.y;
			last.x = point.x;
			last.y = point.y;
		}
		else
		{
			last.x = point.x;
			last.y = point.y;
		}
		WritePointToWKBBuffer(geoJsonWKT, &point);
		index++;
	}

	/* Custom validations for specific type which accepts multipoint geojson format */
	if (type == GeoJsonType_POLYGON)
	{
		if (index == 0)
		{
			RETURN_FALSE_IF_ERROR_NOT_EXPECTED(
				shouldThrowError, (
					errcode(GEO_ERROR_CODE(state->errorCtxt)),
					errmsg("%sLoop has no vertices: []",
						   GEO_ERROR_PREFIX(state->errorCtxt)),
					errhint("%sLoop has no vertices. []",
							GEO_HINT_PREFIX(state->errorCtxt))));
		}

		if (first.x != last.x || first.y != last.y)
		{
			RETURN_FALSE_IF_ERROR_NOT_EXPECTED(
				shouldThrowError, (
					errcode(GEO_ERROR_CODE(state->errorCtxt)),
					errmsg("%sLoop is not closed: %s",
						   GEO_ERROR_PREFIX(state->errorCtxt),
						   BsonValueToJsonForLogging(multiPointValue)),
					errhint("%sLoop is not closed.",
							GEO_HINT_PREFIX(state->errorCtxt))));
		}

		if (index < 4)
		{
			RETURN_FALSE_IF_ERROR_NOT_EXPECTED(
				shouldThrowError, (
					errcode(GEO_ERROR_CODE(state->errorCtxt)),
					errmsg("%sLoop must have at least 3 different vertices: %s",
						   GEO_ERROR_PREFIX(state->errorCtxt),
						   BsonValueToJsonForLogging(multiPointValue)),
					errhint("%sLoop must have at least 3 different vertices.",
							GEO_HINT_PREFIX(state->errorCtxt))));
		}
	}

	if (type == GeoJsonType_LINESTRING)
	{
		if (index < 2)
		{
			RETURN_FALSE_IF_ERROR_NOT_EXPECTED(
				shouldThrowError, (
					errcode(GEO_ERROR_CODE(state->errorCtxt)),
					errmsg("%sGeoJSON LineString must have at least 2 vertices: %s",
						   GEO_ERROR_PREFIX(state->errorCtxt),
						   BsonValueToJsonForLogging(multiPointValue)),
					errhint("%sGeoJSON LineString must have at least 2 vertices.",
							GEO_HINT_PREFIX(state->errorCtxt))));
		}
	}

	/* Destroy points hash */
	if (pointsHash != NULL)
	{
		hash_destroy(pointsHash);
	}

	/* If all good write the number of points at the buffer which advanced earlier */
	WriteNumToWKBBuffer(totalNumOfPoints, index);
	return true;
}


/*
 * GeoJsonTypeName returns the name of the geoJsonType
 */
static const char *
GeoJsonTypeName(GeoJsonType type)
{
	switch (type)
	{
		case GeoJsonType_POINT:
		{
			return "Point";
		}

		case GeoJsonType_LINESTRING:
		{
			return "LineString";
		}

		case GeoJsonType_POLYGON:
		{
			return "Polygon";
		}

		case GeoJsonType_MULTIPOINT:
		{
			return "MultiPoint";
		}

		case GeoJsonType_MULTILINESTRING:
		{
			return "MultiLineString";
		}

		case GeoJsonType_MULTIPOLYGON:
		{
			return "MultiPolygon";
		}

		case GeoJsonType_GEOMETRYCOLLECTION:
		{
			return "GeometryCollection";
		}

		default:
		{
			return "Unknown";
		}
	}
}


/*
 * ValidateCoordinatesNotArray parses and validates "coordinates" field value for different
 * GeoJson types and throws appropriate mongo error if "coordinates" is not array.
 *
 * In case of validity issue the function either throws an error if the error is expected or
 * return false to flag invalidity
 */
static bool
ValidateCoordinatesNotArray(const bson_value_t *coordinatesValue, GeoJsonType geoJsonType,
							GeoJsonParseState *parseState)
{
	bool shouldThrowError = parseState->shouldThrowValidityError;

	if (coordinatesValue->value_type != BSON_TYPE_ARRAY)
	{
		if (geoJsonType == GeoJsonType_POINT)
		{
			if (coordinatesValue->value_type != BSON_TYPE_DOCUMENT)
			{
				/* GeoJSON standard doesn't accept Documents for Points but mongo does. */
				RETURN_FALSE_IF_ERROR_NOT_EXPECTED(
					shouldThrowError, (
						errcode(GEO_ERROR_CODE(parseState->errorCtxt)),
						errmsg("%sPoint must be an array or object",
							   GEO_ERROR_PREFIX(parseState->errorCtxt)),
						errhint("%sPoint must be an array or object",
								GEO_HINT_PREFIX(parseState->errorCtxt))));
			}
		}
		else if (geoJsonType == GeoJsonType_GEOMETRYCOLLECTION)
		{
			RETURN_FALSE_IF_ERROR_NOT_EXPECTED(
				shouldThrowError, (
					errcode(GEO_ERROR_CODE(parseState->errorCtxt)),
					errmsg("%sGeometryCollection geometries must be an array",
						   GEO_ERROR_PREFIX(parseState->errorCtxt)),
					errhint("%sGeometryCollection geometries must be an array",
							GEO_HINT_PREFIX(parseState->errorCtxt))));
		}
		else if ((geoJsonType & (GeoJsonType_POLYGON | GeoJsonType_MULTILINESTRING |
								 GeoJsonType_MULTIPOLYGON)) > 1)
		{
			const char *geoJsonTypeName = GeoJsonTypeName(geoJsonType);
			RETURN_FALSE_IF_ERROR_NOT_EXPECTED(
				shouldThrowError, (
					errcode(GEO_ERROR_CODE(parseState->errorCtxt)),
					errmsg("%s%s coordinates must be an array",
						   GEO_ERROR_PREFIX(parseState->errorCtxt),
						   geoJsonTypeName),
					errhint("%s%s coordinates must be an array",
							GEO_HINT_PREFIX(parseState->errorCtxt),
							geoJsonTypeName)));
		}
		else
		{
			RETURN_FALSE_IF_ERROR_NOT_EXPECTED(
				shouldThrowError, (
					errcode(GEO_ERROR_CODE(parseState->errorCtxt)),
					errmsg("%sGeoJSON coordinates must be an array of coordinates",
						   GEO_ERROR_PREFIX(parseState->errorCtxt)),
					errhint("%sGeoJSON coordinates must be an array of coordinates",
							GEO_HINT_PREFIX(parseState->errorCtxt))));
		}
	}
	return true;
}


/*
 * AdditionalPolygonValidation validates the given polygon rings so that they form a valid polygon.
 * This method creates a polygon "geometry" from the Polygon WKT and runs the 'ST_IsValidReason' for
 * the polygon.
 *
 * Unfortunately we can't validate the "geography" polygon as there is no Postgis Native support,
 * so we have to validate it against the "geometry" polygon only.
 */
static bool
AdditionalPolygonValidation(StringInfo polygonWKB, GeoJsonParseState *parseState,
							int totalRings)
{
	bool shouldThrowError = parseState->shouldThrowValidityError;

	/* Set the varlena size and cast to bytea */
	SET_VARSIZE(polygonWKB->data, polygonWKB->len);
	bytea *wkbPolygon = (bytea *) polygonWKB->data;
	Datum polygonGeometry = GetGeometryFromWKB(wkbPolygon);

	HeapTupleHeader tupleHeader = DatumGetHeapTupleHeader(OidFunctionCall2(
															  PostgisGeometryIsValidDetailFunctionId(),
															  polygonGeometry,
															  Int32GetDatum(0)));
	Oid tupleType = HeapTupleHeaderGetTypeId(tupleHeader);
	int32 tupleTypmod = HeapTupleHeaderGetTypMod(tupleHeader);
	TupleDesc tupleDescriptor = lookup_rowtype_tupdesc(tupleType, tupleTypmod);
	HeapTupleData tupleValue;

	tupleValue.t_len = HeapTupleHeaderGetDatumLength(tupleHeader);
	tupleValue.t_data = tupleHeader;

	bool isNull = false;
	bool isValid = false;
	Datum isValidDatum = heap_getattr(&tupleValue, 1, tupleDescriptor, &isNull);
	if (!isNull)
	{
		isValid = DatumGetBool(isValidDatum);
	}

	if (!isValid)
	{
		Datum invalidReasonDatum = heap_getattr(&tupleValue, 2, tupleDescriptor, &isNull);
		if (isNull)
		{
			ReleaseTupleDesc(tupleDescriptor);
			return false;
		}
		const char *invalidReason = TextDatumGetCString(invalidReasonDatum);

		/* Check if holes are outside */
		if (strstr(invalidReason, "Hole lies outside shell") != NULL)
		{
			RETURN_FALSE_IF_ERROR_NOT_EXPECTED(
				shouldThrowError, (
					errcode(GEO_ERROR_CODE(parseState->errorCtxt)),
					errmsg("%sSecondary loops not contained by first exterior loop - "
						   "secondary loops must be holes",
						   GEO_ERROR_PREFIX(parseState->errorCtxt)),
					errhint("%sSecondary loops not contained by first exterior loop - "
							"secondary loops must be holes",
							GEO_HINT_PREFIX(parseState->errorCtxt))));
		}
		else if (strstr(invalidReason, "Self-intersection") != NULL)
		{
			/*
			 * Hacky workaround alert:
			 * Postgis doesn't provide validation for geography polygons and one specifc case
			 * where the polygon is a straight line in plannar geometry actually resolves to a
			 * surface circle in geography
			 * e.g. North Pole polygon in geography
			 * { type: "Polygon", "coordinates": [[[-120.0, 89.0], [0.0, 89.0], [120.0, 89.0], [-120.0, 89.0]]] }
			 *
			 * This resolves to a straight line intersecting polygon in 2d but a valid geography polygon sphere
			 * around the North Pole.
			 *
			 * There is only 1 way to find these cases as of now, the 2d geometry polygon of above case will have area of 0
			 * so we exclude these from Self Intersection error as they will resolve to a valid geography region
			 *
			 * Also, if there are holes in the polygon and holes cover the complete polygon to make area 0 then thats
			 * self intersection
			 */
			double areaOfPolygon = DatumGetFloat8(OidFunctionCall1(
													  PostgisGeometryAreaFunctionId(),
													  polygonGeometry));
			if (areaOfPolygon > 0 || (areaOfPolygon == 0 &&
									  totalRings > 1))
			{
				/*
				 * Mongo throws this sample error here
				 * Edges 0 and 2 cross. Edge locations in degrees: [0, 0]-[4, 0] and [2, -3]-[2, 1]"
				 * We don't have much information abouth the intersection so we throw generic error
				 */
				RETURN_FALSE_IF_ERROR_NOT_EXPECTED(
					shouldThrowError, (
						errcode(GEO_ERROR_CODE(parseState->errorCtxt)),
						errmsg(
							"%sEdges cross - Loops should not self intersect or share any edge",
							GEO_ERROR_PREFIX(parseState->errorCtxt)),
						errhint(
							"%sEdges cross - Loops should not self intersect or share any edge",
							GEO_HINT_PREFIX(parseState->errorCtxt))));
			}
		}
		else
		{
			/* Something else caused validity failure, for safety instead of wrong behavior throw error */
			RETURN_FALSE_IF_ERROR_NOT_EXPECTED(
				shouldThrowError, (
					errcode(GEO_ERROR_CODE(parseState->errorCtxt)),
					errmsg("%s%s", GEO_ERROR_PREFIX(parseState->errorCtxt),
						   invalidReason),
					errhint("%s%s", GEO_HINT_PREFIX(parseState->errorCtxt),
							invalidReason)));
		}
	}

	ReleaseTupleDesc(tupleDescriptor);
	return true;
}


/*
 * Recursively traverse a GeoJSON document value performing validation
 * for types and values and also keeps creating the WKT for the geography
 */
static bool
WriteBufferGeoJsonCore(const bson_value_t *value, bool insideGeoJsonGeometryCollection,
					   StringInfo goeJsonWKB, GeoJsonParseState *parseState)
{
	check_stack_depth();
	CHECK_FOR_INTERRUPTS();
	bson_iter_t valueItr;
	BsonValueInitIterator(value, &valueItr);
	bson_iter_t typeItr;
	GeoJsonType geoJsonType = GeoJsonType_UNKNOWN;

	bool shouldThrowError = parseState->shouldThrowValidityError;
	bool isValid = false;

	/* First : parse CRS */
	bson_iter_t crsIter;
	if (bson_iter_find_descendant(&valueItr, "crs", &crsIter))
	{
		if (!BSON_ITER_HOLDS_DOCUMENT(&crsIter))
		{
			RETURN_FALSE_IF_ERROR_NOT_EXPECTED(
				shouldThrowError, (
					errcode(GEO_ERROR_CODE(parseState->errorCtxt)),
					errmsg("%sGeoJSON CRS must be an object.",
						   GEO_ERROR_PREFIX(parseState->errorCtxt)),
					errhint("%sGeoJSON CRS must be an object.",
							GEO_HINT_PREFIX(parseState->errorCtxt))));
		}
		bson_iter_t crsChildIter, typeIter;
		bson_iter_recurse(&crsIter, &crsChildIter);
		bson_iter_t crsIterChildCopy = crsChildIter;
		bool foundValidType = false;
		if (bson_iter_find_descendant(&crsChildIter, "type", &typeIter) &&
			BSON_ITER_HOLDS_UTF8(&typeIter))
		{
			uint32_t typeLength;
			const char *typeValue = bson_iter_utf8(&typeIter, &typeLength);
			foundValidType = strncasecmp(typeValue, "name", typeLength) == 0;
		}

		if (!foundValidType)
		{
			RETURN_FALSE_IF_ERROR_NOT_EXPECTED(
				shouldThrowError, (
					errcode(GEO_ERROR_CODE(parseState->errorCtxt)),
					errmsg("%sGeoJSON CRS must have field \"type\": \"name\"",
						   GEO_ERROR_PREFIX(parseState->errorCtxt)),
					errhint("%sGeoJSON CRS must have field \"type\": \"name\"",
							GEO_HINT_PREFIX(parseState->errorCtxt))));
		}

		bson_iter_t propertiesItr;
		bool validPropertiesFound = false;
		crsChildIter = crsIterChildCopy;
		if (bson_iter_find_descendant(&crsChildIter, "properties", &propertiesItr) &&
			BSON_ITER_HOLDS_DOCUMENT(&propertiesItr))
		{
			bson_iter_t propertiesChildIter, propertiesNameIter;
			bson_iter_recurse(&propertiesItr, &propertiesChildIter);
			validPropertiesFound = true;

			bool validNameFound = false;
			if (bson_iter_find_descendant(&propertiesChildIter, "name",
										  &propertiesNameIter) &&
				BSON_ITER_HOLDS_UTF8(&propertiesNameIter))
			{
				uint32_t crsNameLength;
				const char *crsName = bson_iter_utf8(&propertiesNameIter, &crsNameLength);
				validNameFound = true;

				/* Found a valid crs name apply the crs */
				if (strcmp(crsName, GEOJSON_CRS_BIGPOLYGON) == 0 ||
					strcmp(crsName, GEOJSON_CRS_EPSG_4326) == 0 ||
					strcmp(crsName, GEOJSON_CRS_84) == 0)
				{
					parseState->crs = crsName;
				}
				else
				{
					RETURN_FALSE_IF_ERROR_NOT_EXPECTED(
						shouldThrowError, (
							errcode(GEO_ERROR_CODE(parseState->errorCtxt)),
							errmsg("%sUnknown CRS name: %s",
								   GEO_ERROR_PREFIX(parseState->errorCtxt),
								   crsName),
							errhint("%sUnknown CRS name.",
									GEO_HINT_PREFIX(parseState->errorCtxt))));
				}
			}

			if (!validNameFound)
			{
				RETURN_FALSE_IF_ERROR_NOT_EXPECTED(
					shouldThrowError, (
						errcode(GEO_ERROR_CODE(parseState->errorCtxt)),
						errmsg("%sIn CRS, \"properties.name\" must be a string",
							   GEO_ERROR_PREFIX(parseState->errorCtxt)),
						errhint("%sIn CRS, \"properties.name\" must be a string",
								GEO_HINT_PREFIX(parseState->errorCtxt))));
			}
		}

		if (!validPropertiesFound)
		{
			RETURN_FALSE_IF_ERROR_NOT_EXPECTED(
				shouldThrowError, (
					errcode(GEO_ERROR_CODE(parseState->errorCtxt)),
					errmsg("%sCRS must have field \"properties\" which is an object",
						   GEO_ERROR_PREFIX(parseState->errorCtxt)),
					errhint("%sCRS must have field \"properties\" which is an object",
							GEO_HINT_PREFIX(parseState->errorCtxt))));
		}
	}

	/* Second : Now try to parse the geoJSON */
	BsonValueInitIterator(value, &valueItr);
	if (!bson_iter_find_descendant(&valueItr, "type", &typeItr))
	{
		RETURN_FALSE_IF_ERROR_NOT_EXPECTED(
			shouldThrowError, (
				errcode(GEO_ERROR_CODE(parseState->errorCtxt)),
				errmsg("%sUnknwown GeoJSON type: %s",
					   GEO_ERROR_PREFIX(parseState->errorCtxt),
					   BsonValueToJsonForLogging(value)),
				errhint("%sUnknwown GeoJSON type",
						GEO_HINT_PREFIX(parseState->errorCtxt))));
	}
	else
	{
		const bson_value_t *typeValue = bson_iter_value(&valueItr);

		if (typeValue->value_type != BSON_TYPE_UTF8)
		{
			RETURN_FALSE_IF_ERROR_NOT_EXPECTED(
				shouldThrowError, (
					errcode(GEO_ERROR_CODE(parseState->errorCtxt)),
					errmsg("%sUnknwown GeoJSON type: %s",
						   GEO_ERROR_PREFIX(parseState->errorCtxt),
						   BsonValueToJsonForLogging(value)),
					errhint("%sUnknwown GeoJSON type",
							GEO_HINT_PREFIX(parseState->errorCtxt))));
		}

		geoJsonType = GetGeoJsonType(typeValue);

		if (geoJsonType == GeoJsonType_UNKNOWN)
		{
			RETURN_FALSE_IF_ERROR_NOT_EXPECTED(
				shouldThrowError, (
					errcode(GEO_ERROR_CODE(parseState->errorCtxt)),
					errmsg("%sunknown GeoJSON type: %s",
						   GEO_ERROR_PREFIX(parseState->errorCtxt),
						   BsonValueToJsonForLogging(value)),
					errhint("%sunknown GeoJSON type found",
							GEO_HINT_PREFIX(parseState->errorCtxt))));
		}

		/* Not an expected type */
		if ((geoJsonType & parseState->expectedType) != geoJsonType)
		{
			RETURN_FALSE_IF_ERROR_NOT_EXPECTED(
				shouldThrowError, (
					errcode(GEO_ERROR_CODE(parseState->errorCtxt)),
					errmsg("%sGeoJSON Type not expected",
						   GEO_ERROR_PREFIX(parseState->errorCtxt)),
					errhint("%sGeoJSON Type not expected",
							GEO_HINT_PREFIX(parseState->errorCtxt))));
		}
	}

	parseState->type = geoJsonType;

	/* Parse coordinates for GeoJson Geometry */
	BsonValueInitIterator(value, &valueItr);
	bson_iter_t coordinatesIter;
	const char *coordinateKey = geoJsonType == GeoJsonType_GEOMETRYCOLLECTION ?
								"geometries" : "coordinates";
	if (!bson_iter_find_descendant(&valueItr, coordinateKey, &coordinatesIter))
	{
		if (!shouldThrowError)
		{
			return false;
		}

		/*
		 * Coordinates not found create a dummy bson_value to perform validation and throw geoJson type specific errors
		 * Note: Below method invocation will definitely throw error as the type expected is BSON_TYPE_DOCUMENT
		 */
		const bson_value_t dummy = {
			.value_type = BSON_TYPE_EOD
		};
		return WriteBufferGeoJsonCoordinates(&dummy, geoJsonType,
											 insideGeoJsonGeometryCollection,
											 goeJsonWKB, parseState);
	}

	const bson_value_t *coordinatesValue = bson_iter_value(&coordinatesIter);
	isValid = WriteBufferGeoJsonCoordinates(coordinatesValue, geoJsonType,
											insideGeoJsonGeometryCollection, goeJsonWKB,
											parseState);
	return isValid;
}


/*
 * Creates the points hash table for finding duplicate points in the multiple points rings
 */
static HTAB *
CreatePointsHashSet()
{
	HASHCTL hashInfo = CreateExtensionHashCTL(
		sizeof(PointsHashEntry),
		sizeof(PointsHashEntry),
		PointsHashEntryCompareFunc,
		PointsHashEntryHashFunc);

	return hash_create("GeoJSON Polygon Points Hash value", 32, &hashInfo,
					   DefaultExtensionHashFlags);
}


/*
 * Get the GeoJsonType from the value representing the type in string
 */
static GeoJsonType
GetGeoJsonType(const bson_value_t *value)
{
	Assert(value->value_type == BSON_TYPE_UTF8);
	const char *type = value->value.v_utf8.str;
	if (strcmp(type, "Point") == 0)
	{
		return GeoJsonType_POINT;
	}
	else if (strcmp(type, "LineString") == 0)
	{
		return GeoJsonType_LINESTRING;
	}
	else if (strcmp(type, "Polygon") == 0)
	{
		return GeoJsonType_POLYGON;
	}
	else if (strcmp(type, "MultiPoint") == 0)
	{
		return GeoJsonType_MULTIPOINT;
	}
	else if (strcmp(type, "MultiLineString") == 0)
	{
		return GeoJsonType_MULTILINESTRING;
	}
	else if (strcmp(type, "MultiPolygon") == 0)
	{
		return GeoJsonType_MULTIPOLYGON;
	}
	else if (strcmp(type, "GeometryCollection") == 0)
	{
		return GeoJsonType_GEOMETRYCOLLECTION;
	}
	return GeoJsonType_UNKNOWN;
}


/*
 * Compares the hash entries for PointsHashEntry, this is a callback to (HASHCTL.match) function
 */
static int
PointsHashEntryCompareFunc(const void *obj1, const void *obj2, Size objsize)
{
	PointsHashEntry *entry1 = (PointsHashEntry *) obj1;
	PointsHashEntry *entry2 = (PointsHashEntry *) obj2;
	return memcmp(entry1->points, entry2->points, sizeof(entry1->points));
}


/*
 * Creates the hash entries for PointsHashEntry, this is a callback to (HASHCTL.hash) function
 */
static uint32
PointsHashEntryHashFunc(const void *obj, Size objsize)
{
	PointsHashEntry *entry = (PointsHashEntry *) obj;
	return hash_bytes((const unsigned char *) entry->points, sizeof(entry->points));
}
