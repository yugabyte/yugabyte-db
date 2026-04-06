/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/postgres/query_catalog.rs
 *
 *-------------------------------------------------------------------------
 */

use serde::Deserialize;

#[derive(Debug, Deserialize, Default, Clone)]
pub struct QueryCatalog {
    // auth.rs
    pub authenticate_with_scram_sha256: String,
    pub salt_and_iterations: String,
    pub authenticate_with_token: String,

    // dataapi.rs (Not needed for OSS)
    pub authenticate_with_pwd: String,
    pub bson_json_to_bson: String,
    pub bson_to_json_string: String,

    // dynamic.rs
    pub pg_settings: String,
    pub pg_is_in_recovery: String,

    // version.rs
    pub extension_versions: String,

    // explain/mod.rs
    pub explain: String, // Has 2 params
    pub set_explain_all_tasks_true: String,
    pub set_explain_all_plans_true: String,
    pub find_coalesce: String,
    pub find_operator: String,
    pub find_bson_text_meta_qual: String,
    pub find_bson_repath_and_build: String,

    // query_diagnostics.rs
    pub bson_dollar_project_output_regex: String,
    pub index_condition_split_regex: String,
    pub runtime_condition_split_regex: String,
    pub sort_condition_split_regex: String,
    pub single_index_condition_regex: String,
    pub api_catalog_name_regex: String,
    pub output_count_regex: String,
    pub output_bson_count_aggregate: String,
    pub output_bson_command_count_aggregate: String,

    // client.rs
    pub set_search_path_and_timeout: String,

    // cursor.rs
    pub cursor_get_more: String,
    pub kill_cursors: String,

    // data_description.rs
    pub create_collection_view: String,
    pub drop_database: String,
    pub drop_collection: String,
    pub set_allow_write: String,
    pub shard_collection: String,
    pub rename_collection: String,
    pub coll_mod: String,
    pub unshard_collection: String,

    // data_management.rs
    pub delete: String,
    pub find_cursor_first_page: String,
    pub insert: String,
    pub aggregate_cursor_first_page: String,
    pub process_update: String,
    pub list_databases: String, // Has 1 param
    pub list_collections: String,
    pub validate: String,
    pub find_and_modify: String,
    pub distinct_query: String,
    pub count_query: String,
    pub coll_stats: String,
    pub db_stats: String,
    pub current_op: String,
    pub get_parameter: String,
    pub compact: String,
    pub kill_op: String,

    // indexing.rs
    pub create_indexes_background: String,
    pub check_build_index_status: String,
    pub re_index: String,
    pub drop_indexes: String,
    pub list_indexes_cursor_first_page: String,

    // user.rs
    pub create_user: String,
    pub drop_user: String,
    pub update_user: String,
    pub users_info: String,
    pub connection_status: String,

    // roles.rs
    pub create_role: String,
    pub update_role: String,
    pub drop_role: String,
    pub roles_info: String,

    // tests
    pub create_db_user: String,

    pub scan_types: Vec<String>,
}

impl QueryCatalog {
    // Auth getters
    pub fn authenticate_with_scram_sha256(&self) -> &str {
        &self.authenticate_with_scram_sha256
    }

    pub fn salt_and_iterations(&self) -> &str {
        &self.salt_and_iterations
    }

    pub fn authenticate_with_token(&self) -> &str {
        &self.authenticate_with_token
    }

    // Dataapi getters
    pub fn authenticate_with_pwd(&self) -> &str {
        &self.authenticate_with_pwd
    }

    pub fn bson_json_to_bson(&self) -> &str {
        &self.bson_json_to_bson
    }

    pub fn bson_to_json_string(&self) -> &str {
        &self.bson_to_json_string
    }

    // Dynamic getters
    pub fn pg_settings(&self) -> &str {
        &self.pg_settings
    }

    pub fn pg_is_in_recovery(&self) -> &str {
        &self.pg_is_in_recovery
    }

    // Topology getter
    pub fn extension_versions(&self) -> &str {
        &self.extension_versions
    }

    // Explain getters
    pub fn explain(&self, analyze: &str, query_base: &str) -> String {
        self.explain
            .replace("{analyze}", analyze)
            .replace("{query_base}", query_base)
    }

    pub fn set_explain_all_tasks_true(&self) -> &str {
        &self.set_explain_all_tasks_true
    }

    pub fn set_explain_all_plans_true(&self) -> &str {
        &self.set_explain_all_plans_true
    }

    pub fn find_coalesce(&self) -> &str {
        &self.find_coalesce
    }

    pub fn find_operator(&self) -> &str {
        &self.find_operator
    }

    pub fn find_bson_text_meta_qual(&self) -> &str {
        &self.find_bson_text_meta_qual
    }

    pub fn find_bson_repath_and_build(&self) -> &str {
        &self.find_bson_repath_and_build
    }

    // query_diagnostics getters
    pub fn bson_dollar_project_output_regex(&self) -> &str {
        &self.bson_dollar_project_output_regex
    }

    pub fn index_condition_split_regex(&self) -> &str {
        &self.index_condition_split_regex
    }

    pub fn runtime_condition_split_regex(&self) -> &str {
        &self.runtime_condition_split_regex
    }

    pub fn sort_condition_split_regex(&self) -> &str {
        &self.sort_condition_split_regex
    }

    pub fn single_index_condition_regex(&self) -> &str {
        &self.single_index_condition_regex
    }

    pub fn api_catalog_name_regex(&self) -> &str {
        &self.api_catalog_name_regex
    }

    pub fn output_count_regex(&self) -> &str {
        &self.output_count_regex
    }

    pub fn output_bson_count_aggregate(&self) -> &str {
        &self.output_bson_count_aggregate
    }

    pub fn output_bson_command_count_aggregate(&self) -> &str {
        &self.output_bson_command_count_aggregate
    }

    // Client getters
    pub fn set_search_path_and_timeout(&self, timeout: &str, transaction_timeout: &str) -> String {
        self.set_search_path_and_timeout
            .replace("{timeout}", timeout)
            .replace("{transaction_timeout}", transaction_timeout)
    }

    // Cursor getters
    pub fn cursor_get_more(&self) -> &str {
        &self.cursor_get_more
    }

    // Delete getters
    pub fn drop_database(&self) -> &str {
        &self.drop_database
    }

    pub fn drop_collection(&self) -> &str {
        &self.drop_collection
    }

    pub fn delete(&self) -> &str {
        &self.delete
    }

    pub fn set_allow_write(&self) -> &str {
        &self.set_allow_write
    }

    // Indexing getters
    pub fn create_indexes_background(&self) -> &str {
        &self.create_indexes_background
    }

    pub fn check_build_index_status(&self) -> &str {
        &self.check_build_index_status
    }

    pub fn re_index(&self) -> &str {
        &self.re_index
    }

    pub fn drop_indexes(&self) -> &str {
        &self.drop_indexes
    }

    pub fn list_indexes_cursor_first_page(&self) -> &str {
        &self.list_indexes_cursor_first_page
    }

    // Process getters
    pub fn find_cursor_first_page(&self) -> &str {
        &self.find_cursor_first_page
    }

    pub fn insert(&self) -> &str {
        &self.insert
    }

    pub fn aggregate_cursor_first_page(&self) -> &str {
        &self.aggregate_cursor_first_page
    }

    pub fn process_update(&self) -> &str {
        &self.process_update
    }

    pub fn list_databases(&self, filter_string: &str) -> String {
        self.list_databases
            .replace("{filter_string}", filter_string)
    }

    pub fn list_collections(&self) -> &str {
        &self.list_collections
    }

    pub fn validate(&self) -> &str {
        &self.validate
    }

    pub fn find_and_modify(&self) -> &str {
        &self.find_and_modify
    }

    pub fn distinct_query(&self) -> &str {
        &self.distinct_query
    }

    pub fn count_query(&self) -> &str {
        &self.count_query
    }

    pub fn create_collection_view(&self) -> &str {
        &self.create_collection_view
    }

    pub fn coll_stats(&self) -> &str {
        &self.coll_stats
    }

    pub fn db_stats(&self) -> &str {
        &self.db_stats
    }

    pub fn shard_collection(&self) -> &str {
        &self.shard_collection
    }

    pub fn rename_collection(&self) -> &str {
        &self.rename_collection
    }

    pub fn current_op(&self) -> &str {
        &self.current_op
    }

    pub fn coll_mod(&self) -> &str {
        &self.coll_mod
    }

    pub fn get_parameter(&self) -> &str {
        &self.get_parameter
    }

    // User getters
    pub fn create_user(&self) -> &str {
        &self.create_user
    }

    pub fn drop_user(&self) -> &str {
        &self.drop_user
    }

    pub fn update_user(&self) -> &str {
        &self.update_user
    }

    pub fn users_info(&self) -> &str {
        &self.users_info
    }

    pub fn connection_status(&self) -> &str {
        &self.connection_status
    }

    pub fn create_role(&self) -> &str {
        &self.create_role
    }

    pub fn update_role(&self) -> &str {
        &self.update_role
    }

    pub fn drop_role(&self) -> &str {
        &self.drop_role
    }

    pub fn roles_info(&self) -> &str {
        &self.roles_info
    }

    pub fn create_db_user(&self, user: &str, pass: &str) -> String {
        self.create_db_user
            .replace("{user}", user)
            .replace("{pass}", pass)
    }

    pub fn scan_types(&self) -> &Vec<String> {
        &self.scan_types
    }

    pub fn unshard_collection(&self) -> &str {
        &self.unshard_collection
    }

    pub fn compact(&self) -> &str {
        &self.compact
    }

    pub fn kill_op(&self) -> &str {
        &self.kill_op
    }

    pub fn kill_cursors(&self) -> &str {
        &self.kill_cursors
    }
}

pub fn create_query_catalog() -> QueryCatalog {
    QueryCatalog {
            // auth.rs
            authenticate_with_scram_sha256: "SELECT documentdb_api_internal.authenticate_with_scram_sha256($1, $2, $3)".to_string(),
            salt_and_iterations: "SELECT documentdb_api_internal.scram_sha256_get_salt_and_iterations($1)".to_string(),
            authenticate_with_token: "SELECT documentdb_api_internal.authenticate_token($1, $2)".to_string(),

            // dynamic.rs
            pg_settings: "SELECT name, setting FROM pg_settings WHERE name LIKE 'documentdb.%' OR name IN ('max_connections', 'default_transaction_read_only')".to_string(),
            pg_is_in_recovery: "SELECT pg_is_in_recovery()".to_string(),

            // explain/mod.rs
            explain: "EXPLAIN (FORMAT JSON, ANALYZE {analyze}, VERBOSE True, BUFFERS {analyze}, TIMING {analyze}) SELECT document FROM documentdb_api_catalog.bson_aggregation_{query_base}($1, $2)".to_string(),
            set_explain_all_plans_true: "SELECT set_config('documentdb.enableExtendedExplainPlans', 'true', true);".to_string(),
            find_coalesce: "COALESCE(documentdb_api_catalog.bson_array_agg".to_string(),
            find_operator: "OPERATOR(documentdb_api_catalog.@#%)".to_string(),
            find_bson_text_meta_qual: "documentdb_api_catalog.bson_text_meta_qual".to_string(),
            find_bson_repath_and_build: "documentdb_api_catalog.bson_repath_and_build".to_string(),

            // query_diagnostics.rs
            bson_dollar_project_output_regex: "(documentdb_api_catalog.)?bson_dollar_([^\\(]+)\\([^,]+, 'BSONHEX([\\w\\d]+)'::documentdb_core.bson".to_string(),
            index_condition_split_regex: "\\(?((\\s+AND\\s+)?(?<expr>\\S+ (OPERATOR\\(\\S+\\)|(@\\S+)) '[^']+'::(documentdb_core.)?bson))+\\)?".to_string(),
            runtime_condition_split_regex: "\\(?((\\s+AND|OR\\s+)?(?<expr>\\S+ (OPERATOR\\(\\S+\\)|(@\\S+)) '[^']+'::(documentdb_core.)?bson))+\\)?".to_string(),
            sort_condition_split_regex: "(documentdb_api_catalog\\.)?bson_orderby\\(([^,]+), 'BSONHEX([\\w\\d]+)'::documentdb_core.bson\\)".to_string(),
            single_index_condition_regex: "(OPERATOR\\()?(documentdb_api_catalog\\.)?(?<operator>@[^\\)\\s]+)\\)?\\s+'BSONHEX(?<queryBson>\\S+)'".to_string(),
            api_catalog_name_regex: "documentdb_api_catalog.".to_string(),
            output_count_regex: "BSONSUM('{ \"\" : { \"$numberInt\" : \"1\" } }'::documentdb_core.bson)".to_string(),
            output_bson_count_aggregate: "bsoncount(1)".to_string(),
            output_bson_command_count_aggregate: "bsoncommandcount(1)".to_string(),

            // cursor.rs
            cursor_get_more: "SELECT cursorPage, continuation FROM documentdb_api.cursor_get_more($1, $2, $3)".to_string(),
            kill_cursors: "SELECT documentdb_api_internal.delete_cursors($1)".to_string(),

            // client.rs
            set_search_path_and_timeout: "-c search_path=documentdb_api_catalog,documentdb_api,public -c statement_timeout={timeout} -c idle_in_transaction_session_timeout={transaction_timeout}".to_string(),

            // data_description.rs
            drop_database: "SELECT documentdb_api.drop_database($1)".to_string(),
            drop_collection: "SELECT documentdb_api.drop_collection($1, $2)".to_string(),
            set_allow_write: "SET LOCAL documentdb.IsPgReadOnlyForDiskFull to false; SET transaction read write".to_string(),
            create_collection_view: "SELECT documentdb_api.create_collection_view($1, $2)".to_string(),
            shard_collection: "SELECT documentdb_api.shard_collection($1, $2, $3, $4)".to_string(),
            rename_collection: "SELECT documentdb_api.rename_collection($1, $2, $3, $4)".to_string(),
            coll_mod: "SELECT documentdb_api.coll_mod($1, $2, $3)".to_string(),
            unshard_collection: "SELECT documentdb_api.unshard_collection($1)".to_string(),

            // data_management.rs
            delete: "SELECT * FROM documentdb_api.delete($1, $2, $3, NULL)".to_string(),
            find_cursor_first_page: "SELECT cursorPage, continuation, persistConnection, cursorId FROM documentdb_api.find_cursor_first_page($1, $2)".to_string(),
            insert: "SELECT * FROM documentdb_api.insert($1, $2, $3, NULL)".to_string(),
            aggregate_cursor_first_page: "SELECT cursorPage, continuation, persistConnection, cursorId FROM documentdb_api.aggregate_cursor_first_page($1, $2)".to_string(),
            process_update: "SELECT * FROM documentdb_api.update($1, $2, $3, NULL)".to_string(),
            list_databases: "WITH r1 AS (SELECT DISTINCT database_name AS name
                                FROM documentdb_api_catalog.collections),
                             r2 AS (SELECT documentdb_core.row_get_bson(r1) AS document FROM r1),
                             r3 AS (SELECT document FROM r2 {filter_string}),
                             r4 AS (SELECT COALESCE(documentdb_api_catalog.bson_array_agg(r3.document, ''), '{ \"\": [] }') AS \"databases\",
                                           1.0::float8                                                                        AS \"ok\"
                                    FROM r3)
                        SELECT documentdb_core.row_get_bson(r4) AS document
                        FROM r4".to_string(),
            list_collections: "SELECT cursorPage, continuation, persistConnection, cursorId FROM documentdb_api.list_collections_cursor_first_page($1, $2)".to_string(),
            validate: "SELECT documentdb_api.validate($1, $2)".to_string(),
            find_and_modify: "SELECT * FROM documentdb_api.find_and_modify($1, $2, NULL)".to_string(),
            distinct_query: "SELECT document FROM documentdb_api.distinct_query($1, $2)".to_string(),
            count_query: "SELECT document FROM documentdb_api.count_query($1, $2)".to_string(),
            coll_stats: "SELECT documentdb_api.coll_stats($1, $2, $3)".to_string(),
            db_stats: "SELECT documentdb_api.db_stats($1, $2, $3)".to_string(),
            current_op: "SELECT documentdb_api.current_op($1, $2, $3)".to_string(),
            get_parameter: "SELECT documentdb_api.get_parameter($1, $2, $3)".to_string(),
            compact: "SELECT documentdb_api.compact($1)".to_string(),
            kill_op: "SELECT documentdb_api.kill_op($1)".to_string(),

            // indexing.rs
            create_indexes_background: "SELECT * FROM documentdb_api.create_indexes_background($1, $2)".to_string(),
            check_build_index_status: "SELECT * FROM documentdb_api_internal.check_build_index_status($1)".to_string(),
            re_index: "CALL documentdb_api.re_index($1, $2)".to_string(),
            drop_indexes: "CALL documentdb_api.drop_indexes($1, $2)".to_string(),
            list_indexes_cursor_first_page: "SELECT cursorPage, continuation, persistConnection, cursorId FROM documentdb_api.list_indexes_cursor_first_page($1, $2)".to_string(),

            // user.rs
            create_user: "SELECT documentdb_api.create_user($1)".to_string(),
            drop_user: "SELECT documentdb_api.drop_user($1)".to_string(),
            update_user: "SELECT documentdb_api.update_user($1)".to_string(),
            users_info: "SELECT documentdb_api.users_info($1)".to_string(),
            connection_status: "SELECT documentdb_api.connection_status($1)".to_string(),

            // roles.rs
            create_role: "SELECT documentdb_api.create_role($1)".to_string(),
            update_role: "SELECT documentdb_api.update_role($1)".to_string(),
            drop_role: "SELECT documentdb_api.drop_role($1)".to_string(),
            roles_info: "SELECT documentdb_api.roles_info($1)".to_string(),

            // tests
            create_db_user: "CREATE ROLE \"{user}\" WITH LOGIN INHERIT PASSWORD '{pass}' IN ROLE documentdb_readonly_role; 
                             GRANT documentdb_admin_role TO {user} WITH ADMIN OPTION".to_string(),

            // scan_types
            scan_types: vec![
                "DocumentDBApiScan".to_string(),
                "DocumentDBApiQueryScan".to_string(),
            ],

            ..Default::default()
        }
}
