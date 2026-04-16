
SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;

SET citus.next_shard_id TO 61000;
SET documentdb.next_collection_id TO 6100;
SET documentdb.next_collection_index_id TO 6100;

-- insert basic numeric data.
SELECT documentdb_api.insert_one('db','testAggregates','{"_id":"1", "i32": { "$numberInt" : "11" }, "i64": { "$numberLong" : "11" }, "idbl": { "$numberDouble" : "11.0" }}');

SELECT documentdb_api.insert_one('db','testAggregates','{"_id":"2", "i32": { "$numberInt" : "-2" }, "i64": { "$numberLong" : "-2" }, "idbl": { "$numberDouble" : "-2.0" }}');

SELECT documentdb_api.insert_one('db','testAggregates','{"_id":"3", "i32": { "$numberInt" : "20" }, "i64": { "$numberLong" : "20" }, "idbl": { "$numberDouble" : "20" }}');

-- Compute sum/Avg; they should be in the respective types.
SELECT BSONSUM(document-> 'i32'), BSONSUM(document-> 'i64'), BSONSUM(document -> 'idbl') FROM documentdb_api.collection('db', 'testAggregates');
SELECT BSONAVERAGE(document-> 'i32'), BSONAVERAGE(document-> 'i64'), BSONAVERAGE(document -> 'idbl') FROM documentdb_api.collection('db', 'testAggregates');

-- Now add some values that are non numeric.
SELECT documentdb_api.insert_one('db','testAggregates','{"_id":"4", "i32": "stringValue", "i64": "stringValue", "idbl": "stringValue"}');
SELECT documentdb_api.insert_one('db','testAggregates','{"_id":"5", "i32": true, "i64": false, "idbl": true}');
SELECT documentdb_api.insert_one('db','testAggregates','{"_id":"6", "i32": [1, 2, 3], "i64": [4, 5, 6], "idbl": [7, 8, 9]}');

-- Compute sum and average for filters that result in 0 rows.
SELECT BSONSUM(document-> 'i32'), BSONSUM(document-> 'i64'), BSONSUM(document -> 'idbl') FROM documentdb_api.collection('db', 'testAggregates') WHERE document @@ '{ "nonExistentField": 1 }';
SELECT BSONAVERAGE(document-> 'i32'), BSONAVERAGE(document-> 'i64'), BSONAVERAGE(document -> 'idbl') FROM documentdb_api.collection('db', 'testAggregates') WHERE document @@ '{ "nonExistentField": 1 }';
SELECT BSONSUM(document), BSONAVERAGE(document) FROM documentdb_api.collection('db', 'testAggregates') WHERE document @@ '{ "nonExistentField": 1 }';

-- Compute sum/Avg; They should be the same since non-numeric values do not impact the sum/avg.
SELECT BSONSUM(document-> 'i32'), BSONSUM(document-> 'i64'), BSONSUM(document -> 'idbl') FROM documentdb_api.collection('db', 'testAggregates');
SELECT BSONAVERAGE(document-> 'i32'), BSONAVERAGE(document-> 'i64'), BSONAVERAGE(document -> 'idbl') FROM documentdb_api.collection('db', 'testAggregates');
SELECT BSONSUM(document), BSONAVERAGE(document) FROM documentdb_api.collection('db', 'testAggregates');


-- Now add values that cause int32/int64 to roll over to the next highest type (mixed type sum)
SELECT documentdb_api.insert_one('db','testAggregates','{"_id":"7", "i32": { "$numberInt" : "2147483645" }, "i64": { "$numberLong" : "9223372036854775801" }, "idbl": { "$numberDouble" : "1e20" }}');

-- sum/Avg should now move to the next available type
SELECT BSONSUM(document-> 'i32'), BSONSUM(document-> 'i64'), BSONSUM(document -> 'idbl') FROM documentdb_api.collection('db', 'testAggregates');
SELECT BSONAVERAGE(document-> 'i32'), BSONAVERAGE(document-> 'i64'), BSONAVERAGE(document -> 'idbl') FROM documentdb_api.collection('db', 'testAggregates');

-- Now add a field that only exists in i32 - for i64/dbl it won't be there and it'll be a double field to test upgrade when the value type changes.
SELECT documentdb_api.insert_one('db','testAggregates','{"_id":"8", "i32": { "$numberDouble" : "31.6" }}');

-- sum/Avg They all should be dbl and partial data should be ignored for i64/idbl
SELECT BSONSUM(document-> 'i32'), BSONSUM(document-> 'i64'), BSONSUM(document -> 'idbl') FROM documentdb_api.collection('db', 'testAggregates');
SELECT BSONAVERAGE(document-> 'i32'), BSONAVERAGE(document-> 'i64'), BSONAVERAGE(document -> 'idbl') FROM documentdb_api.collection('db', 'testAggregates');

-- Query non existent field.
SELECT BSONSUM(document -> 'nao existe'), BSONAVERAGE(document -> 'nao existe') FROM documentdb_api.collection('db', 'testAggregates');


SELECT documentdb_api.insert_one('db','testAggregates','{"_id":"9",  "a" : { "b" : 1 } }');
SELECT documentdb_api.insert_one('db','testAggregates','{"_id":"10", "a" : { "b" : 2 } }');
SELECT documentdb_api.insert_one('db','testAggregates','{"_id":"11", "a" : { "b" : [ 0, 1, 2 ] } }');
SELECT documentdb_api.insert_one('db','testAggregates','{"_id":"12", "a" : [ { "b" : 1 }, { "b" : 3 } ] }');
SELECT documentdb_api.insert_one('db','testAggregates','{"_id":"13", "a" : [ { "b" : 0 }, { "b" : 1 }, { "b" : 3 } ] }');
SELECT documentdb_api.insert_one('db','testAggregates','{"_id":"14", "a" : [ { "b" : 0 }, { "b" : 1 }, { "b" : 3 } ] }');
SELECT documentdb_api.insert_one('db','testAggregates','{"_id":"15", "a" : [ { "b" : [ -1, 1, 2 ] }, { "b" : [ 0, 1, 2 ] }, { "b" : [ 0, 1, 7 ] } ]}');
SELECT documentdb_api.insert_one('db','testAggregates','{"_id":"16",  "a" : [ { "b" : 9 } ]}');

SELECT BSONMAX(bson_expression_get(document, '{ "": "$a.b" }')) FROM documentdb_api.collection('db', 'testAggregates') WHERE document @? '{ "a.b": 1}';
SELECT BSONMIN(bson_expression_get(document, '{ "": "$a.b" }')) FROM documentdb_api.collection('db', 'testAggregates') WHERE document @? '{ "a.b": 1}';

-- Rebuild bson objects from aggregates
SELECT bson_repath_and_build('max'::text, BSONMAX(document-> 'i32'), 'sum'::text, BSONSUM(document-> 'i32'), 'average'::text, BSONAVERAGE(document-> 'i32')) FROM documentdb_api.collection('db', 'testAggregates');

-- Null values in aggregates
SELECT BSONMAX(document-> 'nao existe') FROM documentdb_api.collection('db', 'testAggregates');

SELECT bson_repath_and_build('max'::text, BSONMAX(document-> 'nao existe'), 'sum'::text, BSONSUM(document-> 'i32')) FROM documentdb_api.collection('db', 'testAggregates');

-- Rebuild with prepared statement
PREPARE q1 (text, text, text) AS SELECT bson_repath_and_build($1, BSONMAX(document-> 'i32'), $2, BSONSUM(document-> 'i32'), $3, BSONAVERAGE(document-> 'i32')) FROM documentdb_api.collection('db', 'testAggregates');
EXECUTE q1 ('max', 'sum', 'average');

-- Invalid rebuild arguments
SELECT bson_repath_and_build('max'::text, BSONMAX(document-> 'i32'), 'sum'::text, BSONSUM(document-> 'i32'), 'average'::text, BSONAVERAGE(document-> 'i32')) FROM documentdb_api.collection('db', 'testAggregates');
SELECT bson_repath_and_build(BSONMAX(document-> 'i32'), 'sum'::text, BSONSUM(document-> 'i32'), 'average'::text, BSONAVERAGE(document-> 'i32')) FROM documentdb_api.collection('db', 'testAggregates');
SELECT bson_repath_and_build('max'::text, 'max2', BSONMAX(document-> 'i32'), 'sum'::text, BSONSUM(document-> 'i32'), 'average'::text, BSONAVERAGE(document-> 'i32')) FROM documentdb_api.collection('db', 'testAggregates');
SELECT bson_repath_and_build(BSONMAX(document-> 'i32'), 'max'::text, 'sum'::text, BSONSUM(document-> 'i32'), 'average'::text, BSONAVERAGE(document-> 'i32')) FROM documentdb_api.collection('db', 'testAggregates');

-- Shard the collection
SELECT documentdb_api.shard_collection('db', 'testAggregates', '{"_id":"hashed"}', false);

-- Try basic aggregates when sharded
SELECT BSONSUM(document-> 'i32'), BSONSUM(document-> 'i64'), BSONSUM(document -> 'idbl') FROM documentdb_api.collection('db', 'testAggregates');
SELECT BSONAVERAGE(document-> 'i32'), BSONAVERAGE(document-> 'i64'), BSONAVERAGE(document -> 'idbl') FROM documentdb_api.collection('db', 'testAggregates');
SELECT BSONMIN(document-> 'i32'), BSONMIN(document-> 'i64'), BSONMIN(document -> 'idbl') FROM documentdb_api.collection('db', 'testAggregates');
SELECT BSONMAX(document-> 'i32'), BSONMAX(document-> 'i64'), BSONMAX(document -> 'idbl') FROM documentdb_api.collection('db', 'testAggregates');

-- shard on a path that not all documents have:
SELECT documentdb_api.shard_collection('db', 'testAggregates', '{"_id":"hashed"}', false);
SELECT BSONSUM(document-> 'i32'), BSONSUM(document-> 'i64'), BSONSUM(document -> 'idbl') FROM documentdb_api.collection('db', 'testAggregates');
SELECT BSONAVERAGE(document-> 'i32'), BSONAVERAGE(document-> 'i64'), BSONAVERAGE(document -> 'idbl') FROM documentdb_api.collection('db', 'testAggregates');
SELECT BSONMIN(document-> 'i32'), BSONMIN(document-> 'i64'), BSONMIN(document -> 'idbl') FROM documentdb_api.collection('db', 'testAggregates');
SELECT BSONMAX(document-> 'i32'), BSONMAX(document-> 'i64'), BSONMAX(document -> 'idbl') FROM documentdb_api.collection('db', 'testAggregates');


-- validation from field_name_validation.js
SELECT documentdb_api.insert_one('db', 'testAggregates', '{ "_id": { "a": 1, "b": 2 }, "c.d": 3 }');

SELECT BSONSUM(bson_expression_get(document, '{ "": "$c.d" }')) FROM documentdb_api.collection('db', 'testAggregates') GROUP BY bson_expression_get(document, '{ "": "$_id.a" }');

SELECT BSONSUM(bson_expression_get(document, '{ "": "$_id.b" }')) FROM documentdb_api.collection('db', 'testAggregates') GROUP BY bson_expression_get(document, '{ "": "$_id.a" }');

SELECT bson_repath_and_build('e.f'::text, BSONSUM(bson_expression_get(document, '{ "": "$_id.b" }'))) FROM documentdb_api.collection('db', 'testAggregates') GROUP BY bson_expression_get(document, '{ "": "$_id.a" }');
SELECT bson_repath_and_build('$e'::text, BSONSUM(bson_expression_get(document, '{ "": "$_id.b" }'))) FROM documentdb_api.collection('db', 'testAggregates') GROUP BY bson_expression_get(document, '{ "": "$_id.a" }');

SELECT documentdb_api.insert_one('db', 'testAggregatesWithIndex', '{ "_id": 1, "a": 1 }');
SELECT documentdb_api.insert_one('db', 'testAggregatesWithIndex', '{ "_id": 2, "a": 2 }');
SELECT documentdb_api.insert_one('db', 'testAggregatesWithIndex', '{ "_id": 3, "a": 3 }');
SELECT documentdb_api.insert_one('db', 'testAggregatesWithIndex', '{ "_id": 4, "a": 4 }');
SELECT documentdb_api.insert_one('db', 'testAggregatesWithIndex', '{ "_id": 5, "a": 5 }');

-- simulate a count
BEGIN;
-- avoid sequential scan (likely to be preferred on small tables)
set local enable_seqscan to off;
set local documentdb.forceUseIndexIfAvailable to on;
SELECT BSONSUM('{ "": 1 }') FROM documentdb_api.collection('db', 'testAggregatesWithIndex');

ROLLBACK;

BEGIN;
-- avoid sequential scan (likely to be preferred on small tables)
set local enable_seqscan to off;
set local documentdb.forceUseIndexIfAvailable to on;
EXPLAIN (COSTS OFF) SELECT BSONSUM('{ "": 1 }') FROM documentdb_api.collection('db', 'testAggregatesWithIndex');
ROLLBACK;

-- create an index.
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', documentdb_distributed_test_helpers.generate_create_index_arg('testAggregatesWithIndex', 'idx_1', '{ "a": 1 }'), true);

-- repeat 
BEGIN;
-- avoid sequential scan (likely to be preferred on small tables)
set local enable_seqscan to off;
set local documentdb.forceUseIndexIfAvailable to on;
SELECT BSONSUM('{ "": 1 }') FROM documentdb_api.collection('db', 'testAggregatesWithIndex');
ROLLBACK;

BEGIN;
-- avoid sequential scan (likely to be preferred on small tables)
set local enable_seqscan to off;
set local documentdb.forceUseIndexIfAvailable to on;
EXPLAIN (COSTS OFF) SELECT BSONSUM('{ "": 1 }') FROM documentdb_api.collection('db', 'testAggregatesWithIndex');
ROLLBACK;


-- now shard the collection.
SELECT documentdb_api.shard_collection('db', 'testAggregatesWithIndex', '{ "_id": "hashed" }', false);

-- repeat
BEGIN;
-- avoid sequential scan (likely to be preferred on small tables)
set local enable_seqscan to off;
set local documentdb.forceUseIndexIfAvailable to on;
SELECT BSONSUM('{ "": 1 }') FROM documentdb_api.collection('db', 'testAggregatesWithIndex');
ROLLBACK;

BEGIN;
-- avoid sequential scan (likely to be preferred on small tables)
set local enable_seqscan to off;
set local documentdb.forceUseIndexIfAvailable to on;
EXPLAIN (COSTS OFF) SELECT BSONSUM('{ "": 1 }') FROM documentdb_api.collection('db', 'testAggregatesWithIndex');
ROLLBACK;
