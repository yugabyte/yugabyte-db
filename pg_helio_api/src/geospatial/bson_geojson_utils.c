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
#include "float.h"
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
#include "include/geospatial/bson_geospatial_wkb_iterator.h"
#include "include/geospatial/bson_geospatial_common.h"


/* Hash entry for points to track duplicate points */
typedef struct PointsHashEntry
{
	/* Points array containing 2 points */
	double points[2];

	/* Index of the point in the point array */
	uint32_t index;
} PointsHashEntry;

/* State for polygon validation */
typedef struct PolygonValidationState
{
	GeospatialErrorContext *errorCtxt;

	bool shouldThrowValidityError;

	bool isValid;

	/* Datum for current ring, to be used for covers check in next ring validation */
	Datum previousRingDatum;

	/* Datum for current ring as LineString, to be used for intersect check in next ring validation */
	Datum previousRingLineStringGeometryDatum;

	/* Store for postgis functions used in polygon validation */
	FmgrInfo **validationFunctions;

	/* Start of previous ring points to construct error msg */
	char *previousRingPointsStart;

	/* Number of points in previous ring to construct error msg */
	int32 previousRingNumPoints;
} PolygonValidationState;

/* struct to hold result values from ST_IsValidDetail function */
typedef struct IsValidDetailState
{
	bool isValid;

	const char *invalidityReason;
} IsValidDetailState;

static const char * GeoJsonTypeName(GeoJsonType type);
static GeoJsonType GetGeoJsonType(const bson_value_t *value);
static bool WriteBufferGeoJsonCore(const bson_value_t *value,
								   bool insideGeoJsonGeometryCollection,
								   StringInfo goeJsonWKB,
								   GeoJsonParseState *parseState);
static bool WriteBufferGeoJsonCoordinates(const bson_value_t *value,
										  GeoJsonType geoJsonType,
										  bool insideGeometryCollection,
										  StringInfo geoJsonWKB,
										  GeoJsonParseState *parseState);
static bool WriteBufferGeoJsonMultiPoints(const bson_value_t *value,
										  const GeoJsonType type,
										  StringInfo geoJsonWKB,
										  GeoJsonParseState *state);
static bool ValidateCoordinatesNotArray(const bson_value_t *coordinatesValue,
										GeoJsonType geoJsonType,
										GeoJsonParseState *parseState);

/* Polygon validation helpers */
static bool AdditionalPolygonValidation(StringInfo geoJsonWKB,
										GeoJsonParseState *parseState,
										bytea **outPolygonWKB);
static void VisitPolygonRingForValidation(const WKBGeometryConst *geometryConst,
										  void *state);
static bool CheckMultiRingPolygonValidity(const WKBGeometryConst *geometryConst,
										  PolygonValidationState *polygonValidationState);
static bool CheckSingleRingPolygonValidity(const WKBGeometryConst *geometryConst,
										   PolygonValidationState *polygonValidationState);
static bool ContinueIfRingValid(void *state);
static void InitPolygonValidationState(PolygonValidationState *polygonValidationState,
									   GeoJsonParseState *parseState,
									   bytea *polygonWKB);
static IsValidDetailState GetPolygonInvalidityReason(Datum polygon,
													 PolygonValidationState *state);
static bool IsRingStraightLine(char *pointsStart, int32 numPoints);
static bool IsHoleFullyCoveredByOuterRing(char *currPtr, int32 numPoints,
										  PolygonValidationState *polygonValidationState,
										  Datum holeDatum);
static char * GetRingPointsStringForError(char *currPtr, int32 numPoints);


/* HashSet utilities for Points */
static HTAB * CreatePointsHashSet(void);
static int PointsHashEntryCompareFunc(const void *obj1, const void *obj2, Size objsize);
static uint32 PointsHashEntryHashFunc(const void *obj, Size objsize);

/*
 * Execution function for validating the GeoJSON Polygons
 */
static const WKBVisitorFunctions PolygonValidationVisitorFuncs = {
	.ContinueTraversal = ContinueIfRingValid,
	.VisitEachPoint = NULL,
	.VisitGeometry = NULL,
	.VisitSingleGeometry = NULL,
	.VisitPolygonRing = VisitPolygonRingForValidation
};

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
			int32 numOfRingsPos = WKBBufferAppend4EmptyBytesForNums(polygonWKB);

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
			WriteNumToWKBBufferAtPosition(polygonWKB, numOfRingsPos, totalRings);

			/* First: Perform additional polygon validity tests and get the WKB for clockwise oriented polygon */
			bytea *validPolygonWkb = NULL;
			isValid = AdditionalPolygonValidation(polygonWKB, parseState,
												  &validPolygonWkb);
			if (!isValid)
			{
				return false;
			}
			WriteBufferWithLengthToWKBBuffer(geoJsonWKB,
											 (char *) VARDATA_ANY(validPolygonWkb),
											 VARSIZE_ANY_EXHDR(validPolygonWkb));

			parseState->numOfRingsInPolygon = Max(parseState->numOfRingsInPolygon,
												  totalRings);

			DeepFreeWKB(polygonWKB);
			pfree(validPolygonWkb);

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
			int32 numOfPointsPos = WKBBufferAppend4EmptyBytesForNums(geoJsonWKB);

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

			WriteNumToWKBBufferAtPosition(geoJsonWKB, numOfPointsPos, totalPoints);
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
			int32 numOfLinesPos = WKBBufferAppend4EmptyBytesForNums(geoJsonWKB);

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

			WriteNumToWKBBufferAtPosition(geoJsonWKB, numOfLinesPos, totalLineStrings);

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
			int32 numOfPolygonPos = WKBBufferAppend4EmptyBytesForNums(geoJsonWKB);

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

			WriteNumToWKBBufferAtPosition(geoJsonWKB, numOfPolygonPos, totalPolygons);

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
			int32 numOfGeometriesPos = WKBBufferAppend4EmptyBytesForNums(geoJsonWKB);

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
				DeepFreeWKB(nestedParseState.buffer);

				totalGeometries++;
			}

			WriteNumToWKBBufferAtPosition(geoJsonWKB, numOfGeometriesPos,
										  totalGeometries);
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
							  StringInfo geoJsonWKB, GeoJsonParseState *state)
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
	int32 totalNumOfPointsPos = WKBBufferAppend4EmptyBytesForNums(geoJsonWKB);
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
		WritePointToWKBBuffer(geoJsonWKB, &point);
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
	WriteNumToWKBBufferAtPosition(geoJsonWKB, totalNumOfPointsPos, index);
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
 * This method creates a polygon "geometry" from the Polygon WKB and runs the 'ST_IsValidReason' for
 * the polygon.
 *
 * Unfortunately we can't validate the "geography" polygon as there is no Postgis Native support,
 * so we have to validate it against the "geometry" polygon only.
 *
 * Sets the WKB of validated polygon (clockwise oriented) in outPolygonWKB
 */
static bool
AdditionalPolygonValidation(StringInfo polygonWKB, GeoJsonParseState *parseState,
							bytea **outPolygonWKB)
{
	bool shouldThrowError = parseState->shouldThrowValidityError;

	/* Set the varlena size and cast to bytea */
	SET_VARSIZE(polygonWKB->data, polygonWKB->len);
	bytea *wkbBytea = (bytea *) polygonWKB->data;
	Datum polygonGeometry = GetGeometryFromWKB(wkbBytea);

	/* Make sure the polygon is clock wise oriented for Postgis */
	Datum clockWiseOrientedGeometry = OidFunctionCall1(PostgisForcePolygonCWFunctionId(),
													   polygonGeometry);
	*outPolygonWKB = DatumGetByteaP(
		OidFunctionCall1(PostgisGeometryAsBinaryFunctionId(),
						 clockWiseOrientedGeometry));

	if (outPolygonWKB == NULL)
	{
		RETURN_FALSE_IF_ERROR_NOT_EXPECTED(
			shouldThrowError, (
				errcode(GEO_ERROR_CODE(parseState->errorCtxt)),
				errmsg(
					"%sUnexpected, found null geometry during polygon validation.",
					GEO_ERROR_PREFIX(parseState->errorCtxt)),
				errhint(
					"%sUnexpected, found null geometry during polygon validation.",
					GEO_HINT_PREFIX(parseState->errorCtxt))));
	}

	IsValidDetailState isValidDetailState =
		GetPolygonInvalidityReason(clockWiseOrientedGeometry, NULL);

	if (isValidDetailState.isValid)
	{
		return true;
	}

	/* Check if holes are outside */
	if (strstr(isValidDetailState.invalidityReason, "Hole lies outside shell") != NULL)
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
	else if (strstr(isValidDetailState.invalidityReason, "Self-intersection") != NULL)
	{
		/*
		 * Hacky workaround alert:
		 * Postgis doesn't provide validation for geography polygons and cases
		 * where the polygon is a straight line in plannar geometry actually resolves to a
		 * surface circle in geography
		 * e.g. North Pole polygon in geography
		 * { type: "Polygon", "coordinates": [[[-120.0, 89.0], [0.0, 89.0], [120.0, 89.0], [-120.0, 89.0]]] }
		 *
		 * This resolves to a straight line intersecting polygon in 2d but a valid geography polygon sphere
		 * around the North Pole.
		 *
		 * So we have followed a multi-step process here -
		 * First, we check if a single ring polygon is a straight line in 2d. If not its definitely self intersecting.
		 *
		 * Next, we check if the current ring is valid or not using ST_IsValidDetail. If not, we check if it is a straight line. If not, its invalid.
		 *
		 * Next, we check if current ring is fully covered by previous ring using ST_Covers. If not, polygon is invalid.
		 * If yes, we check if linestrings made from current ring and previous ring intersect in 2d,
		 * as ST_Covers returns true even if edges are overlapping. If they intersect, polygon is invalid.
		 */
		PolygonValidationState polygonValidationState;
		InitPolygonValidationState(&polygonValidationState, parseState,
								   *outPolygonWKB);

		/* Traverse polygon for validation */
		TraverseWKBBytea(*outPolygonWKB, &PolygonValidationVisitorFuncs,
						 (void *) &polygonValidationState);

		if (polygonValidationState.isValid)
		{
			return true;
		}

		/* If the polygon is not valid, throw error */
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
	else
	{
		/* Something else caused validity failure, for safety instead of wrong behavior throw error */
		RETURN_FALSE_IF_ERROR_NOT_EXPECTED(
			shouldThrowError, (
				errcode(GEO_ERROR_CODE(parseState->errorCtxt)),
				errmsg("%s%s", GEO_ERROR_PREFIX(parseState->errorCtxt),
					   isValidDetailState.invalidityReason),
				errhint("%s%s", GEO_HINT_PREFIX(parseState->errorCtxt),
						isValidDetailState.invalidityReason)));
	}

	/* If everything is okay, get the wkb of validated polygon (clock-wise oriented) and return it */
	return true;
}


/* Visitor function for TraverseWKBBuffer during polygon validation */
static void
VisitPolygonRingForValidation(const WKBGeometryConst *geometryConst, void *state)
{
	if (geometryConst->geometryType != WKBGeometryType_Polygon)
	{
		ereport(ERROR, (
					errcode(MongoInternalError),
					errmsg(
						"%d unexpected geospatial type for polygon validation found in document WKB",
						geometryConst->geometryType),
					errhint(
						"%d unexpected geospatial type for polygon validation found in document WKB",
						geometryConst->geometryType)));
	}

	PolygonValidationState *polygonValidationState = (PolygonValidationState *) state;
	int32 numRings = geometryConst->numRings;
	Assert(geometryConst->ringPointsStart != NULL);

	if (numRings == 1)
	{
		polygonValidationState->isValid = CheckSingleRingPolygonValidity(geometryConst,
																		 polygonValidationState);
	}
	else
	{
		polygonValidationState->isValid = CheckMultiRingPolygonValidity(geometryConst,
																		polygonValidationState);
	}
}


/* Check validity of a single ring polygon */
static bool
CheckSingleRingPolygonValidity(const WKBGeometryConst *geometryConst,
							   PolygonValidationState *polygonValidationState)
{
	int32 numPoints = geometryConst->numPoints;
	char *currPtr = (char *) geometryConst->ringPointsStart;

	/*
	 * In case of single ring, its invalid unless its a straight line in 2d
	 * TODO: currently this misses cases like {type: "Polygon", "coordinates": [[[0, 5], [5, 0], [10, 5], [15, 5], [0, 5]]]}
	 * which is self intersecting in 2d but valid on earth.
	 * This can be fixed later if a solution is found.
	 */
	if (!IsRingStraightLine(currPtr, numPoints))
	{
		RETURN_FALSE_IF_ERROR_NOT_EXPECTED(
			polygonValidationState->shouldThrowValidityError, (
				errcode(GEO_ERROR_CODE(polygonValidationState->errorCtxt)),
				errmsg("%s Loop is not valid: %s - Edges cross",
					   GEO_ERROR_PREFIX(polygonValidationState->errorCtxt),
					   GetRingPointsStringForError(currPtr, numPoints)),
				errhint("%s Loop is not valid - Edges cross",
						GEO_HINT_PREFIX(polygonValidationState->errorCtxt)
						)));
	}

	return true;
}


/* Check validity of a multi-ring polygon */
static bool
CheckMultiRingPolygonValidity(const WKBGeometryConst *geometryConst,
							  PolygonValidationState *polygonValidationState)
{
	int32 numPoints = geometryConst->numPoints;
	char *currPtr = (char *) geometryConst->ringPointsStart;

	/* Buffer for polygon made from just the current ring */
	StringInfo currentRingBuffer = makeStringInfo();
	WriteHeaderToWKBBuffer(currentRingBuffer, WKBGeometryType_Polygon);
	WriteNumToWKBBuffer(currentRingBuffer, 1);
	WriteNumToWKBBuffer(currentRingBuffer, numPoints);
	WriteBufferWithLengthToWKBBuffer(currentRingBuffer,
									 currPtr, (numPoints) * WKB_BYTE_SIZE_POINT);

	bytea *wkbBytea = WKBBufferGetByteaWithSRID(currentRingBuffer);
	Datum currentRingGeographyDatum = GetGeographyFromWKB(wkbBytea);
	Datum currentRingGeometryDatum = GetGeometryFromWKB(wkbBytea);
	pfree(wkbBytea);
	DeepFreeWKB(currentRingBuffer);

	/*
	 * Check if current ring of polygon is valid in 2d.
	 */
	IsValidDetailState isValidDetailState =
		GetPolygonInvalidityReason(currentRingGeometryDatum, polygonValidationState);

	pfree(DatumGetPointer(currentRingGeometryDatum));

	/* Ring is invalid and not a straight line */
	if (!isValidDetailState.isValid && !IsRingStraightLine(currPtr, numPoints))
	{
		RETURN_FALSE_IF_ERROR_NOT_EXPECTED(
			polygonValidationState->shouldThrowValidityError, (
				errcode(GEO_ERROR_CODE(polygonValidationState->errorCtxt)),
				errmsg("%s Loop is not valid: %s - Edges cross",
					   GEO_ERROR_PREFIX(polygonValidationState->errorCtxt),
					   GetRingPointsStringForError(currPtr, numPoints)),
				errhint("%s Loop is not valid - Edges cross",
						GEO_HINT_PREFIX(polygonValidationState->errorCtxt)
						)));
	}

	return IsHoleFullyCoveredByOuterRing(currPtr, numPoints, polygonValidationState,
										 currentRingGeographyDatum);
}


/* Continue polygon traversal if current ring is valid */
static bool
ContinueIfRingValid(void *state)
{
	PolygonValidationState *polygonValidationState = (PolygonValidationState *) state;
	return polygonValidationState->isValid;
}


/* Initialize polygon validation state */
static void
InitPolygonValidationState(PolygonValidationState *polygonValidationState,
						   GeoJsonParseState *parseState,
						   bytea *polygonWKB)
{
	/* Initialize polygon validation state */
	memset(polygonValidationState, 0, sizeof(PolygonValidationState));

	polygonValidationState->shouldThrowValidityError =
		parseState->shouldThrowValidityError;
	polygonValidationState->errorCtxt = parseState->errorCtxt;
	polygonValidationState->isValid = false;
	polygonValidationState->previousRingPointsStart = NULL;
	polygonValidationState->previousRingDatum = (Datum) 0;
	polygonValidationState->previousRingLineStringGeometryDatum = (Datum) 0;

	polygonValidationState->validationFunctions =
		(FmgrInfo **) palloc0(PostgisFuncsForDollarGeo_MAX * sizeof(FmgrInfo *));
	polygonValidationState->validationFunctions[Geography_Covers] =
		palloc0(sizeof(FmgrInfo));
	fmgr_info(PostgisGeographyCoversFunctionId(),
			  polygonValidationState->validationFunctions[Geography_Covers]);
	polygonValidationState->validationFunctions[Geometry_Intersects] =
		palloc0(sizeof(FmgrInfo));
	fmgr_info(PostgisGeometryIntersectsFunctionId(),
			  polygonValidationState->validationFunctions[Geometry_Intersects]);
	polygonValidationState->validationFunctions[Geometry_IsValidDetail] =
		palloc0(sizeof(FmgrInfo));
	fmgr_info(PostgisGeometryIsValidDetailFunctionId(),
			  polygonValidationState->validationFunctions[Geometry_IsValidDetail]);
}


/* Set isValid and invalidity reason for polygon validation using ST_IsValidDetail func */
static IsValidDetailState
GetPolygonInvalidityReason(Datum polygon, PolygonValidationState *state)
{
	IsValidDetailState isValidDetailState;
	HeapTupleHeader tupleHeader;

	if (state != NULL)
	{
		tupleHeader =
			DatumGetHeapTupleHeader(
				FunctionCall2(state->validationFunctions[Geometry_IsValidDetail],
							  polygon,
							  Int32GetDatum(0)));
	}
	else
	{
		tupleHeader =
			DatumGetHeapTupleHeader(
				OidFunctionCall2(PostgisGeometryIsValidDetailFunctionId(),
								 polygon,
								 Int32GetDatum(0)));
	}

	Oid tupleType = HeapTupleHeaderGetTypeId(tupleHeader);
	int32 tupleTypmod = HeapTupleHeaderGetTypMod(tupleHeader);
	TupleDesc tupleDescriptor = lookup_rowtype_tupdesc(tupleType, tupleTypmod);
	HeapTupleData tupleValue;

	tupleValue.t_len = HeapTupleHeaderGetDatumLength(tupleHeader);
	tupleValue.t_data = tupleHeader;

	bool isNull = false;
	isValidDetailState.isValid = false;
	Datum isValidDatum = heap_getattr(&tupleValue, 1, tupleDescriptor, &isNull);
	if (!isNull)
	{
		isValidDetailState.isValid = DatumGetBool(isValidDatum);
	}

	if (!isValidDetailState.isValid)
	{
		Datum invalidityReasonDatum = heap_getattr(&tupleValue, 2, tupleDescriptor,
												   &isNull);
		if (isNull)
		{
			ReleaseTupleDesc(tupleDescriptor);
			return isValidDetailState;
		}

		isValidDetailState.invalidityReason =
			TextDatumGetCString(invalidityReasonDatum);
	}

	ReleaseTupleDesc(tupleDescriptor);
	return isValidDetailState;
}


/* Check if the current ring is a straight line by checking slopes of all consecutive points */
static bool
IsRingStraightLine(char *pointsStart, int32 numPoints)
{
	float8 *point = (float8 *) pointsStart;
	float8 *nextPoint = (float8 *) (pointsStart + WKB_BYTE_SIZE_POINT);

	bool isEdgeVertical = DOUBLE_EQUALS(nextPoint[0], point[0]);
	float8 slope = (nextPoint[1] - point[1]) / (nextPoint[0] - point[0]);
	for (int i = 1; i < numPoints - 1; i++)
	{
		point = nextPoint;
		nextPoint = (float8 *) (pointsStart + (i + 1) * WKB_BYTE_SIZE_POINT);
		float8 newSlope = (nextPoint[1] - point[1]) / (nextPoint[0] - point[0]);

		if (isEdgeVertical)
		{
			isEdgeVertical = DOUBLE_EQUALS(nextPoint[0], point[0]);

			if (isEdgeVertical)
			{
				continue;
			}
		}

		if (!DOUBLE_EQUALS(newSlope, slope))
		{
			return false;
		}
	}

	return true;
}


/* Check if 2 consecutive rings of polygon intersect or outer ring doesn't fully cover the hole */
static bool
IsHoleFullyCoveredByOuterRing(char *currPtr, int32 numPoints,
							  PolygonValidationState *polygonValidationState,
							  Datum holeDatum)
{
	/* Buffer for linestring made from the points of current ring */
	StringInfo currentRingLineString = makeStringInfo();
	WriteHeaderToWKBBuffer(currentRingLineString, WKBGeometryType_LineString);
	WriteNumToWKBBuffer(currentRingLineString, numPoints);
	WriteBufferWithLengthToWKBBuffer(currentRingLineString,
									 currPtr, (numPoints) * WKB_BYTE_SIZE_POINT);

	bytea *wkbBytea = WKBBufferGetByteaWithSRID(currentRingLineString);
	Datum currentRingLineStringGeometryDatum = GetGeometryFromWKB(wkbBytea);
	pfree(wkbBytea);
	DeepFreeWKB(currentRingLineString);

	/* Check if current ring shares an edge/intersects with outer ring */
	if (polygonValidationState->previousRingDatum != (Datum) 0)
	{
		/* Check if outer ring covers hole*/
		bool isHoleCovered =
			DatumGetBool(
				FunctionCall2(
					polygonValidationState->validationFunctions[Geography_Covers],
					polygonValidationState->previousRingDatum,
					holeDatum));

		if (!isHoleCovered)
		{
			RETURN_FALSE_IF_ERROR_NOT_EXPECTED(
				polygonValidationState->shouldThrowValidityError, (
					errcode(GEO_ERROR_CODE(polygonValidationState->errorCtxt)),
					errmsg(
						"%s Secondary loops not contained by first exterior loop - secondary loops must be holes: %s first loop: %s",
						GEO_ERROR_PREFIX(polygonValidationState->errorCtxt),
						GetRingPointsStringForError(currPtr, numPoints),
						GetRingPointsStringForError(
							polygonValidationState->previousRingPointsStart,
							polygonValidationState->previousRingNumPoints)),
					errhint(
						"%s Secondary loops not contained by first exterior loop - secondary loops must be holes",
						GEO_HINT_PREFIX(polygonValidationState->errorCtxt)
						)));
		}
		else
		{
			/*
			 * ST_Covers returns true in case of overlapping edges.
			 * For this we check intersection between linestring made from current ring and previous ring
			 */
			bool isRingIntersecting =
				DatumGetBool(
					FunctionCall2(
						polygonValidationState->validationFunctions[Geometry_Intersects],
						polygonValidationState->previousRingLineStringGeometryDatum,
						currentRingLineStringGeometryDatum));

			if (isRingIntersecting)
			{
				RETURN_FALSE_IF_ERROR_NOT_EXPECTED(
					polygonValidationState->shouldThrowValidityError, (
						errcode(GEO_ERROR_CODE(polygonValidationState->errorCtxt)),
						errmsg(
							"%s Secondary loops not contained by first exterior loop - secondary loops must be holes: %s first loop: %s",
							GEO_ERROR_PREFIX(polygonValidationState->errorCtxt),
							GetRingPointsStringForError(currPtr, numPoints),
							GetRingPointsStringForError(
								polygonValidationState->previousRingPointsStart,
								polygonValidationState->previousRingNumPoints)),
						errhint(
							"%s Secondary loops not contained by first exterior loop - secondary loops must be holes",
							GEO_HINT_PREFIX(polygonValidationState->errorCtxt)
							)));
			}
		}

		pfree(DatumGetPointer(polygonValidationState->previousRingDatum));
		pfree(DatumGetPointer(
				  polygonValidationState->previousRingLineStringGeometryDatum));
	}

	polygonValidationState->previousRingLineStringGeometryDatum =
		currentRingLineStringGeometryDatum;
	polygonValidationState->previousRingDatum = holeDatum;
	polygonValidationState->previousRingPointsStart = currPtr;
	polygonValidationState->previousRingNumPoints = numPoints;

	return true;
}


/* Write points of current loop to buffer to be used in error msg*/
static char *
GetRingPointsStringForError(char *currPtr, int32 numPoints)
{
	StringInfo loop = makeStringInfo();
	appendStringInfo(loop, "[ ");

	float8 *point;

	for (int i = 0; i < numPoints; i++)
	{
		/* Get the x and y coordinates of the current point */
		point = (float8 *) (currPtr + i * WKB_BYTE_SIZE_POINT);

		/* Append the point to the error buffer */
		appendStringInfo(loop, "[%f, %f]%s", point[0], point[1],
						 ((i < numPoints - 1) ? ", " : " ]"));
	}

	return loop->data;
}


/*
 * Recursively traverse a GeoJSON document value performing validation
 * for types and values and also keeps creating the WKB for the geography
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
