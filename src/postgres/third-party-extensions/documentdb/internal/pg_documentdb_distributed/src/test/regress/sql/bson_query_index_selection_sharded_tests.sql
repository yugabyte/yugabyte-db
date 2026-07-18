
SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 3610000;
SET documentdb.next_collection_id TO 3610;
SET documentdb.next_collection_index_id TO 3610;

-- insert 10K documents
SELECT COUNT (*) FROM ( SELECT documentdb_api.insert_one('db', 'test_index_selection_sharded', FORMAT('{ "a": { "b": %s, "c": %s } }', i, i)::bson) FROM generate_series(1, 10000) i) r1;

SELECT documentdb_distributed_test_helpers.drop_primary_key('db', 'test_index_selection_sharded');

-- create indexes on a.b, and a.c
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "test_index_selection_sharded", "indexes": [ { "name": "a_b_1", "key": { "a.b": 1 } }, { "name": "a_c_1", "key": { "a.c": 1 }}] }', true);

-- Now, do an explain with an OR query that each uses 1 of the indexes.
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'test_index_selection_sharded') WHERE document @@ '{ "$or": [ { "a.b": { "$gt": 500 } }, { "a.c": { "$lt": 10 } } ] }';

EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'test_index_selection_sharded') WHERE document @@ '{ "$and": [ { "a.b": { "$gt": 500 } }, { "a.c": { "$lt": 10 } } ] }';

BEGIN;
set local citus.enable_local_execution to off;
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'test_index_selection_sharded') WHERE document @@ '{ "$or": [ { "a.b": { "$gt": 500 } }, { "a.c": { "$lt": 10 } } ] }';
ROLLBACK;


-- Now shard the collection
SELECT documentdb_api.shard_collection('db', 'test_index_selection_sharded', '{ "_id": "hashed" }', false);

ANALYZE documentdb_data.documents_3610;

-- rerun the query
BEGIN;
set local enable_seqscan to off;
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'test_index_selection_sharded') WHERE document @@ '{ "$or": [ { "a.b": { "$gt": 500 } }, { "a.c": { "$lt": 10 } } ] }';

SET LOCAL documentdb.ForceUseIndexIfAvailable to OFF;
SET LOCAL documentdb_rum.enable_custom_cost_estimate to off;
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'test_index_selection_sharded') WHERE document @@ '{ "$and": [ { "a.b": { "$gt": 500 } }, { "a.c": { "$lt": 10 } } ] }';

ROLLBACK;
