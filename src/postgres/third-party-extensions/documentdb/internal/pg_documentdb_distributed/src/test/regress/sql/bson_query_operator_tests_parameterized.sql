SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;

SET citus.next_shard_id TO 66400;
SET documentdb.next_collection_id TO 6640;
SET documentdb.next_collection_index_id TO 6640;

/* Insert with a.b being an object with various types*/
SELECT documentdb_api.insert_one('db','queryOperatorParameterized', '{"_id": 1, "a" : { "b" : 0 }}', NULL);
SELECT documentdb_api.insert_one('db','queryOperatorParameterized', '{"_id": 2, "a" : { "b" : 1 }}', NULL);
SELECT documentdb_api.insert_one('db','queryOperatorParameterized', '{"_id": 3, "a" : { "b" : 2.0 }}', NULL);
SELECT documentdb_api.insert_one('db','queryOperatorParameterized', '{"_id": 4, "a" : { "b" : "someString" }}', NULL);
SELECT documentdb_api.insert_one('db','queryOperatorParameterized', '{"_id": 5, "a" : { "b" : true }}', NULL);

/* insert some documents with a.{some other paths} */
SELECT documentdb_api.insert_one('db','queryOperatorParameterized', '{"_id": 6, "a" : 1}', NULL);
SELECT documentdb_api.insert_one('db','queryOperatorParameterized', '{"_id": 7, "a" : true}', NULL);
SELECT documentdb_api.insert_one('db','queryOperatorParameterized', '{"_id": 8, "a" : [0, 1, 2]}', NULL);
SELECT documentdb_api.insert_one('db','queryOperatorParameterized', '{"_id": 9, "a" : { "c": 1 }}', NULL);

/* insert paths with nested objects arrays */
SELECT documentdb_api.insert_one('db','queryOperatorParameterized', '{"_id": 10, "a" : { "b" : [ 0, 1, 2 ] } }', NULL);
SELECT documentdb_api.insert_one('db','queryOperatorParameterized', '{"_id": 11, "a" : [ { "b": 0 }, { "b": 1 }, { "b": 3.0 }] }', NULL);
SELECT documentdb_api.insert_one('db','queryOperatorParameterized', '{"_id": 12, "a" : [ { "b": [-1, 1, 2] }, { "b": [0, 1, 2] }, { "b": [0, 1, 7] }] }', NULL);
SELECT documentdb_api.insert_one('db','queryOperatorParameterized', '{"_id": 13, "a" : [ { "b": [[-1, 1, 2]] }, { "b": [[0, 1, 2]] }, { "b": [[0, 1, 7]] }] }', NULL);
SELECT documentdb_api.insert_one('db','queryOperatorParameterized', '{"_id": 14, "a" : { "b" : [ { "1" : [1, 2, 3 ] } ] } }', NULL);

SET client_min_messages TO WARNING;
DO $$
DECLARE
  v_collection_id bigint;
BEGIN
    SELECT collection_id INTO v_collection_id FROM documentdb_api_catalog.collections
  	    WHERE database_name = 'db' AND collection_name = 'queryOperatorParameterized';
	EXECUTE format('SELECT undistribute_table(''documentdb_data.documents_%s'')', v_collection_id);
END
$$;
SET client_min_messages TO DEFAULT;

PREPARE q1 (text, text, bson) AS SELECT object_id, document FROM documentdb_api.collection($1, $2) WHERE document @@ $3;

-- valid queries
EXECUTE q1( 'db', 'queryOperatorParameterized', '{ "a.b": { "$eq" : 1 }}');
EXECUTE q1( 'db', 'queryOperatorParameterized', '{ "a.b": { "$lte" : [true, false] }}');
EXECUTE q1( 'db', 'queryOperatorParameterized', '{ "$and": [ { "a.b": 1 }, { "a.b": 2 } ] }');
EXECUTE q1( 'db', 'queryOperatorParameterized', '{ "$or": [ { "a.b": 1 }, { "a.b": 2 } ] }');
EXECUTE q1( 'db', 'queryOperatorParameterized', '{ "$or": [ { "a.b": 1 }, { "a.c": { "$exists": 1 } } ] }');
EXECUTE q1( 'db', 'queryOperatorParameterized', '{ "$and": [ { "a.b": 1 }, { "a.c": { "$exists": 0 } } ] }');
EXECUTE q1( 'db', 'queryOperatorParameterized', '{ "$and": [ { "a.b": { "$gte": 1 } }, { "a.c": { "$exists": 0 } } ] }');

-- invalid queries.
EXECUTE q1( 'db', 'queryOperatorParameterized', '{ "a": { "$invalid": 1 } }');
EXECUTE q1( 'db', 'queryOperatorParameterized', '{ "$and": [ { "a": { "$invalid": 2 } } ] }');
EXECUTE q1( 'db', 'queryOperatorParameterized', '{ "$and": [ ] }');

-- repeat the queries with remote execution on.
BEGIN;
set local citus.enable_local_execution to off;
EXECUTE q1( 'db', 'queryOperatorParameterized', '{ "a.b": { "$eq" : 1 }}');
EXECUTE q1( 'db', 'queryOperatorParameterized', '{ "a.b": { "$lte" : [true, false] }}');
EXECUTE q1( 'db', 'queryOperatorParameterized', '{ "$and": [ { "a.b": 1 }, { "a.b": 2 } ] }');
EXECUTE q1( 'db', 'queryOperatorParameterized', '{ "$or": [ { "a.b": 1 }, { "a.b": 2 } ] }');
EXECUTE q1( 'db', 'queryOperatorParameterized', '{ "$or": [ { "a.b": 1 }, { "a.c": { "$exists": 1 } } ] }');
EXECUTE q1( 'db', 'queryOperatorParameterized', '{ "$and": [ { "a.b": 1 }, { "a.c": { "$exists": 0 } } ] }');
EXECUTE q1( 'db', 'queryOperatorParameterized', '{ "$and": [ { "a.b": { "$gte": 1 } }, { "a.c": { "$exists": 0 } } ] }');

-- invalid queries.
EXECUTE q1( 'db', 'queryOperatorParameterized', '{ "a": { "$invalid": 1 } }');
EXECUTE q1( 'db', 'queryOperatorParameterized', '{ "$and": [ { "a": { "$invalid": 2 } } ] }');
EXECUTE q1( 'db', 'queryOperatorParameterized', '{ "$and": [ ] }');
ROLLBACK;

-- repeat the queries with remote execution on and hex output.
BEGIN;
set local citus.enable_local_execution to off;
set local documentdb_core.bsonUseEJson to off;
EXECUTE q1( 'db', 'queryOperatorParameterized', bson_json_to_bson('{ "a.b": { "$eq" : 1 }}'));
EXPLAIN (COSTS OFF) EXECUTE q1( 'db', 'queryOperatorParameterized', bson_json_to_bson('{ "a.b": { "$eq" : 1 }}'));
ROLLBACK;

-- run an explain analyze
EXPLAIN (COSTS OFF, TIMING OFF, ANALYZE ON, SUMMARY OFF) EXECUTE q1( 'db', 'queryOperatorParameterized', '{ "a.b": { "$eq" : 1 }}');

PREPARE q2(text, text, bson, bson) AS SELECT document FROM documentdb_api.collection($1, $2) WHERE document OPERATOR(documentdb_api_catalog.@@) $3 ORDER BY documentdb_api_catalog.bson_orderby(document, $4) DESC;

EXPLAIN (COSTS OFF) EXECUTE q2( 'db', 'queryOperatorParameterized', '{ "a.b" : { "$gt": 0 } }', '{ "a.b": -1 }');

BEGIN;
set local citus.enable_local_execution to off;
EXPLAIN (COSTS OFF) EXECUTE q2( 'db', 'queryOperatorParameterized', '{ "a.b" : { "$gt": 0 } }', '{ "a.b": -1 }');
ROLLBACK;
