SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 1160000;
SET documentdb.next_collection_id TO 11600;
SET documentdb.next_collection_index_id TO 11600;

-- Set configs to something reasonable for testing.
SET documentdb.indexTermLimitOverride to 20;

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "index_truncation_code_tests", "indexes": [ { "key": { "ikey": 1 }, "name": "ikey_1", "enableLargeIndexKeys": true } ] }');

\d documentdb_data.documents_11600

-- Insert code BSON data
SELECT documentdb_api.insert_one('db', 'index_truncation_code_tests', '{ "_id": 1, "ikey": { "$code" : "eyAiZXhhbXBsZSI6IDEyMywgInZhbHVlIjogImFib2JvcmEiIH0=" }, "item": "A" }');
SELECT documentdb_api.insert_one('db', 'index_truncation_code_tests', '{ "_id": 2, "ikey": { "$code" : "eyAiYSI6IDEsICJiIjogMyB9" } }');
SELECT documentdb_api.insert_one('db', 'index_truncation_code_tests', '{ "_id": 3, "ikey": { "$code" : "eyAiZXhhbXBsZSI6IDEyMywgInZhbHVlIjogImFib2JvcmEiIH0=" } }');
SELECT documentdb_api.insert_one('db', 'index_truncation_code_tests', '{ "_id": 4, "ikey": { "$code" : "YWJvYm9yYQ==" } }');
SELECT documentdb_api.insert_one('db', 'index_truncation_code_tests', '{ "_id": 5, "ikey": { "$code" : "eyAiYSI6IDEsICJiIjogMyB9" } }');
SELECT documentdb_api.insert_one('db', 'index_truncation_code_tests', '{ "_id": 6, "ikey": { "$code" : "eyAiZXhhbXBsZSI6IDEyMywgInZhbHVlIjogImFib2JvcmEiIH0=" }, "item": "F", "customKey": 6 }');
SELECT documentdb_api.insert_one('db', 'index_truncation_code_tests', '{ "_id": 7, "ikey": { "$code" : "eyAiYSI6ICIxMjM0NSIsICJiIjogMTIzMTIzMTIzMjExMjMxIH0=" }, "item": "G", "customKey": 6 }');
SELECT documentdb_api.insert_one('db', 'index_truncation_code_tests', '{ "_id": 8, "ikey": { "$code" : "dGhpcyBpcyBhIHRlc3Qgc3RyaW5n" } }');
SELECT documentdb_api.insert_one('db', 'index_truncation_code_tests', '{ "_id": 9, "ikey": { "$code" : "eyAiYSI6ICIxMjM0NSIsICJiIjogMTIzMTIzMTIzMjExMjMxIH0=" }, "item": "H", "customKey": 8 }');
SELECT documentdb_api.insert_one('db', 'index_truncation_code_tests', '{ "_id": 10, "ikey": { "$code" : "dGhpcyBpcyBhIHRlc3Qgc3RyaW5n" }, "item": "H", "customKey": 9 }');

/* All terms should be truncated */
SELECT did, length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM ( SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms(document, 'ikey', false, true, true, 20) term, document-> '_id' as did from documentdb_data.documents_11600) docs;

/* Only a subset of the terms should be truncated */
SELECT did, length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM ( SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms(document, 'ikey', false, true, true, 40) term, document-> '_id' as did from documentdb_data.documents_11600) docs;

/* $eq (all terms truncated) */
SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests') WHERE document @@ '{ "ikey": { "$code": "eyAiZXhhbXBsZSI6IDEyMywgInZhbHVlIjogImFib2JvcmEiIH0=" } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests') WHERE document @@ '{ "ikey": { "$code" : "eyAiYSI6ICIxMjM0NSIsICJiIjogMTIzMTIzMTIzMjExMjMxIH0=" }, "item": "G" }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests') WHERE document @@ '{ "ikey": { "$code": "eyAiYSI6IDEsICJiIjogMyB9" } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests') WHERE document @@ '{ "ikey": { "$code": "dGhpcyBpcyBhIHRlc3Qgc3RyaW5n" } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests') WHERE document @@ '{ "ikey": { "$code" : "YWJvYm9yYQ==" } }';

EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests') WHERE document @@ '{ "ikey": { "$code": "eyAiZXhhbXBsZSI6IDEyMywgInZhbHVlIjogImFib2JvcmEiIH0=" } }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests') WHERE document @@ '{ "ikey": { "$code" : "eyAiYSI6ICIxMjM0NSIsICJiIjogMTIzMTIzMTIzMjExMjMxIH0=" }, "item": "G" }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests') WHERE document @@ '{ "ikey": { "$code": "eyAiYSI6IDEsICJiIjogMyB9" } }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests') WHERE document @@ '{ "ikey": { "$code": "dGhpcyBpcyBhIHRlc3Qgc3RyaW5n" } }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests') WHERE document @@ '{ "ikey": { "$code" : "YWJvYm9yYQ==" } }';

/* $gt/$gte (all terms truncated) */
SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests') WHERE document @@ '{ "ikey": { "$gt": { "$code" : "eyAiYSI6ICIxMjM0NSIsICJiIjogMTIzMTIzMTIzMjExMjMxIH0=" } } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests') WHERE document @@ '{ "ikey": { "$gte": { "$code" : "eyAiZXhhbXBsZSI6IDEyMywgInZhbHVlIjogImFib2JvcmEiIH0=" } } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests') WHERE document @@ '{ "ikey": { "$gt": { "$minKey" : 1 } } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests') WHERE document @@ '{ "ikey": { "$gt": { "$code" : "YWJvYm9yYQ==" } } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests') WHERE document @@ '{ "ikey": { "$gte": { "$code" : "YWJvYm9yYQ==" } } }';

EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests') WHERE document @@ '{ "ikey": { "$gt": { "$code" : "eyAiYSI6ICIxMjM0NSIsICJiIjogMTIzMTIzMTIzMjExMjMxIH0=" } } }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests') WHERE document @@ '{ "ikey": { "$gte": { "$code" : "eyAiZXhhbXBsZSI6IDEyMywgInZhbHVlIjogImFib2JvcmEiIH0=" } } }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests') WHERE document @@ '{ "ikey": { "$gt": { "$minKey" : 1 } } }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests') WHERE document @@ '{ "ikey": { "$gt": { "$code" : "YWJvYm9yYQ==" } } }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests') WHERE document @@ '{ "ikey": { "$gte": { "$code" : "YWJvYm9yYQ==" } } }';

/* $lt/$lte (all terms truncated) */
SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests') WHERE document @@ '{ "ikey": { "$lt": { "$code" : "eyAiYSI6ICIxMjM0NSIsICJiIjogMTIzMTIzMTIzMjExMjMxIH0=" } } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests') WHERE document @@ '{ "ikey": { "$lte": { "$code" : "eyAiZXhhbXBsZSI6IDEyMywgInZhbHVlIjogImFib2JvcmEiIH0=" } } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests') WHERE document @@ '{ "ikey": { "$lt": { "$maxKey" : 1 } } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests') WHERE document @@ '{ "ikey": { "$lt": { "$code" : "YWJvYm9yYQ==" } } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests') WHERE document @@ '{ "ikey": { "$lte": { "$code" : "YWJvYm9yYQ==" } } }';

EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests') WHERE document @@ '{ "ikey": { "$lt": { "$code" : "eyAiYSI6ICIxMjM0NSIsICJiIjogMTIzMTIzMTIzMjExMjMxIH0=" } } }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests') WHERE document @@ '{ "ikey": { "$lte": { "$code" : "eyAiZXhhbXBsZSI6IDEyMywgInZhbHVlIjogImFib2JvcmEiIH0=" } } }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests') WHERE document @@ '{ "ikey": { "$lt": { "$maxKey" : 1 } } }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests') WHERE document @@ '{ "ikey": { "$lt": { "$code" : "YWJvYm9yYQ==" } } }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests') WHERE document @@ '{ "ikey": { "$lte": { "$code" : "YWJvYm9yYQ==" } } }';

/* $in/$nin/$ne/$all (all terms truncated) */

-- TODO upgrade libbson to fix seg fault errors

-- SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests') WHERE document @@ '{ "ikey": { "$in": [{ "$code" : "dGhpcyBpcyBhIHRlc3Qgc3RyaW5n" }, { "$code" : "MTIzNDUxMjM0NQ==" }] } }';
-- SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests') WHERE document @@ '{ "ikey": { "$nin": [{ "$code" : "dGhpcyBpcyBhIHRlc3Qgc3RyaW5n" }, { "$code" : "MTIzNDUxMjM0NQ==" }] } }';
-- SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests') WHERE document @@ '{ "ikey": { "$in": [{ "$code" : "MTIzNDUxMjM0NQ==" }] } }';
-- SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests') WHERE document @@ '{ "ikey": { "$nin": [{ "$code" : "MTIzNDUxMjM0NQ==" }] } }';
-- SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests') WHERE document @@ '{ "ikey": { "$ne": { "$code" : "YWJvYm9yYQ==" } } }';
-- SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests') WHERE document @@ '{ "ikey": { "$ne": { "$code" : "eyAiYSI6ICIxMjM0NSIsICJiIjogMTIzMTIzMTIzMjExMjMxIH0=" } } }';
-- SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests') WHERE document @@ '{ "ikey": { "$all": [{ "$code" : "dGhpcyBpcyBhIHRlc3Qgc3RyaW5n" }, { "$code" : "MTIzNDUxMjM0NQ==" }] } }';

-- EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests') WHERE document @@ '{ "ikey": { "$in": [{ "$code" : "dGhpcyBpcyBhIHRlc3Qgc3RyaW5n" }, { "$code" : "MTIzNDUxMjM0NQ==" }] } }';
-- EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests') WHERE document @@ '{ "ikey": { "$nin": [{ "$code" : "dGhpcyBpcyBhIHRlc3Qgc3RyaW5n" }, { "$code" : "MTIzNDUxMjM0NQ==" }] } }';
-- EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests') WHERE document @@ '{ "ikey": { "$in": [{ "$code" : "MTIzNDUxMjM0NQ==" }] } }';
-- EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests') WHERE document @@ '{ "ikey": { "$nin": [{ "$code" : "MTIzNDUxMjM0NQ==" }] } }';
-- EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests') WHERE document @@ '{ "ikey": { "$ne": { "$code" : "YWJvYm9yYQ==" } } }';
-- EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests') WHERE document @@ '{ "ikey": { "$ne": { "$code" : "eyAiYSI6ICIxMjM0NSIsICJiIjogMTIzMTIzMTIzMjExMjMxIH0=" } } }';
-- EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests') WHERE document @@ '{ "ikey": { "$all": [{ "$code" : "dGhpcyBpcyBhIHRlc3Qgc3RyaW5n" }, { "$code" : "MTIzNDUxMjM0NQ==" }] } }';

-- insert $code BSON data in nested documents/arrays with different leaf types
SET documentdb.indexTermLimitOverride to 100;

/* create index on key 'ikey2' */
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "index_truncation_code_tests_2", "indexes": [ { "key": { "ikey2" : 1 }, "name": "ikey_2", "enableLargeIndexKeys": true } ] }');

\d documentdb_data.documents_11601

SELECT documentdb_api.insert_one('db', 'index_truncation_code_tests_2', '{ "_id": 1, "test": "a", "ikey2": { "abcdefg": { "bcdef": { "cdefg": { "dhig": { "$code" : "eyAiZXhhbXBsZSI6IDEyMywgInZhbHVlIjogImFib2JvcmEiIH0=" } } } } }, "item": "A" }');
SELECT documentdb_api.insert_one('db', 'index_truncation_code_tests_2', '{ "_id": 2, "test": "b", "ikey2": [[[[[[{ "a": { "$code" : "eyAiZXhhbXBsZSI6IDEyMywgInZhbHVlIjogImFib2JvcmEiIH0=" } }]]]]]], "item": "A" }');
SELECT documentdb_api.insert_one('db', 'index_truncation_code_tests_2', '{ "_id": 3, "test": "c", "ikey2": [["a"]], "item": "A" }');
SELECT documentdb_api.insert_one('db', 'index_truncation_code_tests_2', '{ "_id": 4, "test": "d", "ikey2": { "abcdefg": { "bcdef": { "cdefg": { "dhig": { "f": { "d": "abcdefghijklmnoasadasdsadasdsadsadasdsadasdaspqrstuvxzabcdefghzasaasaasaasasasasas" } } } } } }, "item": "A" }');
SELECT documentdb_api.insert_one('db', 'index_truncation_code_tests_2', '{ "_id": 5, "test": "e", "ikey2": { "abcdeasasfg": { "bcassasdef": { "cdefsssg": { "dhsssig": { "ssssb": { "csssss": 2 } } } } } }, "item": "A" }');
SELECT documentdb_api.insert_one('db', 'index_truncation_code_tests_2', '{ "_id": 6, "test": "f", "ikey2": { "abcdeasasfg": { "bcassasdef": { "cdefsssg": { "dhsssig": { "ssssb": { "c": [[[{ "a": { "$code" : "eyAiZXhhbXBsZSI6IDEyMywgInZhbHVlIjogImFib2JvcmEiIH0=" } }]]] } } } } } }, "item": "A" }');
SELECT documentdb_api.insert_one('db', 'index_truncation_code_tests_2', '{ "_id": 7, "test": "g", "ikey2": [[[[[[{ "a": { "b": { "c": { "$code" : "eyAiZXhhbXBsZSI6IDEyMywgInZhbHVlIjogImFib2JvcmEiIH0=" } } } } ]]]]]], "item": "A" }');

/* check for index terms */
SELECT did, length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM ( SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms(document, 'ikey2', false, true, true, 100) term, document-> '_id' as did from documentdb_data.documents_11601) docs;

/* testing for equality and index usage */
SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests_2') WHERE document @@ '{ "ikey2": { "abcdefg": { "bcdef": { "cdefg": { "dhig": { "$code" : "eyAiZXhhbXBsZSI6IDEyMywgInZhbHVlIjogImFib2JvcmEiIH0=" } } } } }, "item": "A" }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests_2') WHERE document @@ '{ "ikey2": [[[[[[{ "a": { "$code" : "eyAiZXhhbXBsZSI6IDEyMywgInZhbHVlIjogImFib2JvcmEiIH0=" } }]]]]]], "item": "A" }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests_2') WHERE document @@ '{ "ikey2": [["a"]], "item": "A" }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests_2') WHERE document @@ '{ "ikey2": { "abcdefg": { "bcdef": { "cdefg": { "dhig": { "f": { "d": "abcdefghijklmnoasadasdsadasdsadsadasdsadasdaspqrstuvxzabcdefghzasaasaasaasasasasas" } } } } } }, "item": "A" }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests_2') WHERE document @@ '{ "ikey2": { "abcdeasasfg": { "bcassasdef": { "cdefsssg": { "dhsssig": { "ssssb": { "csssss": 2 } } } } } }, "item": "A" }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests_2') WHERE document @@ '{ "ikey2": { "abcdeasasfg": { "bcassasdef": { "cdefsssg": { "dhsssig": { "ssssb": { "c": [[[{ "a": { "$code" : "eyAiZXhhbXBsZSI6IDEyMywgInZhbHVlIjogImFib2JvcmEiIH0=" } }]]] } } } } } }, "item": "A" }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests_2') WHERE document @@ '{ "ikey2": [[[[[[{ "a": { "b": { "c": { "$code" : "eyAiZXhhbXBsZSI6IDEyMywgInZhbHVlIjogImFib2JvcmEiIH0=" } } } } ]]]]]], "item": "A" }';

EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests_2') WHERE document @@ '{ "ikey2": { "abcdefg": { "bcdef": { "cdefg": { "dhig": { "$code" : "eyAiZXhhbXBsZSI6IDEyMywgInZhbHVlIjogImFib2JvcmEiIH0=" } } } } }, "item": "A" }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests_2') WHERE document @@ '{ "ikey2": [[[[[[{ "a": { "$code" : "eyAiZXhhbXBsZSI6IDEyMywgInZhbHVlIjogImFib2JvcmEiIH0=" } }]]]]]], "item": "A" }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests_2') WHERE document @@ '{ "ikey2": [["a"]], "item": "A" }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests_2') WHERE document @@ '{ "ikey2": { "abcdefg": { "bcdef": { "cdefg": { "dhig": { "f": { "d": "abcdefghijklmnoasadasdsadasdsadsadasdsadasdaspqrstuvxzabcdefghzasaasaasaasasasasas" } } } } } }, "item": "A" }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests_2') WHERE document @@ '{ "ikey2": { "abcdeasasfg": { "bcassasdef": { "cdefsssg": { "dhsssig": { "ssssb": { "csssss": 2 } } } } } }, "item": "A" }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests_2') WHERE document @@ '{ "ikey2": { "abcdeasasfg": { "bcassasdef": { "cdefsssg": { "dhsssig": { "ssssb": { "c": [[[{ "a": { "$code" : "eyAiZXhhbXBsZSI6IDEyMywgInZhbHVlIjogImFib2JvcmEiIH0=" } }]]] } } } } } }, "item": "A" }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_code_tests_2') WHERE document @@ '{ "ikey2": [[[[[[{ "a": { "b": { "c": { "$code" : "eyAiZXhhbXBsZSI6IDEyMywgInZhbHVlIjogImFib2JvcmEiIH0=" } } } } ]]]]]], "item": "A" }';
