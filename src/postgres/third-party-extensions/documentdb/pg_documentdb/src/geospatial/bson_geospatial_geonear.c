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
#include <access/reloptions.h>
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
static pgbson * AddKeyAndGetQueryBson(const pgbson *queryDoc, const char *key);
static void UpdateGeonearArgsInPlace(List *args, Oid validateFunctionOid,
									 Datum keyDatum, Datum geoNearQueryDatum);
static bool CheckBsonProjectGeonearFunctionExpr(FuncExpr *expr,
												FuncExpr **geoNearProjectFuncExpr);
static bool ValidateConstExpression(const bson_value_t *value,
									AggregationExpressionData *exprData,
									ParseAggregationExpressionContext *context);

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
					errcode(ERRCODE_DOCUMENTDB_CONVERSIONFAILURE),
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
							errcode(ERRCODE_DOCUMENTDB_TYPEMISMATCH),
							errmsg(
								"Expected 'string' type for $geoNear 'key' but found '%s' type",
								BsonTypeName(value->value_type)),
							errdetail_log(
								"Expected 'string' type for $geoNear 'key' but found '%s' type",
								BsonTypeName(value->value_type))));
			}

			if (value->value.v_utf8.len == 0)
			{
				ereport(ERROR, (
							errcode(ERRCODE_DOCUMENTDB_BADVALUE),
							errmsg(
								"'key' parameter for $geoNear is empty")));
			}

			request->key = value->value.v_utf8.str;
		}
		else if (strcmp(key, "includeLocs") == 0)
		{
			if (value->value_type != BSON_TYPE_UTF8)
			{
				ereport(ERROR, (
							errcode(ERRCODE_DOCUMENTDB_TYPEMISMATCH),
							errmsg(
								"Expected 'string' type for $geoNear 'includeLocs' but found '%s' type",
								BsonTypeName(value->value_type))));
			}

			request->includeLocs = value->value.v_utf8.str;
		}
		else if (strcmp(key, "distanceField") == 0)
		{
			if (value->value_type != BSON_TYPE_UTF8)
			{
				ereport(ERROR, (
							errcode(ERRCODE_DOCUMENTDB_TYPEMISMATCH),
							errmsg(
								"Expected 'string' type for $geoNear 'distanceField'.")));
			}

			request->distanceField = value->value.v_utf8.str;
		}
		else if (strcmp(key, "distanceMultiplier") == 0)
		{
			if (!BsonValueIsNumber(value))
			{
				ereport(ERROR, (
							errcode(ERRCODE_DOCUMENTDB_TYPEMISMATCH),
							errmsg(
								"The value of distanceMultiplier must be a numeric type")));
			}

			request->distanceMultiplier = BsonValueAsDoubleQuiet(value);

			if (request->distanceMultiplier < 0.0)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg("distanceMultiplier must not be negative")));
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
							errcode(ERRCODE_DOCUMENTDB_TYPEMISMATCH),
							errmsg(
								"Expected 'document' or 'array' type for $geoNear 'near' field but found '%s' type",
								BsonTypeName(value->value_type))));
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
								errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg("Invalid parameter detected in geo near query: %s",
									   (request->isGeoJsonPoint ? "coordinates" :
										lastkey)),
								errdetail_log(
									"Invalid parameter detected in geo near query: %s",
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
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_TYPEMISMATCH),
								errmsg(
									"A geo near query requires the $geometry to be specified.")));
			}

			if (index == 1)
			{
				ereport(ERROR, (
							errcode(ERRCODE_DOCUMENTDB_TYPEMISMATCH),
							errmsg("Invalid parameter detected in geo near query: %s",
								   (request->isGeoJsonPoint ? "coordinates" : lastkey)),
							errdetail_log(
								"Invalid parameter detected in geo near query: %s",
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
					errcode(ERRCODE_DOCUMENTDB_TYPEMISMATCH),
					errmsg(
						"$geoNear needs a 'distanceField' option specified as a String value")));
	}

	if (request->spherical && (request->referencePoint.x < -180.0 ||
							   request->referencePoint.x > 180 ||
							   request->referencePoint.y < -90.0 ||
							   request->referencePoint.y > 90))
	{
		if (request->isGeoJsonPoint)
		{
			ereport(ERROR, (
						errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg("Invalid parameter found in geo-near query coordinates")));
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
							errmsg(
								"The legacy point lies outside the valid range for a spherical query.")));
		}
	}

	if (request->key == NULL)
	{
		/* Set key empty for internal use */
		request->key = "";
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
					errcode(ERRCODE_DOCUMENTDB_BADVALUE),
					errmsg(
						"Using text and geoNear together in a single query is not permitted")));
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
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
							errmsg(
								"A geo near query requires the $geometry to be specified.")));
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
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg("Invalid parameter detected in geo near query: %s",
									   op)));
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
				/* Query appears similar to {$near: {$geometry: {coordinates: [0, 0]}}} */
				bson_iter_t geoJsonIter;
				bson_iter_recurse(&valueIter, &geoJsonIter);

				if (!bson_iter_find(&geoJsonIter, "coordinates"))
				{
					ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
									errmsg(
										"The parameter $near expects a GeoJSON point, but received %s instead",
										BsonValueToJsonForLogging(iterValue)),
									errdetail_log(
										"The parameter $near expects a GeoJSON point, but received %s instead",
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
							ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
											errmsg(
												"The value of minDistance must always be zero or greater")));
						}

						PgbsonWriterAppendValue(&writer, "minDistance", 11, distValue);
					}
					else if (strcmp(op, "$maxDistance") == 0)
					{
						const bson_value_t *distValue = bson_iter_value(&valueIter);

						if (IsBsonValueInfinity(distValue))
						{
							ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
											errmsg(
												"maxDistance value must be zero or greater")));
						}

						PgbsonWriterAppendValue(&writer, "maxDistance", 11, distValue);
					}
					else
					{
						ereport(ERROR,
								(errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								 errmsg(
									 "Invalid parameter detected in geo near query: %s",
									 op),
								 errdetail_log(
									 "Invalid parameter detected in geo near query: %s",
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
							(errcode(ERRCODE_DOCUMENTDB_BADVALUE),
							 errmsg("Invalid parameter detected in geo near query: %s",
									op),
							 errdetail_log(
								 "Invalid parameter detected in geo near query: %s",
								 op)));
				}

				if (!BsonValueIsNumber(bson_iter_value(&valueIter)))
				{
					ereport(ERROR,
							(errcode(ERRCODE_DOCUMENTDB_BADVALUE),
							 errmsg("Invalid parameter detected in geo near query: %s",
									op),
							 errdetail_log(
								 "Invalid parameter detected in geo near query: %s",
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
							(errcode(ERRCODE_DOCUMENTDB_BADVALUE),
							 errmsg("Invalid parameter detected in geo near query: %s",
									op),
							 errdetail_log(
								 "Invalid parameter detected in geo near query: %s",
								 op)));
				}

				isGeoJsonPoint = true;
				PgbsonWriterAppendValue(&writer, "near", 4, value);
			}
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
							errmsg(
								"A geo near query requires the $geometry to be specified.")));
		}
	}
	else
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
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
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
							errmsg("Invalid parameter detected in geo near query: %s",
								   op),
							errdetail_log(
								"Invalid parameter detected in geo near query: %s", op)));
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
											   DEFAULT_COLLATION_OID,
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

	/* Postpone updating the resno value */
	TargetEntry *tle = makeTargetEntry(opExpr, 0, "distance", true);
	*targetEntry = tle;

	SortGroupClause *sortGroupClause = makeNode(SortGroupClause);
	sortGroupClause->eqop = Float8LessOperator;
	sortGroupClause->sortop = Float8LessOperator;
	sortGroupClause->tleSortGroupRef = 0; /* Update tleSortGroupRef later in caller */
	*sortClause = sortGroupClause;

	return quals;
}


/*
 * There are 2 cases where we can push geonear queries to an alternate geo index.
 * CanGeonearQueryUseAlternateIndex identifies these 2 scenarios.
 *
 * 1. If no `key` is provided in the geonear spec, then by default there is no index path created
 *    but we can choose either a 2d or 2dsphere index based on availability if these are the only
 *    2 geospatial indexes available.
 * 2. If a geonear legacy spherical query is provided then it can also use a 2dsphere index when no
 *    suitable 2d index is available.
 *    e.g.
 *    {
 *        "$geoNear": { near: [0, 0], spherical: true, key: 'key' ...}
 *    }
 *    This kind of geonear query must use 2d index if one is availbale on `key` otherwise a 2dsphere
 *    index can also be used on the same `key`.
 *
 * Returns true when any of the above scenario is true, and sets the GeonearRequest
 */
bool
CanGeonearQueryUseAlternateIndex(OpExpr *geoNearOpExpr,
								 GeonearRequest **request)
{
	Node *geoNearQuerySpec = lsecond(geoNearOpExpr->args);
	if (!IsA(geoNearQuerySpec, Const))
	{
		return false;
	}
	Const *geoNearConst = (Const *) geoNearQuerySpec;
	pgbson *geoNearDoc = DatumGetPgBson(geoNearConst->constvalue);
	GeonearRequest *req = ParseGeonearRequest(geoNearDoc);
	if (strlen(req->key) == 0 || Is2dWithSphericalDistance(req))
	{
		*request = req;
		return true;
	}

	return false;
}


/*
 * Try to find the geonear expression in the query. If the query is non-sharded
 * it can be obtained from sortclauses but if it is sharded then ORDER BY is not
 * pushed to the shards so we need to get it from the targetlist.
 */
bool
TryFindGeoNearOpExpr(PlannerInfo *root, ReplaceExtensionFunctionContext *context)
{
	List *sorClause = root->parse->sortClause;
	if (sorClause != NIL)
	{
		ListCell *sortClauseCell;
		foreach(sortClauseCell, sorClause)
		{
			SortGroupClause *sortClause = lfirst_node(SortGroupClause,
													  sortClauseCell);
			Expr *tleExpr = (Expr *) get_sortgroupclause_expr(sortClause,
															  root->processed_tlist);

			if (IsA(tleExpr, OpExpr))
			{
				OpExpr *opExpr = (OpExpr *) tleExpr;
				if (opExpr->opno == BsonGeonearDistanceOperatorId())
				{
					context->forceIndexQueryOpData.opExtraState = (void *) opExpr;
					return true;
				}
			}
		}
	}
	else
	{
		/* Maybe a Sharded case try to get expr from targetlist */
		ListCell *targetCell;
		foreach(targetCell, root->processed_tlist)
		{
			TargetEntry *tle = (TargetEntry *) lfirst(targetCell);
			Expr *tleExpr = tle->expr;
			if (IsA(tleExpr, OpExpr))
			{
				OpExpr *opExpr = (OpExpr *) tleExpr;
				if (opExpr->opno == BsonGeonearDistanceOperatorId())
				{
					context->forceIndexQueryOpData.opExtraState = (void *) opExpr;
					return true;
				}
			}
		}
	}
	return false;
}


/*
 * Replaces any "valid" variable reference in geonear spec with the value from let document,
 * and evaluates the expressions provided in the geonear spec.
 */
pgbson *
EvaluateGeoNearConstExpression(const bson_value_t *geoNearSpecValue, Expr *variableExpr)
{
	ParseAggregationExpressionContext parseContext = { 0 };

	ExpressionVariableContext *variableContext = palloc0(
		sizeof(ExpressionVariableContext));
	if (variableExpr != NULL && IsA(variableExpr, Const))
	{
		/*
		 * TODO: Support non-Const variable expression for geonear.
		 * e.g. $lookup nested let
		 */
		Const *varConst = (Const *) variableExpr;
		pgbson *variableSpecPgbson = DatumGetPgBson(varConst->constvalue);

		bson_iter_t letVarsIter;
		if (PgbsonInitIteratorAtPath(variableSpecPgbson, "let", &letVarsIter))
		{
			const bson_value_t *letVars = bson_iter_value(&letVarsIter);
			ParseVariableSpec(letVars, variableContext, &parseContext);
		}
	}

	AggregationExpressionData *exprData = (AggregationExpressionData *) palloc0(
		sizeof(AggregationExpressionData));

	bson_iter_t iter;
	BsonValueInitIterator(geoNearSpecValue, &iter);
	pgbson *source = PgbsonInitEmpty();

	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	bool nullOnEmpty = false;
	while (bson_iter_next(&iter))
	{
		StringView key = bson_iter_key_string_view(&iter);
		const bson_value_t *value = bson_iter_value(&iter);
		if (StringViewEqualsCString(&key, "near") ||
			StringViewEqualsCString(&key, "minDistance") ||
			StringViewEqualsCString(&key, "maxDistance"))
		{
			if (ValidateConstExpression(value, exprData, &parseContext))
			{
				EvaluateAggregationExpressionDataToWriter(exprData, source, key, &writer,
														  variableContext, nullOnEmpty);
			}
			else
			{
				/* Value is not constant, compilation error */
				if (StringViewEqualsCString(&key, "near"))
				{
					ereport(ERROR, (
								errcode(ERRCODE_DOCUMENTDB_LOCATION5860402),
								errmsg(
									"The $geoNear operator must be provided with a fixed and constant near argument")));
				}
				else if (StringViewEqualsCString(&key, "minDistance"))
				{
					ereport(ERROR, (
								errcode(ERRCODE_DOCUMENTDB_LOCATION7555701),
								errmsg(
									"The $geoNear needs the $minDistance to resolve to a fixed numerical value")));
				}
				else
				{
					ereport(ERROR, (
								errcode(ERRCODE_DOCUMENTDB_LOCATION7555702),
								errmsg(
									"The $geoNear operator needs the $maxDistance operator to resolve to a constant numeric value.")));
				}
			}
		}
		else
		{
			PgbsonWriterAppendValue(&writer, key.string, key.length, value);
		}
	}

	pfree(exprData);
	pfree(variableContext);
	return PgbsonWriterGetPgbson(&writer);
}


/*
 * Updates the geonear related quals, projection and sort clause in the query,
 * based on the index we want to use.
 *
 * `geonearOpExpr` is the original geonear operator expression in query
 * `key`: The key for the geonear spec to be used, this can be different from original
 *        in case of empty key and we want to force push to an index.
 * `useSpherical`: This is true in case geonear legacy query needs to be pushed to 2dsphere index.
 * `isEmptyKey`: represents if key is empty in the original geonear query.
 */
void
UpdateGeoNearQueryTreeToUseAlternateIndex(PlannerInfo *root, RelOptInfo *rel,
										  OpExpr *geoNearOpExpr, const char *key,
										  bool useSphericalIndex, bool isEmptyKey)
{
	FuncExpr *geoNearFuncExpr = (FuncExpr *) linitial(geoNearOpExpr->args);
	Const *keyConst = lsecond(geoNearFuncExpr->args);
	Datum keyDatum = keyConst->constvalue;

	Const *querySpecConst = (Const *) lsecond(geoNearOpExpr->args);
	Datum querySpecDatum = querySpecConst->constvalue;
	if (isEmptyKey)
	{
		pgbson *queryDoc = DatumGetPgBson(querySpecConst->constvalue);
		pgbson *queryDocWithKey = AddKeyAndGetQueryBson(queryDoc, key);

		keyDatum = CStringGetTextDatum(key);
		querySpecDatum = PointerGetDatum(queryDocWithKey);
	}

	Oid validateFunctionOid = geoNearFuncExpr->funcid;
	if (useSphericalIndex)
	{
		validateFunctionOid = BsonValidateGeographyFunctionId();
	}

	/* Update the geonear ORDER BY operator expression */
	UpdateGeonearArgsInPlace(geoNearOpExpr->args, validateFunctionOid, keyDatum,
							 querySpecDatum);

	/* Update the base restrict info */
	ListCell *rInfocell;
	foreach(rInfocell, rel->baserestrictinfo)
	{
		RestrictInfo *rinfo = lfirst_node(RestrictInfo, rInfocell);
		if (IsA(rinfo->clause, OpExpr))
		{
			OpExpr *opExpr = (OpExpr *) rinfo->clause;
			if (opExpr->opno == BsonGeonearDistanceRangeOperatorId())
			{
				pgbson_writer rangeSpecWriter;
				PgbsonWriterInit(&rangeSpecWriter);
				PgbsonWriterAppendDocument(&rangeSpecWriter, key, strlen(key),
										   DatumGetPgBson(querySpecDatum));
				pgbson *rangeQueryDoc = PgbsonWriterGetPgbson(&rangeSpecWriter);
				Datum rangeQueryDatum = PointerGetDatum(rangeQueryDoc);

				UpdateGeonearArgsInPlace(opExpr->args, validateFunctionOid, keyDatum,
										 rangeQueryDatum);
			}
		}
		else if (IsA(rinfo->clause, NullTest))
		{
			NullTest *nullTest = (NullTest *) rinfo->clause;
			if (nullTest->nulltesttype == IS_NOT_NULL &&
				IsA(nullTest->arg, FuncExpr))
			{
				FuncExpr *funcExpr = (FuncExpr *) nullTest->arg;
				if (funcExpr->funcid == BsonValidateGeometryFunctionId() ||
					funcExpr->funcid == BsonValidateGeographyFunctionId())
				{
					funcExpr->funcid = validateFunctionOid;

					Const *nullTestKeyConst = (Const *) lsecond(funcExpr->args);
					nullTestKeyConst->constvalue = keyDatum;
				}
			}
		}
	}

	/* Update the target list projector for geonear to have updated query with key */
	if (isEmptyKey)
	{
		ListCell *tlistCell;
		foreach(tlistCell, root->parse->targetList)
		{
			TargetEntry *tle = lfirst_node(TargetEntry, tlistCell);
			if (!tle->resjunk && IsA(tle->expr, FuncExpr))
			{
				FuncExpr *funcExpr = NULL;
				if (CheckBsonProjectGeonearFunctionExpr((FuncExpr *) tle->expr,
														&funcExpr))
				{
					Const *queryConst = (Const *) lsecond(funcExpr->args);
					queryConst->constvalue = querySpecDatum;
				}
			}
		}
	}
}


/*
 * Get all the geo indexes paths from the index list.
 */
void
GetAllGeoIndexesFromRelIndexList(List *indexlist, List **_2dIndexList,
								 List **_2dsphereIndexList)
{
	ListCell *index;
	foreach(index, indexlist)
	{
		IndexOptInfo *indexInfo = lfirst(index);
		if (indexInfo->relam == GIST_AM_OID && indexInfo->indpred != NULL)
		{
			if (indexInfo->nkeycolumns >= 1 &&
				(indexInfo->opfamily[0] == BsonGistGeographyOperatorFamily() ||
				 indexInfo->opfamily[0] == BsonGistGeometryOperatorFamily()))
			{
				bool is2dIndex = indexInfo->opfamily[0] ==
								 BsonGistGeometryOperatorFamily();

				Bson2dGeographyPathOptions *options =
					(Bson2dGeographyPathOptions *) indexInfo->opclassoptions[0];
				const char *indexPath;
				uint32_t indexPathLength;
				Get_Index_Path_Option(options, path, indexPath, indexPathLength);

				const char *copiedPath = pnstrdup(indexPath, indexPathLength);
				if (is2dIndex)
				{
					*_2dIndexList = lappend(*_2dIndexList, (void *) copiedPath);
				}
				else
				{
					*_2dsphereIndexList = lappend(*_2dsphereIndexList,
												  (void *) copiedPath);
				}
			}
		}
	}
}


/*
 * Checks if the geonear query request can be pushed down to the available indexes.
 * If yes then returns the path to use as the key and also sets `useSphericalIndex`
 * to true if 2dsphere index is to be used.
 */
char *
CheckGeonearEmptyKeyCanUseIndex(GeonearRequest *request,
								List *_2dIndexList,
								List *_2dsphereIndexList,
								bool *useSphericalIndex)
{
	int _2dIndexCount = list_length(_2dIndexList);
	int _2dsphereIndexCount = list_length(_2dsphereIndexList);
	if (_2dIndexCount == 0 && _2dsphereIndexCount == 0)
	{
		ThrowNoGeoIndexesFound();
	}

	if (_2dIndexCount > 1)
	{
		ThrowAmbigousIndexesFound("2d");
	}

	if (_2dsphereIndexCount > 1)
	{
		ThrowAmbigousIndexesFound("2dsphere");
	}

	/* No suitable index found for geoneary legacy non-spherical queries */
	if (!request->isGeoJsonPoint && !request->spherical && _2dIndexCount == 0)
	{
		ThrowGeoNearUnableToFindIndex();
	}

	/* GeoJson Query can only use 2dsphere index*/
	if (request->isGeoJsonPoint && _2dsphereIndexCount == 0)
	{
		ThrowGeoNearUnableToFindIndex();
	}

	/* Prioritize 2d index if available */
	if (_2dIndexCount == 1)
	{
		*useSphericalIndex = false;
		return (char *) linitial(_2dIndexList);
	}

	*useSphericalIndex = true;
	return (char *) linitial(_2dsphereIndexList);
}


/* Check if the value of min/max distance is a valid number. */
static double
GetDoubleValueForDistance(const bson_value_t *value, const char *opName)
{
	if (!BsonValueIsNumber(value))
	{
		ereport(ERROR, (
					errcode(ERRCODE_DOCUMENTDB_TYPEMISMATCH),
					errmsg("The value for %s must be a valid numeric type", opName),
					errdetail_log("The value for %s must be a valid numeric type",
								  opName)));
	}

	double distValue = BsonValueAsDouble(value);
	if (isnan(distValue))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg("%s must be positive or zero", opName),
						errdetail_log("%s must be positive or zero", opName)));
	}
	else if (distValue < 0.0)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg("%s value must be zero or greater", opName),
						errdetail_log("%s value must be zero or greater", opName)));
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
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION16433),
						errmsg("Point lies outside the specified interval range [%g, %g]",
							   options->minBound, options->maxBound)));
	}
}


/*
 * Adds the key into the geonear spec based on the matched index metadata
 */
static pgbson *
AddKeyAndGetQueryBson(const pgbson *queryDoc, const char *key)
{
	pgbson_writer writer;
	PgbsonWriterInit(&writer);

	PgbsonWriterAppendUtf8(&writer, "key", 3, key);

	bson_iter_t queryIter;
	PgbsonInitIterator(queryDoc, &queryIter);
	while (bson_iter_next(&queryIter))
	{
		PgbsonWriterAppendValue(&writer, bson_iter_key(&queryIter),
								bson_iter_key_len(&queryIter),
								bson_iter_value(&queryIter));
	}
	return PgbsonWriterGetPgbson(&writer);
}


/*
 * Makes asserts on the args to check if it is of format [bson_validate_geometry/geography(doc, 'path'), <geoNearKeySpec>]
 * and updates the key and geoNearSpec in place.
 *
 * This should only be used for geonear operator expressions which have above arguments
 */
static void
UpdateGeonearArgsInPlace(List *args, Oid validateFunctionOid, Datum keyDatum, Datum
						 geoNearQueryDatum)
{
	Assert(list_length(args) == 2);
	Node *validateExpr = linitial(args);

	Assert(IsA(validateExpr, FuncExpr));
	FuncExpr *validateFuncExpr = (FuncExpr *) validateExpr;

	Assert(list_length(validateFuncExpr->args) == 2);

	Node *querySpecExpr = lsecond(args);
	Assert(IsA(querySpecExpr, Const));
	Const *querySpecConst = (Const *) querySpecExpr;

	Const *validateFunctionKeyConst = lsecond(validateFuncExpr->args);

	validateFuncExpr->funcid = validateFunctionOid;
	validateFunctionKeyConst->constvalue = keyDatum;
	querySpecConst->constvalue = geoNearQueryDatum;
}


/*
 * Validates if the expression for the value is a constant.
 * Variable reference is allowed because these are already validated to be constant at the command
 * level let.
 */
static bool
ValidateConstExpression(const bson_value_t *value, AggregationExpressionData *exprData,
						ParseAggregationExpressionContext *parseContext)
{
	if (exprData == NULL || parseContext == NULL)
	{
		return false;
	}

	memset(exprData, 0, sizeof(AggregationExpressionData));
	ParseAggregationExpressionData(exprData, value, parseContext);
	return exprData->kind == AggregationExpressionKind_Constant ||
		   exprData->kind == AggregationExpressionKind_Variable;
}


/*
 * Recursively check for bson_dollar_project_geonear in targetlist and gets the function expression
 */
static bool
CheckBsonProjectGeonearFunctionExpr(FuncExpr *expr, FuncExpr **geoNearProjectFuncExpr)
{
	CHECK_FOR_INTERRUPTS();
	check_stack_depth();

	if (expr->funcid == BsonDollarProjectGeonearFunctionOid())
	{
		*geoNearProjectFuncExpr = expr;
		return true;
	}

	ListCell *lc;
	foreach(lc, expr->args)
	{
		Node *arg = lfirst(lc);

		/* Unwrap implicit cast if present */
		if (IsA(arg, RelabelType))
		{
			RelabelType *relabeled = (RelabelType *) arg;
			if (relabeled->relabelformat == COERCE_IMPLICIT_CAST)
			{
				arg = (Node *) relabeled->arg;
			}
		}

		if (IsA(arg, FuncExpr))
		{
			if (CheckBsonProjectGeonearFunctionExpr((FuncExpr *) arg,
													geoNearProjectFuncExpr))
			{
				return true;
			}
		}
	}

	return false;
}
