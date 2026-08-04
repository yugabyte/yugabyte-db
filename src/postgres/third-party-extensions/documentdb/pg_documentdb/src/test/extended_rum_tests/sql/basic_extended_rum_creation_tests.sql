SET search_path TO documentdb_api,documentdb_core,documentdb_api_catalog;

SET documentdb.next_collection_id TO 100;
SET documentdb.next_collection_index_id TO 100;

-- create a collection
SELECT documentdb_api.insert_one('exrumdb','index_creation_tests', '{"_id": 1, "a": "hello world"}');

-- create an index
SELECT documentdb_api_internal.create_indexes_non_concurrently('exrumdb', '{ "createIndexes": "index_creation_tests", "indexes": [ { "key": { "a": 1 }, "name": "a_1" } ] }', TRUE);

-- create a composite index
SELECT documentdb_api_internal.create_indexes_non_concurrently('exrumdb', '{ "createIndexes": "index_creation_tests", "indexes": [ { "key": { "a": 1, "b": -1 }, "name": "a_1_b_-1" } ] }', TRUE);

-- create a unique index
SELECT documentdb_api_internal.create_indexes_non_concurrently('exrumdb', '{ "createIndexes": "index_creation_tests", "indexes": [ { "key": { "c": 1 }, "name": "c_1", "unique": true } ] }', TRUE);

-- validate they're all ordered indexes and using the appropriate index handler
\d documentdb_data.documents_101

-- validate they're used in queries
set enable_seqscan to off;
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('exrumdb', '{ "find": "index_creation_tests", "filter": { "a": "hello world" } }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('exrumdb', '{ "find": "index_creation_tests", "filter": { "c": "hello world" } }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('exrumdb', '{ "find": "index_creation_tests", "filter": { "a": "hello world", "b": "myfoo" } }');