SET search_path TO helio_api;

#include "pg_documentdb/sql/udfs/metadata/collection_triggers--0.21-0.sql"
#include "pg_documentdb/sql/schema/collection_metadata--0.21-0.sql"

#include "pg_documentdb/sql/udfs/query/bson_value_functions--0.21-0.sql"
#include "pg_documentdb/sql/udfs/index_mgmt/create_indexes_non_concurrently--0.21-0.sql"

#include "udfs/aggregation/window_aggregate_support--1.21-0.sql"
#include "udfs/aggregation/window_aggregates--1.21-0.sql"
RESET search_path;
