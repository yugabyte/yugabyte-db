SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 30000;
SET documentdb.next_collection_id TO 300;
SET documentdb.next_collection_index_id TO 300;

SELECT documentdb_api.drop_collection('db', 'queryoperator');
SELECT documentdb_api.create_collection('db', 'queryoperator');

SELECT documentdb_api.drop_collection('db', 'queryoperatorIn');
SELECT documentdb_api.create_collection('db', 'queryoperatorIn');

SELECT documentdb_api.drop_collection('db', 'nullfield');
SELECT documentdb_api.create_collection('db', 'nullfield');

SELECT documentdb_api.drop_collection('db', 'singlepathindexexists');
SELECT documentdb_api.create_collection('db', 'singlepathindexexists');

\i sql/bson_query_operator_tests_insert.sql


-- these queries are negative tests for $size operator
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$size" : -3 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$size" : 3.1 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$size" : -3.4 }}' ORDER BY object_id;

-- These can't be tested since the extended json syntax treats $type as an extended json operator and not a call to actual $type function.
-- SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$type" : 123.56 }}' ORDER BY object_id;
-- SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$type" : [] }}' ORDER BY object_id;

-- these queries are negative tests for $all operator
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a"  : {"$all" : [ {"$elemMatch" : {"b" : {"$gt" : 1} } }, 1 ] } }' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a"  : {"$all" : [ 1, {"$elemMatch" : {"b" : {"$gt" : 1} } } ] } }' ORDER BY object_id;

SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a"  : {"$all" : [ {"$elemMatch" : {"b" : {"$gt" : 1} } }, {"$all" : [0]} ] } }' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a"  : {"$all" : [ {"$all" : [0]}, {"$elemMatch" : {"b" : {"$gt" : 1} } } ] } }' ORDER BY object_id;

SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a"  : {"$all" : [ {"$elemMatch" : {"b" : {"$gt" : 1} } }, {} ] } }' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a"  : {"$all" : [ {}, {"$elemMatch" : {"b" : {"$gt" : 1} } } ] } }' ORDER BY object_id;

SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a"  : {"$all" : [ {"$elemMatch" : {"b" : {"$gt" : 1} } }, { "b" : 1 } ] } }' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a"  : {"$all" : [ { "b" : 1 }, {"$elemMatch" : {"b" : {"$gt" : 1} } } ] } }' ORDER BY object_id;

SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a"  : {"$all" : [ {"$elemMatch" : {"b" : {"$gt" : 1} } }, { "$or" : [ {"b":1} ] } ] } }' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a"  : {"$all" : [ { "$or" : [ {"b":1} ] }, {"$elemMatch" : {"b" : {"$gt" : 1} } } ] } }' ORDER BY object_id;

SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a"  : {"$all" : [ {"$all" : [1] } ] } }' ORDER BY object_id;

-- negative tests for $not operator
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a"  : {"$not" : {} } }';

-- negative tests for comp operators with regex
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a"  : {"$ne" : {"$regex" : "hello", "$options" : ""} } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a"  : {"$gt" : {"$regex" : "hello", "$options" : ""} } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a"  : {"$gte" : {"$regex" : "hello", "$options" : ""} } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a"  : {"$lt" : {"$regex" : "hello", "$options" : ""} } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a"  : {"$lte" : {"$regex" : "hello", "$options" : ""} } }';

-- type undefined, no-operator
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a" : {"$undefined" : true } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a" : 1, "b" : { "$undefined" : true } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b" : { "$undefined" : true } }';

-- type undefined, comparison query operators
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a" : { "$eq" : { "$undefined" : true } } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a" : { "$ne" : { "$undefined" : true } } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a" : { "$gt" : { "$undefined" : true } } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a" : { "$gte" : { "$undefined" : true } } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a" : { "$lt" : { "$undefined" : true } } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a" : { "$lte" : { "$undefined" : true } } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a" : { "$in" : [ { "$undefined" : true } ] } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a" : { "$in" : [ { "$undefined" : true }, 2, 3, 4 ] } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a" : { "$nin" : [ { "$undefined" : true } ] } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a" : { "$nin" : [ { "$undefined" : true }, 6, 7, 8] } }';

-- type undefined, array query operators
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a" : { "$all" : [ { "$undefined" : true } ] } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a" : { "$all" : [ { "$undefined" : true }, 2 ] } }';

--type undefined, bitwise query operators
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{"a": {"$bitsAllClear": {"$undefined": true}}}';
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{"a": {"$bitsAllClear": [1, {"$undefined": true}]}}';
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{"a": {"$bitsAllSet": {"$undefined": true}}}';
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{"a": {"$bitsAllSet":[1, {"$undefined": true}]}}';
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{"a": {"$bitsAnyClear": {"$undefined": true}}}';
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{"a": {"$bitsAnyClear": [1, {"$undefined": true}]}}';
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{"a": {"$bitsAnySet": {"$undefined": true}}}';
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{"a": {"$bitsAnySet": [1, {"$undefined": true}]}}';
