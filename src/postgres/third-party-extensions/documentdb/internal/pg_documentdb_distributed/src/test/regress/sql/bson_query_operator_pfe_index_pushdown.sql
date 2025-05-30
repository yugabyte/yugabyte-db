SET search_path to documentdb_api_catalog;
SET citus.next_shard_id TO 10980000;
SET documentdb.next_collection_id TO 10980;
SET documentdb.next_collection_index_id TO 10980;

SELECT documentdb_api.create_collection('db', 'bsonquery');

-- insert documents with different compositions of keys and types
SELECT documentdb_api.insert_one('db','bsonquery', '{"_id": 1, "a" : { "c" : 0 }}', NULL);
SELECT documentdb_api.insert_one('db','bsonquery', '{"_id": 2, "a" : { "d" : 0 }}', NULL);
SELECT documentdb_api.insert_one('db','bsonquery', '{"_id": 3, "a" : { "b" : 1 }, "b": "xyz" }', NULL);
SELECT documentdb_api.insert_one('db','bsonquery', '{"_id": 4, "a" : { "b" : { "$undefined": true } }}', NULL);
SELECT documentdb_api.insert_one('db','bsonquery', '{"_id": 5, "a" : { "b" : "xxx" }}', NULL);
SELECT documentdb_api.insert_one('db','bsonquery', '{"_id": 6, "a" : { "c" : "xxx" }}', NULL);
SELECT documentdb_api.insert_one('db','bsonquery', '{"_id": 7, "a" : { "e" : 1, "f": 1 }}', NULL);

-- create indexes with partial filter expressions
SELECT documentdb_api_internal.create_indexes_non_concurrently(
  'db',
  '{
     "createIndexes": "bsonquery",
     "indexes": [
       {
         "key": {"a.b": 1}, "name": "my_idx_1",
         "partialFilterExpression":
         {
           "a.b": {"$exists": true }
         }
       },
       {
         "key": {"a.c": 1}, "name": "my_idx_2",
         "partialFilterExpression":
         {
           "a.c": {"$gte": "abc" }
         }
       },
       {
         "key": {"a.e": 1, "a.f": 1}, "name": "my_idx_3",
         "partialFilterExpression":
        {
           "a.e": 1,
           "a.f": 1
         }
       }
     ]
   }',
   true
);

SELECT collection_id AS collid FROM documentdb_api_catalog.collections
WHERE collection_name = 'bsonquery' AND database_name = 'db' \gset
\d documentdb_data.documents_:collid

SELECT bson_dollar_unwind(cursorpage, '$cursor.firstBatch') FROM documentdb_api.list_indexes_cursor_first_page('db','{ "listIndexes": "bsonquery" }') ORDER BY 1;

SELECT documentdb_distributed_test_helpers.drop_primary_key('db','bsonquery');;

BEGIN;
SET LOCAL seq_page_cost TO 100;
SET LOCAL documentdb.forceUseIndexIfAvailable to true;

-- should push down to pfe index since types match
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'bsonquery') WHERE document @@ '{ "a.c": { "$gte" :  "c" }}';
SELECT document FROM documentdb_api.collection('db', 'bsonquery') WHERE document @@ '{ "a.c": { "$gte" :  "c" }}';

-- should not push to pfe index due to type mismatch
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'bsonquery') WHERE document @@ '{ "a.c": { "$gte" :  1 }}';
SELECT document FROM documentdb_api.collection('db', 'bsonquery') WHERE document @@ '{ "a.c": { "$gte" :  1 }}';

-- should push to $exists pfe index using minkey
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'bsonquery') WHERE document @@ '{ "a.b": { "$gte" :  "a" }}';
SELECT document FROM documentdb_api.collection('db', 'bsonquery') WHERE document @@ '{ "a.b": { "$gte" :  "a" }}';

-- should not push to $exists pfe index due to key mismatch
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'bsonquery') WHERE document @@ '{ "a.d": { "$gte" :  "a" }}';
SELECT document FROM documentdb_api.collection('db', 'bsonquery') WHERE document @@ '{ "a.d": { "$gte" :  "a" }}';

-- should push to pfe index when $ne is present
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'bsonquery') WHERE document @@ '{ "a": { "$ne" :  null }, "a.e": 1, "a.f": 1}';
SELECT document FROM documentdb_api.collection('db', 'bsonquery') WHERE document @@ '{ "a": { "$ne" :  null }, "a.e": 1, "a.f": 1}';

-- should not push to pfe index due to missing key
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'bsonquery') WHERE document @@ '{ "a": { "$ne" :  null }, "a.e": 1}';
SELECT document FROM documentdb_api.collection('db', 'bsonquery') WHERE document @@ '{ "a": { "$ne" :  null }, "a.e": 1}';

-- should not push to pfe index since $eq: null cannot match $exists: true
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'bsonquery') WHERE document @@ '{ "a": { "$ne" :  null }, "a.b": null }';
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'bsonquery') WHERE document @@ '{ "a": { "$ne" :  null }, "a.b": { "$ne": null } }';
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'bsonquery') WHERE document @@ '{ "a": { "$ne" :  null }, "a.b": { "$gt": null } }';
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'bsonquery') WHERE document @@ '{ "a": { "$ne" :  null }, "a.b": { "$lt": null } }';
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'bsonquery') WHERE document @@ '{ "a": { "$ne" :  null }, "a.b": { "$gte": null } }';
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'bsonquery') WHERE document @@ '{ "a": { "$ne" :  null }, "a.b": { "$lte": null } }';

-- test PFE pushdown for $in 

-- can push down
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'bsonquery') WHERE document @@ '{ "a.b": { "$in" : [ 1, 2, 3 ] } }';

-- cannot push down (fails PFE)
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'bsonquery') WHERE document @@ '{ "a.c": { "$in" : [ "aaa", "aa1" ] } }';
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'bsonquery') WHERE document @@ '{ "a.c": { "$in" : [ "aaa", "bbb" ] } }';

-- can push down
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'bsonquery') WHERE document @@ '{ "a.c": { "$in" : [ "ccc", "bbb" ] } }';
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'bsonquery') WHERE document @@ '{ "a.c": { "$in" : [ "abc", "bbb" ] } }';

-- cannot push down
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'bsonquery') WHERE document @@ '{ "a.e": { "$in" : [ 1, 2 ] } }';
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'bsonquery') WHERE document @@ '{ "a.e": { "$in" : [ 1, 2 ] }, "a.f": { "$in": [ 3, 4 ]} }';
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'bsonquery') WHERE document @@ '{ "a.e": { "$in" : [ 1, 2 ] }, "a.g": { "$in": [ 3, 4 ]} }';
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'bsonquery') WHERE document @@ '{ "a.e": { "$in" : [ 1, 2 ] }, "a.f": { "$in": [ 3, 1 ]} }';

-- can push down
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'bsonquery') WHERE document @@ '{ "a.e": { "$in" : [ 1, 1, 1 ] }, "a.f": { "$in": [ 1, 1 ]} }';
ROLLBACK;

-- shard the collection
SELECT documentdb_api.shard_collection('db', 'bsonquery', '{ "_id": "hashed" }', false);

-- rerun the queries

BEGIN;
SET LOCAL seq_page_cost TO 100;
SET LOCAL documentdb.forceUseIndexIfAvailable to true;

-- should push down to pfe index since types match
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'bsonquery') WHERE document @@ '{ "a.c": { "$gte" :  "c" }}';
SELECT document FROM documentdb_api.collection('db', 'bsonquery') WHERE document @@ '{ "a.c": { "$gte" :  "c" }}';

-- should not push to pfe index due to type mismatch
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'bsonquery') WHERE document @@ '{ "a.c": { "$gte" :  1 }}';
SELECT document FROM documentdb_api.collection('db', 'bsonquery') WHERE document @@ '{ "a.c": { "$gte" :  1 }}';

-- should push to $exists pfe index using minkey
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'bsonquery') WHERE document @@ '{ "a.b": { "$gte" :  "a" }}';
SELECT document FROM documentdb_api.collection('db', 'bsonquery') WHERE document @@ '{ "a.b": { "$gte" :  "a" }}';

-- should not push to $exists pfe index due to key mismatch
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'bsonquery') WHERE document @@ '{ "a.d": { "$gte" :  "a" }}';
SELECT document FROM documentdb_api.collection('db', 'bsonquery') WHERE document @@ '{ "a.d": { "$gte" :  "a" }}';

-- should push to pfe index when $ne is present
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'bsonquery') WHERE document @@ '{ "a": { "$ne" :  null }, "a.e": 1, "a.f": 1}';
SELECT document FROM documentdb_api.collection('db', 'bsonquery') WHERE document @@ '{ "a": { "$ne" :  null }, "a.e": 1, "a.f": 1}';

-- should not push to pfe index due to missing key
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'bsonquery') WHERE document @@ '{ "a": { "$ne" :  null }, "a.e": 1}';
SELECT document FROM documentdb_api.collection('db', 'bsonquery') WHERE document @@ '{ "a": { "$ne" :  null }, "a.e": 1}';

-- should not push to pfe index since $eq: null cannot match $exists: true
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'bsonquery') WHERE document @@ '{ "a": { "$ne" :  null }, "a.b": null }';
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'bsonquery') WHERE document @@ '{ "a": { "$ne" :  null }, "a.b": { "$ne": null } }';
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'bsonquery') WHERE document @@ '{ "a": { "$ne" :  null }, "a.b": { "$gt": null } }';
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'bsonquery') WHERE document @@ '{ "a": { "$ne" :  null }, "a.b": { "$lt": null } }';
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'bsonquery') WHERE document @@ '{ "a": { "$ne" :  null }, "a.b": { "$gte": null } }';
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'bsonquery') WHERE document @@ '{ "a": { "$ne" :  null }, "a.b": { "$lte": null } }';
ROLLBACK;