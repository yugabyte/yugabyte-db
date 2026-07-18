SET search_path TO documentdb_api_catalog, documentdb_core, public;
SET documentdb.next_collection_id TO 400;
SET documentdb.next_collection_index_id TO 400;

\i sql/rum_vacuum_cleanup_tests_core.sql