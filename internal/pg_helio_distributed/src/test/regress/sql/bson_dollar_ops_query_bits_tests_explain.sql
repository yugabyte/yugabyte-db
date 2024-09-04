set search_path to helio_core,helio_api,helio_api_catalog,pg_catalog;
SET citus.next_shard_id TO 9500000;
SET helio_api.next_collection_id TO 9500;
SET helio_api.next_collection_index_id TO 9500;
SELECT helio_api.create_collection('db', 'bitwiseOperators');

--insert data

SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 1, "a": 0}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 2, "a": 1}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 3, "a": 54}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 4, "a": 88}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 5, "a": 255}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id":"9", "a": {"$binary": { "base64": "AAAAAAAAAAAAAAAAAAAAAAAAAAAA", "subType": "01"}}}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id":"10", "a": {"$binary": { "base64": "AANgAAAAAAAAAAAAAAAAAAAAAAAA", "subType": "01"}}}', NULL);

-- Explain Plan on runtime
SELECT helio_distributed_test_helpers.drop_primary_key('db', 'bitwiseOperators');
BEGIN;
set local enable_seqscan TO ON;
set local helio_api.forceRumIndexScantoBitmapHeapScan TO OFF;

--$bitsAllClear runtime
EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : 0} }';
EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : 1, "$lt": 10} }';
EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : [1,5,7]} }';
EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : { "$binary": { "base64": "AAAAAAAAAAAAAAAAAAAAAAAAAAAA", "subType": "01"} } } }';
EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : { "$binary": { "base64": "AAyfAAAAAAAAAAAAAAAAAAAAAAAA", "subType": "01"} } } }';

--$bitsAnyClear runtime
EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : 0} }';
EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : 1, "$lt": 10} }';
EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : [1,5,7]} }';
EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : { "$binary": { "base64": "AAAAAAAAAAAAAAAAAAAAAAAAAAAA", "subType": "01"} } } }';
EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : { "$binary": { "base64": "AAyfAAAAAAAAAAAAAAAAAAAAAAAA", "subType": "01"} } } }';

--$bitsAllSet runtime
EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : 0} }';
EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : 1, "$lt": 10} }';
EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : [1,5,7]} }';
EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : { "$binary": { "base64": "AAAAAAAAAAAAAAAAAAAAAAAAAAAA", "subType": "01"} } } }';
EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : { "$binary": { "base64": "AAyfAAAAAAAAAAAAAAAAAAAAAAAA", "subType": "01"} } } }';

--$bitsAnySet runtime
EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : 0} }';
EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : 1, "$lt": 10} }';
EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : [1,5,7]} }';
EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : { "$binary": { "base64": "AAAAAAAAAAAAAAAAAAAAAAAAAAAA", "subType": "01"} } } }';
EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : { "$binary": { "base64": "AAyfAAAAAAAAAAAAAAAAAAAAAAAA", "subType": "01"} } } }';

END;

-- Explain Plan on Index 
SELECT helio_api_internal.create_indexes_non_concurrently('db', helio_distributed_test_helpers.generate_create_index_arg('bitwiseOperators', 'index_1', '{"a": 1}'), TRUE);

BEGIN;
set local enable_seqscan TO OFF;
set local helio_api.forceRumIndexScantoBitmapHeapScan TO OFF;

--$bitsAllClear Index
EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : 0} }';
EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : 1, "$lt": 10} }';
EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : [1,5,7]} }';
EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : { "$binary": { "base64": "AAAAAAAAAAAAAAAAAAAAAAAAAAAA", "subType": "01"} } } }';
EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : { "$binary": { "base64": "AAyfAAAAAAAAAAAAAAAAAAAAAAAA", "subType": "01"} } } }';

--$bitsAnyClear Index
EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : 0} }';
EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : 1, "$lt": 10} }';
EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : [1,5,7]} }';
EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : { "$binary": { "base64": "AAAAAAAAAAAAAAAAAAAAAAAAAAAA", "subType": "01"} } } }';
EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : { "$binary": { "base64": "AAyfAAAAAAAAAAAAAAAAAAAAAAAA", "subType": "01"} } } }';

--$bitsAllSet Index
EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : 0} }';
EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : 1, "$lt": 10} }';
EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : [1,5,7]} }';
EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : { "$binary": { "base64": "AAAAAAAAAAAAAAAAAAAAAAAAAAAA", "subType": "01"} } } }';
EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : { "$binary": { "base64": "AAyfAAAAAAAAAAAAAAAAAAAAAAAA", "subType": "01"} } } }';

--$bitsAnySet Index
EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : 0} }';
EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : 1, "$lt": 10} }';
EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : [1,5,7]} }';
EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : { "$binary": { "base64": "AAAAAAAAAAAAAAAAAAAAAAAAAAAA", "subType": "01"} } } }';
EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : { "$binary": { "base64": "AAyfAAAAAAAAAAAAAAAAAAAAAAAA", "subType": "01"} } } }';

END;