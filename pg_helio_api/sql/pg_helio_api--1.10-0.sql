
CREATE SCHEMA helio_api;
CREATE SCHEMA helio_api_internal;
CREATE SCHEMA helio_api_catalog;
CREATE SCHEMA helio_data;

SET search_path TO helio_api;

/*
 * Region: Query functions
 */
 #include "udfs/query/bson_query_match--1.10-0.sql"

#include "udfs/query/bson_dollar_comparison--1.10-0.sql"
#include "udfs/query/bson_dollar_array--1.10-0.sql"
#include "udfs/query/bson_dollar_element_bitwise--1.10-0.sql"
#include "udfs/query/bson_dollar_geospatial--1.10-0.sql"
#include "udfs/query/bson_dollar_evaluation--1.10-0.sql"

#include "udfs/query/bson_orderby--1.10-0.sql"

#include "udfs/projection/bson_projection--1.10-0.sql"
#include "udfs/projection/bson_expression--1.10-0.sql"


#include "udfs/query/bsonquery_dollar_operators--1.10-0.sql"

/*
 * Region: Query operators
 */
#include "pg_documentdb/sql/operators/bson_query_operators--0.10-0.sql"
#include "pg_documentdb/sql/operators/bson_partial_filter_operators--0.10-0.sql"
#include "pg_documentdb/sql/operators/bsonquery_dollar_operators--0.10-0.sql"

#include "pg_documentdb/sql/operators/bsonquery_btree_family--0.10-0.sql"

/*
 * Region: Shard key and document
 */
 #include "pg_documentdb/sql/schema/shard_key_and_document--0.10-0.sql"

/*
 * Region: RUM operators and functions
 */
 #include "udfs/rum/handler--1.10-0.sql"
 #include "udfs/rum/extensibility_functions--1.10-0.sql"
 #include "udfs/rum/single_path_extensibility_functions--1.10-0.sql"
 #include "udfs/rum/bson_preconsistent--1.10-0.sql"
 #include "pg_documentdb/sql/operators/bson_path_operators--0.10-0.sql"

 /*
 * Region: RUM metadata
 */
 #include "pg_documentdb/sql/schema/rum_access_method--0.10-0.sql"
 #include "pg_documentdb/sql/schema/single_path_operator_class--0.10-0.sql"

 -- TODO: Re-enable this for Helio when ready
 -- #include "pg_documentdb/sql/schema/index_operator_classes_preconsistent--0.10-0.sql"

/*
 * Region: Aggregation operators.
 */
#include "udfs/aggregation/bson_aggregation_support--1.10-0.sql"
#include "udfs/aggregation/bson_aggregation_pipeline--1.10-0.sql"
#include "udfs/aggregation/bson_aggregation_find--1.10-0.sql"
#include "udfs/aggregation/bson_aggregation_count--1.10-0.sql"
#include "udfs/aggregation/bson_aggregation_distinct--1.10-0.sql"

#include "udfs/aggregation/group_aggregates_support--1.10-0.sql"
#include "udfs/aggregation/group_aggregates--1.10-0.sql"
#include "udfs/aggregation/bson_unwind_functions--1.10-0.sql"
#include "udfs/aggregation/bson_lookup_functions--1.10-0.sql"
#include "udfs/aggregation/distinct_aggregates--1.10-0.sql"

#include "udfs/metadata/empty_data_table--1.10-0.sql"
#include "udfs/metadata/collection--1.10-0.sql"


/*
 * Region: Collection Metadata
 */
#include "udfs/metadata/collection_metadata_functions--1.10-0.sql"
#include "udfs/metadata/collection_indexes_metadata_functions--1.10-0.sql"

#include "pg_documentdb/sql/schema/collection_metadata--0.10-0.sql"
#include "pg_documentdb/sql/schema/collection_metadata_views--0.10-0.sql"
#include "pg_documentdb/sql/schema/collection_metadata_schemavalidation--0.10-0.sql"

#include "pg_documentdb/sql/schema/collection_indexes_metadata--0.10-0.sql"

/*
 * Region: Sharding Metadata
*/
#include "udfs/metadata/sharding_metadata_functions--1.10-0.sql"

/*
 * Region: Schema Management APIs
 */
 #include "udfs/schema_mgmt/create_collection--1.10-0.sql"
 #include "udfs/schema_mgmt/shard_collection--1.10-0.sql"

/*
 * Region: Index Manangement APIs
 */
 #include "udfs/index_mgmt/create_indexes_non_concurrently--1.10-0.sql"
 #include "udfs/index_mgmt/create_builtin_id_index--1.10-0.sql"
 #include "udfs/index_mgmt/record_id_index--1.10-0.sql"
 #include "udfs/index_mgmt/index_build_is_in_progress--1.10-0.sql"
 #include "udfs/index_mgmt/index_spec_as_bson--1.10-0.sql"
 #include "udfs/index_mgmt/index_spec_options_are_equivalent--1.10-0.sql"

/*
 * Region: Basic CRUD APIs & Commands
 */
#include "udfs/commands_crud/insert--1.10-0.sql"
#include "udfs/commands_crud/insert_one_helper--1.10-0.sql"

#include "udfs/commands_crud/bson_update_document--1.10-0.sql"
#include "udfs/commands_crud/update--1.10-0.sql"

#include "udfs/commands_crud/delete--1.10-0.sql"
#include "udfs/commands_crud/find_and_modify--1.10-0.sql"

#include "udfs/commands_crud/query_cursors_aggregate--1.10-0.sql"
#include "udfs/commands_crud/query_cursors_single_page--1.10-0.sql"

/*
 * Region: RBAC Metadata
 */
 #include "pg_documentdb/sql/rbac/extension_admin_setup--0.10-0.sql"

/*
 * Region: Background Index Schema
 */
 #include "schema/background_index_queue--1.10-0.sql"

RESET search_path;
