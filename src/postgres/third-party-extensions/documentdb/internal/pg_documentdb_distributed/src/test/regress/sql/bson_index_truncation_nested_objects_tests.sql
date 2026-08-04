SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 1040000;
SET documentdb.next_collection_id TO 10400;
SET documentdb.next_collection_index_id TO 10400;


-- Set configs to something reasonable for testing.
set documentdb.indexTermLimitOverride to 50;

--------------------------
---- Nested Array
--------------------------

--  gin_bson_get_single_path_generated_terms(
--  		document bson,
--  		path text,
--  		isWildcard bool,
--   		generateNotFoundTerm bool default false,
--   		addMetadata bool default false,
--   		termLength int)


-- non wild card
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 1, "ikey" : [["abcdefghijklmonpqrstuvwsyz"]]}', 'ikey', false, true, true, 50) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 2, "ikey" : [[1]]}', 'ikey', false, true, true, 50) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 3, "ikey" : [[{ "$numberDecimal" : "1234567891011" }]]}', 'ikey', false, true, true, 50) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 3, "ikey" : [[true]]}', 'ikey', false, true, true, 50) term;

SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 1, "ikey" : [["abcdefghijklmonpqrstuvwsyz"]]}', 'ikey', false, true, true, 35) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 2, "ikey" : [[1]]}', 'ikey', false, true, true, 35) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 3, "ikey" : [[{ "$numberDecimal" : "1234567891011" }]]}', 'ikey', false, true, true, 35) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 3, "ikey" : [[true]]}', 'ikey', false, true, true, 35) term;

-- wild card
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 1, "ikey" : [["abcdefghijklmonpqrstuvwsyz"]]}', 'ikey', true, true, true, 50) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 2, "ikey" : [[1]]}', 'ikey', true, true, true, 50) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 3, "ikey" : [[{ "$numberDecimal" : "1234567891011" }]]}', 'ikey', true, true, true, 50) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 3, "ikey" : [[true]]}', 'ikey', true, true, true, 50) term;

SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 1, "ikey" : [["abcdefghijklmonpqrstuvwsyz"]]}', 'ikey', true, true, true, 35) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 2, "ikey" : [[1]]}', 'ikey', true, true, true, 35) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 3, "ikey" : [[{ "$numberDecimal" : "1234567891011" }]]}', 'ikey', true, true, true, 35) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 3, "ikey" : [[true]]}', 'ikey', true, true, true, 35) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 1, "ikey" : [["abcdefghijklmonpqrstuvwsyz"]]}', 'ikey', false, true, true, 50) term;

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "index_truncation_tests_nested_array", "indexes": [ { "key": { "ikey": 1 }, "name": "ikey_1", "enableLargeIndexKeys": true } ] }');

-- now we have an index with truncation enabled and 50 chars.
\d documentdb_data.documents_10400;


-- now insert some documents that does exceed the index term limit.
SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_array', '{ "_id": 1, "ikey": [["abcdefghijklmonpqrstuvwsyz"]] }');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_array', '{ "_id": 2, "ikey": [[1]] }');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_array', '{ "_id": 3, "ikey": [[{ "$numberDecimal" : "1234567891011" }]] }');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_array', '{ "_id": 4, "ikey": [[true]] }');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_array', '{ "_id": 5, "ikey": [[[]]] }');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_array', '{ "_id": 6, "ikey": [[{}]] }');


SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests_nested_array') WHERE document @@ '{ "ikey": { "$gt": [[1]] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests_nested_array') WHERE document @@ '{ "ikey": { "$gt": [[false]] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests_nested_array') WHERE document @@ '{ "ikey": { "$gt": [[[]]] } }';

-- now insert some documents that does exceed the index term limit.
SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_array', '{ "_id": 7, "ikey": [[["abcdefghijklmonpqrstuvwsyz"]]] }');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_array', '{ "_id": 8, "ikey": [[[1]]] }');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_array', '{ "_id": 9, "ikey": [[[{ "$numberDecimal" : "1234567891011" }]]] }');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_array', '{ "_id": 10, "ikey": [[[true]]] }');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_array', '{ "_id": 11, "ikey": [[[[]]]] }');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_array', '{ "_id": 12, "ikey": [[[{}]]] }');


SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests_nested_array') WHERE document @@ '{ "ikey": { "$gt": [[[1]]] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests_nested_array') WHERE document @@ '{ "ikey": { "$gt": [[[{ "$numberDecimal" : "1234567891011" }]]] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests_nested_array') WHERE document @@ '{ "ikey": { "$gt": [[[false]]] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests_nested_array') WHERE document @@ '{ "ikey": { "$gt": [[[[]]]] } }';


SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_array', '{ "_id": 13, "ikey": "abcdefghijklmonpqrstuvwsyz" }');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_array', '{ "_id": 14, "ikey": 1 }');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_array', '{ "_id": 15, "ikey": { "$numberDecimal" : "1234567891011" } }');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_array', '{ "_id": 16, "ikey": true }');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_array', '{ "_id": 17, "ikey": [] }');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_array', '{ "_id": 18, "ikey": {} }');

SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests_nested_array') WHERE document @@ '{ "ikey": { "$gt": 1 } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests_nested_array') WHERE document @@ '{ "ikey": { "$gt": { "$numberDecimal" : "1234567891011" } } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests_nested_array') WHERE document @@ '{ "ikey": { "$gt": false } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests_nested_array') WHERE document @@ '{ "ikey": { "$gt": [] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests_nested_array') WHERE document @@ '{ "ikey": { "$in": [1, true] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests_nested_array') WHERE document @@ '{ "ikey": { "$nin": [1, true] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests_nested_array') WHERE document @@ '{ "ikey": { "$elemMatch": { "$in": [1, true] } } }'; 
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests_nested_array') WHERE document @@ '{ "ikey": { "$elemMatch": { "$in": [[[1]], [[true]]] } } }'; 
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests_nested_array') WHERE document @@ '{ "ikey": { "$elemMatch": { "$in": [[[1]], [[true]]], "$gt": [[{ "$numberDecimal" : "1234567891011" }]] } } }'; 
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests_nested_array') WHERE document @@ '{ "ikey": { "$all": [[[1]]] } }'; 
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests_nested_array') WHERE document @@ '{ "ikey": { "$all": [[[[]]]] } }'; 

SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests_nested_array') WHERE document @@ '{ "ikey": { "$ne": { "$numberDecimal" : "1234567891011" } } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests_nested_array') WHERE document @@ '{ "ikey": { "$in": [[[1]], [[true]]] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests_nested_array') WHERE document @@ '{ "ikey": { "$nin": [[[1]], [[true]]] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests_nested_array') WHERE document @@ '{ "ikey": { "$ne": [[{ "$numberDecimal" : "1234567891011" }]] } }';


--------------------------
---- Nested Documents
--------------------------

--  gin_bson_get_single_path_generated_terms(
--  		document bson,
--  		path text,
--  		isWildcard bool,
--   		generateNotFoundTerm bool default false,
--   		addMetadata bool default false,
--   		termLength int)

-- non wild card
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 1, "ikey" : { "a" : ["abcdefghijklmonpqrstuvwsyz"]}}', 'ikey', false, true, true, 50) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 2, "ikey" : { "a" : [1]}}', 'ikey', false, true, true, 50) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 3, "ikey" : { "a" : [{ "$numberDecimal" : "1234567891011" }]}}', 'ikey', false, true, true, 50) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 3, "ikey" : { "a" : [true]}}', 'ikey', false, true, true, 50) term;

SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 1, "ikey" : { "a" : ["abcdefghijklmonpqrstuvwsyz"]}}', 'ikey', false, true, true, 35) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 2, "ikey" : { "a" : [1]}}', 'ikey', false, true, true, 35) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 3, "ikey" : { "a" : [{ "$numberDecimal" : "1234567891011" }]}}', 'ikey', false, true, true, 35) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 3, "ikey" : { "a" : [true]}}', 'ikey', false, true, true, 35) term;

-- wild card
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 1, "ikey" : { "a" : ["abcdefghijklmonpqrstuvwsyz"]}}', 'ikey', true, true, true, 50) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 2, "ikey" : { "a" : [1]}}', 'ikey', true, true, true, 50) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 3, "ikey" : { "a" : [{ "$numberDecimal" : "1234567891011" }]]}}', 'ikey', true, true, true, 50) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 3, "ikey" : { "a" : [true]}}', 'ikey', true, true, true, 50) term;

SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 1, "ikey" : { "a" : ["abcdefghijklmonpqrstuvwsyz"]}}', 'ikey', true, true, true, 35) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 2, "ikey" : { "a" : [1]}}', 'ikey', true, true, true, 35) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 3, "ikey" : { "a" : [{ "$numberDecimal" : "1234567891011" }]}}', 'ikey', true, true, true, 35) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 3, "ikey" : { "a" : [true]}}', 'ikey', true, true, true, 35) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 1, "ikey" : { "a" : ["abcdefghijklmonpqrstuvwsyz"]}}', 'ikey', false, true, true, 50) term;


SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "index_truncation_tests_nested_documents", "indexes": [ { "key": { "ikey": 1 }, "name": "ikey_1", "enableLargeIndexKeys": true } ] }');

-- now we have an index with truncation enabled and 100 chars.
\d documentdb_data.documents_10401;


-- now insert some documents that does exceed the index term limit.
SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_documents', '{ "_id": 1, "ikey": { "a" : ["abcdefghijklmonpqrstuvwsyz"]} }');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_documents', '{ "_id": 2, "ikey": { "a" : [1]} }');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_documents', '{ "_id": 3, "ikey": { "a" : [{ "$numberDecimal" : "1234567891011" }]} }');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_documents', '{ "_id": 4, "ikey": { "a" : [true]} }');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_documents', '{ "_id": 5, "ikey": { "a" : [[]}] }');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_documents', '{ "_id": 6, "ikey": { "a" : [{}]} }');

SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests_nested_documents') WHERE document @@ '{ "ikey.a": { "$gt": [1] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests_nested_documents') WHERE document @@ '{ "ikey.a": { "$gt": [false] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests_nested_documents') WHERE document @@ '{ "ikey.a": { "$gt": [[]] } }';

-- now insert some documents that does exceed the index term limit.
SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_documents', '{ "_id": 7, "ikey": { "a" : [["abcdefghijklmonpqrstuvwsyz"]]} }');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_documents', '{ "_id": 8, "ikey": { "a" : [[1]]} }');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_documents', '{ "_id": 9, "ikey": { "a" : [[{ "$numberDecimal" : "1234567891011" }]]} }');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_documents', '{ "_id": 10, "ikey": { "a" : [[true]]} }');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_documents', '{ "_id": 11, "ikey": { "a" : [[[]]]} }');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_documents', '{ "_id": 12, "ikey": { "a" : [[{}]]} }');

SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests_nested_documents') WHERE document @@ '{ "ikey.a": { "$gt": [[1]] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests_nested_documents') WHERE document @@ '{ "ikey.a": { "$gt": [[{ "$numberDecimal" : "1234567891011" }]] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests_nested_documents') WHERE document @@ '{ "ikey.a": { "$gt": [[false]] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests_nested_documents') WHERE document @@ '{ "ikey.a": { "$gt": [[[]]] } }';


SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_documents', '{ "_id": 13, "ikey": "abcdefghijklmonpqrstuvwsyz" }');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_documents', '{ "_id": 14, "ikey": 1 }');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_documents', '{ "_id": 15, "ikey": { "$numberDecimal" : "1234567891011" } }');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_documents', '{ "_id": 16, "ikey": true }');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_documents', '{ "_id": 17, "ikey": [] }');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_documents', '{ "_id": 18, "ikey": {} }');

SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests_nested_documents') WHERE document @@ '{ "ikey.a": { "$gt": 1 } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests_nested_documents') WHERE document @@ '{ "ikey.a": { "$gt": { "$numberDecimal" : "1234567891011" } } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests_nested_documents') WHERE document @@ '{ "ikey.a": { "$gt": false } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests_nested_documents') WHERE document @@ '{ "ikey.a": { "$gt": [] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests_nested_documents') WHERE document @@ '{ "ikey.a": { "$in": [1, true] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests_nested_documents') WHERE document @@ '{ "ikey.a": { "$nin": [1, true] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests_nested_documents') WHERE document @@ '{ "ikey.a": { "$elemMatch": { "$in": [1, true] } } }'; 
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests_nested_documents') WHERE document @@ '{ "ikey.a": { "$elemMatch": { "$in": [[1], [true]] } } }'; 
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests_nested_documents') WHERE document @@ '{ "ikey.a": { "$elemMatch": { "$in": [1, "abcdefghijklmonpqrstuvwsyz"] } } }'; 
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests_nested_documents') WHERE document @@ '{ "ikey.a": { "$elemMatch": { "$in": [[1], ["abcdefghijklmonpqrstuvwsyz"]] } } }'; 
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests_nested_documents') WHERE document @@ '{ "ikey.a": { "$elemMatch": { "$in": [[1], [true]], "$gt": [{ "$numberDecimal" : "1234567891011" }] } } }'; 
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests_nested_documents') WHERE document @@ '{ "ikey.a": { "$all": [[1]] } }'; 
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests_nested_documents') WHERE document @@ '{ "ikey.a": { "$all": [[[]]] } }'; 

SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests_nested_documents') WHERE document @@ '{ "ikey.a": { "$ne": { "$numberDecimal" : "1234567891011" } } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests_nested_documents') WHERE document @@ '{ "ikey.a": { "$in": [[1], [true]] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests_nested_documents') WHERE document @@ '{ "ikey.a": { "$nin": [[1], [true]] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests_nested_documents') WHERE document @@ '{ "ikey.a": { "$ne": [{ "$numberDecimal" : "1234567891011" }] } }';

-- term generation tests

set documentdb.indexTermLimitOverride to 100;

SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_objects', '{ "_id": 19, "ikey": [[[[[[[[[[1]]]]]]]]]] }');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_objects', '{ "_id": 20, "ikey": [[[[[[[[[[true]]]]]]]]]] }');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_objects', '{ "_id": 21, "ikey": [[[[[[[[[[{ "$numberDecimal" : "1234567891011" }]]]]]]]]]] }');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_objects', '{ "_id": 22, "ikey": [[[[[[[[[[{ "$maxKey": 1 }]]]]]]]]]] }');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_objects', '{ "_id": 23, "ikey": [[[[[[[[[[{ "$minKey": 1 }]]]]]]]]]] }');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_objects', '{ "_id": 24, "ikey": [[[[[[[[[[{ "$numberDouble" : "1234567891011" }]]]]]]]]]] }');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_objects', '{ "_id": 25, "ikey": [[[[[[[[[[ "abcdefghijklmopqrstuvwxyz" ]]]]]]]]]] }');

SELECT did, length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM ( SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms(document, 'ikey', false, true, true, 115) term, document-> '_id' as did from documentdb_data.documents_10402) docs;
SELECT did, length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM ( SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms(document, 'ikey', false, true, true, 120) term, document-> '_id' as did from documentdb_data.documents_10402) docs;

SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_objects_2', '{ "_id": 26, "ikey": { "a" : {"b" : { "c": { "d": { "e": { "f" : { "g" : { "h": 1}}}}}}}}}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_objects_2', '{ "_id": 27, "ikey": { "a" : {"b" : { "c": { "d": { "e": { "f" : { "g" : { "h": true}}}}}}}}}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_objects_2', '{ "_id": 28, "ikey": { "a" : {"b" : { "c": { "d": { "e": { "f" : { "g" : { "h": { "$maxKey": 1 }}}}}}}}}}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_objects_2', '{ "_id": 29, "ikey": { "a" : {"b" : { "c": { "d": { "e": { "f" : { "g" : { "h": { "$minKey": 1 }}}}}}}}}}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_objects_2', '{ "_id": 30, "ikey": { "a" : {"b" : { "c": { "d": { "e": { "f" : { "g" : { "h": { "$numberDouble" : "1234567891011" }}}}}}}}}}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_objects_2', '{ "_id": 31, "ikey": { "a" : {"b" : { "c": { "d": { "e": { "f" : { "g" : { "h": { "$numberDecimal" : "1234567891011" }}}}}}}}}}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_objects_2', '{ "_id": 32, "ikey": { "a" : {"b" : { "c": { "d": { "e": { "f" : { "g" : { "h": "abcdefghijklmopqrstuvwxyz"}}}}}}}}}');

SELECT did, length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM ( SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms(document, 'ikey', false, true, true, 100) term, document-> '_id' as did from documentdb_data.documents_10403) docs;
SELECT did, length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM ( SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms(document, 'ikey', false, true, true, 105) term, document-> '_id' as did from documentdb_data.documents_10403) docs;

SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_objects_3', '{ "_id": 33, "ikey": [{ "a" : [{"b" : [{ "c": [{ "d": [{ "e": [{ "f" : [{ "g" : [{ "h": 1}]}]}]}]}]}]}]}]}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_objects_3', '{ "_id": 34, "ikey": [{ "a" : [{"b" : [{ "c": [{ "d": [{ "e": [{ "f" : [{ "g" : [{ "h": true}]}]}]}]}]}]}]}]}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_objects_3', '{ "_id": 35, "ikey": [{ "a" : [{"b" : [{ "c": [{ "d": [{ "e": [{ "f" : [{ "g" : [{ "h": { "$numberDecimal" : "1234567891011" }}]}]}]}]}]}]}]}]}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_objects_3', '{ "_id": 36, "ikey": [{ "a" : [{"b" : [{ "c": [{ "d": [{ "e": [{ "f" : [{ "g" : [{ "h": { "$maxKey": 1 }}]}]}]}]}]}]}]}]}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_objects_3', '{ "_id": 37, "ikey": [{ "a" : [{"b" : [{ "c": [{ "d": [{ "e": [{ "f" : [{ "g" : [{ "h": { "$minKey": 1 }}]}]}]}]}]}]}]}]}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_objects_3', '{ "_id": 38, "ikey": [{ "a" : [{"b" : [{ "c": [{ "d": [{ "e": [{ "f" : [{ "g" : [{ "h": { "$numberDouble" : "1234567891011" }}]}]}]}]}]}]}]}]}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested_objects_3', '{ "_id": 39, "ikey": [{ "a" : [{"b" : [{ "c": [{ "d": [{ "e": [{ "f" : [{ "g" : [{ "h": "abcdefghijklmopqrstuvwxyz"}]}]}]}]}]}]}]}]}');

SELECT did, length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM ( SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms(document, 'ikey', false, true, true, 165) term, document-> '_id' as did from documentdb_data.documents_10404) docs;
SELECT did, length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM ( SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms(document, 'ikey', false, true, true, 170) term, document-> '_id' as did from documentdb_data.documents_10404) docs;

SELECT did, length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM ( SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms(document, 'ikey', true, true, true, 165) term, document-> '_id' as did from documentdb_data.documents_10404) docs;
SELECT did, length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM ( SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms(document, 'ikey', true, true, true, 170) term, document-> '_id' as did from documentdb_data.documents_10404) docs;

-- specific tests
set documentdb.indexTermLimitOverride to 200;


SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "index_truncation_tests_nested", "indexes": [ { "key": { "$**" : 1 }, "name": "wildcardIndex",  "enableLargeIndexKeys": true  } ] }', true);
SELECT documentdb_distributed_test_helpers.drop_primary_key('db', 'index_truncation_tests_nested');


-- 1. does not fail.
SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested','{
    "_id" : 1,
    "data": {
        "$binary": {
            "base64": "ewogICAgIl9pZCI6ICI2NzY5ZGI2NmExZGIyMDA4NTczOTdiNjAiLAogICAgImluZGV4IjogMCwKICAgICJndWlkIjogIjMzZjk1N2QwLWU0M2MtNDYyNi1iOGJjLTFkODE0ODE3MmZkNyIsCiAgICAiaXNBY3RpdmUiOiB0cnVlLAogICAgImJhbGFuY2UiOiAiJDMsOTMwLjI4IiwKICAgICJwaWN0dXJlIjogImh0dHA6Ly9wbGFjZWhvbGQuaXQvMzJ4MzIiLAogICAgImFnZSI6IDI3LAogICAgImV5ZUNvbG9yIjogImdyZWVuIiwKICAgICJuYW1lIjogIkxpbGEgVmFsZW56dWVsYSIsCiAgICAiZ2VuZGVyIjogImZlbWFsZSIsCiAgICAiY29tcGFueSI6ICJBUFBMSURFQyIsCiAgICAiZW1haWwiOiAibGlsYXZhbGVuenVlbGFAYXBwbGlkZWMuY29tIiwKICAgICJwaG9uZSI6ICIrMSAoODIyKSA1NDItMjQ1MCIsCiAgICAiYWRkcmVzcyI6ICI2NjcgT3hmb3JkIFdhbGssIENhc2h0b3duLCBBcmthbnNhcywgMjYyMyIsCiAgICAiYWJvdXQiOiAiSW4gYWxpcXVpcCBub24gZXN0IHNpdCBlc3QgTG9yZW0gcXVpIG9jY2FlY2F0IGZ1Z2lhdCBleC4gRG9sb3IgbGFib3JlIG1vbGxpdCBjb25zZXF1YXQgaXBzdW0uIFF1aXMgY29uc2VxdWF0IGRlc2VydW50IGVpdXNtb2QgcHJvaWRlbnQgZHVpcyBvY2NhZWNhdCBldSBlbGl0IG5vbiBhZCBleCBkZXNlcnVudCBwcm9pZGVudCBMb3JlbS4gSWQgbW9sbGl0IGNpbGx1bSBpcHN1bSB2b2x1cHRhdGUgYXV0ZSBsYWJvcmlzIGVzc2UgYW5pbS5cclxuIiwKICAgICJyZWdpc3RlcmVkIjogIjIwMjAtMDktMDRUMDM6MDA6MTggKzAzOjAwIiwKICAgICJsYXRpdHVkZSI6IDguMTI4MTI0LAogICAgImxvbmdpdHVkZSI6IC0zNy4zMjY4MDMsCiAgICAidGFncyI6IFsKICAgICAgImV0IiwKICAgICAgInN1bnQiLAogICAgICAibW9sbGl0IiwKICAgICAgImFuaW0iLAogICAgICAibW9sbGl0IiwKICAgICAgImRvIiwKICAgICAgIm5vbiIKICAgIF0sCiAgICAiZnJpZW5kcyI6IFsKICAgICAgewogICAgICAgICJpZCI6IDAsCiAgICAgICAgIm5hbWUiOiAiTGVuYSBIdWZmbWFuIgogICAgICB9LAogICAgICB7CiAgICAgICAgImlkIjogMSwKICAgICAgICAibmFtZSI6ICJWaWNreSBMdWNhcyIKICAgICAgfSwKICAgICAgewogICAgICAgICJpZCI6IDIsCiAgICAgICAgIm5hbWUiOiAiSm9zZXBoIEdhcmRuZXIiCiAgICAgIH0KICAgIF0sCiAgICAiZ3JlZXRpbmciOiAiSGVsbG8sIExpbGEgVmFsZW56dWVsYSEgWW91IGhhdmUgMTAgdW5yZWFkIG1lc3NhZ2VzLiIsCiAgICAiZmF2b3JpdGVGcnVpdCI6ICJiYW5hbmEiCiAgfQ==",
            "subType": "01"
        }
    },
    "updatedAt": {
        "$date": 101010
    },
    "estimatedTimeOfArrival": {
        "$date": 101010
    },
    "isSomething": false,
    "createdAt": {
        "$date": 101010
    },
    "itemData": [
        {
            "attribute": "1234",
            "cb": 12431,
            "dx": "test",
            "el": {
                "$code": "eyAiZXhhbXBsZSI6IDEyMywgInZhbHVlIjogImFib2JvcmEiIH0="
            },
            "first": null,
            "greaterThan": null,
            "shortPath": "aas",
            "i": null,
            "largePathWithObject": {
                "option1": null,
                "option2": "small-string1",
                "option3": null,
                "option4": 123,
                "onlyPathWithLargeString": "aassadasdasdsdasdasdsadsa-test-data-with-large-string",
                "prior": 123123213213
            }
        },
        {
            "attribute": "xpto",
            "ccb": 34354,
            "dx": 0,
            "el": null,
            "first": null,
            "greaterThan": null,
            "shortPath": null,
            "i": null,
            "largePathWithObject": {
                "option1": null,
                "option2": "small-string2",
                "option3": null,
                "option4": 122121,
                "onlyPathWithLargeString": "aassadasdasdsdasdasdsadsa-test-data-with-large-string2",
                "prior": 1111111111111111
            }
        }
    ],
    "employeeCode": "LBF"
}');

-- 2 a. Terms generated with nested object index term truncation support

SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms(
  '{
    "_id": 1, 
    "itemData" : [
        {
            "attribute": "1234",
            "cb": 12431,
            "dx": "test",
            "el": {
                "$code": "eyAiZXhhbXBsZSI6IDEyMywgInZhbHVlIjogImFib2JvcmEiIH0="
            },
            "first": null,
            "greaterThan": null,
            "shortPath": "aas",
            "i": null,
            "largePathWithObject": {
                "option1": null,
                "option2": "small-string1",
                "option3": null,
                "option4": 123,
                "onlyPathWithLargeString": "aassadasdasdsdasdasdsadsa-test-data-with-large-string",
                "prior": 123123213213
            }
        },
        {
            "attribute": "xpto",
            "ccb": 34354,
            "dx": 0,
            "el": null,
            "first": null,
            "greaterThan": null,
            "shortPath": null,
            "i": null,
            "largePathWithObject": {
                "option1": null,
                "option2": "small-string2",
                "option3": null,
                "option4": 122121,
                "onlyPathWithLargeString": "aassadasdasdsdasdasdsadsa-test-data-with-large-string2",
                "prior": 1111111111111111
            }
        }
    ]
  }'
, 'itemData', true, true, true, 1000) term;

-- 2 b. Terms generated with nested object index term truncation support

SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms(
  '{
    "_id": 1, 
    "itemData" : [
        {
            "attribute": "1234",
            "cb": 12431,
            "dx": "test",
            "el": {
                "$code": "eyAiZXhhbXBsZSI6IDEyMywgInZhbHVlIjogImFib2JvcmEiIH0="
            },
            "first": null,
            "greaterThan": null,
            "shortPath": "aas",
            "i": null,
            "largePathWithObject": {
                "option1": null,
                "option2": "small-string1",
                "option3": null,
                "option4": 123,
                "onlyPathWithLargeString": "aassadasdasdsdasdasdsadsa-test-data-with-large-string",
                "prior": 123123213213
            }
        },
        {
            "attribute": "xpto",
            "ccb": 34354,
            "dx": 0,
            "el": null,
            "first": null,
            "greaterThan": null,
            "shortPath": null,
            "i": null,
            "largePathWithObject": {
                "option1": null,
                "option2": "small-string2",
                "option3": null,
                "option4": 122121,
                "onlyPathWithLargeString": "aassadasdasdsdasdasdsadsa-test-data-with-large-string2",
                "prior": 1111111111111111
            }
        }
    ]
  }'
, 'itemData', true, true, true, 60) term;


-- 3. Now insert the document with nested object index term truncation support

SELECT documentdb_api.insert_one('db', 'index_truncation_tests_nested', '
{
    "_id" : 2,
    "data": {
        "$binary": {
            "base64": "ewogICAgIl9pZCI6ICI2NzY5ZGI2NmExZGIyMDA4NTczOTdiNjAiLAogICAgImluZGV4IjogMCwKICAgICJndWlkIjogIjMzZjk1N2QwLWU0M2MtNDYyNi1iOGJjLTFkODE0ODE3MmZkNyIsCiAgICAiaXNBY3RpdmUiOiB0cnVlLAogICAgImJhbGFuY2UiOiAiJDMsOTMwLjI4IiwKICAgICJwaWN0dXJlIjogImh0dHA6Ly9wbGFjZWhvbGQuaXQvMzJ4MzIiLAogICAgImFnZSI6IDI3LAogICAgImV5ZUNvbG9yIjogImdyZWVuIiwKICAgICJuYW1lIjogIkxpbGEgVmFsZW56dWVsYSIsCiAgICAiZ2VuZGVyIjogImZlbWFsZSIsCiAgICAiY29tcGFueSI6ICJBUFBMSURFQyIsCiAgICAiZW1haWwiOiAibGlsYXZhbGVuenVlbGFAYXBwbGlkZWMuY29tIiwKICAgICJwaG9uZSI6ICIrMSAoODIyKSA1NDItMjQ1MCIsCiAgICAiYWRkcmVzcyI6ICI2NjcgT3hmb3JkIFdhbGssIENhc2h0b3duLCBBcmthbnNhcywgMjYyMyIsCiAgICAiYWJvdXQiOiAiSW4gYWxpcXVpcCBub24gZXN0IHNpdCBlc3QgTG9yZW0gcXVpIG9jY2FlY2F0IGZ1Z2lhdCBleC4gRG9sb3IgbGFib3JlIG1vbGxpdCBjb25zZXF1YXQgaXBzdW0uIFF1aXMgY29uc2VxdWF0IGRlc2VydW50IGVpdXNtb2QgcHJvaWRlbnQgZHVpcyBvY2NhZWNhdCBldSBlbGl0IG5vbiBhZCBleCBkZXNlcnVudCBwcm9pZGVudCBMb3JlbS4gSWQgbW9sbGl0IGNpbGx1bSBpcHN1bSB2b2x1cHRhdGUgYXV0ZSBsYWJvcmlzIGVzc2UgYW5pbS5cclxuIiwKICAgICJyZWdpc3RlcmVkIjogIjIwMjAtMDktMDRUMDM6MDA6MTggKzAzOjAwIiwKICAgICJsYXRpdHVkZSI6IDguMTI4MTI0LAogICAgImxvbmdpdHVkZSI6IC0zNy4zMjY4MDMsCiAgICAidGFncyI6IFsKICAgICAgImV0IiwKICAgICAgInN1bnQiLAogICAgICAibW9sbGl0IiwKICAgICAgImFuaW0iLAogICAgICAibW9sbGl0IiwKICAgICAgImRvIiwKICAgICAgIm5vbiIKICAgIF0sCiAgICAiZnJpZW5kcyI6IFsKICAgICAgewogICAgICAgICJpZCI6IDAsCiAgICAgICAgIm5hbWUiOiAiTGVuYSBIdWZmbWFuIgogICAgICB9LAogICAgICB7CiAgICAgICAgImlkIjogMSwKICAgICAgICAibmFtZSI6ICJWaWNreSBMdWNhcyIKICAgICAgfSwKICAgICAgewogICAgICAgICJpZCI6IDIsCiAgICAgICAgIm5hbWUiOiAiSm9zZXBoIEdhcmRuZXIiCiAgICAgIH0KICAgIF0sCiAgICAiZ3JlZXRpbmciOiAiSGVsbG8sIExpbGEgVmFsZW56dWVsYSEgWW91IGhhdmUgMTAgdW5yZWFkIG1lc3NhZ2VzLiIsCiAgICAiZmF2b3JpdGVGcnVpdCI6ICJiYW5hbmEiCiAgfQ==",
            "subType": "01"
        }
    },
    "updatedAt": {
        "$date": 101010
    },
    "estimatedTimeOfArrival": {
        "$date": 101010
    },
    "isSomething": false,
    "createdAt": {
        "$date": 101010
    },
    "itemData": [
        {
            "attribute": "1234",
            "cb": 12431,
            "dx": "test",
            "el": {
                "$code": "eyAiZXhhbXBsZSI6IDEyMywgInZhbHVlIjogImFib2JvcmEiIH0="
            },
            "first": null,
            "greaterThan": null,
            "shortPath": "aas",
            "i": null,
            "largePathWithObject": {
                "option1": null,
                "option2": "small-string1",
                "option3": null,
                "option4": 123,
                "onlyPathWithLargeString": "aassadasdasdsdasdasdsadsa-test-data-with-large-string",
                "prior": 123123213213
            }
        },
        {
            "attribute": "xpto",
            "ccb": 34354,
            "dx": 0,
            "el": null,
            "first": null,
            "greaterThan": null,
            "shortPath": null,
            "i": null,
            "largePathWithObject": {
                "option1": null,
                "option2": "small-string2",
                "option3": null,
                "option4": 122121,
                "onlyPathWithLargeString": "aassadasdasdsdasdasdsadsa-test-data-with-large-string2",
                "prior": 1111111111111111
            }
        }
    ],
    "employeeCode": "LBF"
}');


-- 4. Now issue a find query

SELECT document FROM bson_aggregation_find('db', '{ "find": "index_truncation_tests_nested", "filter": { "itemData": { "$eq": 

    {
        "attribute": "1234",
        "cb": 12431,
        "dx": "test",
        "el": {
            "$code": "eyAiZXhhbXBsZSI6IDEyMywgInZhbHVlIjogImFib2JvcmEiIH0="
        },
        "first": null,
        "greaterThan": null,
        "shortPath": "aas",
        "i": null,
        "largePathWithObject": {
            "option1": null,
            "option2": "small-string1",
            "option3": null,
            "option4": 123,
            "onlyPathWithLargeString": "aassadasdasdsdasdasdsadsa-test-data-with-large-string",
            "prior": 123123213213
        }
    }

} }, "projection": { "_id": 1 }, "sort": { "_id": 1 }, "skip": 0, "limit": 5 }');

-- 5. Index usage explain

EXPLAIN(ANALYZE ON, COSTS OFF, TIMING OFF, SUMMARY OFF, BUFFERS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "index_truncation_tests_nested", "filter": { "itemData": { "$eq": 

    {
        "attribute": "1234",
        "cb": 12431,
        "dx": "test",
        "el": {
            "$code": "eyAiZXhhbXBsZSI6IDEyMywgInZhbHVlIjogImFib2JvcmEiIH0="
        },
        "first": null,
        "greaterThan": null,
        "shortPath": "aas",
        "i": null,
        "largePathWithObject": {
            "option1": null,
            "option2": "small-string1",
            "option3": null,
            "option4": 123,
            "onlyPathWithLargeString": "aassadasdasdsdasdasdsadsa-test-data-with-large-string",
            "prior": 123123213213
        }
    }

} }, "projection": { "_id": 1 }, "sort": { "_id": 1 }, "skip": 0, "limit": 5 }');


SELECT document FROM bson_aggregation_find('db', '{ "find": "index_truncation_tests_nested", "filter": { "itemData.largePathWithObject.option2": { "$eq": 

"small-string1"

} }, "projection": { "_id": 1 }, "sort": { "_id": 1 }, "skip": 0, "limit": 5 }');

EXPLAIN(ANALYZE ON, COSTS OFF, TIMING OFF, SUMMARY OFF, BUFFERS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "index_truncation_tests_nested", "filter": { "itemData.largePathWithObject.option2": { "$eq": 

"small-string1"

} }, "projection": { "_id": 1 }, "sort": { "_id": 1 }, "skip": 0, "limit": 5 }');
