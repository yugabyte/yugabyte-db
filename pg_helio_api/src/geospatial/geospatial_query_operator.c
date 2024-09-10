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
#include "geospatial/bson_geospatial_wkb_iterator.h"
#include "planner/mongo_query_operator.h"
#include "metadata/metadata_cache.h"
#include "utils/helio_errors.h"
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
	 * Cached FmgrInfo store for the Postgis equivalend functions that decide whether query matched the document or not
	 * e.g For $geoIntersects we use ST_Intersects function from Postgres
	 */
	FmgrInfo **postgisFuncFmgrInfo;

	/* Main postgis function to use for runtime matching */
	PostgisFuncsForDollarGeo runtimePostgisFunc;

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
static bool CompareForGeoWithinDatum(const ProcessCommonGeospatialState *state,
									 StringInfo wkbBuffer);
static void FillFmgrInfoForGeoWithin(const ShapeOperator *shapeOperator,
									 RuntimeBsonGeospatialState *runtimeState);

/*================= Operator Execution functions =======================*/
static bool ContinueIfMatched(void *state);
static void VisitSingleGeometryForGeoWithin(const WKBGeometryConst *geometryConst,
											void *state);
static void VisitEachPointForCenterSphere(const WKBGeometryConst *geometryConst,
										  void *state);


/*
 * Execution function for $geoWithin
 */
static const WKBVisitorFunctions GeoWithinMultiGeometryVisitorFuncs = {
	.ContinueTraversal = ContinueIfMatched,
	.VisitEachPoint = NULL,
	.VisitGeometry = NULL,
	.VisitSingleGeometry = VisitSingleGeometryForGeoWithin,
	.VisitPolygonRing = NULL
};


static const WKBVisitorFunctions GeoWithinCenterSphereVisitorFuncs = {
	.ContinueTraversal = ContinueIfMatched,
	.VisitEachPoint = VisitEachPointForCenterSphere,
	.VisitGeometry = NULL,
	.VisitSingleGeometry = NULL,
	.VisitPolygonRing = NULL
};


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
	runtimeState->opInfo->queryOperatorType = QUERY_OPERATOR_GEOWITHIN;
	runtimeState->state.isSpherical = shapeOperator->isSpherical;
	runtimeState->state.geoSpatialDatum =
		shapeOperator->getShapeDatum(&shapePointsValue, runtimeState->opInfo);

	/*
	 * Postgis provides ST_Within function but that doesn't provide the same
	 * behavior as MongoDB, $geoWithin in Mongo also considers geometries that
	 * are on the boundary to be `within` but Postgis makes the exception and
	 * for boundary shapes.
	 *
	 * So we use ST_Covers which includes geometries at the boundaries
	 */
	FillFmgrInfoForGeoWithin(shapeOperator, runtimeState);
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
	runtimeState->opInfo = palloc0(sizeof(ShapeOperatorInfo));
	runtimeState->opInfo->op = shapeOperator->op;
	runtimeState->opInfo->queryStage = QueryStage_RUNTIME;
	runtimeState->opInfo->queryOperatorType = QUERY_OPERATOR_GEOINTERSECTS;
	runtimeState->state.isSpherical = shapeOperator->isSpherical;
	runtimeState->state.geoSpatialDatum =
		shapeOperator->getShapeDatum(&shapePointsValue, runtimeState->opInfo);

	runtimeState->postgisFuncFmgrInfo =
		(FmgrInfo **) palloc0(PostgisFuncsForDollarGeo_MAX * sizeof(FmgrInfo *));
	Oid intersectsFunctionOid = PostgisGeographyIntersectsFunctionId();
	runtimeState->postgisFuncFmgrInfo[Geography_Intersects] = palloc0(sizeof(FmgrInfo));
	fmgr_info(intersectsFunctionOid,
			  runtimeState->postgisFuncFmgrInfo[Geography_Intersects]);
	runtimeState->runtimePostgisFunc = Geography_Intersects;
}


/* Populate the FmgrInfo store for $geoWithin depending on shape operator */
static void
FillFmgrInfoForGeoWithin(const ShapeOperator *shapeOperator,
						 RuntimeBsonGeospatialState *runtimeState)
{
	Oid coverFunctionOid;
	PostgisFuncsForDollarGeo postgisFunc;
	if (shapeOperator->op == GeospatialShapeOperator_CENTERSPHERE)
	{
		coverFunctionOid = PostgisGeographyDWithinFunctionId();
		postgisFunc = Geography_DWithin;
	}
	else if (shapeOperator->op == GeospatialShapeOperator_CENTER)
	{
		coverFunctionOid = PostgisGeometryDWithinFunctionId();
		postgisFunc = Geometry_DWithin;
	}
	else
	{
		coverFunctionOid = shapeOperator->isSpherical ?
						   PostgisGeographyCoversFunctionId() :
						   PostgisGeometryCoversFunctionId();
		postgisFunc = shapeOperator->isSpherical ? Geography_Covers : Geometry_Covers;
	}

	runtimeState->postgisFuncFmgrInfo =
		(FmgrInfo **) palloc0(PostgisFuncsForDollarGeo_MAX * sizeof(FmgrInfo *));
	runtimeState->postgisFuncFmgrInfo[postgisFunc] = palloc0(sizeof(FmgrInfo));
	fmgr_info(coverFunctionOid, runtimeState->postgisFuncFmgrInfo[postgisFunc]);
	runtimeState->runtimePostgisFunc = postgisFunc;

	runtimeState->postgisFuncFmgrInfo[Geography_Intersects] = palloc0(sizeof(FmgrInfo));
	Oid geoIntersectsOid = PostgisGeographyIntersectsFunctionId();
	fmgr_info(geoIntersectsOid, runtimeState->postgisFuncFmgrInfo[Geography_Intersects]);
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
	withinState.runtimeMatcher.matcherFunc = &CompareForGeoWithinDatum;
	withinState.runtimeMatcher.queryGeoDatum = runtimeState->state.geoSpatialDatum;

	withinState.runtimeMatcher.runtimeFmgrStore =
		(FmgrInfo **) palloc0(PostgisFuncsForDollarGeo_MAX * sizeof(FmgrInfo *));

	withinState.runtimeMatcher.runtimeFmgrStore = runtimeState->postgisFuncFmgrInfo;
	withinState.runtimeMatcher.runtimePostgisFunc = runtimeState->runtimePostgisFunc;

	withinState.opInfo = runtimeState->opInfo;

	if (runtimeState->opInfo->op == GeospatialShapeOperator_CENTERSPHERE ||
		runtimeState->opInfo->op == GeospatialShapeOperator_CENTER)
	{
		withinState.runtimeMatcher.matcherFunc = &CompareGeoDatumsForDollarCenter;
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
	DeepFreeWKB(withinState.WKBBuffer);

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

	geoIntersectState.runtimeMatcher.runtimeFmgrStore =
		(FmgrInfo **) palloc0(PostgisFuncsForDollarGeo_MAX * sizeof(FmgrInfo *));

	geoIntersectState.runtimeMatcher.runtimeFmgrStore =
		runtimeState->postgisFuncFmgrInfo;
	geoIntersectState.runtimeMatcher.runtimePostgisFunc =
		runtimeState->runtimePostgisFunc;

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
	DeepFreeWKB(geoIntersectState.WKBBuffer);

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

	const RuntimeQueryMatcherInfo *runtimeMatcher = &state->runtimeMatcher;
	return DatumGetBool(
		FunctionCall2(
			runtimeMatcher->runtimeFmgrStore[runtimeMatcher->runtimePostgisFunc],
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
		const RuntimeQueryMatcherInfo *runtimeMatcher = &state->runtimeMatcher;

		if (state->opInfo->op == GeospatialShapeOperator_CENTERSPHERE &&
			state->opInfo->opState != NULL)
		{
			DollarCenterOperatorState *centerState =
				(DollarCenterOperatorState *) state->opInfo->opState;

			if (centerState->isRadiusInfinite)
			{
				return true;
			}

			/* Traverse and extract each point and check for distance from the center point */
			TraverseWKBBuffer(buffer, &GeoWithinCenterSphereVisitorFuncs, (void *) state);

			bool isMatched = state->runtimeMatcher.isMatched;

			/* Additionally, check for intersection with compliment area if radius > PI/2 */
			bool geographyIntersectsWithCompliment = false;

			if (isMatched &&
				centerState->complimentArea != (Datum) 0)
			{
				bytea *wkbBytea = WKBBufferGetByteaWithSRID(buffer);
				Datum docDatum = GetGeographyFromWKB(wkbBytea);

				geographyIntersectsWithCompliment =
					DatumGetBool(
						FunctionCall2(
							runtimeMatcher->runtimeFmgrStore[Geography_Intersects],
							centerState->complimentArea,
							docDatum));
				pfree(DatumGetPointer(docDatum));
				pfree(wkbBytea);
			}

			return isMatched && !geographyIntersectsWithCompliment;
		}
		else if (state->opInfo->op == GeospatialShapeOperator_CENTER &&
				 state->opInfo->opState != NULL)
		{
			GeospatialType type = state->geospatialType;

			/* We should not land here, as $center is non-spherical type query */
			if (type == GeospatialType_Geography)
			{
				ereport(ERROR, (
							errcode(ERRCODE_HELIO_INTERNALERROR),
							errmsg("Unexpected geographical type encountered in $center")
							));
			}

			DollarCenterOperatorState *centerState =
				(DollarCenterOperatorState *) state->opInfo->opState;
			if (centerState->isRadiusInfinite)
			{
				return true;
			}

			bytea *wkbBytea = WKBBufferGetByteaWithSRID(buffer);
			Datum documentGeo = GetGeometryFromWKB(wkbBytea);
			pfree(wkbBytea);
			Datum radiusArg = Float8GetDatum(centerState->radius);

			/* Result from ST_DWITHIN */
			bool isMatched = DatumGetBool(
				FunctionCall3(
					runtimeMatcher->runtimeFmgrStore[runtimeMatcher->runtimePostgisFunc],
					runtimeMatcher->queryGeoDatum,
					documentGeo,
					radiusArg));
			pfree(DatumGetPointer(documentGeo));

			return isMatched;
		}
	}

	return false;
}


/*
 * CompareForGeoWithinDatum compares the geospatial datum for $geoWithin operator.
 * There are few cases which are different from how Mongo protocol behaves:
 *
 * Scenario 1: For a multi polygon geowithin queries, mongodb protocol returns "true" when all the region components of
 * the document is covered either by single or multiple components of query, but postgis assumes that only a single region of query
 * should contain all the region of document to be a match.
 *    e.g. Region A (Query) with 3 components A1, A2, A3
 *         Region B (Document) with 3 components B1, B2, B3
 *         Here let's assume A1, covers B1, A2 covers B2 and B3 and A3 doesn't cover any of the components of B
 *         MongoDB => This is a match because overall the document is covered fully by all the components of query,
 *                    it doesn't matter if all components of B are covered by a single component of A.
 *         Postgis => This is not a match because B and all its component should be covered by a single component of A, which
 *                    is not the case here
 *
 *    To support this mongo behavior, we read individual components of documents and pass it to postgis, if all individual components are covered by query then we can
 *    consider this a mongo match
 *
 * Scenario 2: When polygons with holes are part of the comparision for geowithin, postgis has slightly different behavior then mongo db protocol where
 *       a) In postgis a polygon with hole doesn't cover itself
 *       b) Any polygon (with or without holes) doesn't consider other polygons which have holes in the covering region of first polygon.
 *
 *    This is hard to fix without handling the geometries ourselves, so we error out for now if polygons with holes are part of the match
 *    TODO: fix polygons with holes behavior with our own handling.
 */
static bool
CompareForGeoWithinDatum(const ProcessCommonGeospatialState *state, StringInfo wkbBuffer)
{
	GeospatialType type = state->geospatialType;
	WKBGeometryType wkbType = *(int32 *) (wkbBuffer->data + WKB_BYTE_SIZE_ORDER);
	if (!IsWKBCollectionType(wkbType) || type == GeospatialType_Geometry)
	{
		/* Simple Shape case or 2d can be directly delegated to postgis */
		return CompareGeoDatumsWithFmgrInfo(state, wkbBuffer);
	}

	/* Get the type and if type is multi* / collection check each individual geometry */
	TraverseWKBBuffer(wkbBuffer, &GeoWithinMultiGeometryVisitorFuncs, (void *) state);

	return state->runtimeMatcher.isMatched;
}


/*
 * Continue traversing a buffer if the runtime matcher indicates match
 */
static bool
ContinueIfMatched(void *state)
{
	ProcessCommonGeospatialState *geoState = (ProcessCommonGeospatialState *) state;
	return geoState->runtimeMatcher.isMatched;
}


/*
 * Compare each individual geometry in the multi geometry collection with the query geometry
 * to be within the query geometry
 */
static void
VisitSingleGeometryForGeoWithin(const WKBGeometryConst *geometryConst, void *state)
{
	if (geometryConst->geometryType == WKBGeometryType_Polygon &&
		geometryConst->numRings > 1)
	{
		/* TODO: Fix polygon with holes geowithin comparision, for now we throw unsupported error because of
		 * Postgis matching difference for these cases
		 */
		ereport(ERROR, (
					errcode(ERRCODE_HELIO_COMMANDNOTSUPPORTED),
					errmsg("$geoWithin currently doesn't support polygons with holes")
					));
	}

	/* Make a copy of individual geometry in the multi geometry collection */
	StringInfo singleGeometryBuffer = makeStringInfo();
	WriteBufferWithLengthToWKBBuffer(singleGeometryBuffer, geometryConst->geometryStart,
									 geometryConst->length);

	ProcessCommonGeospatialState *geoState = (ProcessCommonGeospatialState *) state;
	geoState->runtimeMatcher.isMatched = CompareGeoDatumsWithFmgrInfo(geoState,
																	  singleGeometryBuffer);

	DeepFreeWKB(singleGeometryBuffer);
}


/*
 * Checks if extracted point is a match for given query $centerSphere query.
 * Each point extracted needs to be checked for distance from the center point.
 */
static void
VisitEachPointForCenterSphere(const WKBGeometryConst *pointConst, void *state)
{
	ProcessCommonGeospatialState *geoState = (ProcessCommonGeospatialState *) state;

	/* extract point and write to point buffer */
	StringInfo pointBuffer = makeStringInfo();
	WriteHeaderToWKBBuffer(pointBuffer, WKBGeometryType_Point);
	WriteBufferWithLengthToWKBBuffer(pointBuffer, pointConst->geometryStart,
									 pointConst->length);

	bytea *wkbBytea = WKBBufferGetByteaWithSRID(pointBuffer);
	Datum queryPoint = GetGeographyFromWKB(wkbBytea);

	pfree(wkbBytea);

	DollarCenterOperatorState *centerState =
		(DollarCenterOperatorState *) geoState->opInfo->opState;

	double radiusInMeters = centerState->radiusInMeters;
	Datum radiusArg = Float8GetDatum(radiusInMeters);
	RuntimeQueryMatcherInfo *runtimeMatcher = &geoState->runtimeMatcher;

	/* result from ST_DWITHIN */
	geoState->runtimeMatcher.isMatched =
		DatumGetBool(
			FunctionCall3(
				runtimeMatcher->runtimeFmgrStore[runtimeMatcher->runtimePostgisFunc],
				runtimeMatcher->queryGeoDatum,
				queryPoint,
				radiusArg));

	DeepFreeWKB(pointBuffer);
}
