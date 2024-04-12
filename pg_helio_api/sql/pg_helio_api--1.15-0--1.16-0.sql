SET search_path TO helio_api;

#include "udfs/aggregation/bson_inverse_match--1.16-0.sql"

#include "udfs/metadata/empty_data_table--1.16-0.sql"
#include "udfs/commands_crud/cursor_functions--1.16-0.sql"

#include "udfs/geospatial/bson_gist_extensibility_functions--1.16-0.sql"
#include "udfs/aggregation/bson_geonear_functions--1.16-0.sql"
#include "operators/bson_geospatial_operators--1.16-0.sql"
#include "operators/bson_gist_geospatial_op_classes--1.16-0.sql"
#include "operators/bson_gist_geospatial_op_classes_members--1.16-0.sql"

#include "udfs/commands_crud/update--1.16-0.sql"
#include "udfs/commands_crud/insert--1.16-0.sql"
#include "udfs/commands_crud/delete--1.16-0.sql"

#include "rbac/extension_admin_setup--1.16-0.sql"

#include "udfs/commands_diagnostic/coll_stats--1.16-0.sql"
RESET search_path;
