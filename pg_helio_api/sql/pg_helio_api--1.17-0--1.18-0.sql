SET search_path TO helio_api;
#include "udfs/projection/bson_projection--1.18-0.sql"
#include "udfs/aggregation/group_aggregates_support--1.18-0.sql"
#include "udfs/aggregation/group_aggregates--1.18-0.sql"

#include "udfs/aggregation/bson_geonear_functions--1.18-0.sql"
#include "operators/bson_geospatial_operators--1.18-0.sql"
#include "operators/bson_gist_geospatial_op_classes_members--1.18-0.sql"
#include "rbac/extension_readonly_setup--1.17-1.sql"
RESET search_path;
