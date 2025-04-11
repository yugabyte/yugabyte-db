
SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 251000;
SET documentdb.next_collection_id TO 2510;
SET documentdb.next_collection_index_id TO 2510;


SELECT COUNT(*) FROM (SELECT documentdb_api.insert_one('db', 'test_object_id_index', FORMAT('{ "_id": %s, "a": %s, "otherField": "aaaa" }', g, g)::bson) FROM generate_series(1, 10000) g) i;

EXPLAIN (COSTS ON) SELECT document FROM documentdb_api.collection('db', 'test_object_id_index') WHERE document @@ '{ "_id": 15 }';
EXPLAIN (COSTS ON) SELECT document FROM documentdb_api.collection('db', 'test_object_id_index') WHERE document @@ '{ "_id": { "$in": [ 15, 55, 90 ] } }';
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'test_object_id_index') WHERE document @@ '{ "_id": { "$gt": 50 } }';
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'test_object_id_index') WHERE document @@ '{ "_id": { "$gt": 50, "$lt": 60 } }';
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'test_object_id_index') WHERE document @@ '{ "_id": { "$gte": 50, "$lte": 60 } }';

EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'test_object_id_index') WHERE document @@ '{ "$and": [ {"_id": 15 }, { "_id": 16 } ] }';
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'test_object_id_index') WHERE document @@ '{ "$and": [ {"_id": { "$in": [ 15, 16, 17] }}, { "_id": { "$in": [ 16, 17, 18 ] } } ] }';
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'test_object_id_index') WHERE document @@ '{ "$and": [ {"_id": { "$gt": 50 } }, { "_id": { "$lt": 60 } } ] }';

-- create a scenario where there's an alternate filter and that can be matched in the RUM index.
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "test_object_id_index", "indexes": [ { "key": { "otherField": 1 }, "name": "idx_1" } ]}', true);

EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'test_object_id_index') WHERE document @@ '{ "_id": { "$in": [ 15, 20 ] }, "otherField": "aaaa" }';
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'test_object_id_index') WHERE document @@ '{ "_id": { "$in": [ 15 ] }, "otherField": "aaaa" }';

-- now shard the collection
SELECT documentdb_api.shard_collection('db', 'test_object_id_index', '{ "a": "hashed" }', false);

-- we shouldn't have object_id filters unless we also have shard key filters
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'test_object_id_index') WHERE document @@ '{ "_id": 15 }';

BEGIN;
SET LOCAL documentdb.ForceUseIndexIfAvailable to OFF;
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'test_object_id_index') WHERE document @@ '{ "_id": { "$in": [ 15, 55, 90 ] } }';
ROLLBACK;

EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'test_object_id_index') WHERE document @@ '{ "_id": 15, "a": 15 }';
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'test_object_id_index') WHERE document @@ '{ "_id": 15, "a": { "$gt": 15 } }';

EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'test_object_id_index') WHERE document @@ '{ "_id": { "$in": [ 15, 20 ] }, "otherField": "aaaa" }';
