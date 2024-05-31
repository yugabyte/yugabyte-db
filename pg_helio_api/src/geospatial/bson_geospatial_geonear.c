/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/geospatial/bson_geospatial_geonear.c
 *
 * Implementation for methods needed for $geoNear aggregation stage
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <float.h>
#include <math.h>
#include <miscadmin.h>
#include <catalog/pg_am.h>
#include <nodes/makefuncs.h>
#include <nodes/pg_list.h>
#include <nodes/nodeFuncs.h>
#include <optimizer/optimizer.h>
#include <optimizer/pathnode.h>

#include "geospatial/bson_geospatial_common.h"
#include "geospatial/bson_geospatial_geonear.h"
#include "utils/fmgr_utils.h"
#include "utils/version_utils.h"

static bool GeonearDistanceWithinRange(const GeonearDistanceState *state,
									   const pgbson *document);

/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */

PG_FUNCTION_INFO_V1(bson_geonear_distance);
PG_FUNCTION_INFO_V1(bson_geonear_within_range);


/*
 * Calculates the distance between the document and a reference point
 * for a $geoNear stage.
 */
Datum
bson_geonear_distance(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON_PACKED(0);
	const pgbson *queryBson = PG_GETARG_PGBSON_PACKED(1);

	const GeonearDistanceState *state;
	int argPosition = 1;

	SetCachedFunctionState(
		state,
		GeonearDistanceState,
		argPosition,
		BuildGeoNearDistanceState,
		queryBson);

	if (state == NULL)
	{
		GeonearDistanceState projectionState;
		memset(&projectionState, 0, sizeof(GeonearDistanceState));
		BuildGeoNearDistanceState(&projectionState, queryBson);
		PG_RETURN_FLOAT8(GeonearDistanceFromDocument(&projectionState, document));
	}

	float8 distance = GeonearDistanceFromDocument(state, document);
	PG_RETURN_FLOAT8(distance);
}


/*
 * Implements the distance range checks for the $geoNear stage using
 * $minDistance and $maxDistance.
 */
Datum
bson_geonear_within_range(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON_PACKED(0);
	const pgbson *queryBson = PG_GETARG_PGBSON_PACKED(1);

	const GeonearDistanceState *state;
	int argPosition = 1;

	SetCachedFunctionState(
		state,
		GeonearDistanceState,
		argPosition,
		BuildGeoNearRangeDistanceState,
		queryBson);

	if (state == NULL)
	{
		GeonearDistanceState projectionState;
		memset(&projectionState, 0, sizeof(GeonearDistanceState));
		BuildGeoNearRangeDistanceState(&projectionState, queryBson);
		PG_RETURN_BOOL(GeonearDistanceWithinRange(&projectionState, document));
	}

	PG_RETURN_BOOL(GeonearDistanceWithinRange(state, document));
}


/*
 * Returns the distance between the document and the reference point of geonear
 * distance is in:
 *      meters: if reference point is GeoJSON or if reference point is legacy and spherical calculation is needed
 *      cartesian distance: if reference point is legacy and spherical calculation is not needed
 */
float8
GeonearDistanceFromDocument(const GeonearDistanceState *state,
							const pgbson *document)
{
	/* Extract from doc */
	Datum points;
	if (state->mode == DistanceMode_Cartesian)
	{
		points = BsonExtractGeometryRuntime(document, &state->key);
	}
	else
	{
		points = BsonExtractGeographyRuntime(document, &state->key);
	}

	float8 distance = DatumGetFloat8(FunctionCall2(state->distanceFnInfo, points,
												   state->referencePoint));
	return distance;
}


/*
 * Checks if the distance is within the range of min and max distance
 */
static bool
GeonearDistanceWithinRange(const GeonearDistanceState *state,
						   const pgbson *document)
{
	float8 distance = GeonearDistanceFromDocument(state, document);

	if (state->maxDistance != NULL && distance > *(state->maxDistance) &&
		fabs(distance - *(state->maxDistance)) > DBL_EPSILON)
	{
		return false;
	}

	if (state->minDistance != NULL && distance < *(state->minDistance) &&
		fabs(distance - *(state->minDistance)) > DBL_EPSILON)
	{
		return false;
	}

	return true;
}


/*
 * Builds a cacheable GeoNear Distance state.
 */
void
BuildGeoNearDistanceState(GeonearDistanceState *state,
						  const pgbson *geoNearQuery)
{
	GeonearRequest *request = ParseGeonearRequest(geoNearQuery);

	state->key = CreateStringViewFromString(pstrdup(request->key));
	state->distanceField = CreateStringViewFromString(pstrdup(request->distanceField));
	state->distanceMultiplier = request->distanceMultiplier;
	if (request->includeLocs != NULL)
	{
		state->includeLocs = CreateStringViewFromString(pstrdup(request->includeLocs));
	}

	/* Set the distance mode */
	if (request->isGeoJsonPoint)
	{
		state->mode = DistanceMode_Spherical;
	}
	else if (Is2dWithSphericalDistance(request))
	{
		state->mode = DistanceMode_Radians;
	}
	else
	{
		state->mode = DistanceMode_Cartesian;
	}
	state->distanceFnInfo = palloc0(sizeof(FmgrInfo));
	state->minDistance = request->minDistance;
	state->maxDistance = request->maxDistance;

	if (state->mode == DistanceMode_Radians)
	{
		if (request->minDistance != NULL)
		{
			*(state->minDistance) = ConvertRadiansToMeters(*(request->minDistance));
		}
		if (request->maxDistance != NULL)
		{
			*(state->maxDistance) = ConvertRadiansToMeters(*(request->maxDistance));
		}
	}

	/* Write a simple point in the buffer */
	StringInfo wkbBuffer = makeStringInfo();
	WriteHeaderToWKBBuffer(wkbBuffer, WKBGeometryType_Point);
	WritePointToWKBBuffer(wkbBuffer, &request->referencePoint);

	bytea *wkbPointBytea = WKBBufferGetByteaWithSRID(wkbBuffer);

	if (state->mode == DistanceMode_Cartesian)
	{
		state->referencePoint = GetGeometryFromWKB(wkbPointBytea);
		fmgr_info(PostgisGeometryDistanceCentroidFunctionId(), state->distanceFnInfo);
	}
	else
	{
		state->referencePoint = GetGeographyFromWKB(wkbPointBytea);
		fmgr_info(PostgisGeographyDistanceKNNFunctionId(), state->distanceFnInfo);
	}

	DeepFreeWKB(wkbBuffer);
	wkbBuffer = NULL;

	pfree(wkbPointBytea);
	wkbPointBytea = NULL;

	pfree(request);
	request = NULL;
}


/*
 * The only difference for this function is it gets the query in {"key": {geoNearSpec}}
 * format for the distance range operator
 */
void
BuildGeoNearRangeDistanceState(GeonearDistanceState *state,
							   const pgbson *geoNearRangeQuery)
{
	pgbsonelement filterElement;
	PgbsonToSinglePgbsonElement(geoNearRangeQuery, &filterElement);
	Assert(filterElement.bsonValue.value_type == BSON_TYPE_DOCUMENT);

	const pgbson *geoNearDoc = PgbsonInitFromBuffer(
		(char *) filterElement.bsonValue.value.v_doc.data,
		filterElement.bsonValue.value.v_doc.
		data_len);
	BuildGeoNearDistanceState(state, geoNearDoc);

	/* Free the buffer, this additional buffer is only allocated once per query for building function cache */
	pfree((void *) geoNearDoc);
}


/*
 * Parses a given $geoNear aggregation stage fields and returns the parsed request.
 */
GeonearRequest *
ParseGeonearRequest(const pgbson *geoNearQuery)
{
	bson_iter_t iter;
	PgbsonInitIterator(geoNearQuery, &iter);

	GeonearRequest *request = palloc0(sizeof(GeonearRequest));
	request->distanceMultiplier = 1.0;

	while (bson_iter_next(&iter))
	{
		const char *key = bson_iter_key(&iter);
		const bson_value_t *value = bson_iter_value(&iter);
		if (strcmp(key, "key") == 0)
		{
			if (value->value_type != BSON_TYPE_UTF8)
			{
				ereport(ERROR, (
							errcode(MongoTypeMismatch),
							errmsg(
								"$geoNear parameter 'key' must be of type string but found type: %s",
								BsonTypeName(value->value_type)),
							errhint(
								"$geoNear parameter 'key' must be of type string but found type: %s",
								BsonTypeName(value->value_type))));
			}

			if (value->value.v_utf8.len == 0)
			{
				ereport(ERROR, (
							errcode(MongoBadValue),
							errmsg(
								"$geoNear parameter 'key' cannot be the empty string")));
			}

			request->key = value->value.v_utf8.str;
		}
		else if (strcmp(key, "includeLocs") == 0)
		{
			if (value->value_type != BSON_TYPE_UTF8)
			{
				ereport(ERROR, (
							errcode(MongoTypeMismatch),
							errmsg(
								"$geoNear requires that 'includeLocs' option is a String")));
			}

			request->includeLocs = value->value.v_utf8.str;
		}
		else if (strcmp(key, "distanceField") == 0)
		{
			if (value->value_type != BSON_TYPE_UTF8)
			{
				ereport(ERROR, (
							errcode(MongoTypeMismatch),
							errmsg(
								"$geoNear requires that 'distanceField' option as a String")));
			}

			request->distanceField = value->value.v_utf8.str;
		}
		else if (strcmp(key, "distanceMultiplier") == 0)
		{
			if (!BsonValueIsNumber(value))
			{
				ereport(ERROR, (
							errcode(MongoTypeMismatch),
							errmsg("distanceMultiplier must be a number")));
			}

			request->distanceMultiplier = BsonValueAsDoubleQuiet(value);

			if (request->distanceMultiplier < 0.0)
			{
				ereport(ERROR, (errcode(MongoBadValue),
								errmsg("distanceMultiplier must be nonnegative")));
			}
		}
		else if (strcmp(key, "maxDistance") == 0)
		{
			if (!BsonValueIsNumber(value))
			{
				ereport(ERROR, (
							errcode(MongoTypeMismatch),
							errmsg("maxDistance must be a number")));
			}

			request->maxDistance = palloc(sizeof(float8));
			*(request->maxDistance) = BsonValueAsDouble(value);
			if (*request->maxDistance < 0.0)
			{
				ereport(ERROR, (errcode(MongoTypeMismatch),
								errmsg("maxDistance must be nonnegative")));
			}
		}
		else if (strcmp(key, "minDistance") == 0)
		{
			if (!BsonValueIsNumber(value))
			{
				ereport(ERROR, (
							errcode(MongoTypeMismatch),
							errmsg("minDistance must be a number")));
			}
			request->minDistance = palloc(sizeof(float8));
			*(request->minDistance) = BsonValueAsDouble(value);

			if (*request->minDistance < 0.0)
			{
				ereport(ERROR, (errcode(MongoTypeMismatch),
								errmsg("minDistance must be nonnegative")));
			}
		}
		else if (strcmp(key, "near") == 0)
		{
			if (value->value_type != BSON_TYPE_DOCUMENT && value->value_type !=
				BSON_TYPE_ARRAY)
			{
				ereport(ERROR, (
							errcode(MongoTypeMismatch),
							errmsg(
								"$geoNear requires near argument to be a GeoJSON object or a legacy point(array)")));
			}

			bson_iter_t valueIter, pointsIter;
			BsonValueInitIterator(value, &valueIter);
			if (value->value_type == BSON_TYPE_ARRAY)
			{
				/* Point is defined by array as legacy point */
				request->isGeoJsonPoint = false;
				BsonValueInitIterator(value, &pointsIter);
			}
			else if (bson_iter_find(&valueIter, "coordinates"))
			{
				/* Point is defined by the coordinates value */
				request->isGeoJsonPoint = true;
				bson_iter_recurse(&valueIter, &pointsIter);
			}
			else
			{
				ereport(ERROR, (errcode(MongoBadValue),
								errmsg("invalid argument in geo near query")));
			}

			int8 index = 0;
			char *lastkey = NULL;
			while (index < 2 && bson_iter_next(&pointsIter))
			{
				const bson_value_t *pointValue = bson_iter_value(&pointsIter);
				lastkey = (char *) bson_iter_key(&pointsIter);
				if (!BsonValueIsNumber(pointValue))
				{
					ereport(ERROR, (
								errcode(MongoBadValue),
								errmsg("invalid argument in geo near query: %s",
									   (request->isGeoJsonPoint ? "coordinates" :
										lastkey)),
								errhint("invalid argument in geo near query: %s",
										(request->isGeoJsonPoint ? "coordinates" :
										 lastkey))));
				}
				if (index == 0)
				{
					request->referencePoint.x = BsonValueAsDoubleQuiet(pointValue);
				}
				else
				{
					request->referencePoint.y = BsonValueAsDoubleQuiet(pointValue);
				}
				index++;
			}

			if (index == 0)
			{
				ereport(ERROR, (errcode(MongoTypeMismatch),
								errmsg("$geometry is required for geo near query")));
			}

			if (index == 1)
			{
				ereport(ERROR, (
							errcode(MongoTypeMismatch),
							errmsg("invalid argument in geo near query: %s",
								   (request->isGeoJsonPoint ? "coordinates" : lastkey)),
							errhint("invalid argument in geo near query: %s",
									(request->isGeoJsonPoint ? "coordinates" :
									 lastkey))));
			}
		}
		else if (strcmp(key, "query") == 0)
		{
			request->query = *value;
		}
		else if (strcmp(key, "spherical") == 0)
		{
			request->spherical = BsonValueAsBool(value);
		}
	}

	if (request->distanceField == NULL)
	{
		ereport(ERROR, (
					errcode(MongoTypeMismatch),
					errmsg("$geoNear requires a 'distanceField' option as a String")));
	}

	if (request->key == NULL)
	{
		/* MongoDB supports optional key as far as there is one 2d and/or one 2dsphere index available and it chooses
		 * the index by first looking for a `2d` and then a `2dsphere` index. This is bad here for 2 reasons & is a required field:
		 *
		 *      1) Imagine a scenario where the user adds a new geospatial index and all the previous $geoNear queries start
		 *      failing because now system can't guess the index to use.
		 *
		 *      2) We also don't want to open the cached relation entries of the table at the time of making the query AST for
		 *      $geoNear queries which is not the right thing to do just to guess the index to use
		 */
		ereport(ERROR, (
					errcode(MongoTypeMismatch),
					errmsg("$geoNear requires a 'key' option as a String")));
	}

	if (request->spherical && (request->referencePoint.x < -180.0 ||
							   request->referencePoint.x > 180 ||
							   request->referencePoint.y < -90.0 ||
							   request->referencePoint.y > 90))
	{
		if (request->isGeoJsonPoint)
		{
			ereport(ERROR, (
						errcode(MongoBadValue),
						errmsg("invalid argument in geo near query: coordinates")));
		}
		else
		{
			ereport(ERROR, (errcode(MongoBadValue),
							errmsg("Legacy point is out of bounds for spherical query")));
		}
	}

	return request;
}


/*
 * Walks the query tree quals to check if there are operators restricted
 * for use with $geoNear.
 */
bool
ValidateQueryOperatorsForGeoNear(Node *node, void *state)
{
	CHECK_FOR_INTERRUPTS();

	if (node == NULL)
	{
		return false;
	}
	Oid operatorFunctionOid = InvalidOid;
	if (IsA(node, OpExpr))
	{
		operatorFunctionOid = ((OpExpr *) node)->opfuncid;
	}

	if (IsA(node, FuncExpr))
	{
		operatorFunctionOid = ((FuncExpr *) node)->funcid;
	}

	if (operatorFunctionOid == BsonTextFunctionId())
	{
		ereport(ERROR, (
					errcode(MongoBadValue),
					errmsg("text and geoNear not allowed in same query")));
	}

	return expression_tree_walker(node, ValidateQueryOperatorsForGeoNear, state);
}
