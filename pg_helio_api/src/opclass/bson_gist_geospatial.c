/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/opclass/bson_gist_geospatial.c
 *
 * GIST operator implementation for geospatial indexes.
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "float.h"
#include "access/gist.h"
#include "access/reloptions.h"
#include "executor/spi.h"
#include "fmgr.h"
#include "math.h"

#include "utils/mongo_errors.h"
#include "geospatial/bson_geospatial_common.h"
#include "geospatial/bson_geospatial_geonear.h"
#include "geospatial/bson_geospatial_shape_operators.h"
#include "opclass/helio_gin_common.h"
#include "opclass/helio_gin_index_mgmt.h"
#include "metadata/metadata_cache.h"
#include "utils/fmgr_utils.h"

extern double MaxSegmentLengthInKms;
extern int32 MaxSegmentVertices;


/* Index strategy that Postgis uses for && bounding box overlap operator */
#define POSTGIS_BOUNDING_BOX_OVERLAP_INDEX_STRATEGY 3

/* Index strategy that Postgis uses for bounding box distance operators */
#define POSTGIS_BOUNDING_BOX_DISTANCE_INDEX_STRATEGY 13

/*
 * Defines the area of 2d cartesian plane that roughly corresponds to the hemisphere of earth
 * i.e. [0 to 180] and [90 to 90] => 180 * 180
 */
#define PLANE_2D_MAX_AREA_HEMISPHERE 32400

/* The base query to segmentize bigger query regions into smaller regions */
static inline const char *
BaseGeoSubdivideQuery(const char *shape)
{
	StringInfo baseQuery = makeStringInfo();
	appendStringInfo(baseQuery, "SELECT %s.ST_SUBDIVIDE(%s.ST_SEGMENTIZE($1::%s.%s"
								", $2)::%s.geometry, $3)::%s.%s",
					 PostgisSchemaName, PostgisSchemaName, PostgisSchemaName, shape,
					 PostgisSchemaName, PostgisSchemaName, shape);

	return baseQuery->data;
}


/* Index specific geospatial state for queries */
typedef struct IndexBsonGeospatialState
{
	/*
	 * Common state including the actual geography
	 */
	CommonBsonGeospatialState state;

	/*
	 * The geography is decomposed into smaller rectiliear regions to have
	 * a tighter index bound check.
	 */
	List *segments;

	/*
	 * If radius is infinite for $center and $centerSphere in $within,
	 * we simply return all documents without running any comparisons
	 */
	bool returnAllIndexTermsWithoutComparison;
} IndexBsonGeospatialState;


/*
 * This is a copy of BOX2DF Structure that Postgis uses to store
 * geometries bounding boxes, we define it here to extract the min/max
 * values from the bounding box in helioapi
 */
typedef struct BSON_BOUNDING_BOXF
{
	/* Minimum of x axis of the underlying bounding box */
	float xMin;

	/* Maximum of x axis of the underlying bounding box */
	float xMax;

	/* Minimum of y axis of the underlying bounding box */
	float yMin;

	/* Maximum of x axis of the underlying bounding box */
	float yMax;
} BSON_BOUNDING_BOXF;

PG_FUNCTION_INFO_V1(bson_gist_geometry_2d_options);
PG_FUNCTION_INFO_V1(bson_gist_geometry_2d_compress);
PG_FUNCTION_INFO_V1(bson_gist_geometry_distance_2d);
PG_FUNCTION_INFO_V1(bson_gist_geometry_consistent_2d);

/* Geography GIST support */
PG_FUNCTION_INFO_V1(bson_gist_geography_options);
PG_FUNCTION_INFO_V1(bson_gist_geography_compress);
PG_FUNCTION_INFO_V1(bson_gist_geography_distance);
PG_FUNCTION_INFO_V1(bson_gist_geography_consistent);


static void PopulateGeospatialQueryState(IndexBsonGeospatialState *state,
										 const pgbson *queryDoc,
										 StrategyNumber strategy);
static void SegmentizeQuery(IndexBsonGeospatialState *state);
static float8 GeonearGISTDistanceWithState(PG_FUNCTION_ARGS, const
										   GeonearDistanceState *state);
static bool GeonearRangeConsistent(PG_FUNCTION_ARGS);


/*
 * bson_gist_geometry_2d_options is GIST index support function to specify the index option in serialized format
 * this is useful to store the mongo 2d indexes options e.g. min, max or the name of the index
 * which is shown in the explain plans.
 */
Datum
bson_gist_geometry_2d_options(PG_FUNCTION_ARGS)
{
	local_relopts *relopts = (local_relopts *) PG_GETARG_POINTER(0);

	init_local_reloptions(relopts, sizeof(Bson2dGeometryPathOptions));

	/* add an option that has a default value of single path and accepts *one* value
	 *  This is used later to key off whether it's a single path or multi-key wildcard index options */
	add_local_int_reloption(relopts, "optionsType",
							"The type of the options struct.",
							IndexOptionsType_2d, /* default value */
							IndexOptionsType_2d, /* min */
							IndexOptionsType_2d, /* max */
							offsetof(Bson2dGeometryPathOptions, base.type));
	add_local_int_reloption(relopts, "version",
							"The version of the options struct.",
							IndexOptionsVersion_V0,         /* default value */
							IndexOptionsVersion_V0,         /* min */
							IndexOptionsVersion_V0,         /* max */
							offsetof(Bson2dGeometryPathOptions, base.version));
	add_local_string_reloption(relopts, "path",
							   "Prefix path for the index",
							   NULL, &ValidateSinglePathSpec, &FillSinglePathSpec,
							   offsetof(Bson2dGeometryPathOptions, path));
	add_local_real_reloption(relopts, "maxbound",
							 "Max Bound for the index",
							 DEFAULT_2D_INDEX_MIN_BOUND, -INFINITY, INFINITY,
							 offsetof(Bson2dGeometryPathOptions, maxBound));
	add_local_real_reloption(relopts, "minbound",
							 "Min Bound for the index",
							 DEFAULT_2D_INDEX_MAX_BOUND, -INFINITY, INFINITY,
							 offsetof(Bson2dGeometryPathOptions, minBound));
	PG_RETURN_VOID();
}


/*
 * bson_gist_geometry_2d_compress is another GIST index support function which determines
 * how the index keys are actually stored, this is wrapper around the PostgisSchemaName.geometry_gist_compress_2d
 * which basically converts the geometry into box2df for storage as bounding boxes. We override this to implement
 * validations based on the 2d index's `min` and `max` options and do a range check here, if range check passes
 * then we call the underlying postgis compress method
 */
Datum
bson_gist_geometry_2d_compress(PG_FUNCTION_ARGS)
{
	GISTENTRY *entry_in = (GISTENTRY *) PG_GETARG_POINTER(0);

	/*
	 * If not a leaf key and already in union'd to make an intermediate node that
	 * means additional validations are already checked and this was compressed earlier successfully.
	 * Return the original entry in that case
	 */
	if (!entry_in->leafkey)
	{
		PG_RETURN_POINTER(entry_in);
	}

	if (!PG_HAS_OPCLASS_OPTIONS())
	{
		ereport(ERROR, (errmsg("Index does not have options")));
	}

	Bson2dGeometryPathOptions *options =
		(Bson2dGeometryPathOptions *) PG_GET_OPCLASS_OPTIONS();
	double maxBound = options->maxBound;
	double minBound = options->minBound;
	const char *indexPath;
	uint32_t indexPathLength;
	Get_Index_Path_Option(options, path, indexPath, indexPathLength);

	const pgbson *document = DatumGetPgBson(entry_in->key);
	StringView pathView = CreateStringViewFromStringWithLength(indexPath,
															   indexPathLength);
	Datum documentGeometryDatum = BsonExtractGeometryStrict(document, &pathView);

	/*
	 * For 2d indexes NULL entries should never come because we don't index NULLs, so skipping any NULL checks for keys
	 */

	entry_in->key = documentGeometryDatum;

	/* Get the GIST index entry from postgis geometry_gist_compress_2d which contains the box2df */
	GISTENTRY *box2dfGistOutEntry =
		(GISTENTRY *) DatumGetPointer(OidFunctionCall1(
										  PostgisGeometryGistCompress2dFunctionId(),
										  PointerGetDatum(entry_in)));

	/* Cast this BOX2DF to BSON_BOUNDING_BOXF which is a similar structure */
	BSON_BOUNDING_BOXF *bsonBoundingBox = (BSON_BOUNDING_BOXF *) DatumGetPointer(
		box2dfGistOutEntry->key);

	if (bsonBoundingBox->xMax > maxBound || bsonBoundingBox->yMax > maxBound ||
		bsonBoundingBox->xMin < minBound || bsonBoundingBox->yMin < minBound)
	{
		/* Out of bounds, throw error */
		ereport(ERROR, (errcode(MongoLocation13027),
						errmsg("point not in interval of [ %g, %g ]",
							   minBound, maxBound)));
	}

	PG_RETURN_POINTER(box2dfGistOutEntry);
}


/*
 * bson_gist_geometry_distance_2d used to support order by and nearest neighbour queries with 2d index
 */
Datum
bson_gist_geometry_distance_2d(PG_FUNCTION_ARGS)
{
	pgbson *query = PG_GETARG_PGBSON(1);

	const GeonearDistanceState *state;
	int argPosition = 1;
	StrategyNumber strategy = (StrategyNumber) PG_GETARG_UINT16(2);
	if (strategy != BSON_INDEX_STRATEGY_GEONEAR)
	{
		ereport(ERROR, errmsg("Strategy not supported for geonear"));
	}

	/* Build state to validate query doc with index state */
	GeonearIndexValidationState validationState = { 0 };
	validationState.validationLevel = GeospatialValidationLevel_Index;
	validationState.options = (BsonGinIndexOptionsBase *) PG_GET_OPCLASS_OPTIONS();

	SetCachedFunctionState(
		state,
		GeonearDistanceState,
		argPosition,
		BuildGeoNearDistanceState,
		query,
		&validationState);

	if (state == NULL)
	{
		GeonearDistanceState distanceState;
		memset(&distanceState, 0, sizeof(GeonearDistanceState));
		BuildGeoNearDistanceState(&distanceState, query, &validationState);
		PG_RETURN_FLOAT8(GeonearGISTDistanceWithState(fcinfo, &distanceState));
	}

	PG_RETURN_FLOAT8(GeonearGISTDistanceWithState(fcinfo, state));
}


/*
 * bson_gist_geometry_consistent_2d checks if the query bson can be satisfied with the index key
 */
Datum
bson_gist_geometry_consistent_2d(PG_FUNCTION_ARGS)
{
	GISTENTRY *entry = (GISTENTRY *) PG_GETARG_POINTER(0);
	const pgbson *queryBson = PG_GETARG_PGBSON(1);
	bool *recheck = (bool *) PG_GETARG_POINTER(4);
	StrategyNumber strategy = (StrategyNumber) PG_GETARG_UINT16(2);

	if (strategy == BSON_INDEX_STRATEGY_GEONEAR_RANGE)
	{
		/*
		 * For a $geoNear range check using $minDistance and $maxDistance we are not dependent on Postgis and
		 * have our own index push down logic, so we can directly return the result here.
		 */
		PG_RETURN_BOOL(GeonearRangeConsistent(fcinfo));
	}


	IndexBsonGeospatialState *state;
	int stateArgPositions[2] = { 1, 2 };

	/*
	 * Cache the query part of the consistent check so that we don't compute it for each key.
	 * This is refreshed per query whenever postgres can push the query down to the index and creates
	 * the index scan nodes for the quals that are pushed.
	 *
	 * For multiple quals that are pushed to same index will have dedicated slots of fcinfo, so cache can
	 * be used safely even when the multiple quals are pushed to same index.
	 *
	 * For more information please refer:
	 * https://github.com/postgres/postgres/blob/eeb0ebad79d9350305d9e111fbac76e20fa4b2fe/src/backend/executor/nodeIndexscan.c#L81
	 * https://github.com/postgres/postgres/blob/eeb0ebad79d9350305d9e111fbac76e20fa4b2fe/src/backend/access/gist/gistget.c#L163
	 */
	SetCachedFunctionStateMultiArgs(
		state,
		IndexBsonGeospatialState,
		&stateArgPositions[0],
		2,
		PopulateGeospatialQueryState,
		queryBson,
		strategy);

	if (state == NULL)
	{
		state = palloc0(sizeof(IndexBsonGeospatialState));
		PopulateGeospatialQueryState(state, queryBson, strategy);
	}


	/* Convert to actual postgis index strategy to be used for our operators */
	switch (strategy)
	{
		case BSON_INDEX_STRATEGY_DOLLAR_GEOWITHIN:
		{
			strategy = POSTGIS_BOUNDING_BOX_OVERLAP_INDEX_STRATEGY;
			break;
		}

		default:
		{
			/* We will never reach here but just in case. */
			ereport(ERROR, (errcode(MongoInternalError),
							errmsg("unknown geospatial query operator with strategy %d",
								   strategy)));
		}
	}

	if (state->returnAllIndexTermsWithoutComparison)
	{
		*recheck = false;
		PG_RETURN_BOOL(true);
	}

	bool result = DatumGetBool(OidFunctionCall5(
								   PostgisGeometryGistConsistent2dFunctionId(),
								   PointerGetDatum(entry),
								   state->state.geoSpatialDatum,
								   Int32GetDatum(strategy),
								   PG_GETARG_DATUM(3),
								   PG_GETARG_DATUM(4)));

	/*
	 * recheck is always true for geospatial query operators because
	 * index scan can not satisfy a query completely on its own
	 */
	*recheck = true;

	if (!result)
	{
		/* If the overall box doesn't overlap no need to check all the segments */
		PG_RETURN_BOOL(false);
	}

	if (state->segments == NIL)
	{
		PG_RETURN_BOOL(result);
	}

	/*
	 * Also consult the smaller segments if this is really an overlap,
	 * The box2df of the document is checked with all the geometries in the segments to find if it is overlapped with
	 * any segment.
	 *
	 * This make sure we are not processing huge number of false positives from the index scan
	 */

	BSON_BOUNDING_BOXF *documentBox2df = (BSON_BOUNDING_BOXF *) DatumGetPointer(
		entry->key);
	bool segmentedResult = false;
	ListCell *cell;
	foreach(cell, state->segments)
	{
		Datum segment = PointerGetDatum(lfirst(cell));
		segmentedResult = OidFunctionCall2(PostgisBox2dfGeometryOverlapsFunctionId(),
										   PointerGetDatum(documentBox2df), segment);
		if (segmentedResult)
		{
			PG_RETURN_BOOL(true);
		}
	}

	PG_RETURN_BOOL(false);
}


/*
 * Converts the query bson to geospatial datum based on the strategy
 * used by the operator
 */
static void
PopulateGeospatialQueryState(IndexBsonGeospatialState *state,
							 const pgbson *queryDoc,
							 StrategyNumber strategy)
{
	bson_iter_t queryDocIterator;
	PgbsonInitIterator(queryDoc, &queryDocIterator);

	/*
	 * Query doc should be already validated in the planning,
	 * so its safe to assume that this is valid and non-empty
	 * and is of the form { <field>: <doc defining the geometry / geography> }
	 * e.g. {a: {$geometry: {type: "Point", coordinates: [10, 10]}}}
	 */
	bson_iter_next(&queryDocIterator);

	bson_value_t points;
	const bson_value_t *queryValue = bson_iter_value(&queryDocIterator);
	const ShapeOperator *shapeOperator = GetShapeOperatorByValue(queryValue, &points);
	state->state.isSpherical = shapeOperator->isSpherical;
	ShapeOperatorInfo *opInfo = palloc0(sizeof(ShapeOperatorInfo));
	opInfo->queryStage = QueryStage_INDEX;

	switch (strategy)
	{
		case BSON_INDEX_STRATEGY_DOLLAR_GEOWITHIN:
		{
			opInfo->queryOperatorType = QUERY_OPERATOR_GEOWITHIN;
			state->state.geoSpatialDatum = shapeOperator->getShapeDatum(&points,
																		opInfo);

			if (shapeOperator->op == GeospatialShapeOperator_CENTERSPHERE ||
				shapeOperator->op == GeospatialShapeOperator_CENTER)
			{
				if (opInfo->opState != NULL)
				{
					DollarCenterOperatorState *centerState =
						(DollarCenterOperatorState *) opInfo->opState;

					if (centerState->isRadiusInfinite)
					{
						state->returnAllIndexTermsWithoutComparison = true;
					}
				}
			}

			break;
		}

		case BSON_INDEX_STRATEGY_DOLLAR_GEOINTERSECTS:
		{
			opInfo->queryOperatorType = QUERY_OPERATOR_GEOINTERSECTS;
			state->state.geoSpatialDatum = shapeOperator->getShapeDatum(&points,
																		opInfo);

			break;
		}

		default:
		{
			ereport(ERROR, (errcode(MongoInternalError),
							errmsg("unknown geospatial query operator with strategy %d",
								   strategy)));
		}
	}

	/* Don't segmentize for $box, $center and $centerSphere*/
	if (shapeOperator->shouldSegmentize)
	{
		SegmentizeQuery(state);
	}
}


/*
 * bson_gist_geography_options is GIST index support function to specify the index option in serialized format
 * this is useful to store the mongo 2dsphere indexes options name of the index
 * which is shown in the explain plans.
 */
Datum
bson_gist_geography_options(PG_FUNCTION_ARGS)
{
	local_relopts *relopts = (local_relopts *) PG_GETARG_POINTER(0);

	init_local_reloptions(relopts, sizeof(Bson2dGeographyPathOptions));

	/* add an option that has a default value of single path and accepts *one* value
	 *  This is used later to key off whether it's a single path or multi-key wildcard index options */
	add_local_int_reloption(relopts, "optionsType",
							"The type of the options struct.",
							IndexOptionsType_2dsphere, /* default value */
							IndexOptionsType_2dsphere, /* min */
							IndexOptionsType_2dsphere, /* max */
							offsetof(Bson2dGeographyPathOptions, base.type));
	add_local_int_reloption(relopts, "version",
							"The version of the options struct.",
							IndexOptionsVersion_V0,         /* default value */
							IndexOptionsVersion_V0,         /* min */
							IndexOptionsVersion_V0,         /* max */
							offsetof(Bson2dGeographyPathOptions, base.version));
	add_local_string_reloption(relopts, "path",
							   "Prefix path for the index",
							   NULL, &ValidateSinglePathSpec, &FillSinglePathSpec,
							   offsetof(Bson2dGeographyPathOptions, path));
	PG_RETURN_VOID();
}


/*
 * bson_gist_geography_compress is GIST index support function which determines
 * how the index keys are actually stored for 2dsphere index, this is wrapper around the PostgisSchemaName.geography_gist_compress
 * which basically converts the geography into gidx for storage as bounding boxes. We override this to extract
 * geography from a given document at path which is part of the index options
 */
Datum
bson_gist_geography_compress(PG_FUNCTION_ARGS)
{
	GISTENTRY *entry_in = (GISTENTRY *) PG_GETARG_POINTER(0);

	/*
	 * If not a leaf key and already in union'd to make an intermediate node that
	 * means additional validations are already checked and this was compressed earlier successfully.
	 * Return the original entry in that case
	 */
	if (!entry_in->leafkey)
	{
		PG_RETURN_POINTER(entry_in);
	}

	if (!PG_HAS_OPCLASS_OPTIONS())
	{
		ereport(ERROR, (errmsg("Index does not have options")));
	}

	Bson2dGeographyPathOptions *options =
		(Bson2dGeographyPathOptions *) PG_GET_OPCLASS_OPTIONS();
	const char *indexPath;
	uint32_t indexPathLength;
	Get_Index_Path_Option(options, path, indexPath, indexPathLength);

	const pgbson *document = (pgbson *) DatumGetPgBson(entry_in->key);
	StringView pathView = CreateStringViewFromStringWithLength(indexPath,
															   indexPathLength);
	Datum geographyDatum = BsonExtractGeographyStrict(document, &pathView);
	entry_in->key = geographyDatum;

	/* Get the GIST index entry from postgis geography_gist_compress which contains the gidx bounding box */
	GISTENTRY *result = (GISTENTRY *) DatumGetPointer(
		OidFunctionCall1(PostgisGeographyGistCompressFunctionId(),
						 PointerGetDatum(entry_in)));

	/* return the compressed GIDX entry */
	PG_RETURN_POINTER(result);
}


/*
 * bson_gist_geography_distance used to support order by and nearest neighbour queries with 2dsphere index
 */
Datum
bson_gist_geography_distance(PG_FUNCTION_ARGS)
{
	pgbson *query = PG_GETARG_PGBSON(1);

	const GeonearDistanceState *state;
	int argPosition = 1;
	StrategyNumber strategy = (StrategyNumber) PG_GETARG_UINT16(2);
	if (strategy != BSON_INDEX_STRATEGY_GEONEAR)
	{
		ereport(ERROR, errmsg("Strategy not supported for geonear"));
	}

	SetCachedFunctionState(
		state,
		GeonearDistanceState,
		argPosition,
		BuildGeoNearDistanceState,
		query,
		NULL);

	if (state == NULL)
	{
		GeonearDistanceState distanceState;
		memset(&distanceState, 0, sizeof(GeonearDistanceState));
		BuildGeoNearDistanceState(&distanceState, query, NULL);
		PG_RETURN_FLOAT8(GeonearGISTDistanceWithState(fcinfo, &distanceState));
	}

	PG_RETURN_FLOAT8(GeonearGISTDistanceWithState(fcinfo, state));
}


/*
 * bson_gist_geography_consistent checks if the query bson can be satisfied with the index key
 */
Datum
bson_gist_geography_consistent(PG_FUNCTION_ARGS)
{
	GISTENTRY *entry = (GISTENTRY *) PG_GETARG_POINTER(0);
	const pgbson *queryBson = PG_GETARG_PGBSON(1);
	bool *recheck = (bool *) PG_GETARG_POINTER(4);
	StrategyNumber strategy = (StrategyNumber) PG_GETARG_UINT16(2);

	if (strategy == BSON_INDEX_STRATEGY_GEONEAR_RANGE)
	{
		/*
		 * For a $geoNear range check using $minDistance and $maxDistance we are not dependent on Postgis and
		 * have our own index push down logic, so we can directly return the result here.
		 */
		PG_RETURN_BOOL(GeonearRangeConsistent(fcinfo));
	}

	IndexBsonGeospatialState *state;
	int stateArgPositions[2] = { 1, 2 };

	/*
	 * Cache the query part of the consistent check so that we don't compute it for each key.
	 * This is refreshed per query whenever postgres can push the query down to the index and creates
	 * the index scan nodes for the quals that are pushed.
	 *
	 * For multiple quals that are pushed to same index will have dedicated slots of fcinfo, so cache can
	 * be used safely even when the multiple quals are pushed to same index.
	 *
	 * For more information please refer:
	 * https://github.com/postgres/postgres/blob/eeb0ebad79d9350305d9e111fbac76e20fa4b2fe/src/backend/executor/nodeIndexscan.c#L81
	 * https://github.com/postgres/postgres/blob/eeb0ebad79d9350305d9e111fbac76e20fa4b2fe/src/backend/access/gist/gistget.c#L163
	 */
	SetCachedFunctionStateMultiArgs(
		state,
		IndexBsonGeospatialState,
		&stateArgPositions[0],
		2,
		PopulateGeospatialQueryState,
		queryBson,
		strategy);

	if (state == NULL)
	{
		state = palloc0(sizeof(IndexBsonGeospatialState));
		PopulateGeospatialQueryState(state, queryBson, strategy);
	}

	/* Convert to actual postgis index strategy to be used for our operators */
	switch (strategy)
	{
		case BSON_INDEX_STRATEGY_DOLLAR_GEOINTERSECTS:
		case BSON_INDEX_STRATEGY_DOLLAR_GEOWITHIN:
		{
			strategy = POSTGIS_BOUNDING_BOX_OVERLAP_INDEX_STRATEGY;
			break;
		}

		default:
		{
			/* We will never reach here but just in case. */
			ereport(ERROR, (errcode(MongoInternalError),
							errmsg("unknown geospatial query operator with strategy %d",
								   strategy)));
		}
	}

	if (state->returnAllIndexTermsWithoutComparison)
	{
		*recheck = false;
		PG_RETURN_BOOL(true);
	}

	bool result = DatumGetBool(OidFunctionCall5(
								   PostgisGeographyGistConsistentFunctionId(),
								   PointerGetDatum(entry),
								   state->state.geoSpatialDatum,
								   Int32GetDatum(strategy),
								   PG_GETARG_DATUM(3),
								   PG_GETARG_DATUM(4)));

	/*
	 * recheck is always true for geospatial query operators because
	 * index scan can not satisfy a query completely on its own
	 */
	*recheck = true;

	if (!result)
	{
		/* If the overall box doesn't overlap no need to check all the segments */
		PG_RETURN_BOOL(false);
	}

	if (state->segments == NIL)
	{
		PG_RETURN_BOOL(result);
	}

	/*
	 * Also consult the smaller segments if this is really an overlap,
	 * The gidx of the document is checked with all the geographies in the segments to find if it is overlapped with
	 * any segment.
	 *
	 * This make sure we are not processing huge number of false positives from the index scan
	 */

	void *documentGIDX = (void *) PG_DETOAST_DATUM(entry->key);
	bool segmentedResult = false;
	ListCell *cell;
	foreach(cell, state->segments)
	{
		Datum segment = PointerGetDatum(lfirst(cell));
		segmentedResult = OidFunctionCall2(PostgisGIDXGeographyOverlapsFunctionId(),
										   PointerGetDatum(documentGIDX), segment);
		if (segmentedResult)
		{
			PG_RETURN_BOOL(true);
		}
	}

	PG_RETURN_BOOL(false);
}


/*
 * We want to decompose the given query into smaller chunks because if a query is large enough then the
 * bounding box created for such a region pulls in a lot of false positives during the index scan and
 * the runtime has to filter a lot of rows.
 * e.g.
 * A line (0, 0 -> 10, 10) will have a huge box (0, 0), (0, 10), (10, 10), (10, 0), (0, 0)
 * as the bounding box which will increase the number of false positives.
 *
 * ---------- (10, 10)
 * |       /|
 * |      / |
 * |     /  |
 * |    /   |
 * |   /    |
 * |  /     |
 * | /      |
 * ----------
 * (0,0)
 *
 *
 *
 * But if we decompose it into smaller segments
 * e.g multiple line segments like (0,0 -> 1, 1), (1,1 -> 2,2)... (9,9 -> 10, 10)
 * now we have a tighter index bound and number of false positive reduces drastically
 *
 *        |/|
 *       |/|
 *      |/|
 *     |/|
 *    |/|
 *   |/|
 *  |/|
 *
 *
 * Please note: Large number of segments are also not great because then it increases the overhead
 * of comparing each segment with each document value, the final results to recheck will be a lot less
 * but we would pay the price with time spent on checking each segments.
 *
 * For more information please refer to these function in Postgis documentation and check out the examples:
 * https://postgis.net/docs/ST_Subdivide.html
 */
static void
SegmentizeQuery(IndexBsonGeospatialState *state)
{
	if (MaxSegmentLengthInKms == 0)
	{
		/*
		 * MaxSegmentLengthInKms 0 means we don't want to decompose the query into smaller chunks.
		 */
		return;
	}

	MemoryContext current = CurrentMemoryContext;
	bool isSpherical = state->state.isSpherical;

	SPI_connect();
	int tupleCountLimit = 0;

	int nargs = 3;
	Oid shapeType = isSpherical ? GeographyTypeId() : GeometryTypeId();
	Oid argTypes[3] = { shapeType, FLOAT8OID, INT4OID };


	/* Segment length is defined in kms for 2dsphere which is the more generic case, for 2d we can derive the number from this. */
	double segmentLength = 0;
	if (isSpherical)
	{
		/* segment length expected in meters for 2dsphere */
		segmentLength = MaxSegmentLengthInKms * 1000;
	}
	else
	{
		/* 2d segments are derived like this:
		 * 2dsphere distance b/w point (0,0 -> 1,1) ~= 156.9 Km
		 * 2d cartesian plane distance b/w point (0,0 -> 1,1) ~= 1.41
		 *
		 * Note: for large distance the ratios might not give accurate result and frankly we don't
		 * need this to be precise an approximate conversion is good enough
		 *
		 * So the ratio of 2d cartesian plane to earth distance is about ~=111.27
		 *
		 * Now this ratio only works if the 2d plane bounds are similar to earth which is [180, -180] to [90, -90] since there is no
		 * limit on the bounds and is customizable, we will need to model larger areas of 2d considering this ratio.
		 *
		 * If area of shape is greater than roughly the area of hemisphere in 2d we scale up the length so that segments increase linearly with
		 * larger boxes.
		 * Note: This is just an approximation to avoid doing segmentation at lower values for larger circle it may not be accurate because of sphere and 2d distance differences.
		 */
		double ratioOf2Dplane = MaxSegmentLengthInKms / 111.27;
		double areaOf2dShape = DatumGetFloat8(OidFunctionCall1(
												  PostgisGeometryAreaFunctionId(),
												  state->state.geoSpatialDatum));
		segmentLength = Max(round(areaOf2dShape / PLANE_2D_MAX_AREA_HEMISPHERE), 1) *
						ratioOf2Dplane;
	}

	Datum argValues[3] = {
		state->state.geoSpatialDatum,
		Float8GetDatum(segmentLength),
		Int32GetDatum(MaxSegmentVertices)
	};

	char *argNulls = NULL;
	bool readOnly = true;
	const char *segmentizeQuery = isSpherical ? BaseGeoSubdivideQuery("geography") :
								  BaseGeoSubdivideQuery("geometry");
	if (SPI_execute_with_args(segmentizeQuery, nargs, argTypes, argValues, argNulls,
							  readOnly, tupleCountLimit) != SPI_OK_SELECT)
	{
		ereport(ERROR, (errmsg("could not run SPI query")));
	}

	if (SPI_processed >= 1 && SPI_tuptable)
	{
		for (int tupleNumber = 0; tupleNumber < (int) SPI_processed; tupleNumber++)
		{
			bool isNull;
			AttrNumber segmentAttrId = 1;
			Datum resultDatum = SPI_getbinval(SPI_tuptable->vals[tupleNumber],
											  SPI_tuptable->tupdesc, segmentAttrId,
											  &isNull);
			if (isNull)
			{
				continue;
			}

			Datum segment = SPI_datumTransfer(resultDatum,
											  SPI_tuptable->tupdesc->attrs[0].attbyval,
											  SPI_tuptable->tupdesc->attrs[0].attlen);
			MemoryContext spiContext = MemoryContextSwitchTo(current);
			state->segments = lappend(state->segments, (void *) DatumGetPointer(segment));
			MemoryContextSwitchTo(spiContext);
		}
	}
	SPI_finish();

	int32 segmentsCount = state->segments == NIL ? 0 : state->segments->length;
	ereport(DEBUG1, (errmsg("%s geo query segmentized into %d segments",
							(isSpherical ? "geography" : "geometry"),
							segmentsCount)));
}


/*
 * Returns the distance between the document's bounding box and the reference point's bounding box for geonear
 * distance is in:
 *      meters: if reference point is GeoJSON or if reference point is legacy and spherical calculation is needed
 *      cartesian distance: if reference point is legacy and spherical calculation is not needed
 *
 * For 2d indexes, Postgis uses the planar cartesian distance calculation always, so here for 2d indexes to support
 * spherical distance calculation we get the distances by calling `geography_gist_distance` function which takes geographies and
 * we create the geography from the 2d bounding box of geometry
 */
static float8
GeonearGISTDistanceWithState(PG_FUNCTION_ARGS, const GeonearDistanceState *state)
{
	GISTENTRY *gistEntry = (GISTENTRY *) PG_GETARG_POINTER(0);
	bool *recheck = (bool *) PG_GETARG_POINTER(4);
	float8 distance = DBL_MAX;
	if (fcinfo->flinfo->fn_oid == BsonGistGeographyDistanceFunctionOid() ||
		fcinfo->flinfo->fn_oid == BsonGistGeographyConsistentFunctionOid())
	{
		/* This infers that the we are using a 2dsphere index and want to use spherical distance, here the
		 * GistEntry is a GIDX box already and we can calculate the spherical distances b/w boundig boxes straight
		 * away.
		 */
		distance = DatumGetFloat8(OidFunctionCall5(
									  PostgisGeographyGistDistanceFunctionId(),
									  PointerGetDatum(gistEntry),
									  state->referencePoint,
									  Int32GetDatum(
										  POSTGIS_BOUNDING_BOX_DISTANCE_INDEX_STRATEGY),
									  PG_GETARG_DATUM(3),
									  PG_GETARG_DATUM(4)));
	}
	else
	{
		/*
		 * We are using a 2d index here in this case
		 */
		if (state->mode == DistanceMode_Radians)
		{
			BSON_BOUNDING_BOXF *box = (BSON_BOUNDING_BOXF *) gistEntry->key;

			Point point;
			memset(&point, 0, sizeof(Point));

			StringInfo boxGeography = makeStringInfo();

			/* Write a polygon for the box */
			WriteHeaderToWKBBuffer(boxGeography, WKBGeometryType_Polygon);
			int32 numOfRings = 1;
			int32 numOfPoints = 5;
			appendBinaryStringInfoNT(boxGeography, (char *) &numOfRings,
									 WKB_BYTE_SIZE_NUM);
			appendBinaryStringInfoNT(boxGeography, (char *) &numOfPoints,
									 WKB_BYTE_SIZE_NUM);

			point.x = box->xMin;
			point.y = box->yMin;
			WritePointToWKBBuffer(boxGeography, &point);

			point.y = box->yMax;
			WritePointToWKBBuffer(boxGeography, &point);

			point.x = box->xMax;
			point.y = box->yMax;
			WritePointToWKBBuffer(boxGeography, &point);

			point.y = box->yMin;
			WritePointToWKBBuffer(boxGeography, &point);

			point.x = box->xMin;
			point.y = box->yMin;
			WritePointToWKBBuffer(boxGeography, &point);

			bytea *wkbWithSRID = WKBBufferGetByteaWithSRID(boxGeography);
			Datum geographyBox = GetGeographyFromWKB(wkbWithSRID);

			distance = DatumGetFloat8(FunctionCall2(
										  state->distanceFnInfo,
										  geographyBox,
										  state->referencePoint));

			/*
			 * Set the leaf GIST nodes to be rechecked
			 */
			if (GIST_LEAF(gistEntry))
			{
				*recheck = true;
			}
		}
		else
		{
			/* 2d cartesian plane distance between bounding box and referrence point */
			distance = DatumGetFloat8(OidFunctionCall5(
										  PostgisGeometryGistDistanceFunctionId(),
										  PointerGetDatum(gistEntry),
										  state->referencePoint,
										  Int32GetDatum(
											  POSTGIS_BOUNDING_BOX_DISTANCE_INDEX_STRATEGY),
										  PG_GETARG_DATUM(3),
										  PG_GETARG_DATUM(4)));
		}
	}

	return distance;
}


/*
 * Checks if the distance between the document's bounding box and the reference point's bounding box is within the
 * min and max distance specified in the query
 */
static bool
GeonearRangeConsistent(PG_FUNCTION_ARGS)
{
	pgbson *query = PG_GETARG_PGBSON(1);

	const GeonearDistanceState *state;
	int argPosition = 1;
	float8 gistBoxDistance = 0.0;

	SetCachedFunctionState(
		state,
		GeonearDistanceState,
		argPosition,
		BuildGeoNearRangeDistanceState,
		query);

	if (state == NULL)
	{
		GeonearDistanceState distanceState;
		memset(&distanceState, 0, sizeof(GeonearDistanceState));
		BuildGeoNearRangeDistanceState(&distanceState, query);
		gistBoxDistance = GeonearGISTDistanceWithState(fcinfo, &distanceState);
	}
	else
	{
		gistBoxDistance = GeonearGISTDistanceWithState(fcinfo, state);
	}

	if (state->maxDistance != NULL && gistBoxDistance > *(state->maxDistance) &&
		!DOUBLE_EQUALS(gistBoxDistance, *(state->maxDistance)))
	{
		return false;
	}

	if (state->minDistance != NULL && gistBoxDistance < *(state->minDistance) &&
		!DOUBLE_EQUALS(gistBoxDistance, *(state->minDistance)))
	{
		return false;
	}

	return true;
}
