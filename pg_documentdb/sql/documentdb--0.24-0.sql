
CREATE SCHEMA documentdb_api;
CREATE SCHEMA documentdb_api_internal;
CREATE SCHEMA documentdb_api_catalog;
CREATE SCHEMA documentdb_data;

/* TODO(OSSRename): the documentdb planner requires the following functions to work correctly. will be removed after udfs migration */
CREATE OR REPLACE FUNCTION documentdb_api_internal.cursor_state(documentdb_core.bson, documentdb_core.bson)
 RETURNS bool
 LANGUAGE c
 IMMUTABLE STRICT
AS 'MODULE_PATHNAME', $function$command_cursor_state$function$;


/*
 * Region: Query functions
 */
//#include "udfs/query/bson_query_match--1.10-0.sql"
//
//#include "udfs/query/bson_dollar_comparison--1.10-0.sql"
//#include "udfs/query/bson_dollar_array--1.10-0.sql"
//#include "udfs/query/bson_dollar_element_bitwise--1.10-0.sql"
//#include "udfs/query/bson_dollar_geospatial--1.10-0.sql"
//#include "udfs/query/bson_dollar_evaluation--1.10-0.sql"

//#include "udfs/query/bson_orderby--1.10-0.sql"

//#include "udfs/projection/bson_projection--1.10-0.sql"
//#include "udfs/projection/bson_expression--1.10-0.sql"


//#include "udfs/query/bsonquery_dollar_operators--1.10-0.sql"

/*
 * Region: Query operators
 */
//#include "operators/bson_query_operators--0.10-0.sql"
//#include "operators/bson_partial_filter_operators--0.10-0.sql"
//#include "operators/bsonquery_dollar_operators--0.10-0.sql"
//
//#include "operators/bsonquery_btree_family--0.10-0.sql"

/*
 * Region: Shard key and document
 */
//#include "schema/shard_key_and_document--0.10-0.sql"

/*
 * Region: RUM operators and functions
 */
//#include "operators/bson_path_operators--0.23-0.sql"
//#include "schema/bson_rum_exclusion_operator_class--0.23-0.sql"
//#include "schema/bson_rum_text_path_ops--0.24-0.sql"
// #include "udfs/rum/handler--1.10-0.sql"
// #include "udfs/rum/extensibility_functions--1.10-0.sql"
// #include "udfs/rum/single_path_extensibility_functions--1.10-0.sql"
// #include "udfs/rum/bson_preconsistent--1.10-0.sql"


/*
* Region: RUM metadata
*/
//#include "schema/rum_access_method--0.10-0.sql"
//#include "schema/single_path_operator_class--0.10-0.sql"

-- TODO: Re-enable this for Helio when ready
-- #include "pg_documentdb/sql/schema/index_operator_classes_preconsistent--0.10-0.sql"

/*
 * Region: Aggregation operators.
 */
//#include "pg_documentdb/sql/udfs/aggregation/bson_aggregation_support--1.10-0.sql"
//#include "pg_documentdb/sql/udfs/aggregation/bson_aggregation_pipeline--1.10-0.sql"
//#include "pg_documentdb/sql/udfs/aggregation/bson_aggregation_find--1.10-0.sql"
//#include "pg_documentdb/sql/udfs/aggregation/bson_aggregation_count--1.10-0.sql"
//#include "pg_documentdb/sql/udfs/aggregation/bson_aggregation_distinct--1.10-0.sql"
//
//#include "udfs/aggregation/group_aggregates_support--1.10-0.sql"
//#include "udfs/aggregation/group_aggregates--1.10-0.sql"
//#include "udfs/aggregation/bson_unwind_functions--1.10-0.sql"
//#include "udfs/aggregation/bson_lookup_functions--1.10-0.sql"
//#include "udfs/aggregation/distinct_aggregates--1.10-0.sql"
//
//#include "udfs/metadata/empty_data_table--1.10-0.sql"
//#include "udfs/metadata/collection--1.10-0.sql"


/*
 * Region: Collection Metadata
 */
//#include "udfs/metadata/collection_metadata_functions--1.10-0.sql"
//#include "udfs/metadata/collection_indexes_metadata_functions--1.10-0.sql"

//#include "schema/collection_metadata--0.23-0.sql"
//#include "schema/collection_metadata_views--0.10-0.sql"
//#include "schema/collection_metadata_schemavalidation--0.10-0.sql"
//
//#include "schema/collection_indexes_metadata--0.23-0.sql"

/*
 * Region: Sharding Metadata
*/
//#include "udfs/metadata/sharding_metadata_functions--1.10-0.sql"

/*
 * Region: Schema Management APIs
 */
// #include "udfs/schema_mgmt/create_collection--1.10-0.sql"
// #include "udfs/schema_mgmt/shard_collection--1.10-0.sql"

/*
 * Region: Index Manangement APIs
 */
// #include "udfs/index_mgmt/create_indexes_non_concurrently--1.10-0.sql"
// #include "udfs/index_mgmt/create_builtin_id_index--1.10-0.sql"
// #include "udfs/index_mgmt/record_id_index--1.10-0.sql"
// #include "udfs/index_mgmt/index_build_is_in_progress--1.10-0.sql"
// #include "udfs/index_mgmt/index_spec_as_bson--1.10-0.sql"
// #include "udfs/index_mgmt/index_spec_options_are_equivalent--1.10-0.sql"

/*
 * Region: Basic CRUD APIs & Commands
 */
//#include "udfs/commands_crud/insert--1.10-0.sql"
//#include "udfs/commands_crud/insert_one_helper--1.10-0.sql"

//#include "udfs/commands_crud/bson_update_document--1.10-0.sql"
//#include "udfs/commands_crud/update--1.10-0.sql"

//#include "udfs/commands_crud/delete--1.10-0.sql"
//#include "udfs/commands_crud/find_and_modify--1.10-0.sql"

//#include "udfs/commands_crud/query_cursors_aggregate--1.10-0.sql"
//#include "udfs/commands_crud/query_cursors_single_page--1.10-0.sql"

/*
 * Region: RBAC Metadata
 */
//#include "rbac/extension_admin_setup--0.16-0.sql"
//#include "rbac/extension_readonly_setup--0.17-1.sql"

/*
 * Region: Background Index Schema
 */
//#include "schema/background_index_queue--0.14-0.sql"

//#include "schema/wildcard_project_path_operator_class--0.12-0.sql"
//#include "operators/bson_geospatial_operators--0.18-0.sql"
//#include "operators/bson_gist_geospatial_op_classes--0.16-0.sql"
//#include "operators/bson_gist_geospatial_op_classes_members--0.18-0.sql"
//
//#include "operators/bson_dollar_operators--0.16-0.sql"
//#include "operators/bson_dollar_negation_operators--0.16-0.sql"
//#include "schema/index_operator_classes_negation--0.16-0.sql"
//
//#include "schema/index_operator_classes_range--0.23-0.sql"
//#include "operators/shard_key_and_document_operators--0.23-0.sql"
//
//#include "schema/bson_hash_operator_class--0.23-0.sql"
//#include "operators/bson_dollar_text_operators--0.24-0.sql"



