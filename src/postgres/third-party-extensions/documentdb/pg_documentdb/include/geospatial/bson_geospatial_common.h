/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/geospatial/bson_geospatial_common.h
 *
 * Common function declarations for method interacting between documentdb_api and
 * postgis extension to convert and process GeoSpatial Data
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_GEOSPATIAL_COMMON_H
#define BSON_GEOSPATIAL_COMMON_H

#include "postgres.h"
#include "float.h"

#include "io/bson_core.h"
#include "geospatial/bson_geospatial_private.h"
#include "planner/mongo_query_operator.h"
#include "geospatial/bson_geospatial_shape_operators.h"
#include "metadata/metadata_cache.h"

/* Default min and max bounds for 2d index */
#define DEFAULT_2D_INDEX_MIN_BOUND -180.0
#define DEFAULT_2D_INDEX_MAX_BOUND 180.0

/* Utility macro to check equality of double value */
#define DOUBLE_EQUALS(a, b) (fabs((a) - (b)) < DBL_EPSILON)


/* Forward declaration of struct */
typedef struct ProcessCommonGeospatialState ProcessCommonGeospatialState;


/*================================*/
/* Data Types*/
/*================================*/

/* Forward declaration of struct */
typedef struct ProcessCommonGeospatialState ProcessCommonGeospatialState;

/*
 * Types of validation performed when processing the geospatial data
 */
typedef enum GeospatialValidationLevel
{
	GeospatialValidationLevel_Unknown = 0,

	/*
	 * Used with bson_validate_* functions to check if a path in the doc has potential geo values or not,
	 * errors are not thrown with this validation level. Also it only checks for first potential geospatial
	 * value and returns immediately after finding first potential geo value
	 */
	GeospatialValidationLevel_BloomFilter,

	/*
	 * This validation level is required by the runtime operator function families e.g. geoIntersects or geoWithin
	 * Errors are not thrown with this level and all valid geometries are returned
	 */
	GeospatialValidationLevel_Runtime,

	/*
	 * Strict validation is used to enforce geospatial index term generation validations, a document with invalid
	 * potential geometry is okay to be inserted if no geospatial index is present otherwise it returns error.
	 * This validation throws error if any invalid geometries processed
	 */
	GeospatialValidationLevel_Index,
} GeospatialValidationLevel;


/*
 * Type of geospatial data being processed
 */
typedef enum GeospatialType
{
	GeospatialType_UNKNOWN = 0,
	GeospatialType_Geometry,
	GeospatialType_Geography,
} GeospatialType;

/*
 * Enum for postgis functions used in runtime matching in $geoWithin and $geoIntersects
 */
typedef enum PostgisFuncsForDollarGeo
{
	Geometry_Intersects = 0,
	Geography_Intersects,
	Geometry_Covers,
	Geography_Covers,
	Geometry_DWithin,
	Geography_DWithin,
	Geometry_IsValidDetail,
	PostgisFuncsForDollarGeo_MAX
} PostgisFuncsForDollarGeo;


/*
 * The common cache state for geometries / geographies which are used for
 * caching precomputed geometries / geographies
 */
typedef struct CommonBsonGeospatialState
{
	/*
	 * Whether the geodetic datum is spherical or not
	 */
	bool isSpherical;

	/*
	 * Pre computed postgis geometry / geography for query
	 */
	Datum geoSpatialDatum;
} CommonBsonGeospatialState;

/* Signature for runtime function to get match result for $geoWithin and $geoIntersects */
typedef bool (*GeospatialQueryMatcherFunc)(const ProcessCommonGeospatialState *,
										   StringInfo);

/*
 * Runtime query matcher for comapring the resulting geometry/geography from
 * document to a query based on the Mongo geo query operators
 */
typedef struct RuntimeQueryMatcherInfo
{
	/* Matcher function to call for checking a match */
	GeospatialQueryMatcherFunc matcherFunc;

	/* FmgrInfo store for the runtime matching functions */
	FmgrInfo **runtimeFmgrStore;

	/* Main postgis function to use for runtime matching */
	PostgisFuncsForDollarGeo runtimePostgisFunc;

	/* Query geometry/geography datum precomputed */
	Datum queryGeoDatum;

	/* True when matched */
	bool isMatched;
} RuntimeQueryMatcherInfo;


/*
 * Common state to process geospatial data in documents
 */
typedef struct ProcessCommonGeospatialState
{
	/* ========== IN VARIABLES ============ */

	/* The geospatial type we are processing, either Geometry or Geography */
	GeospatialType geospatialType;

	/* The level at which we need to parse the geometry/geography. */
	GeospatialValidationLevel validationLevel;

	/* Runtime Query matcher, Only availabl for runtime matching otherwise NULL */
	RuntimeQueryMatcherInfo runtimeMatcher;

	GeospatialErrorContext *errorCtxt;

	/* Carry shape-specific info, for e.g., radius for $center and $centerSphere */
	ShapeOperatorInfo *opInfo;

	/* ========== OUT VARIABLES ============ */

	/* The resulting geometry's / geography's WKB buffer */
	StringInfo WKBBuffer;

	/* Have we processed a multikey case? */
	bool isMultiKeyContext;

	/* Number of total geo values found */
	uint32 total;

	/* Are there no valid regions? */
	bool isEmpty;
} ProcessCommonGeospatialState;

void BsonIterGetLegacyGeometryPoints(bson_iter_t *documentIter, const
									 StringView *keyPathView,
									 ProcessCommonGeospatialState *state);
void BsonIterGetGeographies(bson_iter_t *documentIter, const StringView *keyPathView,
							ProcessCommonGeospatialState *state);
void BsonIterValidateGeographies(bson_iter_t *documentIter, const StringView *keyPathView,
								 ProcessCommonGeospatialState *state);
Datum BsonExtractGeometryStrict(const pgbson *document, const StringView *pathView);
Datum BsonExtractGeographyStrict(const pgbson *document, const StringView *pathView);
Datum BsonExtractGeometryRuntime(const pgbson *document, const StringView *pathView);
Datum BsonExtractGeographyRuntime(const pgbson *document, const StringView *pathView);


/*
 * Initialize ProcessCommonState with given set of values
 */
static inline void
InitProcessCommonGeospatialState(ProcessCommonGeospatialState *state,
								 GeospatialValidationLevel validationLevel,
								 GeospatialType type,
								 GeospatialErrorContext *errCtxt)
{
	memset(state, 0, sizeof(ProcessCommonGeospatialState));
	state->isEmpty = true;
	state->geospatialType = type;
	state->validationLevel = validationLevel;
	state->WKBBuffer = makeStringInfo();

	/*
	 * Error context while processing the data as geospatial data used in
	 * ereports to throw error where valid
	 */
	state->errorCtxt = errCtxt;
}


/*
 * Validates whether this is a geo-within query operator (for both variants)
 */
static inline bool
IsGeoWithinQueryOperator(MongoQueryOperatorType queryOperatorType)
{
	return queryOperatorType == QUERY_OPERATOR_GEOWITHIN ||
		   queryOperatorType == QUERY_OPERATOR_WITHIN;
}


#endif
