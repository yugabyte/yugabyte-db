SET search_path TO helio_core,helio_api,helio_api_catalog,helio_api_internal;
SET citus.next_shard_id TO 1140000;
SET helio_api.next_collection_id TO 11400;
SET helio_api.next_collection_index_id TO 11400;


-- Set configs to something reasonable for testing.
set helio_api.indexTermLimitOverride to 100;
SET helio_api.enableIndexTermTruncationOnNestedObjects to on;




SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_objects', '{ "_id": 1, "ikey": [[[[[[[[[[1]]]]]]]]]] }');
SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_objects', '{ "_id": 2, "ikey": [[[[[[[[[[true]]]]]]]]]] }');
SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_objects', '{ "_id": 3, "ikey": [[[[[[[[[[{ "$numberDecimal" : "1234567891011" }]]]]]]]]]] }');
SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_objects', '{ "_id": 4, "ikey": [[[[[[[[[[{ "$maxKey": 1 }]]]]]]]]]] }');
SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_objects', '{ "_id": 5, "ikey": [[[[[[[[[[{ "$minKey": 1 }]]]]]]]]]] }');
SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_objects', '{ "_id": 6, "ikey": [[[[[[[[[[{ "$numberDouble" : "1234567891011" }]]]]]]]]]] }');
SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_objects', '{ "_id": 7, "ikey": [[[[[[[[[[ "abcdefghijklmopqrstuvwxyz" ]]]]]]]]]] }');

SELECT did, length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM ( SELECT helio_distributed_test_helpers.gin_bson_get_single_path_generated_terms(document, 'ikey', false, true, true, 115) term, document-> '_id' as did from helio_data.documents_11400) docs;
SELECT did, length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM ( SELECT helio_distributed_test_helpers.gin_bson_get_single_path_generated_terms(document, 'ikey', false, true, true, 120) term, document-> '_id' as did from helio_data.documents_11400) docs;


SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_objects_2', '{ "_id": 1, "ikey": { "a" : {"b" : { "c": { "d": { "e": { "f" : { "g" : { "h": 1}}}}}}}}}');
SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_objects_2', '{ "_id": 2, "ikey": { "a" : {"b" : { "c": { "d": { "e": { "f" : { "g" : { "h": true}}}}}}}}}');
SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_objects_2', '{ "_id": 3, "ikey": { "a" : {"b" : { "c": { "d": { "e": { "f" : { "g" : { "h": { "$numberDecimal" : "1234567891011" }}}}}}}}}}');
SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_objects_2', '{ "_id": 4, "ikey": { "a" : {"b" : { "c": { "d": { "e": { "f" : { "g" : { "h": { "$maxKey": 1 }}}}}}}}}}');
SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_objects_2', '{ "_id": 5, "ikey": { "a" : {"b" : { "c": { "d": { "e": { "f" : { "g" : { "h": { "$minKey": 1 }}}}}}}}}}');
SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_objects_2', '{ "_id": 6, "ikey": { "a" : {"b" : { "c": { "d": { "e": { "f" : { "g" : { "h": { "$numberDouble" : "1234567891011" }}}}}}}}}}');
SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_objects_2', '{ "_id": 7, "ikey": { "a" : {"b" : { "c": { "d": { "e": { "f" : { "g" : { "h": "abcdefghijklmopqrstuvwxyz"}}}}}}}}}');

SELECT did, length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM ( SELECT helio_distributed_test_helpers.gin_bson_get_single_path_generated_terms(document, 'ikey', false, true, true, 100) term, document-> '_id' as did from helio_data.documents_11401) docs;
SELECT did, length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM ( SELECT helio_distributed_test_helpers.gin_bson_get_single_path_generated_terms(document, 'ikey', false, true, true, 105) term, document-> '_id' as did from helio_data.documents_11401) docs;

SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_objects_3', '{ "_id": 1, "ikey": [{ "a" : [{"b" : [{ "c": [{ "d": [{ "e": [{ "f" : [{ "g" : [{ "h": 1}]}]}]}]}]}]}]}]}');
SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_objects_3', '{ "_id": 2, "ikey": [{ "a" : [{"b" : [{ "c": [{ "d": [{ "e": [{ "f" : [{ "g" : [{ "h": true}]}]}]}]}]}]}]}]}');
SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_objects_3', '{ "_id": 3, "ikey": [{ "a" : [{"b" : [{ "c": [{ "d": [{ "e": [{ "f" : [{ "g" : [{ "h": { "$numberDecimal" : "1234567891011" }}]}]}]}]}]}]}]}]}');
SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_objects_3', '{ "_id": 4, "ikey": [{ "a" : [{"b" : [{ "c": [{ "d": [{ "e": [{ "f" : [{ "g" : [{ "h": { "$maxKey": 1 }}]}]}]}]}]}]}]}]}');
SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_objects_3', '{ "_id": 5, "ikey": [{ "a" : [{"b" : [{ "c": [{ "d": [{ "e": [{ "f" : [{ "g" : [{ "h": { "$minKey": 1 }}]}]}]}]}]}]}]}]}');
SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_objects_3', '{ "_id": 6, "ikey": [{ "a" : [{"b" : [{ "c": [{ "d": [{ "e": [{ "f" : [{ "g" : [{ "h": { "$numberDouble" : "1234567891011" }}]}]}]}]}]}]}]}]}');
SELECT helio_api.insert_one('db', 'index_truncation_tests_nested_objects_3', '{ "_id": 7, "ikey": [{ "a" : [{"b" : [{ "c": [{ "d": [{ "e": [{ "f" : [{ "g" : [{ "h": "abcdefghijklmopqrstuvwxyz"}]}]}]}]}]}]}]}]}');

SELECT did, length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM ( SELECT helio_distributed_test_helpers.gin_bson_get_single_path_generated_terms(document, 'ikey', false, true, true, 165) term, document-> '_id' as did from helio_data.documents_11402) docs;
SELECT did, length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM ( SELECT helio_distributed_test_helpers.gin_bson_get_single_path_generated_terms(document, 'ikey', false, true, true, 170) term, document-> '_id' as did from helio_data.documents_11402) docs;

SELECT did, length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM ( SELECT helio_distributed_test_helpers.gin_bson_get_single_path_generated_terms(document, 'ikey', true, true, true, 165) term, document-> '_id' as did from helio_data.documents_11402) docs;
SELECT did, length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM ( SELECT helio_distributed_test_helpers.gin_bson_get_single_path_generated_terms(document, 'ikey', true, true, true, 170) term, document-> '_id' as did from helio_data.documents_11402) docs;

