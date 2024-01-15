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

#include "io/bson_core.h"
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
 * Function prototype for getting the shape operators
 */
typedef Datum (*BsonValueGetShapeDatum) (const bson_value_t *, MongoQueryOperatorType);


/*
 * BsonValueGetShapeDatum takes a specific bson value and returns the
 * corresponding postgis geometry/geography shape.
 */
typedef struct ShapeOperator
{
	/* Name of the shape operator */
	const char *shapeOperatorName;

	/* Enum type */
	GeospatialShapeOperator shape;

	/* Whether or not the operator is spherical in nature */
	bool isSpherical;

	/* Function that return the geometry/geography postgis shape */
	BsonValueGetShapeDatum getShapeDatum;
} ShapeOperator;


const ShapeOperator * GetShapeOperatorByValue(const bson_value_t *shapeValue,
											  bson_value_t *shapePointsOut);

#endif
