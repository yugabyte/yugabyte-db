SET search_path TO documentdb_api_catalog, documentdb_core, public;
SET documentdb.next_collection_id TO 500;
SET documentdb.next_collection_index_id TO 500;

set documentdb_rum.enable_new_bulk_delete to on;
set documentdb_rum.enable_new_bulk_delete_inline_data_pages to on;
\i sql/rum_vacuum_cleanup_tests_core.sql