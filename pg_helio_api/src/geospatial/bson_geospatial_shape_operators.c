/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/geospatial/bson_geospatial_shape_operators.c
 *
 * Methods for supporting multiple shape operators of mongodb
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "utils/array.h"
#include "utils/lsyscache.h"
#include "utils/float.h"

#include "utils/mongo_errors.h"
#include "geospatial/bson_geospatial_common.h"
#include "geospatial/bson_geojson_utils.h"
#include "geospatial/bson_geospatial_shape_operators.h"
#include "utils/list_utils.h"

/*
 * This defines the bounds of a rectangle in 2D space.
 */
typedef struct ShapeBox
{
	double xBottomLeft;
	double yBottomLeft;
	double xUpperRight;
	double yUpperRight;
} ShapeBox;


static const ShapeOperator * GetShapeOperatorByName(const char *operatorName);
static Datum GetLegacyPointDatum(double longitude, double latitude);

/* Shape operators */
static Datum BsonValueGetBox(const bson_value_t *value, ShapeOperatorInfo *opInfo);
static Datum BsonValueGetPolygon(const bson_value_t *value, ShapeOperatorInfo *opInfo);
static Datum BsonValueGetCenter(const bson_value_t *value, ShapeOperatorInfo *opInfo);
static Datum BsonValueGetCenterSphere(const bson_value_t *value,
									  ShapeOperatorInfo *opInfo);
static Datum BsonValueGetGeometry(const bson_value_t *value, ShapeOperatorInfo *opInfo);

/* Error margin for $center radius */
static const double RADIUS_ERROR_MARGIN = 9e-15;


/*
 * Shape operators that can be used by $geoWithin operator
 */
static const ShapeOperator GeoWithinShapeOperators[] = {
	{
		.shapeOperatorName = "$box",
		.op = GeospatialShapeOperator_BOX,
		.isSpherical = false,
		.getShapeDatum = BsonValueGetBox,
		.shouldSegmentize = false
	},
	{
		.shapeOperatorName = "$center",
		.op = GeospatialShapeOperator_CENTER,
		.isSpherical = false,
		.getShapeDatum = BsonValueGetCenter,
		.shouldSegmentize = false
	},
	{
		.shapeOperatorName = "$centerSphere",
		.op = GeospatialShapeOperator_CENTERSPHERE,
		.isSpherical = true,
		.getShapeDatum = BsonValueGetCenterSphere,
		.shouldSegmentize = false
	},
	{
		.shapeOperatorName = "$geometry",
		.op = GeospatialShapeOperator_GEOMETRY,
		.isSpherical = true,
		.getShapeDatum = BsonValueGetGeometry,
		.shouldSegmentize = true
	},
	{
		.shapeOperatorName = "$polygon",
		.op = GeospatialShapeOperator_POLYGON,
		.isSpherical = false,
		.getShapeDatum = BsonValueGetPolygon,
		.shouldSegmentize = true
	},
	{
		/* This is a deprecated shape operator but tests still use it to make assertions, ignore this */
		.shapeOperatorName = "$uniqueDocs",
		.op = GeospatialShapeOperator_UNIQUEDOCS,
		.isSpherical = false,
		.getShapeDatum = NULL,
		.shouldSegmentize = false
	},
	{
		.shapeOperatorName = NULL,
		.op = GeospatialShapeOperator_UNKNOWN,
		.isSpherical = false,
		.getShapeDatum = NULL,
		.shouldSegmentize = false
	}
};


/*
 * Sets the Default SRID to the given geometry datum.
 * It uses the postgis function ST_SetSRID(geom, srid) to set
 * the srid to the given geometry.
 */
static inline Datum
DatumWithDefaultSRID(Datum datum)
{
	return OidFunctionCall2(PostgisSetSRIDFunctionId(),
							datum,
							Int32GetDatum(DEFAULT_GEO_SRID));
}


/*
 * This method parses the $geoWithin doc value for shapes operators
 * & return the last shape operator (if multiple are given) and
 * also sets the shapePointsOut to the value of that particular shape
 *
 * e.g. { $box: [[10, 10], [20, 20]], $center: [[10, 10], 10]}
 * returns $center shape operator & sets shapePointsOut to [[10, 10], 10]
 *
 * There are 5 shape operators defined by Mongo:
 * 1- $geometry - Spherical shape
 * 2- $box - Flat shape
 * 3- $polygon - Flat shape
 * 4- $center - Flat shape
 * 5- $centerSphere - Spherical shape
 *
 */
const ShapeOperator *
GetShapeOperatorByValue(const bson_value_t *shapeValue, bson_value_t *shapePointsOut)
{
	bson_iter_t shapeIter;
	BsonValueInitIterator(shapeValue, &shapeIter);

	/*
	 * First pass finds the last valid shape operators's index.
	 * Mostly done to maintain the `const` sanity of shape operators
	 * and also avoid copying all the points value to shapePointsOut
	 * along the way
	 *
	 * In the second pass at that index get the points out and const operator
	 */
	int indexOfValidShapeOperator = -1;
	while (bson_iter_next(&shapeIter))
	{
		const char *shapeOperatorName = bson_iter_key(&shapeIter);
		const bson_value_t *shapeOperatorValue = bson_iter_value(&shapeIter);
		const ShapeOperator *shapeOperator = GetShapeOperatorByName(shapeOperatorName);

		/* If anytime we encounter non existing shape operator return error */
		if (shapeOperator->op == GeospatialShapeOperator_UNKNOWN)
		{
			ereport(ERROR, (errcode(MongoBadValue),
							errmsg("unknown geo specifier: %s: %s",
								   shapeOperatorName,
								   BsonValueToJsonForLogging(shapeOperatorValue)),
							errhint(
								"unknown geo specifier operator %s with argument of type %s",
								shapeOperatorName,
								BsonTypeName(shapeOperatorValue->value_type))));
		}

		if (shapeOperator->op == GeospatialShapeOperator_UNIQUEDOCS)
		{
			/* Ignore $unique for shape operators, this is deprecated but tests */
			/* assert this jstests/core/geo_uniqueDocs.js */
			continue;
		}
		indexOfValidShapeOperator++;
	}

	BsonValueInitIterator(shapeValue, &shapeIter);
	int index = 0;
	while (index <= indexOfValidShapeOperator)
	{
		bson_iter_next(&shapeIter);
		index++;
	}
	const ShapeOperator *validShapeOperator =
		GetShapeOperatorByName(bson_iter_key(&shapeIter));
	*shapePointsOut = *bson_iter_value(&shapeIter);

	return validShapeOperator;
}


/*
 * Returns the ShapeOperator for the given operatorName
 */
static const ShapeOperator *
GetShapeOperatorByName(const char *operatorName)
{
	int index;
	for (index = 0; GeoWithinShapeOperators[index].shapeOperatorName != NULL; index++)
	{
		if (strcmp(operatorName, GeoWithinShapeOperators[index].shapeOperatorName) == 0)
		{
			return &GeoWithinShapeOperators[index];
		}
	}

	/* We reached the end return the last UNKNOWN shape operator */
	return &GeoWithinShapeOperators[index];
}


/*
 * For a {$box: <...>} shape operator returns the
 * box as a geometry datum
 */
static Datum
BsonValueGetBox(const bson_value_t *shapePointValue, ShapeOperatorInfo *opInfo)
{
	if (shapePointValue->value_type != BSON_TYPE_ARRAY &&
		shapePointValue->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(MongoBadValue),
						errmsg("unknown geo specifier: $box: %s",
							   BsonValueToJsonForLogging(shapePointValue)),
						errhint("unknown geo specifier: $box with argument type %s",
								BsonTypeName(shapePointValue->value_type))));
	}

	/*
	 * $box needs 2 points to define the box which are given in legacy
	 * coordinate format
	 */
	bson_iter_t boxValueIter;
	BsonValueInitIterator(shapePointValue, &boxValueIter);
	int16 index = 0;
	ShapeBox box = {
		.xBottomLeft = 0,
		.yBottomLeft = 0,
		.xUpperRight = 0,
		.yUpperRight = 0
	};

	while (bson_iter_next(&boxValueIter) && index < 2)
	{
		const bson_value_t *value = bson_iter_value(&boxValueIter);
		if (value->value_type != BSON_TYPE_ARRAY &&
			value->value_type != BSON_TYPE_DOCUMENT)
		{
			ereport(ERROR, (errcode(MongoBadValue),
							errmsg("Point must be an array or object")));
		}

		Point point;
		memset(&point, 0, sizeof(Point));

		bool throwError = true;

		GeospatialErrorContext errCtxt;
		memset(&errCtxt, 0, sizeof(GeospatialErrorContext));
		errCtxt.errCode = MongoBadValue;

		ParseBsonValueAsPoint(value, throwError, &errCtxt, &point);
		if (index == 0)
		{
			box.xBottomLeft = point.x;
			box.yBottomLeft = point.y;
		}
		else
		{
			box.xUpperRight = point.x;
			box.yUpperRight = point.y;
		}
		index++;
	}

	if (index < 2)
	{
		/*
		 * If 2 points are not given, the box is not defined
		 */
		ereport(ERROR, (errcode(MongoBadValue),
						errmsg("Point must be an array or object")));
	}

	/*
	 * If Box bounds are same then we don't want to create a polygon because
	 * same point is not considered to be covered by the polygon
	 * e.g. Polygon: [[10, 10], [10, 10], [10, 10], [10, 10]]
	 * Point: [10, 10]
	 */
	if (box.xBottomLeft == box.xUpperRight &&
		box.yBottomLeft == box.yUpperRight)
	{
		Datum boxBoundPoint = OidFunctionCall2(PostgisMakePointFunctionId(),
											   Float8GetDatum(box.xBottomLeft),
											   Float8GetDatum(box.yBottomLeft));
		Datum boxBoundPointsArray[2] = { boxBoundPoint, boxBoundPoint };
		Oid geometryTypeOid = GeometryTypeId();
		int16 typLen;
		bool typByVal;
		char typAlign;
		get_typlenbyvalalign(geometryTypeOid, &typLen, &typByVal, &typAlign);
		ArrayType *pointsArray = construct_array(boxBoundPointsArray, 2, geometryTypeOid,
												 typLen,
												 typByVal, typAlign);
		Datum lineStringDatum = OidFunctionCall1(PostgisMakeLineFunctionId(),
												 PointerGetDatum(pointsArray));
		return DatumWithDefaultSRID(lineStringDatum);
	}

	/* Return geometry of the box with default SRID, this returns a Polygon */
	return OidFunctionCall5(PostgisMakeEnvelopeFunctionId(),
							Float8GetDatum(box.xBottomLeft),
							Float8GetDatum(box.yBottomLeft),
							Float8GetDatum(box.xUpperRight),
							Float8GetDatum(box.yUpperRight),
							Int64GetDatum(DEFAULT_GEO_SRID));
}


/*
 * For a {$polygon: <...>} shape operator returns the
 * polygon as a geometry datum. The polygon is defined by
 * at least 3 points and the last point if not same as
 * first one is implicitly assumed to be there to make it
 * a closed ring
 */
static Datum
BsonValueGetPolygon(const bson_value_t *shapeValue, ShapeOperatorInfo *opInfo)
{
	if (shapeValue->value_type != BSON_TYPE_ARRAY &&
		shapeValue->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(MongoBadValue),
						errmsg("unknown geo specifier: $polygon: %s",
							   BsonValueToJsonForLogging(shapeValue)),
						errhint("unknown geo specifier: $polygon with argument type %s",
								BsonTypeName(shapeValue->value_type))));
	}

	/* First pass to just check if all points are same */
	bool allPointsSame = true;

	bson_iter_t valueIter;
	BsonValueInitIterator(shapeValue, &valueIter);
	double lastLong = 0;
	double lastLat = 0;
	int index = 0;
	while (bson_iter_next(&valueIter))
	{
		const bson_value_t *value = bson_iter_value(&valueIter);
		if (value->value_type != BSON_TYPE_ARRAY &&
			value->value_type != BSON_TYPE_DOCUMENT)
		{
			ereport(ERROR, (errcode(MongoBadValue),
							errmsg("Point must be an array or object")));
		}

		Point point;
		memset(&point, 0, sizeof(Point));

		bool throwError = true;

		GeospatialErrorContext errCtxt;
		memset(&errCtxt, 0, sizeof(GeospatialErrorContext));
		errCtxt.errCode = MongoBadValue;

		ParseBsonValueAsPoint(value, throwError, &errCtxt, &point);
		if (index == 0)
		{
			lastLong = point.x;
			lastLat = point.y;
		}
		else if (lastLong != point.x || lastLat != point.y)
		{
			allPointsSame = false;
			break;
		}
		index++;
	}

	BsonValueInitIterator(shapeValue, &valueIter);
	double firstLong = 0;
	double firstLat = 0;
	index = 0;
	List *pointsList = NIL;
	while (bson_iter_next(&valueIter))
	{
		const bson_value_t *value = bson_iter_value(&valueIter);
		if (value->value_type != BSON_TYPE_ARRAY &&
			value->value_type != BSON_TYPE_DOCUMENT)
		{
			ereport(ERROR, (errcode(MongoBadValue),
							errmsg("Point must be an array or object")));
		}

		Point point;
		memset(&point, 0, sizeof(Point));

		bool throwError = true;

		GeospatialErrorContext errCtxt;
		memset(&errCtxt, 0, sizeof(GeospatialErrorContext));
		errCtxt.errCode = MongoBadValue;

		ParseBsonValueAsPoint(value, throwError, &errCtxt, &point);
		if (index == 0)
		{
			firstLong = point.x;
			firstLat = point.y;
		}
		else
		{
			lastLong = point.x;
			lastLat = point.y;
		}

		/* Make a point and insert in list */
		Datum pointGeometry = OidFunctionCall2(PostgisMakePointFunctionId(),
											   Float8GetDatum(point.x),
											   Float8GetDatum(point.y));
		pointsList = lappend(pointsList, DatumGetPointer(pointGeometry));

		index++;
	}

	if (index < 3)
	{
		/* If 3 points are not given, the polygon is not defined
		 */
		ereport(ERROR, (errcode(MongoBadValue),
						errmsg("Polygon must have at least 3 points")));
	}

	/* Check if last and first points are not same then add first point implicitly */
	if (firstLong != lastLong || firstLat != lastLat)
	{
		Datum point = OidFunctionCall2(PostgisMakePointFunctionId(),
									   Float8GetDatum(firstLong),
									   Float8GetDatum(firstLat));
		pointsList = lappend(pointsList,
							 DatumGetPointer(point));
	}

	/*
	 * If all points are same just return a LINESTRING, polygon with only one point
	 * does not cover that same point
	 * e.g. Polygon [[10, 10], [10, 10], [10, 10], [10, 10]]
	 * Point [10, 10] => This is not treated as covered by the polygon
	 */
	ArrayType *pointsArray = PointerListGetPgArray(pointsList, GeometryTypeId());
	Datum lineStringDatum = OidFunctionCall1(PostgisMakeLineFunctionId(),
											 PointerGetDatum(pointsArray));
	if (allPointsSame)
	{
		return DatumWithDefaultSRID(lineStringDatum);
	}
	return DatumWithDefaultSRID(OidFunctionCall1(
									PostgisMakePolygonFunctionId(),
									lineStringDatum));
}


/*
 * For a {$center: <...>} shape operator returns the
 * circle as a geometry datum. The circle is defined by
 * a center point and a radius
 */
static Datum
BsonValueGetCenter(const bson_value_t *shapeValue, ShapeOperatorInfo *opInfo)
{
	Assert(opInfo != NULL);

	if (shapeValue->value_type != BSON_TYPE_ARRAY &&
		shapeValue->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(MongoBadValue),
						errmsg("unknown geo specifier: $center: %s",
							   BsonValueToJsonForLogging(shapeValue)),
						errhint("unknown geo specifier: $center with argument type %s",
								BsonTypeName(shapeValue->value_type))));
	}


	bson_iter_t centerValueIter;
	BsonValueInitIterator(shapeValue, &centerValueIter);
	int16 index = 0;
	Datum centerPoint = 0;
	double radius = 0.0;
	while (bson_iter_next(&centerValueIter))
	{
		if (index > 1)
		{
			ereport(ERROR, (errcode(MongoBadValue),
							errmsg("Only 2 fields allowed for circular region")));
		}
		const bson_value_t *value = bson_iter_value(&centerValueIter);
		if (index == 0)
		{
			if (value->value_type != BSON_TYPE_ARRAY &&
				value->value_type != BSON_TYPE_DOCUMENT)
			{
				ereport(ERROR, (errcode(MongoBadValue),
								errmsg("Point must be an array or object")));
			}
			else
			{
				Point point;
				memset(&point, 0, sizeof(Point));

				bool throwError = true;

				GeospatialErrorContext errCtxt;
				memset(&errCtxt, 0, sizeof(GeospatialErrorContext));
				errCtxt.errCode = MongoBadValue;

				ParseBsonValueAsPoint(value, throwError, &errCtxt, &point);
				centerPoint = GetLegacyPointDatum(point.x, point.y);
			}
		}

		if (index == 1)
		{
			if (!BsonValueIsNumber(value) || BsonValueAsDouble(value) < 0 ||
				IsBsonValueNaN(value))
			{
				ereport(ERROR, (errcode(MongoBadValue),
								errmsg("radius must be a non-negative number")));
			}
			else if (IsBsonValueInfinity(value))
			{
				DollarCenterOperatorState *state = palloc0(
					sizeof(DollarCenterOperatorState));
				state->isRadiusInfinite = true;
				opInfo->opState = (ShapeOperatorState *) state;
				return (Datum) 0;
			}
			{
				radius = BsonValueAsDouble(value);

				/*
				 * This is to counter the effect of double precision error in postgres.
				 * SELECT ST_DWITHIN('Point(5 52)'::geometry, 'Point(5 52.0001)'::geometry, 0.0001) returns false as postgres stores
				 * 52.0001 as 52.00010000000000332 which makes it go beyond the radius and the result is a false negative.
				 * Adding this error margin will adjust the radius to bring it in range with the padding added to the point coordinates.
				 */
				radius += RADIUS_ERROR_MARGIN;
			}
		}
		index++;
	}

	if (index == 0)
	{
		ereport(ERROR, (errcode(MongoBadValue),
						errmsg("Point must be an array or object")));
	}
	else if (index == 1)
	{
		ereport(ERROR, (errcode(MongoBadValue),
						errmsg("radius must be a non-negative number")));
	}

	DollarCenterOperatorState *state = palloc0(sizeof(DollarCenterOperatorState));
	state->isRadiusInfinite = false;
	state->radius = radius;
	opInfo->opState = (ShapeOperatorState *) state;

	if (opInfo->queryStage == QueryStage_INDEX)
	{
		/* Return expanded geometry for index pushdown. */
		return OidFunctionCall2(PostgisGeometryExpandFunctionId(),
								centerPoint,
								Float8GetDatum(radius));
	}

	return centerPoint;
}


/*
 * For a {$geomerty: <GeoJSON>} shape operator return the respective
 * geography.
 */
static Datum
BsonValueGetGeometry(const bson_value_t *value, ShapeOperatorInfo *opInfo)
{
	if (value->value_type != BSON_TYPE_DOCUMENT && value->value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(MongoBadValue),
						errmsg("unknown geo specifier: $geometry: %s",
							   BsonValueToJsonForLogging(value)),
						errhint("unknown geo specifier: $geometry with argument type %s",
								BsonTypeName(value->value_type))));
	}

	ParseFlags parseFlags = ParseFlag_None;
	if (value->value_type == BSON_TYPE_ARRAY)
	{
		/* This can only be legacy */
		parseFlags = parseFlags | ParseFlag_Legacy;
	}
	else
	{
		/* This can be either legacy or geojson, so parse it as legacy without error and then geojson */
		parseFlags = parseFlags | ParseFlag_Legacy_NoError | ParseFlag_GeoJSON_All;
	}

	GeoJsonParseState parseState;
	memset(&parseState, 0, sizeof(GeoJsonParseState));

	/* Throw error on validity */
	parseState.shouldThrowValidityError = true;
	parseState.buffer = makeStringInfo();

	parseState.errorCtxt = palloc0(sizeof(GeospatialErrorContext));
	parseState.errorCtxt->errCode = MongoBadValue;

	bool isValid = BsonValueGetGeometryWKB(value, parseFlags, &parseState);

	if (!isValid)
	{
		/* This is not expected, as we would have already thrown error if any validity issues are there */
		ereport(ERROR, (
					errcode(MongoInternalError),
					errmsg("$geometry: could not extract a valid geo value")));
	}

	if (IsGeoWithinQueryOperator(opInfo->queryOperatorType) &&
		parseState.type != GeoJsonType_POLYGON && parseState.type !=
		GeoJsonType_MULTIPOLYGON)
	{
		ereport(ERROR, (
					errcode(MongoBadValue),
					errmsg("$geoWithin not supported with provided geometry: %s",
						   BsonValueToJsonForLogging(value)),
					errhint("$geoWithin not supported with provided geometry.")));
	}

	if (IsGeoWithinQueryOperator(opInfo->queryOperatorType) &&
		parseState.numOfRingsInPolygon > 1)
	{
		/* TODO: Fix polygon with holes geowithin comparision, for now we throw unsupported error because of
		 * Postgis matching difference for these cases
		 */
		ereport(ERROR, (
					errcode(MongoCommandNotSupported),
					errmsg("$geoWithin currently doesn't support polygons with holes")
					));
	}

	if (parseState.crs != NULL && strcmp(parseState.crs, GEOJSON_CRS_BIGPOLYGON) == 0)
	{
		if (parseState.type != GeoJsonType_POLYGON)
		{
			ereport(ERROR, (
						errcode(MongoCommandNotSupported),
						errmsg("Strict winding order is only supported by Polygon.")));
		}
		else
		{
			/*
			 * TODO: Support for big polygon here in future, Postgis natively doesn't
			 * support this
			 */
			ereport(ERROR, (
						errcode(MongoCommandNotSupported),
						errmsg("Custom CRS for big polygon is not supported yet.")));
		}
	}

	bytea *wkbBytea = WKBBufferGetByteaWithSRID(parseState.buffer);
	Datum result = GetGeographyFromWKB(wkbBytea);

	/* Reset the buffer */
	pfree(wkbBytea);
	DeepFreeWKB(parseState.buffer);

	return result;
}


/*
 * For a {$centerSphere: ...} shape operator return the respective
 * geography.
 */
static Datum
BsonValueGetCenterSphere(const bson_value_t *shapeValue, ShapeOperatorInfo *opInfo)
{
	Assert(opInfo != NULL);

	if (shapeValue->value_type != BSON_TYPE_ARRAY &&
		shapeValue->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(MongoBadValue),
						errmsg("unknown geo specifier: $centerSphere: %s",
							   BsonValueToJsonForLogging(shapeValue)),
						errhint(
							"unknown geo specifier: $centerSphere with argument type %s",
							BsonTypeName(shapeValue->value_type))));
	}

	bson_iter_t centerValueIter;
	BsonValueInitIterator(shapeValue, &centerValueIter);
	int16 index = 0;
	Point point;
	double radius = 0.0;
	while (bson_iter_next(&centerValueIter))
	{
		if (index > 1)
		{
			ereport(ERROR, (errcode(MongoBadValue),
							errmsg("Only 2 fields allowed for circular region")));
		}
		const bson_value_t *value = bson_iter_value(&centerValueIter);
		if (index == 0)
		{
			if (value->value_type != BSON_TYPE_ARRAY &&
				value->value_type != BSON_TYPE_DOCUMENT)
			{
				ereport(ERROR, (errcode(MongoBadValue),
								errmsg("Point must be an array or object")));
			}
			else
			{
				memset(&point, 0, sizeof(Point));

				bool throwError = true;

				GeospatialErrorContext errCtxt;
				memset(&errCtxt, 0, sizeof(GeospatialErrorContext));
				errCtxt.errCode = MongoBadValue;

				ParseBsonValueAsPointWithBounds(value, throwError, &errCtxt, &point);
			}
		}

		if (index == 1)
		{
			if (!BsonValueIsNumber(value) || BsonValueAsDouble(value) < 0 ||
				IsBsonValueNaN(value))
			{
				ereport(ERROR, (errcode(MongoBadValue),
								errmsg("radius must be a non-negative number")));
			}
			else if (IsBsonValueInfinity(value))
			{
				DollarCenterOperatorState *state = palloc0(
					sizeof(DollarCenterOperatorState));
				state->isRadiusInfinite = true;
				opInfo->opState = (ShapeOperatorState *) state;
				return (Datum) 0;
			}
			else
			{
				radius = BsonValueAsDouble(value);
			}
		}
		index++;
	}

	if (index == 0)
	{
		ereport(ERROR, (errcode(MongoBadValue),
						errmsg("Point must be an array or object")));
	}
	else if (index == 1)
	{
		ereport(ERROR, (errcode(MongoBadValue),
						errmsg("radius must be a non-negative number")));
	}

	DollarCenterOperatorState *state = palloc0(sizeof(DollarCenterOperatorState));

	/*
	 * If radius is greater than pi/2 but less than pi, that means an area greater than hemishpere of earth
	 * is being considered. In that case we also need to check intersection with compliment area.
	 * If it is greater than pi, that means whole earth is covered so we can consider radius to be infinite
	 * and return matched for all valid docs.
	 */
	if (radius > (M_PI / 2) &&
		radius < M_PI)
	{
		Point antipode;
		if (point.x > 0)
		{
			antipode.x = point.x - 180;
		}
		else
		{
			antipode.x = point.x + 180;
		}

		antipode.y = -point.y;

		StringInfo buffer = makeStringInfo();

		/* write to buffer */
		WriteHeaderToWKBBuffer(buffer, WKBGeometryType_Point);
		WritePointToWKBBuffer(buffer, &antipode);
		bytea *wkbBytea = WKBBufferGetByteaWithSRID(buffer);
		Datum antipodeDatum = GetGeographyFromWKB(wkbBytea);
		double complimentRadius = (M_PI - radius) * RADIUS_OF_EARTH_M;
		Datum complimentArea = DatumWithDefaultSRID(OidFunctionCall2(
														PostgisGeographyBufferFunctionId(),
														antipodeDatum,
														Float8GetDatum(
															complimentRadius)));
		state->complimentArea = complimentArea;

		DeepFreeWKB(buffer);
		pfree(wkbBytea);
		pfree(DatumGetPointer(antipodeDatum));
	}
	else if (radius >= M_PI)
	{
		state->isRadiusInfinite = true;
		opInfo->opState = (ShapeOperatorState *) state;
		return (Datum) 0;
	}

	Datum centerPoint = 0;
	StringInfo buffer = makeStringInfo();

	/* Valid point, write to buffer */
	WriteHeaderToWKBBuffer(buffer, WKBGeometryType_Point);
	WritePointToWKBBuffer(buffer, &point);
	bytea *wkbBytea = WKBBufferGetByteaWithSRID(buffer);
	centerPoint = GetGeographyFromWKB(wkbBytea);
	pfree(wkbBytea);
	DeepFreeWKB(buffer);

	state->isRadiusInfinite = false;
	state->radius = radius;

	opInfo->opState = (ShapeOperatorState *) state;

	state->radiusInMeters = radius * RADIUS_OF_EARTH_M;

	if (opInfo->queryStage == QueryStage_INDEX)
	{
		/* Return geography with expanded area for index pushdown. */
		return OidFunctionCall2(PostgisGeographyExpandFunctionId(),
								centerPoint,
								Float8GetDatum(state->radiusInMeters));
	}

	return centerPoint;
}


/*
 * Returns the point from the given longitude and latitude.
 * This function uses the postgis function ST_MakePoint(longitude, latitude)
 * to make geometry point out of the values
 */
Datum
GetLegacyPointDatum(double longitude, double latitude)
{
	Datum pointDatum = OidFunctionCall2(PostgisMakePointFunctionId(),
										Float8GetDatum(longitude),
										Float8GetDatum(latitude));

	return DatumWithDefaultSRID(pointDatum);
}
