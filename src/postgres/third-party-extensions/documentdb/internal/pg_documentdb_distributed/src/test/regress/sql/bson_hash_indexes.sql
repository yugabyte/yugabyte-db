
SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog;

SET citus.next_shard_id TO 530000;
SET documentdb.next_collection_id TO 5300;
SET documentdb.next_collection_index_id TO 5300;

-- insert a document
SELECT documentdb_api.create_collection('db', 'queryhashindex');

SELECT documentdb_distributed_test_helpers.drop_primary_key('db','queryhashindex');

-- Create a hash index on the collection.
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "queryhashindex", "indexes": [ { "key" : { "a.b": "hashed" }, "name": "hashIndex" }] }', true);
SELECT * FROM documentdb_distributed_test_helpers.get_collection_indexes('db', 'queryhashindex') ORDER BY collection_id, index_id;

SELECT documentdb_api.list_indexes_cursor_first_page('db','{ "listIndexes": "queryhashindex" }') ORDER BY 1;

-- Explain various hash index scenarios.
BEGIN;
set local enable_seqscan to off;
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryhashindex') WHERE bson_dollar_eq(document, '{ "a.b": 1 }');
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryhashindex') WHERE bson_dollar_in(document, '{ "a.b": [ 1, 2, true ]}'::bson);

-- these should not use the index.
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryhashindex') WHERE bson_dollar_ne(document, '{ "a.b": 1 }');
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryhashindex') WHERE bson_dollar_gt(document, '{ "a.b": 1 }');
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryhashindex') WHERE bson_dollar_gte(document, '{ "a.b": 1 }');
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryhashindex') WHERE bson_dollar_lt(document, '{ "a.b": 1 }');
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryhashindex') WHERE bson_dollar_lte(document, '{ "a.b": 1 }');

-- null can be pushed down.
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryhashindex') WHERE bson_dollar_eq(document, '{ "a.b": null }');
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryhashindex') WHERE bson_dollar_in(document, '{ "a.b": [ 1, 2, null ]}'::bson);

-- now insert some documents and run the queries above.
SELECT documentdb_api.insert_one('db', 'queryhashindex', '{ "a": { "b": 1 } }');
SELECT documentdb_api.insert_one('db', 'queryhashindex', '{ "a": { "b": 2 } }');
SELECT documentdb_api.insert_one('db', 'queryhashindex', '{ "a": { "b": 3 } }');
SELECT documentdb_api.insert_one('db', 'queryhashindex', '{ "a": { "b": "string" } }');
SELECT documentdb_api.insert_one('db', 'queryhashindex', '{ "a": { "b": null } }');
SELECT documentdb_api.insert_one('db', 'queryhashindex', '{ "a": { "b": false } }');
SELECT documentdb_api.insert_one('db', 'queryhashindex', '{ "c": 1, "a": "c only field" }');
SELECT documentdb_api.insert_one('db', 'queryhashindex', '{ "a": { "b": {"$undefined" : true } } }'); -- null should also get undefined values

SELECT document -> 'a' FROM documentdb_api.collection('db', 'queryhashindex') WHERE bson_dollar_eq(document, '{ "a.b": 1 }');
SELECT document -> 'a' FROM documentdb_api.collection('db', 'queryhashindex') WHERE bson_dollar_in(document, '{ "a.b": [ 1, 2, true ]}'::bson);

SELECT document -> 'a' FROM documentdb_api.collection('db', 'queryhashindex') WHERE bson_dollar_eq(document, '{ "a.b": null }');
SELECT document -> 'a' FROM documentdb_api.collection('db', 'queryhashindex') WHERE bson_dollar_in(document, '{ "a.b": [ 1, 2, null ]}'::bson);

ROLLBACK;

