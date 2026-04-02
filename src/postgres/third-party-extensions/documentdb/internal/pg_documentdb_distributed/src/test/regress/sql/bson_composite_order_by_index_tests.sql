SET search_path TO documentdb_api,documentdb_api_catalog,documentdb_api_internal,documentdb_core;

SET citus.next_shard_id TO 680000;
SET documentdb.next_collection_id TO 68000;
SET documentdb.next_collection_index_id TO 68000;

set documentdb.enableExtendedExplainPlans to on;

-- if documentdb_extended_rum exists, set alternate index handler
SELECT pg_catalog.set_config('documentdb.alternate_index_handler_name', 'extended_rum', false), extname FROM pg_extension WHERE extname = 'documentdb_extended_rum';


SELECT documentdb_api.drop_collection('comp_db', 'query_orderby') IS NOT NULL;
SELECT documentdb_api.create_collection('comp_db', 'query_orderby');

SELECT documentdb_api_internal.create_indexes_non_concurrently('comp_db', '{ "createIndexes": "query_orderby", "indexes": [ { "key": { "a": 1, "c": 1 }, "storageEngine": { "enableCompositeTerm": true }, "name": "a_c" }] }', true);

\d documentdb_data.documents_68001

-- now insert some sample docs
SELECT documentdb_api.insert_one('comp_db', 'query_orderby', '{ "_id": 1, "a": 1, "c": 1 }');
SELECT documentdb_api.insert_one('comp_db', 'query_orderby', '{ "_id": 2, "a": -2, "c": 2 }');
SELECT documentdb_api.insert_one('comp_db', 'query_orderby', '{ "_id": 3, "a": "string", "c": 2 }');
SELECT documentdb_api.insert_one('comp_db', 'query_orderby', '{ "_id": 4, "a": { "$minKey": 1 }, "c": 2 }');
SELECT documentdb_api.insert_one('comp_db', 'query_orderby', '{ "_id": 5, "a": true, "c": 2 }');
SELECT documentdb_api.insert_one('comp_db', 'query_orderby', '{ "_id": 6, "a": null, "c": 2 }');
SELECT documentdb_api.insert_one('comp_db', 'query_orderby', '{ "_id": 7, "a": { "b": 1 }, "c": 2 }');


ANALYZE documentdb_data.documents_68001;

set enable_seqscan to off;

-- no pushdown
EXPLAIN (ANALYZE ON, COSTS OFF, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_find('comp_db', '{ "find": "query_orderby", "filter": { "a": { "$exists": true }}, "sort": { "a": 1 } }');

-- now the order by succeeds with an index scan
SET documentdb.enableIndexOrderbyPushdown to on;
EXPLAIN (ANALYZE ON, COSTS OFF, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_find('comp_db', '{ "find": "query_orderby", "filter": { "a": { "$exists": true }}, "sort": { "a": 1 } }');

-- point read on _id with order by was broken initially and was fixed in 108 
EXPLAIN (ANALYZE ON, COSTS OFF, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_find('comp_db', '{ "find": "query_orderby", "filter": { "_id": 1}, "sort": { "_id": 1 } }');

-- do a reverse walk
EXPLAIN (ANALYZE ON, COSTS OFF, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_find('comp_db', '{ "find": "query_orderby", "filter": { "a": { "$exists": true }}, "sort": { "a": -1 } }');

-- now check the correctness (things are ordered)
SELECT document FROM bson_aggregation_find('comp_db', '{ "find": "query_orderby", "filter": { "a": { "$exists": true }}, "sort": { "a": 1 } }');
SELECT document FROM bson_aggregation_find('comp_db', '{ "find": "query_orderby", "filter": { "a": { "$exists": true }}, "sort": { "a": -1 } }');

-- validate type bracketing
SELECT document FROM bson_aggregation_find('comp_db', '{ "find": "query_orderby", "filter": { "a": { "$gt": -100 }}, "sort": { "a": 1 } }');
SELECT document FROM bson_aggregation_find('comp_db', '{ "find": "query_orderby", "filter": { "a": { "$gte": -2 }}, "sort": { "a": 1 } }');
SELECT document FROM bson_aggregation_find('comp_db', '{ "find": "query_orderby", "filter": { "a": { "$gt": false }}, "sort": { "a": 1 } }');
SELECT document FROM bson_aggregation_find('comp_db', '{ "find": "query_orderby", "filter": { "a": { "$gt": true }}, "sort": { "a": 1 } }');
SELECT document FROM bson_aggregation_find('comp_db', '{ "find": "query_orderby", "filter": { "a": { "$gte": true }}, "sort": { "a": 1 } }');

-- validate runtime recheck functions
SELECT document FROM bson_aggregation_find('comp_db', '{ "find": "query_orderby", "filter": { "a": { "$regex": "^str" }}, "sort": { "a": 1 } }');
SELECT document FROM bson_aggregation_find('comp_db', '{ "find": "query_orderby", "filter": { "a": { "$regex": "^str" }}, "sort": { "a": -1 } }');

SELECT document FROM bson_aggregation_find('comp_db', '{ "find": "query_orderby", "filter": { "a": { "$regex": "^abc" }}, "sort": { "a": 1 } }');
SELECT document FROM bson_aggregation_find('comp_db', '{ "find": "query_orderby", "filter": { "a": { "$regex": "^abc" }}, "sort": { "a": -1 } }');

-- validate ordering with truncation
SELECT documentdb_api.insert_one('comp_db', 'query_orderby', FORMAT('{ "_id": 10, "a": "%s", "c": "%s" }', 'abcde' || repeat('z', 3000) || '2', 'cd1233')::bson);
SELECT documentdb_api.insert_one('comp_db', 'query_orderby', FORMAT('{ "_id": 11, "a": "%s", "c": "%s" }', 'abcde' || repeat('z', 3000) || '1', 'cd1234')::bson);
SELECT documentdb_api.insert_one('comp_db', 'query_orderby', FORMAT('{ "_id": 9, "a": "%s", "c": "%s" }', 'abcde' || repeat('z', 3000) || '3', 'cd1235')::bson);
SELECT documentdb_api.insert_one('comp_db', 'query_orderby', FORMAT('{ "_id": 12, "a": "%s", "c": "%s" }', 'abcde', 'cd1232'  || repeat('z', 3000) || '1')::bson);
SELECT documentdb_api.insert_one('comp_db', 'query_orderby', FORMAT('{ "_id": 13, "a": "%s", "c": "%s" }', 'abcde', 'cd1232')::bson);

SELECT document FROM bson_aggregation_find('comp_db', '{ "find": "query_orderby", "projection": { "_id": 1 }, "filter": { "a": { "$regex": "^abc" }}, "sort": { "a": 1 } }');
SELECT document FROM bson_aggregation_find('comp_db', '{ "find": "query_orderby", "projection": { "_id": 1 }, "filter": { "a": { "$regex": "^abc" }}, "sort": { "a": -1 } }');

-- validate sort works after an array is inserted
SELECT documentdb_api.insert_one('comp_db', 'query_orderby', '{ "_id": 8, "a": [ 1, 2, 3 ], "c": 2 }');
SELECT document FROM bson_aggregation_find('comp_db', '{ "find": "query_orderby", "filter": { "a": { "$gt": -100 }}, "sort": { "a": 1 } }');

-- validate that we use disjoint sets more efficiently
SELECT documentdb_api_internal.create_indexes_non_concurrently('comp_db', '{ "createIndexes": "query_orderby_perf_arr", "indexes": [ { "key": { "a": 1, "_id": 1 }, "enableCompositeTerm": true, "name": "a_1_id_1" }] }', true);

-- insert an array to make this a multi-key index
SELECT documentdb_api.insert_one('comp_db', 'query_orderby_perf_arr', '{ "_id": 0, "a": [ true, false ] }');

-- now insert documents 1 - 100
SELECT COUNT(documentdb_api.insert_one('comp_db', 'query_orderby_perf_arr', FORMAT('{ "_id": %s, "a": [ %s, %s ] }', i, i, 100 - i)::bson)) FROM generate_series(1, 100) AS i;

-- now query the index with an order by
set documentdb.forceDisableSeqScan to on;
ANALYZE documentdb_data.documents_68002;

-- should not use the index for sort (since it's a multi-key index)
EXPLAIN (ANALYZE ON, COSTS OFF, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_find('comp_db', '{ "find": "query_orderby_perf_arr", "filter": { "a": { "$lt": 3 }}, "sort": { "a": 1 } }');

-- now check the performance
SELECT document FROM bson_aggregation_find('comp_db', '{ "find": "query_orderby_perf_arr", "filter": { "a": { "$in": [ 3, 49, 90 ] }}, "sort": { "a": 1 } }');

-- scans 1 loop for checking multi-key - then scans 4 loops for entries 0, 1, 2, 3.
SELECT document FROM bson_aggregation_find('comp_db', '{ "find": "query_orderby_perf_arr", "filter": { "a": { "$lt": 3 }}, "sort": { "a": 1 } }');

-- scans 1 loop for checking multi-key - then scans 5 loops for entries 96, 97, 98, 99, 100.
SELECT document FROM bson_aggregation_find('comp_db', '{ "find": "query_orderby_perf_arr", "filter": { "a": { "$gt": 96 }}, "sort": { "a": 1 } }');

-- only pushes filter down
SELECT document FROM bson_aggregation_find('comp_db', '{ "find": "query_orderby_perf_arr", "filter": { "a": { "$lt": 3, "$gt": 96 }}, "sort": { "a": 1 } }');

set documentdb.enableExtendedExplainPlans to on;
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_find('comp_db', '{ "find": "query_orderby_perf_arr", "filter": { "a": { "$in": [ 3, 49, 90 ] }}, "sort": { "a": 1 } }');

-- scans 1 loop for checking multi-key - then scans 4 loops for entries 0, 1, 2, 3.
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_find('comp_db', '{ "find": "query_orderby_perf_arr", "filter": { "a": { "$lt": 3 }}, "sort": { "a": 1 } }');

-- scans 1 loop for checking multi-key - then scans 5 loops for entries 96, 97, 98, 99, 100.
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_find('comp_db', '{ "find": "query_orderby_perf_arr", "filter": { "a": { "$gt": 96 }}, "sort": { "a": 1 } }');

-- only pushes filter down
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_find('comp_db', '{ "find": "query_orderby_perf_arr", "filter": { "a": { "$lt": 3, "$gt": 96 }}, "sort": { "a": 1 } }');

-- test the same for non multi-key index
---------------------------------------------------------------------------------
-- validate that we use disjoint sets more efficiently
SELECT documentdb_api_internal.create_indexes_non_concurrently('comp_db', '{ "createIndexes": "query_orderby_perf", "indexes": [ { "key": { "a": 1, "_id": 1 }, "enableCompositeTerm": true, "name": "a_1" }] }', true);

-- now insert documents 1 - 100
SELECT COUNT(documentdb_api.insert_one('comp_db', 'query_orderby_perf', FORMAT('{ "_id": %s, "a": %s }', i, i)::bson)) FROM generate_series(1, 100) AS i;

-- now query the index with an order by
set documentdb.forceDisableSeqScan to on;
ANALYZE documentdb_data.documents_68003;

-- should use the index for sort
EXPLAIN (ANALYZE ON, COSTS OFF, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_find('comp_db', '{ "find": "query_orderby_perf", "filter": { "a": { "$lt": 3 }}, "sort": { "a": 1 } }');
EXPLAIN (ANALYZE ON, COSTS OFF, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_find('comp_db', '{ "find": "query_orderby_perf", "filter": { "a": { "$lt": 3 }}, "sort": { "a": -1 } }');

-- now check correctness.
SELECT document FROM bson_aggregation_find('comp_db', '{ "find": "query_orderby_perf", "filter": { "a": { "$in": [ 3, 49, 90 ] }}, "sort": { "a": 1 } }');
SELECT document FROM bson_aggregation_find('comp_db', '{ "find": "query_orderby_perf", "filter": { "a": { "$in": [ 3, 49, 90 ] }}, "sort": { "a": -1 } }');

SELECT document FROM bson_aggregation_find('comp_db', '{ "find": "query_orderby_perf", "filter": { "a": { "$lt": 3 }}, "sort": { "a": 1 } }');
SELECT document FROM bson_aggregation_find('comp_db', '{ "find": "query_orderby_perf", "filter": { "a": { "$lt": 3 }}, "sort": { "a": -1 } }');

SELECT document FROM bson_aggregation_find('comp_db', '{ "find": "query_orderby_perf", "filter": { "a": { "$gt": 96 }}, "sort": { "a": 1 } }');
SELECT document FROM bson_aggregation_find('comp_db', '{ "find": "query_orderby_perf", "filter": { "a": { "$gt": 96 }}, "sort": { "a": -1 } }');

SELECT document FROM bson_aggregation_find('comp_db', '{ "find": "query_orderby_perf", "filter": { "a": { "$lt": 94, "$gt": 90 }}, "sort": { "a": 1 } }');
SELECT document FROM bson_aggregation_find('comp_db', '{ "find": "query_orderby_perf", "filter": { "a": { "$lt": 94, "$gt": 90 }}, "sort": { "a": -1 } }');

SELECT document FROM bson_aggregation_find('comp_db', '{ "find": "query_orderby_perf", "filter": { "a": { "$lt": 3, "$gt": 96 }}, "sort": { "a": 1 } }');
SELECT document FROM bson_aggregation_find('comp_db', '{ "find": "query_orderby_perf", "filter": { "a": { "$lt": 3, "$gt": 96 }}, "sort": { "a": -1 } }');

-- now check the performance
EXPLAIN (ANALYZE ON, COSTS OFF, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_find('comp_db', '{ "find": "query_orderby_perf", "filter": { "a": { "$in": [ 3, 49, 90 ] }}, "sort": { "a": 1 } }');
EXPLAIN (ANALYZE ON, COSTS OFF, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_find('comp_db', '{ "find": "query_orderby_perf", "filter": { "a": { "$in": [ 3, 49, 90 ] }}, "sort": { "a": -1 } }');

-- scans 1 loop for checking multi-key - then scans 4 loops for entries 0, 1, 2, 3.
EXPLAIN (ANALYZE ON, COSTS OFF, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_find('comp_db', '{ "find": "query_orderby_perf", "filter": { "a": { "$lt": 3 }}, "sort": { "a": 1 } }');
EXPLAIN (ANALYZE ON, COSTS OFF, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_find('comp_db', '{ "find": "query_orderby_perf", "filter": { "a": { "$lt": 3 }}, "sort": { "a": -1 } }');

-- scans 1 loop for checking multi-key - then scans 5 loops for entries 96, 97, 98, 99, 100.
EXPLAIN (ANALYZE ON, COSTS OFF, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_find('comp_db', '{ "find": "query_orderby_perf", "filter": { "a": { "$gt": 96 }}, "sort": { "a": 1 } }');
EXPLAIN (ANALYZE ON, COSTS OFF, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_find('comp_db', '{ "find": "query_orderby_perf", "filter": { "a": { "$gt": 96 }}, "sort": { "a": -1 } }');

-- only scans th relevant entries
EXPLAIN (ANALYZE ON, COSTS OFF, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_find('comp_db', '{ "find": "query_orderby_perf", "filter": { "a": { "$lt": 94, "$gt": 90 }}, "sort": { "a": 1 } }');
EXPLAIN (ANALYZE ON, COSTS OFF, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_find('comp_db', '{ "find": "query_orderby_perf", "filter": { "a": { "$lt": 94, "$gt": 90 }}, "sort": { "a": -1 } }');

-- scans only > 96 and stops.
EXPLAIN (ANALYZE ON, COSTS OFF, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_find('comp_db', '{ "find": "query_orderby_perf", "filter": { "a": { "$lt": 3, "$gt": 96 }}, "sort": { "a": 1 } }');
EXPLAIN (ANALYZE ON, COSTS OFF, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_find('comp_db', '{ "find": "query_orderby_perf", "filter": { "a": { "$lt": 3, "$gt": 96 }}, "sort": { "a": -1 } }');


-- groupby pushdown works for non-multi-key indexes
EXPLAIN (ANALYZE ON, COSTS OFF, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_pipeline('comp_db', '{ "aggregate": "query_orderby_perf", "pipeline": [ { "$match": { "a": { "$exists": true } } }, { "$group": { "_id": "$a", "c": { "$count": 1 } } } ] }');

set enable_bitmapscan to off;
EXPLAIN (ANALYZE ON, COSTS OFF, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_pipeline('comp_db', '{ "aggregate": "query_orderby_perf", "pipeline": [ { "$group": { "_id": "$a", "c": { "$count": 1 } } } ] }');

-- the same does not work on multi-key indexes
EXPLAIN (ANALYZE ON, COSTS OFF, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_pipeline('comp_db', '{ "aggregate": "query_orderby_perf_arr", "pipeline": [ { "$match": { "a": { "$exists": true } } }, { "$group": { "_id": "$a", "c": { "$count": 1 } } } ] }');
EXPLAIN (ANALYZE ON, COSTS OFF, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_pipeline('comp_db', '{ "aggregate": "query_orderby_perf_arr", "pipeline": [ { "$group": { "_id": "$a", "c": { "$count": 1 } } } ] }');

set enable_bitmapscan to off;
-- for non-multi-key requires, prefix equality until the min order by key only.
-- can't push this
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF)
    SELECT document FROM bson_aggregation_pipeline('comp_db', '{ "aggregate": "query_orderby_perf", "pipeline": [ { "$match": { "a": { "$in": [ 1, 2 ] } } }, { "$sort": { "_id": 1 } } ] }');
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF)
    SELECT document FROM bson_aggregation_pipeline('comp_db', '{ "aggregate": "query_orderby_perf", "pipeline": [ { "$sort": { "_id": 1 } } ] }');

-- can push this
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF)
    SELECT document FROM bson_aggregation_pipeline('comp_db', '{ "aggregate": "query_orderby_perf", "pipeline": [ { "$match": { "a": { "$eq": 1 } } }, { "$sort": { "_id": 1 } } ] }');
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF)
    SELECT document FROM bson_aggregation_pipeline('comp_db', '{ "aggregate": "query_orderby_perf", "pipeline": [ { "$match": { "a": { "$in": [ 1, 2 ] } } }, { "$sort": { "a": 1, "_id": 1 } } ] }');
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF)
    SELECT document FROM bson_aggregation_pipeline('comp_db', '{ "aggregate": "query_orderby_perf", "pipeline": [ { "$match": { "a": { "$gte": 1 } } }, { "$sort": { "a": 1, "_id": 1 } } ] }');
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF)
    SELECT document FROM bson_aggregation_pipeline('comp_db', '{ "aggregate": "query_orderby_perf", "pipeline": [ { "$sort": { "a": 1, "_id": 1 } } ] }');

-- but not these
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF)
    SELECT document FROM bson_aggregation_pipeline('comp_db', '{ "aggregate": "query_orderby_perf", "pipeline": [ { "$match": { "a": { "$in": [ 1, 2 ] } } }, { "$sort": { "_id": 1, "a": 1 } } ] }');


-- for multi-key indexes, we can push down the order by only if there's equality until the max order by key.
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF)
    SELECT document FROM bson_aggregation_pipeline('comp_db', '{ "aggregate": "query_orderby_perf_arr", "pipeline": [ { "$match": { "a": { "$in": [ 1, 2 ] } } }, { "$sort": { "_id": 1 } } ] }');
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF)
    SELECT document FROM bson_aggregation_pipeline('comp_db', '{ "aggregate": "query_orderby_perf_arr", "pipeline": [ { "$sort": { "_id": 1 } } ] }');

-- can push this
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF)
    SELECT document FROM bson_aggregation_pipeline('comp_db', '{ "aggregate": "query_orderby_perf_arr", "pipeline": [ { "$match": { "a": { "$eq": 1 } } }, { "$sort": { "_id": 1 } } ] }');
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF)
    SELECT document FROM bson_aggregation_pipeline('comp_db', '{ "aggregate": "query_orderby_perf_arr", "pipeline": [ { "$sort": { "a": 1, "_id": 1 } } ] }');
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF)
    SELECT document FROM bson_aggregation_pipeline('comp_db', '{ "aggregate": "query_orderby_perf_arr", "pipeline": [ { "$sort": { "a": 1 } } ] }');

-- but not these
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF)
    SELECT document FROM bson_aggregation_pipeline('comp_db', '{ "aggregate": "query_orderby_perf_arr", "pipeline": [ { "$match": { "a": { "$in": [ 1, 2 ] } } }, { "$sort": { "a": 1, "_id": 1 } } ] }');
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF)
    SELECT document FROM bson_aggregation_pipeline('comp_db', '{ "aggregate": "query_orderby_perf_arr", "pipeline": [ { "$match": { "a": { "$gte": 1 } } }, { "$sort": { "a": 1, "_id": 1 } } ] }');
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF)
    SELECT document FROM bson_aggregation_pipeline('comp_db', '{ "aggregate": "query_orderby_perf_arr", "pipeline": [ { "$sort": { "_id": 1, "a": 1 } } ] }');
    


-- now insert composite rows that cause truncation and assert that it gets ordered properly.
SELECT documentdb_api_internal.create_indexes_non_concurrently('comp_db', '{ "createIndexes": "query_orderby_perf2", "indexes": [ { "key": { "b": 1, "c": 1 }, "enableCompositeTerm": true, "name": "b_1" }] }', true);

SELECT COUNT(documentdb_api.insert_one('comp_db', 'query_orderby_perf2', FORMAT('{ "_id": %s, "b": "%s", "c": %s }', i * 5 + j, repeat('aaaa', 10 * i), j)::bson)) FROM generate_series(50, 500) i JOIN generate_series(1, 5) AS j ON True;

ANALYZE documentdb_data.documents_68004;

SET documentdb.enableIndexOrderbyPushdown to on;
set documentdb.forceUseIndexIfAvailable to on;
set enable_bitmapscan to off;
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('comp_db', '{ "aggregate": "query_orderby_perf2", "pipeline": [ { "$match": { "b": { "$exists": true } } }, { "$sort": { "b": 1 } } ] }');

EXPLAIN (COSTS OFF) WITH s1 AS (SELECT document FROM bson_aggregation_pipeline('comp_db', '{ "aggregate": "query_orderby_perf2", "pipeline": [ { "$match": { "b": { "$exists": true } } }, { "$sort": { "b": 1 } } ] }')),
s2 AS (SELECT COALESCE(document -> 'b' >= (LAG(document, 1) OVER ()) -> 'b', true) AS greater_check FROM s1)
SELECT MIN(greater_check::int4), MAX(greater_check::int4) FROM s2;

EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('comp_db', '{ "aggregate": "query_orderby_perf2", "pipeline": [ { "$match": { "b": { "$exists": true } } }, { "$sort": { "b": -1 } } ] }');

EXPLAIN (COSTS OFF) WITH s1 AS (SELECT document FROM bson_aggregation_pipeline('comp_db', '{ "aggregate": "query_orderby_perf2", "pipeline": [ { "$match": { "b": { "$exists": true } } }, { "$sort": { "b": -1 } } ] }')),
s2 AS (SELECT COALESCE(document -> 'b' <= (LAG(document, 1) OVER ()) -> 'b', true) AS greater_check FROM s1)
SELECT MIN(greater_check::int4), MAX(greater_check::int4) FROM s2;

-- validate the order is correct
WITH s1 AS (SELECT document FROM bson_aggregation_pipeline('comp_db', '{ "aggregate": "query_orderby_perf2", "pipeline": [ { "$match": { "b": { "$exists": true } } }, { "$sort": { "b": 1 } } ] }')),
s2 AS (SELECT COALESCE(document -> 'b' >= (LAG(document, 1) OVER ()) -> 'b', true) AS greater_check FROM s1)
SELECT MIN(greater_check::int4), MAX(greater_check::int4) FROM s2;

WITH s1 AS (SELECT document FROM bson_aggregation_pipeline('comp_db', '{ "aggregate": "query_orderby_perf2", "pipeline": [ { "$match": { "b": { "$exists": true } } }, { "$sort": { "b": -1 } } ] }')),
s2 AS (SELECT COALESCE(document -> 'b' <= (LAG(document, 1) OVER ()) -> 'b', true) AS greater_check FROM s1)
SELECT MIN(greater_check::int4), MAX(greater_check::int4) FROM s2;


reset documentdb.forceDisableSeqScan;

-- add the complex tests that were in the composite filter order by
select * from documentdb_api.insert_one('comp_db', 'sortcoll', '{ "_id": 1, "a": { "b": 1 } }');
select * from documentdb_api.insert_one('comp_db', 'sortcoll', '{ "_id": 2, "a": { "b": null } }');
select * from documentdb_api.insert_one('comp_db', 'sortcoll', '{ "_id": 3, "a": { "b": "string value" } }');
select * from documentdb_api.insert_one('comp_db', 'sortcoll', '{ "_id": 4, "a": { "b": true } }');
select * from documentdb_api.insert_one('comp_db', 'sortcoll', '{ "_id": 5, "a": { "b": false } }');
select * from documentdb_api.insert_one('comp_db', 'sortcoll', '{ "_id": 6, "a": { "b": [] } }');
select * from documentdb_api.insert_one('comp_db', 'sortcoll', '{ "_id": 7, "a": { "b": [1, 2, 3] } }');
select * from documentdb_api.insert_one('comp_db', 'sortcoll', '{ "_id": 8, "a": { "b": [1, { "$minKey": 1 }, 3, true] } }');
select * from documentdb_api.insert_one('comp_db', 'sortcoll', '{ "_id": 9, "a": { "b": [1, { "$maxKey": 1 }, 3, true] } }');
select * from documentdb_api.insert_one('comp_db', 'sortcoll', '{ "_id": 10, "a": { "b": { "c": 1 } } }');
select * from documentdb_api.insert_one('comp_db', 'sortcoll', '{ "_id": 11, "a": { "b": { "$maxKey": 1 } } }');

-- now some more esoteric values
select * from documentdb_api.insert_one('comp_db', 'sortcoll', '{ "_id": 12, "a": null }');
select * from documentdb_api.insert_one('comp_db', 'sortcoll', '{ "_id": 13, "a": [ {} ] }');
select * from documentdb_api.insert_one('comp_db', 'sortcoll', '{ "_id": 14, "a": [ 1 ] }');
select * from documentdb_api.insert_one('comp_db', 'sortcoll', '{ "_id": 15, "a": [ 1, { "b": 3 } ] }');
select * from documentdb_api.insert_one('comp_db', 'sortcoll', '{ "_id": 16, "a": [ null, { "b": 4 } ] }');
select * from documentdb_api.insert_one('comp_db', 'sortcoll', '{ "_id": 17, "a": [ {}, { "b": 3 } ] }');
select * from documentdb_api.insert_one('comp_db', 'sortcoll', '{ "_id": 18, "a": [ { "c": 1 } ] }');
select * from documentdb_api.insert_one('comp_db', 'sortcoll', '{ "_id": 19, "a": [ { "c": 1 }, { "b": 3 } ] }');

-- baseline 
select * from documentdb_api.insert_one('comp_db', 'sortcoll', '{ "_id": 20, "a": { "b": 0 } }');
select * from documentdb_api.insert_one('comp_db', 'sortcoll', '{ "_id": 21, "a": { "b": { "$minKey": 1 } } }');

-- test ordering in the composite index
SELECT documentdb_api_internal.create_indexes_non_concurrently('comp_db', '{ "createIndexes": "sortcoll", "indexes": [ { "key": { "a.b": 1, "_id": 1 }, "enableCompositeTerm": true, "name": "a.b_1" }] }', true);

-- now sort manually (we can't rely on the index pushdown feature yet since that requires a filter)

BEGIN;
set local documentdb.forceDisableSeqScan to on;
set local documentdb.enableIndexOrderbyPushdown to on;
set local documentdb.enableExtendedExplainPlans to on;
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_pipeline('comp_db', '{ "aggregate": "sortcoll", "pipeline": [ { "$sort": { "a.b": 1 } } ] }');
ROLLBACK;


BEGIN;
set local documentdb.forceDisableSeqScan to on;
set local documentdb.enableIndexOrderbyPushdown to on;
SELECT document FROM bson_aggregation_pipeline('comp_db', '{ "aggregate": "sortcoll", "pipeline": [ { "$sort": { "a.b": 1 } } ] }');
SELECT document FROM bson_aggregation_pipeline('comp_db', '{ "aggregate": "sortcoll", "pipeline": [ { "$sort": { "a.b": -1 } } ] }');
ROLLBACK;

set documentdb.forceDisableSeqScan to on;
set documentdb.enableIndexOrderbyPushdown to on;
set documentdb.enableExtendedExplainPlans to on;

-- can't push this down (no equality prefix)
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_pipeline('comp_db', '{ "aggregate": "sortcoll", "pipeline": [ { "$sort": { "_id": 1 } } ] }');

-- can't push order by down here:
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_pipeline('comp_db', '{ "aggregate": "sortcoll", "pipeline": [ { "$match": { "a.b": { "$exists": true } } }, { "$sort": { "_id": 1 } } ] }');
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_pipeline('comp_db', '{ "aggregate": "sortcoll", "pipeline": [ { "$match": { "a.b": { "$gt": 5 } } }, { "$sort": { "_id": 1 } } ] }');

-- but this is okay
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_pipeline('comp_db', '{ "aggregate": "sortcoll", "pipeline": [ { "$match": { "a.b": { "$eq": 1 } } }, { "$sort": { "_id": 1 } } ] }');

-- this one isn't okay since we need to resort across a.b
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_pipeline('comp_db', '{ "aggregate": "sortcoll", "pipeline": [ { "$match": { "a.b": { "$in": [ 1, 2 ] } } }, { "$sort": { "_id": 1 } } ] }');

-- but this is fine:
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_pipeline('comp_db', '{ "aggregate": "sortcoll", "pipeline": [ { "$match": { "a.b": { "$in": [ 1 ] } } }, { "$sort": { "_id": 1 } } ] }');

-- allows incremental sorting
set enable_sort to off;
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_pipeline('comp_db', '{ "aggregate": "sortcoll", "pipeline": [ { "$match": { "a.b": { "$eq": 1 } } }, { "$sort": { "_id": 1, "c": 1 } } ] }');

-- forced order by pushdown respects ordering when there is none.
set documentdb_rum.forceRumOrderedIndexScan to on;
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_pipeline('comp_db', '{ "aggregate": "sortcoll", "pipeline": [ { "$match": { "a.b": { "$exists": true } } } ] }');
SELECT document FROM bson_aggregation_pipeline('comp_db', '{ "aggregate": "sortcoll", "pipeline": [ { "$match": { "a.b": { "$exists": true } } } ] }');


-- test order by with primary key _id and composite index on _id with _id filter & order
reset documentdb.forceDisableSeqScan;
select COUNT(documentdb_api.insert_one('comp_db', 'idIndexOrder', FORMAT('{ "_id": %s, "a": %s }', i , i)::bson)) FROM generate_series(1, 100) AS i;
SELECT documentdb_api_internal.create_indexes_non_concurrently('comp_db', '{ "createIndexes": "idIndexOrder", "indexes": [ { "key": { "_id": 1 }, "enableCompositeTerm": true, "name": "id_2" }] }', true);

-- now validate pushdown
set documentdb.forceDisableSeqScan to on;
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_pipeline('comp_db', '{ "aggregate": "idIndexOrder", "pipeline": [ { "$match": { "_id": { "$gte": 10, "$lte": 25 } } }, { "$sort": { "_id": 1 } }, { "$skip": 5 }, { "$limit": 5 } ] }');
SELECT document FROM bson_aggregation_pipeline('comp_db', '{ "aggregate": "idIndexOrder", "pipeline": [ { "$match": { "_id": { "$gte": 10, "$lte": 25 } } }, { "$sort": { "_id": 1 } }, { "$skip": 5 }, { "$limit": 5 } ] }');

-- now repeat sort for nulls with 3 dotted paths

set documentdb.forceDisableSeqScan to off;
SELECT * FROM documentdb_api.insert_one('comp_db', 'sortcoll3', '{ "_id": 1, "a": { "b": { "c": 1 } } }');
SELECT * FROM documentdb_api.insert_one('comp_db', 'sortcoll3', '{ "_id": 2, "a": { "b": [ { "c": 2 } ] } }');
SELECT * FROM documentdb_api.insert_one('comp_db', 'sortcoll3', '{ "_id": 3, "a": [ { "b": [ { "c": 1 } ] } ] }');

-- combinations of those paths going missing
SELECT * FROM documentdb_api.insert_one('comp_db', 'sortcoll3', '{ "_id": 4, "a": { "b": { "d": 1 } } }');
SELECT * FROM documentdb_api.insert_one('comp_db', 'sortcoll3', '{ "_id": 5, "a": { "b": [ { "c": 2 }, {} ] } }');
SELECT * FROM documentdb_api.insert_one('comp_db', 'sortcoll3', '{ "_id": 6, "a": { "b": [ { "c": 2 }, 2 ] } }');
SELECT * FROM documentdb_api.insert_one('comp_db', 'sortcoll3', '{ "_id": 7, "a": { "b": [ 2 ] } }');
SELECT * FROM documentdb_api.insert_one('comp_db', 'sortcoll3', '{ "_id": 8, "a": { "b": [ {} ] } }');

SELECT * FROM documentdb_api.insert_one('comp_db', 'sortcoll3', '{ "_id": 9, "a": [ { "b": { "c": 3 } }, { "b": { "d": 1 } } ] }');
SELECT * FROM documentdb_api.insert_one('comp_db', 'sortcoll3', '{ "_id": 10, "a": [ { "b": { "c": 3 } }, { "b": 2 } ] }');
SELECT * FROM documentdb_api.insert_one('comp_db', 'sortcoll3', '{ "_id": 11, "a": [ { "b": { "c": 3 } }, {  } ] }');
SELECT * FROM documentdb_api.insert_one('comp_db', 'sortcoll3', '{ "_id": 12, "a": [ { "b": { "c": 3 } }, 1 ] }');

-- baseline
SELECT * FROM documentdb_api.insert_one('comp_db', 'sortcoll3', '{ "_id": 13, "a": { "b": { "c": 0 } } }');

set documentdb.forceDisableSeqScan to on;
SELECT documentdb_api_internal.create_indexes_non_concurrently('comp_db', '{ "createIndexes": "sortcoll3", "indexes": [ { "key": { "a.b.c": 1, "_id": 1 }, "enableCompositeTerm": true, "name": "a.b.c_1" }] }', true);
SELECT * FROM bson_aggregation_find('comp_db', '{ "find": "sortcoll3", "filter": {}, "sort": { "a.b.c": 1, "_id": 1 } }');


-- with forced order by scans, recheck of the index on the runtime needs to happen.
reset documentdb.forceDisableSeqScan;

SELECT documentdb_api.insert_one('comp_db', 'large_keys', FORMAT('{ "_id": 1, "a": "%s" }', repeat('a', 9999) || 'bb')::bson);
SELECT documentdb_api.insert_one('comp_db', 'large_keys', FORMAT('{ "_id": 2, "a": "%s" }', repeat('a', 9999) || 'cb')::bson);
SELECT documentdb_api.insert_one('comp_db', 'large_keys', FORMAT('{ "_id": 3, "a": "%s" }', repeat('a', 9999) || 'ab')::bson);

-- 4 does not match
SELECT documentdb_api.insert_one('comp_db', 'large_keys', FORMAT('{ "_id": 4, "a": "%s" }', repeat('a', 9999) || 'ac')::bson);
SELECT documentdb_api_internal.create_indexes_non_concurrently('comp_db', '{ "createIndexes": "large_keys", "indexes": [ { "key": { "a": 1 }, "enableCompositeTerm": true, "name": "a_2" }] }', true);


set documentdb.forceDisableSeqScan to on;
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_pipeline('comp_db', '{ "aggregate": "large_keys", "pipeline": [ { "$match": { "a": { "$regex": ".+b$" } } }, { "$sort": { "a": 1 } }, { "$project": { "_id": 1 } } ] }');
SELECT document FROM bson_aggregation_pipeline('comp_db', '{ "aggregate": "large_keys", "pipeline": [ { "$match": { "a": { "$regex": ".+b$" } } }, { "$sort": { "a": 1 } }, { "$project": { "_id": 1 } } ] }');

reset documentdb.forceDisableSeqScan;
SELECT documentdb_api_internal.create_indexes_non_concurrently('comp_db', '{ "createIndexes": "sortcoll3", "indexes": [ { "key": { "a.b.c": -1, "_id": -1 }, "enableCompositeTerm": true, "name": "a.b.c_-1" }] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('comp_db', '{ "createIndexes": "sortcoll", "indexes": [ { "key": { "a.b": -1, "_id": -1 }, "enableCompositeTerm": true, "name": "a.b_-1" }] }', true);

set documentdb.forceDisableSeqScan to on;
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF) SELECT * FROM bson_aggregation_find('comp_db', '{ "find": "sortcoll3", "filter": {}, "sort": { "a.b.c": -1, "_id": -1 } }');
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF) SELECT * FROM bson_aggregation_find('comp_db', '{ "find": "sortcoll", "filter": {}, "sort": { "a.b": -1, "_id": -1 } }');

SELECT * FROM bson_aggregation_find('comp_db', '{ "find": "sortcoll3", "filter": {}, "sort": { "a.b.c": -1, "_id": -1 } }');
SELECT * FROM bson_aggregation_find('comp_db', '{ "find": "sortcoll", "filter": {}, "sort": { "a.b": -1, "_id": -1 } }');

-- partial sort pushdown
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF) SELECT * FROM bson_aggregation_find('comp_db', '{ "find": "sortcoll3", "filter": {}, "sort": { "a.b.c": -1, "_id": 1 } }');
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF) SELECT * FROM bson_aggregation_find('comp_db', '{ "find": "sortcoll", "filter": {}, "sort": { "a.b": -1, "_id": 1 } }');

-- mixed asc/desc indexes
reset documentdb.forceDisableSeqScan;
CALL documentdb_api.drop_indexes('comp_db', '{ "dropIndexes": "sortcoll3", "index": "a.b.c_-1" }');
CALL documentdb_api.drop_indexes('comp_db', '{ "dropIndexes": "sortcoll", "index": "a.b_-1" }');

-- recreate with mixed asc/desc
SELECT documentdb_api_internal.create_indexes_non_concurrently('comp_db', '{ "createIndexes": "sortcoll3", "indexes": [ { "key": { "a.b.c": 1, "_id": -1 }, "enableCompositeTerm": true, "name": "a.b.c_1_id-1" }] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('comp_db', '{ "createIndexes": "sortcoll", "indexes": [ { "key": { "a.b": -1, "_id": 1 }, "enableCompositeTerm": true, "name": "a.b_-1_id_1" }] }', true);

set documentdb.forceDisableSeqScan to on;
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF) SELECT * FROM bson_aggregation_find('comp_db', '{ "find": "sortcoll3", "filter": {}, "sort": { "a.b.c": 1, "_id": -1 } }');
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF) SELECT * FROM bson_aggregation_find('comp_db', '{ "find": "sortcoll", "filter": {}, "sort": { "a.b": -1, "_id": 1 } }');

SELECT * FROM bson_aggregation_find('comp_db', '{ "find": "sortcoll3", "filter": {}, "sort": { "a.b.c": 1, "_id": -1 } }');
SELECT * FROM bson_aggregation_find('comp_db', '{ "find": "sortcoll", "filter": {}, "sort": { "a.b": -1, "_id": 1 } }');


-- test order by functionality with group
reset documentdb.forceDisableSeqScan;
SELECT documentdb_api_internal.create_indexes_non_concurrently('comp_db', '{ "createIndexes": "ordering_groups", "indexes": [ { "key": { "a": 1 }, "enableOrderedIndex": true, "name": "a_1" }] }', true);

-- a is either 0 or 1 or 2
select COUNT(documentdb_api.insert_one('comp_db', 'ordering_groups', FORMAT('{ "_id": %s, "a": %s }', i , i % 3)::bson)) FROM generate_series(1, 100) AS i;
ANALYZE documentdb_data.documents_68009;

set documentdb.forceDisableSeqScan to on;
set documentdb.enableIndexOrderbyPushdown to on;
EXPLAIN (ANALYZE ON, COSTS OFF, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_pipeline('comp_db', '{ "aggregate": "ordering_groups", "pipeline": [ { "$group": { "_id": "$a", "c": { "$count": 1 } } } ] }');
SELECT document FROM bson_aggregation_pipeline('comp_db', '{ "aggregate": "ordering_groups", "pipeline": [ { "$group": { "_id": "$a", "c": { "$count": 1 } } } ] }');


-- sorting on prefix with missing path is not allowed unless it's equality.
SELECT documentdb_api_internal.create_indexes_non_concurrently('comp_db', '{ "createIndexes": "compOrderSkip", "indexes": [ { "key": { "a": 1, "b": 1, "c": 1, "d": 1}, "name": "idx1", "enableOrderedIndex": true } ] }');
select COUNT(documentdb_api.insert_one('comp_db', 'compOrderSkip', FORMAT('{ "_id": %s, "a": %s, "b": %s, "c": %s, "d": %s }', i , i, i % 5, i % 10, i % 20 )::bson)) FROM generate_series(1, 100) AS i;

-- now given that it's not multi-key, we can push down sorts to the index fully *iff* missing fields are equality.
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF) SELECT * FROM bson_aggregation_find('comp_db', '{ "find": "compOrderSkip", "filter": { "a": { "$in": [ 1, 2 ] }, "b": { "$in": [ 2, 3 ] }, "c": 2 }, "sort": { "a": 1, "b": 1, "d": 1 } }');

-- cannot push non equality in the non-sorted prefix
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF) SELECT * FROM bson_aggregation_find('comp_db', '{ "find": "compOrderSkip", "filter": { "a": { "$in": [ 1, 2 ] }, "b": { "$in": [ 2, 3 ] }, "c": { "$in": [ 5, 6 ]} }, "sort": { "a": 1, "b": 1, "d": 1 } }');

-- once it's multi-key this isn't allowed.
SELECT documentdb_api.insert_one('comp_db', 'compOrderSkip', FORMAT('{ "_id": %s, "a": [ %s, 2, 3 ], "b": %s, "c": %s, "d": %s }', 200, 201, 202, 203, 204 )::bson);
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF) SELECT * FROM bson_aggregation_find('comp_db', '{ "find": "compOrderSkip", "filter": { "a": { "$in": [ 1, 2 ] }, "b": { "$in": [ 2, 3 ] }, "c": 2 }, "sort": { "a": 1, "b": 1, "d": 1 } }');


--composite index selection with order by.
SELECT documentdb_api_internal.create_indexes_non_concurrently('comp_db', '{ "createIndexes": "index_orderby_selection", "indexes": [ { "key": { "a": 1, "b": 1, "c": 1, "d": 1 }, "enableOrderedIndex": true, "name": "a_b_c_d_1" }] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('comp_db', '{ "createIndexes": "index_orderby_selection", "indexes": [ { "key": { "a": 1, "b": 1, "d": 1 }, "enableOrderedIndex": true, "name": "a_b_d_1" }] }', true);

-- insert 100 docs
select COUNT(documentdb_api.insert_one('comp_db', 'index_orderby_selection', FORMAT('{ "_id": %s, "a": %s, "b": %s, "c": %s, "d": %s }', i, i % 3, i % 10, i % 100, i )::bson)) FROM generate_series(1, 100) AS i;
ANALYZE documentdb_data.documents_68010;

-- order by should use the order by filter index (a_b_c_1)
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF) SELECT * FROM bson_aggregation_find('comp_db', '{ "find": "index_orderby_selection", "filter": { "a": { "$in": [ 1, 2 ] }, "b": { "$in": [ 2, 3 ] }, "d": 10 }, "sort": { "a": 1, "b": 1, "c": 1 } }');

-- if we're querying just filters, use a_b_d since it's better
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF) SELECT * FROM bson_aggregation_find('comp_db', '{ "find": "index_orderby_selection", "filter": { "a": { "$in": [ 1, 2 ] }, "b": { "$in": [ 2, 3 ] }, "d": 10 } }');

EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF) SELECT * FROM bson_aggregation_find('comp_db', '{ "find": "index_orderby_selection", "filter": { "a": { "$in": [ 1, 2 ] }, "b": { "$in": [ 2, 3 ] }, "d": 10 }, "sort": { "a": 1, "b": 1, "c": 1 } }');

-- the same should work if the indexes were created in the reverse order
CALL documentdb_api.drop_indexes('comp_db', '{ "dropIndexes": "index_orderby_selection", "index": "a_b_c_d_1" }');
CALL documentdb_api.drop_indexes('comp_db', '{ "dropIndexes": "index_orderby_selection", "index": "a_b_d_1" }');

SELECT documentdb_api_internal.create_indexes_non_concurrently('comp_db', '{ "createIndexes": "index_orderby_selection", "indexes": [ { "key": { "a": 1, "b": 1, "d": 1 }, "enableOrderedIndex": true, "name": "a_b_d_1" }] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('comp_db', '{ "createIndexes": "index_orderby_selection", "indexes": [ { "key": { "a": 1, "b": 1, "c": 1, "d": 1 }, "enableOrderedIndex": true, "name": "a_b_c_d_1" }] }', true);

-- order by should use the order by filter index (a_b_c_1)
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF) SELECT * FROM bson_aggregation_find('comp_db', '{ "find": "index_orderby_selection", "filter": { "a": { "$in": [ 1, 2 ] }, "b": { "$in": [ 2, 3 ] }, "d": 10 }, "sort": { "a": 1, "b": 1, "c": 1 } }');

-- if we're querying just filters, use a_b_d since it's better
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF) SELECT * FROM bson_aggregation_find('comp_db', '{ "find": "index_orderby_selection", "filter": { "a": { "$in": [ 1, 2 ] }, "b": { "$in": [ 2, 3 ] }, "d": 10 } }');


-- order by backward scan with unique index
SELECT documentdb_api_internal.create_indexes_non_concurrently('comp_db', '{ "createIndexes": "unique_sort", "indexes": [ { "key": { "a": 1, "b": 1 }, "enableOrderedIndex": true, "name": "a_b_1", "unique": true }] }', true);
select COUNT(documentdb_api.insert_one('comp_db', 'unique_sort', FORMAT('{ "_id": %s, "a": %s, "b": %s, "c": %s, "d": %s }', i, i, i, i, i )::bson)) FROM generate_series(1, 100) AS i;
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_find('comp_db', '{ "find": "unique_sort", "sort": { "a": -1, "b": -1 }}');


-- test index selectivity for orderby vs filters.
CALL documentdb_api.drop_indexes('comp_db', '{ "dropIndexes": "index_orderby_selection", "index": "a_b_c_d_1" }');
CALL documentdb_api.drop_indexes('comp_db', '{ "dropIndexes": "index_orderby_selection", "index": "a_b_d_1" }');
TRUNCATE documentdb_data.documents_68011;
ANALYZE documentdb_data.documents_68011;

-- now create 2 types of indexes: One that only matches the order by and one that matches the order and filters
SELECT documentdb_api_internal.create_indexes_non_concurrently('comp_db', '{ "createIndexes": "index_orderby_selection", "indexes": [ { "key": { "orderKey": 1, "otherPath": 1 }, "enableOrderedIndex": true, "name": "sortIndex_1" }] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('comp_db', '{ "createIndexes": "index_orderby_selection", "indexes": [ { "key": { "filter1": 1, "filter2": 1, "orderKey": 1, "filter3": 1 }, "enableOrderedIndex": true, "name": "filterSortIndex_1" }] }', true);

SELECT COUNT(*) FROM ( SELECT documentdb_api.insert_one('comp_db', 'index_orderby_selection',
    FORMAT('{ "_id": %s, "filter1": "filter1-%s", "filter2": "filter2-%s", "filter3": %s, "orderKey": %s, "otherPath": "somePath-%s" }', i, i % 10, i, i % 100, i, i)::bson) FROM generate_series(1, 10000) i) j;

set documentdb.enableIndexOrderbyPushdown to on;
reset enable_sort;

-- this has all the paths matching.
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF)
    SELECT * FROM bson_aggregation_find('comp_db', '{ "find": "index_orderby_selection", "filter": { "filter1": "filter1-5", "filter2": "filter2-55", "filter3": { "$gt": 50 } }, "sort": { "orderKey": 1 }, "limit": 10 }');

-- this one can't push the order by but should prefer the filter.
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF)
    SELECT * FROM bson_aggregation_find('comp_db', '{ "find": "index_orderby_selection", "filter": { "filter1": "filter1-5", "filter2": { "$gte": "filter2-55" }, "filter3": { "$gt": 50 } }, "sort": { "orderKey": 1 }, "limit": 10 }');

SELECT documentdb_api.coll_mod('comp_db', 'index_orderby_selection', '{ "collMod": "index_orderby_selection", "index": { "name": "filterSortIndex_1", "hidden": true }}');

-- now it picks the sort index
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF)
    SELECT * FROM bson_aggregation_find('comp_db', '{ "find": "index_orderby_selection", "filter": { "filter1": "filter1-5", "filter2": { "$gte": "filter2-55" }, "filter3": { "$gt": 50 } }, "sort": { "orderKey": 1 }, "limit": 10 }');
