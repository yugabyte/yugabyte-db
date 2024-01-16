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
static bool CompareGeoDatumsWithFmgrInfo(FmgrInfo *fmgrInfo,
										 Datum queryGeo, Datum documentGeo);

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
	runtimeState->state.isSpherical = shapeOperator->isSpherical;
	runtimeState->state.geoSpatialDatum = shapeOperator->getShapeDatum(&shapePointsValue,
																	   QUERY_OPERATOR_GEOWITHIN);

	/*
	 * Postgis provides ST_Within function but that doesn't provide the same
	 * behavior as MongoDB, $geoWithin in Mongo also considers geometries that
	 * are on the boundary to be `within` but Postgis makes the exception and
	 * for boundary shapes.
	 *
	 * So we use ST_Covers which includes geometries at the boundaries
	 */
	Oid coverFunctionOid = shapeOperator->isSpherical ?
						   PostgisGeographyCoversFunctionId() :
						   PostgisGeometryCoversFunctionId();
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
	Assert(shapeOperator->shape == GeospatialShapeOperator_GEOMETRY);

	runtimeState->state.isSpherical = shapeOperator->isSpherical;
	runtimeState->state.geoSpatialDatum = shapeOperator->getShapeDatum(&shapePointsValue,
																	   QUERY_OPERATOR_GEOINTERSECTS);

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
	withinState.runtimeMatcher.matcherFunc = &CompareGeoDatumsWithFmgrInfo;
	withinState.runtimeMatcher.queryGeoDatum = runtimeState->state.geoSpatialDatum;
	withinState.runtimeMatcher.flInfo = runtimeState->postgisFuncFmgrInfo;


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

	/* If there are no valid points return false match */
	if (withinState.isEmpty)
	{
		return false;
	}

	/* Free the buffer */
	pfree(withinState.WKBBuffer);

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
CompareGeoDatumsWithFmgrInfo(FmgrInfo *fmgrInfo, Datum queryGeo,
							 Datum documentGeo)
{
	return DatumGetBool(FunctionCall2(fmgrInfo, queryGeo, documentGeo));
}
