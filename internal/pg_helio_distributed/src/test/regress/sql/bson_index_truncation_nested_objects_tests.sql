SET search_path TO helio_api_catalog;
SET citus.next_shard_id TO 1040000;
SET helio_api.next_collection_id TO 10400;
SET helio_api.next_collection_index_id TO 10400;


-- Set configs to something reasonable for testing.
set helio_api.indexTermLimitOverride to 50;
SET helio_api.enableIndexTermTruncationOnNestedObjects to on;

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
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM helio_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 1, "ikey" : [["abcdefghijklmonpqrstuvwsyz"]]}', 'ikey', false, true, true, 50) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM helio_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 2, "ikey" : [[1]]}', 'ikey', false, true, true, 50) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM helio_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 3, "ikey" : [[{ "$numberDecimal" : "1234567891011" }]]}', 'ikey', false, true, true, 50) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM helio_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 3, "ikey" : [[true]]}', 'ikey', false, true, true, 50) term;

SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM helio_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 1, "ikey" : [["abcdefghijklmonpqrstuvwsyz"]]}', 'ikey', false, true, true, 35) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM helio_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 2, "ikey" : [[1]]}', 'ikey', false, true, true, 35) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM helio_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 3, "ikey" : [[{ "$numberDecimal" : "1234567891011" }]]}', 'ikey', false, true, true, 35) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM helio_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 3, "ikey" : [[true]]}', 'ikey', false, true, true, 35) term;

-- wild card
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM helio_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 1, "ikey" : [["abcdefghijklmonpqrstuvwsyz"]]}', 'ikey', true, true, true, 50) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM helio_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 2, "ikey" : [[1]]}', 'ikey', true, true, true, 50) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM helio_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 3, "ikey" : [[{ "$numberDecimal" : "1234567891011" }]]}', 'ikey', true, true, true, 50) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM helio_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 3, "ikey" : [[true]]}', 'ikey', true, true, true, 50) term;

SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM helio_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 1, "ikey" : [["abcdefghijklmonpqrstuvwsyz"]]}', 'ikey', true, true, true, 35) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM helio_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 2, "ikey" : [[1]]}', 'ikey', true, true, true, 35) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM helio_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 3, "ikey" : [[{ "$numberDecimal" : "1234567891011" }]]}', 'ikey', true, true, true, 35) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM helio_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 3, "ikey" : [[true]]}', 'ikey', true, true, true, 35) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM helio_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 1, "ikey" : [["abcdefghijklmonpqrstuvwsyz"]]}', 'ikey', false, true, true, 50) term;

SELECT helio_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "index_truncation_tests_nested_array", "indexes": [ { "key": { "ikey": 1 }, "name": "ikey_1", "enableLargeIndexKeys": true } ] }');

-- now we have an index with truncation enabled and 50 chars.
\d helio_data.documents_10400;


-- now insert some documents that does exceed the index term limit.
SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_array', '{ "_id": 1, "ikey": [["abcdefghijklmonpqrstuvwsyz"]] }');
SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_array', '{ "_id": 2, "ikey": [[1]] }');
SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_array', '{ "_id": 3, "ikey": [[{ "$numberDecimal" : "1234567891011" }]] }');
SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_array', '{ "_id": 4, "ikey": [[true]] }');
SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_array', '{ "_id": 5, "ikey": [[[]]] }');
SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_array', '{ "_id": 6, "ikey": [[{}]] }');


SELECT document FROM helio_api.collection('db', 'index_truncation_tests_nested_array') WHERE document @@ '{ "ikey": { "$gt": [[1]] } }';
SELECT document FROM helio_api.collection('db', 'index_truncation_tests_nested_array') WHERE document @@ '{ "ikey": { "$gt": [[false]] } }';
SELECT document FROM helio_api.collection('db', 'index_truncation_tests_nested_array') WHERE document @@ '{ "ikey": { "$gt": [[[]]] } }';

-- now insert some documents that does exceed the index term limit.
SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_array', '{ "_id": 7, "ikey": [[["abcdefghijklmonpqrstuvwsyz"]]] }');
SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_array', '{ "_id": 8, "ikey": [[[1]]] }');
SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_array', '{ "_id": 9, "ikey": [[[{ "$numberDecimal" : "1234567891011" }]]] }');
SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_array', '{ "_id": 10, "ikey": [[[true]]] }');
SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_array', '{ "_id": 11, "ikey": [[[[]]]] }');
SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_array', '{ "_id": 12, "ikey": [[[{}]]] }');


SELECT document FROM helio_api.collection('db', 'index_truncation_tests_nested_array') WHERE document @@ '{ "ikey": { "$gt": [[[1]]] } }';
SELECT document FROM helio_api.collection('db', 'index_truncation_tests_nested_array') WHERE document @@ '{ "ikey": { "$gt": [[[{ "$numberDecimal" : "1234567891011" }]]] } }';
SELECT document FROM helio_api.collection('db', 'index_truncation_tests_nested_array') WHERE document @@ '{ "ikey": { "$gt": [[[false]]] } }';
SELECT document FROM helio_api.collection('db', 'index_truncation_tests_nested_array') WHERE document @@ '{ "ikey": { "$gt": [[[[]]]] } }';


SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_array', '{ "_id": 13, "ikey": "abcdefghijklmonpqrstuvwsyz" }');
SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_array', '{ "_id": 14, "ikey": 1 }');
SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_array', '{ "_id": 15, "ikey": { "$numberDecimal" : "1234567891011" } }');
SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_array', '{ "_id": 16, "ikey": true }');
SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_array', '{ "_id": 17, "ikey": [] }');
SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_array', '{ "_id": 18, "ikey": {} }');

SELECT document FROM helio_api.collection('db', 'index_truncation_tests_nested_array') WHERE document @@ '{ "ikey": { "$gt": 1 } }';
SELECT document FROM helio_api.collection('db', 'index_truncation_tests_nested_array') WHERE document @@ '{ "ikey": { "$gt": { "$numberDecimal" : "1234567891011" } } }';
SELECT document FROM helio_api.collection('db', 'index_truncation_tests_nested_array') WHERE document @@ '{ "ikey": { "$gt": false } }';
SELECT document FROM helio_api.collection('db', 'index_truncation_tests_nested_array') WHERE document @@ '{ "ikey": { "$gt": [] } }';
SELECT document FROM helio_api.collection('db', 'index_truncation_tests_nested_array') WHERE document @@ '{ "ikey": { "$in": [1, true] } }';
SELECT document FROM helio_api.collection('db', 'index_truncation_tests_nested_array') WHERE document @@ '{ "ikey": { "$nin": [1, true] } }';
SELECT document FROM helio_api.collection('db', 'index_truncation_tests_nested_array') WHERE document @@ '{ "ikey": { "$elemMatch": { "$in": [1, true] } } }'; 
SELECT document FROM helio_api.collection('db', 'index_truncation_tests_nested_array') WHERE document @@ '{ "ikey": { "$elemMatch": { "$in": [[[1]], [[true]]] } } }'; 
SELECT document FROM helio_api.collection('db', 'index_truncation_tests_nested_array') WHERE document @@ '{ "ikey": { "$elemMatch": { "$in": [[[1]], [[true]]], "$gt": [[{ "$numberDecimal" : "1234567891011" }]] } } }'; 
SELECT document FROM helio_api.collection('db', 'index_truncation_tests_nested_array') WHERE document @@ '{ "ikey": { "$all": [[[1]]] } }'; 
SELECT document FROM helio_api.collection('db', 'index_truncation_tests_nested_array') WHERE document @@ '{ "ikey": { "$all": [[[[]]]] } }'; 

SELECT document FROM helio_api.collection('db', 'index_truncation_tests_nested_array') WHERE document @@ '{ "ikey": { "$ne": { "$numberDecimal" : "1234567891011" } } }';
SELECT document FROM helio_api.collection('db', 'index_truncation_tests_nested_array') WHERE document @@ '{ "ikey": { "$in": [[[1]], [[true]]] } }';
SELECT document FROM helio_api.collection('db', 'index_truncation_tests_nested_array') WHERE document @@ '{ "ikey": { "$nin": [[[1]], [[true]]] } }';
SELECT document FROM helio_api.collection('db', 'index_truncation_tests_nested_array') WHERE document @@ '{ "ikey": { "$ne": [[{ "$numberDecimal" : "1234567891011" }]] } }';


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
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM helio_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 1, "ikey" : { "a" : ["abcdefghijklmonpqrstuvwsyz"]}}', 'ikey', false, true, true, 50) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM helio_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 2, "ikey" : { "a" : [1]}}', 'ikey', false, true, true, 50) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM helio_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 3, "ikey" : { "a" : [{ "$numberDecimal" : "1234567891011" }]}}', 'ikey', false, true, true, 50) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM helio_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 3, "ikey" : { "a" : [true]}}', 'ikey', false, true, true, 50) term;

SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM helio_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 1, "ikey" : { "a" : ["abcdefghijklmonpqrstuvwsyz"]}}', 'ikey', false, true, true, 35) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM helio_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 2, "ikey" : { "a" : [1]}}', 'ikey', false, true, true, 35) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM helio_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 3, "ikey" : { "a" : [{ "$numberDecimal" : "1234567891011" }]}}', 'ikey', false, true, true, 35) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM helio_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 3, "ikey" : { "a" : [true]}}', 'ikey', false, true, true, 35) term;

-- wild card
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM helio_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 1, "ikey" : { "a" : ["abcdefghijklmonpqrstuvwsyz"]}}', 'ikey', true, true, true, 50) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM helio_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 2, "ikey" : { "a" : [1]}}', 'ikey', true, true, true, 50) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM helio_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 3, "ikey" : { "a" : [{ "$numberDecimal" : "1234567891011" }]]}}', 'ikey', true, true, true, 50) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM helio_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 3, "ikey" : { "a" : [true]}}', 'ikey', true, true, true, 50) term;

SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM helio_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 1, "ikey" : { "a" : ["abcdefghijklmonpqrstuvwsyz"]}}', 'ikey', true, true, true, 35) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM helio_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 2, "ikey" : { "a" : [1]}}', 'ikey', true, true, true, 35) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM helio_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 3, "ikey" : { "a" : [{ "$numberDecimal" : "1234567891011" }]}}', 'ikey', true, true, true, 35) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM helio_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 3, "ikey" : { "a" : [true]}}', 'ikey', true, true, true, 35) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM helio_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 1, "ikey" : { "a" : ["abcdefghijklmonpqrstuvwsyz"]}}', 'ikey', false, true, true, 50) term;


SELECT helio_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "index_truncation_tests_nested_documents", "indexes": [ { "key": { "ikey": 1 }, "name": "ikey_1", "enableLargeIndexKeys": true } ] }');

-- now we have an index with truncation enabled and 100 chars.
\d helio_data.documents_10401;


-- now insert some documents that does exceed the index term limit.
SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_documents', '{ "_id": 1, "ikey": { "a" : ["abcdefghijklmonpqrstuvwsyz"]} }');
SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_documents', '{ "_id": 2, "ikey": { "a" : [1]} }');
SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_documents', '{ "_id": 3, "ikey": { "a" : [{ "$numberDecimal" : "1234567891011" }]} }');
SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_documents', '{ "_id": 4, "ikey": { "a" : [true]} }');
SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_documents', '{ "_id": 5, "ikey": { "a" : [[]}] }');
SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_documents', '{ "_id": 6, "ikey": { "a" : [{}]} }');

SELECT document FROM helio_api.collection('db', 'index_truncation_tests_nested_documents') WHERE document @@ '{ "ikey.a": { "$gt": [1] } }';
SELECT document FROM helio_api.collection('db', 'index_truncation_tests_nested_documents') WHERE document @@ '{ "ikey.a": { "$gt": [false] } }';
SELECT document FROM helio_api.collection('db', 'index_truncation_tests_nested_documents') WHERE document @@ '{ "ikey.a": { "$gt": [[]] } }';

-- now insert some documents that does exceed the index term limit.
SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_documents', '{ "_id": 7, "ikey": { "a" : [["abcdefghijklmonpqrstuvwsyz"]]} }');
SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_documents', '{ "_id": 8, "ikey": { "a" : [[1]]} }');
SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_documents', '{ "_id": 9, "ikey": { "a" : [[{ "$numberDecimal" : "1234567891011" }]]} }');
SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_documents', '{ "_id": 10, "ikey": { "a" : [[true]]} }');
SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_documents', '{ "_id": 11, "ikey": { "a" : [[[]]]} }');
SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_documents', '{ "_id": 12, "ikey": { "a" : [[{}]]} }');

SELECT document FROM helio_api.collection('db', 'index_truncation_tests_nested_documents') WHERE document @@ '{ "ikey.a": { "$gt": [[1]] } }';
SELECT document FROM helio_api.collection('db', 'index_truncation_tests_nested_documents') WHERE document @@ '{ "ikey.a": { "$gt": [[{ "$numberDecimal" : "1234567891011" }]] } }';
SELECT document FROM helio_api.collection('db', 'index_truncation_tests_nested_documents') WHERE document @@ '{ "ikey.a": { "$gt": [[false]] } }';
SELECT document FROM helio_api.collection('db', 'index_truncation_tests_nested_documents') WHERE document @@ '{ "ikey.a": { "$gt": [[[]]] } }';


SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_documents', '{ "_id": 13, "ikey": "abcdefghijklmonpqrstuvwsyz" }');
SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_documents', '{ "_id": 14, "ikey": 1 }');
SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_documents', '{ "_id": 15, "ikey": { "$numberDecimal" : "1234567891011" } }');
SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_documents', '{ "_id": 16, "ikey": true }');
SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_documents', '{ "_id": 17, "ikey": [] }');
SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_documents', '{ "_id": 18, "ikey": {} }');

SELECT document FROM helio_api.collection('db', 'index_truncation_tests_nested_documents') WHERE document @@ '{ "ikey.a": { "$gt": 1 } }';
SELECT document FROM helio_api.collection('db', 'index_truncation_tests_nested_documents') WHERE document @@ '{ "ikey.a": { "$gt": { "$numberDecimal" : "1234567891011" } } }';
SELECT document FROM helio_api.collection('db', 'index_truncation_tests_nested_documents') WHERE document @@ '{ "ikey.a": { "$gt": false } }';
SELECT document FROM helio_api.collection('db', 'index_truncation_tests_nested_documents') WHERE document @@ '{ "ikey.a": { "$gt": [] } }';
SELECT document FROM helio_api.collection('db', 'index_truncation_tests_nested_documents') WHERE document @@ '{ "ikey.a": { "$in": [1, true] } }';
SELECT document FROM helio_api.collection('db', 'index_truncation_tests_nested_documents') WHERE document @@ '{ "ikey.a": { "$nin": [1, true] } }';
SELECT document FROM helio_api.collection('db', 'index_truncation_tests_nested_documents') WHERE document @@ '{ "ikey.a": { "$elemMatch": { "$in": [1, true] } } }'; 
SELECT document FROM helio_api.collection('db', 'index_truncation_tests_nested_documents') WHERE document @@ '{ "ikey.a": { "$elemMatch": { "$in": [[1], [true]] } } }'; 
SELECT document FROM helio_api.collection('db', 'index_truncation_tests_nested_documents') WHERE document @@ '{ "ikey.a": { "$elemMatch": { "$in": [1, "abcdefghijklmonpqrstuvwsyz"] } } }'; 
SELECT document FROM helio_api.collection('db', 'index_truncation_tests_nested_documents') WHERE document @@ '{ "ikey.a": { "$elemMatch": { "$in": [[1], ["abcdefghijklmonpqrstuvwsyz"]] } } }'; 
SELECT document FROM helio_api.collection('db', 'index_truncation_tests_nested_documents') WHERE document @@ '{ "ikey.a": { "$elemMatch": { "$in": [[1], [true]], "$gt": [{ "$numberDecimal" : "1234567891011" }] } } }'; 
SELECT document FROM helio_api.collection('db', 'index_truncation_tests_nested_documents') WHERE document @@ '{ "ikey.a": { "$all": [[1]] } }'; 
SELECT document FROM helio_api.collection('db', 'index_truncation_tests_nested_documents') WHERE document @@ '{ "ikey.a": { "$all": [[[]]] } }'; 

SELECT document FROM helio_api.collection('db', 'index_truncation_tests_nested_documents') WHERE document @@ '{ "ikey.a": { "$ne": { "$numberDecimal" : "1234567891011" } } }';
SELECT document FROM helio_api.collection('db', 'index_truncation_tests_nested_documents') WHERE document @@ '{ "ikey.a": { "$in": [[1], [true]] } }';
SELECT document FROM helio_api.collection('db', 'index_truncation_tests_nested_documents') WHERE document @@ '{ "ikey.a": { "$nin": [[1], [true]] } }';
SELECT document FROM helio_api.collection('db', 'index_truncation_tests_nested_documents') WHERE document @@ '{ "ikey.a": { "$ne": [{ "$numberDecimal" : "1234567891011" }] } }';


-- _S_TEMP_STR_ specific tests

SET helio_api.enableIndexTermTruncationOnNestedObjects to off;
set helio_api.indexTermLimitOverride to 250;


SELECT helio_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "_S_TEMP_STR__index_truncation_tests_nested", "indexes": [ { "key": { "$**" : 1 }, "name": "wildcardIndex",  "enableLargeIndexKeys": true  } ] }', true);
SELECT helio_distributed_test_helpers.drop_primary_key('db', '_S_TEMP_STR__index_truncation_tests_nested');


-- 1. Failes without nested object truncation support
SELECT helio_api.insert_one('db', '_S_TEMP_STR__index_truncation_tests_nested', '
{
    "_id" : 1,
    "data": {
        "$binary": "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",
        "$type": "00"
    },
    "lastUpdated": {
        "sourceSystemTimestamp": "2016-09-19T06:10:23.400Z"
    },
    "kafkaPartition": 5,
    "individualIdentifier": "cdb:4:150813964:CS:07148670016266119100001",
    "kafkaTimestamp": {
        "$date": 1693431086173
    },
    "sourceIndividual": null,
    "active": false,
    "upsertTimestamp": {
        "$date": 1720803035593
    },
    "kafkaOffset": 21,
    "memberships": [
        {
            "sourceSystemAttributes": {
                "cdb": {
                    "referenceData": {
                        "coverageTypeCode": null
                    },
                    "nonCs": {
                        "legalEntityName": null
                    }
                }
            },
            "cancelReasonType": null,
            "customerPurchaseIdentifier": "0714867GIW",
            "enrolleeSourceId": "07148670016266119100001",
            "individualIdentifier": "cdb:4:150813964:CS:07148670016266119100001",
            "membershipGroupData": null,
            "legacyAttributes": {
                "stateIssueCode": ""
            },
            "eligibilitySystemType": {
                "code": "01"
            },
            "coverageStatus": {
                "code": "A",
                "description": null
            },
            "terminationDate": "2010-12-31",
            "subscriberIndividualIdentifier": null,
            "panelNumber": null,
            "enrolleeMemberFacingIdentifier": null,
            "claimSystemType": null,
            "medicareAndRetirementMembershipProfiles": null,
            "organizationIdentifier": null,
            "subscriberMemberFacingIdentifier": null,
            "customerAccountIdentifier": "0714867",
            "segmentId": null,
            "enrolleeSourceCode": "CS",
            "plan": {
                "planCode": ""
            },
            "areaGroup": null,
            "packageBenefitPlanCode": null,
            "product": {
                "healthCoverageType": {
                    "code": "G"
                }
            },
            "active": true,
            "applicationIdentifier": null,
            "divisionCode": "",
            "site": null,
            "productCode": "",
            "membershipIdentifier": "cdb:health:0714867:2009-01-01:G:IW",
            "organization": {
                "planVariationCode": "0013",
                "reportingCode": "0013"
            },
            "hContractId": null,
            "customerAccount": {
                "lineOfBusiness": {
                    "code": "E&I"
                },
                "policySuffixCode": null,
                "planCoverageIdentifier": "cdb:CS:0714867:0013:0013:G",
                "businessArrangement": {
                    "code": "A"
                },
                "customerAccountIdentifier": "0714867",
                "sharedArrangement": {
                    "code": "00"
                },
                "obligor": {
                    "code": "01"
                },
                "purchasePlanIdentifier": null
            },
            "memberMarketNumber": null,
            "subscriberEnrolleeSourceId": null,
            "effectiveDate": "2009-01-01"
        },
        {
            "sourceSystemAttributes": {
                "cdb": {
                    "referenceData": {
                        "coverageTypeCode": null
                    },
                    "nonCs": {
                        "legalEntityName": null
                    }
                }
            },
            "cancelReasonType": null,
            "customerPurchaseIdentifier": "0714867GIW",
            "enrolleeSourceId": "07148670016266119100001",
            "individualIdentifier": "cdb:4:150813964:CS:07148670016266119100001",
            "membershipGroupData": null,
            "legacyAttributes": {
                "stateIssueCode": ""
            },
            "eligibilitySystemType": {
                "code": "01"
            },
            "coverageStatus": {
                "code": "A",
                "description": null
            },
            "terminationDate": "2013-12-31",
            "subscriberIndividualIdentifier": null,
            "panelNumber": null,
            "enrolleeMemberFacingIdentifier": null,
            "claimSystemType": null,
            "medicareAndRetirementMembershipProfiles": null,
            "organizationIdentifier": null,
            "subscriberMemberFacingIdentifier": null,
            "customerAccountIdentifier": "0714867",
            "segmentId": null,
            "enrolleeSourceCode": "CS",
            "plan": {
                "planCode": ""
            },
            "areaGroup": null,
            "packageBenefitPlanCode": null,
            "product": {
                "healthCoverageType": {
                    "code": "G"
                }
            },
            "active": true,
            "applicationIdentifier": null,
            "divisionCode": "",
            "site": null,
            "productCode": "",
            "membershipIdentifier": "cdb:health:0714867:2011-01-01:G:IW",
            "organization": {
                "planVariationCode": "0013",
                "reportingCode": "0013"
            },
            "hContractId": null,
            "customerAccount": {
                "lineOfBusiness": {
                    "code": "E&I"
                },
                "policySuffixCode": null,
                "planCoverageIdentifier": "cdb:CS:0714867:0013:0013:G",
                "businessArrangement": {
                    "code": "A"
                },
                "customerAccountIdentifier": "0714867",
                "sharedArrangement": {
                    "code": "00"
                },
                "obligor": {
                    "code": "01"
                },
                "purchasePlanIdentifier": null
            },
            "memberMarketNumber": null,
            "subscriberEnrolleeSourceId": null,
            "effectiveDate": "2011-01-01"
        }
    ],
    "sourceSystemCode": "CDB",
    "rule": {
        "and": [
            {
                "or": [
                    {
                        "permission": "api-source-system-read-all"
                    },
                    {
                        "permission": "0"
                    }
                ]
            },
            {
                "securitySourceSystemCode": "cdb"
            }
        ]
    }
}');


SET helio_api.enableIndexTermTruncationOnNestedObjects to on;

-- 2 a. Terms generated with nested object index term truncation support

SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM helio_distributed_test_helpers.gin_bson_get_single_path_generated_terms(
  '{
    "_id": 1, 
    "memberships" : [
        {
            "sourceSystemAttributes": {
                "cdb": {
                    "referenceData": {
                        "coverageTypeCode": null
                    },
                    "nonCs": {
                        "legalEntityName": null
                    }
                }
            },
            "cancelReasonType": null,
            "customerPurchaseIdentifier": "0714867GIW",
            "enrolleeSourceId": "07148670016266119100001",
            "individualIdentifier": "cdb:4:150813964:CS:07148670016266119100001",
            "membershipGroupData": null,
            "legacyAttributes": {
                "stateIssueCode": ""
            },
            "eligibilitySystemType": {
                "code": "01"
            },
            "coverageStatus": {
                "code": "A",
                "description": null
            },
            "terminationDate": "2010-12-31",
            "subscriberIndividualIdentifier": null,
            "panelNumber": null,
            "enrolleeMemberFacingIdentifier": null,
            "claimSystemType": null,
            "medicareAndRetirementMembershipProfiles": null,
            "organizationIdentifier": null,
            "subscriberMemberFacingIdentifier": null,
            "customerAccountIdentifier": "0714867",
            "segmentId": null,
            "enrolleeSourceCode": "CS",
            "plan": {
                "planCode": ""
            },
            "areaGroup": null,
            "packageBenefitPlanCode": null,
            "product": {
                "healthCoverageType": {
                    "code": "G"
                }
            },
            "active": true,
            "applicationIdentifier": null,
            "divisionCode": "",
            "site": null,
            "productCode": "",
            "membershipIdentifier": "cdb:health:0714867:2009-01-01:G:IW",
            "organization": {
                "planVariationCode": "0013",
                "reportingCode": "0013"
            },
            "hContractId": null,
            "customerAccount": {
                "lineOfBusiness": {
                    "code": "E&I"
                },
                "policySuffixCode": null,
                "planCoverageIdentifier": "cdb:CS:0714867:0013:0013:G",
                "businessArrangement": {
                    "code": "A"
                },
                "customerAccountIdentifier": "0714867",
                "sharedArrangement": {
                    "code": "00"
                },
                "obligor": {
                    "code": "01"
                },
                "purchasePlanIdentifier": null
            },
            "memberMarketNumber": null,
            "subscriberEnrolleeSourceId": null,
            "effectiveDate": "2009-01-01"
        },
        {
            "sourceSystemAttributes": {
                "cdb": {
                    "referenceData": {
                        "coverageTypeCode": null
                    },
                    "nonCs": {
                        "legalEntityName": null
                    }
                }
            },
            "cancelReasonType": null,
            "customerPurchaseIdentifier": "0714867GIW",
            "enrolleeSourceId": "07148670016266119100001",
            "individualIdentifier": "cdb:4:150813964:CS:07148670016266119100001",
            "membershipGroupData": null,
            "legacyAttributes": {
                "stateIssueCode": ""
            },
            "eligibilitySystemType": {
                "code": "01"
            },
            "coverageStatus": {
                "code": "A",
                "description": null
            },
            "terminationDate": "2013-12-31",
            "subscriberIndividualIdentifier": null,
            "panelNumber": null,
            "enrolleeMemberFacingIdentifier": null,
            "claimSystemType": null,
            "medicareAndRetirementMembershipProfiles": null,
            "organizationIdentifier": null,
            "subscriberMemberFacingIdentifier": null,
            "customerAccountIdentifier": "0714867",
            "segmentId": null,
            "enrolleeSourceCode": "CS",
            "plan": {
                "planCode": ""
            },
            "areaGroup": null,
            "packageBenefitPlanCode": null,
            "product": {
                "healthCoverageType": {
                    "code": "G"
                }
            },
            "active": true,
            "applicationIdentifier": null,
            "divisionCode": "",
            "site": null,
            "productCode": "",
            "membershipIdentifier": "cdb:health:0714867:2011-01-01:G:IW",
            "organization": {
                "planVariationCode": "0013",
                "reportingCode": "0013"
            },
            "hContractId": null,
            "customerAccount": {
                "lineOfBusiness": {
                    "code": "E&I"
                },
                "policySuffixCode": null,
                "planCoverageIdentifier": "cdb:CS:0714867:0013:0013:G",
                "businessArrangement": {
                    "code": "A"
                },
                "customerAccountIdentifier": "0714867",
                "sharedArrangement": {
                    "code": "00"
                },
                "obligor": {
                    "code": "01"
                },
                "purchasePlanIdentifier": null
            },
            "memberMarketNumber": null,
            "subscriberEnrolleeSourceId": null,
            "effectiveDate": "2011-01-01"
        }
    ]
  }'
, 'memberships', true, true, true, 1000) term;

-- 2 b. Terms generated with nested object index term truncation support

SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM helio_distributed_test_helpers.gin_bson_get_single_path_generated_terms(
  '{
    "_id": 1, 
    "memberships" : [
        {
            "sourceSystemAttributes": {
                "cdb": {
                    "referenceData": {
                        "coverageTypeCode": null
                    },
                    "nonCs": {
                        "legalEntityName": null
                    }
                }
            },
            "cancelReasonType": null,
            "customerPurchaseIdentifier": "0714867GIW",
            "enrolleeSourceId": "07148670016266119100001",
            "individualIdentifier": "cdb:4:150813964:CS:07148670016266119100001",
            "membershipGroupData": null,
            "legacyAttributes": {
                "stateIssueCode": ""
            },
            "eligibilitySystemType": {
                "code": "01"
            },
            "coverageStatus": {
                "code": "A",
                "description": null
            },
            "terminationDate": "2010-12-31",
            "subscriberIndividualIdentifier": null,
            "panelNumber": null,
            "enrolleeMemberFacingIdentifier": null,
            "claimSystemType": null,
            "medicareAndRetirementMembershipProfiles": null,
            "organizationIdentifier": null,
            "subscriberMemberFacingIdentifier": null,
            "customerAccountIdentifier": "0714867",
            "segmentId": null,
            "enrolleeSourceCode": "CS",
            "plan": {
                "planCode": ""
            },
            "areaGroup": null,
            "packageBenefitPlanCode": null,
            "product": {
                "healthCoverageType": {
                    "code": "G"
                }
            },
            "active": true,
            "applicationIdentifier": null,
            "divisionCode": "",
            "site": null,
            "productCode": "",
            "membershipIdentifier": "cdb:health:0714867:2009-01-01:G:IW",
            "organization": {
                "planVariationCode": "0013",
                "reportingCode": "0013"
            },
            "hContractId": null,
            "customerAccount": {
                "lineOfBusiness": {
                    "code": "E&I"
                },
                "policySuffixCode": null,
                "planCoverageIdentifier": "cdb:CS:0714867:0013:0013:G",
                "businessArrangement": {
                    "code": "A"
                },
                "customerAccountIdentifier": "0714867",
                "sharedArrangement": {
                    "code": "00"
                },
                "obligor": {
                    "code": "01"
                },
                "purchasePlanIdentifier": null
            },
            "memberMarketNumber": null,
            "subscriberEnrolleeSourceId": null,
            "effectiveDate": "2009-01-01"
        },
        {
            "sourceSystemAttributes": {
                "cdb": {
                    "referenceData": {
                        "coverageTypeCode": null
                    },
                    "nonCs": {
                        "legalEntityName": null
                    }
                }
            },
            "cancelReasonType": null,
            "customerPurchaseIdentifier": "0714867GIW",
            "enrolleeSourceId": "07148670016266119100001",
            "individualIdentifier": "cdb:4:150813964:CS:07148670016266119100001",
            "membershipGroupData": null,
            "legacyAttributes": {
                "stateIssueCode": ""
            },
            "eligibilitySystemType": {
                "code": "01"
            },
            "coverageStatus": {
                "code": "A",
                "description": null
            },
            "terminationDate": "2013-12-31",
            "subscriberIndividualIdentifier": null,
            "panelNumber": null,
            "enrolleeMemberFacingIdentifier": null,
            "claimSystemType": null,
            "medicareAndRetirementMembershipProfiles": null,
            "organizationIdentifier": null,
            "subscriberMemberFacingIdentifier": null,
            "customerAccountIdentifier": "0714867",
            "segmentId": null,
            "enrolleeSourceCode": "CS",
            "plan": {
                "planCode": ""
            },
            "areaGroup": null,
            "packageBenefitPlanCode": null,
            "product": {
                "healthCoverageType": {
                    "code": "G"
                }
            },
            "active": true,
            "applicationIdentifier": null,
            "divisionCode": "",
            "site": null,
            "productCode": "",
            "membershipIdentifier": "cdb:health:0714867:2011-01-01:G:IW",
            "organization": {
                "planVariationCode": "0013",
                "reportingCode": "0013"
            },
            "hContractId": null,
            "customerAccount": {
                "lineOfBusiness": {
                    "code": "E&I"
                },
                "policySuffixCode": null,
                "planCoverageIdentifier": "cdb:CS:0714867:0013:0013:G",
                "businessArrangement": {
                    "code": "A"
                },
                "customerAccountIdentifier": "0714867",
                "sharedArrangement": {
                    "code": "00"
                },
                "obligor": {
                    "code": "01"
                },
                "purchasePlanIdentifier": null
            },
            "memberMarketNumber": null,
            "subscriberEnrolleeSourceId": null,
            "effectiveDate": "2011-01-01"
        }
    ]
  }'
, 'memberships', true, true, true, 100) term;


-- 3. Now insert the document with nested object index term truncation support

SELECT helio_api.insert_one('db', '_S_TEMP_STR__index_truncation_tests_nested', '
{
    "_id": 1, 
    "data": {
        "$binary": "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",
        "$type": "00"
    },
    "lastUpdated": {
        "sourceSystemTimestamp": "2016-09-19T06:10:23.400Z"
    },
    "kafkaPartition": 5,
    "individualIdentifier": "cdb:4:150813964:CS:07148670016266119100001",
    "kafkaTimestamp": {
        "$date": 1693431086173
    },
    "sourceIndividual": null,
    "active": false,
    "upsertTimestamp": {
        "$date": 1720803035593
    },
    "kafkaOffset": 21,
    "memberships": [
        {
            "sourceSystemAttributes": {
                "cdb": {
                    "referenceData": {
                        "coverageTypeCode": null
                    },
                    "nonCs": {
                        "legalEntityName": null
                    }
                }
            },
            "cancelReasonType": null,
            "customerPurchaseIdentifier": "0714867GIW",
            "enrolleeSourceId": "07148670016266119100001",
            "individualIdentifier": "cdb:4:150813964:CS:07148670016266119100001",
            "membershipGroupData": null,
            "legacyAttributes": {
                "stateIssueCode": ""
            },
            "eligibilitySystemType": {
                "code": "01"
            },
            "coverageStatus": {
                "code": "A",
                "description": null
            },
            "terminationDate": "2010-12-31",
            "subscriberIndividualIdentifier": null,
            "panelNumber": null,
            "enrolleeMemberFacingIdentifier": null,
            "claimSystemType": null,
            "medicareAndRetirementMembershipProfiles": null,
            "organizationIdentifier": null,
            "subscriberMemberFacingIdentifier": null,
            "customerAccountIdentifier": "0714867",
            "segmentId": null,
            "enrolleeSourceCode": "CS",
            "plan": {
                "planCode": ""
            },
            "areaGroup": null,
            "packageBenefitPlanCode": null,
            "product": {
                "healthCoverageType": {
                    "code": "G"
                }
            },
            "active": true,
            "applicationIdentifier": null,
            "divisionCode": "",
            "site": null,
            "productCode": "",
            "membershipIdentifier": "cdb:health:0714867:2009-01-01:G:IW",
            "organization": {
                "planVariationCode": "0013",
                "reportingCode": "0013"
            },
            "hContractId": null,
            "customerAccount": {
                "lineOfBusiness": {
                    "code": "E&I"
                },
                "policySuffixCode": null,
                "planCoverageIdentifier": "cdb:CS:0714867:0013:0013:G",
                "businessArrangement": {
                    "code": "A"
                },
                "customerAccountIdentifier": "0714867",
                "sharedArrangement": {
                    "code": "00"
                },
                "obligor": {
                    "code": "01"
                },
                "purchasePlanIdentifier": null
            },
            "memberMarketNumber": null,
            "subscriberEnrolleeSourceId": null,
            "effectiveDate": "2009-01-01"
        },
        {
            "sourceSystemAttributes": {
                "cdb": {
                    "referenceData": {
                        "coverageTypeCode": null
                    },
                    "nonCs": {
                        "legalEntityName": null
                    }
                }
            },
            "cancelReasonType": null,
            "customerPurchaseIdentifier": "0714867GIW",
            "enrolleeSourceId": "07148670016266119100001",
            "individualIdentifier": "cdb:4:150813964:CS:07148670016266119100001",
            "membershipGroupData": null,
            "legacyAttributes": {
                "stateIssueCode": ""
            },
            "eligibilitySystemType": {
                "code": "01"
            },
            "coverageStatus": {
                "code": "A",
                "description": null
            },
            "terminationDate": "2013-12-31",
            "subscriberIndividualIdentifier": null,
            "panelNumber": null,
            "enrolleeMemberFacingIdentifier": null,
            "claimSystemType": null,
            "medicareAndRetirementMembershipProfiles": null,
            "organizationIdentifier": null,
            "subscriberMemberFacingIdentifier": null,
            "customerAccountIdentifier": "0714867",
            "segmentId": null,
            "enrolleeSourceCode": "CS",
            "plan": {
                "planCode": ""
            },
            "areaGroup": null,
            "packageBenefitPlanCode": null,
            "product": {
                "healthCoverageType": {
                    "code": "G"
                }
            },
            "active": true,
            "applicationIdentifier": null,
            "divisionCode": "",
            "site": null,
            "productCode": "",
            "membershipIdentifier": "cdb:health:0714867:2011-01-01:G:IW",
            "organization": {
                "planVariationCode": "0013",
                "reportingCode": "0013"
            },
            "hContractId": null,
            "customerAccount": {
                "lineOfBusiness": {
                    "code": "E&I"
                },
                "policySuffixCode": null,
                "planCoverageIdentifier": "cdb:CS:0714867:0013:0013:G",
                "businessArrangement": {
                    "code": "A"
                },
                "customerAccountIdentifier": "0714867",
                "sharedArrangement": {
                    "code": "00"
                },
                "obligor": {
                    "code": "01"
                },
                "purchasePlanIdentifier": null
            },
            "memberMarketNumber": null,
            "subscriberEnrolleeSourceId": null,
            "effectiveDate": "2011-01-01"
        }
    ],
    "sourceSystemCode": "CDB",
    "rule": {
        "and": [
            {
                "or": [
                    {
                        "permission": "api-source-system-read-all"
                    },
                    {
                        "permission": "0"
                    }
                ]
            },
            {
                "securitySourceSystemCode": "cdb"
            }
        ]
    }
}');


-- 4. Now issue a find query

SELECT document FROM bson_aggregation_find('db', '{ "find": "_S_TEMP_STR__index_truncation_tests_nested", "filter": { "memberships": { "$eq": 

{
            "sourceSystemAttributes": {
                "cdb": {
                    "referenceData": {
                        "coverageTypeCode": null
                    },
                    "nonCs": {
                        "legalEntityName": null
                    }
                }
            },
            "cancelReasonType": null,
            "customerPurchaseIdentifier": "0714867GIW",
            "enrolleeSourceId": "07148670016266119100001",
            "individualIdentifier": "cdb:4:150813964:CS:07148670016266119100001",
            "membershipGroupData": null,
            "legacyAttributes": {
                "stateIssueCode": ""
            },
            "eligibilitySystemType": {
                "code": "01"
            },
            "coverageStatus": {
                "code": "A",
                "description": null
            },
            "terminationDate": "2010-12-31",
            "subscriberIndividualIdentifier": null,
            "panelNumber": null,
            "enrolleeMemberFacingIdentifier": null,
            "claimSystemType": null,
            "medicareAndRetirementMembershipProfiles": null,
            "organizationIdentifier": null,
            "subscriberMemberFacingIdentifier": null,
            "customerAccountIdentifier": "0714867",
            "segmentId": null,
            "enrolleeSourceCode": "CS",
            "plan": {
                "planCode": ""
            },
            "areaGroup": null,
            "packageBenefitPlanCode": null,
            "product": {
                "healthCoverageType": {
                    "code": "G"
                }
            },
            "active": true,
            "applicationIdentifier": null,
            "divisionCode": "",
            "site": null,
            "productCode": "",
            "membershipIdentifier": "cdb:health:0714867:2009-01-01:G:IW",
            "organization": {
                "planVariationCode": "0013",
                "reportingCode": "0013"
            },
            "hContractId": null,
            "customerAccount": {
                "lineOfBusiness": {
                    "code": "E&I"
                },
                "policySuffixCode": null,
                "planCoverageIdentifier": "cdb:CS:0714867:0013:0013:G",
                "businessArrangement": {
                    "code": "A"
                },
                "customerAccountIdentifier": "0714867",
                "sharedArrangement": {
                    "code": "00"
                },
                "obligor": {
                    "code": "01"
                },
                "purchasePlanIdentifier": null
            },
            "memberMarketNumber": null,
            "subscriberEnrolleeSourceId": null,
            "effectiveDate": "2009-01-01"
        } 

} }, "projection": { "_id": 1 }, "sort": { "_id": 1 }, "skip": 0, "limit": 5 }');

-- 5. Index usage explain

EXPLAIN(ANALYZE ON, COSTS OFF, TIMING OFF, SUMMARY OFF, BUFFERS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "_S_TEMP_STR__index_truncation_tests_nested", "filter": { "memberships": { "$eq": 

{
            "sourceSystemAttributes": {
                "cdb": {
                    "referenceData": {
                        "coverageTypeCode": null
                    },
                    "nonCs": {
                        "legalEntityName": null
                    }
                }
            },
            "cancelReasonType": null,
            "customerPurchaseIdentifier": "0714867GIW",
            "enrolleeSourceId": "07148670016266119100001",
            "individualIdentifier": "cdb:4:150813964:CS:07148670016266119100001",
            "membershipGroupData": null,
            "legacyAttributes": {
                "stateIssueCode": ""
            },
            "eligibilitySystemType": {
                "code": "01"
            },
            "coverageStatus": {
                "code": "A",
                "description": null
            },
            "terminationDate": "2010-12-31",
            "subscriberIndividualIdentifier": null,
            "panelNumber": null,
            "enrolleeMemberFacingIdentifier": null,
            "claimSystemType": null,
            "medicareAndRetirementMembershipProfiles": null,
            "organizationIdentifier": null,
            "subscriberMemberFacingIdentifier": null,
            "customerAccountIdentifier": "0714867",
            "segmentId": null,
            "enrolleeSourceCode": "CS",
            "plan": {
                "planCode": ""
            },
            "areaGroup": null,
            "packageBenefitPlanCode": null,
            "product": {
                "healthCoverageType": {
                    "code": "G"
                }
            },
            "active": true,
            "applicationIdentifier": null,
            "divisionCode": "",
            "site": null,
            "productCode": "",
            "membershipIdentifier": "cdb:health:0714867:2009-01-01:G:IW",
            "organization": {
                "planVariationCode": "0013",
                "reportingCode": "0013"
            },
            "hContractId": null,
            "customerAccount": {
                "lineOfBusiness": {
                    "code": "E&I"
                },
                "policySuffixCode": null,
                "planCoverageIdentifier": "cdb:CS:0714867:0013:0013:G",
                "businessArrangement": {
                    "code": "A"
                },
                "customerAccountIdentifier": "0714867",
                "sharedArrangement": {
                    "code": "00"
                },
                "obligor": {
                    "code": "01"
                },
                "purchasePlanIdentifier": null
            },
            "memberMarketNumber": null,
            "subscriberEnrolleeSourceId": null,
            "effectiveDate": "2009-01-01"
        } 

} }, "projection": { "_id": 1 }, "sort": { "_id": 1 }, "skip": 0, "limit": 5 }');


SELECT document FROM bson_aggregation_find('db', '{ "find": "_S_TEMP_STR__index_truncation_tests_nested", "filter": { "memberships.organization.planVariationCode": { "$eq": 

"0013"

} }, "projection": { "_id": 1 }, "sort": { "_id": 1 }, "skip": 0, "limit": 5 }');

EXPLAIN(ANALYZE ON, COSTS OFF, TIMING OFF, SUMMARY OFF, BUFFERS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "_S_TEMP_STR__index_truncation_tests_nested", "filter": { "memberships.organization.planVariationCode": { "$eq": 

"0013"

} }, "projection": { "_id": 1 }, "sort": { "_id": 1 }, "skip": 0, "limit": 5 }');

SET helio_api.enableIndexTermTruncationOnNestedObjects to off;