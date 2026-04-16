SET search_path TO documentdb_api_catalog, documentdb_core, public;
SET documentdb.next_collection_id TO 900;
SET documentdb.next_collection_index_id TO 900;

SELECT documentdb_api.drop_collection('p_ixscan', 'parallel_scan');
SELECT documentdb_api.create_collection('p_ixscan', 'parallel_scan');

SELECT collection_id AS p_col FROM documentdb_api_catalog.collections WHERE database_name = 'p_ixscan' AND collection_name = 'parallel_scan' \gset

-- disable autovacuum to have predicatability
SELECT FORMAT('ALTER TABLE documentdb_data.documents_%s set (autovacuum_enabled = off, parallel_workers = 2)', :p_col) \gexec


SELECT COUNT(documentdb_api.insert_one('p_ixscan', 'parallel_scan',  FORMAT('{ "_id": %s, "a": %s, "b": %s }', i, i, i)::bson)) FROM generate_series(1, 1000) AS i;

SELECT documentdb_api_internal.create_indexes_non_concurrently(
    'p_ixscan',
    '{ "createIndexes": "parallel_scan", "indexes": [ { "key": { "a": 1 }, "name": "a_1", "enableCompositeTerm": true } ] }', TRUE);
SELECT documentdb_api_internal.create_indexes_non_concurrently(
    'p_ixscan',
    '{ "createIndexes": "parallel_scan", "indexes": [ { "key": { "b": 1 }, "name": "b_1", "enableCompositeTerm": false } ] }', TRUE);

set documentdb.enableExtendedExplainPlans to on;
set documentdb.enableIndexOrderbyPushdown to on;
set enable_bitmapscan to off;
SELECT documentdb_test_helpers.run_explain_and_trim(
    $cmd$ EXPLAIN (COSTS OFF, ANALYZE ON, VERBOSE OFF, BUFFERS OFF, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_find('p_ixscan',
        '{ "find": "parallel_scan", "filter": { "a": { "$gt": 10 } } }') $cmd$);

set parallel_tuple_cost to 0;
set parallel_setup_cost to 0;
set documentdb.enableCompositeParallelIndexScan to on;
set documentdb.enableIndexOrderbyPushdown to on;
set parallel_leader_participation to off;
set enable_seqscan to off;
set documentdb.forceParallelScanIfAvailable to on;
SELECT documentdb_test_helpers.run_explain_and_trim(
    $cmd$ EXPLAIN (COSTS OFF, ANALYZE ON, VERBOSE OFF, BUFFERS OFF, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_find('p_ixscan',
        '{ "find": "parallel_scan", "filter": { "a": { "$gt": 10, "$lt": 50 } } }') $cmd$);

SELECT documentdb_test_helpers.run_explain_and_trim(
    $cmd$ EXPLAIN (COSTS OFF, ANALYZE ON, VERBOSE OFF, BUFFERS OFF, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_find('p_ixscan',
        '{ "find": "parallel_scan", "filter": { "a": { "$gt": 10, "$lt": 50 } }, "sort": { "a": -1 } }') $cmd$);

SELECT documentdb_test_helpers.run_explain_and_trim(
    $cmd$ EXPLAIN (COSTS OFF, ANALYZE ON, VERBOSE OFF, BUFFERS OFF, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_find('p_ixscan',
        '{ "find": "parallel_scan", "filter": { "a": { "$gt": 10, "$lt": 50 } }, "sort": { "a": 1 } }') $cmd$);

SELECT documentdb_test_helpers.run_explain_and_trim(
    $cmd$ EXPLAIN (COSTS OFF, ANALYZE ON, VERBOSE OFF, BUFFERS OFF, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_find('p_ixscan',
        '{ "find": "parallel_scan", "sort": { "a": 1 } }') $cmd$);

SELECT documentdb_test_helpers.run_explain_and_trim(
    $cmd$ EXPLAIN (COSTS OFF, ANALYZE ON, VERBOSE OFF, BUFFERS OFF, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_find('p_ixscan',
        '{ "find": "parallel_scan", "filter": { "b": { "$gt": 10, "$lt": 50 } } }') $cmd$);

SELECT documentdb_test_helpers.run_explain_and_trim(
    $cmd$ EXPLAIN (COSTS OFF, ANALYZE ON, VERBOSE OFF, BUFFERS OFF, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_find('p_ixscan',
        '{ "find": "parallel_scan", "filter": { "b": { "$gt": 10, "$lt": 50 } }, "sort": { "b": -1 } }') $cmd$);

SELECT documentdb_test_helpers.run_explain_and_trim(
    $cmd$ EXPLAIN (COSTS OFF, ANALYZE ON, VERBOSE OFF, BUFFERS OFF, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_find('p_ixscan',
        '{ "find": "parallel_scan", "filter": { "b": { "$gt": 10, "$lt": 50 } }, "sort": { "b": 1 } }') $cmd$);