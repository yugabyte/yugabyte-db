
CREATE SCHEMA helio_api;
CREATE SCHEMA helio_api_internal;
CREATE SCHEMA helio_api_catalog;
CREATE SCHEMA helio_data;

SET search_path TO helio_api;

/*
 * Region: Query functions
 */
 #include "udfs/query/bson_query_match--0.10-0.sql"

#include "udfs/query/bson_dollar_comparison--0.10-0.sql"
#include "udfs/query/bson_dollar_array--0.10-0.sql"
#include "udfs/query/bson_dollar_element_bitwise--0.10-0.sql"
#include "udfs/query/bson_dollar_geospatial--0.10-0.sql"
#include "udfs/query/bson_dollar_evaluation--0.10-0.sql"

#include "udfs/query/bson_orderby--0.10-0.sql"

#include "udfs/projection/bson_projection--0.10-0.sql"
#include "udfs/projection/bson_expression--0.10-0.sql"


#include "udfs/query/bsonquery_dollar_operators--0.10-0.sql"

/*
 * Region: Query operators
 */
#include "operators/bson_query_operators--0.10-0.sql"
#include "operators/bson_partial_filter_operators--0.10-0.sql"
#include "operators/bsonquery_dollar_operators--0.10-0.sql"

#include "operators/bsonquery_btree_family--0.10-0.sql"

/*
 * Region: Shard key and document
 */
 #include "schema/shard_key_and_document--0.10-0.sql"

/*
 * Region: RUM operators and functions
 */
 #include "udfs/rum/handler--0.10-0.sql"
 #include "udfs/rum/extensibility_functions--0.10-0.sql"
 #include "udfs/rum/single_path_extensibility_functions--0.10-0.sql"
 #include "udfs/rum/bson_preconsistent--0.10-0.sql"
 #include "operators/bson_path_operators--0.10-0.sql"

 /*
 * Region: RUM metadata
 */
 #include "schema/rum_access_method--0.10-0.sql"
 #include "schema/single_path_operator_class--0.10-0.sql"

 -- TODO: Re-enable this for Helio when ready
 -- #include "schema/index_operator_classes_preconsistent--0.10-0.sql"

/*
 * Region: Aggregation operators.
 */
#include "udfs/aggregation/bson_aggregation_support--0.10-0.sql"
#include "udfs/aggregation/bson_aggregation_pipeline--0.10-0.sql"
#include "udfs/aggregation/bson_aggregation_find--0.10-0.sql"
#include "udfs/aggregation/bson_aggregation_count--0.10-0.sql"
#include "udfs/aggregation/bson_aggregation_distinct--0.10-0.sql"

#include "udfs/aggregation/group_aggregates_support--0.10-0.sql"
#include "udfs/aggregation/group_aggregates--0.10-0.sql"
#include "udfs/aggregation/bson_unwind_functions--0.10-0.sql"
#include "udfs/aggregation/bson_lookup_functions--0.10-0.sql"
#include "udfs/aggregation/distinct_aggregates--0.10-0.sql"

#include "udfs/metadata/empty_data_table--0.10-0.sql"
#include "udfs/metadata/collection--0.10-0.sql"


/*
 * Region: Collection Metadata
 */
#include "udfs/metadata/collection_metadata_functions--0.10-0.sql"
#include "udfs/metadata/collection_indexes_metadata_functions--0.10-0.sql"

#include "schema/collection_metadata--0.10-0.sql"
#include "schema/collection_metadata_views--0.10-0.sql"
#include "schema/collection_metadata_schemavalidation--0.10-0.sql"

#include "schema/collection_indexes_metadata--0.10-0.sql"

/*
 * Region: Sharding Metadata
*/
#include "udfs/metadata/sharding_metadata_functions--0.10-0.sql"

/*
 * Region: Schema Management APIs
 */
 #include "udfs/schema_mgmt/create_collection--0.10-0.sql"
 #include "udfs/schema_mgmt/shard_collection--0.10-0.sql"

/*
 * Region: Index Manangement APIs
 */
 #include "udfs/index_mgmt/create_indexes_non_concurrently--0.10-0.sql"
 #include "udfs/index_mgmt/create_builtin_id_index--0.10-0.sql"
 #include "udfs/index_mgmt/record_id_index--0.10-0.sql"
 #include "udfs/index_mgmt/index_build_is_in_progress--0.10-0.sql"
 #include "udfs/index_mgmt/index_spec_as_bson--0.10-0.sql"
 #include "udfs/index_mgmt/index_spec_options_are_equivalent--0.10-0.sql"

/*
 * Region: Basic CRUD APIs & Commands
 */
#include "udfs/commands_crud/insert--0.10-0.sql"
#include "udfs/commands_crud/insert_one_helper--0.10-0.sql"

#include "udfs/commands_crud/bson_update_document--0.10-0.sql"
#include "udfs/commands_crud/update--0.10-0.sql"

#include "udfs/commands_crud/delete--0.10-0.sql"
#include "udfs/commands_crud/find_and_modify--0.10-0.sql"

#include "udfs/commands_crud/query_cursors_aggregate--0.10-0.sql"
#include "udfs/commands_crud/query_cursors_single_page--0.10-0.sql"

/*
 * Region: RBAC Metadata
 */
 #include "rbac/extension_admin_setup--0.10-0.sql"

/*
 * Region: Background Index Schema
 */
 #include "schema/background_index_queue--0.10-0.sql"

RESET search_path;
