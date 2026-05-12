
SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;

SET citus.next_shard_id TO 562000;
SET documentdb.next_collection_id TO 5620;
SET documentdb.next_collection_index_id TO 5620;
SET documentdb.enable_large_unique_index_keys TO false;

-- insert a document
SELECT documentdb_api.insert_one('db','queryuniquecomposite','{"_id":"1", "a": { "b": 1 } }', NULL);

-- Create a unique index on the collection.
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "queryuniquecomposite", "indexes": [ { "key" : { "a.b": 1 }, "name": "rumConstraint1", "unique": 1, "enableCompositeTerm": true }] }', true);
SELECT * FROM documentdb_distributed_test_helpers.get_collection_indexes('db', 'queryuniquecomposite') ORDER BY collection_id, index_id;

-- insert a value that doesn't collide with the unique index.
SELECT documentdb_api.insert_one('db','queryuniquecomposite','{"_id":"2", "a": [ { "b": 2 }, { "b" : 3 }]}', NULL);

-- insert a value that has duplicate values that do not collide with other values.
SELECT documentdb_api.insert_one('db','queryuniquecomposite','{"_id":"3", "a": [ { "b": 4 }, { "b" : 4 }]}', NULL);

-- insert a value that has duplicate values that collide wtih other values.
SELECT documentdb_api.insert_one('db','queryuniquecomposite','{"_id":"4", "a": [ { "b": 5 }, { "b" : 3 }]}', NULL);
SELECT documentdb_api.insert_one('db','queryuniquecomposite','{"_id":"5", "a": { "b": [ 5, 3 ] } }', NULL);
SELECT documentdb_api.insert_one('db','queryuniquecomposite','{"_id":"6", "a": { "b": 3 } }', NULL);

-- valid scenarios again.
SELECT documentdb_api.insert_one('db','queryuniquecomposite','{"_id":"4", "a": [ { "b": 5 }, { "b" : 6 }]}', NULL);
SELECT documentdb_api.insert_one('db','queryuniquecomposite','{"_id":"5", "a": { "b": [ 7, 9 ] } }', NULL);
SELECT documentdb_api.insert_one('db','queryuniquecomposite','{"_id":"6", "a": { "b": 8 } }', NULL);
SELECT documentdb_api.insert_one('db','queryuniquecomposite','{"_id":"7", "a": { "b": true } }', NULL);
SELECT documentdb_api.insert_one('db','queryuniquecomposite','{"_id":"8", "a": { "b": "someValue" } }', NULL);

-- we can use the unique index for queries
BEGIN;
set local documentdb.forceUseIndexIfAvailable to on;
set local enable_seqscan TO off;
set local documentdb.forceRumIndexScantoBitmapHeapScan TO OFF;
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'queryuniquecomposite') WHERE document @@ '{ "a.b": { "$gt": 5 } }';
ROLLBACK;

-- insert a document that does not have an a.b (should succeed)
SELECT documentdb_api.insert_one('db','queryuniquecomposite','{"a": { "c": "someValue" } }', NULL);

-- insert another document that does not have an a.b (should fail)
SELECT documentdb_api.insert_one('db','queryuniquecomposite','{"a": { "d": "someValue" } }', NULL);

-- insert another document that has a.b = null (Should fail)
SELECT documentdb_api.insert_one('db','queryuniquecomposite','{"a": { "b": null } }', NULL);

-- insert a document that has constraint failure on _id
SELECT documentdb_api.insert_one('db','queryuniquecomposite','{"_id": "8", "a": { "b": 2055 } }', NULL);

-- drop the unique index.
CALL documentdb_api.drop_indexes('db', '{"dropIndexes": "queryuniquecomposite", "index": ["rumConstraint1"]}');
SELECT * FROM documentdb_distributed_test_helpers.get_collection_indexes('db', 'queryuniquecomposite') ORDER BY collection_id, index_id;

-- now we can violate the unique constraint
SELECT documentdb_api.insert_one('db','queryuniquecomposite','{"_id":"9", "a": { "b": 1 } }', NULL);
SELECT documentdb_api.insert_one('db','queryuniquecomposite','{"_id":"10", "a": { "b": [ 2, 1 ] } }', NULL);

-- create an index when the collection violates unique. Should fail.
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "queryuniquecomposite", "indexes": [ { "key" : { "a.b": 1 }, "name": "rumConstraint1", "unique": 1, "sparse": 1, "enableCompositeTerm": true }] }', true);

-- create a unique index with the same name ( should be fine since we dropped it )
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "queryuniquecomposite", "indexes": [ { "key" : { "c": 1 }, "name": "rumConstraint1", "unique": 1, "sparse": 1 , "enableCompositeTerm": true}] }', true);
SELECT * FROM documentdb_distributed_test_helpers.get_collection_indexes('db', 'queryuniquecomposite') ORDER BY collection_id, index_id;

-- since this is sparse, we can create several documents without "c" on it.
SELECT documentdb_api.insert_one('db','queryuniquecomposite','{"_id":"11", "d": "someValue" }', NULL);

-- insert another document that does not have an c (should succeed)
SELECT documentdb_api.insert_one('db','queryuniquecomposite','{"_id":"12", "e" : true }', NULL);

-- insert another document that has a.b = null (Should succeed)
SELECT documentdb_api.insert_one('db','queryuniquecomposite','{"_id":"13", "c": null }', NULL);

-- however, inserting 'c' again should fail.
SELECT documentdb_api.insert_one('db','queryuniquecomposite','{"_id":"14", "c": null }', NULL);

-- drop the unique index by key.
CALL documentdb_api.drop_indexes('db', '{"dropIndexes": "queryuniquecomposite", "index": {"c": 1} }');
SELECT * FROM documentdb_distributed_test_helpers.get_collection_indexes('db', 'queryuniquecomposite') ORDER BY collection_id, index_id;

-- create unique index fails for wildcard.
SELECT documentdb_api_internal.create_indexes_non_concurrently('uniquedb', '{"createIndexes": "collection1", "indexes": [{"key": {"f.$**": 1}, "name": "my_idx3", "unique": 1.0 }]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('uniquedb', '{"createIndexes": "collection1", "indexes": [{"key": {"$**": 1}, "wildcardProjection": { "f.g": 0 }, "name": "my_idx3", "unique": 1.0}]}', true);

-- test for sharded
SELECT documentdb_api.insert_one('db','queryuniqueshardedcomposite','{"_id":"1", "a": { "b": 1 }, "d": 1 }', NULL);
SELECT documentdb_api.shard_collection('db', 'queryuniqueshardedcomposite', '{ "d": "hashed" }', false);

-- Create a unique index on the collection.
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "queryuniqueshardedcomposite", "indexes": [ { "key" : { "a.b": 1 }, "name": "rumConstraint1", "unique": 1, "enableCompositeTerm": true }] }', true);
SELECT * FROM documentdb_distributed_test_helpers.get_collection_indexes('db', 'queryuniqueshardedcomposite') ORDER BY collection_id, index_id;

-- valid scenarios:
SELECT documentdb_api.insert_one('db','queryuniqueshardedcomposite','{"_id":"2", "a": { "b": [ 2, 2] }, "d": 1 }', NULL);
SELECT documentdb_api.insert_one('db','queryuniqueshardedcomposite','{"_id":"3", "a": { "b": [ 3, 4 ] }, "d": 1 }', NULL);
SELECT documentdb_api.insert_one('db','queryuniqueshardedcomposite','{"_id":"4", "a": { "b": 5 }, "d": 1 }', NULL);
SELECT documentdb_api.insert_one('db','queryuniqueshardedcomposite','{"_id":"5", "a": { "c": 5 }, "d": 1 }', NULL);

-- now violate unique in shard key "d": 1 
SELECT documentdb_api.insert_one('db','queryuniqueshardedcomposite','{"_id":"6", "a": { "b": [ 3, 6 ] }, "d": 1 }', NULL);
SELECT documentdb_api.insert_one('db','queryuniqueshardedcomposite','{"_id":"7", "a": { "b": null }, "d": 1 }', NULL);

-- now insert something in a different shard - should not violate unique
SELECT documentdb_api.insert_one('db','queryuniqueshardedcomposite','{"_id":"8", "a": { "b": [ 3, 6 ] }, "d": 2 }', NULL);
SELECT documentdb_api.insert_one('db','queryuniqueshardedcomposite','{"_id":"9", "a": { "b": null }, "d": 2 }', NULL);

-- still can be used for query.
BEGIN;
set local documentdb.forceUseIndexIfAvailable to on;
set local enable_seqscan TO off;
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'queryuniqueshardedcomposite') WHERE document @@ '{ "a.b": { "$gt": 5 } }';
ROLLBACK;

-- create unique index with truncation

SELECT string_agg(md5(random()::text), '_') AS longstring1 FROM generate_series(1, 100) \gset
SELECT string_agg(md5(random()::text), '_') AS longstring2 FROM generate_series(1, 100) \gset
SELECT string_agg(md5(random()::text), '_') AS longstring3 FROM generate_series(1, 100) \gset
SELECT string_agg(md5(random()::text), '_') AS longstring4 FROM generate_series(1, 100) \gset

SELECT length(:'longstring1');

-- create with truncation allowed and the new op-class enabled
set documentdb.enable_large_unique_index_keys to on;

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "queryuniquecomposite", "indexes": [ { "key" : { "e": 1 }, "name": "rumConstraint1", "unique": 1, "unique": 1, "sparse": 1, "enableCompositeTerm": true }] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "queryuniquecomposite", "indexes": [ { "key" : { "e": 1, "f": 1 }, "name": "rumConstraint2", "unique": 1, "unique": 1, "sparse": 1, "enableCompositeTerm": true }] }', true);
\d documentdb_data.documents_5620

-- succeeds
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', FORMAT('{ "_id": 1, "e": "%s", "f": 1 }', :'longstring1')::bson);

-- unique conflict
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', FORMAT('{ "_id": 2, "e": [ "%s", "%s" ], "f": 1 }', :'longstring1', :'longstring2')::bson);

-- create with suffix post truncation - succeeds
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', FORMAT('{ "_id": 3, "e": [ "%s-withsuffix", "%s" ], "f": 1 }', :'longstring1', :'longstring2')::bson);

-- this should also fail
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', FORMAT('{ "_id": 4, "e": "%s-withsuffix", "f": 1 }', :'longstring1')::bson);

-- this will work.
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', FORMAT('{ "_id": 5, "e": "%s-withsuffix", "f": 1 }', :'longstring2')::bson);

-- this will fail (suffix match of array and string).
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', FORMAT('{ "_id": 6, "e": [ "%s", "%s-withsuffix" ], "f": 1 }', :'longstring3', :'longstring2')::bson);

-- test truncated elements with numeric types of the same/different equivalent value. 
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', FORMAT('{ "_id": 7, "e": { "path1": "%s", "path2": 1.0 }, "f": 1 }', :'longstring3')::bson);
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', FORMAT('{ "_id": 8, "e": { "path1": "%s", "path2": { "$numberDecimal": "1.0" }}, "f": 1 }', :'longstring3')::bson);
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', FORMAT('{ "_id": 9, "e": { "path1": "%s", "path2": { "$numberDecimal": "1.01" }}, "f": 1 }', :'longstring3')::bson);

-- test composite sparse unique indexes: Should succeed since none of the documents have this path (sparse unique ignore)
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "queryuniquecomposite", "indexes": [ { "key" : { "g": 1, "h": 1 }, "name": "rumConstraint3", "unique": 1, "sparse": 1, "enableCompositeTerm": true }] }', true);

-- works
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "g": 5, "h": 5 }');

-- fails (unique cosntraint)
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "g": 5, "h": 5 }');

-- works
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "g": 5 }');

-- fails (unique constraint)
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "g": 5 }');

-- works
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "h": 5 }');

-- fails (unique constraint)
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "h": 5 }');

-- reset test data
set documentdb.enable_large_unique_index_keys to on;

DELETE FROM documentdb_data.documents_5620;
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "queryuniquecomposite", "index": [ "rumConstraint1", "rumConstraint2", "rumConstraint3" ] }');

\d documentdb_data.documents_5620

-- test unique sparse composite index
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "queryuniquecomposite", "indexes": [ { "key" : { "key1": 1, "key2": 1 }, "name": "constraint1", "unique": 1, "sparse": 1, "enableCompositeTerm": true }] }', true);

\d documentdb_data.documents_5620

-- should succeed and generate terms for all combinations on both arrays
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "key1": [1, 2, 3], "key2": [4, 5, 6] }');

-- should fail due to terms permutation on both arrays
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "key1": [1, 2, 3], "key2": [4, 5, 6] }');

SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "key1": 1, "key2": 4 }');
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "key1": 1, "key2": 5 }');
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "key1": 1, "key2": 6 }');

SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "key1": 2, "key2": 4 }');
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "key1": 2, "key2": 5 }');
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "key1": 2, "key2": 6 }');

SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "key1": 3, "key2": 4 }');
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "key1": 3, "key2": 5 }');
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "key1": 3, "key2": 6 }');

-- now test array permutations with missing key
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "key1": [1, 2, 3, 4, 5] }');

-- should fail with undefined permutations on missing key
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "key1": 1 }');
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "key1": 2 }');
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "key1": 3 }');
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "key1": 4 }');
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "key1": 5 }');

-- should succeed with null permutations on missing key (sparse)
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "key1": 1, "key2": null }');
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "key1": 2, "key2": null }');
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "key1": 3, "key2": null }');
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "key1": 4, "key2": null }');
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "key1": 5, "key2": null }');

-- should succeed due to new combinations
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "key1": 6 }');
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "key1": 6, "key2": 1 }');
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "key1": 6, "key2": 2 }');
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "key1": 6, "key2": 3 }');
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "key1": 6, "key2": 4 }');
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "key2": 1 }');
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "key2": 2 }');
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "key2": 3 }');

-- should work because doesn't fall in unique constraint
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "key3": [1, 2] }');
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "key3": [1, 2] }');

-- reset data
set documentdb.enable_large_unique_index_keys to on;

DELETE FROM documentdb_data.documents_5620;
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "queryuniquecomposite", "index": [ "constraint1" ] }');

\d documentdb_data.documents_5620

-- now test composite not-sparse unique index
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "queryuniquecomposite", "indexes": [ { "key" : { "key1": 1, "key2": 1 }, "name": "constraint1", "unique": true, "sparse": false, "enableCompositeTerm": true }] }', true);

\d+ documentdb_data.documents_5620
\d+ documentdb_data.documents_rum_index_5631

-- test array permutations with missing key
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "key1": [1, 2, 3, 4, 5] }');

-- should fail with undefined permutations on missing key
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "key1": 1 }');
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "key1": 2 }');
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "key1": 3 }');
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "key1": 4 }');
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "key1": 5 }');

-- should fail with null permutations on missing key (non-sparse)
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "key1": 1, "key2": null }');
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "key1": 2, "key2": null }');
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "key1": 3, "key2": null }');
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "key1": 4, "key2": null }');
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "key1": 5, "key2": null }');

-- reset data
set documentdb.enable_large_unique_index_keys to on;

DELETE FROM documentdb_data.documents_5620;
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "queryuniquecomposite", "index": [ "constraint1" ] }');

\d documentdb_data.documents_5620

-- now test composite not-sparse unique index
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "queryuniquecomposite", "indexes": [ { "key" : { "a": 1, "b": 1, "c": 1 }, "name": "constraint1", "unique": true, "sparse": true, "enableCompositeTerm": true }] }', true);

\d documentdb_data.documents_5620

-- should work
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "a": 1 }');
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "a": 1, "b": 1 }');
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "a": 1, "c": 1 }');
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "b": 1, "c": 1 }');

-- repeated documents won't matter because  they don't fall in the index (sparse)
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "z": 1 }');
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "z": 1 }');

-- should fail
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "a": 1 }');
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "a": 1, "b": 1 }');

-- reset data
set documentdb.enable_large_unique_index_keys to on;

DELETE FROM documentdb_data.documents_5620;
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "queryuniquecomposite", "index": [ "constraint1" ] }');

\d documentdb_data.documents_5620

-- now test composite not-sparse unique index
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "queryuniquecomposite", "indexes": [ { "key" : { "a": 1, "b": 1, "c": 1 }, "name": "constraint1", "unique": true, "sparse": false, "enableCompositeTerm": true }] }', true);

\d documentdb_data.documents_5620

-- should work
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "a": 1 }');
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "a": 1, "b": 1 }');
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "a": 1, "c": 1 }');
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "b": 1, "c": 1 }');

-- repeated documents will matter because they fall in the index (non-sparse)
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "z": 1 }');
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "z": 1 }');

-- should fail
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "a": 1 }');
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "a": 1, "b": 1 }');

-- reset data
set documentdb.enable_large_unique_index_keys to on;
set documentdb.indexTermLimitOverride to 60;

DELETE FROM documentdb_data.documents_5620;
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "queryuniquecomposite", "index": [ "constraint1" ] }');

\d documentdb_data.documents_5620

-- now test composite not-sparse unique index
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "queryuniquecomposite", "indexes": [ { "key" : { "a": 1, "b": 1 }, "name": "constraint1", "unique": true, "sparse": false, "enableCompositeTerm": true }] }', true);

\d documentdb_data.documents_5620

-- insert data in a way that bson_rum_single_path_ops will match truncated and bson_rum_unique_shard_path_ops recheck won't
set documentdb.defaultUniqueIndexKeyhashOverride to 1;

-- set log level in order to test hash collision flow
SET client_min_messages TO DEBUG1;

-- works
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "a": "thiskeyisalittlelargerthanthemaximumallowedsizeof60characters", "b": "thiskeyisalotorinotherwordsmuchmuchlargerthanthemaximumallowedsizeof60characters" }');

-- should succeed with hash collision and truncation
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "a": "thiskeyisalittlelargerthanthemaximumallowedsizeof60characters2", "b": "thiskeyisalotorinotherwordsmuchmuchlargerthanthemaximumallowedsizeof60characters2" }');

SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "a": "thiskeyisalittlelargerthanthemaximumallowedsizeof60characters3", "b": "thiskeyisalotorinotherwordsmuchmuchlargerthanthemaximumallowedsizeof60characters3" }');

SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "a": "thiskeyisalittlelargerthanthemaximumallowedsizeof60characters4", "b": "thiskeyisalotorinotherwordsmuchmuchlargerthanthemaximumallowedsizeof60characters4" }');

-- reset log level
RESET client_min_messages;

-- should fail even with hash collision and truncation
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "a": "thiskeyisalittlelargerthanthemaximumallowedsizeof60characters", "b": "thiskeyisalotorinotherwordsmuchmuchlargerthanthemaximumallowedsizeof60characters" }');

-- reset data
set documentdb.enable_large_unique_index_keys to on;
set documentdb.indexTermLimitOverride to 60;

DELETE FROM documentdb_data.documents_5620;
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "queryuniquecomposite", "index": [ "constraint1" ] }');

\d documentdb_data.documents_5620

-- now test composite not-sparse unique index
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "queryuniquecomposite", "indexes": [ { "key" : { "a": 1, "b": 1 }, "name": "constraint1", "unique": true, "sparse": false, "enableCompositeTerm": true }] }', true);

\d documentdb_data.documents_5620

-- insert data in a way that bson_rum_single_path_ops will match truncated but unique shard uuid won't match
set documentdb.defaultUniqueIndexKeyhashOverride to 0;

-- set log level in order to test hash collision flow
SET client_min_messages TO DEBUG1;

-- works -
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "a": "thiskeyisalittlelargerthanthemaximumallowedsizeof60characters", "b": "thiskeyisalotorinotherwordsmuchmuchlargerthanthemaximumallowedsizeof60characters" }');

-- should succeed without recheck logs
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "a": "thiskeyisalittlelargerthanthemaximumallowedsizeof60characters2", "b": "thiskeyisalotorinotherwordsmuchmuchlargerthanthemaximumallowedsizeof60characters2" }');

SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "a": "thiskeyisalittlelargerthanthemaximumallowedsizeof60characters3", "b": "thiskeyisalotorinotherwordsmuchmuchlargerthanthemaximumallowedsizeof60characters3" }');

SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "a": "thiskeyisalittlelargerthanthemaximumallowedsizeof60characters4", "b": "thiskeyisalotorinotherwordsmuchmuchlargerthanthemaximumallowedsizeof60characters4" }');

-- reset log level
RESET client_min_messages;

-- should fail
SELECT documentdb_api.insert_one('db', 'queryuniquecomposite', '{ "a": "thiskeyisalittlelargerthanthemaximumallowedsizeof60characters", "b": "thiskeyisalotorinotherwordsmuchmuchlargerthanthemaximumallowedsizeof60characters" }');
