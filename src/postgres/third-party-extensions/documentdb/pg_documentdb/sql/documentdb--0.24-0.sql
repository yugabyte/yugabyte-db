
CREATE SCHEMA documentdb_api;
CREATE SCHEMA documentdb_api_internal;
CREATE SCHEMA documentdb_api_catalog;
CREATE SCHEMA documentdb_data;

/* total sql files: 125 */
/* udfs files: 84 */
/* operators files: 17 */
/* schema files: 21 */
/* rbac files: 3 */

/*
 * Region: Query functions
 */
#include "udfs/query/bson_query_match--0.14-0.sql"
#include "udfs/query/bson_dollar_comparison--0.23-0.sql"
#include "udfs/query/bsonquery_dollar_operators--0.10-0.sql"
#include "udfs/query/bson_dollar_array--0.10-0.sql"
#include "udfs/query/bson_dollar_element_bitwise--0.10-0.sql"
#include "udfs/query/bson_dollar_evaluation--0.19-0.sql"
#include "udfs/query/bson_orderby--0.19-0.sql"
#include "udfs/query/bson_dollar_text--0.24-0.sql"

#include "udfs/projection/bson_projection--0.24-0.sql"
#include "udfs/projection/bson_expression--0.22-0.sql"

#include "udfs/query/bson_dollar_selectivity--0.16-0.sql"
#include "udfs/query/bson_dollar_negation--0.16-0.sql"
#include "udfs/query/bson_value_functions--0.21-0.sql"


/*
 * Region: Query operators
 */
#include "operators/bson_query_operators--0.10-0.sql"
#include "operators/bson_partial_filter_operators--0.10-0.sql"
#include "operators/bsonquery_dollar_operators--0.10-0.sql"
#include "operators/bsonquery_btree_family--0.10-0.sql"


/*
 * Region: geospatial
 * YB: postgis is not supported.
#include "udfs/aggregation/bson_geonear_functions--0.18-0.sql"
#include "udfs/query/bson_dollar_geospatial--0.10-0.sql"
#include "operators/bson_geospatial_operators--0.16-0.sql"
#include "operators/bson_geospatial_operators--0.18-0.sql"
#include "udfs/geospatial/bson_gist_extensibility_functions--0.16-0.sql"
#include "operators/bson_gist_geospatial_op_classes--0.16-0.sql"
#include "operators/bson_gist_geospatial_op_classes_members--0.16-0.sql"
#include "operators/bson_gist_geospatial_op_classes_members--0.18-0.sql"
 */

/*
 * Region: Shard key and document
 */
#include "schema/shard_key_and_document--0.10-0.sql"
#include "udfs/rum/bson_preconsistent--0.11-0.sql"
#include "udfs/rum/bson_rum_exclusion_functions--0.23-0.sql"
#include "operators/shard_key_and_document_operators--0.23-0.sql"


/*
 * Region: RUM operators and functions
 */
#include "udfs/rum/bson_rum_shard_exclusion_functions--0.24-0.sql"
#include "operators/bson_unique_shard_path_operators--0.24-0.sql"


#include "operators/bson_path_operators--0.10-0.sql"
#include "operators/bson_path_operators--0.23-0.sql"
#include "operators/bson_path_operators--0.24-0.sql"
#include "operators/bson_dollar_operators--0.16-0.sql"
#include "operators/bson_dollar_negation_operators--0.16-0.sql"

/*
 * YB: rum is not supported.
#include "udfs/rum/handler--0.10-0.sql"
#include "schema/rum_access_method--0.10-0.sql"
#include "schema/unique_shard_path_operator_class--0.24-0.sql"
#include "schema/bson_rum_exclusion_operator_class--0.23-0.sql"
#include "operators/bson_dollar_text_operators--0.24-0.sql"
#include "udfs/rum/bson_rum_text_path_funcs--0.24-0.sql"
#include "schema/bson_rum_text_path_ops--0.24-0.sql"
#include "udfs/rum/extensibility_functions--0.10-0.sql"
#include "udfs/rum/single_path_extensibility_functions--0.10-0.sql"
#include "schema/single_path_operator_class--0.10-0.sql"
#include "udfs/rum/wildcard_project_path_extensibility_functions--0.12-0.sql"
#include "schema/wildcard_project_path_operator_class--0.12-0.sql"

#include "udfs/rum/bson_rum_hashed_ops_functions--0.23-0.sql"
#include "schema/bson_hash_operator_class--0.23-0.sql"

#include "schema/index_operator_classes_negation--0.16-0.sql"
#include "schema/index_operator_classes_range--0.23-0.sql"
 */

/*
 * Region: Aggregation operators.
 */
#include "udfs/aggregation/bson_aggregation_support--0.10-0.sql"
#include "udfs/aggregation/bson_aggregation_pipeline--0.10-0.sql"
#include "udfs/aggregation/bson_aggregation_find--0.10-0.sql"
#include "udfs/aggregation/bson_aggregation_count--0.10-0.sql"
#include "udfs/aggregation/bson_aggregation_distinct--0.10-0.sql"

#include "udfs/aggregation/window_aggregate_support--0.22-0.sql"
#include "udfs/aggregation/window_aggregates--0.22-0.sql"

#include "udfs/aggregation/group_aggregates_support--0.24-0.sql"
#include "udfs/aggregation/group_aggregates--0.24-0.sql"

#include "udfs/aggregation/bson_unwind_functions--0.10-0.sql"
#include "udfs/aggregation/bson_lookup_functions--0.23-0.sql"
#include "udfs/aggregation/distinct_aggregates--0.10-0.sql"

#include "udfs/aggregation/bson_coercion_compat--0.24-0.sql"
#include "udfs/aggregation/bson_densify_functions--0.22-0.sql"
#include "udfs/aggregation/bson_inverse_match--0.16-0.sql"
#include "udfs/aggregation/bson_merge_functions--0.20-0.sql"

#include "udfs/metadata/empty_data_table--0.16-0.sql"
#include "udfs/metadata/collection--0.10-0.sql"

#include "udfs/aggregation/bson_aggregation_redact--0.24-0.sql"

/*
 * Region: Collection Metadata
 */
#include "udfs/metadata/collection_metadata_functions--0.23-0.sql"
#include "udfs/metadata/collection_triggers--0.21-0.sql"
#include "schema/collection_metadata--0.10-0.sql"
#include "schema/collection_metadata--0.21-0.sql"
#include "schema/collection_metadata--0.23-0.sql"
#include "schema/collection_metadata_views--0.10-0.sql"
#include "schema/collection_metadata_schemavalidation--0.10-0.sql"

#include "udfs/metadata/collection_indexes_metadata_functions--0.10-0.sql"
#include "schema/collection_indexes_metadata--0.10-0.sql"
#include "schema/collection_indexes_metadata--0.23-0.sql"

/*
 * Region: Sharding Metadata
*/
#include "udfs/metadata/sharding_metadata_functions--0.10-0.sql"

/*
 * Region: Schema Management APIs
 */
#include "udfs/schema_mgmt/create_collection--0.12-0.sql"
#include "udfs/schema_mgmt/shard_collection--0.24-0.sql"
#include "udfs/schema_mgmt/coll_mod--0.12-0.sql"
#include "udfs/schema_mgmt/create_collection_view--0.12-0.sql"
#include "udfs/schema_mgmt/drop_collection--0.12-0.sql"
#include "udfs/schema_mgmt/drop_database--0.13-0.sql"
#include "udfs/schema_mgmt/rename_collection--0.15-0.sql"


/*
 * Region: Index Manangement APIs
 */
#include "udfs/index_mgmt/create_indexes_non_concurrently--0.21-0.sql"
#include "udfs/index_mgmt/create_builtin_id_index--0.10-0.sql"
#include "udfs/index_mgmt/record_id_index--0.10-0.sql"
#include "udfs/index_mgmt/index_build_is_in_progress--0.10-0.sql"
#include "udfs/index_mgmt/index_spec_as_bson--0.10-0.sql"
#include "udfs/index_mgmt/index_spec_options_are_equivalent--0.10-0.sql"
#include "udfs/index_mgmt/create_index_background--0.23-0.sql"
#include "udfs/index_mgmt/drop_indexes--0.12-0.sql"


/*
 * Region: Basic CRUD APIs & Commands
 */
#include "udfs/commands_crud/insert--0.16-0.sql"
#include "udfs/commands_crud/insert_one_helper--0.12-0.sql"

#include "udfs/commands_crud/bson_update_document--0.10-0.sql"
#include "udfs/commands_crud/update--0.16-0.sql"

#include "udfs/commands_crud/delete--0.16-0.sql"
#include "udfs/commands_crud/find_and_modify--0.12-0.sql"

#include "udfs/commands_crud/query_cursors_aggregate--0.12-0.sql"
#include "udfs/commands_crud/query_cursors_single_page--0.10-0.sql"

#include "udfs/commands_crud/cursor_functions--0.16-0.sql"


/*
 * Region: user management APIs
 */
#include "udfs/users/create_user--0.19-0.sql"
#include "udfs/users/drop_user--0.19-0.sql"
#include "udfs/users/update_user--0.19-0.sql"
#include "udfs/users/users_info--0.19-0.sql"

/*
 * Region: schema validation APIs
 */
#include "udfs/schema_validation/schema_validation--0.24-0.sql"


/*
 * Region: Telemetry APIs
 */
#include "udfs/telemetry/command_feature_counter--0.24-0.sql"


/*
 * Region: TTL APIs
 */
#include "udfs/ttl/ttl_support_functions--0.24-0.sql"

/*
 * Region: vector search APIs
 */
#include "udfs/vector/bson_extract_vector--0.24-0.sql"


/*
 * Region: Extension Version APIs
 */
#include "udfs/utils/extension_version_utils--0.11-0.sql"


/*
 * Region: Diagnostic APIs
 */
#include "udfs/commands_diagnostic/coll_stats--0.16-0.sql"
#include "udfs/commands_diagnostic/db_stats--0.24-0.sql"
#include "udfs/commands_diagnostic/index_stats--0.24-0.sql"
#include "udfs/commands_diagnostic/validate--0.24-0.sql"

/*
 * Region: RBAC Metadata
 */
#include "rbac/extension_admin_setup--0.10-0.sql"
#include "rbac/extension_admin_setup--0.16-0.sql"
#include "rbac/extension_readonly_setup--0.17-1.sql"

/*
 * Region: Background Index Schema
 */
#include "schema/background_index_queue--0.11-0.sql"
#include "schema/background_index_queue--0.14-0.sql"
