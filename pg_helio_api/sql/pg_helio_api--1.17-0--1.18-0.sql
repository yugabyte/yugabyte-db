SET search_path TO helio_api;
#include "udfs/projection/bson_projection--1.18-0.sql"

#include "udfs/aggregation/bson_geonear_functions--1.18-0.sql"
#include "operators/bson_geospatial_operators--1.18-0.sql"
#include "operators/bson_gist_geospatial_op_classes_members--1.18-0.sql"
RESET search_path;
