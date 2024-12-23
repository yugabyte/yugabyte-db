/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/geospatial/bson_geospatial_shape_operators.h
 *
 * Common function and type declarations for shape operators
 *
 *-------------------------------------------------------------------------
 */
#ifndef BSON_GEOSPATIAL_SHAPE_OPERATORS_H
#define BSON_GEOSPATIAL_SHAPE_OPERATORS_H

#include "postgres.h"

#include "io/helio_bson_core.h"
#include "planner/mongo_query_operator.h"


/*
 * Type of shape operators.
 */
typedef enum GeospatialShapeOperator
{
	GeospatialShapeOperator_UNKNOWN = 0,
	GeospatialShapeOperator_POLYGON,
	GeospatialShapeOperator_BOX,
	GeospatialShapeOperator_CENTER,
	GeospatialShapeOperator_GEOMETRY,
	GeospatialShapeOperator_CENTERSPHERE,

	/* Deprecated !! It is part of the types so that this can be ignored */
	GeospatialShapeOperator_UNIQUEDOCS,
} GeospatialShapeOperator;

/*
 * $centerSphere uses different postgis functions for different stages
 */
typedef enum QueryStage
{
	QueryStage_UNKNOWN = 0,
	QueryStage_RUNTIME,
	QueryStage_INDEX,
} QueryStage;

/*
 * Parent struct for shape operator state, for e.g. DollarCenterOperatorState.
 * For derived types this should be the first member.
 */
typedef struct ShapeOperatorState
{ }ShapeOperatorState;

/*
 * To carry shape-specific info
 */
typedef struct ShapeOperatorInfo
{
	QueryStage queryStage;

	GeospatialShapeOperator op;

	/* Parent query operator type */
	MongoQueryOperatorType queryOperatorType;

	/*
	 * Radius for $center and $centerSphere.
	 * Can be set to anything for other operators
	 */
	ShapeOperatorState *opState;
} ShapeOperatorInfo;

/*
 * Function prototype for getting the shape operators
 */
typedef Datum (*BsonValueGetShapeDatum) (const bson_value_t *, ShapeOperatorInfo *);

/*
 * BsonValueGetShapeDatum takes a specific bson value and returns the
 * corresponding postgis geometry/geography shape.
 */
typedef struct ShapeOperator
{
	/* Name of the shape operator */
	const char *shapeOperatorName;

	/* Enum type */
	GeospatialShapeOperator op;

	/* Whether or not the operator is spherical in nature */
	bool isSpherical;

	/* Function that return the geometry/geography postgis shape */
	BsonValueGetShapeDatum getShapeDatum;

	/*
	 * Determines if we should segmentize at index pushdown.
	 * Set to false for $box, $center and $centerSphere
	 */
	bool shouldSegmentize;
} ShapeOperator;

/*
 * Operator state for $center and $centerSphere operators.
 */
typedef struct DollarCenterOperatorState
{
	ShapeOperatorState opState;

	/* Input radius, in radians for $centerSphere and in 2d units for $center */
	double radius;

	/* Radius converted to meters */
	double radiusInMeters;

	/* Area of compliment geography for $centerSphere input in case of radius > (pi/2) */
	Datum complimentArea;

	/* Check if radius is infinite for $center or >= pi for $centerSphere */
	bool isRadiusInfinite;
}DollarCenterOperatorState;

const ShapeOperator * GetShapeOperatorByValue(const bson_value_t *shapeValue,
											  bson_value_t *shapePointsOut);


#endif
