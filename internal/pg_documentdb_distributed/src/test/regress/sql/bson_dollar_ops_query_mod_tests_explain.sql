set search_path to helio_core,helio_api,helio_api_catalog,pg_catalog;
SET citus.next_shard_id TO 9600000;
SET helio_api.next_collection_id TO 9600;
SET helio_api.next_collection_index_id TO 9600;

--insert data
SELECT helio_api.insert_one('db','dollarmodtests_explain','{"_id": 100, "a": 10}', NULL);
SELECT helio_api.insert_one('db','dollarmodtests_explain','{"_id": 201, "a": [-10, -11]}', NULL);
SELECT helio_api.insert_one('db','dollarmodtests_explain','{"_id": 302, "a": [[[10, 11], [5]]]}', NULL);
SELECT helio_api.insert_one('db','dollarmodtests_explain','{"_id": 104, "a": {"b": [10, 11], "c": 11}}', NULL);
SELECT helio_api.insert_one('db','dollarmodtests_explain','{"_id": 105, "a": {"b": { "c": [10, 11] }}}', NULL);
SELECT helio_api.insert_one('db','dollarmodtests_explain','{"_id": 202, "a": {"b": -10}}', NULL);
SELECT helio_api.insert_one('db','dollarmodtests_explain','{"_id": 204, "a": {"b": [-10, -11], "c": -11}}', NULL);
SELECT helio_api.insert_one('db','dollarmodtests_explain','{"_id": 106, "a": [ {"b": 10}, {"c": 11}]}', NULL);
SELECT helio_api.insert_one('db','dollarmodtests_explain','{"_id": 110, "a": {"$numberDecimal" : "10.6"}}', NULL);
SELECT helio_api.insert_one('db','dollarmodtests_explain','{"_id": 207, "a": {"$numberInt" : "-10"}}', NULL);
SELECT helio_api.insert_one('db','dollarmodtests_explain','{"_id": 311, "a": false}', NULL);
SELECT helio_api.insert_one('db','dollarmodtests_explain','{"_id": 313, "a": ["Hello", "World"]}', NULL);
SELECT helio_api.insert_one('db','dollarmodtests_explain','{"_id": 314, "a": { "$timestamp": { "t": 1234567890, "i": 1 }}}', NULL);
SELECT helio_api.insert_one('db','dollarmodtests_explain','{"_id": 315, "a": { "$date": { "$numberLong" : "1234567890000" }}}', NULL);
SELECT helio_api.insert_one('db','dollarmodtests_explain','{"_id": 123, "a": NaN}', NULL);
SELECT helio_api.insert_one('db','dollarmodtests_explain','{"_id": 126, "a": [null, NaN]}', NULL);
SELECT helio_api.insert_one('db','dollarmodtests_explain','{"_id": 127, "a": {"$numberDecimal" : "NaN"}}', NULL);

-- Explain Plan on runtime
SELECT helio_distributed_test_helpers.drop_primary_key('db', 'dollarmodtests_explain');
BEGIN;
    set local enable_seqscan TO ON;
    set local helio_api.forceRumIndexScantoBitmapHeapScan TO OFF;

    EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'dollarmodtests_explain') where document @@ '{ "a" : {"$mod" : [5,0]} }';
    EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'dollarmodtests_explain') where document @@ '{ "a.b" : {"$mod" : [5,0]} }';
    EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'dollarmodtests_explain') where document @@ '{ "a.c" : {"$mod" : [5,0]} }';
    EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'dollarmodtests_explain') where document @@ '{ "a.b.c" : {"$mod" : [5,0]} }';
    EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'dollarmodtests_explain') where document @@ '{ "a" : {"$mod" : [3,-2]} }';
    EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'dollarmodtests_explain') where document @@ '{ "a.b" : {"$mod" : [3,-2]} }';
    EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'dollarmodtests_explain') where document @@ '{ "a.c" : {"$mod" : [3,-2]} }';
    EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'dollarmodtests_explain') where document @@ '{ "a.b.c" : {"$mod" : [3,-2]} }';
END;

-- Explain Plan on Index 
SELECT helio_api_internal.create_indexes_non_concurrently('db', helio_distributed_test_helpers.generate_create_index_arg('dollarmodtests_explain', 'index_mod_a1', '{"a": 1}'), TRUE);
SELECT helio_api_internal.create_indexes_non_concurrently('db', helio_distributed_test_helpers.generate_create_index_arg('dollarmodtests_explain', 'index_mod_ab1', '{"a.b": -1}'), TRUE);
SELECT helio_api_internal.create_indexes_non_concurrently('db', helio_distributed_test_helpers.generate_create_index_arg('dollarmodtests_explain', 'index_mod_ac1', '{"a.c": 1}'), TRUE);
SELECT helio_api_internal.create_indexes_non_concurrently('db', helio_distributed_test_helpers.generate_create_index_arg('dollarmodtests_explain', 'index_mod_abc1', '{"a.b.c": -1}'), TRUE);

BEGIN;
set local enable_seqscan TO OFF;
set local helio_api.forceRumIndexScantoBitmapHeapScan TO OFF;
    EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'dollarmodtests_explain') where document @@ '{ "a" : {"$mod" : [5,0]} }';
    EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'dollarmodtests_explain') where document @@ '{ "a.b" : {"$mod" : [5,0]} }';
    EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'dollarmodtests_explain') where document @@ '{ "a.c" : {"$mod" : [5,0]} }';
    EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'dollarmodtests_explain') where document @@ '{ "a.b.c" : {"$mod" : [5,0]} }';
    EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'dollarmodtests_explain') where document @@ '{ "a" : {"$mod" : [3,-2]} }';
    EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'dollarmodtests_explain') where document @@ '{ "a.b" : {"$mod" : [3,-2]} }';
    EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'dollarmodtests_explain') where document @@ '{ "a.c" : {"$mod" : [3,-2]} }';
    EXPLAIN (COSTS OFF)  SELECT document FROM helio_api.collection('db', 'dollarmodtests_explain') where document @@ '{ "a.b.c" : {"$mod" : [3,-2]} }';
END;