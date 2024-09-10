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
#include <catalog/pg_operator.h>
#include <parser/parse_clause.h>
#include <parser/parse_node.h>

#include "geospatial/bson_geospatial_common.h"
#include "geospatial/bson_geospatial_geonear.h"
#include "aggregation/bson_aggregation_pipeline.h"
#include "query/query_operator.h"
#include "utils/fmgr_utils.h"
#include "utils/version_utils.h"
#include "aggregation/bson_aggregation_pipeline_private.h"

static bool GeonearDistanceWithinRange(const GeonearDistanceState *state,
									   const pgbson *document);

static double GetDoubleValueForDistance(const bson_value_t *value, const char *opName);

static void ValidateGeoNearWithIndexBounds(const Bson2dGeometryPathOptions *options,
										   Point referencePoint);

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
		queryBson,
		NULL);

	if (state == NULL)
	{
		GeonearDistanceState projectionState;
		memset(&projectionState, 0, sizeof(GeonearDistanceState));
		BuildGeoNearDistanceState(&projectionState, queryBson, NULL);
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

	if (points == (Datum) 0)
	{
		/*
		 * Because we support geonear in the runtime today and doesn't impose strict index usage.
		 * There can be documents which can pass the validate step and end up here
		 * e.g. empty document {a: {b: {}}}
		 * and would have empty geo values. Don't allow this
		 *
		 * TODO: This would be fixed if we restrict geonear queries only with indexes
		 */
		StringInfo objectIdString = makeStringInfo();
		bson_iter_t iterator;
		if (PgbsonInitIteratorAtPath(document, "_id", &iterator))
		{
			appendStringInfo(objectIdString, "{ _id: %s } ",
							 BsonValueToJsonForLogging(bson_iter_value(&iterator)));
		}
		ereport(ERROR, (
					errcode(ERRCODE_HELIO_CONVERSIONFAILURE),
					errmsg(
						"geoNear fails to convert values at path '%s' to valid points. %s",
						state->key.string, objectIdString->len > 0 ?
						objectIdString->data : ""),
					errdetail_log(
						"geoNear fails to extract valid points from document")));
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
		!DOUBLE_EQUALS(distance, *(state->maxDistance)))
	{
		return false;
	}

	if (state->minDistance != NULL && distance < *(state->minDistance) &&
		!DOUBLE_EQUALS(distance, *(state->minDistance)))
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
						  const pgbson *geoNearQuery,
						  const GeonearIndexValidationState *indexValidationState)
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

	/* validate query document to check if reference point is within index bounds */
	if (indexValidationState != NULL)
	{
		if (indexValidationState->validationLevel == GeospatialValidationLevel_Index &&
			indexValidationState->options->type == IndexOptionsType_2d)
		{
			const Bson2dGeometryPathOptions *options2d =
				(const Bson2dGeometryPathOptions *) (indexValidationState->options);
			ValidateGeoNearWithIndexBounds(options2d, request->referencePoint);
		}
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
	BuildGeoNearDistanceState(state, geoNearDoc, NULL);

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
							errcode(ERRCODE_HELIO_TYPEMISMATCH),
							errmsg(
								"$geoNear parameter 'key' must be of type string but found type: %s",
								BsonTypeName(value->value_type)),
							errdetail_log(
								"$geoNear parameter 'key' must be of type string but found type: %s",
								BsonTypeName(value->value_type))));
			}

			if (value->value.v_utf8.len == 0)
			{
				ereport(ERROR, (
							errcode(ERRCODE_HELIO_BADVALUE),
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
							errcode(ERRCODE_HELIO_TYPEMISMATCH),
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
							errcode(ERRCODE_HELIO_TYPEMISMATCH),
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
							errcode(ERRCODE_HELIO_TYPEMISMATCH),
							errmsg("distanceMultiplier must be a number")));
			}

			request->distanceMultiplier = BsonValueAsDoubleQuiet(value);

			if (request->distanceMultiplier < 0.0)
			{
				ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE),
								errmsg("distanceMultiplier must be nonnegative")));
			}
		}
		else if (strcmp(key, "maxDistance") == 0)
		{
			if (!request->maxDistance)
			{
				request->maxDistance = palloc(sizeof(float8));
			}

			*(request->maxDistance) = GetDoubleValueForDistance(value, key);
		}
		else if (strcmp(key, "minDistance") == 0)
		{
			request->minDistance = palloc(sizeof(float8));
			*(request->minDistance) = GetDoubleValueForDistance(value, key);
		}
		else if (strcmp(key, "near") == 0)
		{
			if (value->value_type != BSON_TYPE_DOCUMENT && value->value_type !=
				BSON_TYPE_ARRAY)
			{
				ereport(ERROR, (
							errcode(ERRCODE_HELIO_TYPEMISMATCH),
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
			else
			{
				if (bson_iter_find(&valueIter, "coordinates") &&
					bson_iter_value(&valueIter)->value_type == BSON_TYPE_ARRAY)
				{
					request->isGeoJsonPoint = true;
					bson_iter_recurse(&valueIter, &pointsIter);
				}
				else
				{
					BsonValueInitIterator(value, &pointsIter);
					request->isGeoJsonPoint = false;
					request->spherical = false;
				}
			}

			int8 index = 0;
			char *lastkey = NULL;
			while (index < 3 && bson_iter_next(&pointsIter))
			{
				const bson_value_t *pointValue = bson_iter_value(&pointsIter);
				lastkey = (char *) bson_iter_key(&pointsIter);
				if (!BsonValueIsNumber(pointValue))
				{
					ereport(ERROR, (
								errcode(ERRCODE_HELIO_BADVALUE),
								errmsg("invalid argument in geo near query: %s",
									   (request->isGeoJsonPoint ? "coordinates" :
										lastkey)),
								errdetail_log("invalid argument in geo near query: %s",
											  (request->isGeoJsonPoint ? "coordinates" :
											   lastkey))));
				}

				if (index == 0)
				{
					request->referencePoint.x = BsonValueAsDoubleQuiet(pointValue);
				}
				else if (index == 1)
				{
					request->referencePoint.y = BsonValueAsDoubleQuiet(pointValue);
				}
				else if (request->maxDistance == NULL)
				{
					request->maxDistance = palloc(sizeof(float8));
					*(request->maxDistance) = GetDoubleValueForDistance(pointValue,
																		"maxDistance");
				}

				index++;
			}

			if (index == 0)
			{
				ereport(ERROR, (errcode(ERRCODE_HELIO_TYPEMISMATCH),
								errmsg("$geometry is required for geo near query")));
			}

			if (index == 1)
			{
				ereport(ERROR, (
							errcode(ERRCODE_HELIO_TYPEMISMATCH),
							errmsg("invalid argument in geo near query: %s",
								   (request->isGeoJsonPoint ? "coordinates" : lastkey)),
							errdetail_log("invalid argument in geo near query: %s",
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
					errcode(ERRCODE_HELIO_TYPEMISMATCH),
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
					errcode(ERRCODE_HELIO_TYPEMISMATCH),
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
						errcode(ERRCODE_HELIO_BADVALUE),
						errmsg("invalid argument in geo near query: coordinates")));
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE),
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
					errcode(ERRCODE_HELIO_BADVALUE),
					errmsg("text and geoNear not allowed in same query")));
	}

	return expression_tree_walker(node, ValidateQueryOperatorsForGeoNear, state);
}


/*
 * Parse query doc for $near and $nearSphere to generate queryDoc for geoNear. All cases listed below -
 * 1. {geo: { $near: [1, 1], $maxDistance: 1 }} - { key: "geo", near: [1, 1], distanceField: "dist", maxDistance: 1 } - Will use 2d index
 * 2. {geo: { $near: { x: 1, y: 1 }, $maxDistance: 1}} - { key: "geo", near: { x: 1, y: 1 }, distanceField: "dist", maxDistance: 1 } - Will use 2d index
 * 3. {geo: { $near: {type: "Point", coordinates: [1, 1] }, $maxDistance: 1}} - { key: "geo", near: {type: "Point", coordinates: [1, 1]}, maxDistance: 1, distanceField: "dist", spherical: true } - Will use 2dsphere index
 * 4. {geo: { $near: {{$geometry: {type: "Point", coordinates: [1, 1] }, $maxDistance: 1}}} - { key: "geo", near: {type: "Point", coordinates: [1, 1]}, maxDistance: 1, distanceField: "dist", spherical: true } - Will use 2dsphere index
 * $nearSphere queries also get converted to similar geoNear specs with spherical set to true in all cases.
 */
pgbson *
GetGeonearSpecFromNearQuery(bson_iter_t *operatorDocIterator, const char *path,
							const char *mongoOperatorName)
{
	const bson_value_t *value = bson_iter_value(operatorDocIterator);

	bson_iter_t valueIter;
	BsonValueInitIterator(value, &valueIter);
	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	bson_value_t fieldValue;

	/* Fill in key */
	fieldValue.value_type = BSON_TYPE_UTF8;
	fieldValue.value.v_utf8.str = (char *) path;
	fieldValue.value.v_utf8.len = strlen(path);
	PgbsonWriterAppendValue(&writer, "key", 3, &fieldValue);

	bool isGeoJsonPoint = false;
	const char *op;

	if (value->value_type == BSON_TYPE_ARRAY)
	{
		if (!bson_iter_next(&valueIter))
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE),
							errmsg("$geometry is required for geo near query")));
		}

		/* Write the array as is it to be parsed later, and extract min and max distance from iterator. */
		PgbsonWriterAppendValue(&writer, "near", 4, value);

		while (bson_iter_next(operatorDocIterator))
		{
			op = bson_iter_key(operatorDocIterator);

			if (strcmp(op, "$minDistance") == 0)
			{
				PgbsonWriterAppendValue(&writer, "minDistance", 11,
										bson_iter_value(operatorDocIterator));
			}
			else if (strcmp(op, "$maxDistance") == 0)
			{
				PgbsonWriterAppendValue(&writer, "maxDistance", 11,
										bson_iter_value(operatorDocIterator));
			}
			else
			{
				ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE),
								errmsg("invalid argument in geo near query: %s", op)));
			}
		}
	}
	else if (value->value_type == BSON_TYPE_DOCUMENT)
	{
		if (bson_iter_next(&valueIter))
		{
			op = bson_iter_key(&valueIter);
			const bson_value_t *iterValue = bson_iter_value(&valueIter);

			if (iterValue->value_type == BSON_TYPE_DOCUMENT)
			{
				/* Query looks like {$near: {$geometry: {coordinates: [0, 0]}}} */
				bson_iter_t geoJsonIter;
				bson_iter_recurse(&valueIter, &geoJsonIter);

				if (!bson_iter_find(&geoJsonIter, "coordinates"))
				{
					ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE),
									errmsg("$near requires geojson point, given %s",
										   BsonValueToJsonForLogging(iterValue)),
									errdetail_log(
										"$near requires geojson point, given %s",
										BsonValueToJsonForLogging(iterValue))));
				}

				PgbsonWriterAppendValue(&writer, "near", 4, iterValue);
				isGeoJsonPoint = true;

				while (bson_iter_next(&valueIter))
				{
					op = bson_iter_key(&valueIter);

					if (strcmp(op, "$minDistance") == 0)
					{
						const bson_value_t *distValue = bson_iter_value(&valueIter);

						if (IsBsonValueInfinity(distValue))
						{
							ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE),
											errmsg("minDistance must be non-negative")));
						}

						PgbsonWriterAppendValue(&writer, "minDistance", 11, distValue);
					}
					else if (strcmp(op, "$maxDistance") == 0)
					{
						const bson_value_t *distValue = bson_iter_value(&valueIter);

						if (IsBsonValueInfinity(distValue))
						{
							ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE),
											errmsg("maxDistance must be non-negative")));
						}

						PgbsonWriterAppendValue(&writer, "maxDistance", 11, distValue);
					}
					else
					{
						ereport(ERROR,
								(errcode(ERRCODE_HELIO_BADVALUE),
								 errmsg("invalid argument in geo near query: %s", op),
								 errdetail_log("invalid argument in geo near query: %s",
											   op)));
					}
				}
			}
			else if (BsonValueIsNumber(iterValue))
			{
				/* Query looks like {$near: {x: 1, y: 1}} */
				if (!bson_iter_next(&valueIter))
				{
					ereport(ERROR,
							(errcode(ERRCODE_HELIO_BADVALUE),
							 errmsg("invalid argument in geo near query: %s", op),
							 errdetail_log("invalid argument in geo near query: %s",
										   op)));
				}

				if (!BsonValueIsNumber(bson_iter_value(&valueIter)))
				{
					ereport(ERROR,
							(errcode(ERRCODE_HELIO_BADVALUE),
							 errmsg("invalid argument in geo near query: %s", op),
							 errdetail_log("invalid argument in geo near query: %s",
										   op)));
				}

				PgbsonWriterAppendValue(&writer, "near", 4, value);
			}
			else
			{
				/* Query looks like {$near: {coordinates: [0, 0]}} */
				if (strcmp(op, "coordinates") != 0 && !bson_iter_find(&valueIter,
																	  "coordinates"))
				{
					ereport(ERROR,
							(errcode(ERRCODE_HELIO_BADVALUE),
							 errmsg("invalid argument in geo near query: %s", op),
							 errdetail_log("invalid argument in geo near query: %s",
										   op)));
				}

				isGeoJsonPoint = true;
				PgbsonWriterAppendValue(&writer, "near", 4, value);
			}
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE),
							errmsg("$geometry is required for geo near query")));
		}
	}
	else
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE),
						errmsg("near must be first in: { %s: %s }", mongoOperatorName,
							   BsonValueToJsonForLogging(value)),
						errdetail_log("near must be first in: { %s: %s }",
									  mongoOperatorName,
									  BsonValueToJsonForLogging(value))));
	}

	while (bson_iter_next(operatorDocIterator))
	{
		op = bson_iter_key(operatorDocIterator);

		if (strcmp(op, "$minDistance") == 0)
		{
			const bson_value_t *distValue = bson_iter_value(operatorDocIterator);
			PgbsonWriterAppendValue(&writer, "minDistance", 11, distValue);
		}
		else if (strcmp(op, "$maxDistance") == 0)
		{
			const bson_value_t *distValue = bson_iter_value(operatorDocIterator);
			PgbsonWriterAppendValue(&writer, "maxDistance", 11, distValue);
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE),
							errmsg("invalid argument in geo near query: %s", op),
							errdetail_log("invalid argument in geo near query: %s", op)));
		}
	}

	fieldValue.value.v_utf8.str = "dist";
	fieldValue.value.v_utf8.len = 4;
	PgbsonWriterAppendValue(&writer, "distanceField", 13, &fieldValue);

	if (isGeoJsonPoint ||
		(strcmp(mongoOperatorName, "$nearSphere") == 0) ||
		(strcmp(mongoOperatorName, "$geoNear") == 0))
	{
		fieldValue.value_type = BSON_TYPE_BOOL;
		fieldValue.value.v_bool = true;
		PgbsonWriterAppendValue(&writer, "spherical", 9, &fieldValue);
	}

	return PgbsonWriterGetPgbson(&writer);
}


/*
 * Common function to create quals and append sort clause for $geoNear aggregation stage
 * and $near & $nearSphere query operators.
 *
 * Out variables
 * targetEntry: The target entry for the distance field in the target list
 * sortClause: The sort clause for the distance field
 *
 * Note:
 * since we don't know overall targetlist structure of query, `resno`, `ressortgroupreg`
 * of targetEntry and sortclause should be updated by caller, these are defaulted to 0 in
 * this function.
 */
List *
CreateExprForGeonearAndNearSphere(const pgbson *queryDoc, Expr *docExpr,
								  const GeonearRequest *request,
								  TargetEntry **targetEntry,
								  SortGroupClause **sortClause)
{
	List *quals = NIL;
	Const *keyConst = MakeTextConst(request->key, strlen(request->key));

	Const *queryConst = MakeBsonConst((pgbson *) queryDoc);

	/* GeoJSON point enforces 2dsphere index and legacy enforces 2d index usage */
	Oid bsonValidateFunctionId = request->isGeoJsonPoint ?
								 BsonValidateGeographyFunctionId() :
								 BsonValidateGeometryFunctionId();

	Expr *validateExpr = (Expr *) makeFuncExpr(bsonValidateFunctionId,
											   BsonTypeId(),
											   list_make2(docExpr,
														  keyConst),
											   InvalidOid,
											   InvalidOid,
											   COERCE_EXPLICIT_CALL);

	/* Add the geo index pfe to match to the index */
	NullTest *nullTest = makeNode(NullTest);
	nullTest->argisrow = false;
	nullTest->nulltesttype = IS_NOT_NULL;
	nullTest->arg = validateExpr;
	quals = lappend(quals, nullTest);

	/* Add $minDistance and $maxDistance checks as range distance operator */
	if (request->minDistance != NULL || request->maxDistance != NULL)
	{
		/*
		 * make the range operator of this form {<key>: <geoNearSpec>}
		 * here key is usually the path for index. We do this so that index
		 * support correctly matches the geo index for this operator.
		 */
		pgbson_writer writer;
		PgbsonWriterInit(&writer);
		PgbsonWriterAppendDocument(&writer, request->key, strlen(request->key),
								   queryDoc);
		pgbson *rangeQueryDoc = PgbsonWriterGetPgbson(&writer);
		Const *rangeQueryDocConst = MakeBsonConst(rangeQueryDoc);
		Expr *distanceRangeOpExpr = make_opclause(BsonGeonearDistanceRangeOperatorId(),
												  BOOLOID, false,
												  (Expr *) validateExpr,
												  (Expr *) rangeQueryDocConst,
												  InvalidOid, InvalidOid);
		quals = lappend(quals, distanceRangeOpExpr);
	}

	/* Add the sort clause and also add the expression in targetlist */
	Expr *opExpr = make_opclause(BsonGeonearDistanceOperatorId(), FLOAT8OID, false,
								 (Expr *) validateExpr,
								 (Expr *) queryConst,
								 InvalidOid, InvalidOid);

	/* Update the resno later */
	TargetEntry *tle = makeTargetEntry(opExpr, 0, "distance", true);
	*targetEntry = tle;

	SortGroupClause *sortGroupClause = makeNode(SortGroupClause);
	sortGroupClause->eqop = Float8LessOperator;
	sortGroupClause->sortop = Float8LessOperator;
	sortGroupClause->tleSortGroupRef = 0; /* Update tleSortGroupRef later in caller */
	*sortClause = sortGroupClause;

	return quals;
}


/* Check if the value of min/max distance is a valid number. */
static double
GetDoubleValueForDistance(const bson_value_t *value, const char *opName)
{
	if (!BsonValueIsNumber(value))
	{
		ereport(ERROR, (
					errcode(ERRCODE_HELIO_TYPEMISMATCH),
					errmsg("%s must be a number", opName),
					errdetail_log("%s must be a number", opName)));
	}
	else if (isnan(value->value.v_double))
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE),
						errmsg("%s must be non-negative", opName),
						errdetail_log("%s must be non-negative", opName)));
	}

	double distValue = BsonValueAsDouble(value);

	if (distValue < 0.0)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE),
						errmsg("%s must be nonnegative", opName),
						errdetail_log("%s must be nonnegative", opName)));
	}

	return distValue;
}


/* Validate reference point to be within index bounds */
static void
ValidateGeoNearWithIndexBounds(const Bson2dGeometryPathOptions *options,
							   Point referencePoint)
{
	if (referencePoint.x < options->minBound || referencePoint.x > options->maxBound ||
		referencePoint.y < options->minBound || referencePoint.y > options->maxBound)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_LOCATION16433),
						errmsg("point not in interval of [ %g, %g ]",
							   options->minBound, options->maxBound)));
	}
}
