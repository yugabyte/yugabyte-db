/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/geospatial/bson_geojson_utils.h
 *
 * Definitions for utilities to work with GeoJSON Data type
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_GEOJSON_UTILS_H
#define BSON_GEOJSON_UTILS_H

#include <postgres.h>

#include "io/bson_core.h"
#include "geospatial/bson_geospatial_private.h"

#define GEOJSON_CRS_BIGPOLYGON "urn:x-mongodb:crs:strictwinding:EPSG:4326"
#define GEOJSON_CRS_EPSG_4326 "EPSG:4326"
#define GEOJSON_CRS_84 "urn:ogc:def:crs:OGC:1.3:CRS84"

bool ParseValueAsGeoJSON(const bson_value_t *value,
						 GeoJsonParseState *parseState);


#endif
