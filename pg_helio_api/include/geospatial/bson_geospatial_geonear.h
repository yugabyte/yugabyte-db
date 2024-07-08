/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/geospatial/bson_geospatial_geonear.h
 *
 * Common function declarations for method used for $geoNear aggregation stage
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_GEOSPATIAL_GEONEAR_H
#define BSON_GEOSPATIAL_GEONEAR_H

#include <postgres.h>
#include <nodes/pathnodes.h>

#include "io/helio_bson_core.h"
#include "geospatial/bson_geospatial_private.h"
#include "opclass/helio_gin_index_mgmt.h"
#include "geospatial/bson_geospatial_common.h"


/*
 * Represents a Geonear Request
 */
typedef struct GeonearRequest
{
	/* Field to project the calculated distance from stage */
	char *distanceField;

	/* Field to project the geo value from document */
	char *includeLocs;

	/* key of the geospatial index to be used in the stage */
	char *key;

	/* calculated distance to multiplied by distanceMultiplier */
	float8 distanceMultiplier;

	/* max distance for the filtering the documents in meters for 2dsphere index
	 * and in radians if spherical is true for 2d index */
	float8 *maxDistance;

	/* min distance for the filtering the documents in meters for 2dsphere index
	 * and in radians if spherical is true for 2d index */
	float8 *minDistance;

	/* Referrence point from where the distance is calculated */
	Point referencePoint;

	/* Whether the point is GeoJSON point or legacy point, helps in deciding the index
	 * to use */
	bool isGeoJsonPoint;

	/* Additional query filters for the stage */
	bson_value_t query;

	/* Whether spherical distance calculation is requested */
	bool spherical;
} GeonearRequest;


/*
 * Enum that defines how $geoNear calculated distance
 */
typedef enum DistanceMode
{
	DistanceMode_Unknown = 0,

	/* Spherical distance calculation based on earth's spheroid */
	DistanceMode_Spherical,

	/* 2d cartesian distance */
	DistanceMode_Cartesian,

	/* Similar to spherical but in radians with respect to earth's radius */
	DistanceMode_Radians,
} DistanceMode;


/*
 * Runtime distance calculating functions cacheable context
 */
typedef struct GeonearDistanceState
{
	/* For below field definitions consult the GeonearRequest struct */
	StringView key;
	StringView distanceField;
	StringView includeLocs;
	float8 distanceMultiplier;
	Datum referencePoint;
	float8 *maxDistance;
	float8 *minDistance;

	/* FmgrInfo of the Postgis runtime distance method, can be spherical or non-spherical based
	 * on the request
	 */
	FmgrInfo *distanceFnInfo;

	/* Distance calculation mode */
	DistanceMode mode;
} GeonearDistanceState;

/*
 * State to store validation level and type of index for geonear index validation
 */
typedef struct GeonearIndexValidationState
{
	/* Index or runtime validation */
	GeospatialValidationLevel validationLevel;

	/* To get index type for validation; may be typecasted to Bson2dGeometryPathOptions for 2d index */
	BsonGinIndexOptionsBase *options;
} GeonearIndexValidationState;

GeonearRequest * ParseGeonearRequest(const pgbson *geoNearQuery);
void BuildGeoNearDistanceState(GeonearDistanceState *state, const pgbson *geoNearQuery,
							   const GeonearIndexValidationState *validationState);
void BuildGeoNearRangeDistanceState(GeonearDistanceState *state, const
									pgbson *geoNearQuery);
float8 GeonearDistanceFromDocument(const GeonearDistanceState *state, const
								   pgbson *document);
bool ValidateQueryOperatorsForGeoNear(Node *node, void *state);
pgbson * ConvertQueryToGeoNearQuery(bson_iter_t *operatorDocIterator, const char *path,
									const char *mongoOperatorName);
List * CreateExprForGeonearAndNearSphere(const pgbson *queryDoc, Expr *docExpr,
										 const GeonearRequest *request,
										 TargetEntry **targetEntry,
										 SortGroupClause **sortClause);
pgbson * GetGeonearSpecFromNearQuery(bson_iter_t *operatorDocIterator, const char *path,
									 const char *mongoOperatorName);

inline static bool
Is2dWithSphericalDistance(const GeonearRequest *request)
{
	return !request->isGeoJsonPoint && request->spherical;
}


inline static float8
ConvertRadiansToMeters(float8 radians)
{
	return radians * RADIUS_OF_EARTH_M;
}


inline static float8
ConvertMetersToRadians(float8 meters)
{
	return meters / RADIUS_OF_EARTH_M;
}


/*
 * Check if the sort clause contains a geonear operator
 */
inline static bool
TargetListContainsGeonearOp(const List *targetList)
{
	if (!targetList)
	{
		return false;
	}

	TargetEntry *tle;
	ListCell *cell;
	foreach(cell, targetList)
	{
		tle = (TargetEntry *) lfirst(cell);

		if (tle->resjunk)
		{
			if (IsA(tle->expr, OpExpr))
			{
				OpExpr *expr = (OpExpr *) tle->expr;
				if (expr->opno == BsonGeonearDistanceOperatorId())
				{
					return true;
				}
			}
		}
	}

	return false;
}


static inline void
pg_attribute_noreturn()
ThrowGeoNearNotAllowedInContextError()
{
	ereport(ERROR, (
				errcode(MongoLocation5626500),
				errmsg(
					"$geoNear, $near, and $nearSphere are not allowed in this context")));
}

#endif
