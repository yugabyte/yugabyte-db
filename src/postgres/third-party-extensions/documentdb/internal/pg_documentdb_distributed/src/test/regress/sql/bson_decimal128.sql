SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;

SET citus.next_shard_id TO 176000;
SET documentdb.next_collection_id TO 17600;
SET documentdb.next_collection_index_id TO 17600;

SELECT 1 FROM documentdb_api.insert_one('db', 'decimal128', '{"a": { "$numberDecimal": "1.0" },"_id":1,"b":1}');
SELECT 1 FROM documentdb_api.insert_one('db', 'decimal128', '{"a": { "$numberDecimal": "1" },"_id":2,"b":2}');
SELECT 1 FROM documentdb_api.insert_one('db', 'decimal128', '{"a": { "$numberDecimal": "-1.0" },"_id":3,"b":3}');
SELECT 1 FROM documentdb_api.insert_one('db', 'decimal128', '{"a": { "$numberDecimal": "NaN" },"_id":4,"b":4}');
SELECT 1 FROM documentdb_api.insert_one('db', 'decimal128', '{"a": { "$numberDecimal": "-NaN" },"_id":5,"b":5}');
SELECT 1 FROM documentdb_api.insert_one('db', 'decimal128', '{"a": { "$numberDecimal": "Infinity" },"_id":6,"b":6}');
SELECT 1 FROM documentdb_api.insert_one('db', 'decimal128', '{"a": { "$numberDecimal": "-Infinity" },"_id":7,"b":7}');
SELECT 1 FROM documentdb_api.insert_one('db', 'decimal128', '{"a": { "$numberDecimal": "2234567832345678423.28293013835682" },"_id":8,"b":8}');
SELECT 1 FROM documentdb_api.insert_one('db', 'decimal128', '{"a": { "$numberDecimal": "141592653.950332e2013" },"_id":9,"b":9}');
SELECT 1 FROM documentdb_api.insert_one('db', 'decimal128', '{"a": { "$numberDecimal": "0" },"_id":10,"b":10}');

SELECT 1 FROM documentdb_api.insert_one('db', 'decimal128', '{"a": { "$numberLong": "1" },"_id":11,"b":11}');
SELECT 1 FROM documentdb_api.insert_one('db', 'decimal128', '{"a": { "$numberLong": "-1" },"_id":12,"b":12}');
SELECT 1 FROM documentdb_api.insert_one('db', 'decimal128', '{"a": { "$numberInt": "1" },"_id":13,"b":13}');
SELECT 1 FROM documentdb_api.insert_one('db', 'decimal128', '{"a": { "$numberInt": "-1" },"_id":14,"b":14}');
SELECT 1 FROM documentdb_api.insert_one('db', 'decimal128', '{"a": { "$numberDouble": "-1.00" },"_id":15,"b":15}');
SELECT 1 FROM documentdb_api.insert_one('db', 'decimal128', '{"a": { "$numberDouble": "1.00" },"_id":16,"b":16}');

SELECT 1 FROM documentdb_api.insert_one('db', 'decimal128', '{"a": [{ "$numberDecimal": "1.00" }, { "$numberDecimal": "2.00" }, { "$numberDecimal": "3.50" }, { "$numberDecimal": "5.00001" }],"_id":17,"b":17}');
SELECT 1 FROM documentdb_api.insert_one('db', 'decimal128', '{"a": { "$numberDouble": "NaN" },"_id":18,"b":15}');

-- Basic find queries
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @= '{ "a": {"$numberDecimal": "Infinity"} }';
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @= '{ "a": {"$numberDecimal": "-Infinity"} }';
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @= '{ "a": {"$numberDecimal": "Inf"} }';
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @= '{ "a": {"$numberDecimal": "-Inf"} }';
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @= '{ "a": {"$numberDecimal": "NaN"} }';
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @= '{ "a": {"$numberDecimal": "-NaN"} }';
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @= '{ "a": {"$numberDecimal": "1"} }';
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @= '{ "a": {"$numberDecimal": "1.0000000"} }';
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @= '{ "a": {"$numberDecimal": "-1"} }';
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @= '{ "a": {"$numberDecimal": "-1.00000"} }';
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @= '{ "a": {"$numberDecimal": "2234567832345678423.28293013835682"} }';

--find with order by
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') ORDER BY bson_orderby(document, '{ "a": 1 }');
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') ORDER BY bson_orderby(document, '{ "a": -1 }') DESC;

-- comparision operations
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @< '{ "a": {"$numberDecimal": "Infinity"} }';
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @<= '{ "a": {"$numberDecimal": "Infinity"} }';
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @> '{ "a": {"$numberDecimal": "Infinity"} }';
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @> '{ "a": {"$numberDecimal": "-Infinity"} }';
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @>= '{ "a": {"$numberDecimal": "-Infinity"} }';
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @< '{ "a": {"$numberDecimal": "-Infinity"} }';
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @@ '{ "a": {"$eq" : 2} }';
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @@ '{ "a": {"$gt" : {"$numberDecimal": "5.00001"}} }';
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @@ '{ "a": {"$gte" : {"$numberDecimal": "5.00001"}} }';
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @@ '{ "a": {"$lt" : {"$numberDecimal": "5.02"}} }';
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @@ '{ "a": {"$lte" : {"$numberDecimal": "5.00001"}} }';
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @@ '{ "a": {"$in" : [{"$numberDecimal": "5.00001"}, {"$numberDecimal": "inf"}] } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @@ '{ "a": {"$ne" : {"$numberDecimal": "5.00001"}} }';
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @@ '{ "a": {"$nin" : [{"$numberDecimal": "-1"}, {"$numberDecimal": "inf"}, {"$numberDecimal": "-inf"}]} }';

-- logical find queries
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @@ '{ "$and": [{ "a": {"$gt": { "$numberDecimal" : "-Infinity"}}}, {"a" : {"$lt" : { "$numberDecimal" : "Infinity"}}}] }';
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @@ '{ "$or": [{ "a": {"$gt": { "$numberDecimal" : "5"}}}, {"a" : {"$lt" : { "$numberDecimal" : "-5"}}}] }';
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @@ '{ "$nor": [{ "a": {"$gt": { "$numberDecimal" : "5"}}}, {"a" : {"$lt" : { "$numberDecimal" : "-5"}}}] }';
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @@ '{ "a": { "$not": {"$gt": { "$numberDecimal" : "-5"} } } }';

-- query element operator
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @@ '{ "a": {"$exists": true, "$gt": { "$numberDecimal" : "5"} } }';

-- array query operators
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @@ '{ "a": {"$all": [{ "$numberDecimal": "1.00" }, { "$numberDecimal": "2.00" }, { "$numberDecimal": "3.50" }, { "$numberDecimal": "5.00001" }] } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @@ '{ "a": {"$elemMatch": { "$gt": {"$numberDecimal": "3"}, "$lt": {"$numberDecimal": "5.2"}} } }';

-- Update operations with $mul and $inc
BEGIN;
SELECT documentdb_api.update('db', '{"update": "decimal128", "updates":[{"q": {"a": -1},"u":{"$mul":{"a": {"$numberDecimal": "11.1"}}},"multi":true}]}');
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @@ '{ "$and": [{ "a": {"$gte": { "$numberDecimal" : "-11.1"}}}, {"a" : {"$lte" : { "$numberDecimal" : "11.1"}}}] }';
ROLLBACK;

BEGIN;
SELECT documentdb_api.update('db', '{"update": "decimal128", "updates":[{"q": {"a": 1},"u":{"$inc":{"a": {"$numberDecimal": "50.012e2"}}},"multi":true}]}');
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @@ '{ "$and": [{ "a": {"$gte": {"$numberInt": "5000"}}}, {"a" : {"$lte" : {"$numberInt": "5003"}}}] }';
ROLLBACK;

-- Overflow operation with decimal128
SELECT documentdb_api_internal.update_bson_document('{"_id": 1, "a": { "b": {"$numberDecimal": "9.999999999999999999999999999999999e6144"} } }',
                                                '{ "": { "$mul": { "a.b": {"$numberDecimal": "2"} } } }', '{}', NULL::documentdb_core.bson, NULL::documentdb_core.bson,NULL::TEXT);
SELECT documentdb_api_internal.update_bson_document('{"_id": 1, "a": { "b": {"$numberDecimal": "-9.999999999999999999999999999999999e6144"} } }',
                                                '{ "": { "$mul": { "a.b": {"$numberDecimal": "2"} } } }', '{}', NULL::documentdb_core.bson, NULL::documentdb_core.bson,NULL::TEXT);
SELECT documentdb_api_internal.update_bson_document('{"_id": 1, "a": { "b": {"$numberDecimal": "9.999999999999999999999999999999999e6144"} } }',
                                                '{ "": { "$inc": { "a.b": {"$numberDecimal": "1.111111111111111111111111111111111e6144"} } } }', '{}', NULL::documentdb_core.bson, NULL::documentdb_core.bson,NULL::TEXT);
SELECT documentdb_api_internal.update_bson_document('{"_id": 1, "a": { "b": {"$numberDecimal": "-9.999999999999999999999999999999999e6144"} } }',
                                                '{ "": { "$inc": { "a.b": {"$numberDecimal": "-1.111111111111111111111111111111111e6144"} } } }', '{}', NULL::documentdb_core.bson, NULL::documentdb_core.bson,NULL::TEXT);

-- Testing index on decimal 128 values

-- BUG: Without this queries always seem to prefer the primary key index.
SELECT documentdb_distributed_test_helpers.drop_primary_key('db','decimal128');

-- Creating an index on the 'a' which is d128
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', documentdb_distributed_test_helpers.generate_create_index_arg('decimal128', 'd128_path_a', '{"a": 1}'), true);

BEGIN;
set local enable_seqscan TO off;
set local documentdb.forceRumIndexScantoBitmapHeapScan TO OFF;
-- EXPLAIN QUERIES
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @< '{ "a": {"$numberDecimal": "Infinity"} }';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @<= '{ "a": {"$numberDecimal": "Infinity"} }';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @> '{ "a": {"$numberDecimal": "Infinity"} }';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @> '{ "a": {"$numberDecimal": "-Infinity"} }';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @>= '{ "a": {"$numberDecimal": "-Infinity"} }';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @< '{ "a": {"$numberDecimal": "-Infinity"} }';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @@ '{ "$and": [{ "a": {"$gt": { "$numberDecimal" : "-Infinity"}}}, {"a" : {"$lt" : { "$numberDecimal" : "Infinity"}}}] }';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @@ '{ "$and": [{ "a": {"$gt": { "$numberDecimal" : "-2"}}}, {"a" : {"$lt" : { "$numberDecimal" : "2"}}}] }';
ROLLBACK;

-- Testing cases where decimal128 operations will signal exception with intel math lib and validating if these matches protocol defined behavior
-- Testing to decimal128 conversion methods using $mul, because $convert is not implemented yet (decimal128 to other types can't be tested now)
-- TODO Add proper test after implementing $convert
SELECT documentdb_api_internal.update_bson_document('{"_id": 1, "a": { "b": {"$numberInt": "2147483647"} } }',
                                                '{ "": { "$mul": { "a.b": {"$numberDecimal": "1"} } } }', '{}', NULL::documentdb_core.bson, NULL::documentdb_core.bson,NULL::TEXT);
SELECT documentdb_api_internal.update_bson_document('{"_id": 1, "a": { "b": {"$numberInt": "-2147483648"} } }',
                                                '{ "": { "$mul": { "a.b": {"$numberDecimal": "1"} } } }', '{}', NULL::documentdb_core.bson, NULL::documentdb_core.bson,NULL::TEXT);
SELECT documentdb_api_internal.update_bson_document('{"_id": 1, "a": { "b": {"$numberLong": "9223372036854775807"} } }',
                                                '{ "": { "$mul": { "a.b": {"$numberDecimal": "1"} } } }', '{}', NULL::documentdb_core.bson, NULL::documentdb_core.bson,NULL::TEXT);
SELECT documentdb_api_internal.update_bson_document('{"_id": 1, "a": { "b": {"$numberLong": "-9223372036854775808"} } }',
                                                '{ "": { "$mul": { "a.b": {"$numberDecimal": "1"} } } }', '{}', NULL::documentdb_core.bson, NULL::documentdb_core.bson,NULL::TEXT);
-- normal & inexact double conversion
SELECT documentdb_api_internal.update_bson_document('{"_id": 1, "a": { "b": {"$numberDouble": "-9.99000000000000000e+02"} } }',
                                                '{ "": { "$mul": { "a.b": {"$numberDecimal": "1"} } } }', '{}', NULL::documentdb_core.bson, NULL::documentdb_core.bson,NULL::TEXT);
SELECT documentdb_api_internal.update_bson_document('{"_id": 1, "a": { "b": {"$numberDouble": "9.535874331e+301"} } }',
                                                '{ "": { "$mul": { "a.b": {"$numberDecimal": "1"} } } }', '{}', NULL::documentdb_core.bson, NULL::documentdb_core.bson,NULL::TEXT);


-- Inexact result
SELECT documentdb_api_internal.update_bson_document('{"_id": 1, "a": { "b": {"$numberDecimal": "+6794057649266099302E-6176"} } }',
                                                '{ "": { "$mul": { "a.b": {"$numberDecimal": "+4548926796094754899573057849605421E+2026"} } } }', '{}', NULL::documentdb_core.bson, NULL::documentdb_core.bson,NULL::TEXT);
SELECT documentdb_api_internal.update_bson_document('{"_id": 1, "a": { "b": {"$numberDecimal": "+6794057649266099302E-6176"} } }',
                                                '{ "": { "$inc": { "a.b": {"$numberDecimal": "+4548926796094754899573057849605421E+2026"} } } }', '{}', NULL::documentdb_core.bson, NULL::documentdb_core.bson,NULL::TEXT);

-- Overflow signal with inexact
SELECT documentdb_api_internal.update_bson_document('{"_id": 1, "a": { "b": {"$numberDecimal": "+9579756848909076089047118570486504E+6111"} } }',
                                                '{ "": { "$mul": { "a.b": {"$numberDecimal": "+1938648456739575048278564590634903E+6111"} } } }', '{}', NULL::documentdb_core.bson, NULL::documentdb_core.bson,NULL::TEXT);
SELECT documentdb_api_internal.update_bson_document('{"_id": 1, "a": { "b": {"$numberDecimal": "+9579756848909076089047118570486504E+6111"} } }',
                                                '{ "": { "$inc": { "a.b": {"$numberDecimal": "+1938648456739575048278564590634903E+6111"} } } }', '{}', NULL::documentdb_core.bson, NULL::documentdb_core.bson,NULL::TEXT);

-- Underflow signal with inexact (no test case found for $inc)
SELECT documentdb_api_internal.update_bson_document('{"_id": 1, "a": { "b": {"$numberDecimal": "-7548269564658974956438658719038456E-6120"} } }',
                                                '{ "": { "$mul": { "a.b": {"$numberDecimal": "+9875467895987245907845734785643106E-2179"} } } }', '{}', NULL::documentdb_core.bson, NULL::documentdb_core.bson,NULL::TEXT);

-- Invalid exceptions are skipped because no valid test cases found (this is generally signalled if "SNaN" is part of operation which is not valid Decimal128 value to store)

-- TEST for double and decimal128 ordering
SELECT documentdb_api.delete('db', '{"delete":"decimal128", "deletes":[{"q":{},"limit":0}]}');
SELECT 1 FROM documentdb_api.insert_one('db', 'decimal128', '{"a": { "$numberDouble": "0.3" },"_id":1,"b":1}');
SELECT 1 FROM documentdb_api.insert_one('db', 'decimal128', '{"a": { "$numberDecimal": "0.3" },"_id":2,"b":2}');
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @@ '{ "a": {"$lt" : {"$numberDecimal": "0.3"}} }';
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @@ '{ "a": {"$gt" : {"$numberDecimal": "0.3"}} }';
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @@ '{ "a": {"$gte" : {"$numberDecimal": "0.3"}} }';
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @@ '{ "a": {"$lte" : {"$numberDecimal": "0.3"}} }';
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @@ '{ "a": {"$lt" : {"$numberDouble": "0.3"}} }';
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @@ '{ "a": {"$gt" : {"$numberDouble": "0.3"}} }';
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @@ '{ "a": {"$gte" : {"$numberDouble": "0.3"}} }';
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @@ '{ "a": {"$lte" : {"$numberDouble": "0.3"}} }';
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @@ '{ "a": {"$eq" : {"$numberDouble": "0.3"}} }';
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @@ '{ "a": {"$eq" : {"$numberDecimal": "0.3"}} }';



SELECT documentdb_api.delete('db', '{"delete":"decimal128", "deletes":[{"q":{},"limit":0}]}');
SELECT 1 FROM documentdb_api.insert_one('db', 'decimal128', '{"a": { "$numberDouble": "1.3" },"_id":3,"b":3}');
SELECT 1 FROM documentdb_api.insert_one('db', 'decimal128', '{"a": { "$numberDecimal": "1.3" },"_id":4,"b":4}');
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @@ '{ "a": {"$lt" : {"$numberDecimal": "1.3"}} }';
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @@ '{ "a": {"$gt" : {"$numberDecimal": "1.3"}} }';
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @@ '{ "a": {"$gte" : {"$numberDecimal": "1.3"}} }';
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @@ '{ "a": {"$lte" : {"$numberDecimal": "1.3"}} }';
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @@ '{ "a": {"$lt" : {"$numberDouble": "1.3"}} }';
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @@ '{ "a": {"$gt" : {"$numberDouble": "1.3"}} }';
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @@ '{ "a": {"$gte" : {"$numberDouble": "1.3"}} }';
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @@ '{ "a": {"$lte" : {"$numberDouble": "1.3"}} }';
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @@ '{ "a": {"$eq" : {"$numberDouble": "1.3"}} }';
SELECT object_id, document FROM documentdb_api.collection('db', 'decimal128') WHERE document @@ '{ "a": {"$eq" : {"$numberDecimal": "1.3"}} }';