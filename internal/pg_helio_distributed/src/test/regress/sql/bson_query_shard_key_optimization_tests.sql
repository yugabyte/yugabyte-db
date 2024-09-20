SET search_path TO helio_core,helio_api,helio_api_catalog,helio_api_internal;
SET citus.next_shard_id TO 170000;
SET helio_api.next_collection_id TO 1700;
SET helio_api.next_collection_index_id TO 1700;

SELECT helio_api.drop_collection('db', 'shardkeyopt') IS NOT NULL;
SELECT helio_api.create_collection('db', 'shardkeyopt');


-- create an index on a.b
SELECT helio_api_internal.create_indexes_non_concurrently('db', helio_distributed_test_helpers.generate_create_index_arg('shardkeyopt', 'a_b_1', '{"a.b": 1}'), true);


BEGIN;
-- avoid sequential scan (likely to be preferred on small tables)
set local enable_seqscan TO off;
set local helio_api.forceUseIndexIfAvailable to on;

EXPLAIN (COSTS OFF) SELECT object_id, document FROM helio_api.collection('db', 'shardkeyopt') WHERE document @@ '{ "a.b": { "$eq" : 1 }}';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM helio_api.collection('db', 'shardkeyopt') WHERE document @@ '{ "a.b": { "$ne" : 1 }}';

EXPLAIN (COSTS OFF) SELECT object_id, document FROM helio_api.collection('db', 'shardkeyopt') WHERE document @@ '{ "a.b": { "$ne" : 1 }, "_id": { "$gt": { "$minKey": 1 } }}';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM helio_api.collection('db', 'shardkeyopt') WHERE document @@ '{ "_id": { "$gt": { "$minKey": 1 } }}';
ROLLBACK;

-- shard the collection
SELECT helio_api.shard_collection('db', 'shardkeyopt', '{ "c": "hashed" }', false);


BEGIN;
-- avoid sequential scan (likely to be preferred on small tables)
set local enable_seqscan TO off;
set local helio_api.forceUseIndexIfAvailable to on;

EXPLAIN (COSTS OFF) SELECT object_id, document FROM helio_api.collection('db', 'shardkeyopt') WHERE document @@ '{ "a.b": { "$eq" : 1 }}';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM helio_api.collection('db', 'shardkeyopt') WHERE document @@ '{ "a.b": { "$ne" : 1 }}';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM helio_api.collection('db', 'shardkeyopt') WHERE document @@ '{ "a.b": { "$ne" : 1 }, "_id": { "$gt": { "$minKey": 1 } }}';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM helio_api.collection('db', 'shardkeyopt') WHERE document @@ '{ "_id": { "$gt": { "$minKey": 1 } }}';

EXPLAIN (COSTS OFF) SELECT object_id, document FROM helio_api.collection('db', 'shardkeyopt') WHERE document @@ '{ "a.b": { "$eq" : 1 }, "c": 2 }';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM helio_api.collection('db', 'shardkeyopt') WHERE document @@ '{ "a.b": { "$ne" : 1 }, "c": 2 }';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM helio_api.collection('db', 'shardkeyopt') WHERE document @@ '{ "a.b": { "$ne" : 1 }, "c": 2, "_id": { "$gt": { "$minKey": 1 } }}';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM helio_api.collection('db', 'shardkeyopt') WHERE document @@ '{ "c": 2, "_id": { "$gt": { "$minKey": 1 } }}';
ROLLBACK;
