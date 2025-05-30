SET search_path TO documentdb_api,documentdb_api_catalog,documentdb_core;

SET documentdb.next_collection_id TO 6700;
SET documentdb.next_collection_index_id TO 6700;

\i sql/bson_aggregation_pipeline_tests_facet_group_explain_core.sql