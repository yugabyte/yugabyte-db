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

	/*
	 * Radius for $center and $centerSphere.
	 * Can be set to anything for other operators
	 */
	ShapeOperatorState *opState;
} ShapeOperatorInfo;

/*
 * Function prototype for getting the shape operators
 */
typedef Datum (*BsonValueGetShapeDatum) (const bson_value_t *, MongoQueryOperatorType,
										 ShapeOperatorInfo *);

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

	double radius;

	bool isRadiusInfinite;
}DollarCenterOperatorState;

const ShapeOperator * GetShapeOperatorByValue(const bson_value_t *shapeValue,
											  bson_value_t *shapePointsOut);

#endif
