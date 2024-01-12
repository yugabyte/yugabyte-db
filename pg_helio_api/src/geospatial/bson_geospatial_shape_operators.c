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
static Datum BsonValueGetBox(const bson_value_t *value, MongoQueryOperatorType operator);
static Datum BsonValueGetPolygon(const bson_value_t *value, MongoQueryOperatorType
								 operator);
static Datum BsonValueGetCenter(const bson_value_t *value, MongoQueryOperatorType
								operator);
static Datum BsonValueGetCenterSphere(const bson_value_t *value,
									  MongoQueryOperatorType operator);
static Datum BsonValueGetGeometry(const bson_value_t *value, MongoQueryOperatorType
								  operator);


/*
 * Shape operators that can be used by $geoWithin operator
 */
static const ShapeOperator GeoWithinShapeOperators[] = {
	{
		.shapeOperatorName = "$box",
		.shape = GeospatialShapeOperator_BOX,
		.isSpherical = false,
		.getShapeDatum = BsonValueGetBox
	},
	{
		.shapeOperatorName = "$center",
		.shape = GeospatialShapeOperator_CENTER,
		.isSpherical = false,
		.getShapeDatum = BsonValueGetCenter
	},
	{
		.shapeOperatorName = "$centerSphere",
		.shape = GeospatialShapeOperator_CENTERSPHERE,
		.isSpherical = true,
		.getShapeDatum = BsonValueGetCenterSphere,
	},
	{
		.shapeOperatorName = "$geometry",
		.shape = GeospatialShapeOperator_GEOMETRY,
		.isSpherical = true,
		.getShapeDatum = BsonValueGetGeometry
	},
	{
		.shapeOperatorName = "$polygon",
		.shape = GeospatialShapeOperator_POLYGON,
		.isSpherical = false,
		.getShapeDatum = BsonValueGetPolygon
	},
	{
		.shapeOperatorName = NULL,
		.shape = GeospatialShapeOperator_UNKNOWN,
		.isSpherical = false,
		.getShapeDatum = NULL
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
 * For more details look at https://www.mongodb.com/docs/manual/geospatial-queries/#geospatial-models
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
		if (shapeOperator->shape == GeospatialShapeOperator_UNKNOWN)
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
BsonValueGetBox(const bson_value_t *shapePointValue, MongoQueryOperatorType operator)
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
BsonValueGetPolygon(const bson_value_t *shapeValue, MongoQueryOperatorType operator)
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
BsonValueGetCenter(const bson_value_t *shapeValue, MongoQueryOperatorType operator)
{
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
			if (!BsonValueIsNumber(value) || BsonValueAsDouble(value) < 0)
			{
				ereport(ERROR, (errcode(MongoBadValue),
								errmsg("radius must be a non-negative number")));
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
	if (index == 1)
	{
		ereport(ERROR, (errcode(MongoBadValue),
						errmsg("radius must be a non-negative number")));
	}

	/* Return geometry of the center with default SRID */
	return DatumWithDefaultSRID(OidFunctionCall2(PostgisBufferFunctionId(),
												 centerPoint,
												 Float8GetDatum(radius)));
}


/*
 * For a {$geomerty: <GeoJSON>} shape operator return the respective
 * geography.
 */
static Datum
BsonValueGetGeometry(const bson_value_t *value, MongoQueryOperatorType operator)
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

	if (operator == QUERY_OPERATOR_GEOWITHIN &&
		parseState.type != GeoJsonType_POLYGON && parseState.type !=
		GeoJsonType_MULTIPOLYGON)
	{
		ereport(ERROR, (
					errcode(MongoBadValue),
					errmsg("$geoWithin not supported with provided geometry: %s",
						   BsonValueToJsonForLogging(value)),
					errhint("$geoWithin not supported with provided geometry.")));
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
						errmsg("Big polygons are not supported yet.")));
		}
	}

	bytea *wkbBytea = WKBBufferGetByteaWithSRID(parseState.buffer);
	Datum result = GetGeographyFromWKB(wkbBytea);

	/* Reset the buffer */
	pfree(wkbBytea);
	pfree(parseState.buffer);

	return result;
}


/*
 * For a {$centerSphere: ...} shape operator return the respective
 * geography. For now it throws error to avoid Seg faults if it is used
 */
static Datum
BsonValueGetCenterSphere(const bson_value_t *value,
						 MongoQueryOperatorType operator)
{
	ereport(ERROR, (errcode(MongoCommandNotSupported),
					errmsg("$centerSphere is not supported yet")));
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
