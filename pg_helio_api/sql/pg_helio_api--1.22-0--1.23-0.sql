SET search_path TO helio_api;

#include "udfs/query/bson_dollar_comparison--1.23-0.sql"
#include "operators/bson_path_operators--1.23-0.sql"
#include "schema/index_operator_classes_range--1.23-0.sql"
#include "udfs/aggregation/bson_lookup_functions--1.23-0.sql"
#include "schema/collection_indexes_metadata--1.23-0.sql"
#include "udfs/metadata/collection_metadata_functions--1.23-0.sql"

#include "udfs/rum/bson_rum_exclusion_functions--1.23-0.sql"
#include "operators/shard_key_and_document_operators--1.23-0.sql"
#include "schema/bson_rum_exclusion_operator_class--1.23-0.sql"
#include "schema/collection_metadata--1.23-0.sql"
RESET search_path;
