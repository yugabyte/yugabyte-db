SET search_path TO documentdb_api,documentdb_api_catalog,documentdb_core;

SET documentdb.next_collection_id TO 1430;
SET documentdb.next_collection_index_id TO 1430;
SET citus.next_shard_id TO 143000;

-- Insert test data for hashed aggregates
SELECT * FROM documentdb_api.insert_one('db', 'hash_aggTest', '{ "_id": 1, "key": 1, "value": "abc" }');
SELECT * FROM documentdb_api.insert_one('db', 'hash_aggTest', '{ "_id": 2, "key": 2, "value": "def" }');

-- Enforce hashed aggregate plans
BEGIN;
SET LOCAL enable_hashagg = on;
SET LOCAL enable_sort = off;
SET LOCAL enable_incremental_sort = off;
SET LOCAL documentdb.defaultcursorfirstpagebatchsize = 1;

-- TEST each aggregate, TODO add more
SELECT cursorPage->'cursor.firstBatch' FROM aggregate_cursor_first_page('db', '{ "aggregate": "hash_aggTest", "pipeline": [ { "$group": { "_id": "$key", "items": { "$push": "$$ROOT" } } } ], "cursor" : {  } }');
ROLLBACK;

-- Check if we are really are doing hash aggregations
BEGIN;
SET LOCAL enable_hashagg = on;
SET LOCAL enable_sort = off;
SET LOCAL enable_incremental_sort = off;
SET LOCAL documentdb.defaultcursorfirstpagebatchsize = 1;

EXPLAIN (COSTS OFF, BUFFERS OFF, ANALYZE ON, TIMING OFF, SUMMARY OFF) SELECT * FROM bson_aggregation_pipeline('db', '{ "aggregate": "hash_aggTest", "pipeline": [ { "$group": { "_id": "$key", "items": { "$push": "$$ROOT" } } } ], "cursor" : {  } }');
ROLLBACK;