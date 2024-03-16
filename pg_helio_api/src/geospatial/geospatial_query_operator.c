/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/planner/geospatial_query_operator.c
 *
 * Implementation of BSON geospatial query to PostGis operators and function conversion.
 *
 *-------------------------------------------------------------------------
 */
#include <postgres.h>
#include <fmgr.h>
#include <miscadmin.h>
#include <nodes/makefuncs.h>
#include <utils/builtins.h>
#include <utils/guc.h>

#include "io/helio_bson_core.h"
#include "io/pgbsonelement.h"
#include "geospatial/bson_geospatial_common.h"
#include "geospatial/bson_geospatial_shape_operators.h"
#include "geospatial/bson_geospatial_wkb_iterator.h"
#include "planner/mongo_query_operator.h"
#include "metadata/metadata_cache.h"
#include "utils/mongo_errors.h"
#include "utils/query_utils.h"
#include "utils/fmgr_utils.h"


/*
 * RuntimeBsonGeospatialState is the runtime state for the geospatial query operators
 * which has the common state and the operator specific function metadata.
 */
typedef struct RuntimeBsonGeospatialState
{
	/* Common geospatial state which represents if query is speherical or not and has the geospatial datum */
	CommonBsonGeospatialState state;

	/* Mongo geospatial query operator */
	MongoQueryOperatorType type;

	/*
	 * Cached FmgrInfo for the Postgis equivalend function that decided whether query matched the document or not
	 * e.g For $geoIntersects we use ST_Intersects function from Postgres
	 */
	FmgrInfo *postgisFuncFmgrInfo;

	/* Shape operator specific state */
	ShapeOperatorInfo *opInfo;
} RuntimeBsonGeospatialState;


/* GeoSpatial operator runtime handlers */
PG_FUNCTION_INFO_V1(bson_dollar_geowithin);
PG_FUNCTION_INFO_V1(bson_dollar_geointersects);

static void PopulateBsonDollarGeoWithinQueryState(RuntimeBsonGeospatialState *state,
												  const bson_value_t *shapeOperatorValue);
static void PopulateBsonDollarGeoIntersectQueryState(RuntimeBsonGeospatialState *state,
													 const bson_value_t *
													 shapeOperatorValue);
static bool CompareGeoWithinState(const pgbson *document, const char *path,
								  const RuntimeBsonGeospatialState *state);
static bool CompareGeoIntersectsState(const pgbson *document, const char *path,
									  const RuntimeBsonGeospatialState *state);
static bool CompareGeoDatumsWithFmgrInfo(const ProcessCommonGeospatialState *state,
										 StringInfo buffer);
static bool CompareGeoDatumsForDollarCenter(const ProcessCommonGeospatialState *state,
											StringInfo buffer);
static bool SetQueryMatcherResultForCenterSphereHelper(WKBBufferIterator *bufferIterator,
													   const ProcessCommonGeospatialState
													   *state,
													   StringInfo pointBuffer);
static bool PointMatchedForCenterSphere(WKBBufferIterator *bufferIterator,
										StringInfo pointBuffer,
										const ProcessCommonGeospatialState *state);


/*
 * bson_dollar_geowithin implements runtime
 * handler for $geoWithin operator
 */
Datum
bson_dollar_geowithin(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON(0);
	pgbson *geoWithinQuery = PG_GETARG_PGBSON(1);
	pgbsonelement element;
	PgbsonToSinglePgbsonElement(geoWithinQuery, &element);

	const RuntimeBsonGeospatialState *state;
	int argPosition = 1;

	SetCachedFunctionState(
		state,
		RuntimeBsonGeospatialState,
		argPosition,
		PopulateBsonDollarGeoWithinQueryState,
		&element.bsonValue);

	if (state == NULL)
	{
		RuntimeBsonGeospatialState state;
		memset(&state, 0, sizeof(RuntimeBsonGeospatialState));

		PopulateBsonDollarGeoWithinQueryState(&state, &element.bsonValue);
		PG_RETURN_BOOL(CompareGeoWithinState(document, element.path, &state));
	}

	PG_RETURN_BOOL(CompareGeoWithinState(document, element.path, state));
}


/*
 * bson_dollar_geointersects implements runtime
 * handler for $geoIntersects operator
 */
Datum
bson_dollar_geointersects(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON(0);
	pgbson *geoWithinQuery = PG_GETARG_PGBSON(1);
	pgbsonelement element;
	PgbsonToSinglePgbsonElement(geoWithinQuery, &element);

	const RuntimeBsonGeospatialState *state;
	int argPosition = 1;

	SetCachedFunctionState(
		state,
		RuntimeBsonGeospatialState,
		argPosition,
		PopulateBsonDollarGeoIntersectQueryState,
		&element.bsonValue);

	if (state == NULL)
	{
		RuntimeBsonGeospatialState state;
		memset(&state, 0, sizeof(RuntimeBsonGeospatialState));

		PopulateBsonDollarGeoIntersectQueryState(&state, &element.bsonValue);
		PG_RETURN_BOOL(CompareGeoIntersectsState(document, element.path, &state));
	}

	PG_RETURN_BOOL(CompareGeoIntersectsState(document, element.path, state));
}


/*
 * Get the geometry or geography based on the $geoWithin query shape operator
 * const value
 */
static void
PopulateBsonDollarGeoWithinQueryState(RuntimeBsonGeospatialState *runtimeState,
									  const bson_value_t *shapeOperatorValue)
{
	/* Get the shape first */
	bson_value_t shapePointsValue;
	const ShapeOperator *shapeOperator =
		GetShapeOperatorByValue(shapeOperatorValue, &shapePointsValue);
	runtimeState->opInfo = palloc0(sizeof(ShapeOperatorInfo));
	runtimeState->opInfo->op = shapeOperator->op;
	runtimeState->opInfo->queryStage = QueryStage_RUNTIME;
	runtimeState->state.isSpherical = shapeOperator->isSpherical;
	runtimeState->state.geoSpatialDatum = shapeOperator->getShapeDatum(&shapePointsValue,
																	   QUERY_OPERATOR_GEOWITHIN,
																	   runtimeState->
																	   opInfo);
	Oid coverFunctionOid;

	/*
	 * Postgis provides ST_Within function but that doesn't provide the same
	 * behavior as MongoDB, $geoWithin in Mongo also considers geometries that
	 * are on the boundary to be `within` but Postgis makes the exception and
	 * for boundary shapes.
	 *
	 * So we use ST_Covers which includes geometries at the boundaries
	 */
	if (shapeOperator->op == GeospatialShapeOperator_CENTERSPHERE)
	{
		coverFunctionOid = PostgisGeographyDWithinFunctionId();
	}
	else
	{
		coverFunctionOid = shapeOperator->isSpherical ?
						   PostgisGeographyCoversFunctionId() :
						   PostgisGeometryCoversFunctionId();
	}

	runtimeState->postgisFuncFmgrInfo = palloc0(sizeof(FmgrInfo));
	fmgr_info(coverFunctionOid, runtimeState->postgisFuncFmgrInfo);
}


/*
 * Get the geography based on the $geoIntersect query shape operator
 * const value
 */
static void
PopulateBsonDollarGeoIntersectQueryState(RuntimeBsonGeospatialState *runtimeState,
										 const bson_value_t *shapeOperatorValue)
{
	/* Get the shape first */
	bson_value_t shapePointsValue;
	const ShapeOperator *shapeOperator =
		GetShapeOperatorByValue(shapeOperatorValue, &shapePointsValue);

	/* We already validated the shape operator to be only $geometry during planning, here just assert we are processing $geometry */
	Assert(shapeOperator->op == GeospatialShapeOperator_GEOMETRY);
	runtimeState->state.isSpherical = shapeOperator->isSpherical;
	runtimeState->state.geoSpatialDatum = shapeOperator->getShapeDatum(&shapePointsValue,
																	   QUERY_OPERATOR_GEOINTERSECTS,
																	   runtimeState->
																	   opInfo);

	Oid intersectsFunctionOid = PostgisGeographyIntersectsFunctionId();
	runtimeState->postgisFuncFmgrInfo = palloc0(sizeof(FmgrInfo));
	fmgr_info(intersectsFunctionOid, runtimeState->postgisFuncFmgrInfo);
}


/*
 * CompareGeoWithinState compares the documents geometry at path against the geometry in the state
 * and returns true if the document geometry is within the state geometry
 */
static bool
CompareGeoWithinState(const pgbson *document, const char *path,
					  const RuntimeBsonGeospatialState *runtimeState)
{
	/* Always disable strict validation for runtime 2d geometry extraction */
	ProcessCommonGeospatialState withinState;
	bool isSphericalQuery = runtimeState->state.isSpherical;
	GeospatialType processType = isSphericalQuery ? GeospatialType_Geography :
								 GeospatialType_Geometry;
	InitProcessCommonGeospatialState(&withinState,
									 GeospatialValidationLevel_Runtime,
									 processType, NULL);

	/* Fill the runtime matching information */
	withinState.runtimeMatcher.isMatched = false;
	withinState.runtimeMatcher.queryGeoDatum = runtimeState->state.geoSpatialDatum;
	withinState.runtimeMatcher.flInfo = runtimeState->postgisFuncFmgrInfo;
	withinState.opInfo = runtimeState->opInfo;

	if (runtimeState->opInfo->op == GeospatialShapeOperator_CENTERSPHERE ||
		runtimeState->opInfo->op == GeospatialShapeOperator_CENTER)
	{
		withinState.runtimeMatcher.matcherFunc = &CompareGeoDatumsForDollarCenter;
	}
	else
	{
		withinState.runtimeMatcher.matcherFunc = &CompareGeoDatumsWithFmgrInfo;
	}

	bson_iter_t documentIterator;
	PgbsonInitIterator(document, &documentIterator);
	StringView pathView = CreateStringViewFromString(path);

	if (isSphericalQuery)
	{
		BsonIterGetGeographies(&documentIterator, &pathView, &withinState);
	}
	else
	{
		BsonIterGetLegacyGeometryPoints(&documentIterator, &pathView,
										&withinState);
	}

	/* Free the buffer */
	pfree(withinState.WKBBuffer);

	/* If there are no valid points return false match */
	if (withinState.isEmpty)
	{
		return false;
	}

	return withinState.runtimeMatcher.isMatched;
}


/*
 * CompareGeoIntersectsState compares the documents geography at path against the geography in the state
 * and returns true if the document geography intersects with the state geography
 */
static bool
CompareGeoIntersectsState(const pgbson *document, const char *path,
						  const RuntimeBsonGeospatialState *runtimeState)
{
	ProcessCommonGeospatialState geoIntersectState;
	InitProcessCommonGeospatialState(&geoIntersectState,
									 GeospatialValidationLevel_Runtime,
									 GeospatialType_Geography, NULL);

	/* Fill the runtime matching information */
	geoIntersectState.runtimeMatcher.isMatched = false;
	geoIntersectState.runtimeMatcher.matcherFunc = &CompareGeoDatumsWithFmgrInfo;
	geoIntersectState.runtimeMatcher.queryGeoDatum = runtimeState->state.geoSpatialDatum;
	geoIntersectState.runtimeMatcher.flInfo = runtimeState->postgisFuncFmgrInfo;

	bson_iter_t documentIterator;
	PgbsonInitIterator(document, &documentIterator);
	StringView pathView = CreateStringViewFromString(path);

	BsonIterGetGeographies(&documentIterator, &pathView, &geoIntersectState);

	/* If there are no valid points return false match */
	if (geoIntersectState.isEmpty)
	{
		return false;
	}

	/* Free the buffer */
	pfree(geoIntersectState.WKBBuffer);

	return geoIntersectState.runtimeMatcher.isMatched;
}


/*
 * CompareGeoDatumsWithFmgrInfo compares the two geospatial datums using the function
 * represented by the fmgrInfo and returns the boolean result.
 */
static bool
CompareGeoDatumsWithFmgrInfo(const ProcessCommonGeospatialState *state, StringInfo buffer)
{
	GeospatialType type = state->geospatialType;
	bytea *wkbBytea = WKBBufferGetByteaWithSRID(buffer);
	Datum documentGeo = type == GeospatialType_Geometry ?
						GetGeometryFromWKB(wkbBytea) :
						GetGeographyFromWKB(wkbBytea);

	pfree(wkbBytea);

	return DatumGetBool(FunctionCall2(state->runtimeMatcher.flInfo,
									  state->runtimeMatcher.queryGeoDatum,
									  documentGeo));
}


/*
 * CompareGeoDatumsForDollarCenter compares the two geospatial datums
 * for $center and $centerSphere operators using the function
 * represented by the fmgrInfo and returns the boolean result.
 */
static bool
CompareGeoDatumsForDollarCenter(const ProcessCommonGeospatialState *state,
								StringInfo buffer)
{
	if (state->opInfo != NULL)
	{
		if (state->opInfo->op == GeospatialShapeOperator_CENTERSPHERE &&
			state->opInfo->opState != NULL)
		{
			DollarCenterOperatorState *centerState =
				(DollarCenterOperatorState *) state->opInfo->opState;

			if (centerState->isRadiusInfinite)
			{
				return true;
			}

			/* buffer to store each extracted point. Do not modify pointBuffer->data explicitly. */
			StringInfo pointBuffer = makeStringInfo();
			WriteHeaderToWKBBuffer(pointBuffer, WKBGeometryType_Point);
			pointBuffer->len += WKB_BYTE_SIZE_POINT;

			/* iterator for geography buffer from doc */
			WKBBufferIterator bufferIterator;
			InitIteratorFromWKBBuffer(&bufferIterator, buffer);

			bool isMatched = SetQueryMatcherResultForCenterSphereHelper(&bufferIterator,
																		state,
																		pointBuffer);

			pfree(pointBuffer->data);

			return isMatched;
		}
		else if (state->opInfo->op == GeospatialShapeOperator_CENTER)
		{
			GeospatialType type = state->geospatialType;
			bytea *wkbBytea = WKBBufferGetByteaWithSRID(buffer);
			Datum documentGeo = type == GeospatialType_Geometry ?
								GetGeometryFromWKB(wkbBytea) :
								GetGeographyFromWKB(wkbBytea);

			pfree(wkbBytea);

			if (state->opInfo->opState != NULL)
			{
				DollarCenterOperatorState *centerState =
					(DollarCenterOperatorState *) state->opInfo->opState;
				if (centerState->isRadiusInfinite)
				{
					return true;
				}
			}

			/* TODO: update this to match $centerSphere in using ST_DWITHIN */
			return DatumGetBool(FunctionCall2(state->runtimeMatcher.flInfo,
											  state->runtimeMatcher.queryGeoDatum,
											  documentGeo));
		}
	}

	return false;
}


/*
 * Extract individual points from document geography WKB and compare with query doc for matches.
 * Refer geospatial.md for WKB format for each GeoJson type to make sense of this parsing.
 * We haven't stuffed the SRID into the buffer yet so not expecting SRID bytes. SRID gets stuffed
 * when we extract bytea from WKB buffer using WKBBufferGetByteaWithSRID/WKBBufferGetCollectionByteaWithSRID.
 */
static bool
SetQueryMatcherResultForCenterSphereHelper(WKBBufferIterator *bufferIterator,
										   const ProcessCommonGeospatialState *state,
										   StringInfo pointBuffer)
{
	/* skip endianness bytes */
	IncrementWKBBufferIteratorByNBytes(bufferIterator, WKB_BYTE_SIZE_ORDER);

	/* Extract GeoJson type */
	uint32 type = *(uint32 *) (bufferIterator->currptr);
	WKBGeometryType geoType = (WKBGeometryType) type;
	IncrementWKBBufferIteratorByNBytes(bufferIterator, WKB_BYTE_SIZE_TYPE);

	bool isMatched = false;

	switch (geoType)
	{
		case WKBGeometryType_Point:
		{
			isMatched = PointMatchedForCenterSphere(bufferIterator, pointBuffer, state);
			break;
		}

		case WKBGeometryType_LineString:
		{
			int32 numPoints = *(int32 *) (bufferIterator->currptr);
			IncrementWKBBufferIteratorByNBytes(bufferIterator, WKB_BYTE_SIZE_NUM);

			for (int i = 0; i < numPoints; i++)
			{
				isMatched = PointMatchedForCenterSphere(bufferIterator, pointBuffer,
														state);

				if (!isMatched)
				{
					return false;
				}
			}

			break;
		}

		case WKBGeometryType_Polygon:
		{
			int32 numRings = *(int32 *) (bufferIterator->currptr);
			IncrementWKBBufferIteratorByNBytes(bufferIterator, WKB_BYTE_SIZE_NUM);

			for (int i = 0; i < numRings; i++)
			{
				int32 numPoints = *(int32 *) (bufferIterator->currptr);
				IncrementWKBBufferIteratorByNBytes(bufferIterator, WKB_BYTE_SIZE_NUM);

				for (int j = 0; j < numPoints; j++)
				{
					isMatched = PointMatchedForCenterSphere(bufferIterator, pointBuffer,
															state);

					if (!isMatched)
					{
						return false;
					}
				}
			}

			break;
		}

		case WKBGeometryType_MultiPoint:
		case WKBGeometryType_MultiLineString:
		case WKBGeometryType_MultiPolygon:
		case WKBGeometryType_GeometryCollection:
		{
			int32 numShapes = *(int32 *) (bufferIterator->currptr);
			IncrementWKBBufferIteratorByNBytes(bufferIterator, WKB_BYTE_SIZE_NUM);

			for (int i = 0; i < numShapes; i++)
			{
				isMatched = SetQueryMatcherResultForCenterSphereHelper(bufferIterator,
																	   state,
																	   pointBuffer);

				if (!isMatched)
				{
					return false;
				}
			}

			break;
		}

		default:
		{
			ereport(ERROR, (
						errcode(MongoInternalError),
						errmsg(
							"%d unexpected WKB found for multi component $centerSphere opearator",
							geoType),
						errhint(
							"%d unexpected WKB found for multi component $centerSphere opearator",
							geoType)));
		}
	}

	return isMatched;
}


/*
 * Checks if extracted point is a match for given query.
 * arg bufferIterator corresponds to the wkb buffer with bufferIterator->currptr at the current point being processed
 * arg pointBuffer is an aux space to store the current point and get its datum from bytea.
 */
static bool
PointMatchedForCenterSphere(WKBBufferIterator *bufferIterator,
							StringInfo pointBuffer,
							const ProcessCommonGeospatialState *state)
{
	/* extract point and write to point buffer */
	float8 *xy = (float8 *) (bufferIterator->currptr);

	Assert(pointBuffer->len >= 21);
	char *pointData = pointBuffer->data;
	pointData += WKB_BYTE_SIZE_ORDER + WKB_BYTE_SIZE_TYPE;
	memcpy(pointData, (char *) xy, WKB_BYTE_SIZE_POINT);

	IncrementWKBBufferIteratorByNBytes(bufferIterator, WKB_BYTE_SIZE_POINT);

	bytea *wkbBytea = WKBBufferGetByteaWithSRID(pointBuffer);
	Datum queryPoint = GetGeographyFromWKB(wkbBytea);

	pfree(wkbBytea);

	DollarCenterOperatorState *centerState =
		(DollarCenterOperatorState *) state->opInfo->opState;

	Datum radiusArg = Float8GetDatum(centerState->radius);

	/* return result from ST_DWITHIN */
	return DatumGetBool(FunctionCall3(state->runtimeMatcher.flInfo,
									  state->runtimeMatcher.queryGeoDatum,
									  queryPoint,
									  radiusArg));
}
