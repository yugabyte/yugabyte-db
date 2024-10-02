
SET search_path TO helio_core,helio_api,helio_api_catalog,helio_api_internal;

SET citus.next_shard_id TO 560000;
SET helio_api.next_collection_id TO 5600;
SET helio_api.next_collection_index_id TO 5600;

-- insert a document
SELECT helio_api.insert_one('db','queryuniqueindex','{"_id":"1", "a": { "b": 1 } }', NULL);

-- Create a unique index on the collection.
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "queryuniqueindex", "indexes": [ { "key" : { "a.b": 1 }, "name": "rumConstraint1", "unique": 1 }] }', true);
SELECT * FROM helio_distributed_test_helpers.get_collection_indexes('db', 'queryuniqueindex') ORDER BY collection_id, index_id;

-- insert a value that doesn't collide with the unique index.
SELECT helio_api.insert_one('db','queryuniqueindex','{"_id":"2", "a": [ { "b": 2 }, { "b" : 3 }]}', NULL);

-- insert a value that has duplicate values that do not collide with other values.
SELECT helio_api.insert_one('db','queryuniqueindex','{"_id":"3", "a": [ { "b": 4 }, { "b" : 4 }]}', NULL);

-- insert a value that has duplicate values that collide wtih other values.
SELECT helio_api.insert_one('db','queryuniqueindex','{"_id":"4", "a": [ { "b": 5 }, { "b" : 3 }]}', NULL);
SELECT helio_api.insert_one('db','queryuniqueindex','{"_id":"5", "a": { "b": [ 5, 3 ] } }', NULL);
SELECT helio_api.insert_one('db','queryuniqueindex','{"_id":"6", "a": { "b": 3 } }', NULL);

-- valid scenarios again.
SELECT helio_api.insert_one('db','queryuniqueindex','{"_id":"4", "a": [ { "b": 5 }, { "b" : 6 }]}', NULL);
SELECT helio_api.insert_one('db','queryuniqueindex','{"_id":"5", "a": { "b": [ 7, 9 ] } }', NULL);
SELECT helio_api.insert_one('db','queryuniqueindex','{"_id":"6", "a": { "b": 8 } }', NULL);
SELECT helio_api.insert_one('db','queryuniqueindex','{"_id":"7", "a": { "b": true } }', NULL);
SELECT helio_api.insert_one('db','queryuniqueindex','{"_id":"8", "a": { "b": "someValue" } }', NULL);

-- we can use the unique index for queries
BEGIN;
set local helio_api.forceUseIndexIfAvailable to on;
set local enable_seqscan TO off;
set local helio_api.forceRumIndexScantoBitmapHeapScan TO OFF;
EXPLAIN (COSTS OFF) SELECT document FROM helio_api.collection('db', 'queryuniqueindex') WHERE document @@ '{ "a.b": { "$gt": 5 } }';
ROLLBACK;

-- insert a document that does not have an a.b (should succeed)
SELECT helio_api.insert_one('db','queryuniqueindex','{"a": { "c": "someValue" } }', NULL);

-- insert another document that does not have an a.b (should fail)
SELECT helio_api.insert_one('db','queryuniqueindex','{"a": { "d": "someValue" } }', NULL);

-- insert another document that has a.b = null (Should fail)
SELECT helio_api.insert_one('db','queryuniqueindex','{"a": { "b": null } }', NULL);

-- insert a document that has constraint failure on _id
SELECT helio_api.insert_one('db','queryuniqueindex','{"_id": "8", "a": { "b": 2055 } }', NULL);

-- drop the unique index.
CALL helio_api.drop_indexes('db', '{"dropIndexes": "queryuniqueindex", "index": ["rumConstraint1"]}');
SELECT * FROM helio_distributed_test_helpers.get_collection_indexes('db', 'queryuniqueindex') ORDER BY collection_id, index_id;

-- now we can violate the unique constraint
SELECT helio_api.insert_one('db','queryuniqueindex','{"_id":"9", "a": { "b": 1 } }', NULL);
SELECT helio_api.insert_one('db','queryuniqueindex','{"_id":"10", "a": { "b": [ 2, 1 ] } }', NULL);

-- create an index when the collection violates unique. Should fail.
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "queryuniqueindex", "indexes": [ { "key" : { "a.b": 1 }, "name": "rumConstraint1", "unique": 1, "sparse": 1 }] }', true);

-- create a unique index with the same name ( should be fine since we dropped it )
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "queryuniqueindex", "indexes": [ { "key" : { "c": 1 }, "name": "rumConstraint1", "unique": 1, "sparse": 1 }] }', true);
SELECT * FROM helio_distributed_test_helpers.get_collection_indexes('db', 'queryuniqueindex') ORDER BY collection_id, index_id;

-- since this is sparse, we can create several documents without "c" on it.
SELECT helio_api.insert_one('db','queryuniqueindex','{"_id":"11", "d": "someValue" }', NULL);

-- insert another document that does not have an c (should succeed)
SELECT helio_api.insert_one('db','queryuniqueindex','{"_id":"12", "e" : true }', NULL);

-- insert another document that has a.b = null (Should succeed)
SELECT helio_api.insert_one('db','queryuniqueindex','{"_id":"13", "c": null }', NULL);

-- however, inserting 'c' again should fail.
SELECT helio_api.insert_one('db','queryuniqueindex','{"_id":"14", "c": null }', NULL);

-- drop the unique index by key.
CALL helio_api.drop_indexes('db', '{"dropIndexes": "queryuniqueindex", "index": {"c": 1} }');
SELECT * FROM helio_distributed_test_helpers.get_collection_indexes('db', 'queryuniqueindex') ORDER BY collection_id, index_id;

-- create unique index fails for wildcard.
SELECT helio_api_internal.create_indexes_non_concurrently('uniquedb', '{"createIndexes": "collection1", "indexes": [{"key": {"f.$**": 1}, "name": "my_idx3", "unique": 1.0}]}', true);
SELECT helio_api_internal.create_indexes_non_concurrently('uniquedb', '{"createIndexes": "collection1", "indexes": [{"key": {"$**": 1}, "wildcardProjection": { "f.g": 0 }, "name": "my_idx3", "unique": 1.0}]}', true);

-- test for sharded
SELECT helio_api.insert_one('db','queryuniqueindexsharded','{"_id":"1", "a": { "b": 1 }, "d": 1 }', NULL);
SELECT helio_api.shard_collection('db', 'queryuniqueindexsharded', '{ "d": "hashed" }', false);

-- Create a unique index on the collection.
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "queryuniqueindexsharded", "indexes": [ { "key" : { "a.b": 1 }, "name": "rumConstraint1", "unique": 1 }] }', true);
SELECT * FROM helio_distributed_test_helpers.get_collection_indexes('db', 'queryuniqueindexsharded') ORDER BY collection_id, index_id;

-- valid scenarios:
SELECT helio_api.insert_one('db','queryuniqueindexsharded','{"_id":"2", "a": { "b": [ 2, 2] }, "d": 1 }', NULL);
SELECT helio_api.insert_one('db','queryuniqueindexsharded','{"_id":"3", "a": { "b": [ 3, 4 ] }, "d": 1 }', NULL);
SELECT helio_api.insert_one('db','queryuniqueindexsharded','{"_id":"4", "a": { "b": 5 }, "d": 1 }', NULL);
SELECT helio_api.insert_one('db','queryuniqueindexsharded','{"_id":"5", "a": { "c": 5 }, "d": 1 }', NULL);

-- now violate unique in shard key "d": 1 
SELECT helio_api.insert_one('db','queryuniqueindexsharded','{"_id":"6", "a": { "b": [ 3, 6 ] }, "d": 1 }', NULL);
SELECT helio_api.insert_one('db','queryuniqueindexsharded','{"_id":"7", "a": { "b": null }, "d": 1 }', NULL);

-- now insert something in a different shard - should not violate unique
SELECT helio_api.insert_one('db','queryuniqueindexsharded','{"_id":"8", "a": { "b": [ 3, 6 ] }, "d": 2 }', NULL);
SELECT helio_api.insert_one('db','queryuniqueindexsharded','{"_id":"9", "a": { "b": null }, "d": 2 }', NULL);

-- still can be used for query.
BEGIN;
set local helio_api.forceUseIndexIfAvailable to on;
set local enable_seqscan TO off;
EXPLAIN (COSTS OFF) SELECT document FROM helio_api.collection('db', 'queryuniqueindexsharded') WHERE document @@ '{ "a.b": { "$gt": 5 } }';
ROLLBACK;