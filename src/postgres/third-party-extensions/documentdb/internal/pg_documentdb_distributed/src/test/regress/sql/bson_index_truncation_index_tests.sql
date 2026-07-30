SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 640000;
SET documentdb.next_collection_id TO 6400;
SET documentdb.next_collection_index_id TO 6400;

-- Set configs to something reasonable for testing.
set documentdb.indexTermLimitOverride to 100;

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "index_truncation_tests", "indexes": [ { "key": { "ikey": 1 }, "name": "ikey_1", "enableLargeIndexKeys": true } ] }');

-- now we have an index with truncation enabled and 100 chars.
\d documentdb_data.documents_6400;



--------------------------
---- DATA TYPE: UTF8
--------------------------

SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 13, "ikey" : "this is another string that does violate the index term limit as it goes much over the 100 character limit after strings and changes the end"}', '', true, true, false, 100) term;

SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 13, "ikey" : "this is another string that does violate the index term limit as it goes much over the 100 character limit after strings and changes the end"}', 'ikey', false, true, false, 100) term;

-- now insert some documents that don't exceed the index term limit.
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{ "_id": 1, "ikey": "this is a string that does not violate the index term limit" }');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{ "_id": 2, "ikey": "this is another string that doesnt violate the index term limit" }');

-- now insert some documents that technically do violate the index term limit.
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{ "_id": 3, "ikey": "this is a string that does violate the index term limit as it goes much over the 100 character limit of strings" }');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{ "_id": 4, "ikey": "this is another string that does violate the index term limit as it goes much over the 100 character limit of strings" }');

-- only changes beyond the index term limit
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{ "_id": 5, "ikey": "this is a string that does violate the index term limit as it goes much over the 100 character limit of strings and changes the end" }');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{ "_id": 6, "ikey": "this is another string that does violate the index term limit as it goes much over the 100 character limit of strings and changes the end" }');

-- this is less than _id: 6
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{ "_id": 7, "ikey": "this is another string that does violate the index term limit as it goes much over the 100 character limit in strings and changes the end" }');

-- this is less than _id: 7
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{ "_id": 8, "ikey": "this is another string that does violate the index term limit as it goes much over the 100 character limit after strings and changes the end" }');

-- now that they've succeeded query all of them.
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": "this is a string that does not violate the index term limit" }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": "this is another string that doesnt violate the index term limit" }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": "this is a string that does violate the index term limit as it goes much over the 100 character limit of strings" }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": "this is another string that does violate the index term limit as it goes much over the 100 character limit of strings" }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": "this is a string that does violate the index term limit as it goes much over the 100 character limit of strings and changes the end" }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": "this is another string that does violate the index term limit as it goes much over the 100 character limit of strings and changes the end" }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": "this is another string that does violate the index term limit as it goes much over the 100 character limit in strings and changes the end" }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": "this is another string that does violate the index term limit as it goes much over the 100 character limit after strings and changes the end" }';

-- test $gt

-- all records
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gt": "this is a" } }';

-- all "this is another string ..."
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gt": "this is aa" } }';

-- no strings
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gt": "this is another string that doesnt violate the index term limit" } }';

-- all long strings with 'another'
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gt": "this is another string that does violate the index term limit" } }';

-- only the string that has the change in the end & the one that doesnt' violate the index term limit.
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gt": "this is another string that does violate the index term limit as it goes much over the 100 character limit of strings" } }';

-- only the one with 'in' and 'of' in the suffix & the one that doesn't violate the index term limit.
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gt": "this is another string that does violate the index term limit as it goes much over the 100 character limit after strings and changes the end" } }';

-- test $gte

-- all records
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gte": "this is a" } }';

-- all "this is another string ..."
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gte": "this is aa" } }';

-- unlike $gt with no strings - this returns the equality on that exact string.
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gte": "this is another string that doesnt violate the index term limit" } }';

-- all strings with 'another' (unlike $gt since we have $eq as well)
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gte": "this is another string that does violate the index term limit" } }';

-- all strings but this one
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gte": "this is another string that does violate the index term limit as it goes much over the 100 character limit of strings" } }';

-- all but 'in' and 'of' in the suffix & the one that doesn't violate the index term limit.
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gte": "this is another string that does violate the index term limit as it goes much over the 100 character limit after strings and changes the end" } }';


-- test $lt

-- no records
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$lt": "this is a" } }';

-- all records
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$lt": "this is b" } }';

-- all that are not "this is another string ..."
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$lt": "this is aa" } }';

-- all that are not "this is another string ..."
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$lt": "this is another string that does violate the index term limit" } }';

-- Matches all that are "this is a string". Also matches all the 'another' strings that are 'after', 'in', but not 'of'
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$lt": "this is another string that does violate the index term limit as it goes much over the 100 character limit of strings" } }';

-- Matches all that are "this is a string".
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$lt": "this is another string that does violate the index term limit as it goes much over the 100 character limit after strings and changes the end" } }';


-- test $lte

-- no records
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$lte": "this is a" } }';

-- all records
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$lte": "this is b" } }';

-- all that are not "this is another string ..."
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$lte": "this is aa" } }';

-- all that are not "this is another string ..."
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$lte": "this is another string that does violate the index term limit" } }';

-- All strings except 'this is another string that doesnt'
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$lte": "this is another string that does violate the index term limit as it goes much over the 100 character limit of strings" } }';

-- Matches all that are "this is a string" & equality on exact match on the string.
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$lte": "this is another string that does violate the index term limit as it goes much over the 100 character limit after strings and changes the end" } }';

-- $range 

-- repeat checks with $gt/$lt
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gt": "this is a", "$lt": "this is an" } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gt": "this is a", "$lte": "this is a string that does not violate the index term limit" } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gt": "this is a", "$lte": "this is another string that does not violate the index term limit" } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gt": "this is a", "$lte": "this is another string that does violate the index term limit as it goes much over the 100 character limit after strings and changes the end" } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gte": "this is another string that does violate the index term limit as it goes much over the 100 character limit after strings and changes the end", "$lt": "this is another string that does violate the index term limit as it goes much over the 100 character limit of strings and changes the end" } }';

-- $in
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$in": [ "this is a string that does not violate the index term limit", "this is a string that does violate the index term limit as it goes much over the 100 character limit of strings" ] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$in": [ "this is a string that does not violate the index term limit", "this is a string that does violate the index term limit as it goes much over the 100 character limit of strings", "this is another string that does violate the index term limit as it goes much over the 100 character limit after strings and changes the end" ] } }';

-- $ne
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$ne": "this is a string that does violate the index term limit as it goes much over the 100 character limit of strings" } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$ne": "this is another string that does violate the index term limit as it goes much over the 100 character limit after strings and changes the end" } }';

-- $nin
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$nin": [ "this is a string that does not violate the index term limit", "this is a string that does violate the index term limit as it goes much over the 100 character limit of strings" ] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$nin": [ "this is a string that does not violate the index term limit", "this is a string that does violate the index term limit as it goes much over the 100 character limit of strings", "this is another string that does violate the index term limit as it goes much over the 100 character limit after strings and changes the end" ] } }';

-- $regex scenarios
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$in": [ "this is a string that does not violate the index term limit", { "$regex": "the end$", "$options": "" } ] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$regex": "the end$", "$options": "" } }';

-- $all scenarios
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$all": [ "this is a string that does not violate the index term limit", { "$regex": "\\s+", "$options": "" } ] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$all": [ { "$regex": "\\d+", "$options": "" }, { "$regex": "\\s+another\\s+", "$options": "" } ] } }';

SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$all": [ "this is a string that does not violate the index term limit" ] } }';


--------------------------
---- DATA TYPE: Array
--------------------------

-- with  60 byte truncation limit, the real limit is 60-21 = 39 bytes. We can tolerate any entries over 39 bytes as long as they're also marked truncated.
-- the base structural overhead for an array is 5 (bson Doc) + 2 (path) + 1 (typecode) + 5 (array header) = 13 bytes
-- each path takes 2-4 characters (index as a string), 1 byte (type code) + path Value (variable) = 3-5 bytes + valueLength

-- track truncation with simple int4

-- empty array (13 bytes)
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 13, "ikey" : [ ]}'::bson, 'ikey', false, true, true, 60) term;

-- int array: int overhead = 3 + sizeof(int) = 7

-- Expectation is 13 bytes of overhead + (2 * 7) = 27 bytes 
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms(FORMAT('{"_id": 13, "ikey" : [ 1234 %s ]}', repeat(', 1234', 1))::bson, 'ikey', false, true, true, 60) term;

-- Expectation is 13 bytes of overhead + (3 * 7) = 34 bytes 
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms(FORMAT('{"_id": 13, "ikey" : [ 1234 %s ]}', repeat(', 1234', 2))::bson, 'ikey', false, true, true, 60) term;

-- Expectation is 13 bytes of overhead + (4 * 7) = 41 bytes -> 41 bytes and truncated
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms(FORMAT('{"_id": 13, "ikey" : [ 1234 %s ]}', repeat(', 1234', 3))::bson, 'ikey', false, true, true, 60) term;

-- Expectation is 13 bytes of overhead + (5 * 7) = 51 bytes -> 41 bytes and truncated
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms(FORMAT('{"_id": 13, "ikey" : [ 1234 %s ]}', repeat(', 1234', 4))::bson, 'ikey', false, true, true, 60) term;

-- track truncation with bools: size overhead = 3 + sizeof(bool) = 4

-- Expectation is 13 bytes of overhead + (4 * 4) = 29 bytes 
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms(FORMAT('{"_id": 13, "ikey" : [ false %s ]}', repeat(', true', 3))::bson, 'ikey', false, true, true, 60) term;

-- Expectation is 13 bytes of overhead + (5 * 4) = 33 bytes 
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms(FORMAT('{"_id": 13, "ikey" : [ false %s ]}', repeat(', false', 4))::bson, 'ikey', false, true, true, 60) term;

-- Expectation is 13 bytes of overhead + (6 * 4) = 37 bytes
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms(FORMAT('{"_id": 13, "ikey" : [ false %s ]}', repeat(', true', 5))::bson, 'ikey', false, true, true, 60) term;

-- Expectation is 13 bytes of overhead + (7 * 4) = 41 bytes -> 41 bytes (truncated)
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms(FORMAT('{"_id": 13, "ikey" : [ false %s ]}', repeat(', true', 6))::bson, 'ikey', false, true, true, 60) term;

-- Expectation is 13 bytes of overhead + (8 * 4) = 45 bytes -> 41 bytes (truncated)
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms(FORMAT('{"_id": 13, "ikey" : [ false %s ]}', repeat(', true', 7))::bson, 'ikey', false, true, true, 60) term;

-- decimal128 overhead = 3 + sizeof(decimal128) = 19 bytes
-- Expectation is 13 bytes of overhead + (1 * 19) = 32 bytes
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 13, "ikey" : [ { "$numberDecimal": "129e400" }]}'::bson, 'ikey', false, true, true, 60) term;

-- 2 decimal128 : 13 + 38 = 51 bytes
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 13, "ikey" : [ { "$numberDecimal": "129e400" }, { "$numberDecimal": "129e400" }]}'::bson, 'ikey', false, true, true, 60) term;

-- 3 decimal128 : 13 + 57 = 70 bytes-> 51 bytes
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 13, "ikey" : [ { "$numberDecimal": "129e400" }, { "$numberDecimal": "129e400" }, { "$numberDecimal": "129e400" }]}'::bson, 'ikey', false, true, true, 60) term;

-- decimal 127 + bool (truncated at decimal128)
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 13, "ikey" : [ { "$numberDecimal": "129e400" }, { "$numberDecimal": "129e400" }, false]}'::bson, 'ikey', false, true, true, 60) term;

-- bool + decimal128 (truncated but has all values)
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 13, "ikey" : [ false, { "$numberDecimal": "129e400" }, { "$numberDecimal": "129e400" }]}'::bson, 'ikey', false, true, true, 60) term;

-- 5 bool + int: Expected is 13 + (5 * 4) + 7 = 33 + 7 = 40 -> Truncated with all values
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms(FORMAT('{"_id": 13, "ikey" : [ false %s, 1234 ]}', repeat(', true', 4))::bson, 'ikey', false, true, true, 60) term;

-- 5 bool + decimal128: Expected is 13 + (5 * 4) + 19 = 33 + 19 = 52 -> Truncated with all values
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms(FORMAT('{"_id": 13, "ikey" : [ false %s, { "$numberDecimal": "129e400" } ]}', repeat(', true', 4))::bson, 'ikey', false, true, true, 60) term;

-- array with mixed fixed + variable.
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms(FORMAT('{"_id": 13, "ikey" : [ { "$numberDecimal": "1234" }, "This is a string that needs to be truncated after an integer term in an array"  ]}', repeat(', true', 4))::bson, 'ikey', false, true, true, 60) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms(FORMAT('{"_id": 13, "ikey" : [ { "$numberInt": "1234" }, "This is a string that needs to be truncated after an integer term in an array"  ]}', repeat(', true', 4))::bson, 'ikey', false, true, true, 60) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms(FORMAT('{"_id": 13, "ikey" : [ { "$numberLong": "1234" }, "This is a string that needs to be truncated after an integer term in an array"  ]}', repeat(', true', 4))::bson, 'ikey', false, true, true, 60) term;


-- Expectation is 13 bytes of overhead + (3 * 7) = 34 bytes + 1 bools == 38 bytes. Now add a string - it shouldn't fail since it will still go over the soft limit of 39
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms(FORMAT('{"_id": 13, "ikey" : [ 1234 %s, true, "abcde" ]}', repeat(', 1234', 2))::bson, 'ikey', false, true, true, 60) term;

-- start fresh with arrays
SELECT documentdb_api.drop_collection('db','index_truncation_tests');

set documentdb.indexTermLimitOverride to 60;
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "index_truncation_tests", "indexes": [ { "key": { "ikey": 1 }, "name": "ikey_1", "enableLargeIndexKeys": true } ] }');
\d documentdb_data.documents_6401

-- empty array
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{ "_id": 1, "ikey": [] }');

-- bool arrays
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{ "_id": 2, "ikey": [ false ] }');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', FORMAT('{ "_id": 3, "ikey": [ false %s ] }', repeat(', true', 3))::bson);
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', FORMAT('{ "_id": 4, "ikey": [ false %s ] }', repeat(', true', 4))::bson);
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', FORMAT('{ "_id": 5, "ikey": [ false %s ] }', repeat(', true', 5))::bson);
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', FORMAT('{ "_id": 6, "ikey": [ false %s ] }', repeat(', true', 6))::bson);

-- int arrays
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', FORMAT('{"_id": 7, "ikey" : [ 1234 %s ]}', repeat(', 1234', 1))::bson);
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', FORMAT('{"_id": 8, "ikey" : [ 1234 %s ]}', repeat(', 1234', 2))::bson);
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', FORMAT('{"_id": 9, "ikey" : [ 1234 %s ]}', repeat(', 1234', 3))::bson);
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', FORMAT('{"_id": 10, "ikey" : [ 1234 %s ]}', repeat(', 1234', 4))::bson);
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', FORMAT('{"_id": 21, "ikey" : [ 1234 %s, 3457 ]}', repeat(', 1234', 4))::bson);

-- decimal array
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', FORMAT('{"_id": 11, "ikey" : [ { "$numberDecimal": "1234" } %s ]}', repeat(', { "$numberDecimal": "1234" }', 1))::bson);
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', FORMAT('{"_id": 12, "ikey" : [ { "$numberDecimal": "1234" } %s ]}', repeat(', { "$numberDecimal": "1234" }', 2))::bson);
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', FORMAT('{"_id": 13, "ikey" : [ { "$numberDecimal": "1234" } %s ]}', repeat(', { "$numberDecimal": "1234" }', 3))::bson);
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', FORMAT('{"_id": 14, "ikey" : [ { "$numberDecimal": "1234" } %s ]}', repeat(', { "$numberDecimal": "1234" }', 4))::bson);
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', FORMAT('{"_id": 15, "ikey" : [ { "$numberDecimal": "1234" } %s, 2345 ]}', repeat(', { "$numberDecimal": "1234" }', 4))::bson);
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', FORMAT('{"_id": 16, "ikey" : [ { "$numberDecimal": "1234" } %s, 3456 ]}', repeat(', { "$numberDecimal": "1234" }', 4))::bson);

SELECT documentdb_api.insert_one('db', 'index_truncation_tests', FORMAT('{"_id": 17, "ikey" : [ { "$numberDecimal": "1e406" } %s ]}', repeat(', { "$numberDecimal": "1e406" }', 1))::bson);
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', FORMAT('{"_id": 18, "ikey" : [ { "$numberDecimal": "1e406" } %s ]}', repeat(', { "$numberDecimal": "1e406" }', 2))::bson);
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', FORMAT('{"_id": 19, "ikey" : [ { "$numberDecimal": "1e406" } %s, 1 ]}', repeat(', { "$numberDecimal": "1e406" }', 3))::bson);
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', FORMAT('{"_id": 20, "ikey" : [ { "$numberDecimal": "1e406" } %s, 5 ]}', repeat(', { "$numberDecimal": "1e406" }', 4))::bson);

-- mixed types
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 22, "ikey" : [ 1234, 1234, "This is a string after the numbers" ]}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 23, "ikey" : [ { "$numberDecimal": "1234" }, { "$numberDecimal": "1234" }, "This is a string after the numbers" ]}');

SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 24, "ikey" : [ "String before", 1234, 1234 ]}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 25, "ikey" : [ "String before", { "$numberDecimal": "1234" }, { "$numberDecimal": "1234" } ]}');

SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 26, "ikey" : [ "String before", 1234, 1234, "This is a string after the numbers" ]}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 27, "ikey" : [ "String before", { "$numberDecimal": "1234" }, { "$numberDecimal": "1234" }, "This is a string after the numbers" ]}');

-- test $eq
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": [] }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": [ false ] }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": [ false, true, true, true, true, true, false ] }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": [ false, true, true, true, true, true, true ] }';

SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": [ 1234, 1234, 1234, 1234 ] }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": [ 1234, 1234, 1234, 1234, 1234 ] }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": [ 1234, 1234, 1234, 1234, 1234, 3457 ] }';

SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": [ { "$numberDecimal": "1e406" }, { "$numberDecimal": "1e406" }, { "$numberDecimal": "1e406" }, { "$numberDecimal": "1e406" }, 1 ] }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": [ { "$numberDecimal": "1e406" }, { "$numberDecimal": "1e406" }, { "$numberDecimal": "1e406" }, { "$numberDecimal": "1e406" }, 2 ] }';

SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": [ 1234, 1234, "This is a string after the numbers" ] }';

SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": [ "String before", { "$numberDecimal": "1234" }, { "$numberDecimal": "1234" } ] }';

SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": [ "String before", { "$numberLong": "1234" }, { "$numberLong": "1234" }, "This is a string after the numbers" ] }';

-- test $gt/$gte
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gt": [ false, true, true, true, true, true, false ] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gte": [ false, true, true, true, true, true, true ] } }';


SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gt": [ 1233 ] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gt": [ 1234, 1234, 1234, 1234, 1234, 3000 ] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gte": [ 1234, 1234, 1234, 1234, 1234, 3457 ] } }';

SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gte": [ 1234, 1234, "This is a string after the numbers greater" ] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gte": [ 1234, 1234, "This is a string after the number" ] } }';

-- $size
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$size": 1 } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$size": 2 } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$size": 3 } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$size": 4 } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$size": 5 } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$size": 6 } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$size": 7 } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$size": 8 } }';

-- $all 
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$all": [ 1234, 3457 ] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$all": [ 1234, "This is a string after the numbers" ] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$all": [ 1234, { "$regex": ".+string.+numbers", "$options": "" } ] } }';

-- $all with $elemMatch
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$all": [{ "$elemMatch": { "$gt": 1000, "$lt": 2000 } }, { "$elemMatch": { "$gt": 2000, "$lt": 3000 } } ] } }';

-- $elemMatch
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$elemMatch": { "$gt": 3000, "$lt": 4000 }} }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$elemMatch": { "$all": [ 2345 ] }} }';


DELETE FROM documentdb_data.documents_6401;

-- Specific operator tests.
-- $gt
BEGIN;
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 31, "ikey" : [ 2345, 1234, 1234, 1234, 1234 ]}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 32, "ikey" : [ { "$numberDecimal": "2344" }, 1234, 1234, 1234, 1234 ]}');

-- both are greater (null < numbers)
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gt": [ null, 2345 ] } }';

-- both are bigger than 2340
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gt": [ 2340, 2345 ] } }';

-- only the 1st doc matches
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gt": [ 2344, 1235 ] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gt": [ 2345, 1234 ] } }';

ROLLBACk;

BEGIN;
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 31, "ikey" : [ 2345, 1234, 1234, 1234, 1234 ]}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 32, "ikey" : [ { "$numberDecimal": "2344" }, 1234, 1234, 1234, 1234 ]}');

-- both are greater (null < numbers)
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gt": [ null, 2345 ] } }';

-- both are bigger than 2340
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gt": [ 2340, 2345 ] } }';

-- only the 1st doc matches
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gt": [ 2344, 1235 ] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gt": [ 2345, 1234 ] } }';

ROLLBACk;

-- from above, for int arrays, the 5th integer is lost in truncation.
BEGIN;
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 31, "ikey" : [ 2345, 1234, 1234, { "$numberInt": "1450" } ]}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 32, "ikey" : [ 2345, 1234, 1234, { "$numberDecimal": "1451" } ]}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 33, "ikey" : [ 2345, 1234, 1234, { "$numberDouble": "1449" } ]}');

-- the "1" would not be in the index term.
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 34, "ikey" : [ 2345, 1234, 1234, { "$numberInt": "1450" }, 1 ]}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 35, "ikey" : [ 2345, 1234, 1234, { "$numberDecimal": "1451" }, 1 ]}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 36, "ikey" : [ 2345, 1234, 1234, { "$numberDouble": "1449" }, 1 ]}');

-- now insert some via Decimal128 - 37 matches 36, 38 matches 32
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 37, "ikey" : [ { "$numberDecimal": "2345" }, 1234, 1234, { "$numberDouble": "1449" }, 1 ]}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 38, "ikey" : [ { "$numberDecimal": "2345" }, 1234, { "$numerDouble": "1234" }, { "$numberDecimal": "1451" } ]}');

-- 31, 32, 34, 35 have values in position 4 that would be > and match, 36 matches on the last index. Only 33 is a mismatch
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gt": [ 2345, 1234, 1234, { "$numberInt": "1449" }, 0 ] } }';

-- Now only 31, 32, 34, 35 match
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gt": [ 2345, 1234, 1234, { "$numberInt": "1449" }, 2 ] } }';

ROLLBACK;

-- integer + string + integer.
BEGIN;
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 31, "ikey" : [ 2345.1, "Some large string that should theoretically go over 60 chars", 1 ]}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 32, "ikey" : [ { "$numberDecimal": "2345.1" }, "Some large string that should theoretically go over 60 chars and then some", 0 ]}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 33, "ikey" : [ { "$numberDecimal": "2345.1" }, "Some large string that should theoretically go over 60 chars", 2 ]}');


SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gt": [ 2345, { "$maxKey": 1 } ] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gt": [ 2345.1, { "$maxKey": 1 } ] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gt": [ 2345.1, "Some maximal string" ] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gt": [ 2345.1, "Some large string that should theoretically" ] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gt": [ 2345.1, "Some large string that should theoretically go over 60 chars", 1 ] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gt": [ { "$numberDecimal": "2345.1" }, "Some large string that should theoretically go over 60 chars and then less", -1 ] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gt": [ { "$numberDecimal": "2345.1" }, "Some large string that should theoretically go over 60 chars and then some", -1 ] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gt": [ { "$numberDecimal": "2345.1" }, "Some large string that should theoretically go over 60 chars and then some", 1 ] } }';

ROLLBACK;


-- $lt
BEGIN;
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 31, "ikey" : [ 2345, 1234, 1234, 1234, 1234 ]}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 32, "ikey" : [ { "$numberDecimal": "2344" }, 1234, 1234, 1234, 1234 ]}');

SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$lt": [ true, 2345 ] } }';

SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$lt": [ 2340, 2345 ] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$lt": [ 2346, 2345 ] } }';

SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$lt": [ 2344, 1235 ] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$lt": [ 2345, 1234 ] } }';

SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$lt": [ 2345, 1234, 1234, 1234, 1234 ] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$lt": [ 2345, 1234, 1234, 1234, 1234, 1234 ] } }';
ROLLBACk;


-- from above, for int arrays, the 5th integer is lost in truncation.
BEGIN;
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 31, "ikey" : [ 2345, 1234, 1234, { "$numberInt": "1450" } ]}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 32, "ikey" : [ 2345, 1234, 1234, { "$numberDecimal": "1451" } ]}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 33, "ikey" : [ 2345, 1234, 1234, { "$numberDouble": "1449" } ]}');

-- the "1" would not be in the index term.
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 34, "ikey" : [ 2345, 1234, 1234, { "$numberInt": "1450" }, 1 ]}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 35, "ikey" : [ 2345, 1234, 1234, { "$numberDecimal": "1451" }, 1 ]}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 36, "ikey" : [ 2345, 1234, 1234, { "$numberDouble": "1449" }, 1 ]}');

-- now insert some via Decimal128 - 37 matches 36, 38 matches 32
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 37, "ikey" : [ { "$numberDecimal": "2345" }, 1234, 1234, { "$numberDouble": "1449" }, 1 ]}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 38, "ikey" : [ { "$numberDecimal": "2345" }, 1234, { "$numerDouble": "1234" }, { "$numberDecimal": "1451" } ]}');


SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$lt": [ 2345, 1234, 1234, { "$numberInt": "1449" }, 0 ] } }';

SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$lt": [ 2345, 1234, 1234, { "$numberInt": "1449" } ] } }';

SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$lt": [ 2345, 1234, 1234, { "$numberInt": "1450" }, 0 ] } }';
ROLLBACK;

-- $gte
BEGIN;
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 31, "ikey" : [ 2345, 1234, 1234, 1234, 1234 ]}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 32, "ikey" : [ { "$numberDecimal": "2344" }, 1234, 1234, 1234, 1234 ]}');

-- both are greater (null < numbers)
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gte": [ null, 2345 ] } }';

-- both are bigger than 2340
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gte": [ 2340, 2345 ] } }';

-- only the 1st doc matches
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gte": [ 2344, 1235 ] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gte": [ 2345, 1234 ] } }';

ROLLBACk;

BEGIN;
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 31, "ikey" : [ 2345, 1234, 1234, 1234, 1234 ]}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 32, "ikey" : [ { "$numberDecimal": "2344" }, 1234, 1234, 1234, 1234 ]}');

-- both are greater (null < numbers)
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gte": [ null, 2345 ] } }';

-- both are bigger than 2340
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gte": [ 2340, 2345 ] } }';

-- only the 1st doc matches
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gte": [ 2344, 1235 ] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gte": [ 2345, 1234 ] } }';

ROLLBACk;

-- from above, for int arrays, the 5th integer is lost in truncation.
BEGIN;
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 31, "ikey" : [ 2345, 1234, 1234, { "$numberInt": "1450" } ]}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 32, "ikey" : [ 2345, 1234, 1234, { "$numberDecimal": "1451" } ]}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 33, "ikey" : [ 2345, 1234, 1234, { "$numberDouble": "1449" } ]}');

-- the "1" would not be in the index term.
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 34, "ikey" : [ 2345, 1234, 1234, { "$numberInt": "1450" }, 1 ]}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 35, "ikey" : [ 2345, 1234, 1234, { "$numberDecimal": "1451" }, 1 ]}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 36, "ikey" : [ 2345, 1234, 1234, { "$numberDouble": "1449" }, 1 ]}');

-- now insert some via Decimal128 - 37 matches 36, 38 matches 32
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 37, "ikey" : [ { "$numberDecimal": "2345" }, 1234, 1234, { "$numberDouble": "1449" }, 1 ]}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 38, "ikey" : [ { "$numberDecimal": "2345" }, 1234, { "$numerDouble": "1234" }, { "$numberDecimal": "1451" } ]}');

-- 31, 32, 34, 35 have values in position 4 that would be > and match, 36 matches on the last index. Only 33 is a mismatch
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gte": [ 2345, 1234, 1234, { "$numberInt": "1449" }, 0 ] } }';

-- Now only 31, 32, 34, 35 match
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gte": [ 2345, 1234, 1234, { "$numberInt": "1449" }, 2 ] } }';

SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gte": [ 2345, 1234, 1234, { "$numberInt": "1449" }, 1 ] } }';
ROLLBACK;

-- integer + string + integer.
BEGIN;
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 31, "ikey" : [ 2345.1, "Some large string that should theoretically go over 60 chars", 1 ]}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 32, "ikey" : [ { "$numberDecimal": "2345.1" }, "Some large string that should theoretically go over 60 chars and then some", 0 ]}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 33, "ikey" : [ { "$numberDecimal": "2345.1" }, "Some large string that should theoretically go over 60 chars", 2 ]}');


SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gte": [ 2345, { "$maxKey": 1 } ] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gte": [ 2345.1, { "$maxKey": 1 } ] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gte": [ 2345.1, "Some maximal string" ] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gte": [ 2345.1, "Some large string that should theoretically" ] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gte": [ 2345.1, "Some large string that should theoretically go over 60 chars", 1 ] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gte": [ { "$numberDecimal": "2345.1" }, "Some large string that should theoretically go over 60 chars and then less", -1 ] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gte": [ { "$numberDecimal": "2345.1" }, "Some large string that should theoretically go over 60 chars and then some", -1 ] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gte": [ { "$numberDecimal": "2345.1" }, "Some large string that should theoretically go over 60 chars and then some", 0 ] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gte": [ { "$numberDecimal": "2345.1" }, "Some large string that should theoretically go over 60 chars and then some", 1 ] } }';

ROLLBACK;

-- $in/$nin/$ne/$all(eq)
BEGIN;

SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 31, "ikey" : [ 2345, 1234, 1234, { "$numberInt": "1450" } ]}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 32, "ikey" : [ 2345, 1234, 1234, { "$numberDecimal": "1451" } ]}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 33, "ikey" : [ 2345, 1234, 1234, { "$numberDouble": "1449" } ]}');

-- the "1" would not be in the index term.
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 34, "ikey" : [ 2345, 1234, 1234, { "$numberInt": "1450" }, 1 ]}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 35, "ikey" : [ 2345, 1234, 1234, { "$numberDecimal": "1451" }, 1 ]}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 36, "ikey" : [ 2345, 1234, 1234, { "$numberDouble": "1449" }, 1 ]}');


SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$in": [ [ 2345, 1234, 1234, { "$numberInt": "1450" } ], [ 2345, 1234, 1234, { "$numberDouble": "1449" }, 1 ] ] }}';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$nin": [ [ 2345, 1234, 1234, { "$numberInt": "1450" } ], [ 2345, 1234, 1234, { "$numberDouble": "1449" }, 1 ] ] }}';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$ne": [ 2345, 1234, 1234, { "$numberDouble": "1449" }, 1 ] }}';

SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$all": [ [ 2345, 1234, 1234, { "$numberDouble": "1449" }, 1 ] ] }}';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$all": [ { "$elemMatch": { "$eq": 1 }}, { "$elemMatch": { "$eq": 1451 } } ] }}';

SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$elemMatch": { "$eq": 1 } }}';
ROLLBACK;

-- $elemMatch (regex)
BEGIN;
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 31, "ikey" : [ 2345, 1234, 1234, "This is a string that goes over the index truncation limit" ]}');

SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$elemMatch": { "$all": [ { "$regex": "limit$", "$options": "" } ] }}}';

ROLLBACK;


-- $range
BEGIN;
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 31, "ikey" : [ 2345, 1234, 1234, 1234, 1234, 1234, 1234 ]}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 32, "ikey" : [ 2345, 1234, 1234, 1234, 1234, 1234, 1234, 1235 ]}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 33, "ikey" : [ 2345, 1234, 1234, 1234, 1234, 1234, 1234, 1236 ]}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 34, "ikey" : [ 2345, 1234, 1234, 1234, 1234, 1234, 1234, 1237 ]}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 35, "ikey" : [ 2345, 1234, 1234, 1234, 1234, 1234, 1234, 1238 ]}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 36, "ikey" : [ 2345, 1234, 1234, 1234, 1234, 1234, 1234, 1239 ]}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 37, "ikey" : [ 2345, 1234, 1234, 1234, 1234, 1234, 1234, 1241 ]}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 38, "ikey" : [ 2345, 1234, 1234, 1234, 1234, 1234, 1234, 1242 ]}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 39, "ikey" : [ 2345, 1234, 1234, 1234, 1234, 1234, 1234, 1243 ]}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 40, "ikey" : [ 2345, 1234, 1234, 1234, 1234, 1234, 1234, 1244 ]}');

-- matches nothing.
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gt": [ 2345, 1234, 1234, 1234, 1234, 1235 ], "$lt": [ 2346, 1234, 1234, 1234, 1234, 1234, 1234 ] } }';

SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gt":[ 2345, 1234, 1234, 1234, 1234, 1234, 1234, 1236 ], "$lt": [ 2345, 1234, 1234, 1234, 1234, 1234, 1234, 1241 ] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gt":[ 2345, 1234, 1234, 1234, 1234, 1234, 1234, 1236 ], "$lte": [ 2345, 1234, 1234, 1234, 1234, 1234, 1234, 1241 ] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gte":[ 2345, 1234, 1234, 1234, 1234, 1234, 1234, 1236 ], "$lt": [ 2345, 1234, 1234, 1234, 1234, 1234, 1234, 1241 ] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gte":[ 2345, 1234, 1234, 1234, 1234, 1234, 1234, 1236 ], "$lte": [ 2345, 1234, 1234, 1234, 1234, 1234, 1234, 1241 ] } }';


SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gt": { "$minKey": 1 } , "$lt": [ 2345, 1234, 1234, 1234, 1234, 1234, 1234, 1241 ] } }';

SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gt": [ 2345, 1234, 1234, 1234, 1234, 1234, 1234, 1236 ], "$lt": { "$maxKey": 1 } } }';
ROLLBACK;


--------------------------
---- DATA TYPE: Document
--------------------------

-- with  60 byte truncation limit, the real limit is 60-18 = 42 bytes. We can tolerate any entries over 42 bytes as long as they're also marked truncated.
-- the base structural overhead for an array is 5 (bson Doc) + 2 (path) + 1 (typecode) + 5 (doc header) = 13 bytes
-- each path takes some characters (string), 1 byte (type code) + path Value (variable) = 1+ bytes + valueLength

-- empty doc (13 bytes)
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 13, "ikey" : {} }'::bson, 'ikey', false, true, true, 60) term;

-- single path (13 bytes + 4 bytes (path) + 1 byte type Code, 9 byte string, 1 for the \0, 4 byte string length) = 32 bytes (not truncated)
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 13, "ikey" : { "abc": "123456789" } }'::bson, 'ikey', false, true, true, 60) term;

-- single path truncated on path - path should be truncated
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms(FORMAT('{"_id": 13, "ikey" : { "%s": "123456789", "b": 1, "c": 1 } }', repeat('abcdefg', 10))::bson, 'ikey', false, true, true, 60) term;

-- single path truncated on path since path is greater than soft limit less than hard limit - path should not be truncated
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms(FORMAT('{"_id": 13, "ikey" : { "%s": "123456789", "b": 1, "c": 1 } }', repeat('abcdefg', 6))::bson, 'ikey', false, true, true, 60) term;

-- value doesn't show up until we're under the soft limit.
-- overhead: 13 bytes, 1 byte type code: for a 42 byte soft limit we're looking at a 28 byte path: from paths >= 29 bytes values Must be maxkey
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms(FORMAT('{"_id": 13, "ikey" : { "%s": "123456789", "b": 1, "c": 1 } }', repeat('a', 41))::bson, 'ikey', false, true, true, 60) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms(FORMAT('{"_id": 13, "ikey" : { "%s": "123456789", "b": 1, "c": 1 } }', repeat('a', 30))::bson, 'ikey', false, true, true, 60) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms(FORMAT('{"_id": 13, "ikey" : { "%s": "123456789", "b": 1, "c": 1 } }', repeat('a', 29))::bson, 'ikey', false, true, true, 60) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms(FORMAT('{"_id": 13, "ikey" : { "%s": "123456789", "b": 1, "c": 1 } }', repeat('a', 28))::bson, 'ikey', false, true, true, 60) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms(FORMAT('{"_id": 13, "ikey" : { "%s": "123456789", "b": 1, "c": 1 } }', repeat('a', 27))::bson, 'ikey', false, true, true, 60) term;

-- value gets truncated when it appears
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms(FORMAT('{"_id": 13, "ikey" : { "%s": "123456789123451234512345", "b": 1, "c": 1 } }', repeat('a', 28))::bson, 'ikey', false, true, true, 60) term;
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms(FORMAT('{"_id": 13, "ikey" : { "%s": "%s", "b": 1, "c": 1 } }', repeat('a', 5), repeat('b', 40))::bson, 'ikey', false, true, true, 60) term;

-- truncation with object + string after works
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms(FORMAT('{"_id": 13, "ikey" : { "a": { "b": 1 }, "%s": "123456789123451234512345", "b": 1, "c": 1 } }', repeat('a', 28))::bson, 'ikey', false, true, true, 60) term;

-- sample docs from subsequent stages
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 31, "ikey" : { "userName": "myFirstUserName", "address": "1st avenue, Rua Amalho" }}'::bson, 'ikey', false, true, true, 60) term;

-- start fresh with documents
DELETE FROM documentdb_data.documents_6401;

-- $eq on something that's past truncation.
BEGIN;
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 31, "ikey" : { "userName": "myFirstUserName", "address": "1st avenue, Rua Amalho" }}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 32, "ikey" : { "userName": "myFirstUserName", "address": "2nd avenue, Rua Amalho" }}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 33, "ikey" : { "userName": "myFirstUserName", "address": "0th avenue, Rua Amalho" }}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 34, "ikey" : { "userName": "myFirstUserName", "address": { "$maxKey": 1 } }}');

SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "userName": "myFirstUserName", "address": "2nd avenue, Rua Amalho" }}';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "userName": "myFirstUserName", "address": "0th avenue, Rua Amalho" }}';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "userName": "myFirstUserName", "address": { "$maxKey": 1 } }}';

SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$ne": { "userName": "myFirstUserName", "address": "2nd avenue, Rua Amalho" } }}';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$ne": { "userName": "myFirstUserName", "address": "0th avenue, Rua Amalho" } }}';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$ne": { "userName": "myFirstUserName", "address": { "$maxKey": 1 } } }}';
ROLLBACK;

-- $gt/$lt/$gte/$gt/$range on something past truncation
BEGIN;
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 31, "ikey" : { "userName": "myFirstUserName", "address": "1st avenue, Rua Amalho" }}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 32, "ikey" : { "userName": "myFirstUserName", "address": "2nd avenue, Rua Amalho" }}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 33, "ikey" : { "userName": "myFirstUserName", "address": "0th avenue, Rua Amalho" }}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 34, "ikey" : { "userName": "myFirstUserName", "address": { "$maxKey": 1 } }}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 35, "ikey" : { "userName": "myFirstUserName", "address1": { "$maxKey": 1 } }}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 36, "ikey" : { "userName": "myFirstUserName", "address": "0th avenue, Rua Amalho", "MaxField": { "$maxKey": 1 } }}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 37, "ikey" : { "userName": "myFirstUserName", "address": "1st avenue, Rua Amalho", "apartment": "compartamento"  }}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 38, "ikey" : { "userName": "my0thUserNamethatgoespasttruncationlimitssothataddressdoesnotshowup", "address": "0th avenue, Rua Amalho", "apartment": "compartamento"  }}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 39, "ikey" : { "userName": "my0thUserNamethatgoespasttruncationlimitssothataddressdoesnotshowup", "address": "1st avenue, Rua Amalho", "apartment": "compartamento"  }}');
SELECT documentdb_api.insert_one('db', 'index_truncation_tests', '{"_id": 40, "ikey" : { "userName": "my0thUserNamethatgoespasttruncationlimitssothataddressdoesnotshowup", "address": "2nd avenue, Rua Amalho", "apartment": "compartamento"  }}');

SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gt": { "userName": "myFirstUserName", "address": "1st avenue, Rua Amalho" } }}';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gt": { "userName": "my0thUserNamethatgoespasttruncationlimitssothataddressdoesnotshowup", "address": "1st avenue, Rua Amalho", "apartment": "compartamento"  } }}';

SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gte": { "userName": "myFirstUserName", "address": "1st avenue, Rua Amalho" } }}';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$gte": { "userName": "my0thUserNamethatgoespasttruncationlimitssothataddressdoesnotshowup", "address": "1st avenue, Rua Amalho", "apartment": "compartamento"  } }}';

SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$lt": { "userName": "myFirstUserName", "address": "1st avenue, Rua Amalho" } }}';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$lt": { "userName": "my0thUserNamethatgoespasttruncationlimitssothataddressdoesnotshowup", "address": "1st avenue, Rua Amalho", "apartment": "compartamento"  } }}';

SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$lte": { "userName": "myFirstUserName", "address": "1st avenue, Rua Amalho" } }}';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$lte": { "userName": "my0thUserNamethatgoespasttruncationlimitssothataddressdoesnotshowup", "address": "1st avenue, Rua Amalho", "apartment": "compartamento"  } }}';

SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$lt": { "userName": "myFirstUserName", "address": "1st avenue, Rua Amalho" }, "$gt": { "userName": "my0thUserNamethatgoespasttruncationlimitssothataddressdoesnotshowup", "address": "1st avenue, Rua Amalho", "apartment": "compartamento" } }}';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$lt": { "userName": "myFirstUserName", "address": "1st avenue, Rua Amalho" }, "$gte": { "userName": "my0thUserNamethatgoespasttruncationlimitssothataddressdoesnotshowup", "address": "1st avenue, Rua Amalho", "apartment": "compartamento" } }}';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$lte": { "userName": "myFirstUserName", "address": "1st avenue, Rua Amalho" }, "$gt": { "userName": "my0thUserNamethatgoespasttruncationlimitssothataddressdoesnotshowup", "address": "1st avenue, Rua Amalho", "apartment": "compartamento" } }}';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$lte": { "userName": "myFirstUserName", "address": "1st avenue, Rua Amalho" }, "$gte": { "userName": "my0thUserNamethatgoespasttruncationlimitssothataddressdoesnotshowup", "address": "1st avenue, Rua Amalho", "apartment": "compartamento" } }}';

SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$in": [ { "userName": "myFirstUserName", "address": "1st avenue, Rua Amalho" }, { "userName": "my0thUserNamethatgoespasttruncationlimitssothataddressdoesnotshowup", "address": "1st avenue, Rua Amalho", "apartment": "compartamento" } ] }}';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_tests') WHERE document @@ '{ "ikey": { "$nin": [ { "userName": "myFirstUserName", "address": "1st avenue, Rua Amalho" }, { "userName": "my0thUserNamethatgoespasttruncationlimitssothataddressdoesnotshowup", "address": "1st avenue, Rua Amalho", "apartment": "compartamento" } ] }}';
ROLLBACK;

-- finally shard_collection and ensure it's Truncation limit is still there.
SELECT * FROM documentdb_api_catalog.collection_indexes WHERE collection_id = 6401 ORDER BY index_id;
SELECT documentdb_api.shard_collection('db', 'index_truncation_tests', '{ "_id": "hashed" }', false);
\d documentdb_data.documents_6401

/* testing wildcard indexes */
SET documentdb.indexTermLimitOverride TO 100;
SET documentdb.maxWildcardIndexKeySize TO 50;

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "wildcard_index_truncation_tests", "indexes": [ { "key": { "$**": 1 }, "name": "wkey_1", "enableLargeIndexKeys": true } ] }');

\d documentdb_data.documents_6402

/* should fail fue to maximum size exceeded */
SELECT documentdb_api.insert_one('db', 'wildcard_index_truncation_tests', '{"_id": 1, "thiskeyislargerthanthemaximumallowedsizeof50characters" : "sample_value"}');

/* should succeed due to maximum size not exceeded */
SELECT documentdb_api.insert_one('db', 'wildcard_index_truncation_tests', '{"_id": 1, "ikey1" : "this is a string that does not violate the index term limit"}');

/* should succeed and truncate index term with wildcard index */
SELECT documentdb_api.insert_one('db', 'wildcard_index_truncation_tests', '{ "_id": 2, "ikey2": "this is a string that does violate the index term limit as it goes much over the 100 character limit of strings" }');

/* query all and check if same index is used for both */
SELECT document FROM documentdb_api.collection('db', 'wildcard_index_truncation_tests') WHERE document @@ '{ "ikey1": { "$gt": "this is a" } }';
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'wildcard_index_truncation_tests') WHERE document @@ '{ "ikey1": { "$gt": "this is a" } }';

SELECT document FROM documentdb_api.collection('db', 'wildcard_index_truncation_tests') WHERE document @@ '{ "ikey2": { "$gt": "this is a" } }';
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'wildcard_index_truncation_tests') WHERE document @@ '{ "ikey2": { "$gt": "this is a" } }';

/* create collection again and test with single field wildcard. */
set documentdb.indexTermLimitOverride to 60;

/* should create wildcard for single path */
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "wildcard_index_truncation_tests_single_field", "indexes": [ { "key": { "a.b.$**": 1 }, "name": "wkey_2", "enableLargeIndexKeys": true } ] }');

\d documentdb_data.documents_6403

/* should succeed and truncate index term with wildcard index */
SELECT documentdb_api.insert_one('db', 'wildcard_index_truncation_tests_single_field', '{ "_id": 1, "a": { "b": { "c": "this is a string that does violate the index term limit as it goes much over the 100 character limit of strings" } } }');

SELECT documentdb_api.insert_one('db', 'wildcard_index_truncation_tests_single_field', '{ "_id": 2, "a": { "d": { "c": "this is a string that does violate the index term limit as it goes much over the 100 character limit of strings" } } }');

SELECT documentdb_api.insert_one('db', 'wildcard_index_truncation_tests_single_field', '{"_id": 3, "a" : { "b": { "c": [ 2345, 1234, 1234, 1234, 1234 ] } } }');

/* making sure the array is being truncated */
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 3, "a.b.c": [ 2345, 1234, 1234, 1234, 1234 ] }', '', true, true, false, 60) term;

/* making sure the wildcard index terms are being generated correctly for a truncated value */
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id": 3, "a.b.c": [ 2345, 1234, 1234, 1234, 1234, 76, 85 ] }', '["a"]', false, true, false, 60) term;

SELECT documentdb_api.insert_one('db', 'wildcard_index_truncation_tests_single_field', '{"_id": 4, "a": { "b": { "c": 1, "d": 2, "e": "12334141424124" } } }');

/* making sure the document is being truncated */
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 4, "a": { "b": { "c": 1, "d": 2, "e": "12334141424124" } } }', '', true, true, false, 60) term;

/* making sure the wildcard index terms are being generated correctly for a truncated value */
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id": 4, "a": { "b": { "c": 1, "d": 2, "e": "12334141424124" } } }', '["a"]', false, true, false, 60) term;

/* second element in array shouldn't appear as a term */
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id": 6, "a": [{ "b": { "c": 1, "d": 2, "e": "1" } }, { "oi": 2} ], "c": 1}', '["a.0"]', false, true, false, 60) term;

/* term key "c" shouldn't appear as a term */
SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id": 6, "a": [{ "b": { "c": 1, "d": 2, "e": "1" } }, { "oi": 2} ], "c": 1}', '["a"]', false, true, false, 60) term;

SELECT document FROM documentdb_api.collection('db', 'wildcard_index_truncation_tests_single_field') WHERE document @@ '{ "a.b.c": { "$gt": "this is a" } }';
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'wildcard_index_truncation_tests_single_field') WHERE document @@ '{ "a.b.c": { "$gt": "this is a" } }';

SELECT document FROM documentdb_api.collection('db', 'wildcard_index_truncation_tests_single_field') WHERE document @@ '{ "a.d.c": { "$gt": "this is a" } }';
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'wildcard_index_truncation_tests_single_field') WHERE document @@ '{ "a.d.c": { "$gt": "this is a" } }';

SELECT document FROM documentdb_api.collection('db', 'wildcard_index_truncation_tests_single_field') WHERE document @@ '{ "a.b.c": { "$gte": [ 2345, 1234, 1234, 1234, 1230 ] , "$lte": [ 2345, 1234, 1234, 1234, 1240 ] } }';
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'wildcard_index_truncation_tests_single_field') WHERE document @@ '{ "a.b.c": { "$gte": [ 2345, 1234, 1234, 1234, 1230 ] , "$lte": [ 2345, 1234, 1234, 1234, 1240 ] } }';

SELECT document FROM documentdb_api.collection('db', 'wildcard_index_truncation_tests_single_field') WHERE document @@ '{ "a.b.c": 1 }';
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'wildcard_index_truncation_tests_single_field') WHERE document @@ '{ "a.b.c": 1 }';

SELECT document FROM documentdb_api.collection('db', 'wildcard_index_truncation_tests_single_field') WHERE document @@ '{ "a.b.c": { "$elemMatch": { "$eq": 2345 } } }';
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'wildcard_index_truncation_tests_single_field') WHERE document @@ '{ "a.b.c": { "$elemMatch": { "$eq": 2345 } } }';

/* create collection again and test with wildcard projection. */
set documentdb.indexTermLimitOverride to 60;

/* should create wildcard index with projection */
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', 
                                '{ 
                                    "createIndexes": "wildcard_index_truncation_tests_wildcard_projection",
                                    "indexes": 
                                    [ 
                                        { "key": { "$**": 1 }, 
                                          "name": "wkey_3", 
                                          "enableLargeIndexKeys": true ,
                                          "wildcardProjection": { "a": 1, "b": 1 }
                                        } 
                                    ] 
                                }');

\d documentdb_data.documents_6404

/* should succeed and truncate index term with wildcard index */
SELECT documentdb_api.insert_one('db', 'wildcard_index_truncation_tests_wildcard_projection', '{ "_id": 1, "a": "this is a string that does violate the index term limit as it goes much over the 100 character limit of strings", "c": 2 }');

SELECT documentdb_api.insert_one('db', 'wildcard_index_truncation_tests_wildcard_projection', '{ "_id": 2, "b": "this is a string that does violate the index term limit as it goes much over the 100 character limit of strings", "c": 2 }');

SELECT documentdb_api.insert_one('db', 'wildcard_index_truncation_tests_wildcard_projection', '{"_id": 3, "a" : [ 2345, 1234, 1234, 1234, 1234 ], "b": "this is a string that does violate the index term limit as it goes much over the 100 character limit of strings" }');

SELECT documentdb_api.insert_one('db', 'wildcard_index_truncation_tests_wildcard_projection', '{"_id": 4, "a": { "l": { "j": 1 } }, "d": 2 }');

SELECT document FROM documentdb_api.collection('db', 'wildcard_index_truncation_tests_wildcard_projection') WHERE document @@ '{ "a": { "$gt": "this is a" } }';
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'wildcard_index_truncation_tests_wildcard_projection') WHERE document @@ '{ "a": { "$gt": "this is a" } }';

SELECT document FROM documentdb_api.collection('db', 'wildcard_index_truncation_tests_wildcard_projection') WHERE document @@ '{ "b": { "$gt": "this is a" } }';
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'wildcard_index_truncation_tests_wildcard_projection') WHERE document @@ '{ "b": { "$gt": "this is a" } }';

SELECT document FROM documentdb_api.collection('db', 'wildcard_index_truncation_tests_wildcard_projection') WHERE document @@ '{ "a": { "$gte": [ 2345, 1234, 1234, 1234, 1230 ] , "$lte": [ 2345, 1234, 1234, 1234, 1240 ] } }';
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'wildcard_index_truncation_tests_wildcard_projection') WHERE document @@ '{ "a": { "$gte": [ 2345, 1234, 1234, 1234, 1230 ] , "$lte": [ 2345, 1234, 1234, 1234, 1240 ] } }';

SELECT document FROM documentdb_api.collection('db', 'wildcard_index_truncation_tests_wildcard_projection') WHERE document @@ '{ "a.l.j": 1 }';
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'wildcard_index_truncation_tests_wildcard_projection') WHERE document @@ '{ "a.l.j": 1 }';

SELECT document FROM documentdb_api.collection('db', 'wildcard_index_truncation_tests_wildcard_projection') WHERE document @@ '{ "a": { "$elemMatch": { "$eq": 2345 } } }';
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'wildcard_index_truncation_tests_wildcard_projection') WHERE document @@ '{ "a": { "$elemMatch": { "$eq": 2345 } } }';

/* should not use index when query uses key not specified in the projection */

SELECT document FROM documentdb_api.collection('db', 'wildcard_index_truncation_tests_wildcard_projection') WHERE document @@ '{ "c": 1 }';
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'wildcard_index_truncation_tests_wildcard_projection') WHERE document @@ '{ "c": 1 }';

SELECT document FROM documentdb_api.collection('db', 'wildcard_index_truncation_tests_wildcard_projection') WHERE document @@ '{ "d": 2 }';
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'wildcard_index_truncation_tests_wildcard_projection') WHERE document @@ '{ "d": 2 }';

/* test that term truncation is enabled by default if user doesn't explicitly disable it. */
set documentdb.indexTermLimitOverride to 60;

/* enableLargeIndexKeys should be false because it was explicitly set to false */
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "test_default_trunc1", "indexes": [ { "key": { "a.b": 1 }, "name": "key" } ] }');
SELECT * FROM documentdb_api_catalog.collection_indexes WHERE collection_id = 6405 ORDER BY index_id;

/* enableLargeIndexKeys should be true because it was not explicitly set to false */
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "test_default_trunc2", "indexes": [ { "key": { "a.b": 1 }, "name": "key" } ] }');
SELECT * FROM documentdb_api_catalog.collection_indexes WHERE collection_id = 6406 ORDER BY index_id;

/* enableLargeIndexKeys should be false because index does not support it (ttl index) */
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "test_default_trunc3", "indexes": [{"key": {"ttl": 1}, "name": "ttl_index", "v" : 1, "expireAfterSeconds": 5}]}');
SELECT * FROM documentdb_api_catalog.collection_indexes WHERE collection_id = 6407 ORDER BY index_id;

/* enableLargeIndexKeys should be false because index does not support it (hashed) */
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "test_default_trunc4", "indexes": [ { "key": { "a.b": "hashed" }, "name": "key" } ] }');
SELECT * FROM documentdb_api_catalog.collection_indexes WHERE collection_id = 6408 ORDER BY index_id;

/* enableLargeIndexKeys should be false because index does not support it (text) */
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "test_default_trunc5", "indexes": [ { "key": { "a.b": "text" }, "name": "key" } ] }');
SELECT * FROM documentdb_api_catalog.collection_indexes WHERE collection_id = 6409 ORDER BY index_id;


-- Additional tests for bug where index term length optimization with '$' was not consistent, i.e., we would generate prefix like 'ikey' and '$.0'

SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 2, "ikey" : { "a": ["abcdefgh"]}}', 'ikey.a', false, true
, true, 500) term;

SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 2, "ikey" : { "a": ["abcdefgh"]}}', 'ikey.a', false, true
, true) term;

SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 2, "ikey" : { "a": ["abcdefgh"]}}', 'ikey.a', true, true
, true, 500) term;

SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 2, "ikey" : { "a": ["abcdefgh"]}}', 'ikey.a', true, true
, true) term;

SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 2, "ikey" : { "a": ["abcdefgh"]}}', '', true, true
, true, 60) term;

SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 2, "ikey" : { "a": ["abcdefgh"]}}', '', true, true
, true) term;

SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id": 2, "ikey" : { "a": ["abcdefgh"]}}', '["ikey"]', false, true               
, true, 60) term;

SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 2, "ikey" : { "a": ["abcdefgh"]}}', 'ikey', true, true, true, 500) term;

SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 2, "ikey" : { "a": ["abcdefgh"]}}', 'ikey', true, true, true) term;

SELECT FORMAT('{ "visits": [ "1" %s ] }', repeat(', { "estimatedTimeOfArrival": { "$date": 101010 }, "estimatedTimeOfDeparture": { "$date": 101010 }, "visitId": "someVisitIdValueString", "visitDocuments": { } } ', 5)) AS documentvalue \gset

SELECT length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms(:'documentvalue'::bson, '', true, true, true, 500) term;
