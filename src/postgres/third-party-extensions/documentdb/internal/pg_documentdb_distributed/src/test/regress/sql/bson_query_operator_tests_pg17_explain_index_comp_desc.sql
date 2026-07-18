SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 1200000;
SET documentdb.next_collection_id TO 1200;
SET documentdb.next_collection_index_id TO 1200;

\i sql/bson_query_operator_tests_explain_index_comp_desc.sql