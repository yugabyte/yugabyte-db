SET search_path TO documentdb_api,documentdb_core,documentdb_api_internal;
SET documentdb.next_collection_id TO 5500;
SET documentdb.next_collection_index_id TO 5500;

\d documentdb_api_catalog.documentdb_index_queue;
SELECT documentdb_api.create_indexes_background('db', NULL);
SELECT documentdb_api.create_indexes_background('db', '{}');

