SET search_path TO helio_api;

#include "udfs/metadata/collection_triggers--1.21-0.sql"
#include "schema/collection_metadata--1.21-0.sql"

#include "udfs/query/bson_value_functions--1.21-0.sql"

#include "udfs/aggregation/window_aggregate_support--1.21-0.sql"
#include "udfs/aggregation/window_aggregates--1.21-0.sql"

RESET search_path;
