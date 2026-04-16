SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 4800000;
SET documentdb.next_collection_id TO 4800;
SET documentdb.next_collection_index_id TO 4800;

-- Test basic usage
SELECT bson_dollar_unwind('{"_id":"1", "a" : { "b" : [1, 2, 3] } }', '$a.b');
SELECT bson_dollar_unwind('{"_id":"1", "a" : { "b" : [[1, 2], [2, 3], [4, 5]] } }', '$a.b');
SELECT bson_dollar_unwind('{"_id":"1", "a" : { "b" : [] } }', '$a.b');
SELECT bson_dollar_unwind('{"_id":"1", "a" : { "b" : null } }', '$a.b');
SELECT bson_dollar_unwind('{"_id":"1", "a" : { "b" : 4 } }', '$a.b');
SELECT bson_dollar_unwind('{"_id":"1", "a" : { "b" : { "c" : 1 } } }', '$a.b');
SELECT bson_dollar_unwind('{"_id":"1", "a" : [1, 2, 3] }', '$a');

SELECT bson_dollar_unwind('{"_id":"1", "a" : [1, {"c":1}, [3,4], "x"] }', '$a');
SELECT bson_dollar_unwind('{"_id":"1", "a" : { "b" : [{"a":1}, {"a":2}, {"a":3}] } }', '$a.b');
SELECT bson_dollar_unwind('{"_id":"1", "x": "y", "a" : { "b" : [1, 2, 3], "c" : [1, 2] } }', '$a.b');
SELECT bson_dollar_unwind('{"_id":"1", "x": "y", "a" : { "b" : [1, 2, 3], "c" : { "x" : [1, 2] } } }', '$a.c');
SELECT bson_dollar_unwind('{"_id":"1", "x": "y", "a" : { "b" : [1, 2, 3], "c" : { "x" : [1, 2] } } }', '$a.c.x');

SELECT bson_dollar_unwind('{"_id":"1", "a" : { "b" : [1, 2, 3, null] } }', '$a.b');

-- Preserve null and empty
SELECT bson_dollar_unwind('{"_id":"1", "a" : { "b" : [1, 2, 3] } }', '{"path":"$a.b", "preserveNullAndEmptyArrays":true}'::bson);
SELECT bson_dollar_unwind('{"_id":"1", "a" : { "b" : [] } }', '{"path":"$a.b", "preserveNullAndEmptyArrays":true}'::bson);
SELECT bson_dollar_unwind('{"_id":"1", "a" : { "b" : null } }', '{"path":"$a.b", "preserveNullAndEmptyArrays":true}'::bson);
SELECT bson_dollar_unwind('{"_id":"1", "a" : { } }', '{"path":"$a.b", "preserveNullAndEmptyArrays":true}'::bson);

-- Project idx field
SELECT bson_dollar_unwind('{"_id":"1", "a" : { "b" : [1, 2, 3] } }', '{"path":"$a.b", "includeArrayIndex":"idx"}'::bson);
SELECT bson_dollar_unwind('{"_id":"1", "a" : { "b" : [] } }', '{"path":"$a.b", "preserveNullAndEmptyArrays":false, "includeArrayIndex":"idx"}'::bson);
SELECT bson_dollar_unwind('{"_id":"1", "a" : { "b" : null } }', '{"path":"$a.b", "preserveNullAndEmptyArrays":false, "includeArrayIndex":"idx"}'::bson);
SELECT bson_dollar_unwind('{"_id":"1", "a" : { "b" : [] } }', '{"path":"$a.b", "preserveNullAndEmptyArrays":true, "includeArrayIndex":"idx"}'::bson);
SELECT bson_dollar_unwind('{"_id":"1", "a" : { "b" : null } }', '{"path":"$a.b", "preserveNullAndEmptyArrays":true, "includeArrayIndex":"idx"}'::bson);
SELECT bson_dollar_unwind('{"_id":"1", "a" : { "b" : [1, 2, 3] } }', '{"path":"$a.b", "includeArrayIndex":""}'::bson);
SELECT bson_dollar_unwind('{"_id":"1", "a" : { } }', '{"path":"$a.b", "includeArrayIndex":"","preserveNullAndEmptyArrays":true}'::bson);
SELECT bson_dollar_unwind('{"_id":"1", "a" : { } }', '{"path":"$a.b", "includeArrayIndex":"","preserveNullAndEmptyArrays":false}'::bson);

-- Project conflicting idx
SELECT bson_dollar_unwind('{"_id":"1", "a" : { "b" : [1, 2, 3] } }', '{"path":"$a.b", "includeArrayIndex":"_id"}'::bson);

-- Test invalid paths
SELECT bson_dollar_unwind('{"_id":"1", "a" : { "b" : 4 } }', 'a.b');
SELECT bson_dollar_unwind('{"_id":"1", "a" : { "b" : 4 } }', '');
SELECT bson_dollar_unwind('{"_id":"1", "a" : { "b" : 4 } }', '$');


-- Invalid Arguments
SELECT bson_dollar_unwind('{"_id":"1", "a" : { "b" : [1, 2, 3] } }', '{"path":"$a.b", "preserveNullAndEmptyArrays":"a"}'::bson);
