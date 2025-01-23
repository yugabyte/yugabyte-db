SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 170000;
SET documentdb.next_collection_id TO 1700;
SET documentdb.next_collection_index_id TO 1700;

SELECT documentdb_api.drop_collection('db', 'shardkeyopt') IS NOT NULL;
SELECT documentdb_api.create_collection('db', 'shardkeyopt');


-- create an index on a.b
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', documentdb_distributed_test_helpers.generate_create_index_arg('shardkeyopt', 'a_b_1', '{"a.b": 1}'), true);


BEGIN;
-- avoid sequential scan (likely to be preferred on small tables)
set local enable_seqscan TO off;
set local documentdb.forceUseIndexIfAvailable to on;

EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'shardkeyopt') WHERE document @@ '{ "a.b": { "$eq" : 1 }}';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'shardkeyopt') WHERE document @@ '{ "a.b": { "$ne" : 1 }}';

EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'shardkeyopt') WHERE document @@ '{ "a.b": { "$ne" : 1 }, "_id": { "$gt": { "$minKey": 1 } }}';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'shardkeyopt') WHERE document @@ '{ "_id": { "$gt": { "$minKey": 1 } }}';
ROLLBACK;

-- shard the collection
SELECT documentdb_api.shard_collection('db', 'shardkeyopt', '{ "c": "hashed" }', false);


BEGIN;
-- avoid sequential scan (likely to be preferred on small tables)
set local enable_seqscan TO off;
set local documentdb.forceUseIndexIfAvailable to on;

EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'shardkeyopt') WHERE document @@ '{ "a.b": { "$eq" : 1 }}';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'shardkeyopt') WHERE document @@ '{ "a.b": { "$ne" : 1 }}';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'shardkeyopt') WHERE document @@ '{ "a.b": { "$ne" : 1 }, "_id": { "$gt": { "$minKey": 1 } }}';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'shardkeyopt') WHERE document @@ '{ "_id": { "$gt": { "$minKey": 1 } }}';

EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'shardkeyopt') WHERE document @@ '{ "a.b": { "$eq" : 1 }, "c": 2 }';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'shardkeyopt') WHERE document @@ '{ "a.b": { "$ne" : 1 }, "c": 2 }';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'shardkeyopt') WHERE document @@ '{ "a.b": { "$ne" : 1 }, "c": 2, "_id": { "$gt": { "$minKey": 1 } }}';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'shardkeyopt') WHERE document @@ '{ "c": 2, "_id": { "$gt": { "$minKey": 1 } }}';
ROLLBACK;
