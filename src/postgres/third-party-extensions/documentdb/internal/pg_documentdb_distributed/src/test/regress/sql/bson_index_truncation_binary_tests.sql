SET search_path TO documentdb_api_catalog, documentdb_core;
SET citus.next_shard_id TO 1150000;
SET documentdb.next_collection_id TO 11500;
SET documentdb.next_collection_index_id TO 11500;

-- Set configs to something reasonable for testing.
SET documentdb.indexTermLimitOverride to 20;

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "index_truncation_binary_tests", "indexes": [ { "key": { "ikey": 1 }, "name": "ikey_1", "enableLargeIndexKeys": true } ] }');

\d documentdb_data.documents_11500

-- Insert binary BSON data with all subtypes
SELECT documentdb_api.insert_one('db', 'index_truncation_binary_tests', '{ "_id": 1, "ikey": { "$binary" : { "base64": "eyAiZXhhbXBsZSI6IDEyMywgInZhbHVlIjogImFib2JvcmEiIH0=", "subType" : "00" } }, "item": "A" }');
SELECT documentdb_api.insert_one('db', 'index_truncation_binary_tests', '{ "_id": 2, "ikey": { "$binary" : { "base64": "eyAiYSI6IDEsICJiIjogMyB9", "subType" : "01" } } }');
SELECT documentdb_api.insert_one('db', 'index_truncation_binary_tests', '{ "_id": 3, "ikey": { "$binary" : { "base64": "eyAiZXhhbXBsZSI6IDEyMywgInZhbHVlIjogImFib2JvcmEiIH0=", "subType" : "04" } } }');
SELECT documentdb_api.insert_one('db', 'index_truncation_binary_tests', '{ "_id": 4, "ikey": { "$binary" : { "base64": "YWJvYm9yYQ==", "subType" : "05" } } }');
SELECT documentdb_api.insert_one('db', 'index_truncation_binary_tests', '{ "_id": 5, "ikey": { "$binary" : { "base64": "eyAiYSI6IDEsICJiIjogMyB9", "subType" : "06" } } }');
SELECT documentdb_api.insert_one('db', 'index_truncation_binary_tests', '{ "_id": 6, "ikey": { "$binary" : { "base64": "eyAiZXhhbXBsZSI6IDEyMywgInZhbHVlIjogImFib2JvcmEiIH0=", "subType" : "07" } }, "item": "F", "customKey": 6 }');
SELECT documentdb_api.insert_one('db', 'index_truncation_binary_tests', '{ "_id": 7, "ikey": { "$binary" : { "base64": "eyAiYSI6ICIxMjM0NSIsICJiIjogMTIzMTIzMTIzMjExMjMxIH0=", "subType" : "01" } }, "item": "G", "customKey": 6 }');
SELECT documentdb_api.insert_one('db', 'index_truncation_binary_tests', '{ "_id": 8, "ikey": { "$binary" : { "base64": "dGhpcyBpcyBhIHRlc3Qgc3RyaW5n", "subType" : "00" } } }');
SELECT documentdb_api.insert_one('db', 'index_truncation_binary_tests', '{ "_id": 9, "ikey": { "$binary" : { "base64": "eyAiYSI6ICIxMjM0NSIsICJiIjogMTIzMTIzMTIzMjExMjMxIH0=", "subType" : "04" } }, "item": "H", "customKey": 8 }');
SELECT documentdb_api.insert_one('db', 'index_truncation_binary_tests', '{ "_id": 10, "ikey": { "$binary" : { "base64": "dGhpcyBpcyBhIHRlc3Qgc3RyaW5n", "subType" : "01" } }, "item": "H", "customKey": 9 }');

/* All terms should be truncated */
SELECT did, length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM ( SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms(document, 'ikey', false, true, true, 20) term, document-> '_id' as did from documentdb_data.documents_11500) docs;

/* Only a subset of the terms should be truncated */
SELECT did, length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM ( SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms(document, 'ikey', false, true, true, 40) term, document-> '_id' as did from documentdb_data.documents_11500) docs;

/* $eq (all terms truncated) */

SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$binary" : { "base64": "eyAiZXhhbXBsZSI6IDEyMywgInZhbHVlIjogImFib2JvcmEiIH0=", "subType" : "00" } } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$binary" : { "base64": "eyAiZXhhbXBsZSI6IDEyMywgInZhbHVlIjogImFib2JvcmEiIH0=", "subType" : "04" } } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$binary" : { "base64": "eyAiZXhhbXBsZSI6IDEyMywgInZhbHVlIjogImFib2JvcmEiIH0=", "subType" : "07" } } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$binary": { "base64": "eyAiYSI6IDEsICJiIjogMyB9", "subType" : "01" } } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$binary": { "base64": "eyAiYSI6IDEsICJiIjogMyB9", "subType" : "06" } } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$binary" : { "base64": "YWJvYm9yYQ==", "subType" : "05" } } }';

EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$binary" : { "base64": "eyAiZXhhbXBsZSI6IDEyMywgInZhbHVlIjogImFib2JvcmEiIH0=", "subType" : "00" } } }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$binary" : { "base64": "eyAiZXhhbXBsZSI6IDEyMywgInZhbHVlIjogImFib2JvcmEiIH0=", "subType" : "04" } } }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$binary" : { "base64": "eyAiZXhhbXBsZSI6IDEyMywgInZhbHVlIjogImFib2JvcmEiIH0=", "subType" : "07" } } }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$binary": { "base64": "eyAiYSI6IDEsICJiIjogMyB9", "subType" : "01" } } }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$binary": { "base64": "eyAiYSI6IDEsICJiIjogMyB9", "subType" : "06" } } }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$binary" : { "base64": "YWJvYm9yYQ==", "subType" : "05" } } }';

/* $gt/$gte (all terms truncated) */
SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$gt": { "$binary" : { "base64": "eyAiYSI6ICIxMjM0NSIsICJiIjogMTIzMTIzMTIzMjExMjMxIH0=", "subType" : "01" } } } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$gte": { "$binary" : { "base64": "eyAiYSI6ICIxMjM0NSIsICJiIjogMTIzMTIzMTIzMjExMjMxIH0=", "subType" : "01" } } } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$gt": { "$minKey" : 1 } } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$gt": { "$binary" : { "base64": "YWJvYm9yYQ==", "subType" : "05" } } } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$gte": { "$binary" : { "base64": "YWJvYm9yYQ==", "subType" : "05" } } } }';

EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$gt": { "$binary" : { "base64": "eyAiYSI6ICIxMjM0NSIsICJiIjogMTIzMTIzMTIzMjExMjMxIH0=", "subType" : "01" } } } }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$gte": { "$binary" : { "base64": "eyAiYSI6ICIxMjM0NSIsICJiIjogMTIzMTIzMTIzMjExMjMxIH0=", "subType" : "00" } } } }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$gt": { "$minKey" : 1 } } }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$gt": { "$binary" : { "base64": "YWJvYm9yYQ==", "subType" : "05" } } } }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$gte": { "$binary" : { "base64": "YWJvYm9yYQ==", "subType" : "05" } } } }';

/* $lt/$lte (all terms truncated) */
SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$lt": { "$binary" : { "base64": "eyAiYSI6ICIxMjM0NSIsICJiIjogMTIzMTIzMTIzMjExMjMxIH0=", "subType" : "01" } } } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$lte": { "$binary" : { "base64": "eyAiYSI6ICIxMjM0NSIsICJiIjogMTIzMTIzMTIzMjExMjMxIH0=", "subType" : "01" } } } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$lt": { "$maxKey" : 1 } } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$lt": { "$binary" : { "base64": "YWJvYm9yYQ==", "subType" : "05" } } } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$lte": { "$binary" : { "base64": "YWJvYm9yYQ==", "subType" : "05" } } } }';

EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$lt": { "$binary" : { "base64": "eyAiYSI6ICIxMjM0NSIsICJiIjogMTIzMTIzMTIzMjExMjMxIH0=", "subType" : "01" } } } }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$lte": { "$binary" : { "base64": "eyAiYSI6ICIxMjM0NSIsICJiIjogMTIzMTIzMTIzMjExMjMxIH0=", "subType" : "01" } } } }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$lt": { "$maxKey" : 1 } } }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$lt": { "$binary" : { "base64": "YWJvYm9yYQ==", "subType" : "05" } } } }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$lte": { "$binary" : { "base64": "YWJvYm9yYQ==", "subType" : "05" } } } }';

/* $in/$nin/$ne/$all (all terms truncated) */
SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$in": [{ "$binary" : { "base64": "dGhpcyBpcyBhIHRlc3Qgc3RyaW5n", "subType" : "01" } }, { "$binary" : { "base64": "MTIzNDUxMjM0NQ==", "subType" : "07" } }] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$nin": [{ "$binary" : { "base64": "dGhpcyBpcyBhIHRlc3Qgc3RyaW5n", "subType" : "01" } }, { "$binary" : { "base64": "MTIzNDUxMjM0NQ==", "subType" : "07" } }] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$in": [{ "$binary" : { "base64": "MTIzNDUxMjM0NQ==", "subType" : "07" } }] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$nin": [{ "$binary" : { "base64": "MTIzNDUxMjM0NQ==", "subType" : "07" } }] } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$ne": { "$binary" : { "base64": "YWJvYm9yYQ==", "subType" : "05" } } } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$ne": { "$binary" : { "base64": "eyAiYSI6ICIxMjM0NSIsICJiIjogMTIzMTIzMTIzMjExMjMxIH0=", "subType" : "01" } } } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$all": [{ "$binary" : { "base64": "dGhpcyBpcyBhIHRlc3Qgc3RyaW5n", "subType" : "01" } }, { "$binary" : { "base64": "MTIzNDUxMjM0NQ==", "subType" : "07" } }] } }';

EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$in": [{ "$binary" : { "base64": "dGhpcyBpcyBhIHRlc3Qgc3RyaW5n", "subType" : "01" } }, { "$binary" : { "base64": "MTIzNDUxMjM0NQ==", "subType" : "07" } }] } }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$nin": [{ "$binary" : { "base64": "dGhpcyBpcyBhIHRlc3Qgc3RyaW5n", "subType" : "01" } }, { "$binary" : { "base64": "MTIzNDUxMjM0NQ==", "subType" : "07" } }] } }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$in": [{ "$binary" : { "base64": "MTIzNDUxMjM0NQ==", "subType" : "07" } }] } }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$nin": [{ "$binary" : { "base64": "MTIzNDUxMjM0NQ==", "subType" : "07" } }] } }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$ne": { "$binary" : { "base64": "YWJvYm9yYQ==", "subType" : "05" } } } }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$ne": { "$binary" : { "base64": "eyAiYSI6ICIxMjM0NSIsICJiIjogMTIzMTIzMTIzMjExMjMxIH0=", "subType" : "01" } } } }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$all": [{ "$binary" : { "base64": "dGhpcyBpcyBhIHRlc3Qgc3RyaW5n", "subType" : "01" } }, { "$binary" : { "base64": "MTIzNDUxMjM0NQ==", "subType" : "07" } }] } }';

/* $bitsAllClear (all terms truncated) */
SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$bitsAllClear": { "$binary" : { "base64": "dGhpcyBpcyBhIHRlc3Qgc3RyaW5n", "subType" : "01" } } } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$bitsAllClear": 0 } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$bitsAllClear": 1 } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$bitsAllClear": 32 } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$bitsAllClear": 54 } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$bitsAllClear": { "$binary": { "base64": "////////////////////////////", "subType": "02"} } } }';

EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$bitsAllClear": { "$binary" : { "base64": "dGhpcyBpcyBhIHRlc3Qgc3RyaW5n", "subType" : "01" } } } }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$bitsAllClear": 0 } }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$bitsAllClear": 1 } }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$bitsAllClear": 32 } }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$bitsAllClear": 54 } }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$bitsAllClear": { "$binary": { "base64": "////////////////////////////", "subType": "02"} } } }';

/* $bitsAllClear (all terms truncated) */
SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$bitsAnyClear": { "$binary" : { "base64": "dGhpcyBpcyBhIHRlc3Qgc3RyaW5n", "subType" : "01" } } } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$bitsAnyClear": { "$binary" : { "base64": "YWJvYm9yYQ==", "subType" : "05" } } } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$bitsAnyClear": { "$binary" : { "base64": "////////////////////////////", "subType" : "02" } } } }';

EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$bitsAnyClear": { "$binary" : { "base64": "dGhpcyBpcyBhIHRlc3Qgc3RyaW5n", "subType" : "01" } } } }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$bitsAnyClear": { "$binary" : { "base64": "YWJvYm9yYQ==", "subType" : "05" } } } }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$bitsAnyClear": { "$binary" : { "base64": "////////////////////////////", "subType" : "02" } } } }';

/* $bitsAllSet (all terms truncated) */
SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$bitsAllSet": { "$binary" : { "base64": "dGhpcyBpcyBhIHRlc3Qgc3RyaW5n", "subType" : "01" } } } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$bitsAllSet": { "$binary": { "base64": "YWJvYm9yYQ==", "subType": "05"}} } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$bitsAllSet": 1 } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$bitsAllSet": 32 } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$bitsAllSet": 54 } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$bitsAllSet": { "$binary": { "base64": "////////////////////////////", "subType": "02"} } } }';

EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$bitsAllSet": { "$binary" : { "base64": "dGhpcyBpcyBhIHRlc3Qgc3RyaW5n", "subType" : "01" } } } }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$bitsAllSet": { "$binary": { "base64": "YWJvYm9yYQ==", "subType": "05"}} } }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$bitsAllSet": 1 } }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$bitsAllSet": 32 } }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$bitsAllSet": 54 } }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$bitsAllSet": { "$binary": { "base64": "////////////////////////////", "subType": "02"} } } }';

/* $bitsAnySet (all terms truncated) */
SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$bitsAnySet": { "$binary" : { "base64": "dGhpcyBpcyBhIHRlc3Qgc3RyaW5n", "subType" : "01" } } } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$bitsAnySet": { "$binary" : { "base64": "YWJvYm9yYQ==", "subType" : "05" } } } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$bitsAnySet": { "$binary" : { "base64": "////////////////////////////", "subType" : "02" } } } }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$bitsAnySet": 0 } }';

EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$bitsAnySet": { "$binary" : { "base64": "dGhpcyBpcyBhIHRlc3Qgc3RyaW5n", "subType" : "01" } } } }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$bitsAnySet": { "$binary" : { "base64": "YWJvYm9yYQ==", "subType" : "05" } } } }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$bitsAnySet": { "$binary" : { "base64": "////////////////////////////", "subType" : "02" } } } }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests') WHERE document @@ '{ "ikey": { "$bitsAnySet": 0 } }';

-- insert binary BSON data in nested documents/arrays with different leaf types
SET documentdb.enableIndexTermTruncationOnNestedObjects to ON;
SET documentdb.indexTermLimitOverride to 100;

/* create index on key 'ikey2' */
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "index_truncation_binary_tests_2", "indexes": [ { "key": { "ikey2" : 1 }, "name": "ikey_2", "enableLargeIndexKeys": true } ] }');

\d documentdb_data.documents_11501

SELECT documentdb_api.insert_one('db', 'index_truncation_binary_tests_2', '{ "_id": 1, "test": "a", "ikey2": { "abcdefg": { "bcdef": { "cdefg": { "dhig": { "$binary" : { "base64": "eyAiZXhhbXBsZSI6IDEyMywgInZhbHVlIjogImFib2JvcmEiIH0=", "subType" : "00" } } } } } }, "item": "A" }');
SELECT documentdb_api.insert_one('db', 'index_truncation_binary_tests_2', '{ "_id": 2, "test": "b", "ikey2": [[[[[[{ "$binary" : { "base64": "eyAiZXhhbXBsZSI6IDEyMywgInZhbHVlIjogImFib2JvcmEiIH0=", "subType" : "00" } }]]]]]], "item": "A" }');
SELECT documentdb_api.insert_one('db', 'index_truncation_binary_tests_2', '{ "_id": 3, "test": "c", "ikey2": [["a"]], "item": "A" }');
SELECT documentdb_api.insert_one('db', 'index_truncation_binary_tests_2', '{ "_id": 4, "test": "d", "ikey2": { "abcdefg": { "bcdef": { "cdefg": { "dhig": { "f": { "d": "abcdefghijklmnoasadasdsadasdsadsadasdsadasdaspqrstuvxzabcdefghzasaasaasaasasasasas" } } } } } }, "item": "A" }');
SELECT documentdb_api.insert_one('db', 'index_truncation_binary_tests_2', '{ "_id": 5, "test": "e", "ikey2": { "abcdeasasfg": { "bcassasdef": { "cdefsssg": { "dhsssig": { "ssssb": { "csssss": 2 } } } } } }, "item": "A" }');
SELECT documentdb_api.insert_one('db', 'index_truncation_binary_tests_2', '{ "_id": 6, "test": "f", "ikey2": { "abcdeasasfg": { "bcassasdef": { "cdefsssg": { "dhsssig": { "ssssb": { "c": [[[{ "$binary" : { "base64": "eyAiZXhhbXBsZSI6IDEyMywgInZhbHVlIjogImFib2JvcmEiIH0=", "subType" : "00" } }]]] } } } } } }, "item": "A" }');
SELECT documentdb_api.insert_one('db', 'index_truncation_binary_tests_2', '{ "_id": 7, "test": "g", "ikey2": [[[[[[{ "a": { "b": { "c": { "$binary" : { "base64": "eyAiZXhhbXBsZSI6IDEyMywgInZhbHVlIjogImFib2JvcmEiIH0=", "subType" : "00" } } } } } ]]]]]], "item": "A" }');

/* check for index terms */
SELECT did, length(bson_dollar_project(term, '{ "t": 0 }')::bytea), term FROM ( SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms(document, 'ikey2', false, true, true, 100) term, document-> '_id' as did from documentdb_data.documents_11501) docs;

/* testing for equality and index usage */
SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests_2') WHERE document @@ '{ "ikey2": { "abcdefg": { "bcdef": { "cdefg": { "dhig": { "$binary" : { "base64": "eyAiZXhhbXBsZSI6IDEyMywgInZhbHVlIjogImFib2JvcmEiIH0=", "subType" : "00" } } } } } }, "item": "A" }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests_2') WHERE document @@ '{ "ikey2": [[[[[[{ "$binary" : { "base64": "eyAiZXhhbXBsZSI6IDEyMywgInZhbHVlIjogImFib2JvcmEiIH0=", "subType" : "00" } }]]]]]], "item": "A" }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests_2') WHERE document @@ '{ "ikey2": [["a"]], "item": "A" }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests_2') WHERE document @@ '{ "ikey2": { "abcdefg": { "bcdef": { "cdefg": { "dhig": { "f": { "d": "abcdefghijklmnoasadasdsadasdsadsadasdsadasdaspqrstuvxzabcdefghzasaasaasaasasasasas" } } } } } }, "item": "A" }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests_2') WHERE document @@ '{ "ikey2": { "abcdeasasfg": { "bcassasdef": { "cdefsssg": { "dhsssig": { "ssssb": { "csssss": 2 } } } } } }, "item": "A" }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests_2') WHERE document @@ '{ "ikey2": { "abcdeasasfg": { "bcassasdef": { "cdefsssg": { "dhsssig": { "ssssb": { "c": [[[{ "$binary" : { "base64": "eyAiZXhhbXBsZSI6IDEyMywgInZhbHVlIjogImFib2JvcmEiIH0=", "subType" : "00" } }]]] } } } } } }, "item": "A" }';
SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests_2') WHERE document @@ '{ "ikey2": [[[[[[{ "a": { "b": { "c": { "$binary" : { "base64": "eyAiZXhhbXBsZSI6IDEyMywgInZhbHVlIjogImFib2JvcmEiIH0=", "subType" : "00" } } } } } ]]]]]], "item": "A" }';

EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests_2') WHERE document @@ '{ "ikey2": { "abcdefg": { "bcdef": { "cdefg": { "dhig": { "$binary" : { "base64": "eyAiZXhhbXBsZSI6IDEyMywgInZhbHVlIjogImFib2JvcmEiIH0=", "subType" : "00" } } } } } }, "item": "A" }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests_2') WHERE document @@ '{ "ikey2": [[[[[[{ "$binary" : { "base64": "eyAiZXhhbXBsZSI6IDEyMywgInZhbHVlIjogImFib2JvcmEiIH0=", "subType" : "00" } }]]]]]], "item": "A" }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests_2') WHERE document @@ '{ "ikey2": [["a"]], "item": "A" }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests_2') WHERE document @@ '{ "ikey2": { "abcdefg": { "bcdef": { "cdefg": { "dhig": { "f": { "d": "abcdefghijklmnoasadasdsadasdsadsadasdsadasdaspqrstuvxzabcdefghzasaasaasaasasasasas" } } } } } }, "item": "A" }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests_2') WHERE document @@ '{ "ikey2": { "abcdeasasfg": { "bcassasdef": { "cdefsssg": { "dhsssig": { "ssssb": { "csssss": 2 } } } } } }, "item": "A" }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests_2') WHERE document @@ '{ "ikey2": { "abcdeasasfg": { "bcassasdef": { "cdefsssg": { "dhsssig": { "ssssb": { "c": [[[{ "$binary" : { "base64": "eyAiZXhhbXBsZSI6IDEyMywgInZhbHVlIjogImFib2JvcmEiIH0=", "subType" : "00" } }]]] } } } } } }, "item": "A" }';
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api.collection('db', 'index_truncation_binary_tests_2') WHERE document @@ '{ "ikey2": [[[[[[{ "a": { "b": { "c": { "$binary" : { "base64": "eyAiZXhhbXBsZSI6IDEyMywgInZhbHVlIjogImFib2JvcmEiIH0=", "subType" : "00" } } } } } ]]]]]], "item": "A" }';
